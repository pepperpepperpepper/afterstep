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
 * Check to see if the pointer is on the edge of the screen, and scroll/page
 * if needed
 ***************************************************************************/
void
HandlePaging (int HorWarpSize, int VertWarpSize, int *xl, int *yt,
							int *delta_x, int *delta_y, Bool Grab, ASEvent * event)
{
#ifndef NO_VIRTUAL
	int x, y, total;
#endif
	Window wdumm;
	int dumm;
	unsigned int udumm;

	*delta_x = 0;
	*delta_y = 0;

#ifndef NO_VIRTUAL
	if (DoHandlePageing) {
		int scroll = Scr.Feel.EdgeResistanceScroll;

		if (Scr.moveresize_in_progress
				&& Scr.Feel.EdgeResistanceDragScroll >= 0)
			scroll = Scr.Feel.EdgeResistanceDragScroll;

		if ((scroll >= 10000) || ((HorWarpSize == 0) && (VertWarpSize == 0)))
			return;

		/* need to move the viewport */
		if ((*xl >= SCROLL_REGION)
				&& (*xl < Scr.MyDisplayWidth - SCROLL_REGION)
				&& (*yt >= SCROLL_REGION)
				&& (*yt < Scr.MyDisplayHeight - SCROLL_REGION))
			return;

		total = 0;
		while (total < scroll) {
			register int i;
			sleep_a_millisec (10);
			total += 10;
			for (i = 0; i < PAN_FRAME_SIDES; i++)
				if (Scr.PanFrame[i].isMapped)
					if (ASCheckWindowEvent
							(Scr.PanFrame[i].win, LeaveWindowMask, &(event->x)))
						return;
		}

		XQueryPointer (dpy, Scr.Root, &wdumm, &wdumm, &x, &y, &dumm, &dumm,
									 &udumm);

		/* fprintf (stderr, "-------- MoveOutline () called from pager.c\ntmp_win == 0xlX\n", (long int) tmp_win); */
		/* Turn off the rubberband if its on */
		/*        MoveOutline ( Scr.Root,  tmp_win, 0, 0, 0, 0); */

		/* Move the viewport */
		/* and/or move the cursor back to the approximate correct location */
		/* that is, the same place on the virtual desktop that it */
		/* started at */
		if (x < SCROLL_REGION)
			*delta_x = -HorWarpSize;
		else if (x >= Scr.MyDisplayWidth - SCROLL_REGION)
			*delta_x = HorWarpSize;
		else
			*delta_x = 0;
		if (y < SCROLL_REGION)
			*delta_y = -VertWarpSize;
		else if (y >= Scr.MyDisplayHeight - SCROLL_REGION)
			*delta_y = VertWarpSize;
		else
			*delta_y = 0;

		/* Ouch! lots of bounds checking */
		if (Scr.Vx + *delta_x < 0) {
			if (!get_flags (Scr.Feel.flags, EdgeWrapX)) {
				*delta_x = -Scr.Vx;
				*xl = x - *delta_x;
			} else {
				*delta_x += Scr.VxMax + Scr.MyDisplayWidth;
				*xl = x + *delta_x % Scr.MyDisplayWidth + HorWarpSize;
			}
		} else if (Scr.Vx + *delta_x > Scr.VxMax) {
			if (!get_flags (Scr.Feel.flags, EdgeWrapX)) {
				*delta_x = Scr.VxMax - Scr.Vx;
				*xl = x - *delta_x;
			} else {
				*delta_x -= Scr.VxMax + Scr.MyDisplayWidth;
				*xl = x + *delta_x % Scr.MyDisplayWidth - HorWarpSize;
			}
		} else
			*xl = x - *delta_x;

		if (Scr.Vy + *delta_y < 0) {
			if (!get_flags (Scr.Feel.flags, EdgeWrapY)) {
				*delta_y = -Scr.Vy;
				*yt = y - *delta_y;
			} else {
				*delta_y += Scr.VyMax + Scr.MyDisplayHeight;
				*yt = y + *delta_y % Scr.MyDisplayHeight + VertWarpSize;
			}
		} else if (Scr.Vy + *delta_y > Scr.VyMax) {
			if (!get_flags (Scr.Feel.flags, EdgeWrapY)) {
				*delta_y = Scr.VyMax - Scr.Vy;
				*yt = y - *delta_y;
			} else {
				*delta_y -= Scr.VyMax + Scr.MyDisplayHeight;
				*yt = y + *delta_y % Scr.MyDisplayHeight - VertWarpSize;
			}
		} else
			*yt = y - *delta_y;

		if (*xl <= SCROLL_REGION)
			*xl = SCROLL_REGION + 1;
		if (*yt <= SCROLL_REGION)
			*yt = SCROLL_REGION + 1;
		if (*xl >= Scr.MyDisplayWidth - SCROLL_REGION)
			*xl = Scr.MyDisplayWidth - SCROLL_REGION - 1;
		if (*yt >= Scr.MyDisplayHeight - SCROLL_REGION)
			*yt = Scr.MyDisplayHeight - SCROLL_REGION - 1;

		if ((*delta_x != 0) || (*delta_y != 0)) {
			if (Grab)
				grab_server ();
/* Pointer warping on viewport change is considered harmfull.
Negative side effects include:
1) Sometime window don't get focus due to distorted sequence on EnterNotify/LeaveNotify
2) User feels at a loss, trying to find where the god damn pointer has jumped to.
If they move it to the edge of the screen - they expect it to stay at the edge of the screen.
*/
#if 0
			fprintf (stderr, "XCROSSING: focused = %lX active = %lX\n",
							 Scr.Windows->focused ? Scr.Windows->focused->w : 0,
							 Scr.Windows->active ? Scr.Windows->active->w : 0);
			fflush (stderr);
			fprintf (stderr, "XCROSSING: curr = %+d%+d, orig = %+d%+d\n",
							 xroot_curr, yroot_curr, xroot_orig, yroot_orig);
			fflush (stderr);
			fprintf (stderr, "XCROSSING: XWarpPointer to %+d%+d\n", *xl, *yt);
			fflush (stderr);
			int xroot_curr, yroot_curr;
			if (xroot_curr == xroot_orig && yroot_curr == yroot_orig)
#endif
/* only want to warp pointer while move-resizing, to keep size from jumping screenwhole */
				if (Scr.moveresize_in_progress
						|| get_flags (Scr.Feel.flags, WarpPointer))
					XWarpPointer (dpy, None, Scr.Root, 0, 0, 0, 0, *xl, *yt);

			MoveViewport (Scr.Vx + *delta_x, Scr.Vy + *delta_y, False);
			XQueryPointer (dpy, Scr.Root, &wdumm, &wdumm, xl, yt, &dumm, &dumm,
										 &udumm);

			if (Grab)
				ungrab_server ();
		}
	}
#endif
}

