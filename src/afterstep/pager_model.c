/*
 * Copyright (c) 2003 Sasha Vasko <sasha@ aftercode.net>
 * Copyright (c) 1999 Ethan Fischer <allanon@crystaltokyo.com>
 * Copyright (C) 1993 Robert Nation
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

/***********************************************************************
 *
 * afterstep pager handling code
 *
 ***********************************************************************/

#define LOCAL_DEBUG
#include "../../configure.h"

#include "asinternals.h"

#include <stdlib.h>
#include <unistd.h>
#include "../../libAfterStep/wmprops.h"
#include "../../libAfterStep/session.h"
#include "../../libAfterStep/moveresize.h"


/***************************************************************************
 *
 *  Moves the viewport within the virtual desktop
 *
 ***************************************************************************/

Bool viewport_aswindow_iter_func (void *data, void *aux_data)
{
	ASWindow *asw = (ASWindow *) data;
	if (asw) {
		asw->status->viewport_x = Scr.Vx;
		asw->status->viewport_y = Scr.Vy;
		if (!ASWIN_GET_FLAGS (asw, AS_Sticky))
			on_window_status_changed (asw, True);

		if (ASWIN_GET_FLAGS (asw, AS_Iconic) && get_flags (Scr.Feel.flags, StickyIcons)) {	/* we must update let all the modules know that icon viewport has changed
																																												 * we dont have to do that for Sticky non-iconified windows, since its assumed
																																												 * that those follow viewport, while Icons only do that when StckiIcons is set. */
			broadcast_config (M_CONFIGURE_WINDOW, asw);
		}
	}
	return True;
}


void MoveViewport (int newx, int newy, Bool grab)
{

	ChangeDeskAndViewport (Scr.CurrentDesk, newx, newy, grab);

#if 0
#ifndef NO_VIRTUAL
	int deltax, deltay;

	LOCAL_DEBUG_CALLER_OUT ("new(%+d%+d), old(%+d%+d), max(%+d,%+d)", newx,
													newy, Scr.Vx, Scr.Vy, Scr.VxMax, Scr.VyMax);

	if (newx > Scr.VxMax)
		newx = Scr.VxMax;
	if (newy > Scr.VyMax)
		newy = Scr.VyMax;
	if (newx < 0)
		newx = 0;
	if (newy < 0)
		newy = 0;

	deltay = Scr.Vy - newy;
	deltax = Scr.Vx - newx;

	Scr.Vx = newx;
	Scr.Vy = newy;
	SendPacket (-1, M_NEW_PAGE, 3, Scr.Vx, Scr.Vy, Scr.CurrentDesk);
	LOCAL_DEBUG_OUT ("Updating viewport property to %d, %d", Scr.Vx, Scr.Vy);
	set_current_viewport_prop (Scr.wmprops, Scr.Vx, Scr.Vy,
														 get_flags (AfterStepState,
																				ASS_NormalOperation));

	if (deltax || deltay) {
		if (grab)
			grab_server ();
		/* traverse window list and redo the titlebar/buttons if necessary */
		iterate_asbidirlist (Scr.Windows->clients, viewport_aswindow_iter_func,
												 NULL, NULL, False);
		/* TODO: autoplace sticky icons so they don't wind up over a stationary icon */
		check_screen_panframes (ASDefaultScr);
		if (grab)
			ungrab_server ();
	}
#endif
#endif
}

/**************************************************************************
 * Move to a new desktop
 *************************************************************************/
Bool deskviewport_aswindow_iter_func (void *data, void *aux_data)
{
	ASWindow *asw = (ASWindow *) data;
	int new_desk = (int)Scr.CurrentDesk;
	int dvx = asw->status->viewport_x - Scr.Vx;
	int dvy = asw->status->viewport_y - Scr.Vy;
	union {
		void *ptr;
		int id;
	} old_desk;
	old_desk.ptr = aux_data;

	asw->status->viewport_x = Scr.Vx;
	asw->status->viewport_y = Scr.Vy;

	if (ASWIN_GET_FLAGS (asw, AS_Sticky) || (ASWIN_GET_FLAGS (asw, AS_Iconic) && get_flags (Scr.Feel.flags, StickyIcons))) {	/* Window is sticky */
		if (ASWIN_DESK (asw) != new_desk && IsValidDesk (new_desk)) {
			ASWIN_DESK (asw) = new_desk;
			if (!ASWIN_GET_FLAGS (asw, AS_Dead))
				set_client_desktop (asw->w, as_desk2ext_desk_safe(new_desk));
			broadcast_config (M_CONFIGURE_WINDOW, asw);
		} else if (ASWIN_GET_FLAGS (asw, AS_Iconic) && get_flags (Scr.Feel.flags, StickyIcons)) {	/* we must update let all the modules know that icon viewport has changed
																																															 * we dont have to do that for Sticky non-iconified windows, since its assumed
																																															 * that those follow viewport, while Icons only do that when StckiIcons is set. */
			broadcast_config (M_CONFIGURE_WINDOW, asw);
		}
	} else {
		if (old_desk.id != new_desk) {
			Window dest =
					(ASWIN_DESK (asw) == new_desk) ? Scr.Root : Scr.ServiceWin;
			quietly_reparent_aswindow (asw, dest, True);
		}
		update_window_frame_pos (asw);
		if (dvx != 0 || dvy != 0 || old_desk.id != new_desk)
			on_window_status_changed (asw, True);
	}
	display_progress (False, ".");
	return True;
}

Bool count_desk_client_iter_func (void *data, void *aux_data)
{
	int *pcount = (int *)aux_data;
	ASWindow *asw = (ASWindow *) data;

	if (!ASWIN_GET_FLAGS (asw, AS_Sticky) &&
			(!ASWIN_GET_FLAGS (asw, AS_Iconic)
			 || !get_flags (Scr.Feel.flags, StickyIcons))) {
		if (ASWIN_DESK (asw) == Scr.CurrentDesk)
			++(*pcount);
	}
	return True;
}

Bool update_desk_prop_iter_func (void *data, void *aux_data)
{
	ASWindow *asw = (ASWindow *) data;
	if (asw != NULL && !ASWIN_GET_FLAGS (asw, AS_Dead)) {
		int ext_desk = as_desk2ext_desk (Scr.wmprops, ASWIN_DESK(asw));
		if (ext_desk != INVALID_DESKTOP_PROP)
			set_client_desktop (asw->w, ext_desk);
	}
	return True;
}

int as_desk2ext_desk_safe (int as_desk)
{
	int ext_desk = as_desk2ext_desk (Scr.wmprops, as_desk);
	if (ext_desk == INVALID_DESKTOP_PROP) {
		set_desktop_num_prop (Scr.wmprops, as_desk, Scr.Root, True);
		ext_desk = as_desk2ext_desk (Scr.wmprops, as_desk);
		/* now we need to update NET_WM_DESKTOP properties on all clients to keep it in sync */
		iterate_asbidirlist (Scr.Windows->clients, update_desk_prop_iter_func, NULL, NULL, False);
	}
	return ext_desk;
}


void
ChangeDeskAndViewport (int new_desk, int new_vx, int new_vy,
											 Bool force_grab)
{
	int dvx, dvy;
	int old_desk = Scr.CurrentDesk;
	Bool desk_covered = False;

	LOCAL_DEBUG_CALLER_OUT ("new(%d%+d%+d), old(%d%+d%+d), max(%+d,%+d)",
													new_desk, new_vx, new_vy, Scr.CurrentDesk,
													Scr.Vx, Scr.Vy, Scr.VxMax, Scr.VyMax);

	if (new_vx > Scr.VxMax)
		new_vx = Scr.VxMax;
	else if (new_vx < 0)
		new_vx = 0;
	if (new_vy > Scr.VyMax)
		new_vy = Scr.VyMax;
	else if (new_vy < 0)
		new_vy = 0;

	dvx = Scr.Vx - new_vx;
	dvy = Scr.Vy - new_vy;

	if (IsValidDesk (old_desk))
		Scr.LastValidDesk = old_desk;

	/* we have to handle all the pending ConfigureNotifys here : */
	ConfigureNotifyLoop ();

	if (old_desk != new_desk && IsValidDesk (old_desk)) {
		int client_count = 0;
		if (get_flags (AfterStepState, ASS_NormalOperation)
				&& get_flags (Scr.Feel.flags, AnimateDeskChange)) {
			cover_desktop ();
			desk_covered = True;
		}

		iterate_asbidirlist (Scr.Windows->clients, count_desk_client_iter_func,
												 (void *)&client_count, NULL, False);
		if (client_count != 0)
			old_desk = INVALID_DESK;
		force_grab = True;
	}

	if (Scr.CurrentDesk != new_desk) {
		Scr.CurrentDesk = new_desk;
		as_desk2ext_desk_safe (new_desk);
		set_current_desk_prop (Scr.wmprops, new_desk);
	}

	Scr.Vx = new_vx;
	Scr.Vy = new_vy;

	if (Scr.CurrentDesk != new_desk || dvx != 0 || dvy != 0)
		set_current_viewport_prop (Scr.wmprops, Scr.Vx, Scr.Vy,
															 get_flags (AfterStepState,
																					ASS_NormalOperation));

	display_progress (True, "Notifying modules of desk/viewport change ...");

	SendPacket (-1, M_NEW_DESKVIEWPORT, 3, Scr.Vx, Scr.Vy, Scr.CurrentDesk);

	if (dvx != 0 || dvy != 0 || old_desk != new_desk) {
		union {
			void *ptr;
			int id;
		} desk_id;

		display_progress (True,
											"Moving off-desktop windows back to desktop #%d ...",
											new_desk);
		if (force_grab)
			grab_server ();

		if (Scr.moveresize_in_progress) {
			LOCAL_DEBUG_OUT
					("adjusting current viewport of move/resize from %+d%+d to %+d%+d",
					 Scr.moveresize_in_progress->grid->curr_vx,
					 Scr.moveresize_in_progress->grid->curr_vy, Scr.Vx, Scr.Vy);

			Scr.moveresize_in_progress->grid->curr_vx = Scr.Vx;
			Scr.moveresize_in_progress->grid->curr_vy = Scr.Vy;
			if (!Scr.moveresize_in_progress->move_only) {
				ASWindow *asw =
						window2ASWindow (AS_WIDGET_WINDOW
														 (Scr.moveresize_in_progress->mr));
				if (!get_flags (asw->status->flags, AS_Sticky)) {
					Scr.moveresize_in_progress->last_x += dvx;
					Scr.moveresize_in_progress->last_y += dvy;
					Scr.moveresize_in_progress->curr.x += dvx;
					Scr.moveresize_in_progress->curr.y += dvy;
				}
			}
		}

		desk_id.id = old_desk;
		/* traverse window list and redo the titlebar/buttons if necessary */
		iterate_asbidirlist (Scr.Windows->clients,
												 deskviewport_aswindow_iter_func, desk_id.ptr,
												 NULL, False);
		/* TODO: autoplace sticky icons so they don't wind up over a stationary icon */
		check_screen_panframes (ASDefaultScr);
		if (force_grab)
			ungrab_server ();
	}
	/* yield to let modules handle desktop/viewport change */
	FlushAllQueues ();

	if (old_desk != new_desk) {
		if (get_flags (Scr.Feel.flags, ClickToFocus)) {
			int i;
			int circ_count = VECTOR_USED (*(Scr.Windows->circulate_list));
			if (!get_flags (Scr.Feel.flags, DontRestoreFocus)) {
				ASWindow **circ_list =
						VECTOR_HEAD (ASWindow *, *(Scr.Windows->circulate_list));
				for (i = 0; i < circ_count; ++i)
					if (circ_list[i] != NULL
							&& circ_list[i]->magic == MAGIC_ASWINDOW)
						if (ASWIN_DESK (circ_list[i]) == new_desk) {
							focus_aswindow (circ_list[i], FOCUS_ASW_CAN_AUTORAISE);
							break;
						}
			} else
				i = circ_count;

			if (i >= circ_count)
				hide_focus ();
		}

		if (IsValidDesk (new_desk)) {
			apply_stacking_order (new_desk);
			send_stacking_order (new_desk);
			FlushAllQueues ();				/* yield to modules */
		}
		/* Change the look to this desktop's one if it really changed */
#ifdef DIFFERENTLOOKNFEELFOREACHDESKTOP
		if (get_flags (AfterStepState, ASS_NormalOperation))
			QuickRestart ("look&feel");
#endif

		/* we need to set the desktop background : */
		if (!get_flags (AfterStepState, ASS_SuppressDeskBack)) {
			if ((IsValidDesk (new_desk) && IsValidDesk (old_desk)) ||
					Scr.LastValidDesk != new_desk) {

				display_progress (True, "Changing desktop background ...");
				change_desktop_background (new_desk);
			}
		}
	}

	if (desk_covered)
		remove_desktop_cover ();
}


void ChangeDesks (int new_desk)
{
	ChangeDeskAndViewport (new_desk, Scr.Vx, Scr.Vy, False);

}

