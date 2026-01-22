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
#include "../../libAfterStep/moveresize.h"
#include "../../libAfterStep/canvas.h"

static void refresh_root_geometry (void)
{
	setupScreenSize (&Scr);
	if (Scr.RootCanvas)
		handle_canvas_config (Scr.RootCanvas);
	get_Xinerama_rectangles (&Scr);
	check_screen_panframes (&Scr);
	if (Scr.wmprops)
		set_desktop_geometry_prop (Scr.wmprops,
														 Scr.VxMax + Scr.MyDisplayWidth,
														 Scr.VyMax + Scr.MyDisplayHeight);
}

void DispatchEvent (ASEvent * event, Bool deffered)
{
	if (Scr.moveresize_in_progress)
		if (check_moveresize_event (event))
			return;

	/* handle menu events specially */
	/* if (HandleMenuEvent (NULL, event) == True)
	 *  return;
	 */

	switch (event->x.type) {
	case KeyPress:
		/* if a key has been pressed and it's not one of those that cause
		   warping, we know the warping is finished */
		HandleKeyPress (event);
		break;
	case ButtonPress:
		/* if warping, a button press, non-warp keypress, or pointer motion
		 * indicates that the warp is done */
		if (get_flags (AfterStepState, ASS_WarpingMode))
			EndWarping ();
		if (event->x.xbutton.button > Button3) {	/* buttons 4 and 5 are for scrollwheel */
			ASInternalWindow *internal =
					event->client ? event->client->internal : NULL;
			if (internal && internal->on_scroll_event)
				internal->on_scroll_event (internal, event);
		} else
			HandleButtonPress (event, deffered);
		break;
	case ButtonRelease:
		/* if warping, a button press, non-warp keypress, or pointer motion
		 * indicates that the warp is done */
		if (get_flags (AfterStepState, ASS_WarpingMode))
			EndWarping ();
		if (Scr.Windows->pressed)
			HandleButtonRelease (event, deffered);
		break;
	case MotionNotify:
		/* if warping, a button press, non-warp keypress, or pointer motion
		 * indicates that the warp is done */
		if (get_flags (AfterStepState, ASS_WarpingMode))
			EndWarping ();
		if (event->client && event->client->internal)
			event->client->internal->on_pointer_event (event->client->internal,
																								 event);
		break;
	case EnterNotify:
		HandleEnterNotify (event);
		break;
	case LeaveNotify:
		HandleLeaveNotify (event);
		break;
	case FocusIn:
		HandleFocusIn (event);
		break;
	case Expose:
		HandleExpose (event);
		break;
	case DestroyNotify:
		HandleDestroyNotify (event);
		break;
	case UnmapNotify:
		HandleUnmapNotify (event);
		break;
	case MapNotify:
		HandleMapNotify (event);
		break;
	case MapRequest:
		HandleMapRequest (event);
		break;
		case ConfigureNotify:
			if (event->client) {
				LOCAL_DEBUG_CALLER_OUT
						("ConfigureNotify:(%p,%lx,asw->w=%lx,(%dx%d%+d%+d)",
					 	 event->client, event->w, event->client->w,
					 	 event->x.xconfigure.width, event->x.xconfigure.height,
					 	 event->x.xconfigure.x, event->x.xconfigure.y);
				on_window_moveresize (event->client, event->w);
			}else if (event->w == Scr.Root) {
				LOCAL_DEBUG_CALLER_OUT ("ConfigureNotify:(RootWindow,%dx%d)",
																event->x.xconfigure.width,
																event->x.xconfigure.height);
				refresh_root_geometry ();
			}
			break;
	case ConfigureRequest:
		HandleConfigureRequest (event);
		break;
	case PropertyNotify:
		HandlePropertyNotify (event);
		break;
	case ColormapNotify:
		HandleColormapNotify (event);
		break;
	case ClientMessage:
		HandleClientMessage (event);
		break;
	case SelectionClear:
		HandleSelectionClear (event);
		break;
	default:
#ifdef HAVE_XRANDR
		if (Scr.RandREventBase != 0) {
			if (event->x.type == (Scr.RandREventBase + RRScreenChangeNotify)) {
				XRRUpdateConfiguration (&event->x);
				refresh_root_geometry ();
				break;
			}
			if (event->x.type == (Scr.RandREventBase + RRNotify)) {
				refresh_root_geometry ();
				break;
			}
		}
#endif
#ifdef SHAPE
		if (event->x.type == (Scr.ShapeEventBase + ShapeNotify))
			HandleShapeNotify (event);
#endif													/* SHAPE */
#ifdef XSHMIMAGE
		LOCAL_DEBUG_OUT
				("XSHMIMAGE> EVENT : completion_type = %d, event->type = %d ",
				 Scr.ShmCompletionEventType, event->x.type);
		if (event->x.type == Scr.ShmCompletionEventType)
			HandleShmCompletion (event);
#endif													/* SHAPE */

		break;
	}
	return;
}

