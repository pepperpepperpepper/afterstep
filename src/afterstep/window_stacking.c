/*
 * Copyright (c) 2000 Sasha Vasko <sashav@sprintmail.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#define LOCAL_DEBUG

#include "../../configure.h"

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include "asinternals.h"
#include "../../libAfterStep/session.h"
#include "../../libAfterStep/wmprops.h"

static int get_sorted_layers_vector (ASVector ** layers)
{
	if (*layers == NULL)
		*layers = create_asvector (sizeof (ASLayer *));
	if (Scr.Windows->layers->items_num > (*layers)->allocated)
		realloc_vector (*layers, Scr.Windows->layers->items_num);
/* Layers are sorted in descending order  (see init_winlist) */
	return sort_hash_items (Scr.Windows->layers, NULL,
													(void **)VECTOR_HEAD_RAW (**layers), 0);
}

static void stack_transients (ASWindow * asw, ASVector * list)
{
	int tnum = PVECTOR_USED (asw->transients);
	LOCAL_DEBUG_OUT ("Client %lX has %d transients", asw->w, tnum);
	if (tnum > 0) {								/* need to collect all the transients and stick them in front of us in order of creation */
		ASWindow **sublist = PVECTOR_HEAD (ASWindow *, asw->transients);
		int curr;
		for (curr = 0; curr < tnum; ++curr)
			if (!ASWIN_GET_FLAGS (sublist[curr], AS_Dead)) {
				if (vector_find_data (list, &sublist[curr]) >= PVECTOR_USED (list)) {
					LOCAL_DEBUG_OUT
							("Adding transient #%d - %p, w = %lX, frame = %lX", curr,
							 sublist[curr], sublist[curr]->w, sublist[curr]->frame);
/* ATTN: Inserting pointer to a transient into the END of the stacking list */
					vector_insert_elem (list, &sublist[curr], 1, NULL, False);
				}
			}
	}
}

static inline void stack_layer_windows (ASLayer * layer, ASVector * list)
{
	int k;
	ASWindow **members = PVECTOR_HEAD (ASWindow *, layer->members);

	LOCAL_DEBUG_OUT ("layer %d, end_k = %d", layer->layer,
									 PVECTOR_USED (layer->members));
	for (k = 0; k < PVECTOR_USED (layer->members); k++) {
		ASWindow *asw = members[k];
		if (!ASWIN_GET_FLAGS (asw, AS_Dead)) {
			LOCAL_DEBUG_OUT ("Group %p", asw->group);
#if 0														/* TODO do we really need that ??? */
			if (asw->group) {					/* transients for any window in the group go on top of non-transients in the group */
				ASWindow **sublist =
						PVECTOR_HEAD (ASWindow *, asw->group->members);
				int curr = PVECTOR_USED (asw->group->members);
				LOCAL_DEBUG_OUT ("Group members %d", curr);
				if (asw->group_lead->transients
						&& !ASWIN_GET_FLAGS (asw->group_lead, AS_Dead))
					stack_transients (asw->group_lead, list);

/* ATTN: traversing group members in reverse order ????*/
				while (--curr >= 0) {		/* most recent group member should have their transients above others */
					if (!ASWIN_GET_FLAGS (sublist[curr], AS_Dead)
							&& sublist[curr]->transients)
						stack_transients (sublist[curr], list);
				}
			} else
#endif
			if (asw->transients)
				stack_transients (asw, list);

			LOCAL_DEBUG_OUT ("Adding client - %p, w = %lX, frame = %lX", asw,
											 asw->w, asw->frame);
/* ATTN: Inserting pointer to a window into the END of the stacking list */
			vector_insert_elem (list, &asw, 1, NULL, False);

/*fprintf (stderr, "\tStacking order append(%s) : id = 0x%8.8lX, ptr = %p, name = \"%s\", total = %d\n", 				 __FUNCTION__, asw->w, asw, ASWIN_NAME(asw), PVECTOR_USED(list));
*/

		}
	}
}

static ASVector *__as_scratch_layers = NULL;

void free_scratch_layers_vector ()
{
	if (__as_scratch_layers)
		destroy_asvector (&__as_scratch_layers);
}

ASVector *get_scratch_layers_vector ()
{
	if (__as_scratch_layers == NULL)
		__as_scratch_layers = create_asvector (sizeof (ASLayer *));
	else
		flush_vector (__as_scratch_layers);

	return __as_scratch_layers;
}


void update_stacking_order ()
{
	ASVector *layers = get_scratch_layers_vector ();
	unsigned long layers_in, i;
	ASLayer **l;
	ASVector *list = Scr.Windows->stacking_order;

	flush_vector (list);
	if (Scr.Windows->clients->count == 0)
		return;

	if ((layers_in = get_sorted_layers_vector (&layers)) == 0)
		return;

	realloc_vector (list, Scr.Windows->clients->count);
	l = PVECTOR_HEAD (ASLayer *, layers);
	for (i = 0; i < layers_in; i++)
		stack_layer_windows (l[i], list);

/*	fprintf (stderr, "updated Stacking order of %d windows. Clients count = %d, Layers count = %d. \n", PVECTOR_USED(list), Scr.Windows->clients->count, layers_in);
*/

}

static ASWindow **get_stacking_order_list (ASWindowList * list,
																					 int *stack_len_return)
{
	int stack_len = PVECTOR_USED (list->stacking_order);
	if (stack_len == 0) {
		update_stacking_order ();
		stack_len = PVECTOR_USED (list->stacking_order);
	}
	*stack_len_return = stack_len;
/****
{
	int i;
	ASWindow **list = PVECTOR_HEAD(ASWindow*, Scr.Windows->stacking_order);
	fprintf (stderr, "Stacking order of %d windows: \n", stack_len);
	for ( i = 0 ; i < stack_len; ++i)
		fprintf (stderr, "\t%4.4d : id = 0x%8.8lX, ptr = %p, name = \"%s\"\n",
				 i, list[i]->w, list[i], ASWIN_NAME(list[i]));
}
****/
	return PVECTOR_HEAD (ASWindow *, Scr.Windows->stacking_order);
}

static ASVector *__as_scratch_ids = NULL;

void free_scratch_ids_vector ()
{
	if (__as_scratch_ids)
		destroy_asvector (&__as_scratch_ids);
}

ASVector *get_scratch_ids_vector ()
{
	if (__as_scratch_ids == NULL)
		__as_scratch_ids = create_asvector (sizeof (Window));
	else
		flush_vector (__as_scratch_ids);

	if (Scr.Windows->clients->count + 2 > __as_scratch_ids->allocated)
		realloc_vector (__as_scratch_ids, Scr.Windows->clients->count + 2);

	return __as_scratch_ids;
}

void send_stacking_order (int desk)
{
	if (Scr.Windows->clients->count > 0) {
		int i;
		int stack_len = 0;
		ASWindow **stack = get_stacking_order_list (Scr.Windows, &stack_len);
		ASVector *ids = get_scratch_ids_vector ();

		for (i = 0; i < stack_len; ++i)
			vector_insert_elem (ids, &(stack[i]->w), 1, NULL, False);

		SendStackingOrder (-1, M_STACKING_ORDER, desk, ids);
	}
}

void apply_stacking_order (int desk)
{
	if (Scr.Windows->clients->count > 0) {
		int i;
		int stack_len = 0;
		ASWindow **stack = get_stacking_order_list (Scr.Windows, &stack_len);
		ASVector *ids = get_scratch_ids_vector ();
		Window cw = get_desktop_cover_window ();
		int windows_num;

		if (cw != None && !get_flags (AfterStepState, ASS_Shutdown)) {
			LOCAL_DEBUG_OUT ("desktop cover id = 0x%lX", cw);
			vector_insert_elem (ids, &cw, 1, NULL, True);
		}

		for (i = 0; i < stack_len; ++i)
			if (ASWIN_DESK (stack[i]) == Scr.CurrentDesk) {
				/* if window is not on root currently - stacking order fails with BadMatch */
				LOCAL_DEBUG_OUT ("name \"%s\", frame id = 0x%lX",
												 ASWIN_NAME (stack[i]), stack[i]->frame);
				vector_insert_elem (ids, &(stack[i]->frame), 1, NULL, False);
			}

		windows_num = PVECTOR_USED (ids);
		LOCAL_DEBUG_OUT ("Setting stacking order: windows_num = %d, ",
										 windows_num);
		if (windows_num > 0) {
			Window *windows = PVECTOR_HEAD (Window, ids);

			XRaiseWindow (dpy, windows[0]);
			if (windows_num > 1)
				XRestackWindows (dpy, windows, windows_num);
			XSync (dpy, False);
		}
		raise_scren_panframes (ASDefaultScr);
		XRaiseWindow (dpy, Scr.ServiceWin);
	}
}

void publish_aswindow_list (ASWindowList * list, Bool stacking_only)
{
	if (!get_flags (AfterStepState, ASS_Shutdown)) {
		int i;
		int stack_len = 0;
		ASWindow **stack = get_stacking_order_list (Scr.Windows, &stack_len);
		ASVector *ids = get_scratch_ids_vector ();

		/* we maybe called from Destroy, in which case one of the clients may be
		   delisted from main list, while still present in it's owner's transients list
		   which is why we use +1 - This was happening for some clients who'd have
		   recursive transients ( transient of a transient of a transient )
		   Since we added check to unroll that sequence in tie_aswindow - problem had gone away,
		   but lets keep on adding 1 just in case.
		 */
		if (!stacking_only) {
			ASBiDirElem *curr = LIST_START (list->clients);
			while (curr) {
				ASWindow *asw = (ASWindow *) LISTELEM_DATA (curr);
				vector_insert_elem (ids, &(asw->w), 1, NULL, False);
				LIST_GOTO_NEXT (curr);
			}
			LOCAL_DEBUG_OUT
					("Setting Client List property to include %d windows ",
					 PVECTOR_USED (ids));
			set_clients_list (Scr.wmprops, PVECTOR_HEAD (Window, ids),
												PVECTOR_USED (ids));
			flush_vector (ids);
		}

		i = stack_len;
		while (--i >= 0)
			vector_insert_elem (ids, &(stack[i]->w), 1, NULL, False);

		set_stacking_order (Scr.wmprops, PVECTOR_HEAD (Window, ids),
												PVECTOR_USED (ids));
	}
}

void restack_window_list (int desk)
{
	update_stacking_order ();
	send_stacking_order (desk);
	publish_aswindow_list (Scr.Windows, True);
	apply_stacking_order (desk);
}

ASWindow *find_topmost_client (int desk, int root_x, int root_y)
{
	if (Scr.Windows->clients->count > 0) {
		int i;
		int stack_len = 0;
		ASWindow **stack = get_stacking_order_list (Scr.Windows, &stack_len);

		for (i = 0; i < stack_len; ++i) {
			register ASWindow *asw = stack[i];
			if (ASWIN_DESK (asw) == desk && !ASWIN_GET_FLAGS (asw, AS_Dead)) {
				register ASCanvas *fc = asw->frame_canvas;
				if (fc->root_x <= root_x && fc->root_y <= root_y &&
						fc->root_x + fc->width + fc->bw * 2 > root_x &&
						fc->root_y + fc->height + fc->bw * 2 > root_y)
					return asw;
			}
		}
	}
	return NULL;
}



/*
 * we better have our own routine for changing window stacking order,
 * instead of simply passing it to X server, whenever client request
 * such thing, as we know more about window layout then server does
 */
/* From Xlib reference :
If a sibling and a stack_mode are specified, the window is restacked
 as follows:
 Above 		The window is placed just above the sibling.
 Below    	The window is placed just below the sibling.
 TopIf          If any sibling occludes the window, the window is placed
				at the top of the stack.
 BottomIf       If the window occludes any sibling, the window is placed
				at the bottom of the stack.
 Opposite       If any sibling occludes the window, the window is placed
				at the top of the stack. If the window occludes any
				sibling, the window is placed at the bottom of the stack.
*/
#define OCCLUSION_ABOVE		-1
#define OCCLUSION_NONE		 0
#define OCCLUSION_BELOW		 1

/* Checks if rectangle above is at least partially obscuring client below */
inline Bool is_rect_overlapping (ASRectangle * above, ASRectangle * below)
{
	if (above == NULL)
		return False;
	if (below == NULL)
		return True;

	return (above->x < below->x + below->width
					&& above->x + above->width > below->x
					&& above->y < below->y + below->height
					&& above->y + above->height > below->y);
}

inline Bool
is_status_overlapping (ASStatusHints * above, ASStatusHints * below)
{
	if (above == NULL)
		return False;
	if (below == NULL)
		return True;

	return (above->x < below->x + below->width
					&& above->x + above->width > below->x
					&& above->y < below->y + below->height
					&& above->y + above->height > below->y);
}

inline Bool is_canvas_overlapping (ASCanvas * above, ASCanvas * below)
{
	if (above == NULL)
		return False;
	if (below == NULL)
		return True;
	else {
		int below_left = below->root_x;
		int below_right = below_left + (int)below->width + (int)below->bw * 2;
		int above_left = above->root_x;
		int above_right = above_left + (int)above->width + (int)above->bw * 2;
		if (above_left < below_right && above_right > below_left) {
			int below_top = below->root_y;
			int below_bottom =
					below_top + (int)below->height + (int)below->bw * 2;
			int above_top = above->root_y;
			int above_bottom =
					above_top + (int)above->height + (int)above->bw * 2;

			return (above_top < below_bottom && above_bottom > below_top);
		}
	}
	return False;
}

#define IS_OVERLAPPING(a,b)    is_canvas_overlapping(a->frame_canvas,b->frame_canvas)

static inline Bool is_overlapping_b (ASWindow * a, ASWindow * b)
{
	int i;
	ASWindow **sublist;
	if (IS_OVERLAPPING (a, b))
		return True;

	if (b->transients) {
		sublist = PVECTOR_HEAD (ASWindow *, b->transients);
		for (i = 0; i < PVECTOR_USED (b->transients); ++i)
			if (!ASWIN_GET_FLAGS (sublist[i], AS_Dead))
				if (IS_OVERLAPPING (a, sublist[i]))
					return True;
	}
#if 0														/* TODO do we really need that ??? */
	if (b->group_members) {
		sublist = PVECTOR_HEAD (ASWindow *, b->group_members);
		for (i = 0; i < PVECTOR_USED (b->group_members); ++i)
			if (!ASWIN_GET_FLAGS (sublist[i], AS_Dead))
				if (IS_OVERLAPPING (a, sublist[i]))
					return True;
	}
#endif
	return False;
}

static inline Bool is_overlapping (ASWindow * a, ASWindow * b)
{
	int i;
	ASWindow **sublist;
	if (is_overlapping_b (a, b))
		return True;

	if (a->transients) {
		sublist = PVECTOR_HEAD (ASWindow *, a->transients);
		for (i = 0; i < PVECTOR_USED (a->transients); ++i)
			if (!ASWIN_GET_FLAGS (sublist[i], AS_Dead))
				if (is_overlapping_b (sublist[i], b))
					return True;
	}
#if 0														/* TODO do we really need that ??? */
	if (a->group_members) {
		sublist = PVECTOR_HEAD (ASWindow *, a->group_members);
		for (i = 0; i < PVECTOR_USED (a->group_members); ++i)
			if (!ASWIN_GET_FLAGS (sublist[i], AS_Dead))
				if (is_overlapping_b (sublist[i], b))
					return True;
	}
#endif
	return False;
}

Bool is_window_obscured (ASWindow * above, ASWindow * below)
{
	ASLayer *l;
	ASWindow **members;

	if (above != NULL && below != NULL)
		return is_overlapping (above, below);

	if (above == NULL && below != NULL) {	/* checking if window "below" is completely obscured by any of the
																				   windows with the same layer above it in stacking order */
		register int i, end_i;

		l = get_aslayer (ASWIN_LAYER (below), Scr.Windows);
		if (AS_ASSERT (l))
			return False;

		end_i = l->members->used;
		members = VECTOR_HEAD (ASWindow *, *(l->members));
		for (i = 0; i < end_i; i++) {
			register ASWindow *t;
			if ((t = members[i]) == below) {
				return False;
			} else if (ASWIN_DESK (t) == ASWIN_DESK (below)) {
				if (is_overlapping (t, below)) {
					return True;
				}
			}
		}
	} else if (above != NULL) {		/* checking if window "above" is completely obscuring any of the
																   windows with the same layer below it in stacking order,
																   or any of its transients !!! */
		register int i;

		l = get_aslayer (ASWIN_LAYER (above), Scr.Windows);
		if (AS_ASSERT (l))
			return False;
		members = VECTOR_HEAD (ASWindow *, *(l->members));
		for (i = VECTOR_USED (*(l->members)) - 1; i >= 0; i--) {
			register ASWindow *t;
			if ((t = members[i]) == above)
				return False;
			else if (ASWIN_DESK (t) == ASWIN_DESK (above))
				if (is_overlapping (above, t))
					return True;
		}
	}
	return False;
}

void restack_window (ASWindow * t, Window sibling_window, int stack_mode)
{
	ASWindow *sibling = NULL;
	ASLayer *dst_layer = NULL, *src_layer;
	Bool above;
	int occlusion = OCCLUSION_NONE;

	if (t == NULL)
		return;

	if (t->transient_owner != NULL)
		t = t->transient_owner;

	if (ASWIN_GET_FLAGS (t, AS_Dead))
		return;

	LOCAL_DEBUG_CALLER_OUT ("%p,%lX,%d", t, sibling_window, stack_mode);
	src_layer = get_aslayer (ASWIN_LAYER (t), Scr.Windows);

	if (sibling_window)
		if ((sibling = window2ASWindow (sibling_window)) != NULL) {
			if (sibling->transient_owner == t)
				sibling = NULL;					/* can't restack relative to its own transient */
			else if (ASWIN_DESK (sibling) != ASWIN_DESK (t))
				sibling = NULL;					/* can't restack relative to window on the other desk */
			else
				dst_layer = get_aslayer (ASWIN_LAYER (sibling), Scr.Windows);
		}

	if (dst_layer == NULL)
		dst_layer = src_layer;

	/* 2. do all the occlusion checks whithin our layer */
	if (stack_mode == TopIf) {
		LOCAL_DEBUG_OUT ("stack_mode = %s", "TopIf");
		if (is_window_obscured (sibling, t))
			occlusion = OCCLUSION_BELOW;
	} else if (stack_mode == BottomIf) {
		LOCAL_DEBUG_OUT ("stack_mode = %s", "BottomIf");
		if (is_window_obscured (t, sibling))
			occlusion = OCCLUSION_ABOVE;
	} else if (stack_mode == Opposite) {
		if (is_window_obscured (sibling, t)) {
			occlusion = OCCLUSION_BELOW;
			LOCAL_DEBUG_OUT ("stack_mode = opposite, occlusion = %s", "below");
		} else if (is_window_obscured (t, sibling)) {
			occlusion = OCCLUSION_ABOVE;
			LOCAL_DEBUG_OUT ("stack_mode = opposite, occlusion = %s", "above");
		} else {
			LOCAL_DEBUG_OUT ("stack_mode = opposite, occlusion = %s", "none");
		}
	}
	if (sibling)
		if (ASWIN_LAYER (sibling) != ASWIN_LAYER (t))
			occlusion = OCCLUSION_NONE;

	if (!((stack_mode == TopIf && occlusion == OCCLUSION_BELOW) ||
				(stack_mode == BottomIf && occlusion == OCCLUSION_ABOVE) ||
				(stack_mode == Opposite && occlusion != OCCLUSION_NONE) ||
				stack_mode == Above || stack_mode == Below)) {
		return;											/* nothing to be done */
	}

	above = (stack_mode == Above || stack_mode == TopIf ||
					 (stack_mode == Opposite && occlusion == OCCLUSION_BELOW));

	if (stack_mode != Above && stack_mode != Below)
		sibling = NULL;

#if 0														/* TODO do we really need that ??? */
	if (t->group_members) {
		int k;
		int todo = 0;
		ASWindow **members = PVECTOR_HEAD (ASWindow *, src_layer->members);
		ASWindow **group_members =
				safemalloc (PVECTOR_USED (src_layer->members) *
										sizeof (ASWindow *));
		for (k = 0; k < PVECTOR_USED (src_layer->members); k++)
			if (members[k] != t && members[k]->group_lead == t)
				group_members[todo++] = members[k];
		for (k = 0; k < todo; ++k) {
			vector_remove_elem (src_layer->members, &group_members[k]);
			vector_insert_elem (dst_layer->members, &group_members[k], 1,
													sibling, above);
			if (sibling)
				sibling = group_members[k];
		}
		free (group_members);
	}
#endif
	vector_remove_elem (src_layer->members, &t);
	vector_insert_elem (dst_layer->members, &t, 1, sibling, above);

	t->last_restack_time = Scr.last_Timestamp;
	restack_window_list (ASWIN_DESK (t));
}

