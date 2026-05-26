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

/* Frame geometry and layout calculations split out from window_frame.c. */

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"
#include "../../libAfterStep/wmprops.h"
#include "window_frame_internal.h"

Bool check_canvas_offscreen (ASCanvas * pc)
{
	if (!pc)
		return True;
	return (pc->root_x >= Scr.MyDisplayWidth
					|| pc->root_y >= Scr.MyDisplayHeight
					|| pc->root_x + (int)pc->width < 0
					|| pc->root_y + (int)pc->height < 0);
}


Bool check_window_offscreen (ASWindow * asw)
{
	if (!ASWIN_GET_FLAGS (asw, AS_Sticky) &&
			ASWIN_DESK (asw) != Scr.CurrentDesk)
		return True;

	return check_canvas_offscreen (asw->client_canvas);
}

Bool check_frame_offscreen (ASWindow * asw)
{
	if (!ASWIN_GET_FLAGS (asw, AS_Sticky) &&
			ASWIN_DESK (asw) != Scr.CurrentDesk)
		return True;

	return check_canvas_offscreen (asw->frame_canvas);
}

Bool check_frame_side_offscreen (ASWindow * asw, int side)
{
	if (!ASWIN_GET_FLAGS (asw, AS_Sticky) &&
			ASWIN_DESK (asw) != Scr.CurrentDesk)
		return True;

	return check_canvas_offscreen (asw->frame_sides[side]);
}

#define GetNormalBarHeight(h,b,od)  \
	do{if((b)!=NULL){ *((od)->in_width)=(b)->width; *((od)->in_height)=(b)->height;(h) = *((od)->out_height);} \
	   else (h) = 0; \
	  }while(0)


static unsigned int make_corner_addon (ASOrientation * od,
																			 ASTBarData * longbar,
																			 ASTBarData * corner1,
																			 ASTBarData * corner2)
{
	unsigned int longbar_height = 0, c1_height = 0, c2_height = 0;
	GetNormalBarHeight (longbar_height, longbar, od);
	GetNormalBarHeight (c1_height, corner1, od);
	GetNormalBarHeight (c2_height, corner2, od);
	LOCAL_DEBUG_OUT ("longbar_height = %d, c1_height = %d, c2_height = %d",
									 longbar_height, c1_height, c2_height);
	if (c1_height >= c2_height)
		return (c1_height > longbar_height) ? c1_height - longbar_height : 0;
	else
		return (c2_height > longbar_height) ? c2_height - longbar_height : 0;
}

void
resize_canvases (ASWindow * asw, ASOrientation * od,
								 unsigned int normal_width, unsigned int normal_height,
								 unsigned int *frame_sizes)
{
	unsigned short tbar_size = frame_sizes[od->tbar_side];
	unsigned short sbar_size = frame_sizes[od->sbar_side];
	unsigned short tbar_addon, sbar_addon;

	/* we need to determine if corners are bigger then frame bars on sbar and tbar : */
	tbar_addon =
			make_corner_addon (od, asw->frame_bars[od->tbar_side],
												 asw->frame_bars[od->tbar_mirror_corners[0]],
												 asw->frame_bars[od->tbar_mirror_corners[1]]);
	sbar_addon =
			make_corner_addon (od, asw->frame_bars[od->sbar_side],
												 asw->frame_bars[od->sbar_mirror_corners[0]],
												 asw->frame_bars[od->sbar_mirror_corners[1]]);
	tbar_size += tbar_addon;
	sbar_size += sbar_addon;
	LOCAL_DEBUG_OUT
			("tbar_size = %d, addon = %d, sbar_size = %d, addon = %d, frame_sizes = {%d,%d}",
			 tbar_size, tbar_addon, sbar_size, sbar_addon,
			 frame_sizes[od->tbar_side], frame_sizes[od->sbar_side]);
	*(od->in_width) = normal_width;
	*(od->in_height) = tbar_size;
	if (asw->frame_sides[od->tbar_side])
		moveresize_canvas (asw->frame_sides[od->tbar_side], 0, 0,
											 *(od->out_width), *(od->out_height));

	*(od->in_x) = 0;
	*(od->in_y) = normal_height - sbar_size;
	*(od->in_height) = sbar_size;
	if (asw->frame_sides[od->sbar_side])
		moveresize_canvas (asw->frame_sides[od->sbar_side], *(od->out_x),
											 *(od->out_y), *(od->out_width), *(od->out_height));

	/* for left and right sides - somewhat twisted logic - we mirror sides over lt2rb diagonal in case of
	 * vertical title orientation. That allows us to apply simple x/y switching instead of complex
	 * calculations. Note that we only do that for placement purposes. Contexts and images are still taken
	 * from MyFrame parts as if it was rotated counterclockwise instead of mirrored.
	 */
	*(od->in_x) = 0;
	*(od->in_y) = tbar_size;
	*(od->in_width) = frame_sizes[od->left_mirror_side];
	*(od->in_height) = normal_height - (tbar_size + sbar_size);
	if (asw->frame_sides[od->left_mirror_side])
		moveresize_canvas (asw->frame_sides[od->left_mirror_side],
											 *(od->out_x), *(od->out_y), *(od->out_width),
											 *(od->out_height));

	*(od->in_x) = normal_width - frame_sizes[od->right_mirror_side];
	*(od->in_width) = frame_sizes[od->right_mirror_side];
	if (asw->frame_sides[od->right_mirror_side])
		moveresize_canvas (asw->frame_sides[od->right_mirror_side],
											 *(od->out_x), *(od->out_y), *(od->out_width),
											 *(od->out_height));
}

#if 0
static unsigned short frame_side_height (ASCanvas * c1, ASCanvas * c2)
{
	unsigned short h = 0;
	if (c1)
		h += c1->height;
	if (c2)
		h += c2->height;
	return h;
}

static unsigned short frame_side_width (ASCanvas * c1, ASCanvas * c2)
{
	unsigned short w = 0;
	if (c1)
		w += c1->width;
	if (c2)
		w += c2->width;
	return w;
}

static unsigned short
frame_corner_height (ASTBarData * c1, ASTBarData * c2)
{
	unsigned short h = 0;
	if (c1)
		h += c1->height;
	if (c2)
		h += c2->height;
	return h;
}

static unsigned short frame_corner_width (ASTBarData * c1, ASTBarData * c2)
{
	unsigned short w = 0;
	if (c1)
		w += c1->width;
	if (c2)
		w += c2->width;
	return w;
}
#endif

int make_shade_animation_step (ASWindow * asw, ASOrientation * od)
{
	int step_size = 0;
	if (asw->tbar) {
		int steps = asw->shading_steps;

		if (steps > 0) {
			int from_size, to_size;
			*(od->in_width) = asw->frame_canvas->width;
			*(od->in_height) = asw->frame_canvas->height;
			from_size = *(od->out_height);
			if (ASWIN_GET_FLAGS (asw, AS_Shaded)) {
				*(od->in_width) = asw->tbar->width;
				*(od->in_height) = asw->tbar->height;
			} else {
				*(od->in_width) = asw->status->width;
				*(od->in_height) = asw->status->height;
			}
			to_size = *(od->out_height);

			if (from_size != to_size) {
				int step_delta = (from_size - to_size) / steps;
				if (step_delta == 0)
					step_delta = (from_size > to_size) ? 1 : -1;
				LOCAL_DEBUG_OUT ("@@ANIM to(%d)->from(%d)->delta(%d)->step(%d)",
												 to_size, from_size, step_delta, steps);

				step_size = from_size - step_delta;
				--(asw->shading_steps);
			}
		} else if (!ASWIN_GET_FLAGS (asw, AS_Shaded)) {
			/* when we shade the window - focus gets set to frame window -
			   we need to revert it back to the client : */
			if (Scr.Windows->focused == asw) {
				focus_window (asw, asw->w);
				autoraise_window (asw);
			}
			if (!ASWIN_GET_FLAGS (asw, AS_Dead))
				XRaiseWindow (dpy, asw->w);
			return 0;
		} else {
			*(od->in_width) = asw->tbar->width;
			*(od->in_height) = asw->tbar->height;
			return *(od->out_height);
		}
	}
	return step_size;
}

inline static Bool
move_resize_frame_bar (ASTBarData * tbar, ASCanvas * canvas, int normal_x,
											 int normal_y, unsigned int normal_width,
											 unsigned int normal_height, Bool force_render)
{
	if (set_astbar_size (tbar, normal_width, normal_height))
		force_render = True;
	if (move_astbar (tbar, canvas, normal_x, normal_y))
		force_render = True;
	if (force_render)
		render_astbar (tbar, canvas);
	return force_render;
}

inline static Bool
move_resize_corner (ASTBarData * bar, ASCanvas * canvas,
										ASOrientation * od, int normal_y,
										unsigned int normal_width, unsigned int normal_height,
										Bool left, Bool force_render)
{
	unsigned int w = bar->width;
	unsigned int h = bar->height;

	*(od->in_y) = normal_y;

	if (od == &VertOrientation)
		w = normal_height - normal_y;
	else
		h = normal_height - normal_y;
	LOCAL_DEBUG_OUT
			(" w = %d, h = %d, bar->width = %d, bar->height = %d, n_w = %d, n_h = %d, n_y = %d",
			 w, h, bar->width, bar->height, normal_width, normal_height,
			 normal_y);
	*(od->in_width) = w;
	*(od->in_height) = h;
	*(od->in_x) = left ? 0 : (normal_width - (*(od->out_width)));
	return move_resize_frame_bar (bar, canvas, *(od->out_x), *(od->out_y), w,
																h, force_render);
}

inline static Bool
move_resize_longbar (ASTBarData * bar, ASCanvas * canvas,
										 ASOrientation * od, int normal_offset,
										 unsigned int normal_length, unsigned int corner_size1,
										 unsigned int corner_size2, Bool vertical,
										 Bool force_render)
{
	unsigned int w = bar->width;
	unsigned int h = bar->height;
	int bar_size;

	LOCAL_DEBUG_OUT
			("normal_offset = %d, normal_length = %d, corner_sizes = %d,%d, vert = %d, force = %d",
			 normal_offset, normal_length, corner_size1, corner_size2, vertical,
			 force_render);
	/* swapping bar height in case of vertical orientation of the etire window: */
	*(od->in_width) = w;
	*(od->in_height) = h;

	if (vertical) {								/*vertical bar - west or east */
		bar_size = *(od->out_width);
		*(od->in_width) = bar_size;
		*(od->in_height) = normal_length;
		*(od->in_x) = normal_offset;
		*(od->in_y) = corner_size1;
	} else {
		bar_size = *(od->out_height);
		*(od->in_height) = bar_size;
		if (corner_size1 + corner_size2 > normal_length)
			*(od->in_width) = 1;
		else
			*(od->in_width) = normal_length - (corner_size1 + corner_size2);
		*(od->in_x) = corner_size1;
		*(od->in_y) = normal_offset;
	}

	return move_resize_frame_bar (bar, canvas, *(od->out_x), *(od->out_y),
																*(od->out_width), *(od->out_height),
																force_render);
}

static unsigned int
condense_tbar (ASTBarData * tbar, unsigned int max_size,
							 unsigned int *off1, unsigned int *off2, Bool vert,
							 ASFlagType align)
{
	unsigned int condensed = max_size;
	*off1 = 0;
	*off2 = 0;

#ifdef SHAPE
	if (get_flags (align, ALIGN_LEFT | ALIGN_RIGHT)) {
		if (vert) {
			condensed = calculate_astbar_height (tbar);
			if (condensed < max_size) {
				if (get_flags (align, ALIGN_LEFT)) {
					*off1 = max_size - condensed;
					if (get_flags (align, ALIGN_RIGHT)) {
						*off1 /= 2;
						*off2 = *off1;
					}
				} else
					*off2 = max_size - condensed;
			} else
				condensed = max_size;
		} else {
			condensed = calculate_astbar_width (tbar);
			if (condensed < max_size) {
				if (get_flags (align, ALIGN_RIGHT)) {
					*off1 = max_size - condensed;
					if (get_flags (align, ALIGN_LEFT)) {
						*off1 /= 2;
						*off2 = *off1;
					}
				} else
					*off2 = max_size - condensed;
			} else
				condensed = max_size;
		}
	}
#endif
	return condensed;
}


static Bool
move_resize_frame_bars (ASWindow * asw, int side, ASOrientation * od,
												unsigned int normal_width,
												unsigned int normal_height, Bool force_render)
{
	int corner_size1 = 0, corner_size2 = 0;
	Bool rendered = False;
	int tbar_size = 0;
	ASCanvas *canvas = asw->frame_sides[side];
	ASTBarData *title = NULL, *corner1 = NULL, *longbar = NULL, *corner2 =
			NULL;
	Bool vertical = False;

	LOCAL_DEBUG_CALLER_OUT ("%p,%d, %ux%u, %s", asw, side, normal_width,
													normal_height,
													force_render ? "force" : "don't force");
	longbar = asw->frame_bars[side];
	if (side == od->tbar_side) {
		title = asw->tbar;
		corner1 = asw->frame_bars[od->tbar_mirror_corners[0]];
		corner2 = asw->frame_bars[od->tbar_mirror_corners[1]];
	} else if (side == od->sbar_side) {
		corner1 = asw->frame_bars[od->sbar_mirror_corners[0]];
		corner2 = asw->frame_bars[od->sbar_mirror_corners[1]];
	} else
		vertical = True;

	if (title) {
		unsigned int tbar_offset1, tbar_offset2, tbar_width;

		tbar_width =
				condense_tbar (title, normal_width, &tbar_offset1, &tbar_offset2,
											 ASWIN_HFLAGS (asw, AS_VerticalTitle),
											 asw->frame_data->condense_titlebar);
		if (tbar_offset1 > 0 || tbar_offset2 > 0 || tbar_width != normal_width)
			set_flags (canvas->state, CANVAS_FORCE_MASK);

		/* title always considered a "horizontal bar" */
		if (move_resize_longbar
				(title, canvas, od, 0, normal_width, tbar_offset1, tbar_offset2,
				 False, force_render))
			rendered = True;
		tbar_size = *(od->in_height);
	}
	/* mirror_corner 0 */
	if (corner1) {
		if (move_resize_corner
				(corner1, canvas, od, tbar_size, normal_width, normal_height, True,
				 force_render))
			rendered = True;
		corner_size1 = *(od->out_width);
	}
	/* mirror_corner 1 */
	if (corner2) {
		if (move_resize_corner
				(corner2, canvas, od, tbar_size, normal_width, normal_height,
				 False, force_render))
			rendered = True;
		corner_size2 = *(od->out_width);
	}
	/* side */
	if (longbar) {
		if (side != od->tbar_side && (corner_size1 > 0 || corner_size2 > 0)) {	/* we are in the sbar */
			*(od->in_width) = longbar->width;
			*(od->in_height) = longbar->height;
			tbar_size = normal_height - (int)(*(od->out_height));
			if (tbar_size < 0)
				tbar_size = 0;
		}
		if (move_resize_longbar (longbar, canvas, od,
														 tbar_size,
														 vertical ? normal_height : normal_width,
														 corner_size1, corner_size2, vertical,
														 force_render))
			rendered = True;
	}

	return rendered;
}

ASFlagType
resize_frame_subwindows (ASWindow * asw, ASOrientation * od,
												 unsigned int frame_win_width,
												 unsigned int frame_win_height)
{
	register unsigned int *frame_size = &(asw->status->frame_size[0]);
	unsigned int normal_width, normal_height;
	ASFlagType client_changes = 0;

	if (od == NULL)
		od = get_orientation_data (asw);

	*(od->in_width) = asw->frame_canvas->width;
	*(od->in_height) = asw->frame_canvas->height;
	normal_width = *(od->out_width);
	normal_height = *(od->out_height);

	resize_canvases (asw, od, normal_width, normal_height, frame_size);
	if (!ASWIN_GET_FLAGS (asw, AS_Shaded))	/* leave shaded client alone ! */
		client_changes = moveresize_canvas (asw->client_canvas,
																				frame_size[FR_W], frame_size[FR_N],
																				(int)frame_win_width -
																				(int)(frame_size[FR_W] +
																							frame_size[FR_E]),
																				(int)frame_win_height -
																				(int)(frame_size[FR_N] +
																							frame_size[FR_S]));
	return client_changes;
}

Bool
check_frame_side_config (ASWindow * asw, Window w, ASOrientation * od)
{
	Bool found = False;
	int i;
	unsigned int normal_width, normal_height;

	for (i = 0; i < FRAME_SIDES; ++i)
		if (asw->frame_sides[i] && asw->frame_sides[i]->w == w) {	/* canvas has beer resized - resize tbars!!! */
			Bool changes = handle_canvas_config (asw->frame_sides[i]);

			/* we must resize using current window size instead of event's size */
			*(od->in_width) = asw->frame_sides[i]->width;
			*(od->in_height) = asw->frame_sides[i]->height;
			normal_width = *(od->out_width);
			normal_height = *(od->out_height);

			/* don't redraw window decoration while in the middle of animation : */
			if (asw->shading_steps <= 0) {
				if (move_resize_frame_bars (asw, i, od, normal_width, normal_height, changes) || changes) {	/* now we need to show them on screen !!!! */
					update_canvas_display (asw->frame_sides[i]);
					if (ASWIN_GET_FLAGS (asw, AS_Shaped | AS_ShapedDecor))
						SetShape (asw, 0);
					else if (get_flags
									 (asw->internal_flags, ASWF_PendingShapeRemoval))
						ClearShape (asw);
				}
			}
			found = True;
			break;
		}
	return found;
}

void
move_shading_frame (ASWindow * asw, ASOrientation * od, int step_size)
{
	if (asw->frame_sides[od->sbar_side]) {
		XRaiseWindow (dpy, asw->frame_sides[od->sbar_side]->w);
		if (ASWIN_HFLAGS (asw, AS_VerticalTitle))
			move_canvas (asw->frame_sides[od->sbar_side],
									 step_size - asw->frame_sides[od->sbar_side]->width, 0);
		else
			move_canvas (asw->frame_sides[od->sbar_side], 0,
									 step_size - asw->frame_sides[od->sbar_side]->height);
	}
	if (asw->frame_sides[od->tbar_side])
		XRaiseWindow (dpy, asw->frame_sides[od->tbar_side]->w);
}

void
validate_window_anchor (ASWindow * asw, XRectangle * new_anchor,
												Bool initial_placement)
{
	if (asw) {
		ASStatusHints status = *(asw->status);
		anchor2status (&status, asw->hints, new_anchor);
		LOCAL_DEBUG_OUT ("status geometry = %dx%d%+d%+d", status.width,
										 status.height, status.x, status.y);

		if (ASWIN_HFLAGS (asw, AS_AvoidCover | AS_ShortLived) != AS_AvoidCover) {
			obey_avoid_cover (asw, &status, new_anchor,
												initial_placement ? AS_LayerHighest :
												ASWIN_LAYER (asw));
		}
	}
}
