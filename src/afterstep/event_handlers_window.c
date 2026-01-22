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

/***********************************************************************
 *
 *  Procedure:
 *	HandleDestroyNotify - DestroyNotify event handler
 *
 ***********************************************************************/
void HandleDestroyNotify (ASEvent * event)
{
	if (event->client) {
		Destroy (event->client, True);
	}
}

/***********************************************************************
 *  Procedure:
 *	HandleMapRequest - MapRequest event handler
 ************************************************************************/
void delayed_add_window (void *vdata)
{
	if (window2ASWindow ((Window) vdata) == NULL)
		AddWindow ((Window) vdata, True);
}

void HandleMapRequest (ASEvent * event)
{
	/* If the window has never been mapped before ... */
	if (event->client == NULL) {	/* lets delay handling map request in case client needs time to update its properties */
		if (get_flags (AfterStepState, ASS_NormalOperation))
			timer_new (200, delayed_add_window, (void *)event->w);
		else
			AddWindow (event->w, True);

/*        if( (event->client = AddWindow (event->w, True)) == NULL ) */
		return;
	} else												/* If no hints, or currently an icon, just "deiconify" */
		set_window_wm_state (event->client, False, True);
}

/***********************************************************************
 *
 *  Procedure:
 *	HandleMapNotify - MapNotify event handler
 *
 ***********************************************************************/
void HandleMapNotify (ASEvent * event)
{
	ASWindow *asw = event->client;
	Bool force_activation = False;
	Bool no_focus = False;

	if (asw == NULL || event->w == Scr.Root)
		return;

	LOCAL_DEBUG_OUT ("asw->w = %lX, event->w = %lX", asw->w, event->w);
	if (event->w != asw->w) {
		if (asw->wm_state_transition == ASWT_Withdrawn2Iconic && event->w == asw->status->icon_window) {	/* we finally reached iconic state : */
			complete_wm_state_transition (asw, IconicState);
		}
		return;
	}

	if (asw->wm_state_transition == ASWT_Withdrawn2Normal) {
		if (ASWIN_HFLAGS (asw, AS_FocusOnMap))
			force_activation = True;
		else
			no_focus = True;
	}
	LOCAL_DEBUG_OUT ("asw->wm_state_transition = %d",
									 asw->wm_state_transition);

	if (asw->wm_state_transition == ASWT_StableState) {
		if (ASWIN_GET_FLAGS (asw, AS_Iconic))
			set_window_wm_state (asw, False, False);	/* client has requested deiconification */
		return;											/* otherwise it is redundand event */
	}
	if (get_flags (asw->wm_state_transition, ASWT_FROM_ICONIC))
		if (get_flags (Scr.Feel.flags, ClickToFocus))
			force_activation = True;

	ASWIN_SET_FLAGS (asw, AS_Mapped);
	ASWIN_CLEAR_FLAGS (asw, AS_IconMapped);
	ASWIN_CLEAR_FLAGS (asw, AS_Iconic);
	complete_wm_state_transition (asw, NormalState);
	LOCAL_DEBUG_OUT
			("no_focus = %d, force_activation = %d, AcceptsFocus = %ld",
			 no_focus, force_activation, ASWIN_HFLAGS (asw, AS_AcceptsFocus));
	if (!no_focus && ASWIN_FOCUSABLE (asw))
		activate_aswindow (asw, force_activation, False);
	broadcast_config (M_MAP, asw);
	/* finally reaches Normal state */
}


/***********************************************************************
 *
 *  Procedure:
 *	HandleUnmapNotify - UnmapNotify event handler
 *
 ************************************************************************/
void HandleUnmapNotify (ASEvent * event)
{
	XEvent dummy;
	ASWindow *asw = event->client;
	Bool destroyed = False;

	if (event->x.xunmap.event == Scr.Root && asw == NULL)
		asw = window2ASWindow (event->x.xunmap.window);

	if (asw == NULL || event->x.xunmap.window != asw->w)
		return;

	ASWIN_CLEAR_FLAGS (asw, AS_Mapped);
	ASWIN_CLEAR_FLAGS (asw, AS_UnMapPending);
	/* Window remains hilited even when unmapped !!!! */
	/* if (Scr.Hilite == asw )
	   Scr.Hilite = NULL; */

	if (Scr.Windows->previous_active == asw)
		Scr.Windows->previous_active = NULL;

	if (Scr.Windows->focused == asw)
		focus_next_aswindow (asw);

	if (get_flags (asw->wm_state_transition, ASWT_TO_WITHDRAWN)) {	/* redundand UnmapNotify - ignoring */
		return;
	}
	if (get_flags (asw->wm_state_transition, ASWT_TO_ICONIC)) {	/* we finally reached iconic state : */
		complete_wm_state_transition (asw, IconicState);
		return;
	}
	/*
	 * The program may have unmapped the client window, from either
	 * NormalState or IconicState.  Handle the transition to WithdrawnState.
	 */

	grab_server ();
	destroyed = ASCheckTypedWindowEvent (event->w, DestroyNotify, &dummy);
	LOCAL_DEBUG_OUT ("wm_state_transition = 0x%X", asw->wm_state_transition);
	if (!get_flags (asw->wm_state_transition, ASWT_FROM_WITHDRAWN))
		asw->wm_state_transition =
				ASWIN_GET_FLAGS (asw,
												 AS_Iconic) ? ASWT_Iconic2Withdrawn :
				ASWT_Normal2Withdrawn;
	else
		asw->wm_state_transition = ASWT_Withdrawn2Withdrawn;
	Destroy (asw, destroyed);			/* do not need to mash event before */
	ungrab_server ();
	ASFlush ();
}

/***********************************************************************
 *
 *  Procedure:
 *	HandleConfigureRequest - ConfigureRequest event handler
 *
 ************************************************************************/
void HandleConfigureRequest (ASEvent * event)
{
	XConfigureRequestEvent *cre = &(event->x.xconfigurerequest);
	ASWindow *asw = event->client;
	XWindowChanges xwc;
	unsigned long xwcm;

	/*
	 * According to the July 27, 1988 ICCCM draft, we should ignore size and
	 * position fields in the WM_NORMAL_HINTS property when we map a window.
	 * Instead, we'll read the current geometry.  Therefore, we should respond
	 * to configuration requests for windows which have never been mapped.
	 */

	LOCAL_DEBUG_OUT ("cre={0x%lx, geom = %dx%d%+d%+d} ", cre->value_mask,
									 cre->width, cre->height, cre->x, cre->y);
	if (asw == NULL) {
		xwcm =
				cre->value_mask & (CWX | CWY | CWWidth | CWHeight | CWBorderWidth);
		if (xwcm == 0) {
			LOCAL_DEBUG_OUT
					("Ignoring ConfigureRequest for untracked window %lX - no supported changes detected.",
					 (unsigned long)event->w);
		} else {
			xwc.x = cre->x;
			xwc.y = cre->y;
			xwc.width = cre->width;
			xwc.height = cre->height;
			xwc.border_width = cre->border_width;
			LOCAL_DEBUG_OUT
					("Configuring untracked window %lX to %dx%d%+d%+d and bw = %d, (flags=%lX)",
					 (unsigned long)event->w, cre->width, cre->height, cre->x,
					 cre->y, cre->border_width, xwcm);
			XConfigureWindow (dpy, event->w, xwcm, &xwc);
			ASSync (False);
		}
		return;
	}

	if (asw->icon_canvas && event->w == asw->icon_canvas->w) {	/* we really should ignore that ! - let's see how it will play out */
		/* we may need to add code to iconbox to handle custom icon geometry for client icons */
		xwcm = cre->value_mask & (CWWidth | CWHeight);
		if (xwcm == 0) {
			LOCAL_DEBUG_OUT
					("Ignoring ConfigureRequest for iconic window %lX - no supported changes detected.",
					 (unsigned long)event->w);
		} else {
			xwc.width = cre->width;
			xwc.height = cre->height;
			LOCAL_DEBUG_OUT
					("Configuring iconic window %lX to %dx%d, (flags=%lX)",
					 (unsigned long)event->w, cre->width, cre->height, xwcm);
			XConfigureWindow (dpy, event->w, xwcm, &xwc);
			ASSync (False);
		}
		return;
	}

	if (event->w != asw->w) {
		LOCAL_DEBUG_OUT
				("Ignoring ConfigureRequest for window %lX. Not a client or client's icon!",
				 (unsigned long)event->w);
		return;
	}

	if (cre->value_mask & CWStackMode) {
		if (!ASWIN_HFLAGS (asw, AS_IgnoreRestackRequest)) {
			restack_window (asw,
											(cre->value_mask & CWSibling) ? cre->above : None,
											cre->detail);
		} else {
			LOCAL_DEBUG_OUT
					("Ignoring Stacking order Request for client %p as required by hints",
					 asw);
		}
	}

	if ((ASWIN_HFLAGS (asw, AS_IgnoreConfigRequest)
			 && !get_flags (cre->value_mask, CWWidth | CWHeight))
			|| ASWIN_GET_FLAGS (asw, AS_Fullscreen)) {
		LOCAL_DEBUG_OUT
				("Ignoring ConfigureRequest for client %p as required by hints",
				 asw);
		SendConfigureNotify (asw);
		return;
	}

	/* check_aswindow_shaped( asw ); */

	/* for restoring */
	if (cre->value_mask & CWBorderWidth)
		asw->status->border_width = cre->border_width;

	/* now we need to update window's anchor : */

	if (cre->value_mask & (CWWidth | CWHeight | CWX | CWY)) {
		XRectangle new_anchor = asw->anchor;

		if (cre->value_mask & CWWidth)
			new_anchor.width = cre->width;
		if (cre->value_mask & CWHeight)
			new_anchor.height = cre->height;
		if (!ASWIN_HFLAGS (asw, AS_IgnoreConfigRequest)) {
			int grav_x, grav_y;
			get_gravity_offsets (asw->hints, &grav_x, &grav_y);
			if (cre->value_mask & CWX)
				new_anchor.x =
						make_anchor_pos (asw->status, cre->x, new_anchor.width, Scr.Vx,
														 grav_x, Scr.VxMax + Scr.MyDisplayWidth);
			if (cre->value_mask & CWY)
				new_anchor.y =
						make_anchor_pos (asw->status, cre->y, new_anchor.height,
														 Scr.Vy, grav_y,
														 Scr.VyMax + Scr.MyDisplayHeight);
		}
		LOCAL_DEBUG_OUT ("old anchor(%dx%d%+d%+d), new_anchor(%dx%d%+d%+d)",
										 asw->anchor.width, asw->anchor.height, asw->anchor.x,
										 asw->anchor.y, new_anchor.width, new_anchor.height,
										 new_anchor.x, new_anchor.y);
		validate_window_anchor (asw, &new_anchor, False);
		LOCAL_DEBUG_OUT ("validated_anchor(%dx%d%+d%+d)", new_anchor.width,
										 new_anchor.height, new_anchor.x, new_anchor.y);
		asw->anchor = new_anchor;
		on_window_anchor_changed (asw);
		enforce_avoid_cover (asw);
		if ((cre->value_mask & (CWWidth | CWHeight)) == 0)
			SendConfigureNotify (asw);
	}
}

