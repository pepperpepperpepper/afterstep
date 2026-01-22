#include "pager_internal.h"

/*************************************************************************
 * shading/unshading
 *************************************************************************/
void shade_desk_column (ASPagerDesk * d, Bool shade)
{
	int i;
	for (i = (d->desk - PagerState.start_desk) % Config->columns;
			 i < PagerState.desks_num; i += Config->columns) {
		if (shade)
			set_flags (PagerState.desks[i].flags, ASP_DeskShaded);
		else
			clear_flags (PagerState.desks[i].flags, ASP_DeskShaded);
	}
	rearrange_pager_desks (False);
}

void shade_desk_row (ASPagerDesk * d, Bool shade)
{
	int col_start =
			((d->desk -
				PagerState.start_desk) / Config->columns) * Config->columns;
	int i;
	for (i = col_start; i < col_start + Config->columns; ++i) {
		if (shade)
			set_flags (PagerState.desks[i].flags, ASP_DeskShaded);
		else
			clear_flags (PagerState.desks[i].flags, ASP_DeskShaded);
	}
	LOCAL_DEBUG_OUT ("Shading - rcol_start = %d", col_start);
	rearrange_pager_desks (False);
}

void on_desk_pressure_changed (ASPagerDesk * d, ASEvent * event)
{
	int root_x = event->x.xbutton.x_root;
	int root_y = event->x.xbutton.y_root;
/*    int state = event->x.xbutton.state ; */
	int context = check_astbar_point (d->title, root_x, root_y);
	LOCAL_DEBUG_OUT ("root_pos(%+d%+d)->title_root_pos(%+d%+d)->context(%s)",
									 root_x, root_y, d->title ? d->title->root_x : 0,
									 d->title ? d->title->root_y : 0,
									 context2text (context));
	if (context != C_NO_CONTEXT && d->title) {
		set_astbar_btn_pressed (d->title, context);	/* must go before next call to properly redraw :  */
		set_astbar_pressed (d->title, d->desk_canvas, context & C_TITLE);
		PagerState.pressed_bar = d->title;
		PagerState.pressed_context = context;
	} else {
		set_astbar_pressed (d->background, d->desk_canvas, context & C_TITLE);
		PagerState.pressed_bar = d->background;
		PagerState.pressed_context = C_ROOT;
	}
	PagerState.pressed_canvas = d->desk_canvas;
	PagerState.pressed_desk = d;
	PagerState.pressed_button = event->x.xbutton.button;

	LOCAL_DEBUG_OUT ("canvas(%p)->bar(%p)->context(%X)",
									 PagerState.pressed_canvas, PagerState.pressed_bar,
									 context);

	if (is_canvas_dirty (d->desk_canvas))
		update_canvas_display (d->desk_canvas);
}

void on_pager_pressure_changed (ASEvent * event)
{
	if (event->client == NULL) {
		if (event->w == PagerState.main_canvas->w) {


		} else {
			int i;
			for (i = 0; i < PagerState.desks_num; ++i)
				if (PagerState.desks[i].desk_canvas->w == event->w) {
					on_desk_pressure_changed (&(PagerState.desks[i]), event);
					break;
				}
		}
	} else
		start_moveresize_client ((ASWindowData *) (event->client),
														 (event->x.xbutton.state & ControlMask) == 0,
														 event);
}

void on_scroll_viewport (ASEvent * event)
{
	ASPagerDesk *d = PagerState.pressed_desk;
	if (d) {
		char command[64];
		int px = 0, py = 0;
		ASQueryPointerRootXY (&px, &py);
		px -= d->desk_canvas->root_x + d->desk_canvas->bw;
		py -= d->desk_canvas->root_y + d->desk_canvas->bw;
		if (px > 0 && px < d->desk_canvas->width &&
				py > 0 && py < d->desk_canvas->height) {
			px -= d->background->win_x;
			py -= d->background->win_y;
			if (px >= 0 && py >= 0 &&
					px < d->background->width && py < d->background->height) {
				int sx = (px * PagerState.vscreen_width) / d->background->width;
				int sy = (py * PagerState.vscreen_height) / d->background->height;

				sprintf (command, "GotoDeskViewport %d%+d%+d\n", (int)d->desk, sx,
								 sy);
				SendInfo (command, 0);
				++PagerState.wait_as_response;
			}
		}
	}
}

void release_pressure ()
{
	if (PagerState.pressed_canvas && PagerState.pressed_bar) {
		LOCAL_DEBUG_OUT ("canvas(%p)->bar(%p)->context(%s)",
										 PagerState.pressed_canvas, PagerState.pressed_bar,
										 context2text (PagerState.pressed_context));
		LOCAL_DEBUG_OUT ("main_geometry(%dx%d%+d%+d)",
										 PagerState.main_canvas->width,
										 PagerState.main_canvas->height,
										 PagerState.main_canvas->root_x,
										 PagerState.main_canvas->root_y);
		if (PagerState.pressed_desk) {
			ASPagerDesk *d = PagerState.pressed_desk;
			if (PagerState.pressed_context == C_TButton0) {
				if (get_flags (Config->flags, VERTICAL_LABEL))
					shade_desk_column (d, !get_flags (d->flags, ASP_DeskShaded));
				else
					shade_desk_row (d, !get_flags (d->flags, ASP_DeskShaded));
			} else {									/* need to switch desktops here ! */

				char command[64];
				int px = 0, py = 0;
				ASQueryPointerRootXY (&px, &py);
				LOCAL_DEBUG_OUT ("pointer root pos(%+d%+d)", px, py);
				px -= d->desk_canvas->root_x + d->desk_canvas->bw;
				py -= d->desk_canvas->root_y + d->desk_canvas->bw;
				if (px > 0 && px < d->desk_canvas->width &&
						py > 0 && py < d->desk_canvas->height) {
					int new_desk, new_vx = -1, new_vy = -1;
					new_desk = d->desk;
					px -= d->background->win_x;
					py -= d->background->win_y;
					if (px >= 0 && py >= 0 &&
							px < d->background->width && py < d->background->height) {
						if (PagerState.pressed_button == Button3) {
							new_vx =
									(px * PagerState.vscreen_width) / d->background->width;
							new_vy =
									(py * PagerState.vscreen_height) / d->background->height;
						} else {						/*  calculate destination page : */
							new_vx =
									(px * PagerState.page_columns) / d->background->width;
							new_vy = (py * PagerState.page_rows) / d->background->height;
							new_vx *= Scr.MyDisplayWidth;
							new_vy *= Scr.MyDisplayHeight;
						}
					}
					if (new_vx >= 0 && new_vy >= 0)
						sprintf (command, "GotoDeskViewport %d%+d%+d\n", new_desk,
										 new_vx, new_vy);
					else
						sprintf (command, "Desk 0 %d\n", new_desk);
					SendInfo (command, 0);
					++PagerState.wait_as_response;
				}
			}
		}
		set_astbar_btn_pressed (PagerState.pressed_bar, 0);	/* must go before next call to properly redraw :  */
		set_astbar_pressed (PagerState.pressed_bar, PagerState.pressed_canvas,
												False);
		if (is_canvas_dirty (PagerState.pressed_canvas))
			update_canvas_display (PagerState.pressed_canvas);
	}
	PagerState.pressed_canvas = NULL;
	PagerState.pressed_bar = NULL;
	PagerState.pressed_desk = NULL;
	PagerState.pressed_context = C_NO_CONTEXT;
}

