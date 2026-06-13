/****************************************************************************
 * Copyright (c) 2001 Sasha Vasko <sasha at aftercode.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *****************************************************************************/

/***********************************************************************
 * afterstep window move/resize code
 ***********************************************************************/

#include "../configure.h"

#define LOCAL_DEBUG
#include "asapp.h"
#include <signal.h>
#include "../libAfterImage/afterimage.h"
#include "afterstep.h"
#include "event.h"
#include "mystyle.h"
#include "screen.h"
#include "hints.h"
#ifdef NO_ASRENDER
#include "decor.h"
#include "canvas.h"
#endif
#include "moveresize.h"

#ifdef NO_DEBUG_OUTPUT
#undef SHOW_CHECKPOINT
#define SHOW_CHECKPOINT while(0)
#endif


#define KEY_ENTER   36
#define KEY_LEFT    100
#define KEY_UP      98
#define KEY_RIGHT   102
#define KEY_DOWN    104

#define MOVE_NPIX_AT_ONCE 10
#include "moveresize_internal.h"

/* Grid snapping and size-constraint math (attract/resist/adjust/restrain),
 * split out of moveresize.c. Self-contained: uses no interactive-state
 * globals and calls back into none of the interaction code. The cross-TU
 * helpers are declared in moveresize_internal.h. */

/***********************************************************************
 * Snapping to the grid :
 **********************************************************************/
#define ATTRACT_SIDE_ABOVE(t,b,band,grav,size) \
	if( (grav) > 0 ) \
	{	if( (t) > (band)-(grav) && (t) < (band) )	(t) = (band) ; \
		else if( size > 0 && (b) > (band)-(grav) && (b) < (band)) (t) = (band)-(size);} \

#define ATTRACT_SIDE_BELOW(t,b,band,grav,size) \
	if( (grav) > 0 ) \
	{ 	if( (t) < (band)+(grav) && (t) > (band) )	(t) = (band) ; \
		else if( size > 0 && (b) < (band)+(grav) && (b) > (band)) (t) = (band)-(size);} \

int
attract_side (ASGrid * g, ASGridLine * l, int pos, int size, int lim1,
							int lim2)
{
	int head = pos;
	int tail = head + size;

	for (; l != NULL; l = l->next) {
		int band, start, end;

		grid_coords2real (g, l, &band, &start, &end);

		if (lim2 >= start && lim1 <= end) {
			ATTRACT_SIDE_ABOVE (head, tail, band, l->gravity_above, size);
			ATTRACT_SIDE_BELOW (head, tail, band, l->gravity_below, size);
			if (head != pos) {
				LOCAL_DEBUG_OUT ("attracted by %d, %d-%d, %d,%d", band, start, end,
												 l->gravity_above, l->gravity_below);
				return head;
			}
		}
	}
	return pos;
}

int
resist_west_side (ASGrid * g, ASGridLine * l, int pos, int new_pos,
									int lim1, int lim2)
{
	for (; l != NULL; l = l->next) {
		int band, start, end;

		grid_coords2real (g, l, &band, &start, &end);
/*LOCAL_DEBUG_OUT( "lim = %+d%+d, l = (%d,%d), pos = (%d,%d), band = %d, grav = %d", lim1, lim2, l->start, l->end, pos, new_pos, l->band, l->gravity_above );*/
		if (lim2 >= start && lim1 <= end && l->gravity_above < 0 && band <= pos
				&& band >= new_pos) {
			new_pos = band;
		}
	}
	return MIN (pos, new_pos);
}

int
resist_east_side (ASGrid * g, ASGridLine * l, int pos, int new_pos,
									int lim1, int lim2)
{
	for (; l != NULL; l = l->next) {
		int band, start, end;

		grid_coords2real (g, l, &band, &start, &end);
		if (lim2 >= start && lim1 <= end && l->gravity_below < 0 && band >= pos
				&& band <= new_pos) {
			new_pos = band;
		}
	}
	return MAX (pos, new_pos);
}

Bool
attract_corner (ASMoveResizeData * data, int *x_inout, int *y_inout)
{
	int new_left;
	int new_top;
	Bool res = False;
	ASGrid *grid = data->grid;
	int bw = data->bw;
	int curr_width = data->curr.width + bw * 2;
	int curr_height = data->curr.height + bw * 2;
	int curr_left = data->curr.x;
	int curr_top = data->curr.y;

	if (grid) {
		/* step 1 - attraction : */
		new_left =
				attract_side (grid, grid->v_lines, *x_inout, curr_width, *y_inout,
											*y_inout + curr_height);
		if (new_left == *x_inout && data->title_west > 0)
			new_left =
					attract_side (grid, grid->v_lines, *x_inout + data->title_west,
												curr_width, *y_inout,
												*y_inout + curr_height) - data->title_west;

		new_top =
				attract_side (grid, grid->h_lines, *y_inout, curr_height, *x_inout,
											*x_inout + curr_width);
		if (new_top == *y_inout && data->title_north > 0)
			new_top =
					attract_side (grid, grid->h_lines, *y_inout + data->title_north,
												curr_height, *x_inout,
												*x_inout + curr_width) - data->title_north;


		/* step 2 - resistance : */
		LOCAL_DEBUG_OUT
				("+++attracted\t\t(%+d%+d) projected\t(%+d%+d) current\t(%+d%+d)",
				 new_left, new_top, *x_inout, *y_inout, curr_left, curr_top);
		if (new_left > curr_left)		/* moving eastwards : */
			new_left =
					resist_east_side (grid, grid->v_lines, curr_left + curr_width,
														new_left + curr_width,
														new_top + data->title_north,
														new_top + curr_height) - curr_width;
		else if (new_left < curr_left)
			new_left =
					resist_west_side (grid, grid->v_lines,
														curr_left + data->title_west,
														new_left + data->title_west,
														new_top + data->title_north,
														new_top + curr_height) - data->title_west;

		if (new_top > curr_top)			/* moving southwards : */
			new_top =
					resist_east_side (grid, grid->h_lines, curr_top + curr_height,
														new_top + curr_height,
														new_left + data->title_west,
														new_left + curr_width) - curr_height;
		else if (new_top < curr_top)
			new_top =
					resist_west_side (grid, grid->h_lines,
														curr_top + data->title_north,
														new_top + data->title_north,
														new_left + data->title_west,
														new_left + curr_width) - data->title_north;
		LOCAL_DEBUG_OUT
				("---resisted\t\t(%+d%+d) projected\t(%+d%+d) current\t(%+d%+d)",
				 new_left, new_top, *x_inout, *y_inout, curr_left, curr_top);


		res = (new_top != *y_inout || new_left != *x_inout);
		*x_inout = new_left;
		*y_inout = new_top;
	}
	return res;
}

int
adjust_west_side (ASGrid * grid, ASGridLine * gridlines, int dpos,
									int *pos_inout, int *size_inout, int lim1, int lim2,
									int title_west, int title_north)
{
	int pos = *pos_inout, new_pos = pos + dpos;
	int adjusted_dpos = dpos;

/* positive dpos - window shrinking, negative - growing */
	if (gridlines) {
		int original = new_pos;

		new_pos = attract_side (grid, gridlines, new_pos, 0, lim1, lim2);
		if (new_pos == original && title_west > 0)
			new_pos =
					attract_side (grid, gridlines, new_pos + title_west, 0, lim1,
												lim2) - title_west;
		if (new_pos < pos) {				/* we need to resist move if we are offending any negative gridline */
			new_pos =
					resist_west_side (grid, gridlines, pos + title_west, new_pos,
														lim1 + title_north, lim2) - title_west;
		}
	}
	adjusted_dpos = MIN (new_pos - pos, *size_inout - 1);

	*pos_inout += adjusted_dpos;
	*size_inout -= adjusted_dpos;
	LOCAL_DEBUG_OUT
			("pos = %d, new_pos = %d, lim1 = %d, lim2 = %d, dpos = %d, adjusted_dpos = %d",
			 pos, new_pos, lim1, lim2, dpos, adjusted_dpos);
	return adjusted_dpos;
}

int
adjust_east_side (ASGrid * grid, ASGridLine * gridlines, int dpos, int pos,
									int *size_inout, int lim1, int lim2, int title_north)
{
	int adjusted_dpos = dpos;
	int new_pos = pos;

	if (gridlines) {
		pos += *size_inout;
		new_pos = pos + dpos;
		new_pos = attract_side (grid, gridlines, new_pos, 0, lim1, lim2);
		if (new_pos > pos) {				/* we need to resist move if we are offending any negative gridline */
			new_pos =
					resist_east_side (grid, gridlines, pos, new_pos,
														lim1 + title_north, lim2);
		}
		adjusted_dpos = new_pos - pos;
	}
	if (adjusted_dpos + *size_inout <= 0)
		adjusted_dpos = 1 - (int)(*size_inout);

	*size_inout += adjusted_dpos;
	LOCAL_DEBUG_OUT
			("pos = %d, new_pos = %d, lim1 = %d, lim2 = %d, dpos = %d, adjusted_dpos = %d",
			 pos, new_pos, lim1, lim2, dpos, adjusted_dpos);
	return adjusted_dpos;
}

int restrain_size (int size, int min_val, int incr, int max_val)
{
	if (size < min_val)
		size = min_val;
	else if (max_val > 0 && size > max_val)
		size = max_val;
	else if (incr != 0)						/* negative increment for growing windows, positive - for shrinking */
		size = min_val + ((size + (incr / 2) - min_val) / incr) * incr;
	return size;
}

int
restrain_east_side (int dpos, int *size_inout, int min_val, int incr,
										int max_val)
{
	int adjusted_dpos = dpos;
	int size =
			restrain_size (*size_inout, min_val, (dpos < 0) ? incr : -incr,
										 max_val);

	adjusted_dpos += size - (*size_inout);
	LOCAL_DEBUG_OUT
			("in_size = %d, out_size = %d, min_val = %d, incr = %d, max_val = %d, dpos = %d, adjusted_dpos = %d",
			 *size_inout, size, min_val, incr, max_val, dpos, adjusted_dpos);
	*size_inout = size;
	return adjusted_dpos;
}

int
restrain_west_side (int dpos, int *wpos_inout, int *size_inout,
										int min_val, int incr, int max_val)
{
	int adjusted_dpos = dpos;
	int size =
			restrain_size (*size_inout, min_val, (dpos > 0) ? incr : -incr,
										 max_val);
	int delta = *size_inout - size;

	adjusted_dpos += delta;
	*wpos_inout += delta;
	LOCAL_DEBUG_OUT
			("in_size = %d, out_size = %d, min_val = %d, incr = %d, max_val = %d, dpos = %d, adjusted_dpos = %d",
			 *size_inout, size, min_val, incr, max_val, dpos, adjusted_dpos);
	*size_inout = size;
	return adjusted_dpos;
}
