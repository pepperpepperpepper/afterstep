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
 *  Procedure:
 *  HandleFocusIn - client received focus
 ************************************************************************/
void HandleFocusIn (ASEvent * event)
{
	int events_count = 0;
	while (ASCheckTypedEvent (FocusIn, &event->x))
		events_count++;
	if (events_count > 0)
		DigestEvent (event);

	if (get_flags (AfterStepState, ASS_WarpingMode))
		ChangeWarpingFocus (event->client);

	LOCAL_DEBUG_OUT ("focused = %p, this event for %p", Scr.Windows->focused,
									 event->client);
	if (Scr.Windows->focused != event->client) {
		LOCAL_DEBUG_OUT ("CHANGE Scr.Windows->focused from %p to NULL",
										 Scr.Windows->focused);
		unset_focused_window();
	}
	if (event->client == NULL
			&& get_flags (AfterStepState, ASS_HousekeepingMode))
		return;
	/* note that hilite_aswindow changes value of Scr.Hilite!!! */
	hilite_aswindow (event->client);
}

/***********************************************************************
 *
 *  Procedure:
 *	HandleKeyPress - key press event handler
 *
 ************************************************************************/
void HandleKeyPress (ASEvent * event)
{
	FuncKey *key;
	XKeyEvent *xk = &(event->x.xkey);
	unsigned int modifier = (xk->state & nonlock_mods);
	int m;

	/* Here's a real hack - some systems have two keys with the
	 * same keysym and different keycodes. This converts all
	 * the cases to one keycode. */
	for (m = 0; m < 8; ++m) {
		KeySym keysym = XkbKeycodeToKeysym (dpy, xk->keycode, m, 0);
		int keycode;
		if (keysym == NoSymbol)
			continue;
		if ((keycode = XKeysymToKeycode (dpy, keysym)) == 0)
			continue;
		xk->keycode = keycode;

		for (key = Scr.Feel.FuncKeyRoot; key != NULL; key = key->next) {
			if ((key->keycode == xk->keycode) &&
					((key->mods == (modifier & (~LockMask))) ||
					 (key->mods == AnyModifier)) && (key->cont & event->context)) {
				/* check if the warp key was pressed */
				ExecuteFunction (key->fdata, event, -1);
				return;
			}
		}
	}
	/* if a key has been pressed and it's not one of those that cause
	   warping, we know the warping is finished */
	if (get_flags (AfterStepState, ASS_WarpingMode))
		EndWarping ();

	LOCAL_DEBUG_OUT ("client = %p, context = %s", event->client,
									 context2text ((event)->context));
	/* if we get here, no function key was bound to the key.  Send it
	 * to the client if it was in a window we know about: */
	if (event->client) {
		LOCAL_DEBUG_OUT ("internal = %p", event->client->internal);
		if (event->client->internal
				&& event->client->internal->on_keyboard_event)
			event->client->internal->on_keyboard_event (event->client->internal,
																									event);
		else if (xk->window != event->client->w) {
			xk->window = event->client->w;
			XSendEvent (dpy, event->client->w, False, KeyPressMask, &(event->x));
		}
	}
}

/***********************************************************************
 *  Procedure:
 *	HandleButtonPress - ButtonPress event handler
 ***********************************************************************/
void HandleButtonPress (ASEvent * event, Bool deffered)
{
	unsigned int modifier;
	MouseButton *MouseEntry;
	Bool AShandled = False;
	ASWindow *asw = event->client;
	XButtonEvent *xbtn = &(event->x.xbutton);
	Bool raise_on_click = False;
	Bool focus_accepted = False;
	Bool eat_click = False;
	Bool activate_window = False;

	/* click to focus stuff goes here */
	if (asw != NULL) {
		LOCAL_DEBUG_OUT ("deferred = %d, button = %X", deffered,
										 (event->context & (~C_TButtonAll)));
		/* if all we do is pressing titlebar buttons - then we should not raise/focus window !!! */
		if (!deffered) {
			if ((event->context & (~C_TButtonAll)) != 0) {
				if (get_flags (Scr.Feel.flags, ClickToFocus)) {
					LOCAL_DEBUG_OUT ("asw = %p, ungrabbed = %p, nonlock_mods = %x",
													 asw, Scr.Windows->ungrabbed,
													 (xbtn->state & nonlock_mods));
					if (asw != Scr.Windows->ungrabbed
							&& (xbtn->state & nonlock_mods) == 0) {
						if (get_flags (Scr.Feel.flags, EatFocusClick)) {
							if (Scr.Windows->focused != asw)
								if ((focus_accepted =
										 activate_aswindow (asw, False, False)))
									eat_click = True;
						} else if (Scr.Windows->focused != asw)
							activate_window = True;
						LOCAL_DEBUG_OUT ("eat_click = %d", eat_click);
					}
				}

				if (get_flags (Scr.Feel.flags, ClickToRaise))
					raise_on_click = (Scr.Feel.RaiseButtons == 0
														|| (Scr.Feel.
																RaiseButtons & (1 << xbtn->button)));
			}

			if (!ASWIN_GET_FLAGS (asw, AS_Iconic)) {
				XSync (dpy, 0);
				XAllowEvents (dpy,
											(event->context ==
											 C_WINDOW) ? ReplayPointer : AsyncPointer,
											CurrentTime);
				XSync (dpy, 0);
			}
		}
		/* !deffered */
		press_aswindow (asw, event->context);
	}


	if (!deffered && !eat_click) {
		LOCAL_DEBUG_OUT ("checking for associated functions...%s", "");
		/* we have to execute a function or pop up a menu : */
		modifier = (xbtn->state & nonlock_mods);
		LOCAL_DEBUG_OUT ("state = %X, modifier = %X", xbtn->state, modifier);
		/* need to search for an appropriate mouse binding */
		MouseEntry = Scr.Feel.MouseButtonRoot;
		while (MouseEntry != NULL) {
			/*LOCAL_DEBUG_OUT( "mouse fdata %p button %d + modifier %X has context %lx", MouseEntry->fdata, MouseEntry->Button, MouseEntry->Modifier, get_flags(MouseEntry->Context, event->context) ); */
			if ((MouseEntry->Button == xbtn->button || MouseEntry->Button == 0)
					&& (MouseEntry->Context & event->context)
					&& (MouseEntry->Modifier == AnyModifier
							|| MouseEntry->Modifier == modifier)) {
				/* got a match, now process it */
				if (MouseEntry->fdata != NULL) {
					ExecuteFunction (MouseEntry->fdata, event, -1);
					raise_on_click = False;
					AShandled = True;
					break;
				}
			}
			MouseEntry = MouseEntry->NextButton;
		}
	}

	LOCAL_DEBUG_OUT ("ashandled = %d, context = %X", AShandled,
									 (event->context & (C_CLIENT | C_TITLE)));
	if (activate_window
			&& (!AShandled || (event->context & (C_CLIENT | C_TITLE))))
		focus_accepted = activate_aswindow (asw, False, False);

	if (raise_on_click
			&& (asw->internal == NULL || (event->context & C_CLIENT) == 0))
		restack_window (asw, None, focus_accepted ? Above : TopIf);

	/* GNOME this click hasn't been taken by AfterStep */
	if (!deffered && !AShandled && !eat_click && xbtn->window == Scr.Root) {
		XUngrabPointer (dpy, CurrentTime);
		XSendEvent (dpy, Scr.wmprops->wm_event_proxy, False,
								SubstructureNotifyMask, &(event->x));
	}
}

/***********************************************************************
 *  Procedure:
 *  HandleButtonRelease - De-press currently pressed window if all buttons are up
 ***********************************************************************/
void HandleButtonRelease (ASEvent * event, Bool deffered)
{																/* click to focus stuff goes here */
	LOCAL_DEBUG_CALLER_OUT ("pressed(%p)->state(0x%X)", Scr.Windows->pressed,
													(event->x.xbutton.
													 state & (Button1Mask | Button2Mask | Button3Mask
																		| Button4Mask | Button5Mask)));
	if ((event->x.xbutton.state & AllButtonMask) ==
			(Button1Mask << (event->x.xbutton.button - Button1)))
		release_pressure ();
}

/***********************************************************************
 *
 *  Procedure:
 *	HandleEnterNotify - EnterNotify event handler
 *
 ************************************************************************/
void HandleEnterNotify (ASEvent * event)
{
	XEnterWindowEvent *ewp = &(event->x.xcrossing);
	XEvent d;
	ASWindow *asw = event->client;
	int i;
/*fprintf (stderr, "XCROSSING: EnterNotify for window %lX\n", ewp->window); fflush(stderr);*/
	/* look for a matching leaveNotify which would nullify this enterNotify */
	if (ewp->window != Scr.Root)
		if (ASCheckTypedWindowEvent (ewp->window, LeaveNotify, &d)) {
/*fprintf (stderr, "XCROSSING: LeaveNotify in queue for window %lX\n", ewp->window); fflush(stderr);*/
			on_astbar_pointer_action (NULL, 0, True, False);
			if ((d.xcrossing.mode == NotifyNormal)
					&& (d.xcrossing.detail != NotifyInferior)) {
/*fprintf (stderr, "XCROSSING: ignoring EnterNotify for window %lX\n", ewp->window); fflush(stderr);*/
				return;
			}
		}
/* an EnterEvent in one of the PanFrameWindows activates the Paging */
#ifndef NO_VIRTUAL
	for (i = 0; i < PAN_FRAME_SIDES; i++) {
		LOCAL_DEBUG_OUT ("checking panframe %d, mapped %d", i,
										 Scr.PanFrame[i].isMapped);
		fflush (stderr);
		if (Scr.PanFrame[i].isMapped && ewp->window == Scr.PanFrame[i].win) {
			int delta_x = 0, delta_y = 0;
			if (!get_flags (Scr.Feel.flags, ClickToFocus)) {
				/* After HandlePaging the configuration of windows on screen will change and
				   we can no longer keep old focused window still focused, as it may be off-screen
				   or otherwise not under the pointer, resulting in input going into the wrong window.
				 */
				if (Scr.Windows->focused != NULL)
					hide_focus ();
				if (Scr.Windows->hilited != NULL)
					hide_hilite ();
			}
/*fprintf (stderr, "XCROSSING: EnterNotify for panframe %d\n", i); fflush(stderr);*/
			/* this was in the HandleMotionNotify before, HEDU */
			HandlePaging (Scr.Feel.EdgeScrollX, Scr.Feel.EdgeScrollY,
										&(ewp->x_root), &(ewp->y_root), &delta_x, &delta_y,
										True, event);

			return;
		}
	}
#endif													/* NO_VIRTUAL */

	if (ewp->window == Scr.Root) {
		if (!get_flags (Scr.Feel.flags, ClickToFocus | SloppyFocus))
			hide_focus ();
		InstallRootColormap ();
		return;
	} else if (event->context != C_WINDOW)
		InstallAfterStepColormap ();

	/* make sure its for one of our windows */
	if (asw == NULL)
		return;
/*fprintf (stderr, "XCROSSING: focused = %lX active = %lX\n", Scr.Windows->focused?Scr.Windows->focused->w:0, Scr.Windows->active?Scr.Windows->active->w:0); fflush(stderr);*/
	if (ASWIN_FOCUSABLE (asw)) {
		if (!get_flags (Scr.Feel.flags, ClickToFocus) || asw->internal != NULL) {
			if (Scr.Windows->focused != asw)
				activate_aswindow (asw, False, False);
		}
		if (!ASWIN_GET_FLAGS (asw, AS_Iconic) && event->context == C_WINDOW)
			InstallWindowColormaps (asw);
	}
}

/***********************************************************************
 *
 *  Procedure:
 *	HandleLeaveNotify - LeaveNotify event handler
 *
 ************************************************************************/
void HandleLeaveNotify (ASEvent * event)
{
	XEnterWindowEvent *ewp = &(event->x.xcrossing);
	/* If we leave the root window, then we're really moving
	 * another screen on a multiple screen display, and we
	 * need to de-focus and unhighlight to make sure that we
	 * don't end up with more than one highlighted window at a time */
/*fprintf (stderr, "XCROSSING: LeaveNotify for window %lX\n", ewp->window); fflush(stderr);*/
	if (ewp->window == Scr.Root) {
		if (ewp->mode == NotifyNormal) {
			if (ewp->detail != NotifyInferior) {
				if (Scr.Windows->focused != NULL)
					hide_focus ();
				if (Scr.Windows->hilited != NULL)
					hide_hilite ();
			}
		}
	}
}
