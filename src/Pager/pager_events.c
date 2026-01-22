#include "pager_internal.h"

void process_message (send_data_type type, send_data_type * body);
void DispatchEvent (ASEvent * event);


void HandleEvents ()
{
	ASEvent event;
	Bool has_x_events = False;
	while (True) {
		LOCAL_DEBUG_OUT ("wait_as_resp = %d", PagerState.wait_as_response);

		while (PagerState.wait_as_response > 0) {
			ASMessage *msg = CheckASMessage (WAIT_AS_RESPONSE_TIMEOUT);
			if (msg) {
				process_message (msg->header[1], msg->body);
				DestroyASMessage (msg);
			}
			--PagerState.wait_as_response;
		}
		while ((has_x_events = XPending (dpy))) {
			if (ASNextEvent (&(event.x), True)) {
				event.client = NULL;
				setup_asevent_from_xevent (&event);
				DispatchEvent (&event);
			}
		}
		module_wait_pipes_input (process_message);
	}
}


/****************************************************************************/
/* PROCESSING OF AFTERSTEP MESSAGES :                                       */
/****************************************************************************/
void process_message (send_data_type type, send_data_type * body)
{
	LOCAL_DEBUG_OUT ("received message %lX, wait_as_resp = %d", type,
									 PagerState.wait_as_response);
	--PagerState.wait_as_response;
	if ((type & WINDOW_PACKET_MASK) != 0) {
		struct ASWindowData *wd = fetch_window_by_id (body[0]);
		WindowPacketResult res;
		/* saving relevant client info since handle_window_packet could destroy the actuall structure */
		Window saved_w = None;
		INT32 saved_desk = wd ? wd->desk : INVALID_DESK;
		INT32 new_desk = saved_desk;
		struct ASWindowData *saved_wd = wd;

		if (wd && wd->canvas)
			saved_w = wd->canvas->w;

/*         show_activity( "message %lX window %X data %p", type, body[0], wd ); */
		res = handle_window_packet (type, body, &wd);
		if (res == WP_DataCreated && get_flags (wd->flags, AS_HitPager))
			add_client (wd);
		else if (res == WP_DataChanged && get_flags (wd->flags, AS_HitPager)) {
			refresh_client (saved_desk, wd);
			new_desk = wd->desk;
		} else if (res == WP_DataDeleted) {
			int i = PagerState.desks_num;
			LOCAL_DEBUG_OUT ("client deleted (%p)->window(%lX)->desk(%ld)",
											 saved_wd, saved_w, (long)saved_desk);
			/* we really want to make sure that no desk is referencing this client : */
			while (--i >= 0)
				forget_desk_client (i, saved_wd);
			unregister_client (saved_w);
		}
		if (!get_flags (PagerState.flags, ASP_ReceivingWindowList)) {
			Bool need_shape_update = False;
			if (IsValidDesk (saved_desk)) {
				register INT32 pager_desk = saved_desk - PagerState.start_desk;
				if (pager_desk >= 0 && pager_desk < PagerState.desks_num)
					if (Config->MSDeskBack[pager_desk]->texture_type ==
							TEXTURE_SHAPED_PIXMAP
							|| Config->MSDeskBack[pager_desk]->texture_type ==
							TEXTURE_SHAPED_SCALED_PIXMAP)
						need_shape_update = True;
			}
			if (need_shape_update && saved_desk != new_desk
					&& IsValidDesk (new_desk)) {
				register INT32 pager_desk = new_desk - PagerState.start_desk;
				if (pager_desk >= 0 && pager_desk < PagerState.desks_num)
					if (Config->MSDeskBack[pager_desk]->texture_type ==
							TEXTURE_SHAPED_PIXMAP
							|| Config->MSDeskBack[pager_desk]->texture_type ==
							TEXTURE_SHAPED_SCALED_PIXMAP)
						need_shape_update = True;
			}
			if (need_shape_update)
				update_pager_shape ();
		}
	} else {
		switch (type) {
		case M_TOGGLE_PAGING:
			break;
		case M_NEW_DESKVIEWPORT:
			{
				LOCAL_DEBUG_OUT ("M_NEW_DESKVIEWPORT(desk = %ld,Vx=%ld,Vy=%ld)",
												 body[2], body[0], body[1]);
				switch_deskviewport (body[2], body[0], body[1]);
				update_pager_shape ();
			}
			break;
			case M_STACKING_ORDER:
				{
					LOCAL_DEBUG_OUT ("M_STACKING_ORDER(desk=%ld, clients_num=%ld)",
													 body[0], body[1]);
					change_desk_stacking (body[0], body[1], &body[2]);
				}
				break;
		case M_END_WINDOWLIST:
			clear_flags (PagerState.flags, ASP_ReceivingWindowList);
			update_pager_shape ();
			break;
		default:
			return;
		}
	}

}


/*************************************************************************
 * Event handling :
 *************************************************************************/
void DispatchEvent (ASEvent * event)
{
	static Bool root_pointer_moved = True;
	SHOW_EVENT_TRACE (event);

	LOCAL_DEBUG_OUT ("mvrdata(%p)->main_canvas(%p)->widget(%p)",
									 Scr.moveresize_in_progress, PagerState.main_canvas,
									 event->widget);
	if (Scr.moveresize_in_progress) {
		event->widget =
				PagerState.resize_desk ? PagerState.resize_desk->
				desk_canvas : PagerState.main_canvas;
		if (check_moveresize_event (event))
			return;
	}

	if ((event->eclass & ASE_POINTER_EVENTS) != 0
			&& is_balloon_click (&(event->x))) {
		withdraw_balloon (NULL);
		return;
	}

	event->client = NULL;
	event->widget = PagerState.main_canvas;

	if (event->w != PagerState.main_canvas->w) {
		ASWindowData *wd = fetch_client (event->w);

		if (wd) {
			if (event->x.type == ButtonPress
					&& event->x.xbutton.button != Button2) {
				event->w = get_pager_desk (wd->desk)->desk_canvas->w;
			} else {
				event->client = (void *)wd;
				event->widget = ((ASWindowData *) (event->client))->canvas;
				if ((event->eclass & ASE_POINTER_EVENTS) != 0) {
					on_astbar_pointer_action (((ASWindowData *) (event->client))->
																		bar, 0, (event->x.type == LeaveNotify),
																		root_pointer_moved);
					root_pointer_moved = False;
				}
			}
		}
	}

	switch (event->x.type) {
	case ConfigureNotify:
		if (event->client != NULL)
			on_client_moveresize ((ASWindowData *) event->client);
		else {
			on_pager_window_moveresize (event->client, event->w,
																	event->x.xconfigure.x,
																	event->x.xconfigure.y,
																	event->x.xconfigure.width,
																	event->x.xconfigure.height);
		}
		break;
	case KeyPress:
		if (event->client != NULL) {
			ASWindowData *wd = (ASWindowData *) (event->client);
			event->x.xkey.window = wd->client;
			XSendEvent (dpy, wd->client, False, KeyPressMask, &(event->x));
		}
		return;
	case KeyRelease:
		if (event->client != NULL) {
			ASWindowData *wd = (ASWindowData *) (event->client);
			event->x.xkey.window = wd->client;
			XSendEvent (dpy, wd->client, False, KeyReleaseMask, &(event->x));
		}
		return;
	case ButtonPress:
		on_pager_pressure_changed (event);
		return;
	case ButtonRelease:
		LOCAL_DEBUG_OUT ("state(0x%X)->state&ButtonAnyMask(0x%X)",
										 event->x.xbutton.state,
										 event->x.xbutton.state & ButtonAnyMask);
		if ((event->x.xbutton.state & ButtonAnyMask) ==
				(Button1Mask << (event->x.xbutton.button - Button1)))
			release_pressure ();
		return;
	case EnterNotify:
		if (event->x.xcrossing.window == Scr.Root)
			withdraw_active_balloon ();
		return;
	case MotionNotify:
		root_pointer_moved = True;
		if ((event->x.xbutton.state & Button3Mask)) {
			XEvent d;
			sleep_a_millisec (10);
			ASSync (False);
			while (ASCheckTypedEvent (MotionNotify, &d)) {
				event->x = d;
				setup_asevent_from_xevent (event);
			}
			on_scroll_viewport (event);
			sleep_a_millisec (100);
		}
		return;
	case ClientMessage:
		LOCAL_DEBUG_OUT
				("ClientMessage(\"%s\",format = %d, data=(%8.8lX,%8.8lX,%8.8lX,%8.8lX,%8.8lX)",
				 XGetAtomName (dpy, event->x.xclient.message_type),
				 event->x.xclient.format, event->x.xclient.data.l[0],
				 event->x.xclient.data.l[1], event->x.xclient.data.l[2],
				 event->x.xclient.data.l[3], event->x.xclient.data.l[4]);
		if (event->x.xclient.format == 32
				&& event->x.xclient.data.l[0] == _XA_WM_DELETE_WINDOW) {
			DeadPipe (0);
		} else if (event->x.xclient.format == 32 &&
							 event->x.xclient.message_type == _AS_BACKGROUND
							 && event->x.xclient.data.l[1] != None) {
			set_desktop_pixmap (event->x.xclient.data.l[0] -
													PagerState.start_desk,
													event->x.xclient.data.l[1]);
		}

		return;
	case PropertyNotify:
		if (event->x.xproperty.atom == _XA_NET_WM_STATE) {
			LOCAL_DEBUG_OUT ("_XA_NET_WM_STATE updated!%s", "");
			return;
		}
		handle_wmprop_event (Scr.wmprops, &(event->x));
		if (event->x.xproperty.atom == _AS_BACKGROUND) {
			register int i = PagerState.desks_num;
			LOCAL_DEBUG_OUT ("root background updated!%s", "");
			safe_asimage_destroy (Scr.RootImage);
			Scr.RootImage = NULL;
			while (--i >= 0) {
				update_astbar_transparency (PagerState.desks[i].title,
																		PagerState.desks[i].desk_canvas, True);
				update_astbar_transparency (PagerState.desks[i].background,
																		PagerState.desks[i].desk_canvas, True);
				render_desk (&(PagerState.desks[i]), False);
			}
		} else if (event->x.xproperty.atom == _AS_STYLE) {
			int i = PagerState.desks_num;
			LOCAL_DEBUG_OUT ("AS Styles updated!%s", "");
			mystyle_list_destroy_all (&(Scr.Look.styles_list));
			LoadColorScheme ();
			CheckConfigSanity ();
			/* now we need to update everything */
			while (--i >= 0) {
				register int k = PagerState.desks[i].clients_num;
				register ASWindowData **clients = PagerState.desks[i].clients;
				LOCAL_DEBUG_OUT ("i = %d, clients_num = %d ", i, k);
				while (--k >= 0) {
					LOCAL_DEBUG_OUT ("k = %d", k);
					if (clients[k])
						set_client_look (clients[k], False);
					else
						show_warning ("client %d of the desk %d is NULL", k, i);
				}
			}
			redecorate_pager_desks ();
			rearrange_pager_desks (False);
		} else if (event->x.xproperty.atom == _AS_TBAR_PROPS) {
			retrieve_pager_astbar_props ();
			redecorate_pager_desks ();
			rearrange_pager_desks (False);
		}
		return;
	default:
#ifdef XSHMIMAGE
		LOCAL_DEBUG_OUT
				("XSHMIMAGE> EVENT : completion_type = %d, event->type = %d ",
				 Scr.ShmCompletionEventType, event->x.type);
		if (event->x.type == Scr.ShmCompletionEventType)
			handle_ShmCompletion (event);
#endif													/* SHAPE */
		return;
	}
	update_pager_shape ();
}
