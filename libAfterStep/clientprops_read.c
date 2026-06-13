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

#define LOCAL_DEBUG
#include "../configure.h"
#include "asapp.h"
#include "screen.h"
#include "clientprops.h"
#include "hints.h"
#include "clientprops_internal.h"

/* Hint reading, atom/handler interning, raw-hint collection, hint printing
 * and client/manager property-update handling, split out of clientprops.c.
 * The shared atom tables, hint_handlers hash and parent_hints_func callback
 * are declared in clientprops_internal.h. */

/******************** Hints reading  functions **************************/

void read_wm_name (ASRawHints * hints, Window w)
{
	if (hints) {
		read_text_property (w, XA_WM_NAME, &(hints->wm_name));
	}
}

void read_wm_icon_name (ASRawHints * hints, Window w)
{
	if (hints) {
		read_text_property (w, XA_WM_ICON_NAME, &(hints->wm_icon_name));
	}
}

void read_wm_class (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (hints->wm_class) {
			if (hints->wm_class->res_name)
				XFree (hints->wm_class->res_name);
			if (hints->wm_class->res_class)
				XFree (hints->wm_class->res_class);
		} else
			hints->wm_class = XAllocClassHint ();

		if (XGetClassHint (dpy, w, hints->wm_class) == 0) {
			XFree (hints->wm_class);
			hints->wm_class = NULL;
		}
	}
}

void read_wm_hints (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (hints->wm_hints)
			XFree (hints->wm_hints);

		if ((hints->wm_hints = XGetWMHints (dpy, w)) != NULL) {
			if (get_flags (hints->wm_hints->flags, WindowGroupHint)
					&& hints->wm_hints->window_group != w) {
				ASParentHints parent_hints;

				if (parent_hints_func
						(hints->wm_hints->window_group, &parent_hints)) {
					if (hints->group_leader == NULL)
						hints->group_leader =
								(ASParentHints *) safecalloc (1, sizeof (ASParentHints));
					*(hints->group_leader) = parent_hints;
				}
			}
			if (get_flags (hints->wm_hints->flags, IconWindowHint)) {
				/* some apps are written by truly insane ppl, most notably those
				 * designed to be docked in Window Maker's dock
				 * So lets bite the bullet and check if it is indeed sane :*/
				if (hints->wm_hints->icon_window != w) {
					unsigned int width, height;

					if (!validate_drawable
							(hints->wm_hints->icon_window, &width, &height))
						hints->wm_hints->icon_window = None;
					else if (get_parent_window (hints->wm_hints->icon_window) == w) {
						if (width == 1 && height == 1) {	/* broken wmdock app - icon is animated, yet not properly sized */
							XMoveResizeWindow (dpy, hints->wm_hints->icon_window, 0, 0,
																 64, 64);
						}
						set_flags (hints->wm_hints->flags, IconWindowIsChildHint);
						hints->wm_hints->icon_window = w;
					}
				}
				/* while merging hints we will disable iconification for the window that
				 * has icon window same as client */

				if (hints->wm_hints->icon_window == None)
					clear_flags (hints->wm_hints->flags, IconWindowHint);
			}
		}
	}
}

void read_wm_normal_hints (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		long supplied;

		if (hints->wm_normal_hints == NULL)
			hints->wm_normal_hints = XAllocSizeHints ();

		if (XGetWMNormalHints (dpy, w, hints->wm_normal_hints, &supplied) == 0) {
			XFree (hints->wm_normal_hints);
			hints->wm_normal_hints = NULL;
		}
	}
}

void read_wm_transient_for (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		Window transient_for;
		ASParentHints parent_hints;

		if (XGetTransientForHint (dpy, w, &transient_for) != 0)
			if (transient_for != w
					&& parent_hints_func (transient_for, &parent_hints)) {
				if (hints->transient_for == NULL)
					hints->transient_for =
							(ASParentHints *) safecalloc (1, sizeof (ASParentHints));
				*(hints->transient_for) = parent_hints;
			}
	}
}

void read_wm_protocols (ASRawHints * hints, Window w)
{
	LOCAL_DEBUG_CALLER_OUT ("window(%lX)", w);
	if (hints && w != None) {
		CARD32 *protocols = NULL;
		long nprotos = 0;

		hints->wm_protocols = 0;
		if (read_32bit_proplist (w, _XA_WM_PROTOCOLS, 3, &protocols, &nprotos)) {
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
			char *aname = XGetAtomName (dpy, protocols[0]);

			LOCAL_DEBUG_OUT ("nprotos=%ld, protocols[0] = 0x%lX(\"%s\")",
											 nprotos, protocols[0], aname);
			XFree (aname);
#endif
			translate_atom_list (&(hints->wm_protocols), WM_Protocols, protocols,
													 nprotos);
			LOCAL_DEBUG_OUT ("translated protocols =0x%lX", hints->wm_protocols);
			translate_atom_list (&(hints->extwm_hints.flags), EXTWM_Protocols,
													 protocols, nprotos);
			LOCAL_DEBUG_OUT ("translated NET_ protocols =0x%lX",
											 hints->extwm_hints.flags);
			free (protocols);
		}
	}
}

void read_wm_cmap_windows (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (hints->wm_cmap_windows)
			free (hints->wm_cmap_windows);
		if (!read_32bit_proplist
				(w, _XA_WM_COLORMAP_WINDOWS, 10, &(hints->wm_cmap_windows),
				 &(hints->wm_cmap_win_count)))
			hints->wm_cmap_win_count = 0;
	}
}

void read_wm_client_machine (ASRawHints * hints, Window w)
{
	if (hints) {
		read_text_property (w, _XA_WM_CLIENT_MACHINE,
												&(hints->wm_client_machine));
	}
}

void read_wm_command (ASRawHints * hints, Window w)
{
	if (hints) {									/* there is some wierd bullshit about this propertie being depreciated and stuff
																 * so we just use standard X function to read it up : */
		if (hints->wm_cmd_argv)
			XFreeStringList (hints->wm_cmd_argv);
		LOCAL_DEBUG_OUT ("Reading WM_COMMAND property from window %lX", w);
		if (XGetCommand (dpy, w, &(hints->wm_cmd_argv), &(hints->wm_cmd_argc))
				== 0) {
			LOCAL_DEBUG_OUT ("XGetCommand returned 0%s", "");
			hints->wm_cmd_argv = NULL;
			hints->wm_cmd_argc = 0;
		}
	}
}

void read_wm_state (ASRawHints * hints, Window w)
{
	if (hints) {
		CARD32 *data = NULL;
		long nitems = 0;

		if (read_32bit_proplist (w, _XA_WM_STATE, 2, &data, &nitems)) {
			if (nitems > 0) {
				hints->wm_state = data[0];
				if (nitems > 1)
					hints->wm_state_icon_win = data[1];
			}
		}
		if (data)
			free (data);
	}
}

void read_motif_hints (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		CARD32 *raw_data = NULL;
		long nitems = 0;

		if (!read_32bit_proplist (w, _XA_MwmAtom, 4, &raw_data, &nitems))
			nitems = 0;

		if (hints->motif_hints) {
			free (hints->motif_hints);
			hints->motif_hints = NULL;
		}
		if (nitems >= 4) {
			hints->motif_hints = (MwmHints *) raw_data;
			raw_data = NULL;
		}
		if (raw_data)
			free (raw_data);
	}
}

void read_gnome_layer (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_32bit_property
				(w, _XA_WIN_LAYER, &(hints->gnome_hints.layer)))
			set_flags (hints->gnome_hints.flags, GNOME_LAYER);
	}
}

void read_gnome_state (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_32bit_property
				(w, _XA_WIN_STATE, &(hints->gnome_hints.state)))
			set_flags (hints->gnome_hints.flags, GNOME_STATE);
	}
}

void read_gnome_workspace (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_32bit_property
				(w, _XA_WIN_WORKSPACE, &(hints->gnome_hints.workspace)))
			set_flags (hints->gnome_hints.flags, GNOME_WORKSPACE);
	}
}

void read_gnome_hints (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_32bit_property
				(w, _XA_WIN_HINTS, &(hints->gnome_hints.hints)))
			set_flags (hints->gnome_hints.flags, GNOME_HINTS);
	}
}

void read_extwm_name (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_text_property
				(w, _XA_NET_WM_NAME, &(hints->extwm_hints.name)))
			set_flags (hints->extwm_hints.flags, EXTWM_NAME);
	}
}

void read_extwm_icon_name (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_text_property
				(w, _XA_NET_WM_ICON_NAME, &(hints->extwm_hints.icon_name)))
			set_flags (hints->extwm_hints.flags, EXTWM_ICON_NAME);
	}
}

void read_extwm_visible_name (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_text_property
				(w, _XA_NET_WM_VISIBLE_NAME, &(hints->extwm_hints.visible_name)))
			set_flags (hints->extwm_hints.flags, EXTWM_VISIBLE_NAME);
	}
}

void read_extwm_visible_icon_name (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_text_property
				(w, _XA_NET_WM_VISIBLE_ICON_NAME,
				 &(hints->extwm_hints.visible_icon_name)))
			set_flags (hints->extwm_hints.flags, EXTWM_VISIBLE_ICON_NAME);
	}
}

void read_extwm_desktop (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_32bit_property
				(w, _XA_NET_WM_DESKTOP, &(hints->extwm_hints.desktop)))
			set_flags (hints->extwm_hints.flags, EXTWM_DESKTOP);
		LOCAL_DEBUG_OUT ("NET_WM_DESKTOP = 0x%X", hints->extwm_hints.desktop);
	}
}

CARD32 read_extwm_desktop_val (Window w)
{
	CARD32 val = 0;

	if (w != None)
		read_32bit_property (w, _XA_NET_WM_DESKTOP, &val);
	return val;
}


void read_extwm_window_type (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		CARD32 *protocols;
		long nprotos = 0;

		if (read_32bit_proplist
				(w, _XA_NET_WM_WINDOW_TYPE, 6, &protocols, &nprotos)) {
			translate_atom_list (&(hints->extwm_hints.type_flags),
													 EXTWM_WindowType, protocols, nprotos);
			set_flags (hints->extwm_hints.flags, EXTWM_TypeSet);
			free (protocols);
		}
	}
}

void read_extwm_state (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		CARD32 *protocols;
		long nprotos = 0;

		if (read_32bit_proplist (w, _XA_NET_WM_STATE, 6, &protocols, &nprotos)) {
			translate_atom_list (&(hints->extwm_hints.state_flags), EXTWM_State,
													 protocols, nprotos);
			set_flags (hints->extwm_hints.flags, EXTWM_StateSet);
			free (protocols);
		}
	}
}


void read_extwm_pid (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_32bit_property (w, _XA_NET_WM_PID, &(hints->extwm_hints.pid)))
			set_flags (hints->extwm_hints.flags, EXTWM_PID);
	}
}

void read_extwm_icon (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_32bit_proplist
				(w, _XA_NET_WM_ICON, 2 + 48 * 48, &(hints->extwm_hints.icon),
				 &(hints->extwm_hints.icon_length)))
			set_flags (hints->extwm_hints.flags, EXTWM_ICON);
	}
}

void read_extwm_window_opacity (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_32bit_property
				(w, _XA_NET_WM_WINDOW_OPACITY,
				 &(hints->extwm_hints.window_opacity)))
			set_flags (hints->extwm_hints.flags, EXTWM_WINDOW_OPACITY);
	}
}

void read_kde_desktop_window (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		CARD32 dummy;

		if (read_32bit_property (w, _XA_KDE_DESKTOP_WINDOW, &dummy))
			set_flags (hints->kde_hints.flags, KDE_DesktopWindow);
	}
}

void read_kde_systray_window_for (ASRawHints * hints, Window w)
{
	if (hints && w != None) {
		if (read_32bit_property
				(w, _XA_KDE_NET_SYSTEM_TRAY_WINDOW_FOR,
				 (CARD32 *) & hints->kde_hints.systray_window_for))
			set_flags (hints->kde_hints.flags, KDE_SysTrayWindowFor);
	}
}

Bool default_parent_hints_func (Window parent, ASParentHints * dst)
{
	Window root;
	int dummy;
	unsigned int udummy;

	if (parent == None && dst == NULL)
		return False;
	if (!XGetGeometry
			(dpy, parent, &root, &dummy, &dummy, &udummy, &udummy, &udummy,
			 &udummy))
		return False;
	if (parent != ASDefaultRoot) {
		CARD32 desktop;

		dst->parent = parent;
		dst->flags = 0;
		if (read_32bit_property (parent, _XA_NET_WM_DESKTOP, &desktop)) {
			set_flags (dst->flags, AS_StartDesktop);
			dst->desktop = desktop;
		}
		/* can't really find out about viewport  here - write custom
		 * function for it, and override this deafult
		 */
	}
	return True;
}

get_parent_hints_func
set_parent_hints_func (get_parent_hints_func new_func)
{
	get_parent_hints_func old_func = parent_hints_func;

	if (new_func != NULL)
		parent_hints_func = new_func;
	return old_func;
}

/******************** Hints management functions **************************/

void intern_hint_atoms ()
{
	intern_atom_list (MainHints);
	intern_atom_list (WM_Protocols);
	intern_atom_list (EXTWM_WindowType);
	intern_atom_list (EXTWM_State);
	intern_atom_list (EXTWM_Protocols);
}

struct hint_description_struct {
	Atom *id_variable;
	void (*read_func) (ASRawHints *, Window);
	ASFlagType hint_class;				/* see HINT_ flags in hints.h */
	HintsTypes hint_type;
} HintsDescriptions[] = {
	{
	&_XA_WM_NAME, read_wm_name, HINT_NAME, HINTS_ICCCM}, {
	&_XA_WM_ICON_NAME, read_wm_icon_name, HINT_NAME, HINTS_ICCCM}, {
	&_XA_WM_CLASS, read_wm_class, HINT_NAME, HINTS_ICCCM}, {
	&_XA_WM_HINTS, read_wm_hints, HINT_STARTUP | HINT_GENERAL, HINTS_ICCCM}, {
	&_XA_WM_NORMAL_HINTS, read_wm_normal_hints,
				HINT_STARTUP | HINT_GENERAL, HINTS_ICCCM}, {
	&_XA_WM_TRANSIENT_FOR, read_wm_transient_for,
				HINT_STARTUP | HINT_GENERAL, HINTS_Transient}, {
	&_XA_WM_PROTOCOLS, read_wm_protocols, HINT_PROTOCOL,
				HINTS_ICCCM | HINTS_ExtendedWM}, {
	&_XA_WM_COLORMAP_WINDOWS, read_wm_cmap_windows, HINT_COLORMAP,
				HINTS_ICCCM}, {
	&_XA_WM_CLIENT_MACHINE, read_wm_client_machine, HINT_STARTUP,
				HINTS_ICCCM}, {
	&_XA_WM_COMMAND, read_wm_command, HINT_STARTUP, HINTS_ICCCM}, {
	&_XA_WM_STATE, read_wm_state, HINT_STARTUP, HINTS_ICCCM},
			/* Motif hints */
	{
	&_XA_MwmAtom, read_motif_hints,
				HINT_GENERAL | HINT_STARTUP | HINT_PROTOCOL, HINTS_Motif},
			/* Gnome hints */
	{
	&_XA_WIN_LAYER, read_gnome_layer, HINT_STARTUP, HINTS_Gnome}, {
	&_XA_WIN_STATE, read_gnome_state, HINT_STARTUP, HINTS_Gnome}, {
	&_XA_WIN_WORKSPACE, read_gnome_workspace, HINT_STARTUP, HINTS_Gnome}, {
	&_XA_WIN_HINTS, read_gnome_hints, HINT_GENERAL, HINTS_Gnome},
			/* wm-spec _NET hints : */
	{
	&_XA_NET_WM_NAME, read_extwm_name, HINT_NAME, HINTS_ExtendedWM}, {
	&_XA_NET_WM_ICON_NAME, read_extwm_icon_name, HINT_NAME,
				HINTS_ExtendedWM}, {
	&_XA_NET_WM_VISIBLE_NAME, read_extwm_visible_name, HINT_NAME,
				HINTS_ExtendedWM}, {
	&_XA_NET_WM_VISIBLE_ICON_NAME, read_extwm_visible_icon_name, HINT_NAME,
				HINTS_ExtendedWM}, {
	&_XA_NET_WM_DESKTOP, read_extwm_desktop, HINT_STARTUP | HINT_PROTOCOL,
				HINTS_ExtendedWM}, {
	&_XA_NET_WM_WINDOW_TYPE, read_extwm_window_type,
				HINT_STARTUP | HINT_GENERAL, HINTS_ExtendedWM}, {
	&_XA_NET_WM_STATE, read_extwm_state, HINT_STARTUP | HINT_GENERAL,
				HINTS_ExtendedWM}, {
	&_XA_NET_WM_PID, read_extwm_pid, HINT_GENERAL, HINTS_ExtendedWM}, {
	&_XA_NET_WM_ICON, read_extwm_icon, HINT_GENERAL, HINTS_ExtendedWM}, {
	&_XA_NET_WM_WINDOW_OPACITY, read_extwm_window_opacity, HINT_GENERAL,
				HINTS_ExtendedWM}, {
	&_XA_KDE_DESKTOP_WINDOW, read_kde_desktop_window, HINT_GENERAL,
				HINTS_KDE}, {
	&_XA_KDE_NET_SYSTEM_TRAY_WINDOW_FOR, read_kde_systray_window_for,
				HINT_GENERAL, HINTS_KDE}, {
	NULL, NULL, 0, HINTS_Supported}
};

void init_hint_handlers ()
{
	register int i;

	if (hint_handlers != NULL)
		destroy_ashash (&hint_handlers);

	hint_handlers = create_ashash (7, NULL, NULL, NULL);

	for (i = 0; HintsDescriptions[i].id_variable != NULL; i++) {
		add_hash_item (hint_handlers,
									 (ASHashableValue) * (HintsDescriptions[i].id_variable),
									 &(HintsDescriptions[i]));
	}
}

void clientprops_cleanup ()
{
	if (hint_handlers != NULL)
		destroy_ashash (&hint_handlers);
}

ASRawHints *collect_hints (ScreenInfo * scr, Window w, ASFlagType what,
													 ASRawHints * reusable_memory)
{
	ASRawHints *hints = NULL;

	if (w != None && what != 0) {
		Atom *all_props = NULL;
		int props_num = 0;
		struct hint_description_struct *descr;
		Window root;
		int x = 0, y = 0;
		unsigned int width = 2, height = 2, bw = 0, depth = 1;

		if (get_flags (what, HINT_STARTUP)) {
			Status res =
					XGetGeometry (dpy, w, &root, &x, &y, &width, &height, &bw,
												&depth);

			if (res == 0)
				return NULL;
		}

		if ((all_props = XListProperties (dpy, w, &props_num)) == NULL)
			return NULL;

		if (reusable_memory == NULL)
			hints = (ASRawHints *) safecalloc (1, sizeof (ASRawHints));
		else {
			hints = reusable_memory;
			memset (hints, 0x00, sizeof (ASRawHints));
		}

		hints->scr = scr ? scr : ASDefaultScr;

		if (get_flags (what, HINT_STARTUP)) {	/* we also need to get window position, size and border width */
			hints->placement.x = x;
			hints->placement.y = y;
			hints->placement.width = width;
			hints->placement.height = height;
			hints->border_width = bw;
			hints->wm_state = WithdrawnState;
		}

		if (hint_handlers == NULL)
			init_hint_handlers ();

		if (all_props) {
			while (props_num-- > 0) {
				ASHashData hdata = { 0 };
				if (get_hash_item
						(hint_handlers, AS_HASHABLE (all_props[props_num]),
						 &hdata.vptr) == ASH_Success) {
					if ((descr = hdata.vptr) != NULL)
						if (get_flags (descr->hint_class, what)
								&& descr->read_func != NULL) {
							descr->read_func (hints, w);
							set_flags (hints->hints_types, (0x01 << descr->hint_type));
						}
				}
			}
			XFree (all_props);
		}
		if (hints->group_leader != NULL)
			set_flags (hints->hints_types, (0x01 << HINTS_GroupLead));
	}
	return hints;
}

void destroy_raw_hints (ASRawHints * hints, Bool reusable)
{
	if (hints) {
		if (hints->wm_name)
			free_text_property (&(hints->wm_name));
		if (hints->wm_icon_name)
			free_text_property (&(hints->wm_icon_name));
		if (hints->wm_class) {
			if (hints->wm_class->res_name)
				XFree (hints->wm_class->res_name);
			if (hints->wm_class->res_class)
				XFree (hints->wm_class->res_class);
			XFree (hints->wm_class);
		}
		if (hints->wm_hints)
			XFree (hints->wm_hints);
		if (hints->group_leader)
			free (hints->group_leader);
		if (hints->wm_normal_hints)
			XFree (hints->wm_normal_hints);

		if (hints->transient_for)
			free (hints->transient_for);

		if (hints->wm_cmap_windows)
			free (hints->wm_cmap_windows);

		if (hints->wm_client_machine)
			free_text_property (&(hints->wm_client_machine));
		if (hints->wm_cmd_argv)
			XFreeStringList (hints->wm_cmd_argv);

		/* Motif Hints : */
		if (hints->motif_hints)
			free (hints->motif_hints);

		/* Gnome Hints : */
		/* nothing to free here */

		/* WM-specs (new Gnome) Hints : */
		if (hints->extwm_hints.name)
			free_text_property (&(hints->extwm_hints.name));
		if (hints->extwm_hints.icon_name)
			free_text_property (&(hints->extwm_hints.icon_name));
		if (hints->extwm_hints.icon)
			free (hints->extwm_hints.icon);

		if (reusable)								/* we are being paranoid */
			memset (hints, 0x00, sizeof (ASRawHints));
		else
			free (hints);
	}
}

/***********************************************************************************
 * Hints printing functions :
 ***********************************************************************************/
void print_wm_hints (stream_func func, void *stream, XWMHints * hints)
{
	if (!pre_print_check
			(&func, &stream, hints, "No ICCCM.WM_HINTS available(NULL)."))
		return;

	func (stream, "ICCCM.WM_HINTS.flags=0x%lX;\n", hints->flags);
	if (get_flags (hints->flags, InputHint))
		func (stream, "ICCCM.WM_HINTS.input = %s;\n",
					hints->input ? "True" : "False");
	if (get_flags (hints->flags, StateHint))
		func (stream, "ICCCM.WM_HINTS.initial_state = %s;\n",
					(hints->initial_state ==
					 NormalState) ? "Normal" : ((hints->initial_state ==
																			 IconicState) ? "Iconic" :
																			"Withdrawn"));
	if (get_flags (hints->flags, IconPixmapHint))
		func (stream, "ICCCM.WM_HINTS.icon_pixmap = 0x%lX;\n",
					hints->icon_pixmap);
	if (get_flags (hints->flags, IconWindowHint))
		func (stream, "ICCCM.WM_HINTS.icon_window = 0x%lX;\n",
					hints->icon_window);
	if (get_flags (hints->flags, IconMaskHint))
		func (stream, "ICCCM.WM_HINTS.icon_mask = 0x%lX;\n", hints->icon_mask);
	if (get_flags (hints->flags, IconPositionHint))
		func (stream,
					"ICCCM.WM_HINTS.icon_x = %d;\nICCCM.WM_HINTS.icon_y = %d;\n",
					hints->icon_x, hints->icon_y);
	if (get_flags (hints->flags, WindowGroupHint))
		func (stream, "ICCCM.WM_HINTS.window_group = 0x%lX;\n",
					hints->window_group);
}

void
print_parent_hints (stream_func func, void *stream, ASParentHints * hints,
										const char *prompt)
{
	if (!pre_print_check (&func, &stream, hints, prompt))
		return;

	if (hints->parent != ASDefaultRoot)
		func (stream, "%s.PARENT.parent = 0x%lX;\n", prompt, hints->parent);
	else
		func (stream, "%s.PARENT.parent = root;\n", prompt);

	func (stream, "%s.PARENT.flags=0x%lX;\n", prompt, hints->flags);
	if (get_flags (hints->flags, AS_StartDesktop))
		func (stream, "%s.PARENT.desktop = %d;\n", prompt, hints->desktop);
	if (get_flags (hints->flags, AS_StartViewportX))
		func (stream, "%s.PARENT.viewport_x = %d;\n", prompt,
					hints->viewport_x);
	if (get_flags (hints->flags, AS_StartViewportY))
		func (stream, "%s.PARENT.viewport_y = %d;\n", prompt,
					hints->viewport_y);
}

void
print_wm_normal_hints (stream_func func, void *stream, XSizeHints * hints)
{
	if (!pre_print_check
			(&func, &stream, hints, "No ICCCM.WM_NORMAL_HINTS available(NULL)."))
		return;

	func (stream, "ICCCM.WM_NORMAL_HINTS.flags = 0x%lX;\n", hints->flags);

	if (get_flags (hints->flags, PMinSize))
		func (stream,
					"ICCCM.WM_NORMAL_HINTS.min_width = %d;\nICCCM.WM_NORMAL_HINTS.min_height = %d;\n",
					hints->min_width, hints->min_height);
	if (get_flags (hints->flags, PMaxSize))
		func (stream,
					"ICCCM.WM_NORMAL_HINTS.max_width = %d;\nICCCM.WM_NORMAL_HINTS.max_height = %d;\n",
					hints->max_width, hints->max_height);
	if (get_flags (hints->flags, PResizeInc))
		func (stream,
					"ICCCM.WM_NORMAL_HINTS.width_inc = %d;\nICCCM.WM_NORMAL_HINTS.height_inc = %d;\n",
					hints->width_inc, hints->height_inc);
	if (get_flags (hints->flags, PAspect)) {
		func (stream,
					"ICCCM.WM_NORMAL_HINTS.min_aspect.x = %d;\nICCCM.WM_NORMAL_HINTS.min_aspect.y = %d;\n",
					hints->min_aspect.x, hints->min_aspect.y);
		func (stream,
					"ICCCM.WM_NORMAL_HINTS.max_aspect.x = %d;\nICCCM.WM_NORMAL_HINTS.max_aspect.y = %d;\n",
					hints->max_aspect.x, hints->max_aspect.y);
	}
	if (get_flags (hints->flags, PBaseSize))
		func (stream,
					"ICCCM.WM_NORMAL_HINTS.base_width = %d;\nICCCM.WM_NORMAL_HINTS.base_height = %d;\n",
					hints->base_width, hints->base_height);
	if (get_flags (hints->flags, PWinGravity))
		func (stream, "ICCCM.WM_NORMAL_HINTS.win_gravity = %d;\n",
					hints->win_gravity);
}

void print_motif_hints (stream_func func, void *stream, MwmHints * hints)
{
	if (!pre_print_check
			(&func, &stream, hints, "No MOTIF_WM_HINTS available(NULL)."))
		return;

	func (stream, "Motif.MOTIF_WM_HINTS.flags = 0x%lX;\n", hints->flags);

	if (get_flags (hints->flags, MWM_HINTS_FUNCTIONS))
		func (stream, "Motif.MOTIF_WM_HINTS.functions = 0x%lX;\n",
					hints->functions);
	if (get_flags (hints->flags, MWM_HINTS_DECORATIONS))
		func (stream, "Motif.MOTIF_WM_HINTS.decorations = 0x%lX;\n",
					hints->decorations);
	if (get_flags (hints->flags, MWM_HINTS_INPUT_MODE))
		func (stream, "Motif.MOTIF_WM_HINTS.inputMode = 0x%lX;\n",
					hints->inputMode);
}

void print_gnome_hints (stream_func func, void *stream, GnomeHints * hints)
{
	if (!pre_print_check
			(&func, &stream, hints, "No GNOME hints (_WIN_*) available(NULL)."))
		return;

	func (stream, "GNOME.flags = 0x%lX;\n", hints->flags);

	if (get_flags (hints->flags, GNOME_LAYER))
		func (stream, "GNOME._WIN_LAYER = %ld;\n", hints->layer);
	if (get_flags (hints->flags, GNOME_STATE))
		func (stream, "GNOME._WIN_STATE = 0x%lX;\n", hints->state);
	if (get_flags (hints->flags, GNOME_WORKSPACE))
		func (stream, "GNOME._WIN_WORKSPACE = %ld;\n", hints->workspace);
	if (get_flags (hints->flags, GNOME_HINTS))
		func (stream, "GNOME._WIN_HINTS = 0x%lX;\n", hints->hints);
}

void
print_extwm_hints (stream_func func, void *stream, ExtendedWMHints * hints)
{
	if (!pre_print_check
			(&func, &stream, hints,
			 "No Extended WM hints (_NET_*) available(NULL)."))
		return;

	func (stream, "EXTWM.flags = 0x%lX;\n", hints->flags);

	if (hints->name && get_flags (hints->flags, EXTWM_NAME))
		print_text_property (func, stream, hints->name, "EXTWM._NET_WM_NAME");
	if (hints->name && get_flags (hints->flags, EXTWM_ICON_NAME))
		print_text_property (func, stream, hints->icon_name,
												 "EXTWM._NET_WM_ICON_NAME");
	if (get_flags (hints->flags, EXTWM_DESKTOP))
		func (stream, "EXTWM._NET_WM_DESKTOP = %ld;\n", hints->desktop);
	if (get_flags (hints->flags, EXTWM_PID))
		func (stream, "EXTWM._NET_WM_PID = %ld;\n", hints->pid);
	if (get_flags (hints->flags, EXTWM_ICON)) {
		func (stream, "EXTWM._NET_WM_ICON.length = %ld;\n",
					hints->icon_length);
		if (hints->icon_length > 2 && hints->icon) {
			func (stream, "EXTWM._NET_WM_ICON.width = %ld;\n", hints->icon[0]);
			func (stream, "EXTWM._NET_WM_ICON.height = %ld;\n", hints->icon[1]);
		}
	}
	if (get_flags (hints->flags, EXTWM_WINDOW_OPACITY))
		func (stream, "EXTWM._NET_WM_WINDOW_OPACITY = %ld;\n",
					hints->window_opacity);

	if (get_flags (hints->flags, EXTWM_TypeSet))
		print_list_hints (func, stream, hints->type_flags, EXTWM_WindowType,
											"EXTWM._NET_WM_WINDOW_TYPE");
	if (get_flags (hints->flags, EXTWM_StateSet))
		print_list_hints (func, stream, hints->state_flags, EXTWM_State,
											"EXTWM._NET_WM_STATE");
	print_list_hints (func, stream, hints->flags, EXTWM_Protocols,
										"EXTWM._NET_WM_PROTOCOLS");
}

void print_hints (stream_func func, void *stream, ASRawHints * hints)
{
	register int i;

	if (!pre_print_check
			(&func, &stream, hints, "No hints available(NULL)."))
		return;

	/* ICCCM hints : */
	if (hints->wm_name)
		print_text_property (func, stream, hints->wm_name, "ICCCM.WM_NAME");
	if (hints->wm_icon_name)
		print_text_property (func, stream, hints->wm_icon_name,
												 "ICCCM.WM_ICON_NAME");
	if (hints->wm_class)
		func (stream,
					"ICCCM.WM_CLASS.res_name = \"%s\";\nICCCM.WM_CLASS.res_class = \"%s\";\n",
					hints->wm_class->res_name, hints->wm_class->res_class);
	if (hints->wm_hints)
		print_wm_hints (func, stream, hints->wm_hints);
	if (hints->group_leader)
		print_parent_hints (func, stream, hints->group_leader,
												"ICCCM.WM_HINTS.WINDOW_GROUP");
	if (hints->wm_normal_hints)
		print_wm_normal_hints (func, stream, hints->wm_normal_hints);

	func (stream,
				"ICCCM.placement.x = %d;\nICCCM.placement.y = %d;\nICCCM.placement.width = %d;\nICCCM.placement.height = %d;\n",
				hints->placement.x, hints->placement.y, hints->placement.width,
				hints->placement.height);

	if (hints->transient_for)
		print_parent_hints (func, stream, hints->transient_for,
												"ICCCM.WM_HINTS.TRANSIENT_FOR");

	print_list_hints (func, stream, hints->wm_protocols, WM_Protocols,
										"ICCCM.WM_PROTOCOLS");

	if (hints->wm_client_machine)
		print_text_property (func, stream, hints->wm_client_machine,
												 "ICCCM.WM_CLIENT_MACHINE");

	func (stream, "ICCCM.WM_COMMAND.argc = %d;\n", hints->wm_cmd_argc);
	if (hints->wm_cmd_argv)
		for (i = 0; i < hints->wm_cmd_argc; i++)
			func (stream, "ICCCM.WM_COMMAND.argv[%d] = \"%s\";\n", i,
						hints->wm_cmd_argv[i] ? hints->wm_cmd_argv[i] : "NULL");

	if (hints->wm_cmap_windows)
		for (i = 0; i < hints->wm_cmap_win_count; i++)
			func (stream, "ICCCM.WM_COLORMAP_WINDOWS[i] = 0x%lX;\n", i,
						hints->wm_cmap_windows[i]);

	func (stream, "ICCCM.WM_STATE = %d;\n", hints->wm_state);
	func (stream, "ICCCM.WM_STATE.icon = 0x%lX;\n",
				hints->wm_state_icon_win);

	/* Motif Hints : */
	if (hints->motif_hints)
		print_motif_hints (func, stream, hints->motif_hints);

	/* Gnome Hints : */
	if (hints->gnome_hints.flags != 0)
		print_gnome_hints (func, stream, &(hints->gnome_hints));

	/* WM-specs (new Gnome) Hints : */
	if (hints->extwm_hints.flags != 0)
		print_extwm_hints (func, stream, &(hints->extwm_hints));
}

/**********************************************************************************/
/* This is so that client app could re-read property updated by WM : */

Bool
handle_client_property_update (Window w, Atom property, ASRawHints * raw)
{
	void (*read_func) (ASRawHints * hints, Window w) = NULL;

	if (w && property && raw) {
		/* Here we are only interested in properties updtaed by the Window Manager : */
		if (property == _XA_WM_STATE)	/* ICCCM : */
			read_func = read_wm_state;
		else if (property == _XA_WIN_LAYER)	/* GNOME : */
			read_func = read_gnome_layer;
		else if (property == _XA_WIN_STATE)
			read_func = read_gnome_state;
		else if (property == _XA_WIN_WORKSPACE)
			read_func = read_gnome_workspace;
		else if (property == _XA_NET_WM_DESKTOP)	/* Extended WM-Hints : */
			read_func = read_extwm_desktop;
		else if (property == _XA_NET_WM_STATE)
			read_func = read_extwm_state;
		else
			return False;

		read_func (raw, w);

		return (validate_drawable (w, NULL, NULL) == w);
	}
	return False;
}

/* This is so that client app could re-read property updated by client : */
Bool
handle_manager_property_update (Window w, Atom property, ASRawHints * raw)
{
	void (*read_func) (ASRawHints * hints, Window w) = NULL;

	if (w && property && raw) {
		/* Here we are only interested in properties updtaed by the Window Manager : */
		if (IsNameProp (property)) {
			read_wm_state (raw, w);
			read_wm_icon_name (raw, w);
			read_wm_class (raw, w);
			read_extwm_name (raw, w);
			read_extwm_icon_name (raw, w);
			read_extwm_visible_name (raw, w);
			read_extwm_visible_icon_name (raw, w);
			return (validate_drawable (w, NULL, NULL) == w);
		} else if (property == _XA_WM_HINTS)
			read_func = read_wm_hints;
		else if (property == _XA_WM_NORMAL_HINTS)
			read_func = read_wm_normal_hints;
		else if (property == _XA_WM_PROTOCOLS)
			read_func = read_wm_protocols;
		else if (property == _XA_WM_COLORMAP_WINDOWS)
			read_func = read_wm_cmap_windows;
		else if (property == _XA_WM_STATE)
			read_func = read_wm_state;
		else if (property == _XA_WM_COMMAND)
			read_func = read_wm_command;
		else if (property == _XA_WM_CLIENT_MACHINE)
			read_func = read_wm_client_machine;
		else {
			show_debug (__FILE__, __FUNCTION__, __LINE__, "unknown property %X",
									property);
			return False;
		}
		read_func (raw, w);
		return (validate_drawable (w, NULL, NULL) == w);
	} else
		show_debug (__FILE__, __FUNCTION__, __LINE__,
								"incomplete parameters (%X, %X, %p)", w, property, raw);

	return False;
}

