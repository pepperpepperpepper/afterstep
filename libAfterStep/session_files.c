/*
 * Copyright (c) 2000 Sasha Vasko <sasha at aftercode.net>
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
 */

/*#define LOCAL_DEBUG */

#include "../configure.h"

#include <unistd.h>
#include <stdarg.h>
#include "asapp.h"
#include "afterstep.h"
#include "screen.h"
#include "functions.h"
#include "session.h"
#include "session_internal.h"

/* Session file/path accessors (get_session_file/make_session_* and the
 * session-override + file-list helpers), split out of session.c. The
 * desk-session helpers it relies on (get_desk_session, check_file) are
 * declared in session_internal.h. */

static const char *get_desk_file (ASDeskSession * d, int function)
{
	char *file = NULL;

	if (d)
		switch (function) {
		case F_CHANGE_BACKGROUND:
			file = d->background_file;
			break;
		case F_CHANGE_LOOK:
			file = d->look_file;
			break;
		case F_CHANGE_FEEL:
			file = d->feel_file;
			break;
		case F_CHANGE_THEME:
		case F_CHANGE_THEME_FILE:

			file = d->theme_file;
			break;
		case F_CHANGE_COLORSCHEME:
			file = d->colorscheme_file;
			break;
		}
	return file;
}

const char *get_session_file (ASSession * session, int desk, int function,
															Bool no_default)
{
	ASDeskSession *d = NULL;
	char *file = NULL;

	if (session) {
		/* backgrounds are not config files, and therefor cannot be overriden : */
		if (session->overriding_look && function == F_CHANGE_LOOK)
			return session->overriding_look;
		if (session->overriding_feel && function == F_CHANGE_FEEL)
			return session->overriding_feel;
		if (session->overriding_theme
				&& (function == F_CHANGE_THEME || function == F_CHANGE_THEME_FILE))
			return session->overriding_theme;
		if (session->overriding_colorscheme
				&& function == F_CHANGE_COLORSCHEME)
			return session->overriding_colorscheme;
		if (session->overriding_file && function != F_CHANGE_BACKGROUND)
			return session->overriding_file;

		switch (function) {
		case F_CHANGE_BACKGROUND:
			d = get_desk_session (session, desk);
			break;
#ifdef DIFFERENTLOOKNFEELFOREACHDESKTOP
		case F_CHANGE_LOOK:
		case F_CHANGE_FEEL:
		case F_CHANGE_THEME:
		case F_CHANGE_THEME_FILE:
		case F_CHANGE_COLORSCHEME:
			d = get_desk_session (session, desk);
			break;
#else
		case F_CHANGE_LOOK:
		case F_CHANGE_FEEL:
		case F_CHANGE_THEME:
		case F_CHANGE_THEME_FILE:
		case F_CHANGE_COLORSCHEME:
			d = session->defaults;
			break;
#endif
		}
		if (d) {
			file = (char *)get_desk_file (d, function);
			LOCAL_DEBUG_OUT ("file for desk %d is \"%s\"", desk,
											 file ? "" : file);
			if (file != NULL)
				if (CheckFile (file) != 0)
					file = NULL;
			/* fallback to defaults */
			if (file == NULL && d != session->defaults && !no_default) {
				file = (char *)get_desk_file (session->defaults, function);
				LOCAL_DEBUG_OUT ("default file is \"%s\"", file);
				if (file != NULL)
					if (CheckFile (file) != 0)
						file = NULL;
			}
		}
	}
	return file;
}

const char *get_session_ws_file (ASSession * session,
																 Bool only_if_available)
{																/* workspace_state filename */
	if (session == NULL)
		return NULL;
	if (session && only_if_available)
		if (CheckFile (session->workspace_state) != 0)
			return NULL;
	return session->workspace_state;
}

char *make_session_apps_path (ASSession * session)
{
	char *apps_path = NULL;
	int len = 0;
	char *priv_apps, *shared_apps;

	if (session == NULL)
		return NULL;

	priv_apps = (char *)make_file_name (session->ashome, AFTERSTEP_APPS_DIR);
	if (check_file_mode (priv_apps, S_IFDIR) != 0)
		destroy_string (&priv_apps);
	else
		len += strlen (priv_apps);
	shared_apps =
			(char *)make_file_name (session->asshare, AFTERSTEP_APPS_DIR);
	if (check_file_mode (shared_apps, S_IFDIR) != 0)
		destroy_string (&shared_apps);
	else {
		if (len > 0)
			++len;
		len += strlen (shared_apps);
	}

	if (len > 0) {
		apps_path = safemalloc (len + 1);
		if (priv_apps && shared_apps)
			sprintf (apps_path, "%s:%s", priv_apps, shared_apps);
		else if (priv_apps)
			strcpy (apps_path, priv_apps);
		else if (shared_apps)
			strcpy (apps_path, shared_apps);

		destroy_string (&priv_apps);
		destroy_string (&shared_apps);
	}

	return apps_path;
}

static inline char *make_session_filedir (ASSession * session,
																					const char *source,
																					Bool use_depth, int mode)
{
	char *realfilename = NULL;

	if (session->overriding_file)
		return mystrdup (session->overriding_file);

	if (source) {
		char *filename = (char *)source;

		if (session->scr->screen != 0) {
			filename = safemalloc (strlen ((char *)source) + 1 + 32 + 32);
			if (use_depth)
				sprintf (filename, "%s.scr%ld.%dbpp", source, session->scr->screen,
								 session->colordepth);
			else
				sprintf (filename, "%s.scr%ld", source, session->scr->screen);

			realfilename = (char *)make_file_name (session->ashome, filename);
			free (filename);
			filename = (char *)source;
		}
		if (realfilename == NULL || check_file_mode (realfilename, mode) != 0) {
			if (use_depth) {
				filename = safemalloc (strlen ((char *)source) + 1 + 32);
				sprintf (filename, "%s.%dbpp", source, session->colordepth);
			}
			if (realfilename)
				free (realfilename);
			realfilename = (char *)make_file_name (session->ashome, filename);
			if (check_file_mode (realfilename, mode) != 0) {
				free (realfilename);
				realfilename = make_file_name (session->asshare, filename);
				if (check_file_mode (realfilename, mode) != 0) {
					free (realfilename);
					realfilename = NULL;
				}
			}
		}
		if (filename != source)
			free (filename);
	}

	return realfilename;
}

char *make_session_file (ASSession * session, const char *source,
												 Bool use_depth)
{
	return make_session_filedir (session, source, use_depth, S_IFREG);
}

char *make_session_dir (ASSession * session, const char *source,
												Bool use_depth)
{
	return make_session_filedir (session, source, use_depth, S_IFDIR);
}

char *make_session_data_file (ASSession * session, Bool shared,
															int if_mode_only, ...)
{
	char *realfilename = NULL;
	va_list ap;
	int len = 0;
	register int i;
	register char *ptr;

	if (session == NULL)
		return NULL;

	va_start (ap, if_mode_only);
	while ((ptr = va_arg (ap, char *)) != NULL) {
		for (i = 0; ptr[i] != '\0'; i++) ;
		len += i + 1;
	}
	va_end (ap);

	ptr = shared ? session->asshare : session->ashome;

	realfilename = safemalloc (strlen (ptr) + 1 + len);
	for (i = 0; ptr[i] != '\0'; i++)
		realfilename[i] = ptr[i];

	if (len == 0) {
		realfilename[i] = '\0';
		return realfilename;
	}

	if (i > 0)
		realfilename[i++] = '/';

	va_start (ap, if_mode_only);
	while ((ptr = va_arg (ap, char *)) != NULL) {
		register int k = 0;

		while (ptr[k])
			realfilename[i++] = ptr[k++];
		realfilename[i++] = '/';
	}
	va_end (ap);

	realfilename[--i] = '\0';

	if (if_mode_only != 0)
		if (check_file_mode (realfilename, if_mode_only) != 0) {
			free (realfilename);
			realfilename = NULL;
		}
	return realfilename;
}

char *make_session_rc_file (ASSession * session, const char *tmpl)
{

	if (tmpl == NULL)
		return NULL;

	if (tmpl[0] == '/' || tmpl[0] == '$' || tmpl[0] == '~')
		return copy_replace_envvar (tmpl);

	return make_session_data_file (session, False, 0, tmpl, NULL);
}

char *make_session_webcache_file (ASSession * session, const char *url)
{
	char *fullfilename = NULL;

	if (url != NULL && session && session->webcache) {
		int len = 0, i;
		char *escapedUrl;

		for (i = 0; url[i]; ++i) {
			if (url[i] == '_')
				++len;
			++len;
		}

		escapedUrl = safemalloc (len + 1);
		len = 0;
		for (i = 0; url[i]; ++i) {
			if (url[i] == '_')
				escapedUrl[len++] = '_';
			if (url[i] != '.' && url[i] != '_' && !isalnum (url[i]))
				escapedUrl[len++] = '_';
			else
				escapedUrl[len++] = url[i];
		}
		escapedUrl[len] = '\0';

		fullfilename = make_file_name (session->webcache, escapedUrl);
		free (escapedUrl);
	}
	return fullfilename;
}


void
set_session_override (ASSession * session, const char *overriding_file,
											int function)
{
	if (session) {
		char **target = &(session->overriding_file);

		if (function == F_CHANGE_LOOK)
			target = &(session->overriding_look);
		else if (function == F_CHANGE_FEEL)
			target = &(session->overriding_feel);
		else if (function == F_CHANGE_THEME || function == F_CHANGE_THEME_FILE)
			target = &(session->overriding_theme);
		else if (function == F_CHANGE_COLORSCHEME)
			target = &(session->overriding_colorscheme);

		if (*target) {
			free (*target);
			*target = NULL;
		}
		if (overriding_file)
			*target = check_file (overriding_file);
	}
}

inline const char *get_session_override (ASSession * session, int function)
{
	if (session) {
		if (session->overriding_file)
			return session->overriding_file;
		else if (function == F_CHANGE_LOOK)
			return session->overriding_look;
		else if (function == F_CHANGE_FEEL)
			return session->overriding_feel;
		else if (function == F_CHANGE_THEME || function == F_CHANGE_THEME_FILE)
			return session->overriding_theme;
		else if (function == F_CHANGE_COLORSCHEME)
			return session->overriding_colorscheme;
	}
	return NULL;
}

char **get_session_file_list (ASSession * session, int desk1, int desk2,
															int function)
{
	char **list = NULL;

	if (session) {
		register int i;

		if (desk1 > desk2) {
			i = desk2;
			desk2 = desk1;
			desk1 = i;
		}
		list = safecalloc ((desk2 - desk1) + 1, sizeof (char *));
		for (i = desk1; i <= desk2; i++)
			list[i - desk1] =
					(char *)get_session_file (session, i, function, False);
	}
	return list;
}

/*************************************************************************************/
ASSession *GetNCASSession (ScreenInfo * scr, const char *home,
													 const char *share)
{
	ASSession *session = NULL;
	char *ashome = put_file_home (home ? home : as_afterstep_dir_name);
	char *asshare = put_file_home (share ? share : as_share_dir_name);

	if (scr == NULL)
		scr = ASDefaultScr;

	check_AfterStep_dirtree (ashome, True);

	session = create_assession (scr, ashome, asshare);

#ifdef DIFFERENTLOOKNFEELFOREACHDESKTOP
	/* TODO : add check of non-cf dir for desktop specific configs : */
#endif
	session->scr = scr;

	return session;
}
