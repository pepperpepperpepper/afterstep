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

/************************************************************************/
/*      Window Manager properties implementation :                      */
/************************************************************************/
ASHashTable *wmprop_root_handlers = NULL;
ASHashTable *wmprop_volitile_handlers = NULL;
Bool wait_event (XEvent * event, Window w, int mask, int max_wait);


/*
 * these atoms are constants in X11, but we still need pointers to them -
 * simply defining our own variables to hold those constants :
 */
Atom _XA_MIT_PRIORITY_COLORS = None;
Atom _XA_WM_CHANGE_STATE = None;

/* from Enlightenment for compatibility with Eterm */
Atom _XROOTPMAP_ID = None;
Atom ESETROOT_PMAP_ID = None;

/* Old Gnome compatibility specs : */
Atom _XA_WIN_SUPPORTING_WM_CHECK = None;
Atom _XA_WIN_PROTOCOLS = None;
Atom _XA_WIN_DESKTOP_BUTTON_PROXY = None;
extern Atom _XA_WIN_WORKSPACE;	/* declared in clientprops.c */

Atom _XA_WIN_WORKSPACE_COUNT = None;
Atom _XA_WIN_WORKSPACE_NAMES = None;
Atom _XA_WIN_CLIENT_LIST = None;

/* Extended Window Manager Compatibility specs : */
Atom _XA_NET_SUPPORTED = None;
Atom _XA_NET_CLIENT_LIST = None;
Atom _XA_NET_CLIENT_LIST_STACKING = None;
Atom _XA_NET_NUMBER_OF_DESKTOPS = None;
Atom _XA_NET_DESKTOP_GEOMETRY = None;
Atom _XA_NET_DESKTOP_VIEWPORT = None;
Atom _XA_NET_CURRENT_DESKTOP = None;
Atom _XA_NET_DESKTOP_NAMES = None;
Atom _XA_NET_ACTIVE_WINDOW = None;
Atom _XA_NET_WORKAREA = None;
Atom _XA_NET_SUPPORTING_WM_CHECK = None;
Atom _XA_NET_VIRTUAL_ROOTS = None;

/* AfterStep specific ; */
Atom _AS_STYLE = None;
Atom _AS_BACKGROUND = None;
Atom _AS_VISUAL = None;
Atom _AS_MODULE_SOCKET = None;
Atom _AS_VIRTUAL_ROOT = None;
Atom _AS_DESK_NUMBERS = None;
Atom _AS_CURRENT_DESK = None;
Atom _AS_CURRENT_VIEWPORT = None;
Atom _AS_SERVICE_WINDOW = None;
Atom _AS_TBAR_PROPS = None;
Atom _AS_BUTTON_CLOSE = None;
Atom _AS_BUTTON_CLOSE_PRESSED = None;
Atom _AS_BUTTON_MAXIMIZE = None;
Atom _AS_BUTTON_MAXIMIZE_PRESSED = None;
Atom _AS_BUTTON_MINIMIZE = None;
Atom _AS_BUTTON_MINIMIZE_PRESSED = None;
Atom _AS_BUTTON_SHADE = None;
Atom _AS_BUTTON_SHADE_PRESSED = None;
Atom _AS_BUTTON_MENU = None;
Atom _AS_BUTTON_MENU_PRESSED = None;
Atom _GTK_READ_RCFILES = None;
Atom _KIPC_COMM_ATOM = None;

/* Crossreferences of atoms into flag value for
   different atom list type of properties :*/
AtomXref _WMPropAtoms[] = {
	{
	 "_XA_MIT_PRIORITY_COLORS", &_XA_MIT_PRIORITY_COLORS}, {
																													"_XA_WM_CHANGE_STATE",
																													&_XA_WM_CHANGE_STATE},
	/* Enlightenment compatibility : */
	{
	 "_XROOTPMAP_ID", &_XROOTPMAP_ID}, {
																			"ESETROOT_PMAP_ID",
																			&ESETROOT_PMAP_ID},
	/* KWM compatibility ? */

	/* Old Gnome compatibility specs : */
	{
	 "_WIN_SUPPORTING_WM_CHECK", &_XA_WIN_SUPPORTING_WM_CHECK}, {
																															 "_WIN_PROTOCOLS",
																															 &_XA_WIN_PROTOCOLS},
	{
	 "_WIN_DESKTOP_BUTTON_PROXY", &_XA_WIN_DESKTOP_BUTTON_PROXY}, {
																																 "_WIN_WORKSPACE",
																																 &_XA_WIN_WORKSPACE},
	{
	 "_WIN_WORKSPACE_COUNT", &_XA_WIN_WORKSPACE_COUNT}, {
																											 "_WIN_WORKSPACE_NAMES",
																											 &_XA_WIN_WORKSPACE_NAMES},
	{ "_WIN_CLIENT_LIST", &_XA_WIN_CLIENT_LIST},
	/* Extended Window Manager Compatibility specs : */
	{ "_NET_SUPPORTED", &_XA_NET_SUPPORTED},
	{ "_NET_CLIENT_LIST", &_XA_NET_CLIENT_LIST},
	{ "_NET_CLIENT_LIST_STACKING", &_XA_NET_CLIENT_LIST_STACKING},
	{ "_NET_NUMBER_OF_DESKTOPS", &_XA_NET_NUMBER_OF_DESKTOPS},
	{ "_NET_DESKTOP_GEOMETRY", &_XA_NET_DESKTOP_GEOMETRY},
	{ "_NET_DESKTOP_VIEWPORT", &_XA_NET_DESKTOP_VIEWPORT},
	{ "_NET_CURRENT_DESKTOP", &_XA_NET_CURRENT_DESKTOP},
	{ "_NET_DESKTOP_NAMES", &_XA_NET_DESKTOP_NAMES},
	{ "_NET_ACTIVE_WINDOW", &_XA_NET_ACTIVE_WINDOW},
	{ "_NET_WORKAREA", &_XA_NET_WORKAREA},
	{ "_NET_SUPPORTING_WM_CHECK", &_XA_NET_SUPPORTING_WM_CHECK},
	{ "_NET_VIRTUAL_ROOTS", &_XA_NET_VIRTUAL_ROOTS},
	/* AfterStep specific stuff : */
	/* these are volitile properties ; */
	{
	 "_AS_STYLE", &_AS_STYLE}, {
															"_AS_BACKGROUND", &_AS_BACKGROUND}, {
																																	 "_AS_VISUAL",
																																	 &_AS_VISUAL},
	{
	 "_AS_MODULE_SOCKET", &_AS_MODULE_SOCKET}, {
																							"_AS_VIRTUAL_ROOT",
																							&_AS_VIRTUAL_ROOT},
	/* these are root properties : */
	{
	 "_AS_DESK_NUMBERS", &_AS_DESK_NUMBERS},	/* translation of the continuous range of */
	{
	 "_AS_CURRENT_DESK", &_AS_CURRENT_DESK},	/* current afterstep desk */
	{
	 "_AS_CURRENT_VIEWPORT", &_AS_CURRENT_VIEWPORT},	/* current afterstep viewport */
	{
	 "_AS_SERVICE_WINDOW", &_AS_SERVICE_WINDOW},	/* current afterstep Scr.ServiceWin */

#define WMPROPS_ATOM_DESC(a)   { #a, &a }
	WMPROPS_ATOM_DESC (_AS_TBAR_PROPS),
	WMPROPS_ATOM_DESC (_AS_BUTTON_CLOSE),
	WMPROPS_ATOM_DESC (_AS_BUTTON_CLOSE_PRESSED),
	WMPROPS_ATOM_DESC (_AS_BUTTON_MAXIMIZE),
	WMPROPS_ATOM_DESC (_AS_BUTTON_MAXIMIZE_PRESSED),
	WMPROPS_ATOM_DESC (_AS_BUTTON_MINIMIZE),
	WMPROPS_ATOM_DESC (_AS_BUTTON_MINIMIZE_PRESSED),
	WMPROPS_ATOM_DESC (_AS_BUTTON_SHADE),
	WMPROPS_ATOM_DESC (_AS_BUTTON_SHADE_PRESSED),
	WMPROPS_ATOM_DESC (_AS_BUTTON_MENU),
	WMPROPS_ATOM_DESC (_AS_BUTTON_MENU_PRESSED),
	WMPROPS_ATOM_DESC (_GTK_READ_RCFILES),
	WMPROPS_ATOM_DESC (_KIPC_COMM_ATOM), {
																				NULL, NULL, 0, None}
};

AtomXref *WMPropAtoms = &_WMPropAtoms[0];

/******************** Window Manager Selection  ***************************/
XErrorHandler catch_redirect_error (Display * dpy, XErrorEvent * event)
{
	show_error ("Another Window Manager is running. Aborting.");
	exit (1);
} static void intern_selection_atom (ASWMProps * wmprops)
{																/* intern us a selection atom : */
	char tmp[32];

	sprintf (tmp, "WM_S%ld", wmprops->scr->screen);
	wmprops->_XA_WM_S = XInternAtom (dpy, tmp, False);
} Bool accure_wm_selection (ASWMProps * wmprops)
{
	Window w;
	int tick_count;

	XEvent event;
	Window old_selection_owner = None;
	Bool accured = False;
	if (wmprops == NULL)
		return False;
	if (wmprops->selection_window)
		return True;

	/* create us a selection window : */
	w = wmprops->selection_window =
			XCreateSimpleWindow (dpy, wmprops->scr->Root, 0, 0, 5, 5, 0, 0,
													 None);
	if (w == None)
		return False;
	XSelectInput (dpy, w, PropertyChangeMask);
	intern_selection_atom (wmprops);
	{															/* now we need to obtain a valid timestamp : */
		CARD32 data = 0xAAAAAAAA;
		XChangeProperty (dpy, w, wmprops->_XA_WM_S, wmprops->_XA_WM_S, 32,
										 PropModeAppend, (unsigned char *)&data, 1);
		XSync (dpy, False);

		/* now lets sit quiet and wait for event to come back : */
		if (wait_event (&event, w, PropertyChangeMask, 100))
			wmprops->selection_time = event.xproperty.time;

		else
			wmprops->selection_time = CurrentTime;
	}
	old_selection_owner = XGetSelectionOwner (dpy, wmprops->_XA_WM_S);

	/* now we are ready to try and accure selection : */
	show_progress
			("Attempting to accure Window Management selection on screen %d ...",
			 wmprops->scr->screen);
	XSetSelectionOwner (dpy, wmprops->_XA_WM_S, w, wmprops->selection_time);
	XSync (dpy, False);

	/* give old owner 30 seconds to release selection : */
	for (tick_count = 0; tick_count < 300; tick_count++) {
		Window present_owner = XGetSelectionOwner (dpy, wmprops->_XA_WM_S);
		if (present_owner == w) {
			accured = True;
			break;
		}
		sleep_a_millisec (100);
	}
	if (accured && old_selection_owner != None) {

		/* we need to wait for old window manager to complete its shutdown, which
		 * will be indicated by its selection window becoming invalid.
		 * Otherwise some resources may still be unavailable, such as pointer grabbed,
		 * etc.
		 */
		unsigned int udumm;

		tick_count = 0;
		while (get_drawable_size (old_selection_owner, &udumm, &udumm)) {
			sleep_a_millisec (100);
			if (++tick_count == 600) {	/* if old window manager failed to shutdown in 60 seconds -
																	 * then just kill it ! */
				XKillClient (dpy, old_selection_owner);
				show_warning
						("Previous Window Manager failed to shutdown in allowed 60 seconds - killing it.");
			}
			if (tick_count > 1800) {
				show_error
						("Previous Window Manager failed to shutdown in allowed 3 minutes - Something is terribly wrong ! Aborting! $%^$&%#&# .... ");
				accured = False;
				break;
			}
		}
	}
	if (accured) {								/* great success !!! */
		XWindowAttributes attr;
		XErrorHandler old_handler = NULL;
		show_progress ("Window Management selection Accured.");

		/* now lets Add SubstructureRedirectMask to over event mask */
		XGetWindowAttributes (dpy, wmprops->scr->Root, &attr);
		old_handler = XSetErrorHandler ((XErrorHandler) catch_redirect_error);
		XSelectInput (dpy, wmprops->scr->Root,
									attr.your_event_mask | SubstructureRedirectMask);
		XSync (dpy, False);
		XSetErrorHandler (old_handler);

		/* lets notify all the clients that we are new selection owner */
		event.xclient.type = ClientMessage;
		event.xclient.message_type = XInternAtom (dpy, "MANAGER", False);
		event.xclient.format = 32;
		event.xclient.data.l[0] = wmprops->selection_time;
		event.xclient.data.l[1] = wmprops->_XA_WM_S;
		event.xclient.data.l[2] = wmprops->selection_window;
		event.xclient.data.l[3] = 0;
		event.xclient.data.l[4] = 0;
		XSendEvent (dpy, wmprops->scr->Root, False, StructureNotifyMask,
								&event);
		return True;
	}
	show_error ("Another Window Manager is running on screen %d. Aborting.",
							wmprops->scr->screen);
	XDestroyWindow (dpy, w);
	return False;
}


/***************** Window Manager compatibility properties :************************/
void setup_compatibility_props (ScreenInfo * scr)
{

	/* Old Gnome Compatibility */
	set_multi32bit_property (scr->Root, _XA_WIN_PROTOCOLS, XA_ATOM, 7,
													 _XA_WIN_LAYER, _XA_WIN_STATE,
													 _XA_WIN_WORKSPACE, _XA_WIN_HINTS,
													 _XA_WIN_WORKSPACE_COUNT,
													 _XA_WIN_CLIENT_LIST,
													 _XA_WIN_DESKTOP_BUTTON_PROXY);

	/* Ext WM compatibility */
	set_multi32bit_property (scr->Root, _XA_NET_SUPPORTED, XA_ATOM, 29, _XA_NET_SUPPORTED, _XA_NET_CLIENT_LIST, _XA_NET_CLIENT_LIST_STACKING, _XA_NET_NUMBER_OF_DESKTOPS, _XA_NET_DESKTOP_GEOMETRY, _XA_NET_DESKTOP_VIEWPORT, _XA_NET_CURRENT_DESKTOP, _XA_NET_ACTIVE_WINDOW, _XA_NET_SUPPORTING_WM_CHECK, _XA_NET_VIRTUAL_ROOTS,	/* 10 */
													 /* client window properties : */
													 _XA_NET_WM_NAME, _XA_NET_WM_DESKTOP, _XA_NET_WM_WINDOW_TYPE, _XA_NET_WM_WINDOW_TYPE_DESKTOP, _XA_NET_WM_WINDOW_TYPE_DOCK, _XA_NET_WM_WINDOW_TYPE_TOOLBAR, _XA_NET_WM_WINDOW_TYPE_MENU, _XA_NET_WM_WINDOW_TYPE_DIALOG, _XA_NET_WM_WINDOW_TYPE_NORMAL, _XA_NET_WM_STATE, _XA_NET_WM_STATE_MODAL, _XA_NET_WM_STATE_STICKY, _XA_NET_WM_STATE_MAXIMIZED_VERT, _XA_NET_WM_STATE_MAXIMIZED_HORZ, _XA_NET_WM_STATE_SHADED, _XA_NET_WM_STATE_SKIP_TASKBAR, _XA_NET_WM_PID, _XA_NET_WM_PING, _XA_NET_WM_ICON);	/*19 */
}

/*********** Setting up mechanismus used to avoid stale properties : **************/
static char *_as_afterstep_name = "AfterStep";

Bool setup_volitile_wmprops (ASWMProps * wmprops)
{
	register Window w;

	if (wmprops == NULL)
		return False;
	if (wmprops->selection_window == None)
		return False;
	w = wmprops->selection_window;

	/* old Gnome compatibility indicator : */
	set_32bit_property (wmprops->scr->Root, _XA_WIN_SUPPORTING_WM_CHECK,
											XA_CARDINAL, w);
	set_32bit_property (w, _XA_WIN_SUPPORTING_WM_CHECK, XA_CARDINAL, w);

	/* old Gnome event proxy : */
	set_32bit_property (wmprops->scr->Root, _XA_WIN_DESKTOP_BUTTON_PROXY,
											XA_CARDINAL, w);

	/* Ext WM specs compatibility indicator : */
	set_32bit_property (wmprops->scr->Root, _XA_NET_SUPPORTING_WM_CHECK,
											XA_WINDOW, w);
	set_32bit_property (w, _XA_NET_SUPPORTING_WM_CHECK, XA_WINDOW, w);
	set_text_property (w, _XA_NET_WM_NAME, &_as_afterstep_name, 1, TPE_UTF8);
	return True;
}

Bool query_wm_selection (ASWMProps * wmprops)
{
	if (wmprops) {
		intern_selection_atom (wmprops);
		wmprops->selection_window =
				XGetSelectionOwner (dpy, wmprops->_XA_WM_S);
		if (wmprops->selection_window != None)
			return True;
	}
	return False;
}

void release_wm_selection (ASWMProps * wmprops)
{
	if (wmprops) {
		show_progress
				("giving up Window Management selection on screen %d. Bye-bye!",
				 wmprops->scr->screen);
		XSetSelectionOwner (dpy, wmprops->_XA_WM_S, None,
												wmprops->selection_time);
		XDestroyWindow (dpy, wmprops->selection_window);
	}
}


/******************** Property reading functions:    **************************/
Bool read_mit_priority_colors (ASWMProps * wmprops, Bool deleted)
{
	if (wmprops) {
		CARD32 *list = NULL;
		long items = 0;

		if (wmprops->preserved_colors) {
			free (wmprops->preserved_colors);
			wmprops->preserved_colors = NULL;
		}
		if (deleted)
			return False;
		if (!read_32bit_proplist
				(wmprops->scr->Root, _XA_MIT_PRIORITY_COLORS, 4, &list, &items))
			return False;
		if (list) {
			if ((wmprops->preserved_colors_num = items) > 0 && list) {
				wmprops->preserved_colors = safemalloc (items * sizeof (CARD32));
				for (items--; items >= 0; items--)
					wmprops->preserved_colors[items] = (CARD32) (list[items]);
			}
			free (list);
		}
		return True;
	}
	return False;
}

Bool read_xrootpmap_id (ASWMProps * wmprops, Bool deleted)
{
	if (wmprops) {
		CARD32 pmap_id = None;
		if (deleted)
			return False;
		if (read_32bit_property (wmprops->scr->Root, _XROOTPMAP_ID, &pmap_id)) {
			wmprops->root_pixmap = (Pixmap) pmap_id;
			return True;
		}
	}
	return False;
}


/* ExtWM properties : ****************************************************/
Bool read_extwm_current_desk (ASWMProps * wmprops, Bool deleted)
{
	if (wmprops && !deleted) {
		CARD32 desk_no = None;
		if (read_32bit_property
				(wmprops->scr->Root, _XA_NET_CURRENT_DESKTOP, &desk_no)) {
			wmprops->desktop_current = (CARD32) desk_no;
			return True;
		}
	}
	return False;
}

Bool read_extwm_desk_viewport (ASWMProps * wmprops, Bool deleted)
{
	Bool success = False;
	if (wmprops && !deleted) {
		CARD32 *raw_data = NULL;
		long nitems = 0;

		if (!read_32bit_proplist
				(wmprops->scr->Root, _XA_NET_DESKTOP_VIEWPORT, 8, &raw_data,
				 &nitems))
			nitems = 0;
		if (wmprops->desktop_viewport) {
			free (wmprops->desktop_viewport);
			wmprops->desktop_viewport = NULL;
		}
		wmprops->desktop_viewports_num = nitems >> 1;
		if (nitems >= 2) {
			register int i = nitems;

			wmprops->desktop_viewport = safemalloc (nitems * sizeof (CARD32));
			while (--i >= 0)
				wmprops->desktop_viewport[i] = raw_data[i];
			success = True;
		}
		if (raw_data)
			free (raw_data);
	}
	return success;
}


/* AfterSTep own properties **********************************************/
Bool read_as_current_desk (ASWMProps * wmprops, Bool deleted)
{
	if (wmprops) {
		CARD32 desk_no = 0;
		if (deleted)
			return False;
		if (read_32bit_property
				(wmprops->scr->Root, _AS_CURRENT_DESK, &desk_no)) {
			wmprops->as_current_desk = desk_no;
			return True;
		}
	}
	return False;
}

Bool read_as_current_viewport (ASWMProps * wmprops, Bool deleted)
{
	Bool success = False;
	if (wmprops && !deleted) {
		CARD32 *raw_data = NULL;
		long nitems = 0;

		if (!read_32bit_proplist
				(wmprops->scr->Root, _AS_CURRENT_VIEWPORT, 2, &raw_data, &nitems))
			nitems = 0;
		if (nitems == 2) {
			wmprops->as_current_vx = raw_data[0];
			wmprops->as_current_vy = raw_data[1];
		}
		if (raw_data)
			free (raw_data);
	}
	return success;
}

Bool read_net_desktop_geometry (ASWMProps * wmprops, Bool deleted)
{
	Bool success = False;
	if (wmprops && !deleted) {
		CARD32 *raw_data = NULL;
		long nitems = 0;

		if (!read_32bit_proplist
				(wmprops->scr->Root, _AS_CURRENT_VIEWPORT, 2, &raw_data, &nitems))
			nitems = 0;
		if (nitems == 2) {
			wmprops->desktop_width = raw_data[0];
			wmprops->desktop_height = raw_data[1];
		}
		if (raw_data)
			free (raw_data);
	}
	return success;
}

Bool read_as_style (ASWMProps * wmprops, Bool deleted)
{
	Bool res = False;
	if (wmprops) {
		if (wmprops->as_styles_data) {
			free (wmprops->as_styles_data);
			wmprops->as_styles_data = NULL;
		}
		wmprops->as_styles_size = 0;
		if (deleted)
			return False;
		res =
				read_as_property (wmprops->selection_window, _AS_STYLE,
													&(wmprops->as_styles_size),
													&(wmprops->as_styles_version),
													&(wmprops->as_styles_data));
	}
	return res;
}

Bool read_as_background (ASWMProps * wmprops, Bool deleted)
{
	Bool res = False;
	if (wmprops) {
		CARD32 pmap_id = None;
		if (deleted)
			return False;
		if (read_32bit_property
				(wmprops->selection_window, _AS_BACKGROUND, &pmap_id)) {
			wmprops->as_root_pixmap = (Pixmap) pmap_id;
			return True;
		}
	}
	return res;
}

Bool read_as_visual (ASWMProps * wmprops, Bool deleted)
{
	Bool res = False;
	if (wmprops) {
		if (wmprops->as_visual_data) {
			free (wmprops->as_visual_data);
			wmprops->as_visual_data = NULL;
		}
		wmprops->as_visual_size = 0;
		if (deleted)
			return False;
		res =
				read_as_property (wmprops->selection_window, _AS_VISUAL,
													&(wmprops->as_visual_size),
													&(wmprops->as_visual_version),
													&(wmprops->as_visual_data));
	}
	return res;
}

Bool read_as_module_socket (ASWMProps * wmprops, Bool deleted)
{
	LOCAL_DEBUG_CALLER_OUT ("wmprops(%p)->deleted(%d)", wmprops, deleted);
	if (wmprops) {
		char *socket_name = NULL;

		if (wmprops->as_socket_filename)
			free (wmprops->as_socket_filename);
		if (deleted)
			return False;
		if (!read_string_property
				(wmprops->selection_window, _AS_MODULE_SOCKET, &socket_name))
			return False;
		LOCAL_DEBUG_OUT ("\tas_socket_name is [%s]", socket_name);
		wmprops->as_socket_filename = mystrdup (socket_name);
		XFree (socket_name);
		return True;
	}
	return False;
}

Bool read_as_virtual_root (ASWMProps * wmprops, Bool deleted)
{
	if (wmprops) {
		CARD32 vroot_id = None;
		if (deleted)
			return False;
		if (read_32bit_property
				(wmprops->selection_window, _AS_VIRTUAL_ROOT, &vroot_id)) {
			wmprops->as_virtual_root = (Window) vroot_id;
			return True;
		}
	}
	return False;
}

Bool read_as_service_window (ASWMProps * wmprops, Bool deleted)
{
	if (wmprops) {
		CARD32 swin_id = None;
		if (deleted)
			return False;
		if (read_32bit_property
				(wmprops->selection_window, _AS_SERVICE_WINDOW, &swin_id)) {
			wmprops->as_service_window = (Window) swin_id;
			return True;
		}
	}
	return False;
}

Bool read_as_tbar_props (ASWMProps * wmprops, Bool deleted)
{
	Bool res = False;
	if (wmprops) {
		if (wmprops->as_tbar_props_data) {
			free (wmprops->as_tbar_props_data);
			wmprops->as_tbar_props_data = NULL;
		}
		wmprops->as_tbar_props_size = 0;
		if (deleted)
			return False;
		res =
				read_as_property (wmprops->selection_window, _AS_TBAR_PROPS,
													&(wmprops->as_tbar_props_size),
													&(wmprops->as_tbar_props_version),
													&(wmprops->as_tbar_props_data));
	}
	return res;
}


/******************** Property management functions: **************************/
void intern_wmprop_atoms ()
{
	intern_atom_list (WMPropAtoms);
}

prop_description_struct WMPropsDescriptions_root[] = {
	{
	 &_XA_MIT_PRIORITY_COLORS, read_mit_priority_colors,
	 WMC_PreservedColors, 0}, {
														 &_XROOTPMAP_ID, read_xrootpmap_id,
														 WMC_RootPixmap, 0},
	/* TODO:implement GNOME/KDE compatibility properties read/write */
	{
	 &_XA_WIN_SUPPORTING_WM_CHECK, NULL, 0, 0}, {
																							 &_XA_WIN_PROTOCOLS, NULL, 0,
																							 0}, {
																										&_XA_WIN_DESKTOP_BUTTON_PROXY,
																										NULL, 0, 0}, {
																																	&_XA_WIN_WORKSPACE,
																																	NULL,
																																	WMC_Desktops,
																																	0}, {
																																			 &_XA_WIN_WORKSPACE_COUNT,
																																			 NULL,
																																			 WMC_Desktops,
																																			 0},
	{
	 &_XA_WIN_WORKSPACE_NAMES, NULL, WMC_DesktopNames, 0}, {
																													&_XA_WIN_CLIENT_LIST,
																													NULL,
																													WMC_ClientList,
																													WMP_NeedsCleanup},
	{ &_XA_NET_SUPPORTED, NULL, 0, 0},
	{ &_XA_NET_CLIENT_LIST, NULL, WMC_ClientList, WMP_NeedsCleanup},
	{ &_XA_NET_CLIENT_LIST_STACKING, NULL, WMC_ClientList, WMP_NeedsCleanup},
	{ &_XA_NET_NUMBER_OF_DESKTOPS, NULL, WMC_Desktops, 0},
	{ &_XA_NET_DESKTOP_GEOMETRY, read_net_desktop_geometry, WMC_Desktops, 0},
	{ &_XA_NET_DESKTOP_VIEWPORT, read_extwm_desk_viewport, WMC_DesktopViewport, 0},
	{ &_XA_NET_CURRENT_DESKTOP, read_extwm_current_desk, WMC_DesktopCurrent, 0},
	{	&_XA_NET_DESKTOP_NAMES, NULL,	WMC_DesktopNames, 0},
	{ &_XA_NET_ACTIVE_WINDOW, NULL, WMC_ActiveWindow, WMP_NeedsCleanup},
	{
	 &_XA_NET_WORKAREA, NULL, WMC_WorkArea, WMP_NeedsCleanup}, {
																															&_XA_NET_SUPPORTING_WM_CHECK,
																															NULL, 0, 0},
	{
	 &_XA_NET_VIRTUAL_ROOTS, NULL, WMC_Desktops, WMP_NeedsCleanup}, {
																																	 &_AS_DESK_NUMBERS,
																																	 NULL,
																																	 WMC_ASDesks,
																																	 0}, {
																																				&_AS_CURRENT_DESK,
																																				read_as_current_desk,
																																				WMC_ASDesks,
																																				0},
	{
	 &_AS_CURRENT_VIEWPORT, read_as_current_viewport, WMC_ASViewport, 0}, {
																																				 NULL,
																																				 NULL,
																																				 0,
																																				 0}
};

prop_description_struct WMPropsDescriptions_volitile[] = {
	{
	 &_AS_STYLE, read_as_style, WMC_ASStyles, 0}, {
																								 &_AS_BACKGROUND,
																								 read_as_background,
																								 WMC_ASBackgrounds,
																								 WMP_ClientWritable}, {
																																			 &_AS_VISUAL,
																																			 read_as_visual,
																																			 WMC_ASVisual,
																																			 0},
	{
	 &_AS_MODULE_SOCKET, read_as_module_socket, WMC_ASModule, 0}, {
																																 &_AS_VIRTUAL_ROOT,
																																 read_as_virtual_root,
																																 WMC_ASVirtualRoot,
																																 0}, {
																																			&_AS_SERVICE_WINDOW,
																																			read_as_service_window,
																																			WMC_ASServiceWindow,
																																			0}, {
																																					 &_AS_TBAR_PROPS,
																																					 read_as_tbar_props,
																																					 WMC_ASTBarProps,
																																					 0},
	{
	 NULL, NULL, 0, 0}
};

void init_wmprop_root_handlers ()
{
	register int i;

	if (wmprop_root_handlers != NULL)
		destroy_ashash (&wmprop_root_handlers);
	wmprop_root_handlers = create_ashash (7, NULL, NULL, NULL);
	for (i = 0; WMPropsDescriptions_root[i].id_variable != NULL; i++) {
		add_hash_item (wmprop_root_handlers,
									 (ASHashableValue) *
									 (WMPropsDescriptions_root[i].id_variable),
									 &(WMPropsDescriptions_root[i]));
	}
}

void init_wmprop_volitile_handlers ()
{
	register int i;

	if (wmprop_volitile_handlers != NULL)
		destroy_ashash (&wmprop_volitile_handlers);
	wmprop_volitile_handlers = create_ashash (7, NULL, NULL, NULL);
	for (i = 0; WMPropsDescriptions_volitile[i].id_variable != NULL; i++) {
		add_hash_item (wmprop_volitile_handlers,
									 (ASHashableValue) *
									 (WMPropsDescriptions_volitile[i].id_variable),
									 &(WMPropsDescriptions_volitile[i]));
	}
}

void wmprops_cleanup ()
{
	destroy_ashash (&wmprop_root_handlers);
	destroy_ashash (&wmprop_volitile_handlers);
} void read_root_wmprops (ASWMProps * wmprops, Bool do_cleanup)
{
	Atom *all_props = NULL;
	int props_num = 0;
	struct prop_description_struct *descr;

	if (wmprops == NULL)
		return;
	all_props = XListProperties (dpy, wmprops->scr->Root, &props_num);
	if (wmprop_root_handlers == NULL)
		init_wmprop_root_handlers ();
	if (all_props) {
		while (props_num-- > 0) {
			ASHashData hdata;
			if (get_hash_item
					(wmprop_root_handlers, (ASHashableValue) all_props[props_num],
					 &hdata.vptr) == ASH_Success)
				if ((descr = hdata.vptr) != NULL) {
					if (get_flags (descr->flags, WMP_NeedsCleanup) && do_cleanup)
						XDeleteProperty (dpy, wmprops->scr->Root,
														 all_props[props_num]);

					else if (descr->read_func != NULL)
						if (descr->read_func (wmprops, False)) {
							set_flags (wmprops->set_props, descr->prop_class);
							clear_flags (wmprops->my_props, descr->prop_class);
						}
				}
		}
		XFree (all_props);
	}
}

void read_volitile_wmprops (ASWMProps * wmprops)
{
	Atom *all_props = NULL;
	int props_num = 0;
	struct prop_description_struct *descr;

	LOCAL_DEBUG_CALLER_OUT ("wmprops(%p)->selection_window(%lX)", wmprops,
													wmprops ? wmprops->selection_window : None);
	if (wmprops == NULL || wmprops->selection_window == None)
		return;
	all_props = XListProperties (dpy, wmprops->selection_window, &props_num);
	LOCAL_DEBUG_OUT ("XListProperties returned %d volitile props",
									 props_num);
	if (wmprop_volitile_handlers == NULL)
		init_wmprop_volitile_handlers ();
	if (all_props) {
		while (props_num-- > 0) {
			ASHashData hdata;
			LOCAL_DEBUG_OUT ("checking property %d [%s]...", props_num,
											 XGetAtomName (dpy, all_props[props_num]));
			if (get_hash_item
					(wmprop_volitile_handlers,
					 (ASHashableValue) all_props[props_num],
					 &hdata.vptr) == ASH_Success) {
				LOCAL_DEBUG_OUT ("\tfound description %p", hdata.vptr);
				if ((descr = hdata.vptr) != NULL)
					if (descr->read_func != NULL)
						if (descr->read_func (wmprops, False)) {
							set_flags (wmprops->set_props, descr->prop_class);
							clear_flags (wmprops->my_props, descr->prop_class);
						}
			}
		}
		XFree (all_props);
	}
}

ASWMProps *setup_wmprops (ScreenInfo * scr, Bool manager,
													ASFlagType what, ASWMProps * reusable_memory)
{
	ASWMProps *wmprops = NULL;
	if (what != 0) {
		if (reusable_memory == NULL)
			wmprops = (ASWMProps *) safecalloc (1, sizeof (ASWMProps));

		else {
			wmprops = reusable_memory;
			memset (wmprops, 0x00, sizeof (ASWMProps));
		}
		wmprops->manager = manager;
		wmprops->scr = scr;
		if (manager) {
			if (!accure_wm_selection (wmprops)) {
				if (reusable_memory == NULL)
					free (wmprops);
				return NULL;
			}
			setup_compatibility_props (scr);
			setup_volitile_wmprops (wmprops);
		} else {
			query_wm_selection (wmprops);
			XSelectInput (dpy, wmprops->selection_window, PropertyChangeMask);
		}
		read_root_wmprops (wmprops, manager);
		if (!manager) {
			read_volitile_wmprops (wmprops);
			scr->ServiceWin = wmprops->as_service_window;
		}
	}
	return wmprops;
}

void destroy_wmprops (ASWMProps * wmprops, Bool reusable)
{
	if (wmprops) {
		if (wmprops->manager) {			/* lets cleanup after us : */
			read_root_wmprops (wmprops, True);
			release_wm_selection (wmprops);
		}
		if (wmprops->supported)
			free (wmprops->supported);
		if (wmprops->preserved_colors)
			free (wmprops->preserved_colors);
		if (wmprops->virtual_roots)
			free (wmprops->virtual_roots);
		if (wmprops->desktop_names) {
			if (wmprops->desktop_names[0])
				free (wmprops->desktop_names[0]);
			free (wmprops->desktop_names);
		}
		if (wmprops->desktop_viewport)
			free (wmprops->desktop_viewport);
		if (wmprops->client_list)
			free (wmprops->client_list);
		if (wmprops->stacking_order)
			free (wmprops->stacking_order);
		if (wmprops->as_styles_data)
			free (wmprops->as_styles_data);
		if (wmprops->as_visual_data)
			free (wmprops->as_visual_data);
		if (wmprops->as_socket_filename)
			free (wmprops->as_socket_filename);
		if (wmprops->as_desk_numbers)
			free (wmprops->as_desk_numbers);
		if (wmprops->as_tbar_props_data)
			free (wmprops->as_tbar_props_data);

		/* we are being paranoid : */
		memset (wmprops, 0x00, sizeof (ASWMProps));
		if (!reusable)
			free (wmprops);
	}
}

