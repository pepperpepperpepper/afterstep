#include "pager_internal.h"

void on_desk_moveresize (ASPagerDesk * d);

int update_main_canvas_config ()
{
	int changes = handle_canvas_config (PagerState.main_canvas);
	if (changes != 0)
		set_root_clip_area (PagerState.main_canvas);
	return changes;
}

static void place_desk_title (ASPagerDesk * d)
{
	if (d->title) {
		int x = 0, y = 0;
		int width = d->title_width, height = d->title_height;
		if (get_flags (Config->flags, VERTICAL_LABEL)) {
			if (get_flags (Config->flags, LABEL_BELOW_DESK)
					&& !get_flags (d->flags, ASP_DeskShaded))
				x = PagerState.desk_width - width - Config->border_width;
			height = PagerState.desk_height - Config->border_width;
		} else {
			if (get_flags (Config->flags, LABEL_BELOW_DESK)
					&& !get_flags (d->flags, ASP_DeskShaded))
				y = PagerState.desk_height - height - Config->border_width;
			width = PagerState.desk_width - Config->border_width;
		}
		move_astbar (d->title, d->desk_canvas, x, y);
		set_astbar_size (d->title, width, height);
	}
}

static void place_desk_background (ASPagerDesk * d)
{
	int x = 0, y = 0;
	int width = PagerState.desk_width;
	int height = PagerState.desk_height;
	if (get_flags (Config->flags, USE_LABEL)) {
		width -= Config->border_width;
		height -= Config->border_width;

		if (get_flags (Config->flags, VERTICAL_LABEL))
			width -= d->title_width;
		else
			height -= d->title_height;


		if (!get_flags (Config->flags, LABEL_BELOW_DESK)
				|| get_flags (d->flags, ASP_DeskShaded)) {
			if (get_flags (Config->flags, VERTICAL_LABEL))
				x = d->title_width;
			else
				y = d->title_height;
		}
	}
	move_astbar (d->background, d->desk_canvas, x, y);
	set_astbar_size (d->background, width, height);
}

Bool render_desk (ASPagerDesk * d, Bool force)
{
	if (is_canvas_needs_redraw (d->desk_canvas))
		force = True;

	if (d->title)
		if (force || DoesBarNeedsRendering (d->title))
			render_astbar (d->title, d->desk_canvas);
	if (force || DoesBarNeedsRendering (d->background))
		render_astbar (d->background, d->desk_canvas);

	if (d->desk_canvas->shape == NULL && get_flags (d->flags, ASP_Shaped)) {
		clear_flags (d->flags, ASP_Shaped);
		set_flags (d->flags, ASP_ShapeDirty);
	} else if (d->desk_canvas->shape != NULL) {
		set_flags (d->flags, ASP_Shaped);
		set_flags (d->flags, ASP_ShapeDirty);
	}

	if (is_canvas_dirty (d->desk_canvas)) {
		update_canvas_display (d->desk_canvas);
//    update_pager_shape();
		return True;
	}
	return False;
}

#if 0
static void update_desk_shape (ASPagerDesk * d)
{
	int i;

	if (d == NULL)
		return;

	update_canvas_display_mask (d->desk_canvas, True);

	LOCAL_DEBUG_CALLER_OUT ("desk %p flags = 0x%lX", d, d->flags);
	if (get_flags (Config->flags, SHOW_SELECTION)
			&& d->desk == Scr.CurrentDesk) {
		XShapeCombineRectangles (dpy, d->desk_canvas->w, ShapeBounding, 0, 0,
														 &(PagerState.selection_bar_rects[0]), 4,
														 ShapeUnion, Unsorted);
		LOCAL_DEBUG_OUT ("added selection_bar_rects to shape%s", "");
	}
	if (get_flags (Config->flags, PAGE_SEPARATOR)) {
		XShapeCombineRectangles (dpy, d->desk_canvas->w, ShapeBounding,
														 0, 0, &(d->separator_bar_rects[0]),
														 d->separator_bars_num, ShapeUnion, Unsorted);
		LOCAL_DEBUG_OUT ("added %d separator_bar_rects to shape",
										 d->separator_bars_num);
#if 0
		i = d->separator_bars_num;
		while (--i >= 0) {
			LOCAL_DEBUG_OUT ("\t %dx%d%+d%+d", d->separator_bar_rects[i].width,
											 d->separator_bar_rects[i].height,
											 d->separator_bar_rects[i].x,
											 d->separator_bar_rects[i].y);
		}
#endif
	}

	if (d->clients_num > 0) {
		register ASWindowData **clients = d->clients;
		i = d->clients_num;
		LOCAL_DEBUG_OUT ("clients_num %d", d->clients_num);
		while (--i >= 0) {
			LOCAL_DEBUG_OUT ("client %d data %p", i, clients[i]);
			if (clients[i]) {
				LOCAL_DEBUG_OUT ("combining client \"%s\"", clients[i]->icon_name);
				combine_canvas_shape (d->desk_canvas, clients[i]->canvas, False,
															True);
			}
		}
	}
	clear_flags (d->flags, ASP_ShapeDirty);
}
#endif

void update_pager_shape ()
{
#ifdef SHAPE
	int i;
	Bool shape_cleared = False;
	XRectangle border[4];

	if (get_flags (PagerState.flags, ASP_ReceivingWindowList))
		return;

	LOCAL_DEBUG_CALLER_OUT ("pager flags = 0x%lX", PagerState.flags);

	if (get_flags (PagerState.flags, ASP_ShapeDirty)) {
		shape_cleared = True;
		clear_flags (PagerState.flags, ASP_ShapeDirty);
	}

	if (PagerState.main_canvas->shape)
		flush_vector (PagerState.main_canvas->shape);
	else
		PagerState.main_canvas->shape = create_shape ();

	for (i = 0; i < PagerState.desks_num; ++i) {
		ASPagerDesk *d = &(PagerState.desks[i]);
		int x, y;
		unsigned int d_width, d_height, bw;

#ifdef STRICT_GEOMETRY
		get_current_canvas_geometry (d->desk_canvas, &x, &y, &d_width,
																 &d_height, &bw);
#else
		x = d->desk_canvas->root_x - PagerState.main_canvas->root_x;
		y = d->desk_canvas->root_y - PagerState.main_canvas->root_y;
		d_width = d->desk_canvas->width;
		d_height = d->desk_canvas->height;
		bw = Config->border_width;
#endif

		LOCAL_DEBUG_OUT ("desk geometry = %dx%d%+d%+d, bw = %d", d_width,
										 d_height, x, y, bw);
		combine_canvas_shape_at_geom (PagerState.main_canvas, d->desk_canvas,
																	x, y, d_width, d_height, bw);

		if (Config->MSDeskBack[i]->texture_type == TEXTURE_SHAPED_PIXMAP ||
				Config->MSDeskBack[i]->texture_type ==
				TEXTURE_SHAPED_SCALED_PIXMAP) {
			if (get_flags (Config->flags, SHOW_SELECTION)
					&& d->desk == Scr.CurrentDesk)
				add_shape_rectangles (PagerState.main_canvas->shape,
															&(PagerState.selection_bar_rects[0]), 4, x,
															y, PagerState.main_canvas->width,
															PagerState.main_canvas->height);

			if (get_flags (Config->flags, PAGE_SEPARATOR))
				add_shape_rectangles (PagerState.main_canvas->shape,
															&(d->separator_bar_rects[0]),
															d->separator_bars_num, x, y,
															PagerState.main_canvas->width,
															PagerState.main_canvas->height);

			if (d->clients_num > 0) {
				register ASWindowData **clients = d->clients;
				int k = d->clients_num;
				LOCAL_DEBUG_OUT ("desk %d clients_num %d", i, d->clients_num);
				while (--k >= 0) {
					LOCAL_DEBUG_OUT ("client %d data %p", i, clients[k]);
					if (clients[k] && clients[k]->canvas) {
						int client_x, client_y;
						unsigned int client_width, client_height, client_bw;
						get_current_canvas_geometry (clients[k]->canvas, &client_x,
																				 &client_y, &client_width,
																				 &client_height, &client_bw);

						LOCAL_DEBUG_OUT ("combining client \"%s\"",
														 clients[k]->icon_name);
						combine_canvas_shape_at_geom (PagerState.main_canvas,
																					clients[k]->canvas, client_x + x,
																					client_y + y, client_width,
																					client_height, client_bw);
					}
				}
			}
		}
		clear_flags (d->flags, ASP_ShapeDirty);
	}

	border[0].x = 0;
	border[0].y = PagerState.main_canvas->height - Config->border_width;
	border[0].width = PagerState.main_canvas->width;
	border[0].height = Config->border_width;
	border[1].x = PagerState.main_canvas->width - Config->border_width;
	border[1].y = 0;
	border[1].width = Config->border_width;
	border[1].height = PagerState.main_canvas->height;
#if 1
	border[2].x = 0;
	border[2].y = 0;
	border[2].width = PagerState.main_canvas->width;
	border[2].height = Config->border_width;
	border[3].x = 0;
	border[3].y = 0;
	border[3].width = Config->border_width;
	border[3].height = PagerState.main_canvas->height;
#endif

	add_shape_rectangles (PagerState.main_canvas->shape, &(border[0]), 4, 0,
												0, PagerState.main_canvas->width,
												PagerState.main_canvas->height);

	update_canvas_display_mask (PagerState.main_canvas, True);

#endif
}


inline unsigned int calculate_desk_width (ASPagerDesk * d)
{
	unsigned int width = PagerState.desk_width;

	if (get_flags (Config->flags, VERTICAL_LABEL) && d->title_width > 0) {
		if (get_flags (d->flags, ASP_DeskShaded))
			width = d->title_width + Config->border_width;
	}
	return width;
}

unsigned int calculate_desk_height (ASPagerDesk * d)
{
	unsigned int height = PagerState.desk_height;

	if (!get_flags (Config->flags, VERTICAL_LABEL) && d->title_height > 0) {
		if (get_flags (d->flags, ASP_DeskShaded))
			height = d->title_height + Config->border_width;
	}
	return height;
}

void place_desk (ASPagerDesk * d, int x, int y, unsigned int width,
								 unsigned int height)
{
	int win_x = 0, win_y = 0;
	unsigned int bw;
	get_canvas_position (d->desk_canvas, NULL, &win_x, &win_y, &bw);
	LOCAL_DEBUG_OUT
			("desk window %lX, curr_geom = %dx%d%+d%+d, bw = %d, new_geom = %dx%d%+d%+d",
			 d->desk_canvas->w, d->desk_canvas->width, d->desk_canvas->height,
			 win_x, win_y, bw, width, height, x, y);
	if (d->desk_canvas->width == width && d->desk_canvas->height == height
			&& win_x == x && win_y == y) {
		on_desk_moveresize (d);
	} else
		moveresize_canvas (d->desk_canvas, x, y, width, height);
}

ASPagerDesk *get_pager_desk (INT32 desk)
{
	if (IsValidDesk (desk)) {
		register INT32 pager_desk = desk - PagerState.start_desk;
		if (pager_desk >= 0 && pager_desk < PagerState.desks_num)
			return &(PagerState.desks[pager_desk]);
	}
	return NULL;
}

void restack_desk_windows (ASPagerDesk * d)
{
	Window *list, *curr;
	int win_count = 0;
	int i, k;
	if (d == NULL)
		return;

	win_count = d->clients_num;
	if (get_flags (Config->flags, SHOW_SELECTION)
			&& d->desk == Scr.CurrentDesk)
		win_count += 4;
	if (get_flags (Config->flags, PAGE_SEPARATOR))
		win_count += d->separator_bars_num;

	if (win_count <= 1)
		return;

	curr = list = safecalloc (win_count, sizeof (Window));
	k = 0;

	if (get_flags (Config->flags, SHOW_SELECTION)
			&& d->desk == Scr.CurrentDesk)
		for (i = 0; i < 4; ++i) {
			if (PagerState.selection_bars[i]) {
				curr[k] = PagerState.selection_bars[i];
				++k;
			}
		}

	if (get_flags (Config->flags, PAGE_SEPARATOR)) {
		register Window *sbars = d->separator_bars;
		i = d->separator_bars_num;
		while (--i >= 0) {
			if (sbars[i]) {
				curr[k] = sbars[i];
				++k;
			}
		}
	}

	if (d->clients_num > 0) {
		register ASWindowData **clients = d->clients;
		i = -1;
		while (++i < d->clients_num) {
			if (clients[i] && clients[i]->desk == d->desk &&
					clients[i]->canvas && clients[i]->canvas->w) {
				LOCAL_DEBUG_OUT
						("k = %d, w = %lX, client = %lX, wd = %p, name = \"%s\"", k,
						 clients[i]->canvas->w, clients[i]->client, clients[i],
						 clients[i]->window_name ? clients[i]->window_name : "(null)");
				curr[k++] = clients[i]->canvas->w;
			}
		}
	}

	XRaiseWindow (dpy, list[0]);
	XRestackWindows (dpy, list, k);
	free (list);
}

void place_separation_bars (ASPagerDesk * d)
{
	register Window *wa = d ? d->separator_bars : NULL;
	register XRectangle *wrecta = d ? d->separator_bar_rects : NULL;
	if (wa) {
		register int p = PagerState.page_columns - 1;
		int pos_inc =
				(Scr.MyDisplayWidth * d->background->width) /
				PagerState.vscreen_width;
		/* d->background->width/PagerState.page_columns ; */
		int pos = d->background->win_x + p * pos_inc;
		int size = d->background->height;
		int pos2 = d->background->win_y;
		/* vertical bars : */
		while (--p >= 0) {
			wrecta[p].x = pos;
			wrecta[p].y = pos2;
			wrecta[p].width = 1;
			wrecta[p].height = size;
			XMoveResizeWindow (dpy, wa[p], pos, pos2, 1, size);
			pos -= pos_inc;
		}
		/* horizontal bars */
		wa += PagerState.page_columns - 1;
		wrecta += PagerState.page_columns - 1;
		p = PagerState.page_rows - 1;
		pos_inc =
				(Scr.MyDisplayHeight * d->background->height) /
				PagerState.vscreen_height;
		/* Scr.MyDisplayHeight/PagerState.vscale_v;     */
		/* d->background->height/PagerState.page_rows ; */
		pos = d->background->win_y + p * pos_inc;
		pos2 = d->background->win_x;
		size = d->background->width;
		while (--p >= 0) {
			wrecta[p].x = pos2;
			wrecta[p].y = pos;
			wrecta[p].width = size;
			wrecta[p].height = 1;
			XMoveResizeWindow (dpy, wa[p], pos2, pos, size, 1);
			pos -= pos_inc;
		}
	}
}

void redecorate_pager_desks ()
{
	/* need to create enough desktop canvases */
	int i;
	char buf[256];
	XSetWindowAttributes attr;
	int wasted_x = Config->border_width * (Config->columns);
	int wasted_y = Config->border_width * (Config->rows);
	int max_title_width = 0;
	int max_title_height = 0;

	for (i = 0; i < PagerState.desks_num; ++i) {
		ASPagerDesk *d = &(PagerState.desks[i]);
		int p;
		Bool just_created_background = False;

		ARGB2PIXEL (Scr.asv, Config->border_color_argb, &(attr.border_pixel));
		if (d->desk_canvas == NULL) {
			Window w;
			attr.event_mask =
					StructureNotifyMask | ButtonReleaseMask | ButtonPressMask |
					ButtonMotionMask;

			w = create_visual_window (Scr.asv, PagerState.main_canvas->w, 0, 0,
																PagerState.desk_width,
																PagerState.desk_height,
																Config->border_width, InputOutput,
																CWEventMask | CWBorderPixel, &attr);
			d->desk_canvas = create_ascanvas (w);
			LOCAL_DEBUG_OUT
					("+CREAT canvas(%p)->desk(%ld)->geom(%dx%d%+d%+d)->parent(%lx)",
					 d->desk_canvas, PagerState.start_desk + i,
					 PagerState.desk_width, PagerState.desk_height, 0, 0,
					 PagerState.main_canvas->w);
			handle_canvas_config (d->desk_canvas);
		} else {
			XSetWindowBorder (dpy, d->desk_canvas->w, attr.border_pixel);
		}
		/* create & moveresize label bar : */
		if (get_flags (Config->flags, USE_LABEL)) {
			int align = Config->align;
			ASFlagType ibevel = Config->inactive_desk_bevel;
			ASFlagType abevel = Config->active_desk_bevel;
			int h_spacing = Config->h_spacing;
			int v_spacing = Config->v_spacing;
			int flip =
					get_flags (Config->flags, VERTICAL_LABEL) ? FLIP_VERTICAL : 0;
			Bool just_created = False;

			if (!get_flags (Config->set_flags, PAGER_SET_ALIGN))
				align = PagerState.tbar_props->align;
			if (!get_flags (Config->set_flags, PAGER_SET_ACTIVE_BEVEL))
				abevel = PagerState.tbar_props->bevel;
			if (!get_flags (Config->set_flags, PAGER_SET_INACTIVE_BEVEL))
				ibevel = PagerState.tbar_props->bevel;
			h_spacing = PagerState.tbar_props->title_h_spacing;
			v_spacing = PagerState.tbar_props->title_v_spacing;

			if (d->title == NULL) {
				d->title = create_astbar ();
				d->title->context = C_TITLE;
				just_created = True;
			} else										/* delete label if it was previously created : */
				delete_astbar_tile (d->title, -1);

			set_astbar_hilite (d->title, BAR_STATE_UNFOCUSED, ibevel);
			set_astbar_hilite (d->title, BAR_STATE_FOCUSED, abevel);

			set_astbar_style_ptr (d->title, -1,
														Config->MSDeskTitle[DESK_ACTIVE]);
			set_astbar_style_ptr (d->title, BAR_STATE_UNFOCUSED,
														Config->MSDeskTitle[DESK_INACTIVE]);

			if (Config->labels && Config->labels[i])
				add_astbar_label (d->title, 0, flip ? 1 : 0, flip, align,
													h_spacing, v_spacing, Config->labels[i],
													AS_Text_ASCII);
			else {
				sprintf (buf, "Desk %d", (int)PagerState.start_desk + i);
				add_astbar_label (d->title, 0, flip ? 1 : 0, flip, align,
													h_spacing, v_spacing, buf, AS_Text_ASCII);
			}
			if (PagerState.shade_button.context != C_NO_CONTEXT) {
				MyButton *list[1];
				list[0] = &(PagerState.shade_button);
				add_astbar_btnblock (d->title, flip ? 0 : 1, 0, flip, NO_ALIGN,
														 &list[0], 0xFFFFFFFF, 1,
														 PagerState.tbar_props->buttons_h_border,
														 PagerState.tbar_props->buttons_v_border,
														 PagerState.tbar_props->buttons_spacing, 0);
			}
			if (get_flags (Config->flags, VERTICAL_LABEL)) {
				int size = calculate_astbar_width (d->title);
				if (size > max_title_width)
					max_title_width = size;
				d->title_height =
						PagerState.desk_height - Config->border_width * 2;
				/*we do it below using max_title_width : wasted_x += d->title_width ; */
			} else {
				int size = calculate_astbar_height (d->title);
				LOCAL_DEBUG_OUT ("size = %d, max_title_height = %d", size,
												 max_title_height);
				d->title_width = PagerState.desk_width - Config->border_width * 2;
				if (size > max_title_height)
					max_title_height = size;
				/*we do it below using max_title_height : wasted_y += d->title_height ; */
			}
		} else {
			if (d->title)
				destroy_astbar (&(d->title));
			d->title_width = d->title_height = 0;
		}

		/* create & moveresize desktop background bar : */
		if (d->background == NULL) {
			d->background = create_astbar ();
			just_created_background = True;
		}

		set_astbar_style_ptr (d->background, -1, Config->MSDeskBack[i]);
		set_astbar_style_ptr (d->background, BAR_STATE_UNFOCUSED,
													Config->MSDeskBack[i]);
		if (Config->styles[i] == NULL)
			set_flags (d->flags, ASP_UseRootBackground);

		if (just_created_background)
			place_desk_background (d);
		if (get_flags (d->flags, ASP_UseRootBackground))
			request_background_image (d);

		if (d->separator_bars) {
			for (p = 0; p < d->separator_bars_num; ++p)
				if (d->separator_bars[p])
					XDestroyWindow (dpy, d->separator_bars[p]);
			free (d->separator_bars);
			free (d->separator_bar_rects);
			d->separator_bars_num = 0;
			d->separator_bars = NULL;
			d->separator_bar_rects = NULL;
		}
		if (get_flags (Config->flags, PAGE_SEPARATOR)) {
			d->separator_bars_num =
					PagerState.page_columns - 1 + PagerState.page_rows - 1;
			d->separator_bars =
					safecalloc (d->separator_bars_num, sizeof (Window));
			d->separator_bar_rects =
					safecalloc (d->separator_bars_num, sizeof (XRectangle));
			ARGB2PIXEL (Scr.asv, Config->grid_color_argb,
									&(attr.background_pixel));
			for (p = 0; p < d->separator_bars_num; ++p) {
				d->separator_bars[p] =
						create_visual_window (Scr.asv, d->desk_canvas->w, 0, 0, 1, 1,
																	0, InputOutput, CWBackPixel, &attr);
				d->separator_bar_rects[p].width =
						d->separator_bar_rects[p].height = 1;
			}
			XMapSubwindows (dpy, d->desk_canvas->w);
			place_separation_bars (d);
		}
	}

	if (get_flags (Config->flags, USE_LABEL)) {
		for (i = 0; i < PagerState.desks_num; ++i) {	/* create & moveresize label bar : */
			ASPagerDesk *d = &(PagerState.desks[i]);

			if (get_flags (Config->flags, VERTICAL_LABEL))
				d->title_width = max_title_width;
			else
				d->title_height = max_title_height;
			place_desk_title (d);
		}

		if (get_flags (Config->flags, VERTICAL_LABEL)) {
			wasted_x += max_title_width * Config->columns;
			LOCAL_DEBUG_OUT ("title_width = %d", max_title_width);
		} else {
			wasted_y += max_title_height * Config->rows;
			LOCAL_DEBUG_OUT ("title_height = %d", max_title_height);
		}
	}

	/* if wasted space changed and configured geometry does not specify size -
	 * adjust desk_width/height accordingly :
	 */
	if (!get_flags (Config->geometry.flags, WidthValue)) {
		int delta = (wasted_x - PagerState.wasted_width) / Config->columns;
		LOCAL_DEBUG_OUT
				("wasted_x = %d, (old was = %d) - adjusting desk_width by %d",
				 wasted_x, PagerState.wasted_width, delta);
		if (delta != 0) {
			PagerState.desk_width += delta;
			PagerState.wasted_width += delta * Config->columns;
		}
	}
	if (!get_flags (Config->geometry.flags, HeightValue)) {
		int delta = (wasted_y - PagerState.wasted_height) / Config->rows;
		LOCAL_DEBUG_OUT
				(" :RESIZING: wasted_y = %d, (old was = %d) - adjusting desk_height(%d) by %d",
				 wasted_y, PagerState.wasted_height, PagerState.desk_height,
				 delta);
		if (delta != 0) {
			PagerState.desk_height += delta;
			PagerState.wasted_height += delta * Config->rows;
		}
	}

	/* selection bars : */
	ARGB2PIXEL (Scr.asv, Config->selection_color_argb,
							&(attr.background_pixel));
	if (get_flags (Config->flags, SHOW_SELECTION)) {
		for (i = 0; i < 4; i++)
			if (PagerState.selection_bars[i] == None) {
				PagerState.selection_bars[i] =
						create_visual_window (Scr.asv, PagerState.main_canvas->w, 0, 0,
																	1, 1, 0, InputOutput, CWBackPixel,
																	&attr);
			} else {
				XSetWindowBackground (dpy, PagerState.selection_bars[i],
															attr.background_pixel);
				XClearWindow (dpy, PagerState.selection_bars[i]);
			}
	} else
		for (i = 0; i < 4; i++)
			if (PagerState.selection_bars[i] != None) {
				XDestroyWindow (dpy, PagerState.selection_bars[i]);
				PagerState.selection_bars[i] = None;
			}
	XMapSubwindows (dpy, PagerState.main_canvas->w);
}

void rearrange_pager_desks (Bool dont_resize_main)
{
	/* need to create enough desktop canvases */
	int i;
	int col = 0;
	int x = 0, y = 0, row_height = 0;
	/* Pass 1: first we must resize or main window !!! */
	update_main_canvas_config ();
	if (!dont_resize_main) {
		int all_width = 0, all_height = 0;
		for (i = 0; i < PagerState.desks_num; ++i) {
			ASPagerDesk *d = &(PagerState.desks[i]);
			int width, height;

			width = calculate_desk_width (d);
			height = calculate_desk_height (d);

			if (height > row_height)
				row_height = height;

			if (++col >= Config->columns) {
				if (all_width < x + width)
					all_width = x + width;
				y += row_height;

				row_height = 0;
				x = 0;
				col = 0;
			} else
				x += width;
			LOCAL_DEBUG_OUT
					(" :RESIZING: desk = %d, size = %dx%d, all_size = %+d%+d, +x+y = %+d%+d, row_height = %d",
					 i, width, height, all_width, all_height, x, y, row_height);

		}
		if (all_width < x)
			all_width = x;
		if (all_height < y + row_height)
			all_height = y + row_height;
		all_width += Config->border_width;
		all_height += Config->border_width;
		LOCAL_DEBUG_OUT
				(" :RESIZING: resizing_main : all_size = %dx%d current size = %dx%d",
				 all_width, all_height, PagerState.main_canvas->width,
				 PagerState.main_canvas->height);
		if (PagerState.main_canvas->width != all_width
				|| PagerState.main_canvas->height != all_height) {
			resize_canvas (PagerState.main_canvas, all_width, all_height);
			return;
		}
	}
	/* Pass 2: now we can resize the rest of the windows : */
	col = 0;
	x = y = 0;
	row_height = 0;
	for (i = 0; i < PagerState.desks_num; ++i) {
		ASPagerDesk *d = &(PagerState.desks[i]);
		int width, height;

		width = calculate_desk_width (d);
		height = calculate_desk_height (d);

		place_desk (d, x, y, width - Config->border_width,
								height - Config->border_width);

		if (height > row_height)
			row_height = height;

		if (++col >= Config->columns) {
			y += row_height;
			row_height = 0;
			x = 0;
			col = 0;
		} else
			x += width;
	}
	ASSync (False);
}

unsigned int calculate_pager_desk_width ()
{
	int main_width = PagerState.main_canvas->width - Config->border_width;
	if (!get_flags (Config->flags, VERTICAL_LABEL))
		return main_width / Config->columns;
	else {
		unsigned int unshaded_col_count = 0;
		unsigned int shaded_width = 0;
		int col = 0;
		/* we have to calculate the number of not-shaded columns,
		 * and then devide size of the main canvas by this number : */
		for (col = 0; col < Config->columns; ++col) {
			unsigned int col_shaded_width = 0;
			int i;

			for (i = col; i < PagerState.desks_num; i += Config->columns) {
				ASPagerDesk *d = &(PagerState.desks[i]);

				if (!get_flags (d->flags, ASP_DeskShaded)) {
					++unshaded_col_count;
					col_shaded_width = 0;
					break;
				} else {
					int dw = calculate_desk_width (d);
					if (col_shaded_width < dw)
						col_shaded_width = dw;
				}
			}
			shaded_width += col_shaded_width;
		}

		LOCAL_DEBUG_OUT ("unshaded_col_count = %d", unshaded_col_count);
		if (unshaded_col_count == 0)
			return PagerState.desk_width;
		return (main_width - shaded_width) / unshaded_col_count;
	}
}

unsigned int calculate_pager_desk_height ()
{
	int main_height = PagerState.main_canvas->height - Config->border_width;
	if (get_flags (Config->flags, VERTICAL_LABEL))
		return main_height / Config->rows;
	else {
		unsigned int unshaded_row_count = 0;
		unsigned int shaded_height = 0;
		int row;
		/* we have to calculate the number of not-shaded columns,
		 * and then devide size of the main canvas by this number : */
		for (row = 0; row < Config->rows; ++row) {
			unsigned int row_shaded_height = 0;
			int i, max_i = (row + 1) * Config->columns;

			if (max_i > PagerState.desks_num)
				max_i = PagerState.desks_num;

			for (i = row * Config->columns; i < max_i; ++i) {
				ASPagerDesk *d = &(PagerState.desks[i]);
				if (!get_flags (d->flags, ASP_DeskShaded)) {
					++unshaded_row_count;
					row_shaded_height = 0;
					break;
				} else {
					int dh = calculate_desk_height (d);
					if (row_shaded_height < dh)
						row_shaded_height = dh;
				}
			}
			shaded_height += row_shaded_height;
		}
		LOCAL_DEBUG_OUT
				("unshaded_row_count = %d, shaded_height = %d, main_height = %d",
				 unshaded_row_count, shaded_height, main_height);
		if (unshaded_row_count == 0)
			return PagerState.desk_height;
		return (main_height - shaded_height) / unshaded_row_count;
	}
}

void on_desk_moveresize (ASPagerDesk * d)
{
	ASFlagType changes = handle_canvas_config (d->desk_canvas);

	if (get_flags (changes, CANVAS_RESIZED)) {
		place_desk_title (d);
		place_desk_background (d);
		/* place all the grid separation windows : */
		place_separation_bars (d);

		/* rearrange all the client windows : */
		if (d->clients && d->clients_num > 0) {
			register int i;
			i = d->clients_num;
			while (--i >= 0)
				place_client (d, d->clients[i], False, True);
		}
		if (d->desk == Scr.CurrentDesk)
			place_selection ();
		if (get_flags (d->flags, ASP_UseRootBackground))
			request_background_image (d);
	}
	update_astbar_transparency (d->title, d->desk_canvas, False);
	update_astbar_transparency (d->background, d->desk_canvas, False);

	render_desk (d, (changes != 0));
}

void on_pager_window_moveresize (void *client, Window w, int x, int y,
																 unsigned int width, unsigned int height)
{
	if (client == NULL) {					/* then its one of our canvases !!! */
		int i;

		if (w == PagerState.main_canvas->w) {	/* need to rescale everything? maybe: */
			int new_desk_width;
			int new_desk_height = 0;
			int changes;

			changes = update_main_canvas_config ();
			if (changes & CANVAS_RESIZED) {
				set_flags (PagerState.flags, ASP_ShapeDirty);

				new_desk_width = calculate_pager_desk_width ();
				new_desk_height = calculate_pager_desk_height ();

				if (new_desk_width <= 0)
					new_desk_width = 1;
				if (new_desk_height <= 0)
					new_desk_height = 1;
				LOCAL_DEBUG_OUT
						(" :RESIZING:  old_desk_size(%dx%d) new_desk_size(%dx%d)",
						 PagerState.desk_width, PagerState.desk_height, new_desk_width,
						 new_desk_height);
				if (new_desk_width != PagerState.desk_width
						|| new_desk_height != PagerState.desk_height) {
					PagerState.desk_width = new_desk_width;
					PagerState.desk_height = new_desk_height;
				}

				rearrange_pager_desks (True);
			} else if (changes != 0) {
				for (i = 0; i < PagerState.desks_num; ++i)
					on_desk_moveresize (&(PagerState.desks[i]));
			}
		} else {										/* then its one of our desk subwindows : */

			for (i = 0; i < PagerState.desks_num; ++i) {
				if (PagerState.desks[i].desk_canvas->w == w) {
					on_desk_moveresize (&(PagerState.desks[i]));
					break;
				}
			}
		}
	}
}
