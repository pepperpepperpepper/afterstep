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


/*************************************************************************/
/* placement routines : */
/*************************************************************************/
void
apply_placement_result (ASStatusHints * status, XRectangle * anchor,
												ASHints * hints, ASFlagType flags, int vx, int vy,
												unsigned int width, unsigned int height)
{
#define apply_placement_result_asw(asw,flags,vx,vy,width,height)  apply_placement_result((asw)->status, &((asw)->anchor), (asw)->hints, flags, vx, vy, width, height )

	if (get_flags (flags, XValue)) {
		status->x = vx;
		if (!get_flags (status->flags, AS_Sticky))
			status->x -= status->viewport_x;
		else
			status->x -= Scr.Vx;
	}
	if (get_flags (flags, YValue)) {
		status->y = vy;
		if (!get_flags (status->flags, AS_Sticky))
			status->y -= status->viewport_y;
		else
			status->y -= Scr.Vy;
	}
	if (get_flags (flags, WidthValue) && width > 0)
		status->width = width;

	if (get_flags (flags, HeightValue) && height > 0)
		status->height = height;

	status2anchor (anchor, hints, status, Scr.VxMax + Scr.MyDisplayWidth,
								 Scr.VyMax + Scr.MyDisplayHeight);
}

static int
move_placement_left (ASVector * free_space_list, int x, int y, int w,
										 int h)
{
	XRectangle *rects = PVECTOR_HEAD (XRectangle, free_space_list);
	int i = PVECTOR_USED (free_space_list);
	while (--i >= 0)
		if (y >= rects[i].y && y + h <= rects[i].y + (int)rects[i].height
				&& rects[i].x < x && rects[i].x + (int)rects[i].width >= x)
			x = rects[i].x;
	return x;
}

static int
move_placement_right (ASVector * free_space_list, int x, int y, int w,
											int h)
{
	XRectangle *rects = PVECTOR_HEAD (XRectangle, free_space_list);
	int i = PVECTOR_USED (free_space_list);
	while (--i >= 0)
		if (y >= rects[i].y && y + h <= rects[i].y + (int)rects[i].height
				&& rects[i].x <= x + w && rects[i].x + (int)rects[i].width > x + w)
			x = rects[i].x + (int)rects[i].width - w;
	return x;
}

static int
move_placement_up (ASVector * free_space_list, int x, int y, int w, int h)
{
	XRectangle *rects = PVECTOR_HEAD (XRectangle, free_space_list);
	int i = PVECTOR_USED (free_space_list);
	while (--i >= 0)
		if (x >= rects[i].x && x + w <= rects[i].x + (int)rects[i].width
				&& rects[i].y < y && rects[i].y + (int)rects[i].height >= y)
			y = rects[i].y;
	return y;
}

static int
move_placement_down (ASVector * free_space_list, int x, int y, int w,
										 int h)
{
	XRectangle *rects = PVECTOR_HEAD (XRectangle, free_space_list);
	int i = PVECTOR_USED (free_space_list);
	while (--i >= 0)
		if (x >= rects[i].x && x + w <= rects[i].x + (int)rects[i].width
				&& rects[i].y <= y + h
				&& rects[i].y + (int)rects[i].height > y + h)
			y = rects[i].y + (int)rects[i].height - h;
	return y;
}

static Bool do_smart_placement (ASWindow * asw, ASWindowBox * aswbox,
																ASGeometry * area)
{
	ASVector *free_space_list =
			build_free_space_list (asw, area, ASWIN_LAYER (asw),
														 AS_LayerHighest);
	XRectangle *rects = PVECTOR_HEAD (XRectangle, free_space_list);
	int i, selected = -1;
	unsigned short w = asw->status->width;
	unsigned short h = asw->status->height;
	unsigned short dw = w > 100 ? w * 5 / 100 : 5, dh =
			h >= 100 ? h * 5 / 100 : 5;
	int spacer_x = -1;
	int spacer_y = -1;

	LOCAL_DEBUG_OUT ("size=%dx%d, delta=%dx%d", w, h, dw, dh);
	/* now we have to find the optimal rectangle from the list */
	/* pass 1: find rectangle that fits both width and height with margin +- 5% of the window size */
	i = PVECTOR_USED (free_space_list);
	while (--i >= 0)
		if (rects[i].width >= w && rects[i].height >= h &&
				rects[i].width - w < dw && rects[i].height - h < dh) {
			if (selected >= 0)
				if (rects[i].width * rects[i].height >=
						rects[selected].width * rects[selected].height)
					continue;
			selected = i;
		}
	LOCAL_DEBUG_OUT ("pass1: %d", selected);

	if (selected < 0) {
		i = PVECTOR_USED (free_space_list);
		if (w > 200 && h > 200) {		/* simply find the biggest rectangle that fits  */
			while (--i >= 0)
				if (rects[i].width >= w && rects[i].height >= h) {
					if (selected >= 0)
						if (rects[i].width * rects[i].height <=
								rects[selected].width * rects[selected].height)
							continue;
					selected = i;
				}
			LOCAL_DEBUG_OUT ("pass2a: %d", selected);
		} else if (w > h) {					/* try and fit it by the horizontal edge of the screen */
			while (--i >= 0)
				if (rects[i].width >= w && rects[i].height >= h &&
						(rects[i].y < 100
						 || rects[i].y + rects[i].height >
						 area->y + area->height - 100)) {
					selected = i;
					if (rects[i].y >= 100)
						spacer_y = rects[i].height - h;
					break;
				}
			LOCAL_DEBUG_OUT ("pass2b: %d", selected);
		} else {
			/* try and fit it by the vertical edge of the screen */
			while (--i >= 0)
				if (rects[i].width >= w && rects[i].height >= h &&
						(rects[i].x < 100
						 || rects[i].x + rects[i].width >
						 area->x + area->width - 100)) {
					selected = i;
					if (rects[i].x >= 100)
						spacer_x = rects[i].width - w;
					break;
				}
			LOCAL_DEBUG_OUT ("pass2c: %d", selected);
		}
	}
	/* if width < height then swap passes 2 and 3 */
	/* pass 2: find rectangle that fits width within margin +- 5% of the window size  and has maximum height
	 * left after placement */
	if (selected < 0) {
		i = PVECTOR_USED (free_space_list);
		if (w >= h) {
			while (--i >= 0)
				if (rects[i].width >= w && rects[i].height >= h
						&& rects[i].width - w < dw) {
					if (selected >= 0)
						if (rects[i].height < rects[selected].height)
							continue;
					selected = i;
				}
		} else {
			while (--i >= 0)
				if (rects[i].width >= w && rects[i].height >= h
						&& rects[i].height - h < dh) {
					if (selected >= 0)
						if (rects[i].width < rects[selected].width)
							continue;
					selected = i;
				}
		}
	}
	LOCAL_DEBUG_OUT ("pass2: %d", selected);
	/* pass 3: find rectangle that fits height within margin +- 5% of the window size  and has maximum width
	 * left after placement */
	if (selected < 0) {
		i = PVECTOR_USED (free_space_list);
		if (w >= h) {
			while (--i >= 0)
				if (rects[i].width >= w && rects[i].height >= h
						&& rects[i].height - h < dh) {
					if (selected >= 0)
						if (rects[i].width < rects[selected].width)
							continue;
					selected = i;
				}
		} else {
			while (--i >= 0)
				if (rects[i].width >= w && rects[i].height >= h
						&& rects[i].width - w < dw) {
					if (selected >= 0)
						if (rects[i].height < rects[selected].height)
							continue;
					selected = i;
				}
		}
	}
	LOCAL_DEBUG_OUT ("pass3: %d", selected);
	/* pass 4: if width >= height then find rectangle with smallest width difference and largest height difference */
	if (selected < 0 && w >= h) {
		i = PVECTOR_USED (free_space_list);
		while (--i >= 0)
			if (rects[i].width >= w && rects[i].height >= h) {
				selected = i;
				break;
			}
		while (--i >= 0)
			if (rects[i].width >= w && rects[i].height >= h) {
				int dw = (rects[i].width > w) ? rects[i].width - w : 1;
				int dw_sel =
						(rects[selected].width > w) ? rects[selected].width - w : 1;
				if (((rects[i].height - h) * Scr.MyDisplayWidth) / dw >
						((rects[selected].height - h) * Scr.MyDisplayWidth) / dw_sel)
					selected = i;
			}
	}
	LOCAL_DEBUG_OUT ("pass4: %d", selected);
	/* pass 5: if width < height then find rectangle with biggest width difference and smallest height difference */
	if (selected < 0) {
		i = PVECTOR_USED (free_space_list);
		while (--i >= 0)
			if (rects[i].width >= w && rects[i].height >= h) {
				selected = i;
				break;
			}
		while (--i >= 0)
			if (rects[i].width >= w && rects[i].height >= h) {
				int dh = (rects[i].height > h) ? rects[i].height - h : 1;
				int dh_sel =
						(rects[selected].height > h) ? rects[selected].height - h : 1;
				if (((rects[i].width - w) * Scr.MyDisplayHeight) / dh >
						((rects[selected].width - w) * Scr.MyDisplayHeight) / dh_sel)
					selected = i;
			}
	}
	LOCAL_DEBUG_OUT ("pass5: %d", selected);

	if (selected >= 0) {
		int target_x, target_y;
		int dx, dy;
		int move_left, move_up;
		Bool changed;
		if (spacer_x < 0) {
			spacer_x = 0;
			if (rects[selected].width > w) {
				int to_right =
						(area->x + (int)area->width) - (rects[selected].x +
																						(int)rects[selected].width);
				if (to_right < rects[selected].x - area->x)
					spacer_x = (int)rects[selected].width - (int)w;
			}
		}
		if (spacer_y < 0) {
			spacer_y = 0;
			if (rects[selected].height > h) {
				int to_bottom =
						(area->y + (int)area->height) - (rects[selected].y +
																						 (int)rects[selected].height);
				if (to_bottom < rects[selected].y - area->y)
					spacer_y = (int)rects[selected].height - (int)h;
			}
		}
		target_x = rects[selected].x + spacer_x;
		target_y = rects[selected].y + spacer_y;

		do {
			int new_x = target_x, new_y = target_y;
			changed = False;
			dx = target_x - area->x;
			dy = target_y - area->y;

			move_left = (dx < area->width - (dx + w));
			if (!move_left)
				dx = area->width - (dx + w);

			move_up = (dy < area->height - (dy + h));
			if (!move_up)
				dy = area->height - (dy + h);
			/* we want to place window as close as possible to the edge of the area */
			if (dx > dy) {
				if (move_left)
					new_x =
							move_placement_left (free_space_list, target_x, target_y, w,
																	 h);
				else
					new_x =
							move_placement_right (free_space_list, target_x, target_y, w,
																		h);
				if (dy > 0) {
					if (move_up)
						new_y =
								move_placement_up (free_space_list, target_x, target_y, w,
																	 h);
					else
						new_y =
								move_placement_down (free_space_list, target_x, target_y,
																		 w, h);
				}

			} else if (dy > 0) {
				if (move_up)
					new_y =
							move_placement_up (free_space_list, target_x, target_y, w,
																 h);
				else
					new_y =
							move_placement_down (free_space_list, target_x, target_y, w,
																	 h);
				if (dx > 0) {
					if (move_left)
						new_x =
								move_placement_left (free_space_list, target_x, target_y,
																		 w, h);
					else
						new_x =
								move_placement_right (free_space_list, target_x, target_y,
																			w, h);
				}
			}
			LOCAL_DEBUG_OUT
					("move_left = %d, move_up = %d, new = %+d%+d, org = %+d%+d",
					 move_left, move_up, new_x, new_y, target_x, target_y);
			changed = (target_x != new_x || target_y != new_y);
			target_x = new_x;
			target_y = new_y;
		} while (changed);
		apply_placement_result_asw (asw, XValue | YValue, target_x, target_y,
																0, 0);
		LOCAL_DEBUG_OUT ("success: status(%+d%+d), anchor(%+d,%+d)",
										 asw->status->x, asw->status->y, asw->anchor.x,
										 asw->anchor.y);
	} else {
		LOCAL_DEBUG_OUT ("failed%s", "");
	}

	destroy_asvector (&free_space_list);
	return (selected >= 0);
}

static Bool do_random_placement (ASWindow * asw, ASWindowBox * aswbox,
																 ASGeometry * area, Bool free_space_only)
{
	int selected = -1;
	unsigned int w = asw->status->width;
	unsigned int h = asw->status->height;
	static CARD32 rnd32_seed = 345824357;
	ASVector *free_space_list = NULL;
	XRectangle *rects = NULL;
	int i;
	long selected_deficiency = 1000000000;

#ifndef MY_RND32
#define MAX_MY_RND32		0x00ffffffff
#ifdef WORD64
#define MY_RND32() (rnd32_seed = ((1664525L*rnd32_seed)&MAX_MY_RND32)+1013904223L)
#else
#define MY_RND32() (rnd32_seed = (1664525L*rnd32_seed)+1013904223L)
#endif
#endif

	if (rnd32_seed == 345824357)
		rnd32_seed += time (NULL);

	/* even though we are not limited to free space - it is best to avoid windows with AvoidCover
	 * bit set */
	free_space_list = build_free_space_list (asw, area,
																					 free_space_only ?
																					 ASWIN_LAYER (asw) :
																					 AS_LayerHighest,
																					 AS_LayerHighest);
	rects = PVECTOR_HEAD (XRectangle, free_space_list);

	i = PVECTOR_USED (free_space_list);
	while (--i >= 0) {
		if (rects[i].width >= w && rects[i].height >= h) {
			selected_deficiency = 0;
			if (selected >= 0) {
				CARD32 r = MY_RND32 ();
				if ((r & 0x00000100) == 0)
					continue;;

			}
			selected = i;
		} else if (selected_deficiency > 0) {
			int deficiency = 0;
			if (rects[i].width < w)
				deficiency += h * (w - rects[i].width);
			if (rects[i].height < h) {
				deficiency += w * (h - rects[i].height);
				if (rects[i].width < w)
					deficiency -= (w - rects[i].width) * (h - rects[i].height);
			}
#if 0
			/* we may use it if we are required to place window, so we can 
			 * select the largest area available. But ordinarily we should 
			 * default to manuall placement instead! : */
			if (deficiency < selected_deficiency || selected < 0) {
				selected = i;
				selected_deficiency = deficiency;
			}
#endif
		}
	}
	if (selected >= 0) {
		unsigned int new_x = 0, new_y = 0;
		if (rects[selected].width > w) {
			new_x = MY_RND32 ();
			new_x = (new_x % (rects[selected].width - w));
		}

		if (rects[selected].height > h) {
			new_y = MY_RND32 ();
			new_y = (new_y % (rects[selected].height - h));
		}
		LOCAL_DEBUG_OUT ("rect %dx%d%+d%+d, new_pos = %+d%+d",
										 rects[selected].width, rects[selected].height,
										 rects[selected].x, rects[selected].y, new_x, new_y);
		apply_placement_result_asw (asw, XValue | YValue,
																rects[selected].x + new_x,
																rects[selected].y + new_y, 0, 0);
		LOCAL_DEBUG_OUT ("success: status(%+d%+d), anchor(%+d,%+d)",
										 asw->status->x, asw->status->y, asw->anchor.x,
										 asw->anchor.y);
	} else {
		LOCAL_DEBUG_OUT ("failed%s", "");
	}

	destroy_asvector (&free_space_list);
	return (selected >= 0);
}

static Bool
do_maximized_placement (ASWindow * asw, ASWindowBox * aswbox,
												ASGeometry * area)
{
	int selected = -1;
	unsigned int w = asw->status->width;
	unsigned int h = asw->status->height;
#if defined(HAVE_XINERAMA) || defined(HAVE_XRANDR)
	unsigned int x = asw->status->x;
	unsigned int y = asw->status->y;
#endif
	ASVector *free_space_list = NULL;
	XRectangle *rects = NULL;
	int i;

	/* even though we are not limited to free space - it is best to avoid windows with AvoidCover
	 * bit set */
	free_space_list =
			build_free_space_list (asw, area, AS_LayerHighest, AS_LayerHighest);
	rects = PVECTOR_HEAD (XRectangle, free_space_list);

	i = PVECTOR_USED (free_space_list);
	/* now we need to find the biggest rectangle : */
	while (--i >= 0)
		if (rects[i].width >= w && rects[i].height >= h) {
			/* if a rect has been selected */
			if (selected > 0) {
				/* if this rect is smaller than the selected one */
				if (rects[i].width * rects[i].height <
						rects[selected].width * rects[selected].height)
					continue;
			}
			/* select this rectangle because it's bigger */
			selected = i;
		}


	/* if a rect has NOT been selected */
	if (selected < 0) {						/* we simply select the biggest area available : */
		i = PVECTOR_USED (free_space_list);
		while (--i >= 0) {
			if (selected > 0) {
				if (rects[i].width * rects[i].height <
						rects[selected].width * rects[selected].height)
					continue;
			}
			selected = i;
		}
	}


	if (selected >= 0) {
		ASFlagType flags = 0;
		int max_width = rects[selected].width;
		int max_height = rects[selected].height;

#if defined(HAVE_XINERAMA) || defined(HAVE_XRANDR)
		/* the following block makes sure windows
		   are not maximized over multiply heads. */

		int i;
		XRectangle *s = Scr.xinerama_screens;
		int dest_size = -1;
		int dest_rect = 0;
		int inter_width;
		int inter_height;

		if (s != NULL) {

			/* select the xinerama-screen holding most of the
			   window as it is before maximizing it */
			for (i = 0; i < Scr.xinerama_screens_num; ++i) {
				/* if window is not on this xin-rect at all. */
				if ((x < s[i].x && x > s[i].x + s[i].width) ||
						(y < s[i].y && y > s[i].y + s[i].height))
					continue;

				/* window starts left of screen */
				if (s[i].x > x)
					/* and ends on the screen */
					if (s[i].x + s[i].width > x + w)
						inter_width = x + w - s[i].x;
					else
						inter_width = s[i].width;
				else if (s[i].x + s[i].width > x + w)
					inter_width = s[i].width;
				else
					inter_width = s[i].x + s[i].width - x;


				/* window starts above of screen */
				if (s[i].y > y)
					/* and ends on the screen */
					if (s[i].y + s[i].height > y + h)
						inter_height = y + h - s[i].y;
					else
						inter_height = s[i].height;
				else if (s[i].y + s[i].height > y + h)
					inter_height = s[i].height;
				else
					inter_height = s[i].y + s[i].height - y;

				if (inter_width * inter_height > dest_size) {
					/* I like this rect better than the last. */
					dest_rect = i;
					dest_size = inter_width * inter_height;
				}


			}


			if (rects[selected].x < s[dest_rect].x)
				rects[selected].x = s[dest_rect].x;
			if (rects[selected].y < s[dest_rect].y)
				rects[selected].y = s[dest_rect].y;
			if (max_width > s[dest_rect].width)
				max_width = s[dest_rect].width;
			if (max_height > s[dest_rect].height)
				max_height = s[dest_rect].height;
		}
#endif													/* XINERAMA || XRANDR */


		save_aswindow_anchor (asw, ASWIN_GET_FLAGS (asw, AS_MaximizedX),
													ASWIN_GET_FLAGS (asw, AS_MaximizedY));

		if (ASWIN_GET_FLAGS (asw, AS_MaximizedX))
			set_flags (flags, XValue | WidthValue);
		if (ASWIN_GET_FLAGS (asw, AS_MaximizedY))
			set_flags (flags, YValue | HeightValue);

		if (asw->maximize_ratio_x > 0)
			max_width = (asw->maximize_ratio_x * max_width) / 100;
		if (asw->maximize_ratio_y > 0)
			max_height = (asw->maximize_ratio_y * max_height) / 100;

		apply_placement_result_asw (asw, flags, rects[selected].x,
																rects[selected].y, max_width, max_height);
		LOCAL_DEBUG_OUT ("success: status(%+d%+d), anchor(%+d,%+d)",
										 asw->status->x, asw->status->y, asw->anchor.x,
										 asw->anchor.y);
	} else {
		LOCAL_DEBUG_OUT ("failed%s", "");
	}

	destroy_asvector (&free_space_list);
	return (selected >= 0);
}



static Bool do_tile_placement (ASWindow * asw, ASWindowBox * aswbox,
															 ASGeometry * area)
{
	int selected = -1;
	unsigned short w = asw->status->width;
	unsigned short h = asw->status->height;
	ASVector *free_space_list = NULL;
	XRectangle *rects = NULL;
	int i;

	free_space_list =
			build_free_space_list (asw, area, ASWIN_LAYER (asw),
														 AS_LayerHighest);
	rects = PVECTOR_HEAD (XRectangle, free_space_list);

	i = PVECTOR_USED (free_space_list);
	while (--i >= 0)
		if (rects[i].width >= w && rects[i].height >= h) {
			if (selected > 0) {
				if (get_flags (aswbox->flags, ASA_VerticalPriority)) {
					if (get_flags (aswbox->flags, ASA_ReverseOrder)
							|| get_flags (aswbox->flags, ASA_ReverseOrderH)) {
						if (rects[selected].x > rects[i].x)
							continue;
						if (rects[selected].x == rects[i].x
								&& rects[selected].y > rects[i].y)
							continue;
					} else {
						if (rects[selected].x < rects[i].x)
							continue;
						if (rects[selected].x == rects[i].x
								&& rects[selected].y < rects[i].y)
							continue;
					}
				} else if (get_flags (aswbox->flags, ASA_ReverseOrder)
									 || get_flags (aswbox->flags, ASA_ReverseOrderV)) {
					if (rects[selected].y > rects[i].y)
						continue;
					if (rects[selected].y == rects[i].y
							&& rects[selected].x > rects[i].x)
						continue;
				} else {
					if (rects[selected].y < rects[i].y)
						continue;
					if (rects[selected].y == rects[i].y
							&& rects[selected].x < rects[i].x)
						continue;
				}

			}
			selected = i;
		}

	if (selected >= 0) {
		int spacer_x = aswbox->x_spacing;
		int spacer_y = aswbox->y_spacing;
		if (rects[selected].width < w + spacer_x)
			spacer_x = rects[selected].width - w;
		if (rects[selected].height < h + spacer_y)
			spacer_y = rects[selected].height - h;
		apply_placement_result_asw (asw, XValue | YValue,
																rects[selected].x + spacer_x,
																rects[selected].y + spacer_y, 0, 0);
		LOCAL_DEBUG_OUT ("success: status(%+d%+d), anchor(%+d,%+d)",
										 asw->status->x, asw->status->y, asw->anchor.x,
										 asw->anchor.y);
	} else {
		LOCAL_DEBUG_OUT ("failed%s", "");
	}

	destroy_asvector (&free_space_list);
	return (selected >= 0);
}

static Bool do_cascade_placement (ASWindow * asw, ASWindowBox * aswbox,
																	ASGeometry * area)
{
	int newpos = aswbox->cascade_pos + 25;
	int x = newpos, y = newpos;

	if (get_flags (aswbox->flags, ASA_ReverseOrder) ||
			(get_flags (aswbox->flags, ASA_ReverseOrderV) &&
			 get_flags (aswbox->flags, ASA_ReverseOrderH))) {
		x = ((int)(area->width) + area->x) - newpos;
		y = ((int)(area->height) + area->y) - newpos;
	} else if (get_flags (aswbox->flags, ASA_ReverseOrderV)) {
		x = newpos + area->x;
		y = ((int)(area->height) + area->y) - newpos;
	} else if (get_flags (aswbox->flags, ASA_ReverseOrderH)) {
		x = ((int)(area->width) + area->x) - newpos;
		y = newpos + area->y;
	} else {
		x = newpos + area->x;
		y = newpos + area->y;
	}

	if (x + asw->status->width > area->x + area->width)
		x = (area->x + area->width - asw->status->width);
	if (y + asw->status->height > area->y + area->height)
		y = (area->y + area->height - asw->status->height);

	asw->status->x = x - asw->status->viewport_x;
	asw->status->y = y - asw->status->viewport_y;

	aswbox->cascade_pos = newpos;

	apply_placement_result_asw (asw, XValue | YValue, x, y, 0, 0);

	return True;
}


static Bool do_manual_placement (ASWindow * asw, ASWindowBox * aswbox,
																 ASGeometry * area)
{
	ASMoveResizeData *mvrdata;
	int start_x = 0, start_y = 0;

	ConfigureNotifyLoop ();

	ASQueryPointerRootXY (&start_x, &start_y);
	move_canvas (asw->frame_canvas, start_x - 2, start_y - 2);
	handle_canvas_config (asw->frame_canvas);

/*    moveresize_canvas( asw->frame_canvas, ((int)Scr.MyDisplayWidth - (int)asw->status->width)/2,
										  ((int)Scr.MyDisplayHeight - (int)asw->status->height)/2,
										   asw->status->width, asw->status->height );
	moveresize_canvas( asw->client_canvas, 0, 0, asw->status->width, asw->status->height );
	handle_canvas_config( asw->frame_canvas );
 */
	if (asw->status->width * asw->status->height <
			(Scr.Feel.OpaqueMove * Scr.MyDisplayWidth * Scr.MyDisplayHeight) /
			100) {
		LOCAL_DEBUG_OUT ("Mapping client window %lX", asw->client_canvas->w);
		map_canvas_window (asw->client_canvas, True);
		map_canvas_window (asw->frame_canvas, False);
		if (get_desktop_cover_window () != None) {
			Window w[2];
			w[0] = get_desktop_cover_window ();
			w[1] = asw->frame;
			XRaiseWindow (dpy, w[0]);
			XRestackWindows (dpy, w, 2);
			ASSync (False);
		}
	}
	ASSync (False);
	ASWIN_SET_FLAGS (asw, AS_MoveresizeInProgress);
	mvrdata = move_widget_interactively (Scr.RootCanvas,
																			 asw->frame_canvas,
																			 NULL,
																			 apply_aswindow_move,
																			 complete_aswindow_move);
	if (mvrdata) {
		setup_aswindow_moveresize (asw, mvrdata);
		InteractiveMoveLoop ();
	} else
		ASWIN_CLEAR_FLAGS (asw, AS_MoveresizeInProgress);
	/* window may have been destroyed while we were placing it */
	return (ASWIN_GET_FLAGS (asw, AS_Dead) == 0
					&& !get_flags (asw->wm_state_transition, ASWT_TO_WITHDRAWN));
}

static int
move_to_closest_position (ASWindow * asw, ASGeometry * area,
													ASStatusHints * status, int *x_inout,
													int *y_inout, int max_layer)
{
	int x = *x_inout;
	int y = *y_inout;
	int selected_x = x;
	int selected_y = y;
	int w = status->width;
	int h = status->height;
	int i, selected = -1, selected_factor = 0;;

	LOCAL_DEBUG_OUT ("current=%dx%d%+d%+d", w, h, x, y);
	/* now we have to find the optimal rectangle from the list */
	ASVector *free_space_list =
			build_free_space_list (asw, area, AS_LayerHighest, max_layer);
	XRectangle *rects = PVECTOR_HEAD (XRectangle, free_space_list);
	i = PVECTOR_USED (free_space_list);
	while (--i >= 0)
		if (rects[i].width >= w && rects[i].height >= h) {
			int new_x = rects[i].x, new_y = rects[i].y;
			int max_x = rects[i].x + rects[i].width - w;
			int max_y = rects[i].y + rects[i].height - h;
			int new_factor;
			if (new_x < x)
				new_x = min (x, max_x);
			if (new_y < y)
				new_y = min (y, max_y);
			new_factor = (x - new_x) * (x - new_x) + (y - new_y) * (y - new_y);

			if (selected >= 0 && new_factor > selected_factor)
				continue;
			selected_x = new_x;
			selected_y = new_y;
			selected_factor = new_factor;
			selected = i;
		}
	LOCAL_DEBUG_OUT ("selected: %d, %+d%+d", selected, selected_x,
									 selected_y);
	destroy_asvector (&free_space_list);
	*x_inout = selected_x;
	*y_inout = selected_y;

	return selected;
}


Bool
find_closest_position (ASWindow * asw, ASGeometry * area,
											 ASStatusHints * status, int *closest_x,
											 int *closest_y, int max_layer)
{
	int selected_x = status->x + Scr.Vx;
	int selected_y = status->y + Scr.Vy;
	/* pass 1: find rectangle that is the closest to current position :  */
	int selected =
			move_to_closest_position (asw, area, status, &selected_x,
																&selected_y, max_layer);
	if (selected >= 0) {
		*closest_x = selected_x;
		*closest_y = selected_y;
	}
	return (selected >= 0);
}

static Bool do_pointer_placement (ASWindow * asw, ASWindowBox * aswbox,
																	ASGeometry * area)
{
	int x = area->x, y = area->y;

	ASQueryPointerRootXY (&x, &y);

/*fprintf( stderr, "%d: x = %d, y = %d\n", __LINE__, x, y);*/
	x += Scr.Vx - (int)asw->status->width / 2;
	y += Scr.Vy - (int)asw->status->height / 2;
/*fprintf( stderr, "%d: x = %d, y = %d\n", __LINE__, x, y);*/
	if (x < area->x)
		x = area->x;
	else if (x + asw->status->width > area->x + area->width)
		x = (area->x + area->width - asw->status->width);

	if (y < area->y)
		y = area->y;
	else if (y + asw->status->height > area->y + area->height)
		y = (area->y + area->height - asw->status->height);

	move_to_closest_position (asw, area, asw->status, &x, &y,
														AS_LayerHighest);

/*fprintf( stderr, "%d: x = %d, y = %d\n", __LINE__, x, y);*/
	asw->status->x = x - asw->status->viewport_x;
	asw->status->y = y - asw->status->viewport_y;

	apply_placement_result_asw (asw, XValue | YValue, x, y, 0, 0);

	return True;
}

Bool do_closest_placement (ASWindow * asw, ASWindowBox * aswbox,
																	ASGeometry * area)
{
	int selected_x = 0;
	int selected_y = 0;

	if (find_closest_position
			(asw, area, asw->status, &selected_x, &selected_y,
			 AS_LayerHighest)) {
		apply_placement_result_asw (asw, XValue | YValue, selected_x,
																selected_y, 0, 0);
		LOCAL_DEBUG_OUT ("success: status(%+d%+d), anchor(%+d,%+d)",
										 asw->status->x, asw->status->y, asw->anchor.x,
										 asw->anchor.y);
		return True;
	} else {
		LOCAL_DEBUG_OUT ("failed%s", "");
	}

	return False;
}

Bool
place_aswindow_in_windowbox (ASWindow * asw, ASWindowBox * aswbox,
														 ASUsePlacementStrategy which, Bool force)
{
	ASGeometry area;
	Bool res = False;

	if (ASWIN_GET_FLAGS (asw, AS_Dead)
			|| get_flags (asw->wm_state_transition, ASWT_TO_WITHDRAWN))
		return False;

	area = aswbox->area;
	if (!get_flags (aswbox->flags, ASA_Virtual)) {
		area.x += Scr.Vx;						/*asw->status->viewport_x ; */
		area.y += Scr.Vy;						/*asw->status->viewport_y ; */
		if (!force) {
			if (area.x >= Scr.VxMax + Scr.MyDisplayWidth)
				return False;
			if (area.y >= Scr.VyMax + Scr.MyDisplayHeight)
				return False;
		}
		if (area.width <= 0)
			area.width = (Scr.VxMax + Scr.MyDisplayWidth) - area.x;
		else if (area.x + area.width > Scr.VxMax + Scr.MyDisplayWidth)
			area.width = Scr.VxMax + Scr.MyDisplayWidth - area.x;
		if (area.height <= 0)
			area.height = (Scr.VyMax + Scr.MyDisplayHeight) - area.y;
		else if (area.y + area.height > Scr.VyMax + Scr.MyDisplayHeight)
			area.height = Scr.VyMax + Scr.MyDisplayHeight - area.y;
	}
	LOCAL_DEBUG_OUT ("placement area is %dx%d%+d%+d", area.width,
									 area.height, area.x, area.y);

	if (!force) {
		if (get_flags (asw->status->flags, AS_StartViewportX))
			if (asw->status->viewport_x < area.x
					|| asw->status->viewport_x >= area.x + area.width)
				return False;
		if (get_flags (asw->status->flags, AS_StartViewportY))
			if (asw->status->viewport_y < area.y
					|| asw->status->viewport_y >= area.y + area.height)
				return False;
	}

	if (ASWIN_GET_FLAGS (asw, AS_MaximizedX | AS_MaximizedY))
		return do_maximized_placement (asw, aswbox, &area);

	if (which == ASP_UseMainStrategy) {
		if (aswbox->main_strategy == ASP_SmartPlacement)
			return do_smart_placement (asw, aswbox, &area);
		else if (aswbox->main_strategy == ASP_RandomPlacement)
			return do_random_placement (asw, aswbox, &area, True);
		else if (aswbox->main_strategy == ASP_Tile)
			return do_tile_placement (asw, aswbox, &area);
		else if (aswbox->main_strategy == ASP_UnderPointer)
			res = do_pointer_placement (asw, aswbox, &area);
		if (force)
			do_tile_placement (asw, aswbox, &area);
	} else {
		if (aswbox->backup_strategy == ASP_RandomPlacement)
			res = do_random_placement (asw, aswbox, &area, False);
		else if (aswbox->backup_strategy == ASP_Cascade)
			res = do_cascade_placement (asw, aswbox, &area);
		else if (aswbox->backup_strategy == ASP_UnderPointer)
			res = do_pointer_placement (asw, aswbox, &area);

		if (aswbox->backup_strategy == ASP_Manual || (force && !res))
			res = do_manual_placement (asw, aswbox, &area);
	}
	return res;
}

