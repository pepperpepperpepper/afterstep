#include "pager_internal.h"

/*************************************************************************
 * selection + viewport
 *************************************************************************/
void place_selection ()
{
	LOCAL_DEBUG_CALLER_OUT ("Scr.CurrentDesk(%d)->start_desk(%ld)",
													Scr.CurrentDesk, PagerState.start_desk);
	if (get_flags (Config->flags, SHOW_SELECTION)) {
		ASPagerDesk *sel_desk = get_pager_desk (Scr.CurrentDesk);

		if (sel_desk != NULL && sel_desk->background && sel_desk->desk_canvas) {
			int sel_x = sel_desk->background->win_x;
			int sel_y = sel_desk->background->win_y;
			int page_width =					/*Scr.MyDisplayWidth/PagerState.vscale_h ; */
					(Scr.MyDisplayWidth * sel_desk->background->width) /
					PagerState.vscreen_width;
			int page_height =					/* Scr.MyDisplayHeight/PagerState.vscale_v ; */
					(Scr.MyDisplayHeight * sel_desk->background->height) /
					PagerState.vscreen_height;
			int i = 4;

			sel_x += (Scr.Vx * page_width) / Scr.MyDisplayWidth;
			sel_y += (Scr.Vy * page_height) / Scr.MyDisplayHeight;
			LOCAL_DEBUG_OUT ("sel_pos(%+d%+d)->page_size(%dx%d)->desk(%ld)",
											 sel_x, sel_y, page_width, page_height,
											 sel_desk->desk);
			while (--i >= 0)
				XReparentWindow (dpy, PagerState.selection_bars[i],
												 sel_desk->desk_canvas->w, -10, -10);

			PagerState.selection_bar_rects[0].x = sel_x - 1;
			PagerState.selection_bar_rects[0].y = sel_y - 1;
			PagerState.selection_bar_rects[0].width = page_width + 2;
			PagerState.selection_bar_rects[0].height = 1;

			PagerState.selection_bar_rects[1].x = sel_x - 1;
			PagerState.selection_bar_rects[1].y = sel_y - 1;
			PagerState.selection_bar_rects[1].width = 1;
			PagerState.selection_bar_rects[1].height = page_height + 2;

			PagerState.selection_bar_rects[2].x = sel_x - 1;
			PagerState.selection_bar_rects[2].y = sel_y + page_height + 1;
			PagerState.selection_bar_rects[2].width = page_width + 2;
			PagerState.selection_bar_rects[2].height = 1;

			PagerState.selection_bar_rects[3].x = sel_x + page_width + 1;
			PagerState.selection_bar_rects[3].y = sel_y - 1;
			PagerState.selection_bar_rects[3].width = 1;
			PagerState.selection_bar_rects[3].height = page_height + 2;

			if (!get_flags (sel_desk->flags, ASP_DeskShaded)) {
				i = 4;
				while (--i >= 0)
					XMoveResizeWindow (dpy, PagerState.selection_bars[i],
														 PagerState.selection_bar_rects[i].x,
														 PagerState.selection_bar_rects[i].y,
														 PagerState.selection_bar_rects[i].width,
														 PagerState.selection_bar_rects[i].height);
			}
			XMapSubwindows (dpy, sel_desk->desk_canvas->w);
			ASSync (False);
			restack_desk_windows (sel_desk);
			set_flags (sel_desk->flags, ASP_ShapeDirty);
		}
	}
}

/*************************************************************************
 * background + viewport switching
 *************************************************************************/
void set_desktop_pixmap (int desk, Pixmap pmap)
{
	ASPagerDesk *d = get_pager_desk (desk);
	unsigned int width, height;

	if (!get_drawable_size (pmap, &width, &height))
		pmap = None;
	LOCAL_DEBUG_OUT ("desk(%d)->d(%p)->pmap(%lX)->size(%dx%d)", desk, d,
									 pmap, width, height);
	if (pmap == None)
		return;
	if (d == NULL) {
		XFreePixmap (dpy, pmap);
		return;
	}

	if (get_flags (d->flags, ASP_UseRootBackground)) {
		ASImage *im =
				pixmap2asimage (Scr.asv, pmap, 0, 0, width, height, 0xFFFFFFFF,
												False, 100);

		XFreePixmap (dpy, pmap);
		if (d->back)
			destroy_asimage (&(d->back));
		d->back = im;
		delete_astbar_tile (d->background, 0);
		add_astbar_icon (d->background, 0, 0, 0, NO_ALIGN, d->back);
		render_astbar (d->background, d->desk_canvas);
		update_canvas_display (d->desk_canvas);
	}
}

void move_sticky_clients ()
{
	int desk = PagerState.desks_num;
	ASPagerDesk *current_desk = get_pager_desk (Scr.CurrentDesk);

	while (--desk >= 0) {
		ASPagerDesk *d = &(PagerState.desks[desk]);
		if (d->clients && d->clients_num > 0) {
			register int i = d->clients_num;
			register ASWindowData **clients = d->clients;
			while (--i >= 0)
				if (clients[i] && get_flags (clients[i]->state_flags, AS_Sticky)) {
					if (clients[i]->desk != Scr.CurrentDesk && current_desk) {	/* in order to make an illusion of smooth desktop
																																			 * switching - we'll reparent window ahead of time */
						LOCAL_DEBUG_OUT ("reparenting client to desk %ld", d->desk);
						quietly_reparent_canvas (clients[i]->canvas,
																		 current_desk->desk_canvas->w,
																		 CLIENT_EVENT_MASK, False, None);
					}
					place_client (d, clients[i], True, True);
				}
			set_flags (d->flags, ASP_ShapeDirty);
		}
	}
}

void switch_deskviewport (int new_desk, int new_vx, int new_vy)
{
	Bool view_changed = (new_vx != Scr.Vx || new_vy != Scr.Vy);
	Bool desk_changed = (new_desk != Scr.CurrentDesk);

	Scr.Vx = new_vx;
	Scr.Vy = new_vy;
	Scr.CurrentDesk = new_desk;

	if (desk_changed && IsValidDesk (new_desk)) {
		ASPagerDesk *new_d = get_pager_desk (new_desk);

		if (PagerState.focused_desk != new_d) {
			if (PagerState.focused_desk) {
				set_astbar_focused (PagerState.focused_desk->title,
														PagerState.focused_desk->desk_canvas, False);
				set_astbar_focused (PagerState.focused_desk->background,
														PagerState.focused_desk->desk_canvas, False);
				if (is_canvas_dirty (PagerState.focused_desk->desk_canvas))
					update_canvas_display (PagerState.focused_desk->desk_canvas);
				PagerState.focused_desk = NULL;
			}
			if (new_d) {
				set_astbar_focused (new_d->title, new_d->desk_canvas, True);
				set_astbar_focused (new_d->background, new_d->desk_canvas, True);
				if (is_canvas_dirty (new_d->desk_canvas))
					update_canvas_display (new_d->desk_canvas);
				PagerState.focused_desk = new_d;
			}
		}
	}

	if (view_changed || desk_changed) {
		move_sticky_clients ();
		place_selection ();
		if (new_desk < PagerState.start_desk
				|| new_desk >= PagerState.start_desk + PagerState.desks_num) {
			int i = 4;
			while (--i >= 0)
				XUnmapWindow (dpy, PagerState.selection_bars[i]);

		}
	}
}

void request_background_image (ASPagerDesk * d)
{
	if (d->back == NULL ||
			(d->back->width != d->background->width ||
			 d->back->height != d->background->height)) {
		XEvent e;
		e.xclient.type = ClientMessage;
		e.xclient.message_type = _AS_BACKGROUND;
		e.xclient.format = 16;
		e.xclient.window = PagerState.main_canvas->w;
		e.xclient.data.s[0] = d->desk;
		e.xclient.data.s[1] = 0;
		e.xclient.data.s[2] = 0;
		e.xclient.data.s[3] = d->background->width;
		e.xclient.data.s[4] = d->background->height;
		e.xclient.data.s[5] = 0x01;
		e.xclient.data.s[6] = 0;
		e.xclient.data.s[7] = 0;
		e.xclient.data.s[8] = 0;
		LOCAL_DEBUG_OUT ("size(%dx%d)", e.xclient.data.s[3],
										 e.xclient.data.s[4]);
		XSendEvent (dpy, Scr.Root, False, PropertyChangeMask, &e);
		ASSync (False);
	}
}

