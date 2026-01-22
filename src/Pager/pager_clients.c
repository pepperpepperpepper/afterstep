#include "pager_internal.h"

void set_client_name (ASWindowData * wd, Bool redraw)
{
	if (wd->bar) {
		LOCAL_DEBUG_OUT ("name_enc = %ld, name = \"%s\"",
										 wd->window_name_encoding,
										 wd->window_name ? wd->window_name : "(null)");
		change_astbar_first_label (wd->bar, wd->window_name,
															 wd->window_name_encoding);
		set_astbar_balloon (wd->bar, 0, wd->window_name,
												wd->window_name_encoding);
	}
	if (redraw && wd->canvas)
		render_astbar (wd->bar, wd->canvas);
}

void
place_client (ASPagerDesk * d, ASWindowData * wd, Bool force_redraw,
							Bool dont_update_shape)
{
	int x = 0, y = 0, width = 1, height = 1;
	int curr_x, curr_y;
	int desk_width = d->background->width;
	int desk_height = d->background->height;

	if (desk_width == 0 || desk_height == 0)
		return;
	if (wd) {
		int client_x = wd->frame_rect.x;
		int client_y = wd->frame_rect.y;
		int client_width = wd->frame_rect.width;
		int client_height = wd->frame_rect.height;
		if (get_flags (wd->state_flags, AS_Iconic)) {
			client_x = wd->icon_rect.x + Scr.Vx;
			client_y = wd->icon_rect.y + Scr.Vy;
			client_width = wd->icon_rect.width;
			client_height = wd->icon_rect.height;
		} else {
			if (get_flags (wd->state_flags, AS_Sticky)) {
				client_x += Scr.Vx;
				client_y += Scr.Vy;
			}
			if (get_flags (wd->state_flags, AS_Shaded)) {
				if (get_flags (wd->flags, AS_VerticalTitle))
					client_width = (PagerState.vscreen_width * 2) / desk_width;
				else
					client_height = (PagerState.vscreen_height * 2) / desk_height;
			}
		}
		LOCAL_DEBUG_OUT ("+PLACE->client(%lX)->frame_geom(%dx%d%+d%+d)",
										 wd->client, client_width, client_height, client_x,
										 client_y);
		x = (client_x * desk_width) / PagerState.vscreen_width;
		y = (client_y * desk_height) / PagerState.vscreen_height;
		width = (client_width * desk_width) / PagerState.vscreen_width;
		height = (client_height * desk_height) / PagerState.vscreen_height;
		if (x < 0) {
			width += x;
			x = 0;
		}
		if (y < 0) {
			height += y;
			y = 0;
		}
		if (width <= 0)
			width = 1;
		if (height <= 0)
			height = 1;

		LOCAL_DEBUG_OUT
				("@@@@     ###   $$$   Client \"%s\" background position is %+d%+d, client's = %+d%+d",
				 wd->window_name ? wd->window_name : "(null)",
				 d->background->win_x, d->background->win_y, x, y);
		x += (int)d->background->win_x;
		y += (int)d->background->win_y;
		if (wd->canvas) {
			ASCanvas *canvas = wd->canvas;
			unsigned int bw = 0;
			get_canvas_position (canvas, NULL, &curr_x, &curr_y, &bw);
			LOCAL_DEBUG_OUT ("current canvas at = %+d%+d", curr_x, curr_y);
			if (curr_x == x && curr_y == y && width == canvas->width
					&& height == canvas->height) {
				if (force_redraw) {
					render_astbar (wd->bar, canvas);
					update_canvas_display (canvas);
				}
			} else
				moveresize_canvas (canvas, x - bw, y - bw, width, height);

			LOCAL_DEBUG_OUT ("+PLACE->canvas(%p)->geom(%dx%d%+d%+d)", wd->canvas,
											 width, height, x, y);
		}
	}
}

void set_client_look (ASWindowData * wd, Bool redraw)
{
	LOCAL_DEBUG_CALLER_OUT ("%p, %p", wd, wd->bar);

	if (wd->bar) {
		int state = get_flags (wd->state_flags, AS_Sticky) ?
				BACK_STICKY : BACK_UNFOCUSED;
		set_astbar_style_ptr (wd->bar, -1, Scr.Look.MSWindow[state]);
		set_astbar_style_ptr (wd->bar, BAR_STATE_FOCUSED,
													Scr.Look.MSWindow[BACK_FOCUSED]);
	} else
		show_warning ("NULL tbar for window data found. client = %lX",
									wd->client);

	if (redraw && wd->canvas)
		render_astbar (wd->bar, wd->canvas);
}

void on_client_moveresize (ASWindowData * wd)
{
	if (handle_canvas_config (wd->canvas) != 0) {
		ASPagerDesk *d = get_pager_desk (wd->desk);
		set_astbar_size (wd->bar, wd->canvas->width, wd->canvas->height);
		render_astbar (wd->bar, wd->canvas);
		update_canvas_display (wd->canvas);
		if (d)
			set_flags (d->flags, ASP_ShapeDirty);
	}
}

Bool register_client (ASWindowData * wd)
{
	if (PagerClients == NULL)
		PagerClients = create_ashash (0, NULL, NULL, NULL);

	return (add_hash_item (PagerClients, AS_HASHABLE (wd->canvas->w), wd) ==
					ASH_Success);
}


ASWindowData *fetch_client (Window w)
{
	ASHashData hdata = { 0 };
	if (PagerClients)
		if (get_hash_item (PagerClients, AS_HASHABLE (w), &hdata.vptr) !=
				ASH_Success)
			hdata.vptr = NULL;
	return hdata.vptr;
}

void unregister_client (Window w)
{
	if (PagerClients)
		remove_hash_item (PagerClients, AS_HASHABLE (w), NULL, False);
}

void forget_desk_client (int desk, ASWindowData * wd)
{
	ASPagerDesk *d = get_pager_desk (desk);
	LOCAL_DEBUG_CALLER_OUT ("%d(%p),%p", desk, d, wd);
	if (d && wd && d->clients) {
		register int i = d->clients_num;
		while (--i >= 0)
			if (d->clients[i] == wd) {
				register int k = i, last_k = d->clients_num;
				LOCAL_DEBUG_OUT ("client found at %d", i);
				while (++k < last_k)
					d->clients[k - 1] = d->clients[k];
				d->clients[k - 1] = NULL;
				--(d->clients_num);
			}
		if (i >= 0)
			set_flags (d->flags, ASP_ShapeDirty);
	}
}

void add_desk_client (ASPagerDesk * d, ASWindowData * wd)
{
	LOCAL_DEBUG_OUT ("%p, %p, index %d", d, wd, d ? d->clients_num : -1);
	if (d && wd) {
		int i = d->clients_num;
		while (--i >= 0)
			if (d->clients[i] == wd)
				return;									/* already belongs to that desk */
		d->clients =
				realloc (d->clients,
								 (d->clients_num + 1) * sizeof (ASWindowData *));
		d->clients[d->clients_num] = wd;
		++(d->clients_num);
		set_flags (d->flags, ASP_ShapeDirty);
	}
}

void add_client (ASWindowData * wd)
{
	ASPagerDesk *d = get_pager_desk (wd->desk);
	Window w;
	XSetWindowAttributes attr;

	if (d == NULL)
		return;

	attr.event_mask = CLIENT_EVENT_MASK;
	/* create window, canvas and tbar : */
	w = create_visual_window (Scr.asv, d->desk_canvas->w, -1, -1, 1, 1, 0,
														InputOutput, CWEventMask, &attr);
	if (w == None)
		return;

	wd->canvas = create_ascanvas (w);
	wd->bar = create_astbar ();

	add_desk_client (d, wd);
	register_client (wd);

	set_astbar_hilite (wd->bar, BAR_STATE_UNFOCUSED,
										 NORMAL_HILITE | NO_HILITE_OUTLINE);
	set_astbar_hilite (wd->bar, BAR_STATE_FOCUSED,
										 NORMAL_HILITE | NO_HILITE_OUTLINE);
	add_astbar_label (wd->bar, 0, 0, 0, NO_ALIGN, 0, 0, NULL, AS_Text_ASCII);
	move_astbar (wd->bar, wd->canvas, 0, 0);
	if (wd->focused)
		set_astbar_focused (wd->bar, NULL, True);

	set_client_name (wd, False);
	set_client_look (wd, False);
	place_client (d, wd, True, False);
	map_canvas_window (wd->canvas, True);
	LOCAL_DEBUG_OUT ("+CREAT->canvas(%p)->bar(%p)->client_win(%lX)",
									 wd->canvas, wd->bar, wd->client);
}

void refresh_client (INT32 old_desk, ASWindowData * wd)
{
	ASPagerDesk *d = get_pager_desk (wd->desk);
	LOCAL_DEBUG_OUT
			("client(%lX)->name(%s)->icon_name(%s)->desk(%ld)->old_desk(%ld)",
			 wd->client, wd->window_name ? wd->window_name : "(null)",
			 wd->icon_name, wd->desk, old_desk);
	if (old_desk != wd->desk) {
		forget_desk_client (old_desk, wd);
		if (d != NULL) {
			add_desk_client (d, wd);
			LOCAL_DEBUG_OUT ("reparenting client to desk %ld", d->desk);
			quietly_reparent_canvas (wd->canvas, d->desk_canvas->w,
															 CLIENT_EVENT_MASK, False, None);
		}
	}
	set_client_name (wd, False);
	LOCAL_DEBUG_OUT ("client \"%s\" focused = %d",
									 wd->window_name ? wd->window_name : "(null)",
									 wd->focused);
	set_astbar_focused (wd->bar, NULL, wd->focused);
	set_client_look (wd, True);
	LOCAL_DEBUG_OUT ("placing client%s", "");
	if (d != NULL)
		place_client (d, wd, False, False);
	LOCAL_DEBUG_OUT ("all done%s", "");
}


void
change_desk_stacking (int desk, unsigned int clients_num, send_data_type * clients)
{
	ASPagerDesk *d = get_pager_desk (desk);
	int i, real_clients_count = 0;
	if (d == NULL)
		return;

	if (d->clients_num < clients_num) {
		d->clients =
				realloc (d->clients, clients_num * sizeof (ASWindowData *));
		memset (d->clients, 0x00, clients_num * sizeof (ASWindowData *));
		d->clients_num = clients_num;
	}
	for (i = 0; i < clients_num; ++i) {
		ASWindowData *wd = fetch_window_by_id ((Window)clients[i]);
		if (wd != NULL) {						/* window is in stacking order, but wew were not notifyed about it yet */
			int k = real_clients_count;
			while (--k >= 0)
				if (d->clients[k] == wd)
					break;								/* already belongs to that desk */
			if (k < 0) {
				d->clients[i] = wd;
				++real_clients_count;
				LOCAL_DEBUG_OUT ("id(%lX)->wd(%p)", (unsigned long)clients[i],
												 d->clients[i]);
			}
		}
	}
	d->clients_num = real_clients_count;
	set_flags (d->flags, ASP_ShapeDirty);
	restack_desk_windows (d);
}
