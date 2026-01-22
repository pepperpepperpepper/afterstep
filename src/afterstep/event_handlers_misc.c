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

#ifdef XSHMIMAGE
# include <sys/ipc.h>
# include <sys/shm.h>
# include <X11/extensions/XShm.h>
#endif

/***********************************************************************
 *
 *  Procedure:
 *	HandleExpose - expose event handler
 *
 ***********************************************************************/
void HandleExpose (ASEvent * event)
{
	/* do nothing on expose - we use doublebuffering !!! */
}

/***********************************************************************
 *
 *  Procedure:
 *      HandleShapeNotify - shape notification event handler
 *
 ***********************************************************************/
void HandleShapeNotify (ASEvent * event)
{
#ifdef SHAPE
	XShapeEvent *sev = (XShapeEvent *) & (event->x);
	Window w;
	Bool needs_update = (sev->kind == ShapeBounding);
	Bool shaped = sev->shaped;

	if (!event->client)
		return;
	if (event->client->w != sev->window)
		return;

	w = event->client->w;
	while (ASCheckTypedWindowEvent
				 (w, Scr.ShapeEventBase + ShapeNotify, &(event->x))) {
		if (sev->kind == ShapeBounding) {
			needs_update = True;
			shaped = sev->shaped;
		}
		ASSync (False);
		sleep_a_millisec (10);
	}

	if (needs_update) {
		if (shaped)
			ASWIN_SET_FLAGS (event->client, AS_Shaped);
		else
			ASWIN_CLEAR_FLAGS (event->client, AS_Shaped);
		if (refresh_container_shape (event->client->client_canvas))
			SetShape (event->client, 0);
	}
#endif													/* SHAPE */
}

void HandleShmCompletion (ASEvent * event)
{
#ifdef XSHMIMAGE
	XShmCompletionEvent *sev = (XShmCompletionEvent *) & (event->x);
	LOCAL_DEBUG_OUT ("XSHMIMAGE> EVENT : offset   %ld(%lx), shmseg = %lx",
									 (long)sev->offset, (unsigned long)(sev->offset),
									 sev->shmseg);
	if (!is_background_xfer_ximage (sev->shmseg))
		destroy_xshm_segment (sev->shmseg);
#endif													/* SHAPE */
}

