#include "pager_internal.h"

static char as_comm_buf[256];

void move_client_to_desk (ASWindowData * wd, int desk)
{
	sprintf (as_comm_buf, "WindowsDesk %d", desk);
	SendInfo (as_comm_buf, wd->client);
}

ASPagerDesk *translate_client_pos_main (int x, int y, unsigned int width,
																				unsigned int height, int desk,
																				int *ret_x, int *ret_y)
{
	ASPagerDesk *d = NULL;
	if (x + width >= PagerState.main_canvas->root_x
			&& y + height >= PagerState.main_canvas->root_y
			&& x < PagerState.main_canvas->width + PagerState.main_canvas->root_x
			&& y <
			PagerState.main_canvas->height + PagerState.main_canvas->root_y) {
		int i = PagerState.desks_num;
		if ((d = get_pager_desk (desk)) != NULL) {
			if (d->desk_canvas->root_x + d->desk_canvas->bw > x + width ||
					d->desk_canvas->root_x + d->desk_canvas->bw +
					d->desk_canvas->width <= x
					|| d->desk_canvas->root_y + d->desk_canvas->bw > y + height
					|| d->desk_canvas->root_y + d->desk_canvas->bw +
					d->desk_canvas->height <= y) {
				d = NULL;
			}
		}

		while (--i >= 0 && d == NULL) {
			d = &(PagerState.desks[i]);
			LOCAL_DEBUG_OUT
					("checking desk %d: pos(%+d%+d)->desk_geom(%dx%d%+d%+d)", i, x,
					 y, d->desk_canvas->width, d->desk_canvas->height,
					 d->desk_canvas->root_x, d->desk_canvas->root_y);
			if (d->desk_canvas->root_x + d->desk_canvas->bw > x + width
					|| d->desk_canvas->root_x + d->desk_canvas->bw +
					d->desk_canvas->width <= x
					|| d->desk_canvas->root_y + d->desk_canvas->bw > y + height
					|| d->desk_canvas->root_y + d->desk_canvas->bw +
					d->desk_canvas->height <= y) {
				d = NULL;
			}
		}
	}

	if (d) {
		x -= d->background->root_x;
		y -= d->background->root_y;
		LOCAL_DEBUG_OUT
				("desk(%ld) pager_vpos(%+d%+d) vscreen_size(%dx%d) desk_size(%dx%d) ",
				 d->desk, x, y, PagerState.vscreen_width,
				 PagerState.vscreen_height, d->background->width,
				 d->background->height);
		if (d->background->width > 0)
			x = (x * PagerState.vscreen_width) / d->background->width;
		if (d->background->height > 0)
			y = (y * PagerState.vscreen_height) / d->background->height;
		*ret_x = x;
		*ret_y = y;
	} else {
		*ret_x = x;
		*ret_y = y;
	}
	return d;
}

void
translate_client_size (unsigned int width, unsigned int height,
											 unsigned int *ret_width, unsigned int *ret_height)
{
	if (PagerState.resize_desk) {
		ASPagerDesk *d = PagerState.resize_desk;
		if (d->background->width > 0) {
			width = (width * PagerState.vscreen_width) / d->background->width;
		}
		if (d->background->height > 0) {
			height =
					(height * PagerState.vscreen_height) / d->background->height;
		}
		*ret_width = width;
		*ret_height = height;
	}
}

typedef struct ASPagerMoveResizeReq {
	FunctionCode func;
	send_signed_data_type func_val[2];
	Window client;
	Bool pending;
} ASPagerMoveResizeReq;

ASPagerMoveResizeReq PagerMoveResizeReq = { 0, {0, 0}, None, False };

void exec_moveresize_req (void *data)
{
	ASPagerMoveResizeReq *req = (ASPagerMoveResizeReq *) data;
	if (req && req->pending) {
		send_signed_data_type unit_val[2] = { 1, 1 };
		SendNumCommand (req->func, NULL, &(req->func_val[0]), &(unit_val[0]),
										req->client);
		req->pending = False;
	}
}

void
schedule_moveresize_req (FunctionCode func, send_signed_data_type val1,
												 send_signed_data_type val2, Window client,
												 Bool immediate)
{
	if (PagerMoveResizeReq.pending) {
		if (PagerMoveResizeReq.client != client
				|| PagerMoveResizeReq.func != func) {
			timer_remove_by_data (&PagerMoveResizeReq);
			exec_moveresize_req (&PagerMoveResizeReq);
		} else if (immediate) {
			timer_remove_by_data (&PagerMoveResizeReq);
			PagerMoveResizeReq.pending = False;
		}
	}
	PagerMoveResizeReq.func = func;
	PagerMoveResizeReq.func_val[0] = val1;
	PagerMoveResizeReq.func_val[1] = val2;
	PagerMoveResizeReq.client = client;
	if (immediate)
		exec_moveresize_req (&PagerMoveResizeReq);
	else if (!PagerMoveResizeReq.pending) {
		PagerMoveResizeReq.pending = True;
		timer_new (40, exec_moveresize_req, &PagerMoveResizeReq);
	}
}

void apply_client_move (struct ASMoveResizeData *data)
{
	ASWindowData *wd = fetch_client (AS_WIDGET_WINDOW (data->mr));
	int real_x = 0, real_y = 0;
	ASPagerDesk *d =
			translate_client_pos_main (PagerState.main_canvas->root_x +
																 data->curr.x,
																 PagerState.main_canvas->root_y +
																 data->curr.y,
																 data->curr.width, data->curr.height,
																 wd->desk, &real_x, &real_y);
	if (d && d->desk != wd->desk) {
		move_client_to_desk (wd, d->desk);
		set_moveresize_aspect (data, PagerState.vscreen_width,
													 d->background->width, PagerState.vscreen_height,
													 d->background->height, d->background->root_x,
													 d->background->root_y);
	}
	LOCAL_DEBUG_OUT ("d(%p)->curr(%+d%+d)->real(%+d%+d)", d, data->curr.x,
									 data->curr.y, real_x, real_y);
	schedule_moveresize_req (F_MOVE, real_x, real_y, wd->client, False);
}

void complete_client_move (struct ASMoveResizeData *data, Bool cancelled)
{
	ASWindowData *wd = fetch_client (AS_WIDGET_WINDOW (data->mr));
	int real_x = 0, real_y = 0;
	ASPagerDesk *d = NULL;
	MRRectangle *rect = &(data->curr);

	if (cancelled)
		rect = &(data->start);

	d = translate_client_pos_main (rect->x + PagerState.main_canvas->root_x,
																 rect->y + PagerState.main_canvas->root_y,
																 rect->width,
																 rect->height, wd->desk, &real_x, &real_y);

	if (d && d->desk != wd->desk) {
		move_client_to_desk (wd, d->desk);
		set_moveresize_aspect (data, PagerState.vscreen_width,
													 d->background->width, PagerState.vscreen_height,
													 d->background->height, d->background->root_x,
													 d->background->root_y);
	}
	LOCAL_DEBUG_OUT ("d(%p)->start(%+d%+d)->curr(%+d%+d)->real(%+d%+d)", d,
									 data->start.x, data->start.y, data->curr.x,
									 data->curr.y, real_x, real_y);
	schedule_moveresize_req (F_MOVE, real_x, real_y, wd->client, True);
	Scr.moveresize_in_progress = NULL;
}

void apply_client_resize (struct ASMoveResizeData *data)
{
	ASWindowData *wd = fetch_client (AS_WIDGET_WINDOW (data->mr));
	unsigned int real_width = 1, real_height = 1;
	LOCAL_DEBUG_OUT ("desk(%p)->size(%dx%d)", PagerState.resize_desk,
									 data->curr.width, data->curr.height);
	translate_client_size (data->curr.width, data->curr.height, &real_width,
												 &real_height);
	schedule_moveresize_req (F_RESIZE, real_width, real_height, wd->client,
													 False);
}

void complete_client_resize (struct ASMoveResizeData *data, Bool cancelled)
{
	ASWindowData *wd = fetch_client (AS_WIDGET_WINDOW (data->mr));
	unsigned int real_width = 1, real_height = 1;

	if (cancelled) {
		LOCAL_DEBUG_OUT ("%dx%d%+d%+d", data->start.x, data->start.y,
										 data->start.width, data->start.height);
		translate_client_size (data->start.width, data->start.height,
													 &real_width, &real_height);
	} else {
		LOCAL_DEBUG_OUT ("%dx%d%+d%+d", data->curr.x, data->curr.y,
										 data->curr.width, data->curr.height);
		translate_client_size (data->curr.width, data->curr.height,
													 &real_width, &real_height);
	}
	schedule_moveresize_req (F_RESIZE, real_width, real_height, wd->client,
													 True);
	PagerState.resize_desk = NULL;
	Scr.moveresize_in_progress = NULL;
}

ASGrid *make_pager_grid ()
{
	ASGrid *grid;
	int resist = Scr.Feel.EdgeResistanceMove;
	int attract = Scr.Feel.EdgeAttractionScreen;
	int i;

	grid = safecalloc (1, sizeof (ASGrid));

	for (i = 0; i < PagerState.desks_num; ++i) {
		ASPagerDesk *d = &(PagerState.desks[i]);
		int k = d->clients_num;
		ASWindowData *wd;
		ASTBarData *bb = d->background;

		add_gridline (grid, bb->root_y, bb->root_x, bb->root_x + bb->width,
									resist, attract, ASGL_Absolute);
		add_gridline (grid, bb->root_y + bb->height, bb->root_x,
									bb->root_x + bb->width, resist, attract, ASGL_Absolute);
		add_gridline (grid, bb->root_x, bb->root_y, bb->root_y + bb->height,
									resist, attract, ASGL_Absolute | ASGL_Vertical);
		add_gridline (grid, bb->root_x + bb->width, bb->root_y,
									bb->root_y + bb->height, resist, attract,
									ASGL_Absolute | ASGL_Vertical);

		if (!get_flags (d->flags, ASP_DeskShaded)) {	/* add all the grid separation windows : */
			register int p = PagerState.page_columns - 1;
			int pos_inc = bb->width / PagerState.page_columns;
			int pos = bb->root_x + p * pos_inc;
			int size = bb->height;
			int pos2 = bb->root_y;
			/* vertical bars : */
			while (--p >= 0) {
				add_gridline (grid, pos, pos2, pos2 + size, resist, attract,
											ASGL_Absolute | ASGL_Vertical);
				pos -= pos_inc;
			}
			/* horizontal bars */
			p = PagerState.page_rows - 1;
			pos_inc = bb->height / PagerState.page_rows;
			pos = bb->root_y + p * pos_inc;
			pos2 = bb->root_x;
			size = bb->width;
			while (--p >= 0) {
				add_gridline (grid, pos, pos2, pos2 + size, resist, attract,
											ASGL_Absolute);
				pos -= pos_inc;
			}
		}
		while (--k >= 0)
			if ((wd = PagerState.desks[i].clients[k]) != NULL) {
				int outer_gravity = Scr.Feel.EdgeAttractionWindow;
				int inner_gravity = Scr.Feel.EdgeAttractionWindow;
				if (get_flags (wd->flags, AS_AvoidCover))
					inner_gravity = -1;

				if (inner_gravity != 0)
					add_canvas_grid (grid, wd->canvas, outer_gravity, inner_gravity,
													 ASGL_Absolute);
			}
	}
	/* add all the window edges for this desktop : */
	/* iterate_asbidirlist( Scr.Windows->clients, get_aswindow_grid_iter_func, (void*)&grid_data, NULL, False ); */

#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	print_asgrid (grid);
#endif

	return grid;
}


void
start_moveresize_client (ASWindowData * wd, Bool move, ASEvent * event)
{
	ASMoveResizeData *mvrdata = NULL;
	ASPagerDesk *d = get_pager_desk (wd->desk);
	MyStyle *pager_focused_style = Scr.Look.MSWindow[BACK_FOCUSED];

	release_pressure ();
	if (Scr.moveresize_in_progress)
		return;


	if (move) {
		Scr.Look.MSWindow[BACK_FOCUSED] =
				mystyle_find_or_default ("focused_window_style");
		mvrdata =
				move_widget_interactively (PagerState.main_canvas, wd->canvas,
																	 event, apply_client_move,
																	 complete_client_move);
	} else if (d != NULL) {
		if (get_flags (wd->state_flags, AS_Shaded)) {
			XBell (dpy, Scr.screen);
			return;
		}

		Scr.Look.MSWindow[BACK_FOCUSED] =
				mystyle_find_or_default ("focused_window_style");
		PagerState.resize_desk = d;
		mvrdata = resize_widget_interactively (d->desk_canvas,
																					 wd->canvas,
																					 event,
																					 apply_client_resize,
																					 complete_client_resize, FR_SE);
	}
	Scr.Look.MSWindow[BACK_FOCUSED] = pager_focused_style;

	LOCAL_DEBUG_OUT ("mvrdata(%p)", mvrdata);
	if (mvrdata) {
		if (d)
			set_moveresize_aspect (mvrdata, PagerState.vscreen_width,
														 d->background->width,
														 PagerState.vscreen_height,
														 d->background->height, d->background->root_x,
														 d->background->root_y);

		mvrdata->grid = make_pager_grid ();
		Scr.moveresize_in_progress = mvrdata;
	}
}
