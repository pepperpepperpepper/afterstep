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

#include "placement_internal.h"


typedef struct ASFreeRectangleAuxData {
	ASVector *list;
	long desk;
	int min_layer;								/* applies only to non-AvoidCover windows */
	int max_layer;								/* applies to all windows - including Avoid Cover */
	Bool frame_only;
	ASGeometry area;
	ASWindow *to_skip;
} ASFreeRectangleAuxData;


/*************************************************************************/
/* here we build vector of rectangles, representing one available
 * space each :
 */
/*************************************************************************/

static Bool get_free_rectangles_iter_func (void *data, void *aux_data)
{
	ASFreeRectangleAuxData *fr_data = (ASFreeRectangleAuxData *) aux_data;
	ASWindow *asw = (ASWindow *) data;

	if (asw
			&& (!IsValidDesk (fr_data->desk)
					|| ASWIN_DESK (asw) == fr_data->desk)
			&& asw != fr_data->to_skip
			&& !ASWIN_GET_FLAGS (asw, AS_Fullscreen | AS_ShortLived)
			&& fr_data->max_layer >= ASWIN_LAYER (asw)
			&& (ASWIN_HFLAGS (asw, AS_AvoidCover)
					|| fr_data->min_layer <= ASWIN_LAYER (asw))) {
		int min_vx = fr_data->area.x, max_vx =
				fr_data->area.x + fr_data->area.width;
		int min_vy = fr_data->area.y, max_vy =
				fr_data->area.y + fr_data->area.height;
		int x, y;
		unsigned int width, height, bw;

		if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
			if (asw->icon_canvas != asw->icon_title_canvas
					&& asw->icon_title_canvas != NULL) {
				get_current_canvas_geometry (asw->icon_title_canvas, &x, &y,
																		 &width, &height, &bw);
				x += Scr.Vx;
				y += Scr.Vy;
				if (x + width + bw >= min_vx && x - bw < max_vx
						&& y + height + bw >= min_vy && y - bw < max_vy)
					subtract_rectangle_from_list (fr_data->list, x - bw, y - bw,
																				x + width + bw, y + height + bw);
			}
			get_current_canvas_geometry (asw->icon_canvas, &x, &y, &width,
																	 &height, &bw);
		} else
			get_current_canvas_geometry (asw->frame_canvas, &x, &y, &width,
																	 &height, &bw);
		x += Scr.Vx;
		y += Scr.Vy;
		LOCAL_DEBUG_OUT
				("frame_geom = %dx%d%+d%+d, h_limits = %d,%d; v_limits = %d,%d",
				 width, height, x - bw, y - bw, min_vx, max_vx, min_vy, max_vy);
		if (x + (int)width + (int)bw >= min_vx && x - (int)bw < max_vx
				&& y + (int)height + (int)bw >= min_vy && y - (int)bw < max_vy)
			subtract_rectangle_from_list (fr_data->list, x - (int)bw,
																		y - (int)bw, x + (int)width + (int)bw,
																		y + (int)height + (int)bw);
	}

	return True;
}

ASVector *build_free_space_list (ASWindow * to_skip,
																 ASGeometry * area, int min_layer,
																 int max_layer)
{
	ASVector *list = create_asvector (sizeof (XRectangle));
	ASFreeRectangleAuxData aux_data;
	XRectangle seed_rect;

	aux_data.to_skip = to_skip;
	aux_data.list = list;
	aux_data.desk = Scr.CurrentDesk;
	aux_data.min_layer = min_layer;
	aux_data.max_layer = max_layer;
	aux_data.area = *area;

	/* must seed the list with the single rectangle representing the area : */
	seed_rect.x = area->x;
	seed_rect.y = area->y;
	seed_rect.width = area->width;
	seed_rect.height = area->height;

	append_vector (list, &seed_rect, 1);

	iterate_asbidirlist (Scr.Windows->clients, get_free_rectangles_iter_func,
											 (void *)&aux_data, NULL, False);

#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	print_rectangles_list (list);
#endif

	return list;
}

