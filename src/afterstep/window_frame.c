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

/* icon geometry relative to the root window :                      */
Bool get_icon_root_geometry (ASWindow * asw, ASRectangle * geom)
{
	ASCanvas *canvas = NULL;
	if (AS_ASSERT (asw) || AS_ASSERT (geom))
		return False;

	geom->height = 0;
	if (asw->icon_canvas) {
		canvas = asw->icon_canvas;
		if (asw->icon_title_canvas && asw->icon_title_canvas != canvas)
			geom->height = asw->icon_title_canvas->height;
	} else if (asw->icon_title_canvas)
		canvas = asw->icon_title_canvas;

	if (canvas) {
		geom->x = canvas->root_x;
		geom->y = canvas->root_y;
		geom->width = canvas->width + canvas->bw * 2;
		geom->height += canvas->height + canvas->bw * 2;
		return True;
	}
	return False;
}

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




/* this gets called when Root background changes : */
void update_window_transparency (ASWindow * asw, Bool force)
{
	ASOrientation *od = get_orientation_data (asw);
	int i;
	ASCanvas *changed_canvases[6] = { NULL, NULL, NULL, NULL, NULL, NULL };

	if (!ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		ASCanvas *fc;
		for (i = 0; i < FRAME_PARTS; ++i)
			if (asw->frame_bars[i]) {
				fc = asw->frame_sides[od->tbar2canvas_xref[i]];
				if (!check_canvas_offscreen (fc)) {
					update_astbar_transparency (asw->frame_bars[i], fc, force);
					if (DoesBarNeedsRendering (asw->frame_bars[i])) {
						changed_canvases[od->tbar2canvas_xref[i]] = fc;
						render_astbar (asw->frame_bars[i], fc);
					}
				}
			}

		if (asw->tbar) {
			fc = asw->frame_sides[od->tbar_side];
			if (!check_canvas_offscreen (fc)) {
				update_astbar_transparency (asw->tbar, fc, force);
				if (DoesBarNeedsRendering (asw->tbar)) {
					changed_canvases[od->tbar_side] = fc;
					render_astbar (asw->tbar, fc);
				}
			}
		}
	} else {
		if (asw->icon_button) {
			update_astbar_transparency (asw->icon_button, asw->icon_canvas,
																	force);
			if (DoesBarNeedsRendering (asw->icon_button)) {
				changed_canvases[4] = asw->icon_canvas;
				render_astbar (asw->icon_button, asw->icon_canvas);
			}
		}
		if (asw->icon_title) {
			update_astbar_transparency (asw->icon_title,
																	asw->icon_title_canvas ? asw->
																	icon_title_canvas : asw->icon_canvas,
																	force);
			if (DoesBarNeedsRendering (asw->icon_title)) {
				if (asw->icon_title_canvas != NULL
						&& asw->icon_title_canvas != asw->icon_canvas)
					changed_canvases[5] = asw->icon_title_canvas;
				else
					changed_canvases[4] = asw->icon_canvas;
				render_astbar (asw->icon_title,
											 asw->icon_title_canvas ? asw->
											 icon_title_canvas : asw->icon_canvas);
			}
		}
	}
	for (i = 0; i < 6; ++i)
		if (changed_canvases[i]) {
			invalidate_canvas_save (changed_canvases[i]);
			update_canvas_display (changed_canvases[i]);
		}

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

static void
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

static int make_shade_animation_step (ASWindow * asw, ASOrientation * od)
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

static ASFlagType
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



Bool apply_window_status_size (register ASWindow * asw, ASOrientation * od)
{
	Bool moved = False;
	Bool resized = False;
	ASFlagType client_changes = 0;
	/* note that icons are handled by iconbox */
	if (!ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		int step_size = make_shade_animation_step (asw, od);
		int new_width = asw->status->width;
		int new_height = asw->status->height;
		LOCAL_DEBUG_OUT
				("**CONFG Client(%lx(%s))->status(%ux%u%+d%+d,%s,%s(%d>-%d))",
				 asw->w, ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname",
				 asw->status->width, asw->status->height, asw->status->x,
				 asw->status->y, ASWIN_HFLAGS (asw,
																			 AS_VerticalTitle) ? "Vert" : "Horz",
				 step_size > 0 ? "Shaded" : "Unshaded", asw->shading_steps,
				 step_size);

		if (step_size > 0) {
			if (ASWIN_HFLAGS (asw, AS_VerticalTitle)) {
				new_width = step_size;
			} else {
				new_height = step_size;
			}
		}
		resized = (asw->frame_canvas->width != new_width ||
							 asw->frame_canvas->height != new_height);
		moved = (asw->frame_canvas->root_x != asw->status->x ||
						 asw->frame_canvas->root_y != asw->status->y);

		moveresize_canvas (asw->frame_canvas, asw->status->x, asw->status->y,
											 new_width, new_height);
		/* when we resize the client - our frame should already be positioned correctly ! */
		if (step_size <= 0)
			client_changes =
					resize_frame_subwindows (asw, od, new_width, new_height);
#if 0
		fprintf (stderr, "client_changes = %X, moved = %d. Called from :\n",
						 client_changes, moved);
		print_simple_backtrace ();
#endif
		if (moved && client_changes == 0)
			send_canvas_configure_notify (asw->frame_canvas, asw->client_canvas);
	}
	return moved || resized || client_changes != 0;
}

static void
on_frame_bars_moved (ASWindow * asw, unsigned int side, ASOrientation * od)
{
	ASCanvas *canvas = asw->frame_sides[side];

	update_astbar_transparency (asw->frame_bars[side], canvas, False);
	if (side == od->tbar_side) {
		update_astbar_transparency (asw->tbar, canvas, False);
		update_astbar_transparency (asw->
																frame_bars[od->tbar_mirror_corners[0]],
																canvas, False);
		update_astbar_transparency (asw->
																frame_bars[od->tbar_mirror_corners[1]],
																canvas, False);
	} else if (side == od->sbar_side) {
		update_astbar_transparency (asw->
																frame_bars[od->sbar_mirror_corners[0]],
																canvas, False);
		update_astbar_transparency (asw->
																frame_bars[od->sbar_mirror_corners[1]],
																canvas, False);
	}
}

void update_window_frame_moved (ASWindow * asw, ASOrientation * od)
{
	int i;

	if (ASWIN_GET_FLAGS (asw, AS_Dead | AS_MoveresizeInProgress))
		return;

	handle_canvas_config (asw->client_canvas);

	if (!check_window_offscreen (asw))
		if (asw->internal && asw->internal->on_moveresize)
			asw->internal->on_moveresize (asw->internal, None);

	if (!check_frame_offscreen (asw))
		for (i = 0; i < FRAME_SIDES; ++i)
			if (asw->frame_sides[i]) {
				handle_canvas_config (asw->frame_sides[i]);
				if (!check_frame_side_offscreen (asw, i)) {	/* canvas has been resized - resize tbars!!! */
					on_frame_bars_moved (asw, i, od);
				}
			}
}

void update_window_frame_pos (ASWindow * asw)
{
	if (ASWIN_GET_FLAGS (asw, AS_Dead | AS_MoveresizeInProgress))
		return;

	handle_canvas_config (asw->client_canvas);

/*
	if (!check_window_offscreen( asw ))
		if( asw->internal && asw->internal->on_moveresize )
			asw->internal->on_moveresize( asw->internal, None );
*/

	if (!check_frame_offscreen (asw)) {
		int i;
		ASFlagType changes = 0;
		for (i = 0; i < FRAME_SIDES; ++i)
			if (asw->frame_sides[i])
				changes |= handle_canvas_config (asw->frame_sides[i]);

		if (changes != 0)
			update_window_transparency (asw, False);
	}
}


void SendConfigureNotify (ASWindow * asw)
{
	XEvent client_event;

	if (ASWIN_GET_FLAGS (asw, AS_Dead | AS_MoveresizeInProgress))
		return;

	client_event.type = ConfigureNotify;
	client_event.xconfigure.display = dpy;
	client_event.xconfigure.event = asw->w;
	client_event.xconfigure.window = asw->w;

	client_event.xconfigure.x = asw->client_canvas->root_x;
	client_event.xconfigure.y = asw->client_canvas->root_y;
	client_event.xconfigure.width = asw->client_canvas->width;
	client_event.xconfigure.height = asw->client_canvas->height;

	client_event.xconfigure.border_width = asw->status->border_width;
	/* Real ConfigureNotify events say we're above title window, so ... */
	/* what if we don't have a title ????? */
	client_event.xconfigure.above =
			asw->frame_sides[FR_N] ? asw->frame_sides[FR_N]->w : asw->frame;
	client_event.xconfigure.override_redirect = False;
	XSendEvent (dpy, asw->w, False, StructureNotifyMask, &client_event);
}

static Bool
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

static void
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


/* this gets called when StructureNotify/SubstractureNotify arrives : */
void on_window_moveresize (ASWindow * asw, Window w)
{
	int i;
	ASOrientation *od;
	unsigned int normal_width, normal_height;
	Bool update_shape = False;

	LOCAL_DEBUG_CALLER_OUT ("(%p,%lx,asw->w=%lx)", asw, w, asw->w);
	if (AS_ASSERT (asw))
		return;

	od = get_orientation_data (asw);

	if (w == asw->w) {						/* simply update client's size and position */
		ASFlagType changes;
		if (ASWIN_GET_FLAGS (asw, AS_Dead))
			return;
		changes = handle_canvas_config (asw->client_canvas);
		if (asw->internal && asw->internal->on_moveresize)
			asw->internal->on_moveresize (asw->internal, w);

		update_shape = (changes != 0);
		if (XPending (dpy) > 0) {
			XEvent tmp;
			XNextEvent (dpy, &tmp);
			if (tmp.type == ConfigureNotify
					&& tmp.xconfigure.window == asw->frame)
				w = asw->frame;
			else
				XPutBackEvent (dpy, &tmp);
		}

	}

	if (w == asw->frame) {				/* resize canvases here : */
		int changes = handle_canvas_config (asw->frame_canvas);
		LOCAL_DEBUG_OUT ("changes=0x%X", changes);
		/* we must resize using current window size instead of event's size */
		*(od->in_width) = asw->frame_canvas->width;
		*(od->in_height) = asw->frame_canvas->height;
		normal_width = *(od->out_width);
		normal_height = *(od->out_height);

		if (get_flags (changes, CANVAS_RESIZED)) {
			register unsigned int *frame_size = &(asw->status->frame_size[0]);
			int step_size = make_shade_animation_step (asw, od);
			LOCAL_DEBUG_OUT ("step_size = %d", step_size);
			if (step_size <= 0) {			/* don't moveresize client window while shading !!!! */
				resize_frame_subwindows (asw, od, asw->frame_canvas->width,
																 asw->frame_canvas->height);
			} else {
				if (normal_height != step_size) {
					*(od->in_width) = normal_width;
					*(od->in_height) = step_size;

					if (normal_height < step_size) {
						/* we get smoother animation if we move decoration ahead of actually
						 * resizing frame window : */
						move_shading_frame (asw, od, step_size);
						ASSync (False);
						sleep_a_millisec (10);
						/*LOCAL_DEBUG_OUT( "**SHADE Client(%lx(%s))->(%d>-%d)", asw->w, ASWIN_NAME(asw)?ASWIN_NAME(asw):"noname", asw->shading_steps, step_size ); */
						resize_canvas (asw->frame_canvas, *(od->out_width),
													 *(od->out_height));
						ASSync (False);
					} else {
						sleep_a_millisec (10);
						resize_canvas (asw->frame_canvas, *(od->out_width),
													 *(od->out_height));
						ASSync (False);
						move_shading_frame (asw, od, step_size);
						ASSync (False);
					}
				} else {								/* probably just the change of the look - titlebar may need to be resized */
					resize_canvases (asw, od, normal_width, normal_height,
													 frame_size);
					move_canvas (asw->client_canvas, frame_size[FR_W],
											 frame_size[FR_N]);
				}

			}
			if (asw->shading_steps == 0) {
				for (i = 0; i < FRAME_SIDES; ++i)
					if (asw->frame_sides[i])
						check_frame_side_config (asw, asw->frame_sides[i]->w, od);
			}
			update_shape = True;
		} else if (get_flags (changes, CANVAS_MOVED)) {
			LOCAL_DEBUG_OUT ("window is moved but not resized %s", "");
			update_window_frame_moved (asw, od);
			/* also sent synthetic ConfigureNotify : */
			SendConfigureNotify (asw);
		}

		if (changes != 0) {
			LOCAL_DEBUG_OUT ("resized = %X, shaped = %lX",
											 get_flags (changes, CANVAS_RESIZED),
											 ASWIN_GET_FLAGS (asw, AS_ShapedDecor | AS_Shaped));

			if (!check_frame_offscreen (asw))
				update_window_transparency (asw, False);

			if (!ASWIN_GET_FLAGS (asw, AS_Dead | AS_MoveresizeInProgress))
				broadcast_config (M_CONFIGURE_WINDOW, asw);
		}
	} else if (asw->icon_canvas && w == asw->icon_canvas->w) {
		ASFlagType changes = handle_canvas_config (asw->icon_canvas);
		LOCAL_DEBUG_OUT ("icon resized to %dx%d%+d%+d",
										 asw->icon_canvas->width, asw->icon_canvas->height,
										 asw->icon_canvas->root_x, asw->icon_canvas->root_y);
		if (get_flags (changes, CANVAS_RESIZED)) {
			unsigned short title_size = 0;
			if (asw->icon_title
					&& (asw->icon_title_canvas == asw->icon_canvas
							|| asw->icon_title_canvas == NULL)) {
				title_size = calculate_astbar_height (asw->icon_title);
				move_astbar (asw->icon_title, asw->icon_canvas, 0,
										 asw->icon_canvas->height - title_size);
				set_astbar_size (asw->icon_title, asw->icon_canvas->width,
												 title_size);
				if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
					render_astbar (asw->icon_title, asw->icon_canvas);
				}
				LOCAL_DEBUG_OUT ("title_size = %d", title_size);
			}
			set_astbar_size (asw->icon_button, asw->icon_canvas->width,
											 asw->icon_canvas->height - title_size);
			if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
				render_astbar (asw->icon_button, asw->icon_canvas);
				update_canvas_display (asw->icon_canvas);
			}
		}
		if (ASWIN_GET_FLAGS (asw, AS_Iconic))
			broadcast_config (M_CONFIGURE_WINDOW, asw);
	} else if (asw->icon_title_canvas && w == asw->icon_title_canvas->w) {
		if (handle_canvas_config (asw->icon_title_canvas) && asw->icon_title) {
			LOCAL_DEBUG_OUT ("icon_title resized to %dx%d%+d%+d",
											 asw->icon_title_canvas->width,
											 asw->icon_title_canvas->height,
											 asw->icon_title_canvas->root_x,
											 asw->icon_title_canvas->root_y);
			set_astbar_size (asw->icon_title, asw->icon_title_canvas->width,
											 asw->icon_title->height);
			if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
				render_astbar (asw->icon_title, asw->icon_title_canvas);
				update_canvas_display (asw->icon_title_canvas);
			}
		}
		if (ASWIN_GET_FLAGS (asw, AS_Iconic))
			broadcast_config (M_CONFIGURE_WINDOW, asw);
	} else if (asw->shading_steps == 0) {	/* one of the frame canvases : */
		if (!check_frame_side_config (asw, w, od))
			if (asw->internal && asw->internal->on_moveresize)
				asw->internal->on_moveresize (asw->internal, w);
	}

	if (update_shape) {
		if (ASWIN_GET_FLAGS (asw, AS_ShapedDecor | AS_Shaped))
			SetShape (asw, 0);
		else if (get_flags (asw->internal_flags, ASWF_PendingShapeRemoval))
			ClearShape (asw);
	}

	ASSync (False);
}

int update_window_tbar_size (ASWindow * asw);


void on_window_title_changed (ASWindow * asw, Bool update_display)
{
	if (AS_ASSERT (asw))
		return;
	if (is_output_level_under_threshold (OUTPUT_LEVEL_HINTS))
		print_clean_hints (NULL, NULL, asw->hints);
	if (asw->tbar) {
		ASCanvas *canvas =
				ASWIN_HFLAGS (asw,
											AS_VerticalTitle) ? asw->frame_sides[FR_W] : asw->
				frame_sides[FR_N];
		if (change_astbar_first_label
				(asw->tbar, ASWIN_NAME (asw), ASWIN_NAME_ENCODING (asw)))
			if (canvas && update_display) {
				invalidate_canvas_save (canvas);

				if (canvas->shape) {
					XRectangle rect;
					rect.x = asw->tbar->win_x;
					rect.y = asw->tbar->win_y;
					rect.width = asw->tbar->width;
					rect.height = asw->tbar->height;
					subtract_shape_rectangle (canvas->shape, &rect, 1, 0, 0,
																		canvas->width, canvas->height);
				}
				update_window_tbar_size (asw);
				render_astbar (asw->tbar, canvas);
				update_canvas_display (canvas);
				//if( ASWIN_GET_FLAGS( asw, AS_ShapedDecor ) )
				SetShape (asw, 0);
			}
	}
	LOCAL_DEBUG_OUT ("icon_title = %p, icon_name = \"%s\"", asw->icon_title,
									 ASWIN_ICON_NAME (asw) ? ASWIN_ICON_NAME (asw) :
									 "(null)");
	if (asw->icon_title) {
		if (change_astbar_first_label
				(asw->icon_title, ASWIN_ICON_NAME (asw),
				 ASWIN_ICON_NAME_ENC (asw)))
			if (ASWIN_GET_FLAGS (asw, AS_Iconic))
				on_icon_changed (asw);
	}
}

Bool collect_aswindow_hints (ASWindow * asw, ASRawHints * raw_hints);

void on_window_hints_changed (ASWindow * asw)
{
	static ASRawHints raw_hints;
	ASHints *hints = NULL, *old_hints = NULL;
	Bool tie_changed = False;
	ASStatusHints scratch_status;	/* all we need from it is AS_Urgent state change */
	Bool status_changed = False;


	if (AS_ASSERT (asw))
		return;
	if (ASWIN_GET_FLAGS (asw, AS_Dead))
		return;
	if (!collect_aswindow_hints (asw, &raw_hints))
		return;

	memset (&scratch_status, 0x00, sizeof (scratch_status));

	hints =
			merge_hints (&raw_hints, Database, &scratch_status,
									 Scr.Look.supported_hints, HINT_ANY, NULL, asw->w);

	destroy_raw_hints (&raw_hints, True);
	if (hints) {
		show_debug (__FILE__, __FUNCTION__, __LINE__,
								"Window management hints collected and merged for window %X",
								asw->w);
		if (is_output_level_under_threshold (OUTPUT_LEVEL_HINTS))
			print_clean_hints (NULL, NULL, hints);
	} else {
		show_warning ("Failed to merge window management hints for window %X",
									asw->w);
		return;
	}

	old_hints = asw->hints;
	tie_changed = ((old_hints->transient_for != hints->transient_for) ||
								 (old_hints->group_lead != hints->group_lead));

	if (tie_changed)
		untie_aswindow (asw);

	asw->hints = hints;

	if (tie_changed)
		tie_aswindow (asw);

	SelectDecor (asw);

	LOCAL_DEBUG_OUT
			("redecorating window %p(\"%s\") to update to new hints...", asw,
			 hints->names[0] ? hints->names[0] : "none");
	/* we do not want to do a complete refresh of decorations -
	 * we want to do only what is neccessary: */

	if (get_flags (scratch_status.flags, AS_Urgent)) {
		if (!get_flags (asw->status->flags, AS_Urgent)) {
			set_flags (asw->status->flags, AS_Urgent);
			status_changed = True;
		}
	} else if (get_flags (asw->status->flags, AS_Urgent)) {
		ASFlagType extwm_state = 0;

		if (!get_extwm_state_flags (asw->w, &extwm_state))
			extwm_state = 0;					/* just in case */
		if (!get_flags (extwm_state, EXTWM_StateDemandsAttention)) {
			clear_flags (asw->status->flags, AS_Urgent);
			status_changed = True;
		}
	}
	if (hints2decorations (asw, old_hints))
		status_changed = True;

	if (status_changed)
		on_window_status_changed (asw, True);

	if (mystrcmp (old_hints->res_name, hints->res_name) != 0 ||
			mystrcmp (old_hints->res_class, hints->res_class) != 0)
		broadcast_res_names (asw);
	if (mystrcmp (old_hints->names[0], hints->names[0]) != 0) {
		broadcast_window_name (asw);
		set_flags (asw->internal_flags, ASWF_NameChanged);
	}
	if (mystrcmp (old_hints->icon_name, hints->icon_name) != 0)
		broadcast_icon_name (asw);

	destroy_hints (old_hints, False);
}

void on_window_opacity_changed (ASWindow * asw)
{
	CARD32 new_opacity = NET_WM_WINDOW_OPACITY_OPAQUE;
	Bool set = False;
	ASDatabaseRecord db_rec;

	if (AS_ASSERT (asw))
		return;
	if (ASWIN_GET_FLAGS (asw, AS_Dead))
		return;
	set =
			read_32bit_property (asw->w, _XA_NET_WM_WINDOW_OPACITY,
													 &new_opacity);
	if (set && new_opacity == asw->hints->window_opacity
			&& ASWIN_HFLAGS (asw, AS_WindowOpacity))
		return;

	if (fill_asdb_record (Database, asw->hints->names, &db_rec, False)) {
		if (get_flags (db_rec.set_data_flags, STYLE_WINDOW_OPACITY)) {
			new_opacity =
					set_hints_window_opacity_percent (NULL, db_rec.window_opacity);
			set = True;
		}
	}

	if (!set)
		XDeleteProperty (dpy, asw->frame, _XA_NET_WM_WINDOW_OPACITY);
	else if (new_opacity != asw->hints->window_opacity
					 || !ASWIN_HFLAGS (asw, AS_WindowOpacity)) {
		set_flags (asw->hints->flags, AS_WindowOpacity);
		asw->hints->window_opacity = new_opacity;
		set_32bit_property (asw->frame, _XA_NET_WM_WINDOW_OPACITY, XA_CARDINAL,
												asw->hints->window_opacity);
	}
}

int update_window_tbar_size (ASWindow * asw)
{
	int tbar_size = 0;
	if (asw->tbar) {
		unsigned int tbar_width = 0;
		unsigned int tbar_height = 0;
		int x_offset = 0, y_offset = 0;
		LOCAL_DEBUG_OUT ("IsVertical = %lX",
										 ASWIN_HFLAGS (asw, AS_VerticalTitle));

		if (ASWIN_HFLAGS (asw, AS_VerticalTitle)) {
			tbar_size = calculate_astbar_width (asw->tbar);
			tbar_width = tbar_size;
			tbar_height = asw->frame_canvas->height;
#ifdef SHAPE
			if (get_flags
					(asw->frame_data->condense_titlebar, ALIGN_LEFT | ALIGN_RIGHT)) {
				int condensed = calculate_astbar_height (asw->tbar);
				if (condensed < tbar_height) {
					if (get_flags (asw->frame_data->condense_titlebar, ALIGN_LEFT)) {
						y_offset = tbar_height - condensed;
						if (get_flags
								(asw->frame_data->condense_titlebar, ALIGN_RIGHT))
							y_offset /= 2;
					}
					tbar_height = condensed;
				}
			}
#endif
		} else {
			tbar_size = calculate_astbar_height (asw->tbar);
			tbar_width = asw->frame_canvas->width;
			tbar_height = tbar_size;
#ifdef SHAPE
			if (get_flags
					(asw->frame_data->condense_titlebar, ALIGN_LEFT | ALIGN_RIGHT)) {
				int condensed = calculate_astbar_width (asw->tbar);
				if (condensed < tbar_width) {
					if (get_flags (asw->frame_data->condense_titlebar, ALIGN_RIGHT)) {
						x_offset = tbar_width - condensed;
						if (get_flags (asw->frame_data->condense_titlebar, ALIGN_LEFT))
							x_offset /= 2;
					}
					tbar_width = condensed;
				}
			}
#endif
		}
		/* we need that to set up tbar size : */
		set_astbar_size (asw->tbar, tbar_width, tbar_height);
		/* does not matter if we use frame canvas, since part's
		 * canvas resizes at frame canvas origin anyway */
		move_astbar (asw->tbar, asw->frame_canvas, x_offset, y_offset);

	}
	return tbar_size;
}


void on_window_status_changed (ASWindow * asw, Bool reconfigured)
{
	char *unfocus_mystyle = NULL;
	char *frame_unfocus_mystyle = NULL;
	int i;
	Bool changed = False;
	ASOrientation *od = get_orientation_data (asw);

	if (AS_ASSERT (asw))
		return;

/*	get_extwm_state_flags (asw->w, &i); */

	LOCAL_DEBUG_CALLER_OUT ("(%p,%s Reconfigured)", asw,
													reconfigured ? "" : "Not");
	LOCAL_DEBUG_OUT ("status geometry = %dx%d%+d%+d", asw->status->width,
									 asw->status->height, asw->status->x, asw->status->y);
	if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		unfocus_mystyle = ASWIN_GET_FLAGS (asw, AS_Sticky) ?
				AS_ICON_TITLE_STICKY_MYSTYLE : AS_ICON_TITLE_UNFOCUS_MYSTYLE;
		if (asw->icon_title)
			changed =
					set_astbar_style (asw->icon_title, BAR_STATE_UNFOCUSED,
														unfocus_mystyle);
		if (changed || reconfigured)	/* now we need to update icon title size */
			on_icon_changed (asw);
	} else {
		Bool decor_shaped = False;
		MyFrame *frame_data = asw->frame_data;
		int back_type;
		ASFlagType *frame_bevel, title_bevel;
		int title_cm, title_hue = -1, title_sat = -1;

		if (ASWIN_GET_FLAGS (asw, AS_Sticky)) {
			back_type = BACK_STICKY;
			frame_bevel = &(frame_data->part_sbevel[0]);
			title_bevel = frame_data->title_sbevel;
			title_cm = frame_data->title_scm;
			if (get_flags (frame_data->set_title_attr, MYFRAME_TitleSHueSet))
				title_hue = frame_data->title_shue;
			if (get_flags (frame_data->set_title_attr, MYFRAME_TitleSSatSet))
				title_sat = frame_data->title_ssat;
		} else {
			back_type = BACK_UNFOCUSED;
			frame_bevel = &(frame_data->part_ubevel[0]);
			title_bevel = frame_data->title_ubevel;
			title_cm = frame_data->title_ucm;
			if (get_flags (frame_data->set_title_attr, MYFRAME_TitleUHueSet))
				title_hue = frame_data->title_uhue;
			if (get_flags (frame_data->set_title_attr, MYFRAME_TitleUSatSet))
				title_sat = frame_data->title_usat;
		}

		unfocus_mystyle = asw->hints->mystyle_names[back_type];
		if (frame_data->title_style_names[back_type])
			unfocus_mystyle = frame_data->title_style_names[back_type];
		if (unfocus_mystyle == NULL)
			unfocus_mystyle = Scr.Look.MSWindow[back_type]->name;
		if (unfocus_mystyle == NULL)
			unfocus_mystyle = Scr.Look.MSWindow[BACK_UNFOCUSED]->name;

		frame_unfocus_mystyle =
				(frame_data->frame_style_names[back_type] ==
				 NULL) ? unfocus_mystyle : frame_data->
				frame_style_names[back_type];

		for (i = 0; i < FRAME_PARTS; ++i) {
			unsigned int real_part = od->frame_rotation[i];
			if (asw->frame_bars[real_part]) {
				if (set_astbar_style
						(asw->frame_bars[real_part], BAR_STATE_UNFOCUSED,
						 frame_unfocus_mystyle))
					changed = True;
				if (set_astbar_hilite
						(asw->frame_bars[real_part], BAR_STATE_UNFOCUSED,
						 frame_bevel[i]))
					changed = True;
				if (is_astbar_shaped (asw->frame_bars[real_part], -1))
					decor_shaped = True;
			}
		}
		if (asw->tbar) {
			if (set_astbar_style
					(asw->tbar, BAR_STATE_UNFOCUSED, unfocus_mystyle))
				changed = True;
			if (set_astbar_hilite (asw->tbar, BAR_STATE_UNFOCUSED, title_bevel))
				changed = True;
			if (set_astbar_composition_method
					(asw->tbar, BAR_STATE_UNFOCUSED, title_cm))
				changed = True;
			if (set_astbar_huesat
					(asw->tbar, BAR_STATE_UNFOCUSED, title_hue, title_sat))
				changed = True;
			if (get_flags
					(asw->frame_data->condense_titlebar, ALIGN_LEFT | ALIGN_RIGHT)
					|| is_astbar_shaped (asw->tbar, -1)) {
				decor_shaped = True;
			}
		}
		if (decor_shaped)
			ASWIN_SET_FLAGS (asw, AS_ShapedDecor);
		else
			ASWIN_CLEAR_FLAGS (asw, AS_ShapedDecor);

		LOCAL_DEBUG_OUT ("status geometry = %dx%d%+d%+d", asw->status->width,
										 asw->status->height, asw->status->x, asw->status->y);
		if (changed || reconfigured) {	/* now we need to update frame sizes in status */
			unsigned int *frame_size = &(asw->status->frame_size[0]);
			unsigned short tbar_size = 0;
/*			int bw = 0 ;
			if( asw->hints && get_flags(asw->hints->flags, AS_Border))
				bw = asw->hints->border_width ; */
			tbar_size = update_window_tbar_size (asw);
			for (i = 0; i < FRAME_SIDES; ++i) {
				if (asw->frame_bars[i])
					frame_size[i] = IsSideVertical (i) ? asw->frame_bars[i]->width :
							asw->frame_bars[i]->height;
				else
					frame_size[i] = 0;
				//frame_size[i] += bw ;
			}
			if (tbar_size > 0) {
				for (i = FRAME_SIDES; i < FRAME_PARTS; ++i) {
					if (get_flags
							(asw->internal_flags,
							 ASWF_FirstCornerFollowsTbarSize << (i - FRAME_SIDES))) {
						unsigned int real_part = od->frame_rotation[i];
						if (asw->frame_bars[real_part]) {
							if (ASWIN_HFLAGS (asw, AS_VerticalTitle))
								set_astbar_size (asw->frame_bars[real_part],
																 asw->frame_bars[real_part]->width,
																 tbar_size);
							else
								set_astbar_size (asw->frame_bars[real_part], tbar_size,
																 asw->frame_bars[real_part]->height);
						}
					}
				}
			}

			frame_size[od->tbar_side] += tbar_size;
			LOCAL_DEBUG_OUT
					("status geometry = %dx%d%+d%+d frame_size = %d,%d,%d,%d",
					 asw->status->width, asw->status->height, asw->status->x,
					 asw->status->y, frame_size[0], frame_size[1], frame_size[2],
					 frame_size[3]);
			anchor2status (asw->status, asw->hints, &(asw->anchor));
			LOCAL_DEBUG_OUT ("status geometry = %dx%d%+d%+d", asw->status->width,
											 asw->status->height, asw->status->x,
											 asw->status->y);
		}
	}

	/* now we need to move/resize our frame window */
	if (!apply_window_status_size (asw, od))
		changed = True;
	if (changed)
		broadcast_config (M_STATUS_CHANGE, asw);	/* must enforce status change propagation */
	if (!ASWIN_GET_FLAGS (asw, AS_Dead))
		set_client_state (asw->w, asw->status);
}

void on_window_anchor_changed (ASWindow * asw)
{
	if (asw) {
		ASOrientation *od = get_orientation_data (asw);
		anchor2status (asw->status, asw->hints, &(asw->anchor));
		LOCAL_DEBUG_OUT ("status geometry = %dx%d%+d%+d", asw->status->width,
										 asw->status->height, asw->status->x, asw->status->y);

		/* now we need to move/resize our frame window */
		if (!apply_window_status_size (asw, od))
			broadcast_config (M_CONFIGURE_WINDOW, asw);	/* must enforce status change propagation */
		if (!ASWIN_GET_FLAGS (asw, AS_Dead))
			set_client_state (asw->w, asw->status);
	}
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

void on_window_hilite_changed (ASWindow * asw, Bool focused)
{
	ASOrientation *od = get_orientation_data (asw);
	int i;

	LOCAL_DEBUG_CALLER_OUT ("(%p,%s focused)", asw, focused ? "" : "not");
	if (AS_ASSERT (asw))
		return;

	for (i = FRAME_SIDES; --i >= 0;) {
		ASCanvas *update_canvas = asw->frame_sides[i];
		int k;
		if (swap_save_canvas (update_canvas)) {
			LOCAL_DEBUG_OUT ("canvas save swapped for side %d", i);
			update_canvas = NULL;
		}
		if (i == od->tbar_side)
			set_astbar_focused (asw->tbar, update_canvas, focused);

		for (k = 0; k < FRAME_PARTS; ++k)
			if (od->tbar2canvas_xref[k] == i)
				set_astbar_focused (asw->frame_bars[k], update_canvas, focused);
	}
	/* now posting all the changes on display : */
	for (i = FRAME_SIDES; --i >= 0;)
		if (is_canvas_dirty (asw->frame_sides[i]))
			update_canvas_display (asw->frame_sides[i]);
	if (asw->internal && asw->internal->on_hilite_changed)
		asw->internal->on_hilite_changed (asw->internal, NULL, focused);
	if (ASWIN_GET_FLAGS (asw, AS_ShapedDecor))
		SetShape (asw, 0);
	if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		set_astbar_focused (asw->icon_button, asw->icon_canvas, focused);
		set_astbar_focused (asw->icon_title, asw->icon_title_canvas, focused);
		if (is_canvas_dirty (asw->icon_canvas))
			update_canvas_display (asw->icon_canvas);
		if (is_canvas_dirty (asw->icon_title_canvas))
			update_canvas_display (asw->icon_title_canvas);
	}
}

void on_window_pressure_changed (ASWindow * asw, int pressed_context)
{
	ASOrientation *od = get_orientation_data (asw);
	LOCAL_DEBUG_CALLER_OUT ("(%p,%s)", asw, context2text (pressed_context));

	if (AS_ASSERT (asw) || asw->status == NULL)
		return;

	if (!ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		register int i = FRAME_PARTS;
		/* Titlebar */
		set_astbar_btn_pressed (asw->tbar, pressed_context);	/* must go before next call to properly redraw :  */
		set_astbar_pressed (asw->tbar, asw->frame_sides[od->tbar_side],
												pressed_context & C_TITLE);
		/* frame decor : */
		for (i = FRAME_PARTS; --i >= 0;)
			set_astbar_pressed (asw->frame_bars[i],
													asw->frame_sides[od->tbar2canvas_xref[i]],
													pressed_context & (C_FrameN << i));
		/* now posting all the changes on display : */
		for (i = FRAME_SIDES; --i >= 0;)
			if (is_canvas_dirty (asw->frame_sides[i])) {
				update_canvas_display (asw->frame_sides[i]);
			}
		if (asw->internal && asw->internal->on_pressure_changed)
			asw->internal->on_pressure_changed (asw->internal,
																					pressed_context & C_CLIENT);
	} else {											/* Iconic !!! */

		set_astbar_pressed (asw->icon_button, asw->icon_canvas,
												pressed_context & C_IconButton);
		set_astbar_pressed (asw->icon_title, asw->icon_title_canvas,
												pressed_context & C_IconTitle);
		if (is_canvas_dirty (asw->icon_canvas))
			update_canvas_display (asw->icon_canvas);
		if (is_canvas_dirty (asw->icon_title_canvas))
			update_canvas_display (asw->icon_title_canvas);
	}
}

/********************************************************************/
/* end of ASWindow frame decorations management                     */

