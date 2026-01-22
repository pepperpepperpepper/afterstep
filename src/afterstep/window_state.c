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

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"
#include "../../libAfterStep/wmprops.h"

/***************************************************************************************/
/* iconify/deiconify code :                                                            */
/***************************************************************************************/
void complete_wm_state_transition (ASWindow * asw, int state)
{
	asw->wm_state_transition = ASWT_StableState;
	if (state == NormalState) {
		LOCAL_DEBUG_OUT
				("mapping frame subwindows for client %lX, frame canvas = %p",
				 asw->w, asw->frame_canvas);
		XMapSubwindows (dpy, asw->frame);
		map_canvas_window (asw->frame_canvas, False);
		restack_desktop_cover ();
	} else if (state == IconicState) {
		unmap_canvas_window (asw->frame_canvas);
	}
	if (!ASWIN_GET_FLAGS (asw, AS_Dead))
		set_multi32bit_property (asw->w, _XA_WM_STATE, _XA_WM_STATE, 2, state,
														 (state ==
															IconicState) ? asw->status->
														 icon_window : None);
}

Bool
set_window_wm_state (ASWindow * asw, Bool iconify, Bool force_unmapped)
{
	XWindowAttributes attr;

	LOCAL_DEBUG_CALLER_OUT ("client = %p, iconify = %d", asw, iconify);

	if (AS_ASSERT (asw))
		return False;

	if (iconify) {
		asw->DeIconifyDesk = ASWIN_DESK (asw);
		if (asw->wm_state_transition == ASWT_StableState) {
			if (get_flags (asw->status->flags, AS_Iconic))
				return False;

			asw->wm_state_transition = ASWT_Normal2Iconic;
			set_flags (asw->status->flags, AS_Iconic);
			if (get_flags (Scr.Feel.flags, StickyIcons)
					|| ASWIN_DESK (asw) == Scr.CurrentDesk)
				quietly_reparent_aswindow (asw, Scr.Root, True);
			else
				quietly_reparent_aswindow (asw, Scr.ServiceWin, True);
		}

		asw->status->icon_window =
				asw->icon_canvas ? asw->icon_canvas->w : None;

		if (asw->icon_canvas)
			asw->status->icon_window = asw->icon_canvas->w;
		else if (asw->icon_title_canvas)
			asw->status->icon_window = asw->icon_title_canvas->w;

		if (get_flags (Scr.Feel.flags, ClickToFocus)
				|| get_flags (Scr.Feel.flags, SloppyFocus)) {
			if (asw == Scr.Windows->focused)
				focus_prev_aswindow (asw);
		}

		LOCAL_DEBUG_OUT ("unmaping client window 0x%lX",
										 (unsigned long)asw->w);
		if (!ASWIN_GET_FLAGS (asw, AS_Dead)) {
			if (!XGetWindowAttributes (dpy, asw->w, &attr))
				ASWIN_SET_FLAGS (asw, AS_Dead);
			else {
				if (attr.map_state != IsUnmapped)
					XUnmapWindow (dpy, asw->w);
				else {
					ASWIN_CLEAR_FLAGS (asw, AS_Mapped);
					complete_wm_state_transition (asw, IconicState);
				}
			}
			ASSync (False);
		}

		LOCAL_DEBUG_OUT ("hilited == %p", Scr.Windows->hilited);
		if (Scr.Windows->hilited == asw)
			hide_hilite ();

		/* finally mapping the icon windows : */
		add_iconbox_icon (asw);
		restack_window (asw, None, Below);
		map_canvas_window (asw->icon_canvas, True);
		if (asw->icon_canvas != asw->icon_title_canvas)
			map_canvas_window (asw->icon_title_canvas, True);
		on_window_status_changed (asw, True);
		LOCAL_DEBUG_OUT ("updating status to iconic for client %p(\"%s\")",
										 asw, ASWIN_NAME (asw));
	} else {											/* Performing transition IconicState->NormalState  */
		if (asw->wm_state_transition == ASWT_StableState) {
			if (!get_flags (asw->status->flags, AS_Iconic))
				return False;
			asw->wm_state_transition = ASWT_Iconic2Normal;
			clear_flags (asw->status->flags, AS_Iconic);
			remove_iconbox_icon (asw);
			unmap_canvas_window (asw->icon_canvas);
			if (asw->icon_canvas != asw->icon_title_canvas)
				unmap_canvas_window (asw->icon_title_canvas);

			change_aswindow_desktop (asw,
															 get_flags (Scr.Feel.flags,
																					StubbornIcons) ? asw->
															 DeIconifyDesk : Scr.CurrentDesk, True);
		}

		asw->status->icon_window = None;

		if (!ASWIN_GET_FLAGS (asw, AS_Dead) && !force_unmapped) {
			if (!XGetWindowAttributes (dpy, asw->w, &attr))
				ASWIN_SET_FLAGS (asw, AS_Dead);
		}
		if (!ASWIN_GET_FLAGS (asw, AS_Dead)) {
			{
				/* TODO: make sure that the window is on this screen */
				if (attr.map_state == IsUnmapped || force_unmapped)
					XMapRaised (dpy, asw->w);
				else {
					complete_wm_state_transition (asw, NormalState);
					ASWIN_SET_FLAGS (asw, AS_Mapped);
					if (get_flags (Scr.Feel.flags, ClickToFocus))
						activate_aswindow (asw, True, False);
				}
			}
		}
		on_window_status_changed (asw, False);
	}

	if (!get_flags (asw->wm_state_transition, ASWT_FROM_WITHDRAWN))
		broadcast_config (M_CONFIGURE_WINDOW, asw);

	return True;
}

Bool bring_aswindow_on_vscreen (ASWindow * asw)
{
	int min_x, min_y, max_x, max_y;
	int margin_x = Scr.MyDisplayWidth >> 5, margin_y =
			Scr.MyDisplayHeight >> 5;

	if (asw == NULL)
		return False;

	if (!ASWIN_GET_FLAGS (asw, AS_Sticky)) {
		min_x = -Scr.Vx;
		max_x = Scr.VxMax + Scr.MyDisplayWidth - Scr.Vx;
		min_y = -Scr.Vy;
		max_y = Scr.VyMax + Scr.MyDisplayHeight - Scr.Vy;
	} else {
		min_x = 0;
		max_x = Scr.MyDisplayWidth;
		min_y = 0;
		max_y = Scr.MyDisplayHeight;
	}
	if (!ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		int new_x = asw->status->x;
		int new_y = asw->status->y;
		int w = asw->status->width;
		int h = asw->status->height;

		if (margin_x > w >> 2) {
			margin_x = w >> 2;
			if (margin_x == 0)
				margin_x = 1;
		}
		if (margin_y > h >> 2) {
			margin_y = h >> 2;
			if (margin_y == 0)
				margin_y = 1;
		}

		if (new_x + w < min_x + margin_x)
			new_x = min_x + margin_x - w;
		else if (new_x > max_x - margin_x)
			new_x = max_x - margin_x;

		if (new_y + h < min_y + margin_y)
			new_y = min_y + margin_y - h;
		else if (new_y > max_y - margin_y)
			new_y = max_y - margin_y;
		LOCAL_DEBUG_OUT
				("min_pos = (%+d%+d), max_pos = (%+d%+d), new_pos = (%+d%+d)",
				 min_x, min_y, max_x, max_y, new_x, new_y);
		if (new_x != asw->status->x || new_y != asw->status->y)
			moveresize_aswindow_wm (asw, new_x, new_y, asw->status->width,
															asw->status->height, False);
	}
	return True;
}


Bool make_aswindow_visible (ASWindow * asw, Bool deiconify)
{
	if (asw == NULL || !get_flags (AfterStepState, ASS_NormalOperation))
		return False;

	if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		if (deiconify) {
			set_window_wm_state (asw, False, False);
		}
	}

	if (ASWIN_DESK (asw) != Scr.CurrentDesk) {
		ChangeDesks (ASWIN_DESK (asw));
	}

	bring_aswindow_on_vscreen (asw);

#ifndef NO_VIRTUAL
	if (!ASWIN_GET_FLAGS (asw, AS_Sticky)) {
		int dx, dy;
		int cx = Scr.MyDisplayWidth / 2;
		int cy = Scr.MyDisplayHeight / 2;

		if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
			if (asw->icon_canvas) {
				cx = asw->icon_canvas->root_x + asw->icon_canvas->width / 2;
				cy = asw->icon_canvas->root_y + asw->icon_canvas->height / 2;
			}
		} else if (asw->frame_canvas) {
			cx = asw->frame_canvas->root_x + asw->frame_canvas->width / 2;
			cy = asw->frame_canvas->root_y + asw->frame_canvas->height / 2;
		}

		/* Put center of window on the visible screen */
		if (get_flags (Scr.Feel.flags, CenterOnCirculate)) {
			dx = cx - Scr.MyDisplayWidth / 2 + Scr.Vx;
			dy = cy - Scr.MyDisplayHeight / 2 + Scr.Vy;
		} else {
			dx = (cx + Scr.Vx) / Scr.MyDisplayWidth * Scr.MyDisplayWidth;
			dy = (cy + Scr.Vy) / Scr.MyDisplayHeight * Scr.MyDisplayHeight;
		}
		MoveViewport (dx, dy, True);
	}
#endif

	RaiseObscuredWindow (asw);
	if (!get_flags (Scr.Feel.flags, ClickToFocus)) {
		int x, y;
		/* need to to center on window */
		if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
			if (asw->icon_title_canvas
					&& asw->icon_canvas != asw->icon_title_canvas)
				on_window_moveresize (asw, asw->icon_title_canvas->w);
			if (asw->icon_canvas) {
				on_window_moveresize (asw, asw->icon_canvas->w);
				x = asw->icon_canvas->root_x;
				y = asw->icon_canvas->root_y;
			} else if (asw->icon_title_canvas) {
				x = asw->icon_title_canvas->root_x;
				y = asw->icon_title_canvas->root_y;
			} else
				return False;
		} else {
			on_window_moveresize (asw, asw->frame);
			x = asw->client_canvas->root_x;
			y = asw->client_canvas->root_y;
		}
		LOCAL_DEBUG_OUT ("Warping pointer to : %+d%+d", x + Scr.Feel.Xzap,
										 y + Scr.Feel.Yzap);
		XWarpPointer (dpy, None, Scr.Root, 0, 0, 0, 0, x + Scr.Feel.Xzap,
									y + Scr.Feel.Yzap);
	}
	return True;
}

void change_aswindow_layer (ASWindow * asw, int layer)
{
	if (AS_ASSERT (asw))
		return;
	if (asw->transient_owner == NULL && ASWIN_LAYER (asw) != layer) {
		remove_aswindow_from_layer (asw, ASWIN_LAYER (asw));
		LOCAL_DEBUG_OUT ("changing window's layer to %d", layer);
		ASWIN_LAYER (asw) = layer;
		add_aswindow_to_layer (asw, layer);
		restack_window_list (ASWIN_DESK (asw));
		ASWIN_SET_FLAGS (asw, AS_Layer);
		set_client_state (asw->w, asw->status);
	}
}

static void
do_change_aswindow_desktop (ASWindow * asw, int new_desk, Bool force)
{
	int old_desk = ASWIN_DESK (asw);

	if (!force && ASWIN_DESK (asw) == new_desk)
		return;

	ASWIN_DESK (asw) = new_desk;

	if (!ASWIN_GET_FLAGS (asw, AS_Dead)) {
		set_client_desktop (asw->w, as_desk2ext_desk_safe(new_desk));

		/* desktop changing : */
		if (new_desk == Scr.CurrentDesk) {
			quietly_reparent_aswindow (asw, Scr.Root, True);
		} else if (old_desk == Scr.CurrentDesk)
			quietly_reparent_aswindow (asw, Scr.ServiceWin, True);
		broadcast_config (M_CONFIGURE_WINDOW, asw);
	}
}

static void
change_aswindow_desktop_nontransient (ASWindow * asw, int new_desk,
																			Bool force)
{
	ASWindow **sublist;
	int i;

	if (ASWIN_GET_FLAGS (asw, AS_Sticky))
		new_desk = Scr.CurrentDesk;

	do_change_aswindow_desktop (asw, new_desk, force);
	if (asw->transients) {
		sublist = PVECTOR_HEAD (ASWindow *, asw->transients);
		for (i = 0; i < PVECTOR_USED (asw->transients); ++i)
			do_change_aswindow_desktop (sublist[i], new_desk, force);
	}
}

#if 0														/* TODO do we really need that ??? */

struct ChangeGroupDesktopAuxData {
	Window group_lead;
	ASWindow *initiator;
	int new_desk;
	Bool force;

};

static Bool
change_aswindow_desktop_for_group_func (void *data, void *aux_data)
{
	ASWindow *asw = (ASWindow *) data;
	struct ChangeGroupDesktopAuxData *ad =
			(struct ChangeGroupDesktopAuxData *)aux_data;
	LOCAL_DEBUG_OUT
			("asw = %p(w = %lX), initiator = %p, to = %p, asw->gl = %lX, gl = %lX",
			 asw, asw->w, ad->initiator, asw->transient_owner,
			 asw->hints->group_lead, ad->group_lead);
	if (asw && asw != ad->initiator && asw->transient_owner == NULL
			&& asw->hints->group_lead == ad->group_lead)
		change_aswindow_desktop_nontransient (asw, ad->new_desk, ad->force);
	return True;
}
#endif

void change_aswindow_desktop (ASWindow * asw, int new_desk, Bool force)
{
	ASWindow **sublist;
	int i;

	if (AS_ASSERT (asw))
		return;
	if (asw->transient_owner)
		asw = asw->transient_owner;

	change_aswindow_desktop_nontransient (asw, new_desk, force);

#if 0														/* TODO do we really need that ??? */
	LOCAL_DEBUG_OUT ("group_members = %p; group_lead = %lX",
									 asw->group_members, asw->hints->group_lead);
	if (asw->group_members) {
		sublist = PVECTOR_HEAD (ASWindow *, asw->group_members);
		for (i = 0; i < PVECTOR_USED (asw->group_members); ++i)
			if (sublist[i]->transient_owner == NULL)
				do_change_aswindow_desktop (sublist[i], new_desk, force);
	} else if (asw->hints->group_lead != None) {	/* sometimes group lead may be unmapped untracked window */
		struct ChangeGroupDesktopAuxData ad;
		ad.group_lead = asw->hints->group_lead;
		ad.initiator = asw;
		ad.new_desk = new_desk;
		ad.force = force;
		iterate_asbidirlist (Scr.Windows->clients,
												 change_aswindow_desktop_for_group_func, &ad, NULL,
												 False);
	}
#endif
}

static Bool restore_anchor_x (ASWindow * asw)
{
	if (asw->saved_anchor.width > 0) {
		asw->anchor.x = asw->saved_anchor.x;
		asw->anchor.width = asw->saved_anchor.width;
		asw->saved_anchor.width = 0;	/* invalidating saved anchor */
		return True;
	}
	return False;
}

static Bool restore_anchor_y (ASWindow * asw)
{
	if (asw->saved_anchor.height > 0) {
		asw->anchor.y = asw->saved_anchor.y;
		asw->anchor.height = asw->saved_anchor.height;
		asw->saved_anchor.height = 0;	/* invalidating saved anchor */
		return True;
	}
	return False;
}

void toggle_aswindow_status (ASWindow * asw, ASFlagType flags)
{
	ASFlagType on_flags, off_flags;
	Bool need_placement = False;
	Bool reconfigured = False;

	if (AS_ASSERT (asw))
		return;

	if (flags == 0)
		return;

	if (get_flags (flags, AS_Fullscreen))
		clear_flags (flags, AS_MaximizedX | AS_MaximizedY);

	on_flags = (~(asw->status->flags)) & flags;
	off_flags = (asw->status->flags) & (~flags);
	asw->status->flags = on_flags | off_flags;
	LOCAL_DEBUG_OUT ("flags = %lx, on_flags = %lx, off_flags = %lx", flags,
									 on_flags, off_flags);
	if (get_flags (flags, AS_Shaded)) {
		if (get_flags (asw->status->flags, AS_Shaded)) {
			ASOrientation *od = get_orientation_data (asw);
			if (asw->frame_sides[od->tbar_side]) {
				Window ww[2];
				ww[0] = asw->w;
				ww[1] = asw->frame_sides[od->tbar_side]->w;
				XRestackWindows (dpy, &(ww[0]), 2);
			}
		}
		asw->shading_steps = Scr.Feel.ShadeAnimationSteps;
	}
	if (get_flags (flags, AS_Sticky)) {	/* anchor of sticky window is always in real coordinates, while
																			 * for non-sticky its in virtual coordinates
																			 */
		if (ASWIN_GET_FLAGS (asw, AS_Sticky)) {
			asw->anchor.x -= asw->status->viewport_x;
			asw->anchor.y -= asw->status->viewport_y;
		} else {
			asw->anchor.x += asw->status->viewport_x;
			asw->anchor.y += asw->status->viewport_y;
		}
	}

	if (get_flags (flags, AS_Fullscreen)) {
		if (!ASWIN_GET_FLAGS (asw, AS_Fullscreen)) {	/* fullscreen->normal */
			if (!ASWIN_GET_FLAGS (asw, AS_MaximizedX))
				if (!restore_anchor_x (asw)) {
					asw->anchor.width = Scr.MyDisplayWidth / 3;
					asw->anchor.x = (Scr.MyDisplayWidth - asw->anchor.width) / 2;
				}

			if (!ASWIN_GET_FLAGS (asw, AS_MaximizedY))
				if (!restore_anchor_y (asw)) {
					asw->anchor.height = Scr.MyDisplayHeight / 3;
					asw->anchor.y = (Scr.MyDisplayHeight - asw->anchor.height) / 2;
				}

			reconfigured = True;
			if (ASWIN_GET_FLAGS (asw, AS_MaximizedY | AS_MaximizedX))
				need_placement = True;
		} else {										/* normal->fullscreen */
			ASStatusHints scratch_status = *(asw->status);
			scratch_status.x = 0;
			scratch_status.y = 0;
			scratch_status.width = Scr.MyDisplayWidth;
			scratch_status.height = Scr.MyDisplayHeight;

			save_aswindow_anchor (asw, True, True);
			status2anchor (&(asw->anchor), asw->hints, &scratch_status,
										 Scr.VxMax + Scr.MyDisplayWidth,
										 Scr.VyMax + Scr.MyDisplayHeight);
			reconfigured = True;
		}
	}
	if (get_flags (flags, AS_MaximizedX)) {
		if (!ASWIN_GET_FLAGS (asw, AS_MaximizedX)) {
			if (restore_anchor_x (asw))
				reconfigured = True;
		} else if (!ASWIN_GET_FLAGS (asw, AS_Fullscreen))
			need_placement = True;
	}
	if (get_flags (flags, AS_MaximizedY)) {
		if (!ASWIN_GET_FLAGS (asw, AS_MaximizedY)) {
			if (restore_anchor_y (asw))
				reconfigured = True;
		} else if (!ASWIN_GET_FLAGS (asw, AS_Fullscreen))
			need_placement = True;
	}

	if (get_flags (flags, AS_Focused))
		activate_aswindow (asw, False, True);

	if (need_placement)
		place_aswindow (asw);

	on_window_status_changed (asw, reconfigured);
	if (get_flags (flags, AS_Sticky))
		update_window_transparency (asw, False);
	LOCAL_DEBUG_OUT ("Window is %sticky",
									 ASWIN_GET_FLAGS (asw, AS_Sticky) ? "S" : "NotS");
}

Bool activate_aswindow (ASWindow * asw, Bool force, Bool deiconify)
{
	Bool res = False;
	LOCAL_DEBUG_CALLER_OUT ("%p, %d, %d", asw, force, deiconify);
	LOCAL_DEBUG_OUT ("current focused is %p, active is %p",
									 Scr.Windows->focused, Scr.Windows->active);
	if (asw == NULL || asw->status == NULL || ASWIN_GET_FLAGS (asw, AS_Dead))
		return False;

	if (force) {
		GrabEm (ASDefaultScr, Scr.Feel.cursors[ASCUR_Select]);	/* to prevent Enter Notify events to
																														   be sent to us while shifting windows around */
		if ((res = make_aswindow_visible (asw, deiconify))) {
			LOCAL_DEBUG_OUT ("CHANGE Scr.Windows->active from %p to %p",
											 Scr.Windows->active, asw);
			Scr.Windows->active = asw;	/* must do that prior to UngrabEm, so that window gets focused */
		}
		UngrabEm ();
	} else {
		if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
			LOCAL_DEBUG_OUT ("Window is iconic - pending implementation%s", "");
			if (deiconify)
				set_window_wm_state (asw, False, False);
			else
				return False;
		}
		if (ASWIN_DESK (asw) != Scr.CurrentDesk) {
			LOCAL_DEBUG_OUT ("Window is on inactive desk - can't focus%s", "");
			return False;
		}
		if (asw->status->x + asw->status->width < 0
				|| asw->status->x >= Scr.MyDisplayWidth
				|| asw->status->y + asw->status->height < 0
				|| asw->status->y >= Scr.MyDisplayHeight) {
			LOCAL_DEBUG_OUT ("Window is out of the screen - can't focus%s", "");
			return False;							/* we are out of screen - can't focus */
		}
		LOCAL_DEBUG_OUT ("CHANGE Scr.Windows->active from %p to %p",
										 Scr.Windows->active, asw);
		Scr.Windows->active = asw;	/* must do that prior to UngrabEm, so that window gets focused */
		res = focus_active_window ();
	}
	return res;
}


void hilite_aswindow (ASWindow * asw)
{
	if (Scr.Windows->hilited != asw) {
		if (Scr.Windows->hilited)
			on_window_hilite_changed (Scr.Windows->hilited, False);
		if (asw)
			on_window_hilite_changed (asw, True);

		if (Scr.Windows->hilited)
			broadcast_focus_change (Scr.Windows->hilited, False);
		if (asw)
			broadcast_focus_change (asw, True);

		Scr.Windows->hilited = asw;
		set_active_window_prop (Scr.wmprops, asw ? asw->w : None);
	}
}

void hide_hilite ()
{
	if (Scr.Windows->hilited != NULL) {
		on_window_hilite_changed (Scr.Windows->hilited, False);
		broadcast_focus_change (Scr.Windows->hilited, False);
		Scr.Windows->hilited = NULL;
	}
}

void press_aswindow (ASWindow * asw, int context)
{
	if (context == C_NO_CONTEXT) {
		if (Scr.Windows->pressed == asw)
			Scr.Windows->pressed = NULL;
	} else if (Scr.Windows->pressed != asw) {
		/* TODO :may need to do something to avoid recursion here */
		if (Scr.Windows->pressed != NULL)
			on_window_pressure_changed (Scr.Windows->pressed, C_NO_CONTEXT);
		Scr.Windows->pressed = asw;
	}

	if (asw)
		on_window_pressure_changed (asw, context);
}

void release_pressure ()
{
	if (Scr.Windows->pressed != NULL) {
		on_window_pressure_changed (Scr.Windows->pressed, C_NO_CONTEXT);
		Scr.Windows->pressed = NULL;
	}
}

/**********************************************************************/
/* The End                                                            */
/**********************************************************************/

