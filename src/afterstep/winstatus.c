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

/**********************************************************************
 *
 * Add a new window, put the titlbar and other stuff around
 * the window
 *
 **********************************************************************/
#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"
#include "../../libAfterStep/wmprops.h"

/* ASWindow frame decorations management was split out of this file into window_frame.c */

void save_aswindow_anchor (ASWindow * asw, Bool hor, Bool vert)
{
	if (hor && asw->saved_anchor.width == 0
			&& asw->status->width < Scr.MyDisplayWidth) {
		asw->saved_anchor.x = asw->anchor.x;
		asw->saved_anchor.width = asw->anchor.width;
	}
	if (vert && asw->saved_anchor.height == 0
			&& asw->status->height < Scr.MyDisplayHeight) {
		asw->saved_anchor.y = asw->anchor.y;
		asw->saved_anchor.height = asw->anchor.height;
	}
}

void
moveresize_aswindow_wm (ASWindow * asw, int x, int y, unsigned int width,
												unsigned int height, Bool save_anchor)
{
	LOCAL_DEBUG_CALLER_OUT ("asw(%p)->geom(%dx%d%+d%+d)->save_anchor(%d)",
													asw, width, height, x, y, save_anchor);
	if (!AS_ASSERT (asw)) {
		ASStatusHints scratch_status = *(asw->status);
		scratch_status.x = x;
		scratch_status.y = y;
		if (width > 0)
			scratch_status.width = width;
		if (height > 0)
			scratch_status.height = height;

		if (ASWIN_GET_FLAGS (asw, AS_Shaded)) {	/* tricky tricky */
			if (ASWIN_HFLAGS (asw, AS_VerticalTitle))
				scratch_status.width = asw->status->width;
			else
				scratch_status.height = asw->status->height;
		}
		if (save_anchor)
			save_aswindow_anchor (asw, True, True);
		/* need to apply two-way conversion in order to make sure that size restrains are applied : */
		status2anchor (&(asw->anchor), asw->hints, &scratch_status,
									 Scr.VxMax + Scr.MyDisplayWidth,
									 Scr.VyMax + Scr.MyDisplayHeight);
		anchor2status (asw->status, asw->hints, &(asw->anchor));

		/* now lets actually resize the window : */
		apply_window_status_size (asw, get_orientation_data (asw));
	}
}

Bool init_aswindow_status (ASWindow * t, ASStatusHints * status)
{
	Bool pending_placement = False;

	if (t->status == NULL) {
		t->status = safecalloc (1, sizeof (ASStatusHints));
		*(t->status) = *status;
	}
	if (get_flags (status->flags, AS_StartDesktop)
			&& status->desktop != Scr.CurrentDesk) {
		t->status->desktop = status->desktop;
		if (get_flags (AfterStepState, ASS_NormalOperation))
			ChangeDesks (status->desktop);
	} else
		t->status->desktop = Scr.CurrentDesk;

	if (get_flags (status->flags, AS_StartViewportX)
			&& get_flags (AfterStepState, ASS_NormalOperation)) {
		t->status->viewport_x = MIN (status->viewport_x, Scr.VxMax);
		t->status->x %= Scr.MyDisplayWidth;
		if (!get_flags (t->status->flags, AS_Sticky))
			t->status->x += t->status->viewport_x;
	} else if (!get_flags (t->status->flags, AS_Sticky)) {
		if (t->status->x < 0)
			t->status->viewport_x = 0;
		else
			t->status->viewport_x =
					(t->status->x / Scr.MyDisplayWidth) * Scr.MyDisplayWidth;
		if (!get_flags (AfterStepState, ASS_NormalOperation))
			set_flags (status->flags, AS_StartViewportX);
	} else
		t->status->x %= Scr.MyDisplayWidth;

	if (get_flags (status->flags, AS_StartViewportY)
			&& get_flags (AfterStepState, ASS_NormalOperation)) {
		t->status->viewport_y = MIN (status->viewport_y, Scr.VyMax);
		t->status->y %= Scr.MyDisplayWidth;
		if (!get_flags (t->status->flags, AS_Sticky))
			t->status->y += t->status->viewport_y;
	} else if (!get_flags (t->status->flags, AS_Sticky)) {
		if (t->status->y < 0)
			t->status->viewport_y = 0;
		else
			t->status->viewport_y =
					(t->status->y / Scr.MyDisplayHeight) * Scr.MyDisplayHeight;
		if (!get_flags (AfterStepState, ASS_NormalOperation))
			set_flags (status->flags, AS_StartViewportY);
	} else
		t->status->y %= Scr.MyDisplayHeight;

	if (t->status->viewport_x != Scr.Vx || t->status->viewport_y != Scr.Vy) {
		int new_vx =
				get_flags (status->flags,
									 AS_StartViewportX) ? t->status->viewport_x : Scr.Vx;
		int new_vy =
				get_flags (status->flags,
									 AS_StartViewportY) ? t->status->viewport_y : Scr.Vy;
		MoveViewport (new_vx, new_vy, False);
		t->status->viewport_x = Scr.Vx;
		t->status->viewport_y = Scr.Vy;
	}
	if (!get_flags (t->status->flags, AS_Sticky)) {
		Bool absolute_origin = (!ASWIN_HFLAGS (t, AS_UseCurrentViewport));

		if (absolute_origin && get_flags (t->hints->flags, AS_Transient) && get_flags (t->status->flags, AS_StartPositionUser)) {	/* most likely stupid KDE or GNOME app that is abusing USPosition
																																																															   for no good reason - place it on current viewport */
			absolute_origin = (t->status->x >= Scr.MyDisplayWidth ||
												 t->status->y >= Scr.MyDisplayHeight);
		}
		if (absolute_origin) {
			t->status->x -= t->status->viewport_x;
			t->status->y -= t->status->viewport_y;
		}
	}
	LOCAL_DEBUG_OUT ("status->pos = %+d%+d, Scr.Vpos = %+d%+d", t->status->x,
									 t->status->y, Scr.Vx, Scr.Vy);

	/* TODO: AS_Iconic */
	if (!ASWIN_GET_FLAGS (t, AS_StartLayer))
		ASWIN_LAYER (t) = AS_LayerNormal;
	else if (ASWIN_LAYER (t) < AS_LayerLowest)
		ASWIN_LAYER (t) = AS_LayerLowest;
	else if (ASWIN_LAYER (t) > AS_LayerHighest)
		ASWIN_LAYER (t) = AS_LayerHighest;

	if (get_flags (t->status->flags, AS_Fullscreen)) {
		t->status->width = Scr.MyDisplayWidth;
		t->status->height = Scr.MyDisplayHeight;
		t->status->x = 0;
		t->status->y = 0;
	} else {
		if (get_flags (t->hints->flags, AS_MinSize)) {
			int width = t->status->width;
			int height = t->status->height;
			if ((!get_flags (t->status->flags, AS_StartSizeUser)
					 && width < t->hints->min_width) || width == 1)
				width = min (t->hints->min_width, Scr.VxMax + Scr.MyDisplayWidth);
			if ((!get_flags (t->status->flags, AS_StartSizeUser)
					 && height < t->hints->min_height) || height == 1)
				height =
						min (t->hints->min_height, Scr.VyMax + Scr.MyDisplayHeight);
			if (width != t->status->width || height != t->status->height) {
				int dx = 0, dy = 0;
				if (t->hints->gravity == EastGravity ||
						t->hints->gravity == SouthEastGravity ||
						t->hints->gravity == NorthEastGravity ||
						t->hints->gravity == CenterGravity)
					dx = (int)(t->status->width) - width;
				if (t->hints->gravity == SouthGravity ||
						t->hints->gravity == SouthEastGravity ||
						t->hints->gravity == SouthWestGravity ||
						t->hints->gravity == CenterGravity)
					dy = (int)(t->status->height) - height;
				if (t->hints->gravity == EastGravity
						|| t->hints->gravity == CenterGravity)
					dx = dx / 2;
				if (t->hints->gravity == SouthGravity
						|| t->hints->gravity == CenterGravity)
					dy = dy / 2;
				t->status->x += dx;
				t->status->y += dy;
				t->status->width = width;
				t->status->height = height;
				XResizeWindow (dpy, t->w, width, height);
			}
		}
		if (!get_flags (AfterStepState, ASS_NormalOperation)) {
			int min_x, min_y, max_x, max_y;
			int margin = Scr.MyDisplayWidth >> 5;
			if (!ASWIN_GET_FLAGS (t, AS_Sticky)) {
				min_x = -Scr.Vx;
				max_x = Scr.VxMax + Scr.MyDisplayWidth;
				min_y = -Scr.Vy;
				max_y = Scr.VyMax + Scr.MyDisplayHeight;
			} else {
				min_x = 0;
				max_x = Scr.MyDisplayWidth;
				min_y = 0;
				max_y = Scr.MyDisplayHeight;
			}
			/* we have to make sure that window is visible !!!! */
			LOCAL_DEBUG_OUT ("x_range(%d,%d), y_range(%d,%d), margin = %d",
											 min_x, max_x, min_y, max_y, margin);
			if ((int)t->status->x + (int)t->status->width < min_x + margin)
				t->status->x = min_x + margin - (int)t->status->width;
			else if ((int)t->status->x > max_x - margin)
				t->status->x = max_x - margin;
			if ((int)t->status->y + (int)t->status->height < min_y + margin)
				t->status->y = min_y + margin - (int)t->status->height;
			else if ((int)t->status->y > max_y - margin)
				t->status->y = max_y - margin;

			LOCAL_DEBUG_OUT ("status->pos = %+d%+d, Scr.Vpos = %+d%+d",
											 t->status->x, t->status->y, Scr.Vx, Scr.Vy);

			set_flags (t->status->flags, AS_Position);

		} else if (get_flags (Scr.Feel.flags, NoPPosition)) {

			if (!get_flags (t->hints->flags, AS_Transient) &&
					!get_flags (t->status->flags, AS_StartPositionUser))
				clear_flags (t->status->flags, AS_Position);
		}
		if (get_flags (status->flags, AS_MaximizedX | AS_MaximizedY))
			pending_placement = True;
		else if (!get_flags (t->status->flags, AS_Position)) {
			if (!get_flags (t->status->flags, AS_StartsIconic)) {
				int x = -1, y = -1;
				pending_placement = True;
				ASQueryPointerRootXY (&x, &y);
				if (get_flags (t->hints->flags, AS_Transient | AS_ShortLived)) {
					x -= (int)t->status->width / 2;
					y -= (int)t->status->height / 2;
					set_flags (t->status->flags, AS_Position);
					pending_placement = False;
				}

				if (x + (int)t->status->width > (int)Scr.MyDisplayWidth)
					x = (int)Scr.MyDisplayWidth - (int)t->status->width;
				if (x < 0)
					x = 0;

				if (y + (int)t->status->height > (int)Scr.MyDisplayHeight)
					y = Scr.MyDisplayHeight - (int)t->status->height;
				if (y < 0)
					y = 0;

				t->status->x = x;
				t->status->y = y;

				LOCAL_DEBUG_OUT ("status->pos = %+d%+d, Scr.Vpos = %+d%+d",
												 t->status->x, t->status->y, Scr.Vx, Scr.Vy);
			}
		}
	}

	if (is_output_level_under_threshold (OUTPUT_LEVEL_HINTS))
		print_status_hints (NULL, NULL, t->status);

	/* by now we have a valid position for the window: */
	set_flags (t->status->flags, AS_Position);

	status2anchor (&(t->anchor), t->hints, t->status,
								 Scr.VxMax + Scr.MyDisplayWidth,
								 Scr.VyMax + Scr.MyDisplayHeight);
	LOCAL_DEBUG_OUT
			("status->geom=%dx%d%+d%+d,status->viewport=%+d%+d,anchor=%dx%d%+d%+d",
			 t->status->width, t->status->height, t->status->x, t->status->y,
			 t->status->viewport_x, t->status->viewport_y, t->anchor.width,
			 t->anchor.height, t->anchor.x, t->anchor.y);

	if (!pending_placement) {
		validate_window_anchor (t, &(t->anchor), True);
		anchor2status (t->status, t->hints, &(t->anchor));
	}

	return pending_placement;
}

/* Window state transitions were split out of this file into window_state.c */
