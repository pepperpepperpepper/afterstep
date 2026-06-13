/*
 * Copyright (C) 2002 Sasha Vasko <sasha at aftercode.net>
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

#undef LOCAL_DEBUG
#undef DO_CLOCKING

#include <string.h>

#include "../configure.h"

#include "asapp.h"
#include "../libAfterImage/afterimage.h"
#include "screen.h"
#include "shape.h"
#include "canvas.h"
#include "canvas_internal.h"


inline Bool
get_current_canvas_geometry (ASCanvas * pc, int *px, int *py,
														 unsigned int *pwidth, unsigned int *pheight,
														 unsigned int *pbw)
{
	Window wdumm;
	unsigned int udumm;
	int dumm;

	if (pc == NULL)
		return False;

	if (px == NULL)
		px = &dumm;
	if (py == NULL)
		py = &dumm;
	if (pwidth == NULL)
		pwidth = &udumm;
	if (pheight == NULL)
		pheight = &udumm;
	if (pbw == NULL)
		pbw = &udumm;

	return (XGetGeometry
					(dpy, pc->w, &wdumm, px, py, pwidth, pheight, pbw, &udumm) != 0);
}

void set_canvas_shape_to_rectangle (ASCanvas * pc)
{
#ifdef SHAPE
	unsigned int width, height, bw;
	XRectangle rect;

	rect.x = 0;
	rect.y = 0;
#ifdef STRICT_GEOMETRY
	get_current_canvas_geometry (pc, NULL, NULL, &width, &height, &bw);
#else
	width = pc->width;
	height = pc->height;
	bw = pc->bw;
#endif
	rect.width = width + bw * 2;
	rect.height = height + bw * 2;
	LOCAL_DEBUG_OUT ("XShapeCombineRectangles(%lX) (%dx%d%+d%+d)", pc->w,
									 rect.width, rect.height, -bw, -bw);
	XShapeCombineRectangles (dpy, pc->w, ShapeBounding, -bw, -bw, &rect, 1,
													 ShapeSet, Unsorted);
	set_flags (pc->state, CANVAS_SHAPE_SET);
#endif
}

void set_canvas_shape_to_nothing (ASCanvas * pc)
{
#ifdef SHAPE
	LOCAL_DEBUG_OUT ("XShapeCombineMask(%lX) (clearing mask)", pc->w);
	XShapeCombineMask (dpy, pc->w, ShapeBounding, 0, 0, None, ShapeSet);
	clear_flags (pc->state, CANVAS_SHAPE_SET);
#endif
	if (pc->shape)
		destroy_shape (&pc->shape);
}




/********************************************************************/
/* ASCanvas :                                                       */
/********************************************************************/
static ASFlagType refresh_canvas_config (ASCanvas * pc)
{
	ASFlagType changed = 0;

	if (pc && pc->w != None) {
		int root_x = pc->root_x, root_y = pc->root_y, dumm;
		unsigned int width = pc->width, height = pc->height, udumm;
		Window wdumm;
		unsigned int bw;

		XTranslateCoordinates (dpy, pc->w, ASDefaultRoot, 0, 0, &root_x,
													 &root_y, &wdumm);
		XGetGeometry (dpy, pc->w, &wdumm, &dumm, &dumm, &udumm, &udumm, &bw,
									&udumm);
		root_x -= bw;
		root_y -= bw;
		if (root_x != pc->root_x)
			set_flags (changed, CANVAS_X_CHANGED);
		if (root_y != pc->root_y)
			set_flags (changed, CANVAS_Y_CHANGED);
		pc->root_x = root_x;
		pc->root_y = root_y;
		pc->bw = bw;

		if (!get_drawable_size (pc->w, &width, &height))
			return 0;									/* drawable is bad */
		if (width != pc->width)
			set_flags (changed, CANVAS_WIDTH_CHANGED);
		if (height != pc->height)
			set_flags (changed, CANVAS_HEIGHT_CHANGED);

		clear_flags (pc->state, CANVAS_CONFIG_INVALID);

		if (width != pc->width || height != pc->height) {
			destroy_visual_pixmap (ASDefaultVisual, &(pc->saved_canvas));
			destroy_visual_pixmap (ASDefaultVisual, &(pc->canvas));

			if (pc->saved_shape)
				destroy_shape (&(pc->saved_shape));

			if (pc->shape) {
				destroy_shape (&(pc->shape));
			}
			set_flags (pc->state, CANVAS_DIRTY | CANVAS_OUT_OF_SYNC);
			pc->width = width;
			pc->height = height;
		}
	}
	return changed;
}

Pixmap get_canvas_canvas (ASCanvas * pc)
{
	if (pc == NULL)
		return None;
	if (get_flags (pc->state, CANVAS_CONTAINER))
		return None;
	LOCAL_DEBUG_CALLER_OUT ("ASCanvas(%p)->canvas(%lX)", pc, pc->canvas);
	if (pc->canvas == None) {
		pc->canvas =
				create_visual_pixmap (ASDefaultVisual, ASDefaultRoot, pc->width,
															pc->height, 0);
		set_flags (pc->state, CANVAS_DIRTY | CANVAS_OUT_OF_SYNC);
		XSetWindowBackgroundPixmap (dpy, pc->w, pc->canvas);
	}
	return pc->canvas;
}

ASCanvas *create_ascanvas (Window w)
{
	ASCanvas *pc = NULL;

	if (w) {
		pc = safecalloc (1, sizeof (ASCanvas));
		pc->w = w;
		refresh_canvas_config (pc);
		LOCAL_DEBUG_CALLER_OUT
				("<<#########>>canvas %p created for w: %lX; geom = %dx%d%+d%+d, bw = %d",
				 pc, pc->w, pc->width, pc->height, pc->root_x, pc->root_y, pc->bw);
	}
	return pc;
}

ASCanvas *create_ascanvas_container (Window w)
{
	ASCanvas *pc = NULL;

	if (w) {
		pc = safecalloc (1, sizeof (ASCanvas));
		pc->w = w;
		pc->state = CANVAS_CONTAINER;
		refresh_canvas_config (pc);
		LOCAL_DEBUG_CALLER_OUT
				("<<#########>>container canvas %p created for w: %lX; geom = %dx%d%+d%+d, bw = %d",
				 pc, pc->w, pc->width, pc->height, pc->root_x, pc->root_y, pc->bw);
	}
	return pc;
}

void destroy_ascanvas (ASCanvas ** pcanvas)
{
	if (pcanvas) {
		ASCanvas *pc = *pcanvas;

		LOCAL_DEBUG_CALLER_OUT
				("<<#########>>destroying canvas %p for window %lX", pc,
				 pc ? pc->w : None);
		if (pc) {
			destroy_visual_pixmap (ASDefaultVisual, &(pc->saved_canvas));
			destroy_visual_pixmap (ASDefaultVisual, &(pc->canvas));
			LOCAL_DEBUG_OUT ("saved_shape = %p, shape = %p", pc->saved_shape,
											 pc->shape);
			if (pc->saved_shape && pc->saved_shape != pc->shape)
				destroy_shape (&(pc->saved_shape));
			if (pc->shape)
				destroy_shape (&(pc->shape));
			memset (pc, 0x00, sizeof (ASCanvas));
			free (pc);
		}
		*pcanvas = NULL;
	}
}

ASFlagType handle_canvas_config (ASCanvas * canvas)
{
	ASFlagType res;

	LOCAL_DEBUG_CALLER_OUT
			("canvas(%p)->window(%lx)->orig_geom(%ux%u%+d%+d)", canvas,
			 canvas->w, canvas->width, canvas->height, canvas->root_x,
			 canvas->root_y);
	res = refresh_canvas_config (canvas);
	LOCAL_DEBUG_CALLER_OUT
			("canvas(%p)->window(%lx)->new__geom(%ux%u%+d%+d)->change(0x%lX)",
			 canvas, canvas->w, canvas->width, canvas->height, canvas->root_x,
			 canvas->root_y, res);
	return res;
}

void invalidate_canvas_config (ASCanvas * pc)
{
	if (pc) {
		if (get_flags (pc->state, CANVAS_CONFIG_INVALID))
			return;

		LOCAL_DEBUG_OUT ("resizing to %dx%d", pc->width + 1, pc->height + 1);
		XResizeWindow (dpy, pc->w, pc->width + 1, pc->height + 1);
#ifdef SHAPE
		if (!get_flags (pc->state, CANVAS_CONTAINER)
				&& get_flags (pc->state, CANVAS_SHAPE_SET))
			set_canvas_shape_to_nothing (pc);
#endif

		pc->width = 0;
		pc->height = 0;
		destroy_visual_pixmap (ASDefaultVisual, &(pc->canvas));
		destroy_visual_pixmap (ASDefaultVisual, &(pc->saved_canvas));
		if (pc->shape)
			destroy_shape (&(pc->shape));
		if (pc->saved_shape)
			destroy_shape (&(pc->saved_shape));
		set_flags (pc->state,
							 CANVAS_DIRTY | CANVAS_OUT_OF_SYNC |
							 CANVAS_MASK_OUT_OF_SYNC);
		set_flags (pc->state, CANVAS_CONFIG_INVALID);
	}
}

