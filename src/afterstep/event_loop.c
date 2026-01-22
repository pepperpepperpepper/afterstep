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

static void afterstep_wait_pipes_input (int timeout_sec);

static int
_exec_while_x_pending ()
{
	int handled_count = 0;
	ASEvent event;
	while (XPending (dpy)) {
		if (ASNextEvent (&(event.x), True)) {
			DigestEvent (&event);
			DispatchEvent (&event, False);
			++handled_count;
		}
		asdbus_process_messages (0);
		ASSync (False);
		/* before we exec any function - we ought to process any Unmap and Destroy
		 * events to handle all the pending window destroys : */
		while (ASCheckTypedEvent (DestroyNotify, &(event.x)) ||
					 ASCheckTypedEvent (UnmapNotify, &(event.x)) ||
					 ASCheckMaskEvent (FocusChangeMask, &(event.x))) {
			DigestEvent (&event);
			DispatchEvent (&event, False);
			++handled_count;
		}
		asdbus_process_messages (0);
		ExecutePendingFunctions ();
	}
	return handled_count;
}


void HandleEvents ()
{
	/* this is the only loop that allowed to run ExecutePendingFunctions(); */
	while (True) {
		_exec_while_x_pending ();
		afterstep_wait_pipes_input (0);
		ExecutePendingFunctions ();
	}
}

void HandleEventsWhileFunctionsPending ()
{
	int events_handled = 1;
	/* this is the only loop that allowed to run ExecutePendingFunctions(); */
	while (FunctionsPending () || events_handled > 0) {
		ExecutePendingFunctions ();
		afterstep_wait_pipes_input (3);
		events_handled = _exec_while_x_pending ();
	}
}


/***************************************************************************
 * Wait for all mouse buttons to be released
 * This can ease some confusion on the part of the user sometimes
 *
 * Discard superflous button events during this wait period.
 ***************************************************************************/
#define MOVERESIZE_LOOP_MASK 	(KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask| \
								EnterWindowMask|LeaveWindowMask|PointerMotionMask|PointerMotionHintMask| \
								Button1MotionMask|Button2MotionMask|Button3MotionMask|Button4MotionMask| \
								Button5MotionMask|ButtonMotionMask|KeymapStateMask| \
								StructureNotifyMask|SubstructureNotifyMask)


void InteractiveMoveLoop ()
{
	ASEvent event;
	Bool has_x_events = False;
	while (Scr.moveresize_in_progress != NULL) {
		LOCAL_DEBUG_OUT ("checking masked events ...%s", "");
		while ((has_x_events =
						ASCheckMaskEvent (MOVERESIZE_LOOP_MASK, &(event.x)))) {
			DigestEvent (&event);
			DispatchEvent (&event, False);
			if (Scr.moveresize_in_progress == NULL)
				return;
		}
		afterstep_wait_pipes_input (0);
	}
}



void WaitForButtonsUpLoop ()
{
	XEvent JunkEvent;
	unsigned int mask;

	if (!get_flags (AfterStepState, ASS_PointerOutOfScreen)) {
		do {
			XAllowEvents (dpy, ReplayPointer, CurrentTime);
			ASQueryPointerMask (&mask);
			ASFlushAndSync ();
		} while ((mask &
							(Button1Mask | Button2Mask | Button3Mask | Button4Mask |
							 Button5Mask)) != 0);

		while (ASCheckMaskEvent
					 (ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
						&JunkEvent))
			XAllowEvents (dpy, ReplayPointer, CurrentTime);
	}
}

Bool
WaitEventLoop (ASEvent * event, int finish_event_type, long timeout,
							 ASHintWindow * hint)
{
	unsigned long mask = ButtonPressMask | ButtonReleaseMask |
			ExposureMask | KeyPressMask | ButtonMotionMask |
			PointerMotionMask /* | EnterWindowMask | LeaveWindowMask */ ;
	Bool done = False;

	while (!done) {
		/* block until there is an event */
		ASFlushIfEmpty ();
		ASMaskEvent (mask, &(event->x));

		if (event->x.type == KeyPress)
			KeyboardShortcuts (&(event->x), finish_event_type, 20);
		/* above line might have changed event code !!! */
		else if (event->x.type == ButtonPress)
			XAllowEvents (dpy, ReplayPointer, CurrentTime);

		if (event->x.type == finish_event_type) {
			done = True;
			if (event->x.xbutton.window == Scr.Root)
				event->x.xbutton.window = event->x.xbutton.subwindow;
			/* otherwise event will be reported as if it occured relative to
			   root window */
		}
		DigestEvent (event);
		DispatchEvent (event, done);
		if (event->x.type == MotionNotify && hint)
			update_ashint_geometry (hint, False);
	}

	return True;
}

/*****************************************************************************
 * Waits click_time, or until it is evident that the user is not
 * clicking, but is moving the cursor
 ****************************************************************************/
Bool
IsClickLoop (ASEvent * event, unsigned int end_mask,
						 unsigned int click_time)
{
	int dx = 0, dy = 0;
	int x_orig = event->x.xbutton.x_root;
	int y_orig = event->x.xbutton.y_root;
	/* we are in the middle of running Complex function - we must only do mandatory
	 * processing on received events, but do not actually handle them !!
	 * Client window affected, as well as the context must not change -
	 * only the X event could */
	ASEvent tmp_event;
	register XEvent *xevt = &(tmp_event.x);

	ASSync (False);
	start_ticker (click_time);
	do {
		sleep_a_millisec (10);
		if (ASCheckMaskEvent (end_mask, xevt)) {
			DigestEvent (&tmp_event);
			event->x = *xevt;					/* everything else must remain the same !!! */
			DispatchEvent (event, True);
			return True;
		}
		if (is_tick ())
			break;

		if (ASCheckMaskEvent (ButtonMotionMask | PointerMotionMask, xevt)) {
			dx = x_orig - xevt->xmotion.x_root;
			dy = y_orig - xevt->xmotion.y_root;
			DigestEvent (&tmp_event);
			event->x = *xevt;					/* everything else must remain the same !!! */
		}
	} while (dx > -5 && dx < 5 && dy > -5 && dy < 5);

	return False;
}

ASWindow *WaitWindowLoop (char *pattern, long timeout)
{
	Bool done = False;
	ASEvent event;
	Bool has_x_events;
	time_t end_time =
			(timeout <= 0 ? DEFAULT_WINDOW_WAIT_TIMEOUT : timeout) / 100;
	time_t click_end_time = end_time / 4;
	time_t start_time = time (NULL);
	ASWindow *asw = NULL;
	ASHintWindow *hint;
	char *text;

	end_time += start_time;
	click_end_time += start_time;

	if (pattern == NULL || pattern[0] == '\0')
		return NULL;

	LOCAL_DEBUG_OUT ("waiting for \"%s\"", pattern);
	if ((asw = complex_pattern2ASWindow (pattern)) != NULL)
		return asw;

	text = safemalloc (64 + strlen (pattern) + 1);
	sprintf (text,
					 "Waiting for window matching \"%s\" ... Press button to cancel.",
					 pattern);
	hint = create_ashint_window (ASDefaultScr, &(Scr.Look), text);
	free (text);
	while (!done) {
		do {
			ASFlush ();
			while ((has_x_events = XPending (dpy))) {
				if (ASNextEvent (&(event.x), True)) {

					DigestEvent (&event);
					/* we do not want user to do anything interactive at that time - hence
					   deffered == True */
					DispatchEvent (&event, True);
					if (event.x.type == ButtonPress || event.x.type == KeyPress) {
						end_time = click_end_time;
						break;
					} else if (event.x.type == MotionNotify && hint)
						update_ashint_geometry (hint, False);

					if ((event.x.type == MapNotify || event.x.type == PropertyNotify)
							&& event.client)
						if ((asw = complex_pattern2ASWindow (pattern)) != NULL) {
							destroy_ashint_window (&hint);
							return asw;
						}
				}
			}
			if (time (NULL) > end_time) {
				done = True;
				break;
			}

		} while (has_x_events);

		if (time (NULL) > end_time)
			break;
		afterstep_wait_pipes_input (1);
	}
	destroy_ashint_window (&hint);
	return NULL;
}

/*************************************************************************
 * This loop handles all the pending Configure Notifys so that canvases get
 * all nicely synchronized. This is generaly needed when we need to do
 * reparenting.
 *************************************************************************/
void ConfigureNotifyLoop ()
{
	ASEvent event;
	while (ASCheckTypedEvent (ConfigureNotify, &(event.x))) {
		DigestEvent (&event);
		DispatchEvent (&event, False);
		ASSync (False);
	}
}

void MapConfigureNotifyLoop ()
{
	ASEvent event;

	do {
		if (!ASCheckTypedEvent (MapNotify, &(event.x)))
			if (!ASCheckTypedEvent (ConfigureNotify, &(event.x)))
				return;
		DigestEvent (&event);
		DispatchEvent (&event, False);
		ASSync (False);
	} while (1);
}

/****************************************************************************
 * For menus, move, and resize operations, we can effect keyboard
 * shortcuts by warping the pointer.
 ****************************************************************************/
Bool KeyboardShortcuts (XEvent * xevent, int return_event, int move_size)
{
	int x, y, x_root, y_root;
	int x_move, y_move;
	KeySym keysym;

	/* Pick the size of the cursor movement */
	if (xevent->xkey.state & ControlMask)
		move_size = 1;
	if (xevent->xkey.state & ShiftMask)
		move_size = 100;

	keysym = XLookupKeysym (&(xevent->xkey), 0);

	x_move = 0;
	y_move = 0;
	switch (keysym) {
	case XK_Up:
	case XK_k:
	case XK_p:
		y_move = -move_size;
		break;
	case XK_Down:
	case XK_n:
	case XK_j:
		y_move = move_size;
		break;
	case XK_Left:
	case XK_b:
	case XK_h:
		x_move = -move_size;
		break;
	case XK_Right:
	case XK_f:
	case XK_l:
		x_move = move_size;
		break;
	case XK_Return:
	case XK_space:
		/* beat up the event */
		xevent->type = return_event;
		break;
	default:
		return False;
	}
	ASQueryPointerXY (&xevent->xany.window, &x_root, &y_root, &x, &y);

	if ((x_move != 0) || (y_move != 0)) {
		/* beat up the event */
		XWarpPointer (dpy, None, Scr.Root, 0, 0, 0, 0, x_root + x_move,
									y_root + y_move);

		/* beat up the event */
		xevent->type = MotionNotify;
		xevent->xkey.x += x_move;
		xevent->xkey.y += y_move;
		xevent->xkey.x_root += x_move;
		xevent->xkey.y_root += y_move;
	}
	return True;
}

/***************************************************************************
 *
 * Waits for next X event, or for an auto-raise timeout.
 *
 ****************************************************************************/
static void afterstep_wait_pipes_input (int timeout_sec)
{
	fd_set in_fdset, out_fdset;
	int retval;
	struct timeval tv;
	struct timeval *t = NULL;
	int max_fd = 0;
	ASVector *asdbus_fds = NULL;

	if (ASDBusConnected)
		asdbus_fds = asdbus_getFds();


	LOCAL_DEBUG_OUT ("waiting pipes%s", "");
	FD_ZERO (&in_fdset);
	FD_ZERO (&out_fdset);

	FD_SET (x_fd, &in_fdset);
	max_fd = x_fd;
#define AS_FD_SET(fd,fdset) \
	do{ if (fd>=0) { FD_SET((fd),(fdset)); if ((fd)>max_fd) max_fd = (fd);}}while(0)

	LOCAL_DEBUG_OUT ("asdbus_fds = %p", asdbus_fds);
	if (asdbus_fds != NULL) {
		register int i;
		LOCAL_DEBUG_OUT ("asdbus_fds->used = %d", asdbus_fds->used);
		for ( i = 0 ; i < asdbus_fds->used; ++i) {
			ASDBusFd* fd = PVECTOR_HEAD(ASDBusFd*,asdbus_fds)[i];
			LOCAL_DEBUG_OUT ("asdbus_fds[%d] = %p", i, fd);
			if (fd && fd->readable){
				AS_FD_SET (fd->fd, &in_fdset);
				LOCAL_DEBUG_OUT ("adding asdbus_fds[%d].fd = %d", i, fd->fd);
			}
		}
	}
	LOCAL_DEBUG_OUT ("done with asdbus_fds", "");

	AS_FD_SET (Module_fd, &in_fdset);

	if (Modules != NULL) {				/* adding all the modules pipes to our wait list */
		register int i = MIN (MODULES_NUM, Module_npipes);
		register module_t *list = MODULES_LIST;
		while (--i >= 0) {
			if (list[i].fd >= 0) {
				AS_FD_SET (list[i].fd, &in_fdset);
				if (list[i].output_queue != NULL)
					FD_SET (list[i].fd, &out_fdset);
			} else										/* man, this modules is dead! get rid of it - it stinks! */
				vector_remove_index (Modules, i);
		}
	}

	/* watch for timeouts */
	if (timer_delay_till_next_alarm
			((time_t *) & tv.tv_sec, (time_t *) & tv.tv_usec))
		t = &tv;
	else if (timeout_sec > 0) {
		t = &tv;
		tv.tv_sec = timeout_sec;
		tv.tv_usec = 0;
	}

	show_debug (__FILE__, __FUNCTION__, __LINE__,"selecting ... max_fd = %d, timeout : sec = %d, usec = %d", max_fd, t?t->tv_sec:-1, t?t->tv_usec:-1);
	retval =
			PORTABLE_SELECT (min (max_fd + 1, fd_width), &in_fdset, &out_fdset,
											 NULL, t);

	LOCAL_DEBUG_OUT ("select ret val = %d", retval);
	if (retval > 0) {
		register module_t *list;
		register int i;
		/* check for incoming module connections */
		if (Module_fd >= 0)
			if (FD_ISSET (Module_fd, &in_fdset))
				if (AcceptModuleConnection (Module_fd) != -1)
					show_progress ("accepted module connection");
		/* note that we have to do it AFTER we accepted incoming connections as those alter the list */
		list = MODULES_LIST;
		i = MIN (MODULES_NUM, Module_npipes);
		/* Check for module input. */
		while (--i >= 0)
			if (list[i].fd > 0) {
				Bool has_input = FD_ISSET (list[i].fd, &in_fdset);
				Bool has_output = FD_ISSET (list[i].fd, &out_fdset);
				if (has_input || has_output)
					HandleModuleInOut (i, has_input, has_output);
			}
		if (asdbus_fds != NULL) {
			register int i;
			for ( i = 0 ; i < asdbus_fds->used; ++i) {
				ASDBusFd* fd = PVECTOR_HEAD(ASDBusFd*,asdbus_fds)[i];
				show_debug(__FILE__,__FUNCTION__,__LINE__, "dbus fd = %d, isset = %d", fd->fd, FD_ISSET (fd->fd, &in_fdset));
				if (fd && FD_ISSET (fd->fd, &in_fdset)){
					asdbus_process_messages (fd);
					break;
				}
			}
		}
	}

	/* handle timeout events */
	timer_handle ();
	asdbus_handleDispatches ();

}
