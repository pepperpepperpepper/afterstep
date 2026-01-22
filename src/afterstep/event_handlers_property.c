/*
 * Copyright (C) 2003 Sasha Vasko
 * Copyright (C) 1995 Bo Yang
 * Copyright (C) 1993 Robert Nation
 * Copyright (C) 1993 Frank Fejes
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

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include "../../libAfterStep/wmprops.h"

/***********************************************************************
 *
 *  Procedure:
 *	HandlePropertyNotify - property notify event handler
 *
 ***********************************************************************/
#define MAX_NAME_LEN 200L				/* truncate to this many */
#define MAX_ICON_NAME_LEN 200L	/* ditto */

Bool update_transp_iter_func (void *data, void *aux_data)
{
	ASWindow *asw = (ASWindow *) data;

	if (!check_window_offscreen (asw))
		if (asw->internal && asw->internal->on_root_background_changed)
			asw->internal->on_root_background_changed (asw->internal);

	if (!check_frame_offscreen (asw))
		update_window_transparency (asw, True);
	return True;
}

static Bool check_wm_hints_changed (ASWindow * asw)
{
	unsigned char *ptr = (unsigned char *)&(asw->saved_wm_hints);
	XWMHints *tmp = XGetWMHints (dpy, asw->w);
	Bool changed = False;
	if (tmp == NULL) {
		int i;
		for (i = 0; i < sizeof (XWMHints); ++i)
			if (ptr[i] != 0)
				return True;
	} else {
		changed = (memcmp (tmp, ptr, sizeof (XWMHints)) != 0);
		XFree (tmp);
	}
	return changed;
}

static Bool check_wm_normal_hints_changed (ASWindow * asw)
{
	unsigned char *ptr = (unsigned char *)&(asw->saved_wm_normal_hints);
	XSizeHints *tmp = XAllocSizeHints ();
	unsigned char *ptr2 = (unsigned char *)tmp;
	Bool changed = False;
	long unused;
	int i;

	if (XGetWMNormalHints (dpy, asw->w, tmp, &unused) == 0) {
		for (i = 0; i < sizeof (XSizeHints); ++i)
			if (ptr[i] != 0) {
				changed = True;
				break;
			}
	} else
		for (i = 0; i < sizeof (XSizeHints); ++i)
			if (ptr[i] != ptr2[i]) {
				changed = True;
				break;
			}
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	if (changed) {
		LOCAL_DEBUG_OUT ("normal hints differ at offset %d", i);
		LOCAL_DEBUG_OUT ("Old hints : %s", "");
		print_wm_normal_hints (NULL, NULL, (XSizeHints *) ptr);
		LOCAL_DEBUG_OUT ("New hints : %s", "");
		print_wm_normal_hints (NULL, NULL, tmp);
	}
#endif
	XFree (tmp);
	return changed;
}

void HandlePropertyNotify (ASEvent * event)
{
	ASWindow *asw;
	XPropertyEvent *xprop = &(event->x.xproperty);
	Atom atom = xprop->atom;
	XEvent prop_xev;

	/* force updates for "transparent" windows */
	if (atom == _XROOTPMAP_ID && event->w == Scr.Root) {
		read_xrootpmap_id (Scr.wmprops, (xprop->state == PropertyDelete));
		if (Scr.RootImage) {

			safe_asimage_destroy (Scr.RootImage);
			Scr.RootImage = NULL;
		}
		if (Scr.RootBackground && Scr.RootBackground->im != NULL) {
			if (Scr.RootBackground->pmap
					&& Scr.wmprops->root_pixmap == Scr.RootBackground->pmap)
				Scr.RootImage = dup_asimage (Scr.RootBackground->im);
		}
		if (Scr.wmprops->as_root_pixmap != Scr.wmprops->root_pixmap)
			set_as_background (Scr.wmprops, Scr.wmprops->root_pixmap);

		iterate_asbidirlist (Scr.Windows->clients, update_transp_iter_func,
												 NULL, NULL, False);

		/* use move_menu() to update transparent menus; this is a kludge, but it works */
#if 0														/* reimplement menu redrawing : */
		if ((*Scr.MSMenuTitle).texture_type == 129
				|| (*Scr.MSMenuItem).texture_type == 129
				|| (*Scr.MSMenuHilite).texture_type == 129) {
			MenuRoot *menu;

			for (menu = Scr.first_menu; menu != NULL; menu = menu->next)
				if ((*menu).is_mapped)
					move_menu (menu, (*menu).x, (*menu).y);
		}
#endif
		return;
	}

	if ((asw = event->client) == NULL || ASWIN_GET_FLAGS (asw, AS_Dead)) {
		if (event->w != Scr.Root)
			while (XCheckTypedWindowEvent
						 (dpy, event->w, PropertyNotify, &prop_xev)) ;
		return;
	} else {
		char *prop_name = NULL;
		LOCAL_DEBUG_OUT ("property %s",
										 (prop_name = XGetAtomName (dpy, atom)));
		if (prop_name)
			XFree (prop_name);
	}
	if (IsNameProp (atom)) {
		char *old_name =
				get_flags (asw->internal_flags,
									 ASWF_NameChanged) ? NULL : mystrdup (ASWIN_NAME (asw));

		/* we want to check if there were some more events generated
		 * as window names tend to change multiple properties : */
		while (XCheckTypedWindowEvent (dpy, asw->w, PropertyNotify, &prop_xev))
			if (!IsNameProp (prop_xev.xproperty.atom)) {
				XPutBackEvent (dpy, &prop_xev);
				break;
			}

		/*ASFlagType old_hflags = asw->hints->flags ; */
		show_debug (__FILE__, __FUNCTION__, __LINE__, "name prop changed...");
		if (get_flags (Scr.Feel.flags, FollowTitleChanges))
			on_window_hints_changed (asw);
		else if (update_property_hints_manager (asw->w, xprop->atom,
																						Scr.Look.supported_hints,
																						Database,
																						asw->hints, asw->status)) {
			if (ASWIN_GET_FLAGS (asw, AS_Dead))
				return;
			show_debug (__FILE__, __FUNCTION__, __LINE__,
									"New name is \"%s\", icon_name \"%s\", following title change ? %s",
									ASWIN_NAME (asw), ASWIN_ICON_NAME (asw),
									get_flags (Scr.Feel.flags,
														 FollowTitleChanges) ? "yes" : "no");
			LOCAL_DEBUG_OUT ("hints flags = %lX, ShortLived ? %lX ",
											 asw->hints->flags, ASWIN_HFLAGS (asw,
																												AS_ShortLived));
			if (old_name && strcmp (old_name, ASWIN_NAME (asw)) != 0)
				set_flags (asw->internal_flags, ASWF_NameChanged);
			/* fix the name in the title bar */
			if (!ASWIN_GET_FLAGS (asw, AS_Iconic))
				on_window_title_changed (asw, True);
			broadcast_res_names (asw);
			broadcast_window_name (asw);
			broadcast_icon_name (asw);
		}
		if (old_name)
			free (old_name);
		LOCAL_DEBUG_OUT ("hints flags = %lX, ShortLived ? %lX ",
										 asw->hints->flags, ASWIN_HFLAGS (asw, AS_ShortLived));
#if (defined(LOCAL_DEBUG)||defined(DEBUG)) && defined(DEBUG_ALLOCS)
		{
			static time_t old_t = 0;
			time_t t = time (NULL);
			if (old_t < t) {
				char fname[256];
				sprintf (fname, "afterstep.allocs.name_change.%lu.log", t);
//          spool_unfreed_mem( fname, NULL );
				old_t = t;
			}
		}
#endif

		/* otherwise we should check if this is the status property that we change ourselves : */
	} else if (atom == XA_WM_COMMAND || atom == XA_WM_CLIENT_MACHINE) {
		update_cmd_line_hints (asw->w, atom, asw->hints, asw->status);
	} else if (atom == _XA_NET_WM_WINDOW_OPACITY) {
		if (event->context == C_WINDOW && event->w != asw->frame)
			on_window_opacity_changed (asw);
	} else if (atom == XA_WM_HINTS) {
		if (check_wm_hints_changed (asw))
			on_window_hints_changed (asw);
		else {
			LOCAL_DEBUG_OUT ("ignoring WM_HINTS change - data is the same%s",
											 "");
		}
	} else if (atom == XA_WM_NORMAL_HINTS) {
		if (check_wm_normal_hints_changed (asw))
			on_window_hints_changed (asw);
		else {
			LOCAL_DEBUG_OUT
					("ignoring WM_NORMAL_HINTS change - data is the same%s", "");
		}
	} else if (NeedToTrackPropChanges (atom))
		on_window_hints_changed (asw);

	/* we have to do the complete refresh of hints, since we have to override WH_HINTS with database, etc. */
}

/***********************************************************************
 *
 *  Procedure:
 *	HandleClientMessage - client message event handler
 *
 ************************************************************************/
void HandleClientMessage (ASEvent * event)
{
	char *aname = NULL;
	LOCAL_DEBUG_OUT ("ClientMessage(\"%s\")",
									 (aname =
										XGetAtomName (dpy, event->x.xclient.message_type)));
	if (aname != NULL)
		XFree (aname);

	if ((event->x.xclient.message_type == _XA_WM_CHANGE_STATE) &&
			(event->client) &&
			(event->x.xclient.data.l[0] == IconicState) &&
			!ASWIN_GET_FLAGS (event->client, AS_Iconic)) {
		set_window_wm_state (event->client, True, False);
#ifdef ENABLE_DND
		/* Pass the event to the client window */
		if (event->x.xclient.window != event->client->w) {
			event->x.xclient.window = event->client->w;
			XSendEvent (dpy, event->client->w, True, NoEventMask, &(event->x));
		}
#endif
	} else if (event->x.xclient.message_type == _AS_BACKGROUND) {
		HandleBackgroundRequest (event);
	} else if (event->x.xclient.message_type == _XA_NET_WM_STATE
						 && event->client != NULL) {
		ASFlagType extwm_flags = 0, as_flags = 0;
		CARD32 props[2];
		XClientMessageEvent *xcli = &(event->x.xclient);

		props[0] = xcli->data.l[1];
		props[1] = xcli->data.l[2];

		translate_atom_list (&extwm_flags, EXTWM_State, &props[0], 2);
		/* now we need to translate EXTWM flags into AS flags : */
		as_flags = extwm_state2as_state_flags (extwm_flags);
		if (xcli->data.l[0] == EXTWM_StateRemove) {
			as_flags = ASWIN_GET_FLAGS (event->client, as_flags);
		} else if (xcli->data.l[0] == EXTWM_StateAdd)
			as_flags = as_flags & (~ASWIN_GET_FLAGS (event->client, as_flags));

		if (props[0] == _XA_NET_WM_STATE_DEMANDS_ATTENTION || props[1] == _XA_NET_WM_STATE_DEMANDS_ATTENTION) {	/* requires special treatment as it competes with ICCCM HintUrgency in WM_HINTS */
			Bool set = True;
			if (xcli->data.l[0] == EXTWM_StateRemove ||
					(xcli->data.l[0] == EXTWM_StateToggle
					 && ASWIN_GET_FLAGS (event->client, AS_Urgent)))
				set = False;
			set_extwm_urgency_state (event->client->w, set);
		}

		if (as_flags != 0)
			toggle_aswindow_status (event->client, as_flags);
	} else if (event->x.xclient.message_type == _XA_NET_CURRENT_DESKTOP) {
		CARD32 desktop_idx, timestamp;
		XClientMessageEvent *xcli = &(event->x.xclient);

		desktop_idx = xcli->data.l[0];
		timestamp = xcli->data.l[1];

		if (desktop_idx < Scr.wmprops->as_desk_num && get_flags (AfterStepState, ASS_NormalOperation))
			ChangeDesks (Scr.wmprops->as_desk_numbers[desktop_idx]);
	}
}

void HandleSelectionClear (ASEvent * event)
{
	LOCAL_DEBUG_OUT
			("SelectionClearEvent : window = %lx, selection = %lx, time = %ld. our( %lx,%lx,%ld )",
			 event->x.xselectionclear.window, event->x.xselectionclear.selection,
			 event->x.xselectionclear.time, Scr.wmprops->selection_window,
			 Scr.wmprops->_XA_WM_S, Scr.wmprops->selection_time);
	if (event->x.xselectionclear.window == Scr.wmprops->selection_window
			&& event->x.xselectionclear.selection == Scr.wmprops->_XA_WM_S) {
		/* must give up window manager's selection if time of the event
		 * after time of us accuring the selection */
		if (event->x.xselectionclear.time > Scr.wmprops->selection_time)
			Done (False, NULL);
	}
}
