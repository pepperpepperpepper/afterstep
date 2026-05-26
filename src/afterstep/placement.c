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


typedef struct ASAvoidCoverAuxData {
	ASWindow *new_aswin;
	ASWindowBox *aswbox;
	ASGeometry *area;
} ASAvoidCoverAuxData;

/* Placement strategy implementations live in placement_strategies.c. */

Bool place_aswindow (ASWindow * asw)
{
	/* if window has predefined named windowbox for it - we use only this windowbox
	 * otherwise we use all suitable windowboxes in two passes :
	 *   we first try and apply main strategy to place window in the empty space for each box
	 *   if all fails we apply backup strategy of the default windowbox
	 */
	ASWindowBox *aswbox = NULL;

	LOCAL_DEBUG_CALLER_OUT ("%p", asw);
	if (AS_ASSERT (asw))
		return False;

	LOCAL_DEBUG_OUT ("hints(%p),status(%p)", asw->hints, asw->status);
	if (AS_ASSERT (asw->hints) || AS_ASSERT (asw->status))
		return False;

	LOCAL_DEBUG_OUT ("status->geom(%dx%d%+d%+d), anchor->geom(%dx%d%+d%+d)",
									 asw->status->width, asw->status->height, asw->status->x,
									 asw->status->y, asw->anchor.width, asw->anchor.height,
									 asw->anchor.x, asw->anchor.y);

	if (asw->hints->windowbox_name) {
		aswbox = find_window_box (&(Scr.Feel), asw->hints->windowbox_name);
		if (aswbox != NULL) {
			if (!place_aswindow_in_windowbox
					(asw, aswbox, ASP_UseMainStrategy, False))
				return place_aswindow_in_windowbox (asw, aswbox,
																						ASP_UseBackupStrategy, True);
			return True;
		}
	}

	/* search for a window-box if none was specified. */
	if (aswbox == NULL) {
		int i, t;
		ASWindowBox **aswbox_sorted =
				safemalloc (sizeof (ASWindowBox *) * Scr.Feel.window_boxes_num);
		aswbox = &(Scr.Feel.window_boxes[0]);

		/* the following code will make sure window-boxes which have the
		   virtual-flag set and are not on the current viewport will
		   be considered for placement after all others. */
		t = Scr.Feel.window_boxes_num;
		for (i = 0; i < Scr.Feel.window_boxes_num; ++i) {
			if (get_flags (aswbox[i].flags, ASA_Virtual)
					&& ((asw->status->viewport_x / Scr.MyDisplayWidth
							 != aswbox[i].area.x / Scr.MyDisplayWidth) ||
							(asw->status->viewport_y / Scr.MyDisplayHeight
							 != aswbox[i].area.y / Scr.MyDisplayHeight))
					)
				/* place window-box at the end */
				aswbox_sorted[--t] = &aswbox[i];
			else
				/* place window-box at the front */
				aswbox_sorted[i - Scr.Feel.window_boxes_num + t] = &aswbox[i];
		}

		for (i = 0; i < Scr.Feel.window_boxes_num; ++i) {
			LOCAL_DEBUG_OUT
					("window_box \"%s\": main_strategy = %d, backup_strategy = %d",
					 aswbox_sorted[i]->name, aswbox_sorted[i]->main_strategy,
					 aswbox_sorted[i]->backup_strategy);

			if (IsValidDesk (aswbox_sorted[i]->desk)
					&& aswbox_sorted[i]->desk != asw->status->desktop)
				continue;
			if (aswbox_sorted[i]->min_layer > asw->status->layer
					|| aswbox_sorted[i]->max_layer < asw->status->layer)
				continue;
			if (aswbox_sorted[i]->min_width > asw->status->width
					|| (aswbox_sorted[i]->max_width > 0
							&& aswbox_sorted[i]->max_width < asw->status->width))
				continue;
			if (aswbox_sorted[i]->min_height > asw->status->height
					|| (aswbox_sorted[i]->max_height > 0
							&& aswbox_sorted[i]->max_height < asw->status->height))
				continue;


			if (ASWIN_GET_FLAGS (asw, AS_MaximizedX | AS_MaximizedY)) {
				int win_x =
						get_flags (aswbox_sorted[i]->flags,
											 ASA_Virtual) ? asw->status->viewport_x : asw->
						status->x;
				int win_y =
						get_flags (aswbox_sorted[i]->flags,
											 ASA_Virtual) ? asw->status->viewport_y : asw->
						status->y;


				if (aswbox_sorted[i]->area.x > win_x + (int)(asw->status->width) ||
						aswbox_sorted[i]->area.y > win_y + (int)(asw->status->height)
						|| aswbox_sorted[i]->area.x + (int)aswbox[i].area.width < win_x
						|| aswbox_sorted[i]->area.y + (int)aswbox[i].area.height <
						win_y)
					continue;
			}

			if (ASWIN_GET_FLAGS (asw, AS_MaximizedX | AS_MaximizedY)) {
				Bool retval =
						place_aswindow_in_windowbox (asw, aswbox_sorted[i],
																				 ASP_UseBackupStrategy, True);
				free (aswbox_sorted);
				return retval;
			} else
					if (place_aswindow_in_windowbox
							(asw, aswbox_sorted[i], ASP_UseMainStrategy, False)) {
				free (aswbox_sorted);
				return True;
			}
		}
		free (aswbox_sorted);
	}
	return place_aswindow_in_windowbox (asw, Scr.Feel.default_window_box,
																			ASP_UseBackupStrategy, True);
}

Bool avoid_covering_aswin_iter_func (void *data, void *aux_data)
{
	ASWindow *asw = (ASWindow *) data;
	ASAvoidCoverAuxData *ac_aux_data = (ASAvoidCoverAuxData *) aux_data;
	ASWindow *new_aswin = ac_aux_data->new_aswin;
	ASWindowBox *aswbox = ac_aux_data->aswbox;
	ASGeometry *area = ac_aux_data->area;

	if (asw && ASWIN_DESK (asw) == ASWIN_DESK (new_aswin)
			&& asw != new_aswin) {
		ASStatusHints *n = new_aswin->status;
		ASStatusHints *o = asw->status;
		int dn, ds, dw, de, min_dh;

		LOCAL_DEBUG_OUT ("comparing to %dx%d%+d%+d, layer = %d",
										 asw->status->width, asw->status->height,
										 asw->status->x, asw->status->y, ASWIN_LAYER (asw));

		/* we want to move out even lower layer windows so they would not be overlapped by us */
		if ( /*ASWIN_LAYER(asw) < ASWIN_LAYER(new_aswin) || */
				ASWIN_GET_FLAGS (asw, AS_Iconic))
			return True;

		dw = o->x + (int)o->width - n->x;
		de = (int)(n->x + n->width) - o->x;
		dn = o->y + (int)o->height - n->y;
		ds = (int)(n->y + n->height) - o->y;
		LOCAL_DEBUG_OUT ("deltas : w=%d e=%d s=%d n=%d", dw, de, dn, ds);
		if (dw > 0 && de > 0 && dn > 0 && ds > 0) {
			ASGeometry clip_area = *area;

			min_dh = min (dw, de);
			if (min_dh < dn && min_dh < ds) {
				if (dw <= de) {					/* better move window westwards */
					clip_area.width = (n->x <= clip_area.x) ? 1 : n->x - clip_area.x;
				} else {								/* better move window eastwards */

					int d = n->x + (int)n->width - clip_area.x;
					clip_area.x = n->x + n->width;
					clip_area.width =
							(d >= clip_area.width) ? 1 : clip_area.width - d;
				}
			} else if (dn <= ds)			/* better move window southwards */
				clip_area.height = (n->y <= clip_area.y) ? 1 : n->y - clip_area.y;
			else {
				int d = n->y + (int)n->height - clip_area.y;
				clip_area.y = n->y + n->height;
				clip_area.height =
						(d >= clip_area.height) ? 1 : clip_area.height - d;
			}
			LOCAL_DEBUG_OUT ("adjusted area is %dx%d%+d%+d", clip_area.width,
											 clip_area.height, clip_area.x, clip_area.y);
			/* move only affected windows : */
			if (do_closest_placement (asw, aswbox, &clip_area)) {
				anchor2status (asw->status, asw->hints, &(asw->anchor));
				/* now lets actually resize the window : */
				apply_window_status_size (asw, get_orientation_data (asw));
			}
		}
	}
	return True;
}

void do_enforce_avoid_cover (ASWindow * asw)
{
	if (asw
			&& ASWIN_HFLAGS (asw,
											 AS_AvoidCover | AS_ShortLived) == AS_AvoidCover) {
		ASWindowBox aswbox;
		ASAvoidCoverAuxData aux_data;
		/* we need to move all the res of the window out of the area occupied by us */
		LOCAL_DEBUG_OUT ("status = %dx%d%+d%+d, layer = %d",
										 asw->status->width, asw->status->height,
										 asw->status->x, asw->status->y, ASWIN_LAYER (asw));
		/* if window has predefined named windowbox for it - we use only this windowbox
		 * otherwise we use all suitable windowboxes in two passes :
		 *   we first try and apply main strategy to place window in the empty space for each box
		 *   if all fails we apply backup strategy of the default windowbox
		 */
		aux_data.new_aswin = asw;
		aux_data.aswbox = &aswbox;
		aswbox.name = mystrdup ("default");
		aswbox.area.x = Scr.Vx;
		aswbox.area.y = Scr.Vy;
		aswbox.area.width = Scr.MyDisplayWidth;
		aswbox.area.height = Scr.MyDisplayHeight;
		aswbox.main_strategy = ASP_Manual;
		aswbox.backup_strategy = ASP_Manual;
		/* we should enforce this one : */
		aswbox.desk = INVALID_DESK;
		aswbox.min_layer = AS_LayerLowest;
		aswbox.max_layer = AS_LayerHighest;

		aux_data.area = &(aswbox.area);

		iterate_asbidirlist (Scr.Windows->clients,
												 avoid_covering_aswin_iter_func, (void *)&aux_data,
												 NULL, False);

		free (aswbox.name);

	}
}

void delayed_enforce_avoid_cover (void *vdata)
{
	ASWindow *asw = (ASWindow *) vdata;
	if (asw && asw->magic == MAGIC_ASWINDOW)
		do_enforce_avoid_cover (asw);
}

void enforce_avoid_cover (ASWindow * asw)
{
	/* we do not want to enforce avoid cover right away - clients may want to reposition 
	   themselves automatically */

	if (asw)
		if (ASWIN_HFLAGS (asw, AS_AvoidCover | AS_ShortLived) == AS_AvoidCover) {
/* we don't want to remove by  data as there are other functions associated wit this
*			while( timer_remove_by_data( (void*)asw ) ); */
			timer_new (500, delayed_enforce_avoid_cover, (void *)asw);
		}
}


void obey_avoid_cover (ASWindow * asw, ASStatusHints * tmp_status,
											 XRectangle * tmp_anchor, int max_layer)
{
	if (asw) {
		ASWindowBox aswbox;
		int selected_x = 0;
		int selected_y = 0;
		int left = Scr.Vx, right = Scr.Vx + Scr.MyDisplayWidth;
		int top = Scr.Vy, bottom = Scr.Vy + Scr.MyDisplayHeight;

		/* we need to move all the res of the window out of the area occupied by us */
		LOCAL_DEBUG_OUT ("status = %dx%d%+d%+d, layer = %d",
										 asw->status->width, asw->status->height,
										 asw->status->x, asw->status->y, ASWIN_LAYER (asw));
		/* if window has predefined named windowbox for it - we use only this windowbox
		 * otherwise we use all suitable windowboxes in two passes :
		 *   we first try and apply main strategy to place window in the empty space for each box
		 *   if all fails we apply backup strategy of the default windowbox
		 */
		LOCAL_DEBUG_OUT ("max_layer = %d", max_layer);

		aswbox.name = mystrdup ("default");
		if (!ASWIN_GET_FLAGS (asw, AS_Sticky)) {
			if (asw->status->x < 0)
				left = 0;
			if (asw->status->x + (int)asw->status->width >= Scr.MyDisplayWidth)
				right = Scr.VxMax + Scr.MyDisplayWidth;
			if (asw->status->y < 0)
				top = 0;
			if (asw->status->y + (int)asw->status->height >= Scr.MyDisplayHeight)
				bottom = Scr.VyMax + Scr.MyDisplayHeight;
		}
#if 0
		aswbox.area.x = Scr.Vx;
		aswbox.area.y = Scr.Vy;
		aswbox.area.width = Scr.MyDisplayWidth;
		aswbox.area.height = Scr.MyDisplayHeight;
#else
		aswbox.area.x = left;
		aswbox.area.y = top;
		aswbox.area.width = right - left;
		aswbox.area.height = bottom - top;
#endif
		aswbox.main_strategy = ASP_Manual;
		aswbox.backup_strategy = ASP_Manual;
		/* we should enforce this one : */
		aswbox.desk = INVALID_DESK;
		aswbox.min_layer = AS_LayerLowest;
		aswbox.max_layer = AS_LayerHighest;

		if (find_closest_position
				(asw, &(aswbox.area), tmp_status, &selected_x, &selected_y,
				 max_layer))
			apply_placement_result (tmp_status, tmp_anchor, asw->hints,
															XValue | YValue, selected_x, selected_y, 0,
															0);

		free (aswbox.name);
	}
}

/**************************************************************************
 * ************************************************************************
 * ************************************************************************
 **************************************************************************/
#if 0
/*
 * pass 0: do not ignore windows behind the target window's layer
 * pass 1: ignore windows behind the target window's layer
 */
int
SmartPlacement (ASWindow * t, int *x, int *y, int width, int height,
								int rx, int ry, int rw, int rh, int pass)
{
	int test_x = 0, test_y;
	int loc_ok = 0;
	ASWindow *twin;
	int xb = rx, xmax = rx + rw - width, xs = 1;
	int yb = ry, ymax = ry + rh - height, ys = 1;

	if (rw < width || rh < height)
		return loc_ok;

	/* if closer to the right edge than the left, scan from right to left */
	if (Scr.MyDisplayWidth - (rx + rw) < rx) {
		xb = rx + rw - width;
		xs = -1;
	}

	/* if closer to the bottom edge than the top, scan from bottom to top */
	if (Scr.MyDisplayHeight - (ry + rh) < ry) {
		yb = ry + rh - height;
		ys = -1;
	}

	for (test_y = yb; ry <= test_y && test_y <= ymax && !loc_ok;
			 test_y += ys)
		for (test_x = xb; rx <= test_x && test_x <= xmax && !loc_ok;
				 test_x += xs) {
			int tx, ty, tw, th;

			loc_ok = 1;

			for (twin = Scr.ASRoot.next; twin != NULL && loc_ok;
					 twin = twin->next) {
				/* ignore windows on other desks, and our own window */
				if (ASWIN_DESK (twin) != ASWIN_DESK (t) || twin == t)
					continue;

				/* ignore non-iconified windows, if we're iconified and not using
				 * StubbornIconPlacement */
				if (!(twin->flags & ICONIFIED) && (t->flags & ICONIFIED) &&
						!(Scr.flags & StubbornIconPlacement))
					continue;

				/* ignore iconified windows, if we're not iconified and not using
				 * StubbornPlacement */
				if ((twin->flags & ICONIFIED) && !(t->flags & ICONIFIED) &&
						!(Scr.flags & StubbornPlacement))
					continue;

				/* ignore a window on a lower layer, unless it's an AvoidCover
				 * window or instructed to pay attention to it (ie, pass == 0) */
				if (!(twin->flags & ICONIFIED)
						&& ASWIN_LAYER (twin) < ASWIN_LAYER (t)
						&& !ASWIN_HFLAGS (twin, AS_AvoidCover) && pass)
					continue;

				get_window_geometry (twin, twin->flags, &tx, &ty, &tw, &th);
				tw += 2 * twin->bw;
				th += 2 * twin->bw;
				if (tx <= test_x + width && tx + tw >= test_x &&
						ty <= test_y + height && ty + th >= test_y) {
					loc_ok = 0;
					if (xs > 0)
						test_x = tx + tw;
					else
						test_x = tx - width;
				}
			}
		}
	if (loc_ok) {
		*x = test_x - xs;
		*y = test_y - ys;
	}
	return loc_ok;
}

/**************************************************************************
 *
 * Handles initial placement and sizing of a new window
 * Returns False in the event of a lost window.
 *
 **************************************************************************/
Bool PlaceWindow (ASWindow * tmp_win, unsigned long tflag, int Desk)
{
	int xl = -1, yt = -1, DragWidth, DragHeight;
	extern Bool PPosOverride;
	XRectangle srect = { 0, 0, Scr.MyDisplayWidth, Scr.MyDisplayHeight };
	int x, y;
	unsigned int width, height;

	y = tmp_win->attr.y;
	x = tmp_win->attr.x;
	width = tmp_win->frame_width;
	height = tmp_win->frame_height;

#if defined(HAVE_XINERAMA) || defined(HAVE_XRANDR)
	if (Scr.xinerama_screens_num > 1) {
		register int i;
		XRectangle *s = Scr.xinerama_screens;

		for (i = 0; i < Scr.xinerama_screens_num; ++i) {
			/* if window is completely in this xinerama-screen */
			if (s[i].x < x + width && s[i].x + s[i].width > x &&
					s[i].y < y + height && s[i].y + s[i].height > y) {
				srect = s[i];
				break;
			}
		}
	}
#endif													/* XINERAMA || XRANDR */


	tmp_win->Desk = InvestigateWindowDesk (tmp_win);

	/* I think it would be good to switch to the selected desk
	 * whenever a new window pops up, except during initialization */
	if (!PPosOverride && Scr.CurrentDesk != tmp_win->Desk)
		changeDesks (0, tmp_win->Desk);

	/* Desk has been selected, now pick a location for the window */
	/*
	 *  If
	 *     o  the window is a transient, or
	 *
	 *     o  a USPosition was requested, or
	 *
	 *     o  Prepos flag was given
	 *
	 *   then put the window where requested.
	 *
	 *   If RandomPlacement was specified,
	 *       then place the window in a psuedo-random location
	 */
	if (!ASWIN_HFLAGS (tmp_win, AS_Transient) &&
			!(tmp_win->normal_hints.flags & USPosition) &&
			((Scr.flags & NoPPosition)
			 || !(tmp_win->normal_hints.flags & PPosition)) && !(PPosOverride)
			&& !(tflag & PREPOS_FLAG) && !((tmp_win->wmhints)
																		 && (tmp_win->wmhints->
																				 flags & StateHint)
																		 && (tmp_win->wmhints->initial_state ==
																				 IconicState))) {
		/* Get user's window placement, unless RandomPlacement is specified */
		if (Scr.flags & SMART_PLACEMENT) {
			if (!SmartPlacement (tmp_win, &xl, &yt,
													 tmp_win->frame_width + 2 * tmp_win->bw,
													 tmp_win->frame_height + 2 * tmp_win->bw,
													 srect.x, srect.y, srect.width, srect.height, 0))
				SmartPlacement (tmp_win, &xl, &yt,
												tmp_win->frame_width + 2 * tmp_win->bw,
												tmp_win->frame_height + 2 * tmp_win->bw,
												srect.x, srect.y, srect.width, srect.height, 1);
		}
		if (Scr.flags & RandomPlacement) {
			if (xl < 0) {
				/* place window in a random location */
				if (tmp_win->flags & VERTICAL_TITLE) {
					Scr.randomx += 2 * tmp_win->title_width;
					Scr.randomy += tmp_win->title_width;
				} else {
					Scr.randomx += tmp_win->title_height;
					Scr.randomy += 2 * tmp_win->title_height;
				}
				if (Scr.randomx > srect.x + (srect.width / 2))
					Scr.randomx = srect.x;
				if (Scr.randomy > srect.y + (srect.height / 2))
					Scr.randomy = srect.y;
				xl = Scr.randomx - tmp_win->old_bw;
				yt = Scr.randomy - tmp_win->old_bw;
			}

			if (xl + tmp_win->frame_width + 2 * tmp_win->bw > srect.width) {
				xl = srect.width - tmp_win->frame_width - 2 * tmp_win->bw;
				Scr.randomx = srect.x;
			}
			if (yt + tmp_win->frame_height + 2 * tmp_win->bw > srect.height) {
				yt = srect.height - tmp_win->frame_height - 2 * tmp_win->bw;
				Scr.randomy = srect.y;
			}
		}
		if (xl < 0) {
			if (GrabEm (POSITION)) {
				/* Grabbed the pointer - continue */
				grab_server ();
				DragWidth = tmp_win->frame_width + 2 * tmp_win->bw;
				DragHeight = tmp_win->frame_height + 2 * tmp_win->bw;
				XMapRaised (dpy, Scr.SizeWindow);
				moveLoop (tmp_win, 0, 0, DragWidth, DragHeight, &xl, &yt, False,
									True);
				XUnmapWindow (dpy, Scr.SizeWindow);
				ungrab_server ();
				UngrabEm ();
			} else {
				/* couldn't grab the pointer - better do something */
				XBell (dpy, Scr.screen);
				xl = 0;
				yt = 0;
			}
		}
		tmp_win->attr.y = yt;
		tmp_win->attr.x = xl;
	} else {
		/* the USPosition was specified, or the window is a transient,
		 * or it starts iconic so let it place itself */
		if (!(tmp_win->normal_hints.flags & USPosition)) {
			if (width <= srect.width) {
				if (x < srect.x)
					x = srect.x;
				else if (x + width > srect.x + srect.width)
					x = srect.x + srect.width - width;
			}
			if (height <= srect.height) {
				if (y < srect.y)
					y = srect.y;
				else if (y + height > srect.y + srect.height)
					y = srect.y + srect.height - height;
			}
			tmp_win->attr.y = y;
			tmp_win->attr.x = x;
		}
	}
	aswindow_set_desk_property (tmp_win, tmp_win->Desk);
	return True;
}


#endif
