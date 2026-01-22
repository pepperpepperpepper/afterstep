/*
 * Copyright (c) 2000 Sasha Vasko <sashav@sprintmail.com>
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

#include "../../configure.h"

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include "asinternals.h"
#include "../../libAfterStep/wmprops.h"

/*
 * Find next window in circulate csequence forward (dir 1) or backward (dir -1)
 * from specifyed window. when we reach top or bottom we are turning back
 * checking AutoRestart here to determine what to do when we have warped through
 * all the windows, and came back to start.
 */

ASWindow *get_next_window (ASWindow * curr_win, char *action, int dir)
{
	int end_i, i;
	ASWindow **clients;

	if (Scr.Windows == NULL || curr_win == NULL)
		return NULL;

	end_i = VECTOR_USED (*(Scr.Windows->circulate_list));
	clients = VECTOR_HEAD (ASWindow *, *(Scr.Windows->circulate_list));

	if (end_i <= 1)
		return NULL;
	for (i = 0; i < end_i; ++i)
		if (clients[i] == curr_win) {
			if (i == 0 && dir < 0)
				return clients[end_i - 1];
			else if (i == end_i - 1 && dir > 0)
				return clients[0];
			else
				return clients[i + dir];
		}

	return NULL;
}

/********************************************************************
 * hides focus for the screen.
 **********************************************************************/
void unset_focused_window()
{
	if (Scr.Windows->focused) {
		if (Scr.Windows->focused->status) {
			ASWIN_CLEAR_FLAGS(Scr.Windows->focused, AS_Focused);
			set_client_state (Scr.Windows->focused->w, Scr.Windows->focused->status);
		}
		Scr.Windows->focused = NULL;
	}
}

void hide_focus ()
{
	if (get_flags (Scr.Feel.flags, ClickToFocus)
			&& Scr.Windows->ungrabbed != NULL)
		grab_aswindow_buttons (Scr.Windows->ungrabbed, False);

	LOCAL_DEBUG_CALLER_OUT ("CHANGE Scr.Windows->focused from %p to NULL",
													Scr.Windows->focused);

	unset_focused_window();
	Scr.Windows->ungrabbed = NULL;
	XRaiseWindow (dpy, Scr.ServiceWin);
	LOCAL_DEBUG_OUT ("XSetInputFocus(window= %lX (service_win), time = %lu)",
									 Scr.ServiceWin, Scr.last_Timestamp);
	XSetInputFocus (dpy, Scr.ServiceWin, RevertToParent, Scr.last_Timestamp);
	XSync (dpy, False);
}

/********************************************************************
 * Sets the input focus to the indicated window.
 **********************************************************************/
void commit_circulation ()
{
	ASWindow *asw = Scr.Windows->active;
	LOCAL_DEBUG_OUT ("circulation completed with active window being %p",
									 asw);
	if (asw)
		if (vector_remove_elem (Scr.Windows->circulate_list, &asw) == 1) {
			LOCAL_DEBUG_OUT
					("reinserting %p into the head of circulation list : ", asw);
			vector_insert_elem (Scr.Windows->circulate_list, &asw, 1, NULL,
													True);
		}
	Scr.Windows->warp_curr_index = -1;
}

void autoraise_aswindow (void *data)
{
	struct timeval tv;
	time_t msec = Scr.Feel.AutoRaiseDelay;
	time_t exp_sec =
			Scr.Windows->last_focus_change_sec + (msec * 1000 +
																						Scr.Windows->
																						last_focus_change_usec) /
			1000000;
	time_t exp_usec =
			(msec * 1000 + Scr.Windows->last_focus_change_usec) % 1000000;

	if (Scr.Windows->focused
			&& !get_flags (AfterStepState, ASS_HousekeepingMode)) {
		gettimeofday (&tv, NULL);
		if (exp_sec < tv.tv_sec ||
				(exp_sec == tv.tv_sec && exp_usec <= tv.tv_usec)) {
/*fprintf (stderr, "Stacking order : Autoraise ptr = %p\n", Scr.Windows->focused);
*/
			RaiseObscuredWindow (Scr.Windows->focused);
		}
	}
}

Bool focus_window (ASWindow * asw, Window w)
{
	LOCAL_DEBUG_CALLER_OUT ("asw = %p, w = %lX", asw, w);

	if (asw != NULL)
		if (get_flags (asw->hints->protocols, AS_DoesWmTakeFocus)
				&& !ASWIN_GET_FLAGS (asw, AS_Dead))
			send_wm_protocol_request (asw->w, _XA_WM_TAKE_FOCUS,
																Scr.last_Timestamp);

	ASSync (False);
	LOCAL_DEBUG_OUT ("focusing window %lX, client %lX, frame %lX, asw %p", w,
									 asw->w, asw->frame, asw);
	/* using last_Timestamp here causes problems when moving between screens */
	/* at the same time using CurrentTime all the time seems to cause some apps to fail,
	 * most noticeably GTK-perl
	 *
	 * Take 2: disabled CurrentTime altogether as it screwes up focus handling
	 * Basically if you use CurrentTime when there are still bunch of Events
	 * in the queue, those evens will not have any effect if you try setting
	 * focus using their time, as X aready used its own friggin current time.
	 * Don't ask, its a mess.
	 * */
	if (w != None && ASWIN_HFLAGS (asw, AS_AcceptsFocus)) {
		Time t =
				/*(Scr.Windows->focused == NULL)?CurrentTime: */
				Scr.last_Timestamp;
		LOCAL_DEBUG_OUT ("XSetInputFocus(window= %lX, time = %lu)", w, t);
		XSetInputFocus (dpy, w, RevertToParent, t);
	}

	ASSync (False);
	return (w != None);
}

void autoraise_window (ASWindow * asw)
{
	if (Scr.Feel.AutoRaiseDelay == 0) {
/*fprintf (stderr, "Stacking order : Autoraise ptr = %p\n", asw);
*/
		RaiseWindow (asw);
	} else if (Scr.Feel.AutoRaiseDelay > 0) {
		struct timeval tv;
		LOCAL_DEBUG_OUT ("setting autoraise timer for asw %p",
										 Scr.Windows->focused);
		gettimeofday (&tv, NULL);
		Scr.Windows->last_focus_change_sec = tv.tv_sec;
		Scr.Windows->last_focus_change_usec = tv.tv_usec;
		timer_new (Scr.Feel.AutoRaiseDelay, autoraise_aswindow,
							 Scr.Windows->focused);
	}

}

Bool focus_aswindow (ASWindow * asw, Bool suppress_autoraise)
{
	Bool do_hide_focus = False;
	Bool do_nothing = False;
	Window w = None;

	LOCAL_DEBUG_CALLER_OUT ("asw = %p", asw);
	if (asw) {
		if (!get_flags (AfterStepState, ASS_WarpingMode))
			if (vector_remove_elem (Scr.Windows->circulate_list, &asw) == 1)
				vector_insert_elem (Scr.Windows->circulate_list, &asw, 1, NULL,
														True);

#if 0
		/* ClickToFocus focus queue manipulation */
		if (asw != Scr.Focus)
			asw->focus_sequence = Scr.next_focus_sequence++;
#endif
		do_hide_focus = (ASWIN_DESK (asw) != Scr.CurrentDesk) ||
				(ASWIN_GET_FLAGS (asw, AS_Iconic) &&
				 asw->icon_canvas == NULL && asw->icon_title_canvas == NULL);

		if (!ASWIN_FOCUSABLE (asw)) {
			if (Scr.Windows->focused != NULL
					&& ASWIN_DESK (Scr.Windows->focused) == Scr.CurrentDesk)
				do_nothing = True;
			else
				do_hide_focus = True;
		}
	} else
		do_hide_focus = True;

	if (Scr.NumberOfScreens > 1 && !do_hide_focus) {
		Window pointer_root;
		/* if pointer went onto another screen - we need to release focus
		 * and let other screen's manager manage it from now on, untill
		 * pointer comes back to our screen :*/
		ASQueryPointerRoot (&pointer_root, &w);
		if (pointer_root != Scr.Root) {
			do_hide_focus = True;
			do_nothing = False;
		}
	}
	if (!do_nothing && do_hide_focus)
		hide_focus ();
	if (do_nothing || do_hide_focus)
		return False;

	if (get_flags (Scr.Feel.flags, ClickToFocus) && Scr.Windows->ungrabbed != asw) {	/* need to grab all buttons for window that we are about to
																																										 * unfocus */
		grab_aswindow_buttons (Scr.Windows->ungrabbed, False);
		grab_aswindow_buttons (asw, True);
		Scr.Windows->ungrabbed = asw;
	}

	if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {	/* focus icon window or icon title of the iconic window */
		if (asw->icon_canvas && !ASWIN_GET_FLAGS (asw, AS_Dead)
				&& validate_drawable (asw->icon_canvas->w, NULL, NULL) != None)
			w = asw->icon_canvas->w;
		else if (asw->icon_title_canvas)
			w = asw->icon_title_canvas->w;
	} else if (ASWIN_GET_FLAGS (asw, AS_Shaded)) {	/* focus frame window of shaded clients */
		w = asw->frame;
	} else if (!ASWIN_GET_FLAGS (asw, AS_Dead)) {	/* clients with visible top window can get focus directly:  */
		w = asw->w;
	}

	if (w == None)
		show_warning ("unable to focus window %lX for client %lX, frame %lX",
									w, asw->w, asw->frame);
	else if (!ASWIN_GET_FLAGS (asw, AS_Mapped))
		show_warning
				("unable to focus unmapped window %lX for client %lX, frame %lX",
				 w, asw->w, asw->frame);
	else if (ASWIN_GET_FLAGS (asw, AS_UnMapPending))
		show_warning
				("unable to focus window %lX that is about to be unmapped for client %lX, frame %lX",
				 w, asw->w, asw->frame);
	else {
		focus_window (asw, w);

		LOCAL_DEBUG_CALLER_OUT ("CHANGE Scr.Windows->focused from %p to %p",
														Scr.Windows->focused, asw);
		unset_focused_window();
		Scr.Windows->focused = asw;
		ASWIN_SET_FLAGS(asw, AS_Focused);
		set_client_state (asw->w, asw->status);

		if (!suppress_autoraise)
			autoraise_window (asw);
	}

	XSync (dpy, False);
	return True;
}

/*********************************************************************/
/* focus management goes here :                                      */
/*********************************************************************/
/* making window active : */
/* handing over actuall focus : */
Bool focus_active_window ()
{
	/* don't fiddle with focus if we are in housekeeping mode !!! */
	LOCAL_DEBUG_CALLER_OUT ("checking if we are in housekeeping mode (%ld)",
													get_flags (AfterStepState,
																		 ASS_HousekeepingMode));
	if (get_flags (AfterStepState, ASS_HousekeepingMode)
			|| Scr.Windows->active == NULL)
		return False;

	if (Scr.Windows->focused == Scr.Windows->active)
		return True;								/* already has focus */

	return focus_aswindow (Scr.Windows->active, FOCUS_ASW_CAN_AUTORAISE);
}

/* second version of above : */
void focus_next_aswindow (ASWindow * asw)
{
	ASWindow *new_focus = NULL;

	if (get_flags (Scr.Feel.flags, ClickToFocus))
		new_focus = get_next_window (asw, NULL, 1);
	if (!activate_aswindow (new_focus, False, False))
		hide_focus ();
}

void focus_prev_aswindow (ASWindow * asw)
{
	ASWindow *new_focus = NULL;

	if (get_flags (Scr.Feel.flags, ClickToFocus))
		new_focus = get_next_window (asw, NULL, -1);
	if (!activate_aswindow (new_focus, False, False))
		hide_focus ();
}

void warp_to_aswindow (ASWindow * asw, Bool deiconify)
{
	if (asw)
		activate_aswindow (asw, True, deiconify);
}

/*************************************************************************/
/* end of the focus management                                           */
/*************************************************************************/


/*********************************************************************************
 * Find next window in circulate csequence forward (dir 1) or backward (dir -1)
 * from specifyed window. when we reach top or bottom we are turning back
 * checking AutoRestart here to determine what to do when we have warped through
 * all the windows, and came back to start.
 *********************************************************************************/
ASWindow *warp_aswindow_list (ASWindowList * list, Bool backwards)
{
	register int i;
	register int dir = backwards ? -1 : 1;
	int end_i;
	ASWindow **clients;
	int loop_count = 0;

	if (list == NULL)
		return NULL;

	end_i = VECTOR_USED (*(list->circulate_list));
	clients = VECTOR_HEAD (ASWindow *, *(list->circulate_list));

	if (end_i <= 1)
		return NULL;

	if (list->warp_curr_index < 0) {	/* need to initialize warping : */
		list->warp_curr_index = (dir > 0) ? 0 : end_i;
		list->warp_user_dir = dir;
		list->warp_init_dir = dir;
		list->warp_curr_dir = dir;
	} else if (dir == list->warp_user_dir) {
		dir = list->warp_curr_dir;
	} else {
		list->warp_user_dir = dir;
		/* user reversed direction - so do we : */
		dir = (list->warp_curr_dir > 0) ? -1 : 1;
		list->warp_curr_dir = dir;
	}

	i = (dir > 0) ? 1 : end_i - 1;	/*list->warp_curr_index + dir */
	do {
		LOCAL_DEBUG_OUT ("checking i(%d)->end_i(%d)->dir(%d)->AutoReverse(%d)",
										 i, end_i, dir, Scr.Feel.AutoReverse);
		if (0 > i || i >= end_i) {
			if (Scr.Feel.AutoReverse == AST_OpenLoop)
				i = (dir < 0) ? end_i - 1 : 0;
			else if (Scr.Feel.AutoReverse == AST_ClosedLoop) {
				i = (dir < 0) ? 0 : end_i - 1;
				list->warp_curr_dir = dir = (dir < 0) ? 1 : -1;
				i += dir;								/* we need to skip the one that was focused at the moment ! */
			} else
				return NULL;
			if (++loop_count >= 2)
				return NULL;
		}

		list->warp_curr_index = i;
		if (!(ASWIN_HFLAGS (clients[i], AS_DontCirculate)) &&
				!(ASWIN_GET_FLAGS (clients[i], AS_Iconic)
					&& get_flags (Scr.Feel.flags, CirculateSkipIcons))
				&& (ASWIN_DESK (clients[i]) == Scr.CurrentDesk
						|| get_flags (Scr.Feel.flags, AutoTabThroughDesks))) {
			return clients[i];
		}
		i += dir;
	} while (1);
	return NULL;
}

