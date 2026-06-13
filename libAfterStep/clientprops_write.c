/*
 * Copyright (C) 2000 Sasha Vasko <sasha at aftercode.net>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#define LOCAL_DEBUG
#include "../configure.h"
#include "asapp.h"
#include "screen.h"
#include "clientprops.h"
#include "hints.h"
#include "clientprops_internal.h"

/* EXTWM state-flag queries and the client/EXTWM/GNOME hint setters, split
 * out of clientprops.c. Uses the shared AtomXref tables via
 * clientprops_internal.h. */

Bool get_extwm_state_flags (Window w, ASFlagType * flags)
{
	if (flags && w != None) {
		CARD32 *protocols;
		long nprotos = 0;

		if (read_32bit_proplist
				(w, _XA_NET_WM_STATE, MAX_NET_WM_STATES, &protocols, &nprotos)) {
/*			LOCAL_DEBUG_OUT( "natoms =  %ld", nprotos ); */
			translate_atom_list (flags, EXTWM_State, protocols, nprotos);
			free (protocols);
			return True;
		}
	}
	return False;
}



/**********************************************************************************/
/**********************************************************************************/
/***************** Setting property values here  : ********************************/
/**********************************************************************************/

/****************************************************************************
 * This is used to tell applications which windows on the screen are
 * top level appication windows, and which windows are the icon windows
 * that go with them.
 ****************************************************************************/
void set_client_state (Window w, struct ASStatusHints *status)
{
	LOCAL_DEBUG_CALLER_OUT ("w = %lX, status->flags = 0x%lX", w,
													status ? status->flags : 0);
	if (w != None) {
		if (status != NULL) {
			CARD32 extwm_states[MAX_NET_WM_STATES];
			long used = 0;
			CARD32 gnome_state = 0;
			ASFlagType old_state = 0;

			if (get_flags (status->flags, AS_Sticky)) {
				extwm_states[used++] = _XA_NET_WM_STATE_STICKY;
				gnome_state |= WIN_STATE_STICKY;
			}
			if (get_flags (status->flags, AS_Shaded)) {
				extwm_states[used++] = _XA_NET_WM_STATE_SHADED;
				gnome_state |= WIN_STATE_SHADED;
			}
			if (get_flags (status->flags, AS_Hidden)) {
				extwm_states[used++] = _XA_NET_WM_STATE_HIDDEN;
				gnome_state |= WIN_STATE_HIDDEN;
			}
			if (get_flags (status->flags, AS_MaximizedX)) {
				extwm_states[used++] = _XA_NET_WM_STATE_MAXIMIZED_HORZ;
				gnome_state |= WIN_STATE_MAXIMIZED_HORIZ;
			}
			if (get_flags (status->flags, AS_MaximizedY)) {
				extwm_states[used++] = _XA_NET_WM_STATE_MAXIMIZED_VERT;
				gnome_state |= WIN_STATE_MAXIMIZED_VERT;
			}
			if (get_flags (status->flags, AS_Focused)) {
				extwm_states[used++] = _XA_NET_WM_STATE_FOCUSED;
			}
/*			if (get_flags (status->flags, AS_Urgent))
			{
				extwm_states[used++] = _XA_NET_WM_STATE_DEMANDS_ATTENTION;
			}
 */

/*			LOCAL_DEBUG_OUT( "window %lX used =  %ld", w, used ); */
			if (get_extwm_state_flags (w, &old_state)) {
				if (get_flags (old_state, EXTWM_StateModal))
					extwm_states[used++] = _XA_NET_WM_STATE_MODAL;
				if (get_flags (old_state, EXTWM_StateSkipTaskbar))
					extwm_states[used++] = _XA_NET_WM_STATE_SKIP_TASKBAR;
				if (get_flags (old_state, EXTWM_StateSkipPager))
					extwm_states[used++] = _XA_NET_WM_STATE_SKIP_PAGER;
				if (get_flags (old_state, EXTWM_StateDemandsAttention))
					extwm_states[used++] = _XA_NET_WM_STATE_DEMANDS_ATTENTION;
				if (get_flags (old_state, EXTWM_StateHidden))
					extwm_states[used++] = _XA_NET_WM_STATE_HIDDEN;
			}
			LOCAL_DEBUG_OUT ("window %lX old_extwm_state = 0x%lX, used =  %ld",
											 w, old_state, used);

			if (used == 0) {
				XDeleteProperty (dpy, w, _XA_NET_WM_STATE);
				XDeleteProperty (dpy, w, _XA_WIN_STATE);
			} else {
				set_32bit_proplist (w, _XA_NET_WM_STATE, XA_ATOM,
														&(extwm_states[0]), used);
				set_32bit_property (w, _XA_WIN_STATE, XA_CARDINAL, gnome_state);
			}

			if (get_flags (status->flags, AS_Layer))
				set_32bit_property (w, _XA_WIN_LAYER, XA_CARDINAL, status->layer);
		}
	}
}

void set_extwm_urgency_state (Window w, Bool set)
{
	LOCAL_DEBUG_CALLER_OUT ("w = %lX, set = %d", w, set);
	if (w != None) {
		CARD32 *states = NULL;
		long nstates = 0;
		int i;
		Bool changed = False;

		read_32bit_proplist (w, _XA_NET_WM_STATE, MAX_NET_WM_STATES, &states,
												 &nstates);

		for (i = 0; i < nstates; ++i)
			if (states[i] == _XA_NET_WM_STATE_DEMANDS_ATTENTION)
				break;
		if (set && i >= nstates) {
			++nstates;
			states = realloc (states, sizeof (CARD32) * nstates);
			states[nstates - 1] = _XA_NET_WM_STATE_DEMANDS_ATTENTION;
			changed = True;
		} else if (!set && i < nstates) {
			while (++i < nstates)
				states[i - 1] = states[i];
			--nstates;
			changed = True;
		}
		if (changed) {
			if (nstates == 0)
				XDeleteProperty (dpy, w, _XA_NET_WM_STATE);
			else
				set_32bit_proplist (w, _XA_NET_WM_STATE, XA_ATOM, &states[0],
														nstates);
		}
		free (states);
	}
}



void set_client_desktop (Window w, int ext_desk)
{
	if (w) {
		if (ext_desk >= 0) {
			set_32bit_property (w, _XA_WIN_WORKSPACE, XA_CARDINAL, ext_desk);
			set_32bit_property (w, _XA_NET_WM_DESKTOP, XA_CARDINAL, ext_desk);
		}
	}
}

void
set_client_names (Window w, char *name, char *icon_name, char *res_class,
									char *res_name)
{
	if (w) {
		if (name) {
			set_text_property (w, _XA_WM_NAME, &name, 1, TPE_String);
			/* need to convert string to UTF8 */
			set_text_property (w, _XA_NET_WM_NAME, &name, 1, TPE_UTF8);
		}
		if (icon_name) {
			set_text_property (w, _XA_WM_ICON_NAME, &icon_name, 1, TPE_String);
			/* need to convert string to UTF8 */
			set_text_property (w, _XA_NET_WM_ICON_NAME, &icon_name, 1, TPE_UTF8);
		}
		if (res_class || res_name) {
			XClassHint *class_hint = XAllocClassHint ();

			if (class_hint) {
				class_hint->res_class = res_class;
				class_hint->res_name = res_name;
				XSetClassHint (dpy, w, class_hint);
				XFree (class_hint);
			}
		}
	}
}

void
set_client_protocols (Window w, ASFlagType protocols,
											ASFlagType extwm_protocols)
{
	LOCAL_DEBUG_OUT ("protocols=0x%lX", protocols);
	if (w && protocols) {
		CARD32 *list = NULL, *extwm_list = NULL;
		long nitems, extwm_nitems;

		encode_atom_list (&(WM_Protocols[0]), &list, &nitems, protocols);
		encode_atom_list (&(EXTWM_Protocols[0]), &extwm_list, &extwm_nitems,
											extwm_protocols);

		LOCAL_DEBUG_OUT ("nitems=%ld, extwm_nitems = %ld", nitems,
										 extwm_nitems);
		if (extwm_nitems > 0) {
			int i;

			list = realloc (list, sizeof (CARD32) * (nitems + extwm_nitems));
			for (i = 0; i < extwm_nitems; ++i)
				list[nitems + i] = extwm_list[i];
			free (extwm_list);
			nitems += extwm_nitems;
		}

		if (nitems > 0 && list)
			set_32bit_proplist (w, _XA_WM_PROTOCOLS, XA_ATOM, list, nitems);
		if (list)
			free (list);
	}
}

void set_extwm_hints (Window w, ExtendedWMHints * extwm_hints)
{
	if (w && extwm_hints) {
		CARD32 *list;
		long nitems;

		if (get_flags (extwm_hints->flags, EXTWM_TypeSet)) {
			encode_atom_list (&(EXTWM_WindowType[0]), &list, &nitems,
												extwm_hints->type_flags);
			if (nitems > 0) {
				set_32bit_proplist (w, _XA_NET_WM_WINDOW_TYPE, XA_ATOM, list,
														nitems);
				free (list);
			}
		}
		if (get_flags (extwm_hints->flags, EXTWM_StateSet)) {
			encode_atom_list (&(EXTWM_State[0]), &list, &nitems,
												extwm_hints->state_flags);
			if (nitems > 0) {
				set_32bit_proplist (w, _XA_NET_WM_STATE, XA_CARDINAL, list,
														nitems);
				free (list);
				list = NULL;
			}
		}
		if (get_flags (extwm_hints->flags, EXTWM_DESKTOP))
			set_32bit_property (w, _XA_NET_WM_DESKTOP, XA_CARDINAL,
													extwm_hints->desktop);
		if (get_flags (extwm_hints->flags, EXTWM_PID))
			set_32bit_property (w, _XA_NET_WM_PID, XA_CARDINAL,
													extwm_hints->pid);
		if (get_flags (extwm_hints->flags, EXTWM_WINDOW_OPACITY))
			set_32bit_property (w, _XA_NET_WM_WINDOW_OPACITY, XA_CARDINAL,
													extwm_hints->window_opacity);
		if (get_flags (extwm_hints->flags, EXTWM_ICON))
			set_32bit_proplist (w, _XA_NET_WM_ICON, XA_CARDINAL,
													extwm_hints->icon, extwm_hints->icon_length);
	}
}

void set_gnome_hints (Window w, GnomeHints * gnome_hints)
{

}

void
set_client_hints (Window w, XWMHints * hints, XSizeHints * size_hints,
									ASFlagType protocols, ExtendedWMHints * extwm_hints)
{
	if (w) {
		if (hints)
			XSetWMHints (dpy, w, hints);
		if (size_hints)
			XSetWMNormalHints (dpy, w, size_hints);
		if (protocols)
			set_client_protocols (w, protocols, extwm_hints->flags);

		if (extwm_hints)
			set_extwm_hints (w, extwm_hints);
	}
}

void set_client_cmd (Window w)
{
	if (w != None && MyArgsPtr->saved_argv && MyArgsPtr->saved_argc > 0) {
		XSetCommand (dpy, w, MyArgsPtr->saved_argv, MyArgsPtr->saved_argc);
	}
}

/***************************************************************************
 * ICCCM Client Messages - Section 4.2.8 of the ICCCM dictates that all
 * client messages will have the following form:
 *
 *     event type	ClientMessage
 *     message type	_XA_WM_PROTOCOLS
 *     window       client window
 *     format		32
 *     data[0]		message atom
 *     data[1]		time stamp
 ****************************************************************************/
void send_wm_protocol_request (Window w, Atom request, Time timestamp)
{
	XClientMessageEvent ev;

	ev.type = ClientMessage;
	ev.window = w;
	ev.message_type = _XA_WM_PROTOCOLS;
	ev.format = 32;
	ev.data.l[0] = request;
	ev.data.l[1] = timestamp;
	XSendEvent (dpy, w, False, 0, (XEvent *) & ev);
}
