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

#include "../configure.h"

#define LOCAL_DEBUG
#include "asapp.h"
#include "afterstep.h"
#include "asdatabase.h"
#include "screen.h"
#include "functions.h"
#include "clientprops.h"
#include "hints.h"
#include "hints_private.h"

static ASFlagType
get_afterstep_resources (XrmDatabase db, ASStatusHints * status)
{
	ASFlagType found = 0;

	if (db != NULL && status) {
		if (read_int_resource
				(db, "afterstep*desk", "AS*Desk", &(status->desktop)))
			set_flags (found, AS_StartDesktop);
		if (read_int_resource
				(db, "afterstep*layer", "AS*Layer", &(status->layer)))
			set_flags (found, AS_StartLayer);
		if (read_int_resource
				(db, "afterstep*viewportx", "AS*ViewportX", &(status->viewport_x)))
			set_flags (found, AS_StartViewportX);
		if (read_int_resource
				(db, "afterstep*viewporty", "AS*ViewportY", &(status->viewport_y)))
			set_flags (found, AS_StartViewportY);
		set_flags (status->flags, found);
	}
	return found;
}

void
merge_command_line (ASHints * clean, ASStatusHints * status,
										ASRawHints * raw)
{
	static XrmOptionDescRec xrm_cmd_opts[] = {
		/* Want to accept -xrm "afterstep*desk:N" as options
		 * to specify the desktop. Have to include dummy options that
		 * are meaningless since Xrm seems to allow -x to match -xrm
		 * if there would be no ambiguity. */
		{"-xrnblahblah", NULL, XrmoptionResArg, (caddr_t) NULL},
		{"-xrm", NULL, XrmoptionResArg, (caddr_t) NULL},
	};

	if (raw == NULL)
		return;
	if (raw->wm_cmd_argc > 0 && raw->wm_cmd_argv != NULL) {
		XrmDatabase cmd_db = NULL;	/* otherwise it will try to use value as if it was database */
		ASFlagType found;

		init_xrm ();

		XrmParseCommand (&cmd_db, xrm_cmd_opts, 2, "afterstep",
										 &(raw->wm_cmd_argc), raw->wm_cmd_argv);
		if (status != NULL) {
			found = get_afterstep_resources (cmd_db, status);

			if (!get_flags (found, AS_StartDesktop)) {	/* checking for CDE workspace specification */
				if (read_int_resource
						(cmd_db, "*workspaceList", "*WorkspaceList",
						 &(status->desktop)))
					set_flags (status->flags, AS_StartDesktop);
			}
			XrmDestroyDatabase (cmd_db);
		}
		if (raw->wm_client_machine && clean)
			clean->client_host = text_property2string (raw->wm_client_machine);

		if (raw->wm_cmd_argc > 0 && clean) {
			int i;
			int len = 0;

			/* there still are some args left ! lets save it for future use : */
			/* first check to remove any geometry settings : */
			for (i = 0; i < raw->wm_cmd_argc; i++)
				if (raw->wm_cmd_argv[i]) {
					if (i + 1 < raw->wm_cmd_argc)
						if (raw->wm_cmd_argv[i + 1] != NULL) {
							register char *g = raw->wm_cmd_argv[i + 1];

							if (isdigit ((int)*g)
									|| ((*g == '-' || *g == '+') && isdigit ((int)*(g + 1))))
								if (mystrcasecmp (raw->wm_cmd_argv[i], "-g") == 0
										|| mystrcasecmp (raw->wm_cmd_argv[i],
																		 "-geometry") == 0) {
									raw->wm_cmd_argv[i] = NULL;
									raw->wm_cmd_argv[++i] = NULL;
									continue;
								}
						}
					len += strlen (raw->wm_cmd_argv[i]) + 1;
				}
			if (len > 0) {
				register char *trg, *src;

				if (clean->client_cmd)
					free (clean->client_cmd);
				trg = clean->client_cmd =
						safecalloc (1, len + raw->wm_cmd_argc * 2 + 1);
				for (i = 0; i < raw->wm_cmd_argc; i++)
					if ((src = raw->wm_cmd_argv[i]) != NULL) {
						register int k;
						Bool add_quotes = False;

						for (k = 0; src[k]; k++)
							if (isspace (src[k]) ||
									src[k] == '#' ||
									src[k] == '*' ||
									src[k] == '$' ||
									src[k] == ';' ||
									src[k] == '&' || src[k] == '<' || src[k] == '>'
									|| src[k] == '|' || iscntrl (src[k])) {
								add_quotes = True;
								break;
							}

						if (add_quotes) {
							trg[0] = '"';
							++trg;
						}
						for (k = 0; src[k]; k++)
							trg[k] = src[k];
						if (add_quotes) {
							trg[k] = '"';
							++k;
						}
						trg[k] = ' ';
						trg += k + 1;
					}
				if (trg > clean->client_cmd)
					trg--;
				*trg = '\0';
			}
		}
	}
}

void
merge_xresources_hints (ASHints * clean, ASRawHints * raw,
												ASDatabaseRecord * db_rec, ASStatusHints * status,
												ASFlagType what)
{
	XrmDatabase db = NULL;
	extern XrmDatabase as_xrm_user_db;
	char *rm_string;


	if (raw == NULL || status == NULL || !get_flags (what, HINT_STARTUP))
		return;

	init_xrm ();

	if ((rm_string = XResourceManagerString (dpy)) != NULL)
		db = XrmGetStringDatabase (rm_string);
	else
		db = as_xrm_user_db;

	if (db) {
		get_afterstep_resources (db, status);
		if (db != as_xrm_user_db)
			XrmDestroyDatabase (db);
	}
}

