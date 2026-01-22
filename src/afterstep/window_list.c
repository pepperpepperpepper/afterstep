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

Bool on_dead_aswindow (Window w);
/********************************************************************************/
/* window list management */

void auto_destroy_aswindow (void *data)
{
	if (data && ((ASMagic *) data)->magic == MAGIC_ASWINDOW) {
		ASWindow *asw = (ASWindow *) data;
		Destroy (asw, False);
	}
}

void destroy_aslayer (ASHashableValue value, void *data);
void destroy_window_group (ASHashableValue value, void *data);

ASWindowList *init_aswindow_list ()
{
	ASWindowList *list;

	list = safecalloc (1, sizeof (ASWindowList));
	list->clients = create_asbidirlist (auto_destroy_aswindow);
	list->aswindow_xref = create_ashash (0, NULL, NULL, NULL);
	list->layers =
			create_ashash (7, NULL, desc_long_compare_func, destroy_aslayer);
	list->bookmarks =
			create_ashash (7, string_hash_value, string_compare,
										 string_destroy_without_data);

	list->window_groups =
			create_ashash (0, NULL, NULL, destroy_window_group);

	list->circulate_list = create_asvector (sizeof (ASWindow *));
	list->sticky_list = create_asvector (sizeof (ASWindow *));

	list->stacking_order = create_asvector (sizeof (ASWindow *));

	Scr.on_dead_window = on_dead_aswindow;

	return list;
}

void destroy_aswindow_list (ASWindowList ** list, Bool restore_root)
{
	if (list)
		if (*list) {
			if (restore_root)
				InstallRootColormap ();

			destroy_asbidirlist (&((*list)->clients));
			destroy_ashash (&((*list)->aswindow_xref));
			destroy_ashash (&((*list)->layers));
			destroy_ashash (&((*list)->bookmarks));
			destroy_ashash (&((*list)->window_groups));
			destroy_asvector (&((*list)->sticky_list));
			destroy_asvector (&((*list)->circulate_list));
			destroy_asvector (&((*list)->stacking_order));

			free (*list);
			*list = NULL;
		}
}

/*************************************************************************
 * We maintain crossreference of X Window ID to ASWindow structure - that is
 * faster then using XContext since we don't have to worry about multiprocessing,
 * thus saving time on interprocess synchronization, that Xlib has to do in
 * order to access list of window contexts.
 *************************************************************************/
ASWindow *window2ASWindow (Window w)
{
	ASHashData hdata = { 0 };
	if (Scr.Windows->aswindow_xref)
		if (get_hash_item
				(Scr.Windows->aswindow_xref, AS_HASHABLE (w),
				 &hdata.vptr) != ASH_Success)
			hdata.vptr = NULL;
	return hdata.vptr;
}

ASWindowGroup *window2ASWindowGroup (Window w)
{
	ASHashData hdata = { 0 };
	if (Scr.Windows->window_groups)
		if (get_hash_item
				(Scr.Windows->window_groups, AS_HASHABLE (w),
				 &hdata.vptr) != ASH_Success)
			hdata.vptr = NULL;
	return hdata.vptr;
}

Bool register_aswindow (Window w, ASWindow * asw)
{
	if (w && asw)
		return (add_hash_item
						(Scr.Windows->aswindow_xref, AS_HASHABLE (w),
						 asw) == ASH_Success);
	return False;
}

Bool unregister_aswindow (Window w)
{
	if (w)
		return (remove_hash_item
						(Scr.Windows->aswindow_xref, AS_HASHABLE (w), NULL,
						 False) == ASH_Success);
	return False;
}

Bool destroy_registered_window (Window w)
{
	Bool res = False;
	if (w) {
		res = unregister_aswindow (w);
		XDestroyWindow (dpy, w);
	}
	return res;
}

Bool on_dead_aswindow (Window w)
{
	ASWindow *asw = window2ASWindow (w);
	if (asw) {
		if (w == asw->w && asw->status != NULL) {
			ASWIN_SET_FLAGS (asw, AS_Dead);
			show_progress
					("marking client's window as destroyed for client \"%s\", window 0x%X",
					 ASWIN_NAME (asw), w);
			return True;
		}
	}
	return False;
}

/*******************************************************************************/
/* layer management */

ASLayer *get_aslayer (int layer, ASWindowList * list)
{
	ASLayer *l = NULL;
	if (list && list->layers) {
		ASHashableValue hlayer = AS_HASHABLE (layer);
		ASHashData hdata = { 0 };
		if (get_hash_item (list->layers, hlayer, &hdata.vptr) != ASH_Success) {
			l = safecalloc (1, sizeof (ASLayer));
			if (add_hash_item (list->layers, hlayer, l) == ASH_Success) {
				l->members = create_asvector (sizeof (ASWindow *));
				l->layer = layer;
				LOCAL_DEBUG_OUT ("added new layer %p(%d) to hash", l, layer);
			} else {
				free (l);
				LOCAL_DEBUG_OUT ("failed to add new layer %p(%d) to hash", l,
												 layer);
				l = NULL;
			}
		} else
			l = hdata.vptr;
	}
	return l;
}

void destroy_aslayer (ASHashableValue value, void *data)
{
	if (data) {
		ASLayer *l = data;
		LOCAL_DEBUG_OUT ("destroying layer %p(%d)", l, l->layer);
		destroy_asvector (&(l->members));
		free (data);
	}
}

