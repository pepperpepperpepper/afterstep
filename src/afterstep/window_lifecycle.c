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

#include "asinternals.h"
#include "../../libAfterStep/wmprops.h"

void destroy_window_group (ASHashableValue value, void *data)
{
	if (data) {
		ASWindowGroup *g = data;
		LOCAL_DEBUG_OUT
				("destroying window group %p(%lx), sm_client_id = \"%s\"", g,
				 g->leader, g->sm_client_id ? g->sm_client_id : "none");
		if (g->sm_client_id)
			free (g->sm_client_id);
		destroy_asvector (&(g->members));
		free (data);
	}
}

/********************************************************************************/
/* ASWindow management */

ASWindow *find_group_lead_aswindow (Window id)
{
	ASWindow *gl = window2ASWindow (id);
	if (gl == NULL) {							/* let's find previous window with the same group lead */
		ASBiDirElem *curr = LIST_START (Scr.Windows->clients);
		while (curr) {
			ASWindow *asw = (ASWindow *) LISTELEM_DATA (curr);
			if (asw->hints->group_lead == id) {
				gl = asw;
				break;
			}
			LIST_GOTO_NEXT (curr);
		}
	}
	return gl;
}

static void add_member_to_group (ASWindow * asw)
{
	if (asw && asw->hints->group_lead) {
		ASWindowGroup *g = window2ASWindowGroup (asw->hints->group_lead);
		if (g == NULL) {
			g = safecalloc (1, sizeof (ASWindowGroup));
			g->leader = asw->hints->group_lead;
			g->member_count++;
			if (add_hash_item
					(Scr.Windows->window_groups, AS_HASHABLE (g->leader),
					 g) != ASH_Success) {
				free (g);
				g = NULL;
			}
			read_string_property (g->leader, _XA_SM_CLIENT_ID,
														&(g->sm_client_id));
		}
		if (g != NULL) {
#if 0
			if (group_lead->group_members == NULL)
				group_lead->group_members = create_asvector (sizeof (ASWindow *));
			/* ATTN: Inserting pointer to a member into the beginning of the list */
			vector_insert_elem (group_lead->group_members, &member, 1, NULL,
													True);
#endif
		}
		asw->group = g;
	}
}

void add_aswindow_to_layer (ASWindow * asw, int layer)
{
	if (!AS_ASSERT (asw) && asw->transient_owner == NULL) {
		ASLayer *dst_layer = get_aslayer (layer, Scr.Windows);
		/* inserting window into the top of the new layer */

		LOCAL_DEBUG_OUT ("adding window %p to the top of the layer %p (%d)",
										 asw, dst_layer, layer);
/* ATTN: Inserting pointer to a member into the beginning of the list */
		if (!AS_ASSERT (dst_layer))
			vector_insert_elem (dst_layer->members, &asw, 1, NULL, True);
	}
}

void remove_aswindow_from_layer (ASWindow * asw, int layer)
{
	if (!AS_ASSERT (asw)) {
		ASLayer *src_layer = get_aslayer (layer, Scr.Windows);
		LOCAL_DEBUG_OUT ("removing window %p from layer %p (%d)", asw,
										 src_layer, layer);
		if (!AS_ASSERT (src_layer)) {
			LOCAL_DEBUG_OUT ("can be found at index %d",
											 vector_find_data (src_layer->members, &asw));
			while (vector_find_data (src_layer->members, &asw) <
						 src_layer->members->used) {
				vector_remove_elem (src_layer->members, &asw);
				LOCAL_DEBUG_OUT ("after deletion can be found at index %d(used%d)",
												 vector_find_data (src_layer->members, &asw),
												 src_layer->members->used);
			}
		}
	}
}

void tie_aswindow (ASWindow * t)
{
	if (t->hints->transient_for != None) {
		ASWindow *transient_owner = window2ASWindow (t->hints->transient_for);
		if (transient_owner != NULL) {	/* we want to find the topmost window */
			while (transient_owner->transient_owner != NULL &&
						 transient_owner != transient_owner->transient_owner) {
				if (ASWIN_GET_FLAGS (transient_owner->transient_owner, AS_Dead)) {
					ASWindow *t = transient_owner;
					if (t->transient_owner->transients != NULL)
						vector_remove_elem (t->transient_owner->transients, &t);
					t->transient_owner = NULL;
					add_aswindow_to_layer (t, ASWIN_LAYER (t));
				} else
					transient_owner = transient_owner->transient_owner;
			}

			t->transient_owner = transient_owner;
			if (transient_owner->transients == NULL)
				transient_owner->transients =
						create_asvector (sizeof (ASWindow *));
/* ATTN: Inserting pointer to a transient into the beginning of the list */
			vector_insert_elem (transient_owner->transients, &t, 1, NULL, True);
		}
	}
	add_member_to_group (t);
}

void untie_aswindow (ASWindow * t)
{
	ASWindow **sublist;
	int i;

	if (t->transient_owner != NULL
			&& t->transient_owner->magic == MAGIC_ASWINDOW) {
		if (t->transient_owner != NULL)
			vector_remove_elem (t->transient_owner->transients, &t);
		t->transient_owner = NULL;
	}
	if (t->transients && PVECTOR_USED (t->transients) > 0) {
		sublist = PVECTOR_HEAD (ASWindow *, t->transients);

/* ATTN: this code reverses the order if add_aswindow_to_layer inserts into the beginnig of the list !!! */
		/*for( i = 0 ; i < PVECTOR_USED(t->transients) ; ++i ) */
/* ATTN: this code keeps the order if add_member_to_group_lead inserts into the beginnig of the list !!! */
		i = PVECTOR_USED (t->transients);
		while (--i >= 0)
			if (sublist[i] && sublist[i]->magic == MAGIC_ASWINDOW && sublist[i]->transient_owner == t) {	/* we may need to delete this windows actually */
				sublist[i]->transient_owner = NULL;
				add_aswindow_to_layer (sublist[i], ASWIN_LAYER (sublist[i]));
			}
	}
#if 0														/* Possibly need to remove us from group's list */
	if (t->group_lead && t->group_lead->magic == MAGIC_ASWINDOW) {
		if (t->group_lead->group_members)
			vector_remove_elem (t->group_lead->group_members, &t);
		t->group_lead = NULL;
	}
	if (t->group) {
		ASWindow *new_gl;
		sublist = PVECTOR_HEAD (ASWindow *, t->group_members);
		new_gl = sublist[0];
		new_gl->group_lead = NULL;

/* ATTN: this code reverses the order if add_member_to_group_lead inserts into the beginnig of the list !!! */
		/*for( i = 1 ; i < PVECTOR_USED(t->group_members) ; ++i ) */
/* ATTN: this code keeps the order if add_member_to_group_lead inserts into the beginnig of the list !!! */
		i = PVECTOR_USED (t->group_members);
		while (--i >= 0)
			if (sublist[i] && sublist[i]->magic == MAGIC_ASWINDOW
					&& sublist[i]->group_lead == t) {
				sublist[i]->group_lead = NULL;
				add_member_to_group_lead (new_gl, sublist[i]);
			}
	}
#endif
}

Bool enlist_aswindow (ASWindow * t)
{
	if (Scr.Windows == NULL)
		Scr.Windows = init_aswindow_list ();

	append_bidirelem (Scr.Windows->clients, t);

/* ATTN: Inserting pointer to a member into the beginning of the circulate list */
	vector_insert_elem (Scr.Windows->circulate_list, &t, 1, NULL, True);

	tie_aswindow (t);

/*fprintf (stderr, "Stacking order (%s): ptr = %p, transient_owner = %p, Layer = %d\n",		 __FUNCTION__, t, t->transient_owner, ASWIN_LAYER(t));
*/
	if (t->transient_owner == NULL)
		add_aswindow_to_layer (t, ASWIN_LAYER (t));

	publish_aswindow_list (Scr.Windows, False);

	return True;
}

void delist_aswindow (ASWindow * t)
{
	if (Scr.Windows == NULL)
		return;

	if (AS_ASSERT (t))
		return;
	/* set desktop for window */
	if (t->w != Scr.Root)
		vector_remove_elem (Scr.Windows->circulate_list, &t);

	if (ASWIN_GET_FLAGS (t, AS_Sticky))
		vector_remove_elem (Scr.Windows->sticky_list, &t);

	remove_aswindow_from_layer (t, ASWIN_LAYER (t));

	vector_remove_elem (Scr.Windows->stacking_order, &t);

	untie_aswindow (t);
	discard_bidirelem (Scr.Windows->clients, t);
	publish_aswindow_list (Scr.Windows, False);
}

