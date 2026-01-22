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
#define EVENT_TRACE

#include "../../configure.h"

#include "asinternals.h"

#include <limits.h>
#include <sys/types.h>
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
#include <unistd.h>
#include <signal.h>

#include "../../libAfterStep/wmprops.h"
#include "../../libAfterStep/moveresize.h"
#include "../../libAfterStep/canvas.h"

#include <X11/keysym.h>
#ifdef XSHMIMAGE
# include <sys/ipc.h>
# include <sys/shm.h>
# include <X11/extensions/XShm.h>
#endif

/***********************************************************************
 *  _______________________EVENT HANDLING ______________________________
 *
 *  HandleEvents  - event loop
 *  DigestEvent   - preprocesses event - finds ASWindow, context etc.
 *  DispatchEvent - calls appropriate handler for the event
 ************************************************************************/
void DigestEvent (ASEvent * event);

static void digest_event_moveresize_pointer_event (ASEvent * event)
{
	event->context = C_ROOT;
	event->widget = Scr.RootCanvas;
	/* we have to do this at all times !!!! */
	if (event->x.type == ButtonRelease && Scr.Windows->pressed)
		release_pressure ();
}

static void digest_event_resolve_target_window (ASEvent * event)
{
	register int i;
	XButtonEvent *xbtn = &(event->x.xbutton);

	if (event->w == Scr.Root) {
		event->context = C_ROOT;
#ifndef NO_VIRTUAL
		if ((event->eclass & ASE_MousePressEvent) != 0
				&& xbtn->subwindow != None) {
			LOCAL_DEBUG_OUT ("subwindow = %lX", event->x.xbutton.subwindow);
			for (i = 0; i < PAN_FRAME_SIDES; i++) {
				LOCAL_DEBUG_OUT ("checking panframe %d, mapped %d", i,
												 Scr.PanFrame[i].isMapped);
				if (Scr.PanFrame[i].isMapped && xbtn->subwindow == Scr.PanFrame[i].win) {	/* we should try and pass through this click onto any client that
																																										   maybe under the pan frames */
					LOCAL_DEBUG_OUT ("looking for client at %+d%+d", xbtn->x_root,
													 xbtn->y_root);
					if ((event->client =
							 find_topmost_client (Scr.CurrentDesk, xbtn->x_root,
																		xbtn->y_root)) != NULL) {
						ASCanvas *cc = event->client->client_canvas;
						LOCAL_DEBUG_OUT ("underlying new client %p", event->client);
						if (cc && cc->root_x <= xbtn->x_root
								&& cc->root_y <= xbtn->y_root
								&& cc->root_x + cc->width > xbtn->x_root
								&& cc->root_y + cc->height > xbtn->y_root) {
							event->w = xbtn->window = event->client->w;
							xbtn->subwindow = None;
							XSendEvent (dpy, event->client->w, False,
													(xbtn->type ==
													 ButtonPress) ? ButtonPressMask :
													ButtonReleaseMask, &(event->x));
							xbtn->subwindow = event->client->w;
							xbtn->window = Scr.Root;
							/* xbtn->window should still be Root, so we can proxy it down to the app */
						} else {
							event->w = xbtn->window = event->client->frame;
							xbtn->subwindow = None;
						}
						LOCAL_DEBUG_OUT ("new event window %lX", event->w);
						event->context = C_NO_CONTEXT;
					}
					break;
				}
			}
		}
#endif
	} else if (event->w == Scr.ServiceWin || event->w == Scr.SizeWindow) {
		event->context = C_ROOT;
	} else
		event->context = C_NO_CONTEXT;

	if (event->context == C_ROOT) {
		event->widget = Scr.RootCanvas;
		event->client = NULL;
	} else if (event->client == NULL) {
		if ((event->eclass & ASE_POINTER_EVENTS) != 0
				&& is_balloon_click (&(event->x)) != NULL) {
			event->client = NULL;
			event->widget = NULL;
		} else {
			event->widget = NULL;
			event->client = window2ASWindow (event->w);
		}
	}
}

static void digest_event_resolve_pointer_context (ASEvent * event)
{
	register int i;
	Window w = event->w;
	ASWindow *asw = event->client;
	XKeyEvent *xk = &(event->x.xkey);
	ASCanvas *canvas = asw->frame_canvas;
	ASTBarData *pointer_bar = NULL;
	int pointer_root_x = xk->x_root;
	int pointer_root_y = xk->y_root;
	static int last_pointer_root_x = -1, last_pointer_root_y = -1;
	int tbar_side = ASWIN_HFLAGS (asw, AS_VerticalTitle) ? FR_W : FR_N;

	/* now lets determine the context of the event : (former GetContext) */
	/* Since key presses and button presses are grabbed in the frame
	 * when we have re-parented windows, we need to find out the real
	 * window where the event occured */
	if (!ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		if (w != asw->client_canvas->w)
			if (xk->subwindow != None)
				w = xk->subwindow;
		if (w == asw->client_canvas->w) {
			canvas = asw->client_canvas;
			event->context = C_CLIENT;
		} else if (w != asw->frame) {
			i = FRAME_SIDES;
			while (--i >= 0)
				if (asw->frame_sides[i] != NULL && asw->frame_sides[i]->w == w) {
					canvas = asw->frame_sides[i];
					/* determine what part of the frame : */
					event->context = C_FRAME;
					break;
				}
		} else {									/* we are on the border of the frame : see what side ofthe frame we are on */
			event->context = C_FRAME;
			if (pointer_root_x <
					asw->frame_canvas->root_x + (int)asw->frame_canvas->bw)
				pointer_root_x =
						asw->frame_canvas->root_x + (int)asw->frame_canvas->bw;
			else if (pointer_root_x >=
							 asw->frame_canvas->root_x + (int)asw->frame_canvas->bw +
							 (int)asw->frame_canvas->width)
				pointer_root_x =
						asw->frame_canvas->root_x + (int)asw->frame_canvas->bw +
						(int)asw->frame_canvas->width - 1;
			if (pointer_root_y <
					asw->frame_canvas->root_y + (int)asw->frame_canvas->bw)
				pointer_root_y =
						asw->frame_canvas->root_y + (int)asw->frame_canvas->bw;
			else if (pointer_root_y >=
							 asw->frame_canvas->root_y + (int)asw->frame_canvas->bw +
							 (int)asw->frame_canvas->height)
				pointer_root_y =
						asw->frame_canvas->root_y + (int)asw->frame_canvas->bw +
						(int)asw->frame_canvas->height - 1;
			else
				event->context = C_CLIENT;

		}

		if (ASWIN_GET_FLAGS (asw, AS_Shaded)
				&& canvas != asw->frame_sides[tbar_side]) {
			event->context = C_NO_CONTEXT;
			if (asw->frame_sides[tbar_side])
				XRaiseWindow (dpy, asw->frame_sides[tbar_side]->w);
		} else if (w != asw->frame) {
			if (event->w == asw->frame) {
				xk->x = pointer_root_x - (canvas->root_x + (int)canvas->bw);
				xk->y = pointer_root_y - (canvas->root_y + (int)canvas->bw);
			} else {
				Window dumm;
				XTranslateCoordinates (dpy, Scr.Root, w, xk->x_root, xk->y_root,
															 &(xk->x), &(xk->y), &dumm);
			}
		}
		if (event->context == C_FRAME) {
			int tbar_context;
			if (asw->tbar != NULL &&
					(tbar_context =
					 check_astbar_point (asw->tbar, pointer_root_x,
															 pointer_root_y)) != C_NO_CONTEXT) {
				event->context = tbar_context;
				pointer_bar = asw->tbar;
			} else {
				for (i = 0; i < FRAME_PARTS; ++i)
					if (asw->frame_bars[i] != NULL &&
							(tbar_context =
							 check_astbar_point (asw->frame_bars[i], pointer_root_x,
																	 pointer_root_y)) != C_NO_CONTEXT) {
						event->context = tbar_context;
						pointer_bar = asw->frame_bars[i];
						break;
					}
			}
		}
		if (event->context == C_NO_CONTEXT
				&& get_flags (Scr.Feel.flags, ClickToFocus)) {
			w = asw->frame;
			event->context = C_FRAME;
		}
		event->w = w;
	} else {
		if (asw->icon_canvas && w == asw->icon_canvas->w) {
			event->context = C_IconButton;
			canvas = asw->icon_canvas;
			pointer_bar = asw->icon_button;
			if (canvas == asw->icon_title_canvas) {
				int c =
						check_astbar_point (asw->icon_title, pointer_root_x,
																pointer_root_y);
				if (c != C_NO_CONTEXT) {
					event->context = c;
					pointer_bar = asw->icon_title;
				}
			}
		} else if (asw->icon_title_canvas && w == asw->icon_title_canvas->w) {
			canvas = asw->icon_title_canvas;
			event->context = C_IconTitle;
			pointer_bar = asw->icon_title;
		}
	}

	if (pointer_bar != NULL) {
		on_astbar_pointer_action (pointer_bar, event->context,
															(event->x.type == LeaveNotify),
															(last_pointer_root_x != pointer_root_x
															 || last_pointer_root_y !=
															 pointer_root_y));
	}
	if (event->x.type == LeaveNotify) {
		withdraw_active_balloon_from (TitlebarBalloons);
	}
	last_pointer_root_x = pointer_root_x;
	last_pointer_root_y = pointer_root_y;

	if (asw != NULL && w != asw->w && w != asw->frame
			&& event->context != C_NO_CONTEXT)
		apply_context_cursor (w, &(Scr.Feel), event->context);
	event->widget = canvas;
	/* we have to do this at all times !!!! */
	/* if( event->x.type == ButtonRelease && Scr.Windows->pressed )
	   release_pressure(); */
}

void DigestEvent (ASEvent * event)
{
	setup_asevent_from_xevent (event);
	event->client = NULL;
	SHOW_EVENT_TRACE (event);
	/* in housekeeping mode we handle pointer events only as applied to root window ! */
	if (Scr.moveresize_in_progress
			&& (event->eclass & ASE_POINTER_EVENTS) != 0) {
		digest_event_moveresize_pointer_event (event);
	} else {
		digest_event_resolve_target_window (event);
	}

	if ((event->eclass & ASE_POINTER_EVENTS) != 0 && event->client) {
		digest_event_resolve_pointer_context (event);
	}
	SHOW_EVENT_TRACE (event);
}
