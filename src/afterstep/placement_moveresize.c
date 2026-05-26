/*
 * Copyright (C) 2005 Fabian Yamaguchi
 * Copyright (C) 2004-2005 Sasha Vasko
 * Copyright (C) 1996 Frank Fejes
 * Copyright (C) 1995 Bo Yang
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

#define LOCAL_DEBUG
#include "../../configure.h"

#include "asinternals.h"
#include "../../libAfterStep/moveresize.h"

#include "placement_internal.h"


struct ASWindowGridAuxData {
	ASGrid *grid;
	long desk;
	int min_layer;
	Bool frame_only;
	int vx, vy;
	Bool ignore_avoid_cover;
	ASWindow *target;
};

/*************************************************************************
 * here we build the grid  to facilitate avoid cover and snap-to-grid
 * while moving resizing window
 *************************************************************************/
static Bool get_aswindow_grid_iter_func (void *data, void *aux_data)
{
	ASWindow *asw = (ASWindow *) data;
	struct ASWindowGridAuxData *grid_data =
			(struct ASWindowGridAuxData *)aux_data;

	if (asw
			&& (!IsValidDesk (grid_data->desk)
					|| ASWIN_DESK (asw) == grid_data->desk)) {
		int outer_gravity = Scr.Feel.EdgeAttractionWindow;
		int inner_gravity = Scr.Feel.EdgeAttractionWindow;
		if (ASWIN_HFLAGS (asw, AS_AvoidCover) && !grid_data->ignore_avoid_cover
				&& asw != grid_data->target)
			inner_gravity = -1;
		else if (inner_gravity == 0
						 || grid_data->min_layer > ASWIN_LAYER (asw))
			return True;

		if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
			add_canvas_grid (grid_data->grid, asw->icon_canvas, outer_gravity,
											 inner_gravity, get_flags (Scr.Feel.flags,
																								 StickyIcons));
			if (asw->icon_canvas != asw->icon_title_canvas)
				add_canvas_grid (grid_data->grid, asw->icon_title_canvas,
												 outer_gravity, inner_gravity,
												 get_flags (Scr.Feel.flags, StickyIcons));
		} else {
			add_canvas_grid (grid_data->grid, asw->frame_canvas, outer_gravity,
											 inner_gravity, ASWIN_GET_FLAGS (asw, AS_Sticky));
			if (!grid_data->frame_only)
				add_canvas_grid (grid_data->grid, asw->client_canvas,
												 outer_gravity / 2, (inner_gravity * 2) / 3,
												 ASWIN_GET_FLAGS (asw, AS_Sticky));
		}
	}
	return True;
}

ASGrid *make_desktop_grid (int desk, int min_layer, Bool frame_only,
													 ASWindow * target)
{
	struct ASWindowGridAuxData grid_data;
	int resist = Scr.Feel.EdgeResistanceMove;
	int attract = Scr.Feel.EdgeAttractionScreen;
	int i;
	ASVector *free_space_list = NULL;
	XRectangle *rects = NULL;
	int w = target->status->width;
	int h = target->status->height;
	ASGeometry area;

	grid_data.desk = desk;
	grid_data.min_layer = min_layer;
	grid_data.frame_only = frame_only;
	grid_data.grid = safecalloc (1, sizeof (ASGrid));
	grid_data.grid->curr_vx = Scr.Vx;
	grid_data.grid->curr_vy = Scr.Vy;
	grid_data.ignore_avoid_cover = True;
	grid_data.target = target;
#if 0
	area.x = vx;
	area.y = vy;
	area.width = Scr.MyDisplayWidth;
	area.height = Scr.MyDisplayHeight;
#else
	area.x = 0;
	area.y = 0;
	area.width = Scr.VxMax + Scr.MyDisplayWidth;
	area.height = Scr.VyMax + Scr.MyDisplayHeight;
#endif
	/* even though we are not limited to free space - it is best to avoid windows with AvoidCover
	 * bit set */
	free_space_list =
			build_free_space_list (target, &area, AS_LayerHighest,
														 AS_LayerHighest);
	rects = PVECTOR_HEAD (XRectangle, free_space_list);

	i = PVECTOR_USED (free_space_list);
	/* now we need to find the biggest rectangle : */
	while (--i >= 0)
		if (rects[i].width >= w && rects[i].height >= h) {
			grid_data.ignore_avoid_cover = False;
			break;
		}
	destroy_asvector (&free_space_list);

//    add_canvas_grid( grid_data.grid, Scr.RootCanvas, resist, attract, vx, vy );

	add_gridline (grid_data.grid, 0, 0, Scr.MyDisplayWidth, resist, attract,
								ASGL_Absolute);
	add_gridline (grid_data.grid, Scr.MyDisplayHeight, 0, Scr.MyDisplayWidth,
								attract, resist, ASGL_Absolute);
	add_gridline (grid_data.grid, 0, 0, Scr.MyDisplayHeight, resist, attract,
								ASGL_Absolute | ASGL_Vertical);
	add_gridline (grid_data.grid, Scr.MyDisplayWidth, 0, Scr.MyDisplayHeight,
								attract, resist, ASGL_Absolute | ASGL_Vertical);

	/* add all the window edges for this desktop : */
	iterate_asbidirlist (Scr.Windows->clients, get_aswindow_grid_iter_func,
											 (void *)&grid_data, NULL, False);

#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	print_asgrid (grid_data.grid);
#endif

	return grid_data.grid;
}

void setup_aswindow_moveresize (ASWindow * asw,
																struct ASMoveResizeData *mvrdata)
{
	if (asw->frame_data && asw->tbar) {
		if (asw->frame_data->condense_titlebar !=
				NO_ALIGN /* && mvrdata->move_only  */ ) {
			if (ASWIN_HFLAGS (asw, AS_VerticalTitle))
				mvrdata->title_west = asw->tbar->width;
			else
				mvrdata->title_north = asw->tbar->height;
		}
	}
	raise_scren_panframes (ASDefaultScr);
	mvrdata->below_sibling = get_lowest_panframe (ASDefaultScr);
	set_moveresize_restrains (mvrdata, asw->hints, asw->status);
	mvrdata->grid =
			make_desktop_grid (Scr.CurrentDesk, AS_LayerDesktop, False, asw);
	Scr.moveresize_in_progress = mvrdata;
}

void apply_aswindow_moveresize (struct ASMoveResizeData *data)
{
	ASWindow *asw = window2ASWindow (AS_WIDGET_WINDOW (data->mr));
	LOCAL_DEBUG_OUT ("%dx%d%+d%+d", data->curr.width, data->curr.height,
									 data->curr.x, data->curr.y);
	if (asw && !ASWIN_GET_FLAGS (asw, AS_Dead)) {
		int new_width = data->curr.width;
		int new_height = data->curr.height;
		Bool server_grabbed = False;
		if (ASWIN_GET_FLAGS (asw, AS_Shaded)) {
			new_width = asw->status->width;
			new_height = asw->status->height;
#if 0
			/* lets only move us as we are in shaded state : */
			move_canvas (asw->frame_canvas, data->curr.x, data->curr.y);
		} else {
			if (data->curr.width != data->last.width ||
					data->curr.height != data->last.height) {
				int client_width =
						data->curr.width - (asw->frame_canvas->width -
																asw->client_canvas->width);
				int client_height =
						data->curr.height - (asw->frame_canvas->height -
																 asw->client_canvas->height);
				XGrabServer (dpy);
				server_grabbed = True;
				resize_canvas (asw->client_canvas, client_width, client_height);
			}
			moveresize_canvas (asw->frame_canvas, data->curr.x, data->curr.y,
												 data->curr.width, data->curr.height);
			ASSync (False);
#endif
		}
		if (ASWIN_GET_FLAGS (asw, AS_ShapedDecor | AS_Shaped) && (data->curr.width > data->last.width || data->curr.height > data->last.height)) {	/* this greately reduces flickering on resizing of shaped windows : */
			XRectangle rect;
			rect.x = 0;
			rect.y = 0;
			moveresize_canvas (asw->frame_canvas, data->curr.x, data->curr.y,
												 data->curr.width, data->curr.height);
			if (data->curr.width > data->last.width) {
				if (get_flags (asw->frame_data->condense_titlebar, ALIGN_LEFT)
						&& asw->tbar)
					rect.y = asw->tbar->height;
				rect.width = data->curr.width - data->last.width;
				rect.height = (int)data->last.height - rect.y;
				XShapeCombineRectangles (dpy, asw->frame, ShapeBounding,
																 data->last.width, 0, &rect, 1, ShapeUnion,
																 Unsorted);
			}
			if (data->curr.height > data->last.height) {
				if (get_flags (asw->frame_data->condense_titlebar, ALIGN_RIGHT)
						&& asw->tbar)
					rect.x = asw->tbar->width;
				rect.width = (int)data->curr.width - rect.x;
				rect.height = data->curr.height - data->last.height;
				XShapeCombineRectangles (dpy, asw->frame, ShapeBounding,
																 0, data->last.height, &rect, 1,
																 ShapeUnion, Unsorted);
			}
		}
		moveresize_aswindow_wm (asw, data->curr.x, data->curr.y, new_width,
														new_height, False);
		if (server_grabbed)
			XUngrabServer (dpy);
	}
}

void apply_aswindow_move (struct ASMoveResizeData *data)
{
	ASWindow *asw = window2ASWindow (AS_WIDGET_WINDOW (data->mr));
	LOCAL_DEBUG_OUT ("%dx%d%+d%+d", asw->status->width, asw->status->height,
									 data->curr.x, data->curr.y);
	if (asw && !ASWIN_GET_FLAGS (asw, AS_Dead)) {
		/* lets only move us as we maybe in shaded state : */
		move_canvas (asw->frame_canvas, data->curr.x, data->curr.y);
		ASSync (False);
		moveresize_aswindow_wm (asw, data->curr.x, data->curr.y,
														asw->status->width, asw->status->height,
														False);
	}
}

void complete_aswindow_moveresize (struct ASMoveResizeData *data,
																	 Bool cancelled)
{
	ASWindow *asw = window2ASWindow (AS_WIDGET_WINDOW (data->mr));
	if (asw && !ASWIN_GET_FLAGS (asw, AS_Dead)) {
		if (cancelled) {
			SHOW_CHECKPOINT;
			LOCAL_DEBUG_OUT ("%dx%d%+d%+d", data->start.width,
											 data->start.height, data->start.x, data->start.y);
			moveresize_aswindow_wm (asw, data->start.x, data->start.y,
															data->start.width, data->start.height,
															False);
		} else {
			SHOW_CHECKPOINT;
			LOCAL_DEBUG_OUT ("%dx%d%+d%+d", data->curr.width, data->curr.height,
											 data->curr.x, data->curr.y);
			moveresize_aswindow_wm (asw, data->curr.x, data->curr.y,
															data->curr.width, data->curr.height, False);
		}
		ASWIN_CLEAR_FLAGS (asw, AS_MoveresizeInProgress);
		asw->frame_canvas->root_x = -10000;
		asw->frame_canvas->root_y = -10000;
		asw->frame_canvas->width = 1;
		asw->frame_canvas->height = 1;

		on_window_moveresize (asw, asw->frame);
		broadcast_config (M_CONFIGURE_WINDOW, asw);
	}
	Scr.moveresize_in_progress = NULL;
}

void complete_aswindow_move (struct ASMoveResizeData *data, Bool cancelled)
{
	ASWindow *asw = window2ASWindow (AS_WIDGET_WINDOW (data->mr));
	if (asw && !ASWIN_GET_FLAGS (asw, AS_Dead)) {
		if (cancelled) {
			SHOW_CHECKPOINT;
			LOCAL_DEBUG_OUT ("%dx%d%+d%+d", data->start.width,
											 data->start.height, data->start.x, data->start.y);
			moveresize_aswindow_wm (asw, data->start.x, data->start.y,
															data->start.width, data->start.height,
															False);
		} else {
			SHOW_CHECKPOINT;
			LOCAL_DEBUG_OUT ("%dx%d%+d%+d", data->start.width,
											 data->start.height, data->curr.x, data->curr.y);
			moveresize_aswindow_wm (asw, data->curr.x, data->curr.y,
															data->start.width, data->start.height,
															False);
		}

		ASWIN_CLEAR_FLAGS (asw, AS_MoveresizeInProgress);
		asw->frame_canvas->root_x = -10000;
		asw->frame_canvas->root_y = -10000;
		on_window_moveresize (asw, asw->frame);
		broadcast_config (M_CONFIGURE_WINDOW, asw);
	}
	Scr.moveresize_in_progress = NULL;
}

