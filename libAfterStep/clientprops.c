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

/************************************************************************/
/*		New hints implementation :				*/
/************************************************************************/

ASHashTable *hint_handlers = NULL;
Bool default_parent_hints_func (Window parent, ASParentHints * dst);
get_parent_hints_func parent_hints_func = default_parent_hints_func;

Bool as_xrm_initialized = False;
XrmDatabase as_xrm_user_db = None;

/*
 * these atoms are constants in X11, but we still need pointers to them -
 * simply defining our own variables to hold those constants :
 */
Atom _XA_WM_NAME = XA_WM_NAME;
Atom _XA_WM_ICON_NAME = XA_WM_ICON_NAME;
Atom _XA_WM_CLASS = XA_WM_CLASS;
Atom _XA_WM_HINTS = XA_WM_HINTS;
Atom _XA_WM_NORMAL_HINTS = XA_WM_NORMAL_HINTS;
Atom _XA_WM_TRANSIENT_FOR = XA_WM_TRANSIENT_FOR;
Atom _XA_WM_COMMAND = XA_WM_COMMAND;
Atom _XA_WM_CLIENT_MACHINE = XA_WM_CLIENT_MACHINE;

/*
 * rest of the atoms has to be interned by us :
 */
Atom _XA_WM_PROTOCOLS;
Atom _XA_WM_TAKE_FOCUS;
Atom _XA_WM_DELETE_WINDOW;
Atom _XA_WM_COLORMAP_WINDOWS;
Atom _XA_WM_STATE;
Atom _XA_SM_CLIENT_ID;
Atom _XA_WM_WINDOW_ROLE;
Atom _XA_WM_CLIENT_LEADER;

/* Motif hints */
Atom _XA_MwmAtom;

/* Gnome hints */
Atom _XA_WIN_LAYER;
Atom _XA_WIN_STATE;
Atom _XA_WIN_WORKSPACE;
Atom _XA_WIN_HINTS;

/* wm-spec _NET hints : */
Atom _XA_NET_WM_NAME;
Atom _XA_NET_WM_ICON_NAME;

Atom _XA_NET_WM_VISIBLE_NAME;
Atom _XA_NET_WM_VISIBLE_ICON_NAME;

Atom _XA_NET_WM_DESKTOP;
Atom _XA_NET_WM_WINDOW_TYPE;
Atom _XA_NET_WM_WINDOW_TYPE_DESKTOP;
Atom _XA_NET_WM_WINDOW_TYPE_DOCK;
Atom _XA_NET_WM_WINDOW_TYPE_TOOLBAR;
Atom _XA_NET_WM_WINDOW_TYPE_MENU;
Atom _XA_NET_WM_WINDOW_TYPE_DIALOG;
Atom _XA_NET_WM_WINDOW_TYPE_NORMAL;
Atom _XA_NET_WM_WINDOW_TYPE_UTILITY;
Atom _XA_NET_WM_WINDOW_TYPE_SPLASH;
Atom _XA_AS_WM_WINDOW_TYPE_MODULE;

Atom _XA_NET_WM_STATE;
Atom _XA_NET_WM_STATE_MODAL;
Atom _XA_NET_WM_STATE_STICKY;
Atom _XA_NET_WM_STATE_MAXIMIZED_VERT;
Atom _XA_NET_WM_STATE_MAXIMIZED_HORZ;
Atom _XA_NET_WM_STATE_SHADED;
Atom _XA_NET_WM_STATE_SKIP_TASKBAR;
Atom _XA_NET_WM_STATE_SKIP_PAGER;
Atom _XA_NET_WM_STATE_HIDDEN;
Atom _XA_NET_WM_STATE_FULLSCREEN;
Atom _XA_NET_WM_STATE_ABOVE;
Atom _XA_NET_WM_STATE_BELOW;
Atom _XA_NET_WM_STATE_DEMANDS_ATTENTION;
Atom _XA_NET_WM_STATE_FOCUSED;

Atom _XA_NET_WM_PID;
Atom _XA_NET_WM_ICON;
Atom _XA_NET_WM_PING;
Atom _XA_NET_WM_WINDOW_OPACITY;

/* Implements KDE System tray specs:
 * http://developer.kde.org/documentation/library/kdeqt/kde3arch/protocols-docking.html
 * https://listman.redhat.com/archives/xdg-list/2002-March/msg00014.html
 * http://standards.freedesktop.org/systemtray-spec/systemtray-spec-0.2.html#ftn.id2494129
 *
 */
Atom _XA_KDE_DESKTOP_WINDOW = None;
Atom _XA_KDE_NET_SYSTEM_TRAY_WINDOW_FOR = None;


/* Crossreferences of atoms into flag value for
   different atom list type of properties :*/

AtomXref MainHints[] = {
	{"WM_PROTOCOLS", &_XA_WM_PROTOCOLS},
	{"WM_COLORMAP_WINDOWS", &_XA_WM_COLORMAP_WINDOWS},
	{"WM_STATE", &_XA_WM_STATE},
	{"SM_CLIENT_ID", &_XA_SM_CLIENT_ID},
	{"WM_WINDOW_ROLE", &_XA_WM_WINDOW_ROLE},
	{"WM_CLIENT_LEADER", &_XA_WM_CLIENT_LEADER},
	{"_MOTIF_WM_HINTS", &_XA_MwmAtom},
	{"_WIN_LAYER", &_XA_WIN_LAYER},
	{"_WIN_STATE", &_XA_WIN_STATE},
	{"_WIN_WORKSPACE", &_XA_WIN_WORKSPACE},
	{"_WIN_HINTS", &_XA_WIN_HINTS},
	{"_NET_WM_NAME", &_XA_NET_WM_NAME},
	{"_NET_WM_ICON_NAME", &_XA_NET_WM_ICON_NAME},
	{"_NET_WM_VISIBLE_NAME", &_XA_NET_WM_VISIBLE_NAME},
	{"_NET_WM_VISIBLE_ICON_NAME", &_XA_NET_WM_VISIBLE_ICON_NAME},
	{"_NET_WM_DESKTOP", &_XA_NET_WM_DESKTOP},
	{"_NET_WM_WINDOW_TYPE", &_XA_NET_WM_WINDOW_TYPE},
	{"_NET_WM_STATE", &_XA_NET_WM_STATE},
	{"_NET_WM_PID", &_XA_NET_WM_PID},
	{"_NET_WM_ICON", &_XA_NET_WM_ICON},
	{"_NET_WM_WINDOW_OPACITY", &_XA_NET_WM_WINDOW_OPACITY},
	{"_KDE_DESKTOP_WINDOW", &_XA_KDE_DESKTOP_WINDOW},
	{"_KDE_NET_SYSTEM_TRAY_WINDOW_FOR", &_XA_KDE_NET_SYSTEM_TRAY_WINDOW_FOR},
	{NULL, NULL, 0, None}
};

AtomXref WM_Protocols[] = {
	{"WM_TAKE_FOCUS", &_XA_WM_TAKE_FOCUS, AS_DoesWmTakeFocus},
	{"WM_DELETE_WINDOW", &_XA_WM_DELETE_WINDOW, AS_DoesWmDeleteWindow},
	{NULL, NULL, 0, None}
};

AtomXref EXTWM_WindowType[] = {
	{"_NET_WM_WINDOW_TYPE_DESKTOP", &_XA_NET_WM_WINDOW_TYPE_DESKTOP,
	 EXTWM_TypeDesktop},
	{"_NET_WM_WINDOW_TYPE_DOCK", &_XA_NET_WM_WINDOW_TYPE_DOCK,
	 EXTWM_TypeDock},
	{"_NET_WM_WINDOW_TYPE_TOOLBAR", &_XA_NET_WM_WINDOW_TYPE_TOOLBAR,
	 EXTWM_TypeToolbar},
	{"_NET_WM_WINDOW_TYPE_MENU", &_XA_NET_WM_WINDOW_TYPE_MENU,
	 EXTWM_TypeMenu},
	{"_NET_WM_WINDOW_TYPE_DIALOG", &_XA_NET_WM_WINDOW_TYPE_DIALOG,
	 EXTWM_TypeDialog},
	{"_NET_WM_WINDOW_TYPE_NORMAL", &_XA_NET_WM_WINDOW_TYPE_NORMAL,
	 EXTWM_TypeNormal},
	{"_NET_WM_WINDOW_TYPE_UTILITY", &_XA_NET_WM_WINDOW_TYPE_UTILITY,
	 EXTWM_TypeUtility},
	{"_NET_WM_WINDOW_TYPE_SPLASH", &_XA_NET_WM_WINDOW_TYPE_SPLASH,
	 EXTWM_TypeSplash},
	{"_AS_WM_WINDOW_TYPE_MODULE", &_XA_AS_WM_WINDOW_TYPE_MODULE,
	 EXTWM_TypeASModule},

	{NULL, NULL, 0, None}
};

AtomXref _EXTWM_State[] = {
	{"_NET_WM_STATE_MODAL", &_XA_NET_WM_STATE_MODAL, EXTWM_StateModal},
	{"_NET_WM_STATE_STICKY", &_XA_NET_WM_STATE_STICKY, EXTWM_StateSticky},
	{"_NET_WM_STATE_MAXIMIZED_VERT", &_XA_NET_WM_STATE_MAXIMIZED_VERT,
	 EXTWM_StateMaximizedV},
	{"_NET_WM_STATE_MAXIMIZED_HORZ", &_XA_NET_WM_STATE_MAXIMIZED_HORZ,
	 EXTWM_StateMaximizedH},
	{"_NET_WM_STATE_SHADED", &_XA_NET_WM_STATE_SHADED, EXTWM_StateShaded},
	{"_NET_WM_STATE_SKIP_TASKBAR", &_XA_NET_WM_STATE_SKIP_TASKBAR,
	 EXTWM_StateSkipTaskbar},
	{"_NET_WM_STATE_SKIP_PAGER", &_XA_NET_WM_STATE_SKIP_PAGER,
	 EXTWM_StateSkipPager},
	{"_NET_WM_STATE_HIDDEN", &_XA_NET_WM_STATE_HIDDEN, EXTWM_StateHidden},
	{"_NET_WM_STATE_FULLSCREEN", &_XA_NET_WM_STATE_FULLSCREEN,
	 EXTWM_StateFullscreen},
	{"_NET_WM_STATE_ABOVE", &_XA_NET_WM_STATE_ABOVE, EXTWM_StateAbove},
	{"_NET_WM_STATE_BELOW", &_XA_NET_WM_STATE_BELOW, EXTWM_StateBelow},
	{"_NET_WM_STATE_DEMANDS_ATTENTION",
	 &_XA_NET_WM_STATE_DEMANDS_ATTENTION, EXTWM_StateDemandsAttention},
	{"_NET_WM_STATE_FOCUSED", &_XA_NET_WM_STATE_FOCUSED, EXTWM_StateFocused},
	{NULL, NULL, 0, None}
};

AtomXref *EXTWM_State = &(_EXTWM_State[0]);

AtomXref EXTWM_Protocols[] = {
	{"_NET_WM_PING", &_XA_NET_WM_PING, EXTWM_DoesWMPing},
	{NULL, NULL, 0, None}
};

/*********************** Utility functions ******************************/
/* X resources access :*/
void init_xrm ()
{
	if (!as_xrm_initialized) {
		XrmInitialize ();
		as_xrm_initialized = True;
	}
}

Bool
read_int_resource (XrmDatabase db, const char *res_name,
									 const char *res_class, int *value)
{
	char *str_type;
	XrmValue rm_value;

	if (XrmGetResource (db, res_name, res_class, &str_type, &rm_value) != 0)
		if (rm_value.size > 0) {
			register char *ptr = rm_value.addr;
			int val;

			if (*ptr == 'w')
				ptr++;
			val = atoi (ptr);
			while (*ptr)
				if (!isdigit ((int)*ptr++))
					break;
			if (*ptr == '\0') {
				*value = val;
				return True;
			}
		}
	return False;
}

void load_user_database ()
{
	if (!as_xrm_initialized)
		init_xrm ();

	destroy_user_database ();			/* just in case */

	if (XResourceManagerString (dpy) == NULL) {
		char *xdefaults_file = put_file_home ("~/.Xdefaults");

		if (!CheckFile (xdefaults_file)) {
			char *xenv = getenv ("XENVIRONMENT");

			show_warning ("Can't locate X resources database in \"%s\".",
										xdefaults_file);
			free (xdefaults_file);
			if (xenv == NULL)
				return;
			xdefaults_file = put_file_home (xenv);
			if (!CheckFile (xdefaults_file)) {
				show_warning ("Can't locate X resources database in \"%s\".",
											xdefaults_file);
				free (xdefaults_file);
				return;
			}
		}
		as_xrm_user_db = XrmGetFileDatabase (xdefaults_file);
		free (xdefaults_file);
	}
}

void destroy_user_database ()
{
	if (as_xrm_initialized && as_xrm_user_db != NULL) {
		XrmDestroyDatabase (as_xrm_user_db);
		as_xrm_user_db = NULL;
	}
}

