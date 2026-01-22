#include "pager_internal.h"

/*****************************************************************************
 * Grab the pointer and keyboard
 ****************************************************************************/
static ScreenInfo *grabbed_screen = NULL;

Bool GrabEm (ScreenInfo * scr, Cursor cursor)
{
	int i = 0;
	unsigned int mask;
	int res;

	XSync (dpy, 0);
	/* move the keyboard focus prior to grabbing the pointer to
	 * eliminate the enterNotify and exitNotify events that go
	 * to the windows */
	grabbed_screen = scr;

	mask = ButtonPressMask | ButtonReleaseMask | ButtonMotionMask |
			PointerMotionMask | EnterWindowMask | LeaveWindowMask;
	while ((res =
					XGrabPointer (dpy, PagerState.main_canvas->w, True, mask,
												GrabModeAsync, GrabModeAsync, scr->Root, cursor,
												CurrentTime)) != GrabSuccess) {
		if (i++ >= 1000) {
#define MAX_GRAB_ERROR 4
			static char *_as_grab_error_code[MAX_GRAB_ERROR + 1 + 1] = {
				"Grab Success",
				"pointer is actively grabbed by some other client",
				"the specified time is earlier than the last-pointer-grab time or later than the current X server time",
				"window is not viewable or lies completely outside the boundaries of the root window",
				"pointer is frozen by an active grab of another client",
				"I'm totally messed up - restart me please"
			};
			char *error_text = _as_grab_error_code[MAX_GRAB_ERROR + 1];
			if (res <= MAX_GRAB_ERROR)
				error_text = _as_grab_error_code[res];

			show_warning
					("Failed to grab pointer for requested interactive operation.(X server says:\"%s\")",
					 error_text);
			return False;
		}
		/* If you go too fast, other windows may not get a change to release
		 * any grab that they have. */
		sleep_a_millisec (100);
		XSync (dpy, 0);
	}
	return True;
}

/*****************************************************************************
 * UnGrab the pointer and keyboard
 ****************************************************************************/
void UngrabEm ()
{
	if (grabbed_screen) {					/* check if we grabbed everything */
		XSync (dpy, 0);
		XUngrabPointer (dpy, CurrentTime);
		XSync (dpy, 0);
		grabbed_screen = NULL;
	}
}
