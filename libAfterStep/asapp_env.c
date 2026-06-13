/****************************************************************************
 *
 * Copyright (c) 1999 Sasha Vasko <sasha at aftercode.net>
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
 ****************************************************************************/

#define LOCAL_DEBUG
#include "../configure.h"

#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#include "asapp.h"
#include "asapp_internal.h"
#include "afterstep.h"
#include "parser.h"
#include "screen.h"
#include "functions.h"
#include "session.h"
#include "balloon.h"
#include "mystyle.h"
#include "mylook.h"
#include "wmprops.h"
#include "desktop_category.h"
#include "../libAfterImage/asimage.h"
#include "../libAfterImage/xpm.h"
#include "../libAfterImage/char2uni.h"

/* Tool/browser/editor discovery, themed-icon and environment-image
 * loading, default-environment construction and desktop-category lookups,
 * split out of asapp.c. */

void free_func_hash ()
{
	if (FuncSyntax.term_hash) {
		FreeSyntaxHash (&FuncSyntax);
	}
}

/*********** end command line parsing **************************/
static char *_as_known_terms[] = {
	"$TERMINAL",
	"x-terminal-emulator",
	"xterm",
	"uxterm",
	"alacritty",
	"kitty",
	"foot",
	"st",
	"konsole",
	"xfce4-terminal",
	"lxterminal",
	"terminator",
	"mate-terminal",
	"tilix",
	"gnome-terminal",
	"urxvt",
	"aterm",
	"rxvt",
	"Eterm",
	NULL
};

static char *_as_known_browsers[] = {
	"$BROWSER",
	"sensible-browser",
	"x-www-browser",							/* don't like default debian selection of konqueror */
	"firefox",
	"mozilla-firefox",
	"mozilla",
	"opera",
	NULL
};

static char *_as_known_editors[] = {
	"editor",
	"$EDITOR",
	"nedit",
	"xemacs",
	"gedit",
	"kedit",
	"kate",
	NULL
};

static char **_as_known_tools[ASTool_Count] = {
	_as_known_terms,
	_as_known_browsers,
	_as_known_editors
};

static char *_as_tools_name[ASTool_Count] = {
	"Terminal",
	"Browser",
	"Editor"
};


char *as_get_default_tool (ASToolType type)
{
	int i;

	for (i = 0; _as_known_tools[type][i]; ++i) {
		char *tmp = _as_known_tools[type][i];
		char *fullname = NULL;
		int res;

		if (tmp[0] == '$')
			tmp = copy_replace_envvar (tmp);
		res = get_executable_in_path (tmp, &fullname);
		if (tmp != _as_known_tools[type][i])
			free (tmp);
		if (res > 0)
			return fullname;
	}
	return NULL;
}

void
set_environment_tool_from_list (ASEnvironment * e, ASToolType type,
																char **list, int list_len)
{
	int i;

	destroy_string (&(e->tool_command[type]));
	for (i = 0; i < list_len; ++i)
		if (list[i]) {
			char *tmp = list[i];
			char *fullname = NULL;

			if (tmp[0] == '$')
				tmp = copy_replace_envvar (tmp);
			if (get_executable_in_path (tmp, &fullname)) {
				e->tool_command[type] = fullname;
				break;
			} else
				show_warning ("%s command %s is not in the path",
											_as_tools_name[type], tmp);
			if (tmp != list[i])
				free (tmp);
		}
	if (e->tool_command[type] == NULL)
		e->tool_command[type] = as_get_default_tool (type);
	show_progress ("%s is set to: \"%s\"", _as_tools_name[type],
								 e->tool_command[type] ? e->tool_command[type] : "none");
}

static char *make_themed_icon_category_path (const char *category,
																						 int desired_size_idx,
																						 Bool fallback)
{
	char *tmp, *tmp2, *theme_path;
	static char *standard_sizes[] =
			{ "16x16", "32x32", "48x48", "64x64", "128x128", NULL };

	if (!fallback && Environment->IconTheme == NULL)
		return NULL;
	if (fallback && Environment->IconThemeFallback == NULL)
		return NULL;

	tmp =
			make_file_name (fallback ? Environment->
											IconThemeFallback : Environment->IconTheme,
											standard_sizes[desired_size_idx]);
	tmp2 = make_file_name (Environment->IconThemePath, tmp);
	free (tmp);
	theme_path = make_file_name (tmp2, category);
	free (tmp2);

	return theme_path;
}

Bool is_themable_icon (const char *name)
{
	return (Environment && Environment->IconTheme
					&& Environment->IconThemePath && strchr (name, '/') == NULL);
}

ASImage *load_environment_icon (const char *category, const char *name,
																int desired_size)
{
	ASImage *icon = NULL;
	char *png_name = NULL;
	char *svg_name = NULL;
	Bool add_to_man = False;
	int len;
	int desired_size_idx;

	if (name == NULL || ASDefaultScr == NULL)
		return NULL;

	desired_size_idx = (desired_size + 15) / 16 - 1;
	if (desired_size_idx > 4)
		desired_size_idx = 4;
	else if (desired_size_idx < 0)
		desired_size_idx = 0;

	show_debug (__FILE__, __FUNCTION__, __LINE__, "IconTheme = %s",
							Environment->IconTheme);

	len = strlen (name);
	if (len < 4 || name[len - 4] != '.') {
		png_name = add_file_extension (name, "png");
		svg_name = add_file_extension (name, "svg");
	}
	/* we maybe already loaded so try with just the name : */
	icon = fetch_asimage (ASDefaultScr->image_manager, name);

	/* nope we are not, so try loading as themed icon first: */
	if (icon == NULL && is_themable_icon (name) && category) {
		int fallback;
		for (fallback = 0; fallback <= 1; fallback++) {
			int try, try_size_idx = desired_size_idx;
			for (try = 0; icon == NULL && try < 3; ++try) {
				char *theme_path =
						make_themed_icon_category_path (category, try_size_idx,
																						fallback);
				if (theme_path == NULL)
					break;

				if (icon == NULL && png_name) {
					char *themed_name = make_file_name (theme_path, png_name);
					icon =
							get_asimage_quiet (ASDefaultScr->image_manager, themed_name,
																 ASFLAGS_EVERYTHING, 100);
					free (themed_name);
				}
				if (icon == NULL && svg_name) {
					char *themed_name = make_file_name (theme_path, svg_name);
					icon =
							get_asimage_quiet (ASDefaultScr->image_manager, themed_name,
																 ASFLAGS_EVERYTHING, 100);
					free (themed_name);
				}
				if (icon == NULL) {
					char *themed_name = make_file_name (theme_path, name);
					icon =
							get_asimage_quiet (ASDefaultScr->image_manager, themed_name,
																 ASFLAGS_EVERYTHING, 100);
					free (themed_name);
				}
				free (theme_path);
				switch (try) {
				case 0:
					try_size_idx =
							desired_size_idx <
							4 ? desired_size_idx + 1 : desired_size_idx - 1;
					break;
				case 1:
					try_size_idx = desired_size_idx == 0
							|| desired_size_idx == 4 ? 2 : desired_size_idx - 1;
					break;
				default:
					break;
				}
			}
		}
		add_to_man = True;
	}

	/* finally, try loading straight (for native AS icons primarily): */
	if (icon == NULL) {
		icon =
				get_asimage_quiet (ASDefaultScr->image_manager, name,
													 ASFLAGS_EVERYTHING, 100);
		if (icon == NULL) {
			add_to_man = True;
			if (png_name)
				icon =
						get_asimage_quiet (ASDefaultScr->image_manager, png_name,
															 ASFLAGS_EVERYTHING, 100);
			if (icon == NULL && svg_name)
				icon =
						get_asimage_quiet (ASDefaultScr->image_manager, svg_name,
															 ASFLAGS_EVERYTHING, 100);
		}
	}

	if (icon && add_to_man && icon->ref_count == 1) {
		forget_asimage (icon);
		if (icon->imageman == NULL)
			store_asimage (ASDefaultScr->image_manager, icon, name);
	}
	if (png_name)
		free (png_name);
	if (svg_name)
		free (svg_name);
	return icon;
}

ASImage *load_environment_icon_any (const char *filename, int desired_size)
{
	ASImage *icon = NULL;
	if (filename) {
		Bool possibly_themed = is_themable_icon (filename);
		icon = load_environment_icon ("apps", filename, desired_size);
		if (!icon && possibly_themed)
			icon = load_environment_icon ("actions", filename, desired_size);
		if (!icon && possibly_themed)
			icon = load_environment_icon ("places", filename, desired_size);
		if (!icon && possibly_themed)
			icon = load_environment_icon ("categories", filename, desired_size);
		if (!icon && possibly_themed)
			icon = load_environment_icon ("devices", filename, desired_size);
	}
	return icon;
}

ASEnvironment *make_default_environment ()
{
	int i;
	ASEnvironment *e = safecalloc (1, sizeof (ASEnvironment));
	static const char *default_pixmap_path_format =
			"%s/desktop/icons/:"
			"%s/desktop/icons/:"
			"%s/desktop/:"
			"%s/desktop/:" "%s/desktop/buttons/:" "%s/desktop/buttons/:"
			"%s/backgrounds/:" "%s/backgrounds/:" "%s";

	static const char *default_font_path_format =
			"%s/desktop/fonts/:" "%s/desktop/fonts/:"
			"/usr/share/fonts/default/TrueType/:" "%s";

	static const char *default_cursor_path_format =
			"%s/desktop/cursors:" "%s/desktop/cursors";

	e->desk_scale = 24;
	e->desk_pages_h = 2;
	e->desk_pages_v = 2;
	e->module_path = mystrdup (AFTER_BIN_DIR);
	e->icon_path = mystrdup (DEFAULT_ICON_DIR);
	e->pixmap_path =
			safemalloc (strlen ((char *)default_pixmap_path_format) +
									strlen (AFTER_DIR) * 4 + strlen (AFTER_SHAREDIR) * 4 +
									strlen (DEFAULT_PIXMAP_DIR) + 1);
	sprintf (e->pixmap_path, default_pixmap_path_format, AFTER_DIR,
					 AFTER_SHAREDIR, AFTER_DIR, AFTER_SHAREDIR, AFTER_DIR,
					 AFTER_SHAREDIR, AFTER_DIR, AFTER_SHAREDIR, DEFAULT_PIXMAP_DIR);

	e->font_path = safemalloc (strlen ((char *)default_font_path_format) +
														 strlen (AFTER_DIR) + strlen (AFTER_SHAREDIR) +
														 strlen (DEFAULT_TTF_DIR) + 1);
	sprintf (e->font_path, default_font_path_format, AFTER_DIR,
					 AFTER_SHAREDIR, DEFAULT_TTF_DIR);

	e->cursor_path =
			safemalloc (strlen ((char *)default_cursor_path_format) +
									strlen (AFTER_DIR) + strlen (AFTER_SHAREDIR) + 1);
	sprintf (e->cursor_path, default_cursor_path_format, AFTER_DIR,
					 AFTER_SHAREDIR);

	for (i = 0; i < ASTool_Count; ++i)
		e->tool_command[i] = as_get_default_tool (i);

	/* by default - don't do overwrite gtkrc files so to not aggrave people */
	e->gtkrc_path = NULL;					/* make_session_rc_file(Session, GTKRC_FILE); */
	e->gtkrc20_path = NULL;				/* make_session_rc_file(Session, GTKRC20_FILE) ; */
	e->IconTheme = mystrdup ("oxygen");
	e->IconThemePath = mystrdup ("/usr/chare/icons");
	e->IconThemeFallback = mystrdup ("hicolor");
	return e;
}

void destroy_asenvironment (ASEnvironment ** penv)
{
	if (penv) {
		ASEnvironment *e = *penv;

		if (e) {
			int i;

			if (e->module_path)
				free (e->module_path);
			if (e->sound_path)
				free (e->sound_path);
			if (e->icon_path)
				free (e->icon_path);
			if (e->pixmap_path)
				free (e->pixmap_path);
			if (e->font_path)
				free (e->font_path);
			if (e->cursor_path)
				free (e->cursor_path);
			for (i = 0; i < ASTool_Count; ++i)
				destroy_string (&(e->tool_command[i]));

			destroy_string (&(e->gtkrc_path));
			destroy_string (&(e->gtkrc20_path));
			destroy_string (&(e->IconTheme));
			destroy_string (&(e->IconThemePath));
			destroy_string (&(e->IconThemeFallback));

			free (e);
			*penv = NULL;
		}
	}
}

/*
 * Initialize database variables
 */

void destroy_asdatabase ()
{
	if (Database)
		destroy_asdb (&Database);
	/* XResources : */
	destroy_user_database ();
}

static ASCategoryTree *name2desktop_category_tree (const char *name,
																									 int *tree_name_len)
{
	ASCategoryTree *ct = CombinedCategories;
	int offset = 0;

/*	fprintf( stderr, __FUNCTION__ ": checking \"%s\" (AfterSTep categories = %p)\n", name, AfterStepCategories );*/

	if (!mystrncasecmp (name, "AfterStep:", 10)) {
		ct = AfterStepCategories;
		offset = 10;
	} else if (!mystrncasecmp (name, "KDE:", 4)) {
		ct = KDECategories;
		offset = 4;
	} else if (!mystrncasecmp (name, "GNOME:", 6)) {
		ct = GNOMECategories;
		offset = 6;
	} else if (!mystrncasecmp (name, "OTHER:", 7)) {
		ct = OtherCategories;
		offset = 7;
	} else if (!mystrncasecmp (name, "COMBINED:", 9)) {
		ct = CombinedCategories;
		offset = 9;
	}
	if (tree_name_len)
		*tree_name_len = offset;
	return ct;
}

ASDesktopCategory *name2desktop_category (const char *name,
																					ASCategoryTree ** tree_return)
{
	int offset = 0;
	ASCategoryTree *ct = name2desktop_category_tree (name, &offset);

	if (tree_return)
		*tree_return = ct;

	return fetch_desktop_category (ct, name + offset);
}

ASDesktopEntry *name2desktop_entry (const char *name,
																		ASCategoryTree ** tree_return)
{
	int offset = 0;
	ASCategoryTree *ct = name2desktop_category_tree (name, &offset);

	if (tree_return)
		*tree_return = ct;

	return fetch_desktop_entry (ct, name + offset);
}

void InitSession ()
{
	/* initializing our dirs names */
	if (Session == NULL) {
		Session =
				GetNCASSession (ASDefaultScr, as_app_args.override_home,
												as_app_args.override_share);
		if (as_app_args.override_config)
			set_session_override (Session, as_app_args.override_config, 0);
		if (as_app_args.override_look)
			set_session_override (Session, as_app_args.override_look,
														F_CHANGE_LOOK);
		if (as_app_args.override_feel)
			set_session_override (Session, as_app_args.override_feel,
														F_CHANGE_FEEL);
	}
}

void free_as_app_args ()
{
	int i;

	for (i = 0; i < as_app_args.saved_argc; ++i)
		if (as_app_args.saved_argv[i])
			free (as_app_args.saved_argv[i]);
	free (as_app_args.saved_argv);
	as_app_args.saved_argv = NULL;

	destroy_string (&(as_app_args.locale));

}

void FreeMyAppResources ()
{
	cleanup_default_balloons ();
	destroy_asdatabase ();
	mystyle_destroy_all ();
	mylook_init (&(ASDefaultScr->Look), True, ASFLAGS_EVERYTHING);
	destroy_image_manager (ASDefaultScr->image_manager, False);
	destroy_font_manager (ASDefaultScr->font_manager, False);
	clientprops_cleanup ();
	destroy_wmprops (ASDefaultScr->wmprops, False);
	wmprops_cleanup ();
	free_func_hash ();
	flush_keyword_ids ();
	purge_asimage_registry ();
	asxml_var_cleanup ();
	custom_color_cleanup ();
	build_xpm_colormap (NULL);
	destroy_screen_gcs (ASDefaultScr);
	if (ASDefaultScr->RootImage) {
		safe_asimage_destroy (ASDefaultScr->RootImage);
		ASDefaultScr->RootImage = NULL;
	}
	destroy_asvisual (ASDefaultScr->asv, False);
	free_as_app_args ();
	destroy_assession (Session);
	Session = NULL;
	destroy_asenvironment (&Environment);
	is_executable_in_path (NULL);
#ifdef XSHMIMAGE
	flush_shm_cache ();
#endif
	free (ASDefaultScr);
	flush_default_asstorage ();
	flush_asbidirlist_memory_pool ();
	flush_ashash_memory_pool ();

}


