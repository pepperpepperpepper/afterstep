/*
 * Copyright (C) 2000 Sasha Vasko <sasha at aftercode.net>
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

#undef LOCAL_DEBUG
#include "../configure.h"
#include "asapp.h"
#include "screen.h"
#include "clientprops.h"
#include "wmprops.h"
#include "wmprops_internal.h"

/* WM-property writers, broadcast/flush and the property-change event
 * dispatcher, split out of wmprops.c. The atom decoders, selection setup
 * and handler/table initialisation stay in wmprops.c; the shared handler
 * hashes, description tables and prop_description_struct type are declared
 * in wmprops_internal.h. */


/***********************************************************************************
 * Hints printing functions :
 ***********************************************************************************/
void print_wmprops (stream_func func, void *stream, ASWMProps * wmprops)
{
	if (!pre_print_check
			(&func, &stream, wmprops,
			 "No Window Management properties available(NULL)."))
		return;
}


/**********************************************************************************/
/***************** Setting property values here  : ********************************/
/**********************************************************************************/
void set_prop_updated (ASHashTable * handlers, Atom prop)
{																/* useless really for now */

/*    prop_description_struct *descr ;
	ASHashableValue  hprop = (ASHashableValue)((unsigned long)prop);
	if( get_hash_item( handlers, hprop, (void**)&descr ) == ASH_Success )
		descr->update_time = ASDefaultScr->last_Timestamp ;
 */
} void set_as_module_socket (ASWMProps * wmprops, char *new_socket)
{
	if (wmprops && new_socket) {
		set_string_property (wmprops->selection_window, _AS_MODULE_SOCKET,
												 new_socket);
		if (wmprops->as_socket_filename)
			free (wmprops->as_socket_filename);
		wmprops->as_socket_filename = mystrdup (new_socket);
	}
}

void
set_as_style (ASWMProps * wmprops, CARD32 size, CARD32 version,
							CARD32 * data)
{
	if (wmprops) {
		if (wmprops->selection_window == None)
			return;
		if (data == NULL || size == 0)
			XDeleteProperty (dpy, wmprops->selection_window, _AS_STYLE);

		else
			set_as_property (wmprops->selection_window, _AS_STYLE, data, size,
											 version);
		if (wmprops->as_styles_data
				&& (size > wmprops->as_styles_size || data == NULL)) {
			free (wmprops->as_styles_data);
			wmprops->as_styles_data = NULL;
		}
		if (data) {
			if (wmprops->as_styles_data == NULL)
				wmprops->as_styles_data = safemalloc (size);
			memcpy (wmprops->as_styles_data, data, size);
		}
		wmprops->as_styles_size = size;
		wmprops->as_styles_version = version;
	}
}

void set_as_background (ASWMProps * wmprops, Pixmap new_pmap)
{
	if (wmprops) {
		if (wmprops->selection_window == None)
			return;
		set_32bit_property (wmprops->selection_window, _AS_BACKGROUND,
												XA_PIXMAP, new_pmap);
		XFlush (dpy);
		wmprops->as_root_pixmap = new_pmap;
	}
}

void set_xrootpmap_id (ASWMProps * wmprops, Pixmap new_pmap)
{
	if (wmprops) {
		CARD32 esetroot_pmap_id = None;
		if (read_32bit_property
				(wmprops->scr->Root, ESETROOT_PMAP_ID, &esetroot_pmap_id))
			if (esetroot_pmap_id == wmprops->root_pixmap
					&& wmprops->root_pixmap != None)
				XKillClient (dpy, esetroot_pmap_id);
		set_32bit_property (wmprops->scr->Root, _XROOTPMAP_ID, XA_PIXMAP,
												new_pmap);
		XFlush (dpy);
		wmprops->root_pixmap = new_pmap;
	}
}

void validate_rootpmap_props (ASWMProps * wmprops)
{
	if (wmprops) {
		if (wmprops->root_pixmap) {
			if (!validate_drawable (wmprops->root_pixmap, NULL, NULL))
				set_xrootpmap_id (wmprops, None);
		}
		if (wmprops->as_root_pixmap) {
			if (!validate_drawable (wmprops->as_root_pixmap, NULL, NULL))
				set_as_background (wmprops, None);
		}
	}
}

CARD32 as_desk2ext_desk (ASWMProps * wmprops, INT32 as_desk)
{
	register CARD32 i;

	if (wmprops->as_desk_numbers) {
		for (i = 0; i < wmprops->as_desk_num; i++)
			if (wmprops->as_desk_numbers[i] >= as_desk) {
				if (wmprops->as_desk_numbers[i] == as_desk)
					return i;
				break;
			}
	}
	return INVALID_DESKTOP_PROP;
}

void
set_desktop_num_prop (ASWMProps * wmprops, INT32 new_desk, Window vroot,
											Bool add)
{
	if (wmprops) {
		register int k;
		int index = as_desk2ext_desk (wmprops, new_desk);

		if (index == INVALID_DESKTOP_PROP && add) {
			register int i;

			for (i = 0; i < wmprops->as_desk_num; i++)
				if (wmprops->as_desk_numbers[i] >= new_desk)
					break;
			wmprops->as_desk_num++;
			wmprops->desktop_num = wmprops->as_desk_num;
			wmprops->as_desk_numbers =
					realloc (wmprops->as_desk_numbers,
									 wmprops->as_desk_num * sizeof (long));
			wmprops->virtual_roots =
					realloc (wmprops->virtual_roots,
									 wmprops->as_desk_num * sizeof (Window));
			k = wmprops->as_desk_num - 1;
			while (k > i && k > 0) {
				wmprops->as_desk_numbers[k] = wmprops->as_desk_numbers[k - 1];
				wmprops->virtual_roots[k] = wmprops->virtual_roots[k - 1];
				--k;
			}
			wmprops->as_desk_numbers[i] = new_desk;
			wmprops->virtual_roots[i] = vroot;
			index = i;
		} else if (index != INVALID_DESKTOP_PROP && !add) {	/* removing old desk */
			wmprops->as_desk_num--;
			wmprops->desktop_num = wmprops->as_desk_num;
			k = index;
			while (++k < wmprops->as_desk_num) {
				wmprops->as_desk_numbers[k - 1] = wmprops->as_desk_numbers[k];
				wmprops->virtual_roots[k - 1] = wmprops->virtual_roots[k];
			}
		} else
			return;										/* nothing to do */
		if (is_output_level_under_threshold (OUTPUT_LEVEL_VROOT))
			fprintf (stderr,
							 "%s: %s desktop with AfterStep number %ld and public number %lu (virtual root 0x%lX)\n",
							 MyName, add ? "added" : "removed", (long)new_desk,
							 (unsigned long)index, (unsigned long)vroot);

		/* need to update crossreference table here : */
		set_32bit_property (wmprops->scr->Root, _XA_NET_NUMBER_OF_DESKTOPS,
												XA_CARDINAL, wmprops->desktop_num);
		set_32bit_property (wmprops->scr->Root, _XA_WIN_WORKSPACE_COUNT,
												XA_CARDINAL, wmprops->desktop_num);
		set_32bit_proplist (wmprops->scr->Root, _AS_DESK_NUMBERS, XA_CARDINAL,
												(CARD32 *) & (wmprops->as_desk_numbers[0]),
												wmprops->as_desk_num);
		set_32bit_proplist (wmprops->scr->Root, _XA_NET_VIRTUAL_ROOTS,
												XA_WINDOW,
												(CARD32 *) & (wmprops->virtual_roots[0]),
												wmprops->desktop_num);
		if (add && !get_flags (wmprops->set_props, WMC_ASDesks)) {
			wmprops->as_current_desk = INVALID_DESKTOP_PROP;
			set_current_desk_prop (wmprops, new_desk);
			set_flags (wmprops->set_props, WMC_ASDesks);
		}
	}
}

Bool set_current_desk_prop (ASWMProps * wmprops, INT32 new_desk)
{
	if (wmprops) {
		CARD32 ext_desk_no;
		if (wmprops->as_current_desk == new_desk)
			return True;
		ext_desk_no = as_desk2ext_desk (wmprops, new_desk);

		/* adding desktops has to be handled in different function, since we need to update
		 * bunch of other things as well - virtual roots etc. */
		if (ext_desk_no != INVALID_DESKTOP_PROP) {
			set_32bit_property (wmprops->scr->Root, _AS_CURRENT_DESK,
													XA_CARDINAL, new_desk);
			wmprops->as_current_desk = new_desk;
			set_32bit_property (wmprops->scr->Root, _XA_NET_CURRENT_DESKTOP,
													XA_CARDINAL, ext_desk_no);
			set_32bit_property (wmprops->scr->Root, _XA_WIN_WORKSPACE,
													XA_CARDINAL, ext_desk_no);
			wmprops->desktop_current = ext_desk_no;
			return True;
		}
		show_error ("Attempt to set current desktop to invalid number %d",
								new_desk);
	}
	return False;
}

Bool set_current_viewport_prop (ASWMProps * wmprops, CARD32 vx,
																CARD32 vy, Bool normal)
{
	if (wmprops) {
		CARD32 viewport[2];
		if (wmprops->as_current_vx == vx && wmprops->as_current_vy == vy)
			return True;

		/* adding desktops has to be handled in different function, since we need to update
		 * bunch of other things as well - virtual roots etc. */
		if (wmprops->desktop_current >= wmprops->desktop_viewports_num) {
			int max_i = (wmprops->desktop_current + 1) * 2;
			int i = wmprops->desktop_viewports_num * 2;

			wmprops->desktop_viewport =
					saferealloc (wmprops->desktop_viewport, max_i * sizeof (CARD32));
			while (i < max_i) {
				wmprops->desktop_viewport[i] = 0;
				++i;
			}
			wmprops->desktop_viewports_num = wmprops->desktop_current + 1;
		}
		if (wmprops->desktop_current < wmprops->desktop_viewports_num) {
			int pos = wmprops->desktop_current << 1;

			wmprops->desktop_viewport[pos] = vx;
			wmprops->desktop_viewport[pos + 1] = vy;
			set_32bit_proplist (wmprops->scr->Root, _XA_NET_DESKTOP_VIEWPORT,
													XA_CARDINAL, &(wmprops->desktop_viewport[0]),
													wmprops->desktop_viewports_num * 2);
		}
		if (normal) {
			wmprops->as_current_vx = viewport[0] = vx;
			wmprops->as_current_vy = viewport[1] = vy;
			set_32bit_proplist (wmprops->scr->Root, _AS_CURRENT_VIEWPORT,
													XA_CARDINAL, &viewport[0], 2);
		}
		return True;
	}
	return False;
}

Bool set_desktop_geometry_prop (ASWMProps * wmprops, CARD32 width,
																CARD32 height)
{
	if (wmprops) {
		CARD32 size[2];
		if (wmprops->desktop_width == width
				&& wmprops->desktop_height == height)
			return True;
		wmprops->desktop_width = size[0] = width;
		wmprops->desktop_height = size[1] = height;
		set_32bit_proplist (wmprops->scr->Root, _XA_NET_DESKTOP_GEOMETRY,
												XA_CARDINAL, &size[0], 2);
		return True;
	}
	return False;
}

void set_service_window_prop (ASWMProps * wmprops, Window service_win)
{
	if (wmprops) {
		if (wmprops->as_service_window == service_win)
			return;
		wmprops->as_service_window = service_win;
		set_32bit_property (wmprops->selection_window, _AS_SERVICE_WINDOW,
												XA_WINDOW, service_win);
	}
}

void flush_wmprop_data (ASWMProps * wmprops, ASFlagType what)
{
	if (wmprops && what) {
		register int i;

		for (i = 0; WMPropsDescriptions_root[i].id_variable != NULL; i++) {
			register prop_description_struct *descr =
					&(WMPropsDescriptions_root[i]);

			if (get_flags (descr->prop_class, what)) {
				XDeleteProperty (dpy, wmprops->scr->Root, *(descr->id_variable));
				if (descr->read_func != NULL)
					descr->read_func (wmprops, True);
				clear_flags (wmprops->set_props, descr->prop_class);
				clear_flags (wmprops->my_props, descr->prop_class);
			}
		}
		if (wmprops->selection_window)
			for (i = 0; WMPropsDescriptions_volitile[i].id_variable != NULL; i++) {
				register prop_description_struct *descr =
						&(WMPropsDescriptions_volitile[i]);

				if (get_flags (descr->prop_class, what)) {
					XDeleteProperty (dpy, wmprops->selection_window,
													 *(descr->id_variable));
					if (descr->read_func != NULL)
						descr->read_func (wmprops, True);
					clear_flags (wmprops->set_props, descr->prop_class);
					clear_flags (wmprops->my_props, descr->prop_class);
				}
			}
		XFlush (dpy);
	}
}

static void realloc_clients_list (ASWMProps * wmprops, int nclients)
{
	if (nclients <= 0) {
		if (wmprops->client_list)
			free (wmprops->client_list);
		if (wmprops->stacking_order)
			free (wmprops->stacking_order);
		wmprops->client_list = NULL;
		wmprops->stacking_order = NULL;
		wmprops->clients_num = 0;
	} else {
		if (wmprops->clients_num < nclients) {
			wmprops->client_list =
					realloc (wmprops->client_list, nclients * sizeof (CARD32));
			wmprops->stacking_order =
					realloc (wmprops->stacking_order, nclients * sizeof (CARD32));
		}
		wmprops->clients_num = nclients;
	}
}

void set_clients_list (ASWMProps * wmprops, Window * list, int nclients)
{
	if (wmprops) {
		realloc_clients_list (wmprops, nclients);
		if (nclients <= 0) {
			XDeleteProperty (dpy, wmprops->scr->Root, _XA_NET_CLIENT_LIST);
			XDeleteProperty (dpy, wmprops->scr->Root, _XA_WIN_CLIENT_LIST);
		} else {
			int i;
			for (i = 0 ; i < nclients ; ++i) wmprops->client_list[i] = list[i];

			set_32bit_proplist (wmprops->scr->Root, _XA_NET_CLIENT_LIST,
													XA_WINDOW, wmprops->client_list, nclients);
			set_32bit_proplist (wmprops->scr->Root, _XA_WIN_CLIENT_LIST,
													XA_CARDINAL, wmprops->client_list, nclients);
		}
		XFlush (dpy);
	}
}

void set_stacking_order (ASWMProps * wmprops, Window * list, int nclients)
{
	if (wmprops) {
		realloc_clients_list (wmprops, nclients);
		if (nclients <= 0)
			XDeleteProperty (dpy, wmprops->scr->Root,
											 _XA_NET_CLIENT_LIST_STACKING);

		else {
			int i;
			for (i = 0 ; i < nclients ; ++i) wmprops->stacking_order[i] = list[i];
			set_32bit_proplist (wmprops->scr->Root,
													_XA_NET_CLIENT_LIST_STACKING, XA_WINDOW,
													wmprops->stacking_order, nclients);
		}
		XFlush (dpy);
	}
}

void set_active_window_prop (ASWMProps * wmprops, Window active)
{
	if (wmprops) {
		if (wmprops->active_window != active) {
			wmprops->active_window = active;
			set_32bit_property (wmprops->scr->Root, _XA_NET_ACTIVE_WINDOW,
													XA_WINDOW, active);
			XFlush (dpy);
		}
	}
}

ASTBarProps *get_astbar_props (ASWMProps * wmprops)
{
	ASTBarProps *tbar_props = NULL;
	if (wmprops && wmprops->as_tbar_props_size > 0
			&& wmprops->as_tbar_props_data != NULL) {
		CARD32 *prop = wmprops->as_tbar_props_data;
		int i, nbuttons;

		tbar_props = safecalloc (1, sizeof (ASTBarProps));
		tbar_props->align = prop[0];
		tbar_props->bevel = prop[1];
		tbar_props->title_h_spacing = prop[2];
		tbar_props->title_v_spacing = prop[3];
		tbar_props->buttons_h_border = prop[4];
		tbar_props->buttons_v_border = prop[5];
		tbar_props->buttons_spacing = prop[6];
		tbar_props->buttons_num = prop[7];
		nbuttons = tbar_props->buttons_num;
		if (nbuttons > 10
				|| (nbuttons * 4 + 8) * sizeof (CARD32) >
				wmprops->as_tbar_props_size) {
			tbar_props->buttons_num = 0;
		} else {
			tbar_props->buttons =
					safemalloc (nbuttons * sizeof (struct ASButtonPropElem));
			for (i = 0; i < nbuttons; ++i) {
				tbar_props->buttons[i].kind = prop[8 + i * 3];
				tbar_props->buttons[i].pmap = prop[8 + i * 3 + 1];
				tbar_props->buttons[i].mask = prop[8 + i * 3 + 2];
				tbar_props->buttons[i].alpha = prop[8 + i * 3 + 3];
			}
		}
	}
	return tbar_props;
}

void set_astbar_props (ASWMProps * wmprops, ASTBarProps * tbar_props)
{
	CARD32 *prop = NULL;
	int size = 0;

	CARD32 version = (1 << 8) + 1;	/* version 1.1 */
	if (wmprops == NULL || wmprops->selection_window == None)
		return;
	if (tbar_props) {
		int i, nbuttons = tbar_props->buttons_num;

		size = sizeof (CARD32) * (nbuttons * 4 + 8);
		prop = safecalloc (1, size);
		prop[0] = tbar_props->align;
		prop[1] = tbar_props->bevel;
		prop[2] = tbar_props->title_h_spacing;
		prop[3] = tbar_props->title_v_spacing;
		prop[4] = tbar_props->buttons_h_border;
		prop[5] = tbar_props->buttons_v_border;
		prop[6] = tbar_props->buttons_spacing;
		prop[7] = tbar_props->buttons_num;
		for (i = 0; i < nbuttons; ++i) {
			prop[8 + i * 3] = tbar_props->buttons[i].kind;
			prop[8 + i * 3 + 1] = tbar_props->buttons[i].pmap;
			prop[8 + i * 3 + 2] = tbar_props->buttons[i].mask;
			prop[8 + i * 3 + 3] = tbar_props->buttons[i].alpha;
		}
	}
	if (prop == NULL || size == 0)
		XDeleteProperty (dpy, wmprops->selection_window, _AS_TBAR_PROPS);

	else
		set_as_property (wmprops->selection_window, _AS_TBAR_PROPS, prop,
										 size, version);
	if (wmprops->as_tbar_props_data)
		free (wmprops->as_tbar_props_data);
	wmprops->as_tbar_props_data = prop;
	wmprops->as_tbar_props_size = size;
	wmprops->as_tbar_props_version = version;
}


/********************************************************************************/
/* We need to handle Property Notify Events : 						            */
/********************************************************************************/
WMPropClass handle_wmprop_event (ASWMProps * wmprops, XEvent * event)
{
	WMPropClass result = WMC_NotHandled;
	if (event != NULL && wmprops != NULL)
		if (event->type == PropertyNotify) {
			prop_description_struct *descr = NULL;
			ASHashData hdata = {
				0
			};
			if (event->xproperty.window == wmprops->selection_window) {
				if (get_hash_item
						(wmprop_volitile_handlers,
						 AS_HASHABLE (event->xproperty.atom),
						 &hdata.vptr) != ASH_Success)
					hdata.vptr = NULL;
			} else if (event->xproperty.window == wmprops->scr->Root)
				if (get_hash_item
						(wmprop_root_handlers, AS_HASHABLE (event->xproperty.atom),
						 &hdata.vptr) != ASH_Success)
					hdata.vptr = NULL;
			if ((descr = hdata.vptr) != NULL) {
				result = descr->prop_class;
				if (event->xproperty.state == PropertyDelete) {
					clear_flags (wmprops->set_props, descr->prop_class);
					clear_flags (wmprops->my_props, descr->prop_class);
				}
				if (descr->read_func != NULL)
					if (descr->read_func (wmprops,
																(event->xproperty.state ==
																 PropertyDelete))) {
						set_flags (wmprops->set_props, descr->prop_class);
						clear_flags (wmprops->my_props, descr->prop_class);
					}
			}
		}
	return result;
}
