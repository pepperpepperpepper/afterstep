/*
 * Copyright (c) 2002,2003 Sasha Vasko <sasha@aftercode.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/**********************************************************************
 *
 * Add a new window, put the titlbar and other stuff around
 * the window
 *
 **********************************************************************/
#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"
#include "../../libAfterStep/wmprops.h"
#include "window_frame_internal.h"

/* icon geometry relative to the root window :                      */
Bool get_icon_root_geometry (ASWindow * asw, ASRectangle * geom)
{
	ASCanvas *canvas = NULL;
	if (AS_ASSERT (asw) || AS_ASSERT (geom))
		return False;

	geom->height = 0;
	if (asw->icon_canvas) {
		canvas = asw->icon_canvas;
		if (asw->icon_title_canvas && asw->icon_title_canvas != canvas)
			geom->height = asw->icon_title_canvas->height;
	} else if (asw->icon_title_canvas)
		canvas = asw->icon_title_canvas;

	if (canvas) {
		geom->x = canvas->root_x;
		geom->y = canvas->root_y;
		geom->width = canvas->width + canvas->bw * 2;
		geom->height += canvas->height + canvas->bw * 2;
		return True;
	}
	return False;
}

Bool apply_window_status_size (register ASWindow * asw, ASOrientation * od)
{
	Bool moved = False;
	Bool resized = False;
	ASFlagType client_changes = 0;
	/* note that icons are handled by iconbox */
	if (!ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		int step_size = make_shade_animation_step (asw, od);
		int new_width = asw->status->width;
		int new_height = asw->status->height;
		LOCAL_DEBUG_OUT
				("**CONFG Client(%lx(%s))->status(%ux%u%+d%+d,%s,%s(%d>-%d))",
				 asw->w, ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname",
				 asw->status->width, asw->status->height, asw->status->x,
				 asw->status->y, ASWIN_HFLAGS (asw,
																			 AS_VerticalTitle) ? "Vert" : "Horz",
				 step_size > 0 ? "Shaded" : "Unshaded", asw->shading_steps,
				 step_size);

		if (step_size > 0) {
			if (ASWIN_HFLAGS (asw, AS_VerticalTitle)) {
				new_width = step_size;
			} else {
				new_height = step_size;
			}
		}
		resized = (asw->frame_canvas->width != new_width ||
							 asw->frame_canvas->height != new_height);
		moved = (asw->frame_canvas->root_x != asw->status->x ||
						 asw->frame_canvas->root_y != asw->status->y);

		moveresize_canvas (asw->frame_canvas, asw->status->x, asw->status->y,
											 new_width, new_height);
		/* when we resize the client - our frame should already be positioned correctly ! */
		if (step_size <= 0)
			client_changes =
					resize_frame_subwindows (asw, od, new_width, new_height);
#if 0
		fprintf (stderr, "client_changes = %X, moved = %d. Called from :\n",
						 client_changes, moved);
		print_simple_backtrace ();
#endif
		if (moved && client_changes == 0)
			send_canvas_configure_notify (asw->frame_canvas, asw->client_canvas);
	}
	return moved || resized || client_changes != 0;
}

void SendConfigureNotify (ASWindow * asw)
{
	XEvent client_event;

	if (ASWIN_GET_FLAGS (asw, AS_Dead | AS_MoveresizeInProgress))
		return;

	client_event.type = ConfigureNotify;
	client_event.xconfigure.display = dpy;
	client_event.xconfigure.event = asw->w;
	client_event.xconfigure.window = asw->w;

	client_event.xconfigure.x = asw->client_canvas->root_x;
	client_event.xconfigure.y = asw->client_canvas->root_y;
	client_event.xconfigure.width = asw->client_canvas->width;
	client_event.xconfigure.height = asw->client_canvas->height;

	client_event.xconfigure.border_width = asw->status->border_width;
	/* Real ConfigureNotify events say we're above title window, so ... */
	/* what if we don't have a title ????? */
	client_event.xconfigure.above =
			asw->frame_sides[FR_N] ? asw->frame_sides[FR_N]->w : asw->frame;
	client_event.xconfigure.override_redirect = False;
	XSendEvent (dpy, asw->w, False, StructureNotifyMask, &client_event);
}

/* this gets called when StructureNotify/SubstractureNotify arrives : */
void on_window_moveresize (ASWindow * asw, Window w)
{
	int i;
	ASOrientation *od;
	unsigned int normal_width, normal_height;
	Bool update_shape = False;

	LOCAL_DEBUG_CALLER_OUT ("(%p,%lx,asw->w=%lx)", asw, w, asw->w);
	if (AS_ASSERT (asw))
		return;

	od = get_orientation_data (asw);

	if (w == asw->w) {						/* simply update client's size and position */
		ASFlagType changes;
		if (ASWIN_GET_FLAGS (asw, AS_Dead))
			return;
		changes = handle_canvas_config (asw->client_canvas);
		if (asw->internal && asw->internal->on_moveresize)
			asw->internal->on_moveresize (asw->internal, w);

		update_shape = (changes != 0);
		if (XPending (dpy) > 0) {
			XEvent tmp;
			XNextEvent (dpy, &tmp);
			if (tmp.type == ConfigureNotify
					&& tmp.xconfigure.window == asw->frame)
				w = asw->frame;
			else
				XPutBackEvent (dpy, &tmp);
		}

	}

	if (w == asw->frame) {				/* resize canvases here : */
		int changes = handle_canvas_config (asw->frame_canvas);
		LOCAL_DEBUG_OUT ("changes=0x%X", changes);
		/* we must resize using current window size instead of event's size */
		*(od->in_width) = asw->frame_canvas->width;
		*(od->in_height) = asw->frame_canvas->height;
		normal_width = *(od->out_width);
		normal_height = *(od->out_height);

		if (get_flags (changes, CANVAS_RESIZED)) {
			register unsigned int *frame_size = &(asw->status->frame_size[0]);
			int step_size = make_shade_animation_step (asw, od);
			LOCAL_DEBUG_OUT ("step_size = %d", step_size);
			if (step_size <= 0) {			/* don't moveresize client window while shading !!!! */
				resize_frame_subwindows (asw, od, asw->frame_canvas->width,
																 asw->frame_canvas->height);
			} else {
				if (normal_height != step_size) {
					*(od->in_width) = normal_width;
					*(od->in_height) = step_size;

					if (normal_height < step_size) {
						/* we get smoother animation if we move decoration ahead of actually
						 * resizing frame window : */
						move_shading_frame (asw, od, step_size);
						ASSync (False);
						sleep_a_millisec (10);
						/*LOCAL_DEBUG_OUT( "**SHADE Client(%lx(%s))->(%d>-%d)", asw->w, ASWIN_NAME(asw)?ASWIN_NAME(asw):"noname", asw->shading_steps, step_size ); */
						resize_canvas (asw->frame_canvas, *(od->out_width),
													 *(od->out_height));
						ASSync (False);
					} else {
						sleep_a_millisec (10);
						resize_canvas (asw->frame_canvas, *(od->out_width),
													 *(od->out_height));
						ASSync (False);
						move_shading_frame (asw, od, step_size);
						ASSync (False);
					}
				} else {								/* probably just the change of the look - titlebar may need to be resized */
					resize_canvases (asw, od, normal_width, normal_height,
													 frame_size);
					move_canvas (asw->client_canvas, frame_size[FR_W],
											 frame_size[FR_N]);
				}

			}
			if (asw->shading_steps == 0) {
				for (i = 0; i < FRAME_SIDES; ++i)
					if (asw->frame_sides[i])
						check_frame_side_config (asw, asw->frame_sides[i]->w, od);
			}
			update_shape = True;
		} else if (get_flags (changes, CANVAS_MOVED)) {
			LOCAL_DEBUG_OUT ("window is moved but not resized %s", "");
			update_window_frame_moved (asw, od);
			/* also sent synthetic ConfigureNotify : */
			SendConfigureNotify (asw);
		}

		if (changes != 0) {
			LOCAL_DEBUG_OUT ("resized = %X, shaped = %lX",
											 get_flags (changes, CANVAS_RESIZED),
											 ASWIN_GET_FLAGS (asw, AS_ShapedDecor | AS_Shaped));

			if (!check_frame_offscreen (asw))
				update_window_transparency (asw, False);

			if (!ASWIN_GET_FLAGS (asw, AS_Dead | AS_MoveresizeInProgress))
				broadcast_config (M_CONFIGURE_WINDOW, asw);
		}
	} else if (asw->icon_canvas && w == asw->icon_canvas->w) {
		ASFlagType changes = handle_canvas_config (asw->icon_canvas);
		LOCAL_DEBUG_OUT ("icon resized to %dx%d%+d%+d",
										 asw->icon_canvas->width, asw->icon_canvas->height,
										 asw->icon_canvas->root_x, asw->icon_canvas->root_y);
		if (get_flags (changes, CANVAS_RESIZED)) {
			unsigned short title_size = 0;
			if (asw->icon_title
					&& (asw->icon_title_canvas == asw->icon_canvas
							|| asw->icon_title_canvas == NULL)) {
				title_size = calculate_astbar_height (asw->icon_title);
				move_astbar (asw->icon_title, asw->icon_canvas, 0,
										 asw->icon_canvas->height - title_size);
				set_astbar_size (asw->icon_title, asw->icon_canvas->width,
												 title_size);
				if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
					render_astbar (asw->icon_title, asw->icon_canvas);
				}
				LOCAL_DEBUG_OUT ("title_size = %d", title_size);
			}
			set_astbar_size (asw->icon_button, asw->icon_canvas->width,
											 asw->icon_canvas->height - title_size);
			if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
				render_astbar (asw->icon_button, asw->icon_canvas);
				update_canvas_display (asw->icon_canvas);
			}
		}
		if (ASWIN_GET_FLAGS (asw, AS_Iconic))
			broadcast_config (M_CONFIGURE_WINDOW, asw);
	} else if (asw->icon_title_canvas && w == asw->icon_title_canvas->w) {
		if (handle_canvas_config (asw->icon_title_canvas) && asw->icon_title) {
			LOCAL_DEBUG_OUT ("icon_title resized to %dx%d%+d%+d",
											 asw->icon_title_canvas->width,
											 asw->icon_title_canvas->height,
											 asw->icon_title_canvas->root_x,
											 asw->icon_title_canvas->root_y);
			set_astbar_size (asw->icon_title, asw->icon_title_canvas->width,
											 asw->icon_title->height);
			if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
				render_astbar (asw->icon_title, asw->icon_title_canvas);
				update_canvas_display (asw->icon_title_canvas);
			}
		}
		if (ASWIN_GET_FLAGS (asw, AS_Iconic))
			broadcast_config (M_CONFIGURE_WINDOW, asw);
	} else if (asw->shading_steps == 0) {	/* one of the frame canvases : */
		if (!check_frame_side_config (asw, w, od))
			if (asw->internal && asw->internal->on_moveresize)
				asw->internal->on_moveresize (asw->internal, w);
	}

	if (update_shape) {
		if (ASWIN_GET_FLAGS (asw, AS_ShapedDecor | AS_Shaped))
			SetShape (asw, 0);
		else if (get_flags (asw->internal_flags, ASWF_PendingShapeRemoval))
			ClearShape (asw);
	}

	ASSync (False);
}

void on_window_title_changed (ASWindow * asw, Bool update_display)
{
	if (AS_ASSERT (asw))
		return;
	if (is_output_level_under_threshold (OUTPUT_LEVEL_HINTS))
		print_clean_hints (NULL, NULL, asw->hints);
	if (asw->tbar) {
		ASCanvas *canvas =
				ASWIN_HFLAGS (asw,
											AS_VerticalTitle) ? asw->frame_sides[FR_W] : asw->
				frame_sides[FR_N];
		if (change_astbar_first_label
				(asw->tbar, ASWIN_NAME (asw), ASWIN_NAME_ENCODING (asw)))
			if (canvas && update_display) {
				invalidate_canvas_save (canvas);

				if (canvas->shape) {
					XRectangle rect;
					rect.x = asw->tbar->win_x;
					rect.y = asw->tbar->win_y;
					rect.width = asw->tbar->width;
					rect.height = asw->tbar->height;
					subtract_shape_rectangle (canvas->shape, &rect, 1, 0, 0,
																		canvas->width, canvas->height);
				}
				update_window_tbar_size (asw);
				render_astbar (asw->tbar, canvas);
				update_canvas_display (canvas);
				//if( ASWIN_GET_FLAGS( asw, AS_ShapedDecor ) )
				SetShape (asw, 0);
			}
	}
	LOCAL_DEBUG_OUT ("icon_title = %p, icon_name = \"%s\"", asw->icon_title,
									 ASWIN_ICON_NAME (asw) ? ASWIN_ICON_NAME (asw) :
									 "(null)");
	if (asw->icon_title) {
		if (change_astbar_first_label
				(asw->icon_title, ASWIN_ICON_NAME (asw),
				 ASWIN_ICON_NAME_ENC (asw)))
			if (ASWIN_GET_FLAGS (asw, AS_Iconic))
				on_icon_changed (asw);
	}
}

Bool collect_aswindow_hints (ASWindow * asw, ASRawHints * raw_hints);

void on_window_hints_changed (ASWindow * asw)
{
	static ASRawHints raw_hints;
	ASHints *hints = NULL, *old_hints = NULL;
	Bool tie_changed = False;
	ASStatusHints scratch_status;	/* all we need from it is AS_Urgent state change */
	Bool status_changed = False;


	if (AS_ASSERT (asw))
		return;
	if (ASWIN_GET_FLAGS (asw, AS_Dead))
		return;
	if (!collect_aswindow_hints (asw, &raw_hints))
		return;

	memset (&scratch_status, 0x00, sizeof (scratch_status));

	hints =
			merge_hints (&raw_hints, Database, &scratch_status,
									 Scr.Look.supported_hints, HINT_ANY, NULL, asw->w);

	destroy_raw_hints (&raw_hints, True);
	if (hints) {
		show_debug (__FILE__, __FUNCTION__, __LINE__,
								"Window management hints collected and merged for window %X",
								asw->w);
		if (is_output_level_under_threshold (OUTPUT_LEVEL_HINTS))
			print_clean_hints (NULL, NULL, hints);
	} else {
		show_warning ("Failed to merge window management hints for window %X",
									asw->w);
		return;
	}

	old_hints = asw->hints;
	tie_changed = ((old_hints->transient_for != hints->transient_for) ||
								 (old_hints->group_lead != hints->group_lead));

	if (tie_changed)
		untie_aswindow (asw);

	asw->hints = hints;

	if (tie_changed)
		tie_aswindow (asw);

	SelectDecor (asw);

	LOCAL_DEBUG_OUT
			("redecorating window %p(\"%s\") to update to new hints...", asw,
			 hints->names[0] ? hints->names[0] : "none");
	/* we do not want to do a complete refresh of decorations -
	 * we want to do only what is neccessary: */

	if (get_flags (scratch_status.flags, AS_Urgent)) {
		if (!get_flags (asw->status->flags, AS_Urgent)) {
			set_flags (asw->status->flags, AS_Urgent);
			status_changed = True;
		}
	} else if (get_flags (asw->status->flags, AS_Urgent)) {
		ASFlagType extwm_state = 0;

		if (!get_extwm_state_flags (asw->w, &extwm_state))
			extwm_state = 0;					/* just in case */
		if (!get_flags (extwm_state, EXTWM_StateDemandsAttention)) {
			clear_flags (asw->status->flags, AS_Urgent);
			status_changed = True;
		}
	}
	if (hints2decorations (asw, old_hints))
		status_changed = True;

	if (status_changed)
		on_window_status_changed (asw, True);

	if (mystrcmp (old_hints->res_name, hints->res_name) != 0 ||
			mystrcmp (old_hints->res_class, hints->res_class) != 0)
		broadcast_res_names (asw);
	if (mystrcmp (old_hints->names[0], hints->names[0]) != 0) {
		broadcast_window_name (asw);
		set_flags (asw->internal_flags, ASWF_NameChanged);
	}
	if (mystrcmp (old_hints->icon_name, hints->icon_name) != 0)
		broadcast_icon_name (asw);

	destroy_hints (old_hints, False);
}

void on_window_opacity_changed (ASWindow * asw)
{
	CARD32 new_opacity = NET_WM_WINDOW_OPACITY_OPAQUE;
	Bool set = False;
	ASDatabaseRecord db_rec;

	if (AS_ASSERT (asw))
		return;
	if (ASWIN_GET_FLAGS (asw, AS_Dead))
		return;
	set =
			read_32bit_property (asw->w, _XA_NET_WM_WINDOW_OPACITY,
													 &new_opacity);
	if (set && new_opacity == asw->hints->window_opacity
			&& ASWIN_HFLAGS (asw, AS_WindowOpacity))
		return;

	if (fill_asdb_record (Database, asw->hints->names, &db_rec, False)) {
		if (get_flags (db_rec.set_data_flags, STYLE_WINDOW_OPACITY)) {
			new_opacity =
					set_hints_window_opacity_percent (NULL, db_rec.window_opacity);
			set = True;
		}
	}

	if (!set)
		XDeleteProperty (dpy, asw->frame, _XA_NET_WM_WINDOW_OPACITY);
	else if (new_opacity != asw->hints->window_opacity
					 || !ASWIN_HFLAGS (asw, AS_WindowOpacity)) {
		set_flags (asw->hints->flags, AS_WindowOpacity);
		asw->hints->window_opacity = new_opacity;
		set_32bit_property (asw->frame, _XA_NET_WM_WINDOW_OPACITY, XA_CARDINAL,
												asw->hints->window_opacity);
	}
}

void on_window_status_changed (ASWindow * asw, Bool reconfigured)
{
	char *unfocus_mystyle = NULL;
	char *frame_unfocus_mystyle = NULL;
	int i;
	Bool changed = False;
	ASOrientation *od = get_orientation_data (asw);

	if (AS_ASSERT (asw))
		return;

/*	get_extwm_state_flags (asw->w, &i); */

	LOCAL_DEBUG_CALLER_OUT ("(%p,%s Reconfigured)", asw,
													reconfigured ? "" : "Not");
	LOCAL_DEBUG_OUT ("status geometry = %dx%d%+d%+d", asw->status->width,
									 asw->status->height, asw->status->x, asw->status->y);
	if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		unfocus_mystyle = ASWIN_GET_FLAGS (asw, AS_Sticky) ?
				AS_ICON_TITLE_STICKY_MYSTYLE : AS_ICON_TITLE_UNFOCUS_MYSTYLE;
		if (asw->icon_title)
			changed =
					set_astbar_style (asw->icon_title, BAR_STATE_UNFOCUSED,
														unfocus_mystyle);
		if (changed || reconfigured)	/* now we need to update icon title size */
			on_icon_changed (asw);
	} else {
		Bool decor_shaped = False;
		MyFrame *frame_data = asw->frame_data;
		int back_type;
		ASFlagType *frame_bevel, title_bevel;
		int title_cm, title_hue = -1, title_sat = -1;

		if (ASWIN_GET_FLAGS (asw, AS_Sticky)) {
			back_type = BACK_STICKY;
			frame_bevel = &(frame_data->part_sbevel[0]);
			title_bevel = frame_data->title_sbevel;
			title_cm = frame_data->title_scm;
			if (get_flags (frame_data->set_title_attr, MYFRAME_TitleSHueSet))
				title_hue = frame_data->title_shue;
			if (get_flags (frame_data->set_title_attr, MYFRAME_TitleSSatSet))
				title_sat = frame_data->title_ssat;
		} else {
			back_type = BACK_UNFOCUSED;
			frame_bevel = &(frame_data->part_ubevel[0]);
			title_bevel = frame_data->title_ubevel;
			title_cm = frame_data->title_ucm;
			if (get_flags (frame_data->set_title_attr, MYFRAME_TitleUHueSet))
				title_hue = frame_data->title_uhue;
			if (get_flags (frame_data->set_title_attr, MYFRAME_TitleUSatSet))
				title_sat = frame_data->title_usat;
		}

		unfocus_mystyle = asw->hints->mystyle_names[back_type];
		if (frame_data->title_style_names[back_type])
			unfocus_mystyle = frame_data->title_style_names[back_type];
		if (unfocus_mystyle == NULL)
			unfocus_mystyle = Scr.Look.MSWindow[back_type]->name;
		if (unfocus_mystyle == NULL)
			unfocus_mystyle = Scr.Look.MSWindow[BACK_UNFOCUSED]->name;

		frame_unfocus_mystyle =
				(frame_data->frame_style_names[back_type] ==
				 NULL) ? unfocus_mystyle : frame_data->
				frame_style_names[back_type];

		for (i = 0; i < FRAME_PARTS; ++i) {
			unsigned int real_part = od->frame_rotation[i];
			if (asw->frame_bars[real_part]) {
				if (set_astbar_style
						(asw->frame_bars[real_part], BAR_STATE_UNFOCUSED,
						 frame_unfocus_mystyle))
					changed = True;
				if (set_astbar_hilite
						(asw->frame_bars[real_part], BAR_STATE_UNFOCUSED,
						 frame_bevel[i]))
					changed = True;
				if (is_astbar_shaped (asw->frame_bars[real_part], -1))
					decor_shaped = True;
			}
		}
		if (asw->tbar) {
			if (set_astbar_style
					(asw->tbar, BAR_STATE_UNFOCUSED, unfocus_mystyle))
				changed = True;
			if (set_astbar_hilite (asw->tbar, BAR_STATE_UNFOCUSED, title_bevel))
				changed = True;
			if (set_astbar_composition_method
					(asw->tbar, BAR_STATE_UNFOCUSED, title_cm))
				changed = True;
			if (set_astbar_huesat
					(asw->tbar, BAR_STATE_UNFOCUSED, title_hue, title_sat))
				changed = True;
			if (get_flags
					(asw->frame_data->condense_titlebar, ALIGN_LEFT | ALIGN_RIGHT)
					|| is_astbar_shaped (asw->tbar, -1)) {
				decor_shaped = True;
			}
		}
		if (decor_shaped)
			ASWIN_SET_FLAGS (asw, AS_ShapedDecor);
		else
			ASWIN_CLEAR_FLAGS (asw, AS_ShapedDecor);

		LOCAL_DEBUG_OUT ("status geometry = %dx%d%+d%+d", asw->status->width,
										 asw->status->height, asw->status->x, asw->status->y);
		if (changed || reconfigured) {	/* now we need to update frame sizes in status */
			unsigned int *frame_size = &(asw->status->frame_size[0]);
			unsigned short tbar_size = 0;
/*			int bw = 0 ;
			if( asw->hints && get_flags(asw->hints->flags, AS_Border))
				bw = asw->hints->border_width ; */
			tbar_size = update_window_tbar_size (asw);
			for (i = 0; i < FRAME_SIDES; ++i) {
				if (asw->frame_bars[i])
					frame_size[i] = IsSideVertical (i) ? asw->frame_bars[i]->width :
							asw->frame_bars[i]->height;
				else
					frame_size[i] = 0;
				//frame_size[i] += bw ;
			}
			if (tbar_size > 0) {
				for (i = FRAME_SIDES; i < FRAME_PARTS; ++i) {
					if (get_flags
							(asw->internal_flags,
							 ASWF_FirstCornerFollowsTbarSize << (i - FRAME_SIDES))) {
						unsigned int real_part = od->frame_rotation[i];
						if (asw->frame_bars[real_part]) {
							if (ASWIN_HFLAGS (asw, AS_VerticalTitle))
								set_astbar_size (asw->frame_bars[real_part],
																 asw->frame_bars[real_part]->width,
																 tbar_size);
							else
								set_astbar_size (asw->frame_bars[real_part], tbar_size,
																 asw->frame_bars[real_part]->height);
						}
					}
				}
			}

			frame_size[od->tbar_side] += tbar_size;
			LOCAL_DEBUG_OUT
					("status geometry = %dx%d%+d%+d frame_size = %d,%d,%d,%d",
					 asw->status->width, asw->status->height, asw->status->x,
					 asw->status->y, frame_size[0], frame_size[1], frame_size[2],
					 frame_size[3]);
			anchor2status (asw->status, asw->hints, &(asw->anchor));
			LOCAL_DEBUG_OUT ("status geometry = %dx%d%+d%+d", asw->status->width,
											 asw->status->height, asw->status->x,
											 asw->status->y);
		}
	}

	/* now we need to move/resize our frame window */
	if (!apply_window_status_size (asw, od))
		changed = True;
	if (changed)
		broadcast_config (M_STATUS_CHANGE, asw);	/* must enforce status change propagation */
	if (!ASWIN_GET_FLAGS (asw, AS_Dead))
		set_client_state (asw->w, asw->status);
}

void on_window_anchor_changed (ASWindow * asw)
{
	if (asw) {
		ASOrientation *od = get_orientation_data (asw);
		anchor2status (asw->status, asw->hints, &(asw->anchor));
		LOCAL_DEBUG_OUT ("status geometry = %dx%d%+d%+d", asw->status->width,
										 asw->status->height, asw->status->x, asw->status->y);

		/* now we need to move/resize our frame window */
		if (!apply_window_status_size (asw, od))
			broadcast_config (M_CONFIGURE_WINDOW, asw);	/* must enforce status change propagation */
		if (!ASWIN_GET_FLAGS (asw, AS_Dead))
			set_client_state (asw->w, asw->status);
	}
}

/********************************************************************/
/* end of ASWindow frame decorations management                     */
