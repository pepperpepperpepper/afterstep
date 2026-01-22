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

/********************************************************************************/
/* session/workspace persistence */

struct SaveWindowAuxData {
	char this_host[MAXHOSTNAME];
	FILE *f;
	ASHashTable *res_name_counts;
	Bool only_modules;
	ASWindowGroup *group;
};

int get_res_name_count (ASHashTable * res_name_counts, char *res_name)
{
	ASHashData hdata;

	if (res_name == NULL || res_name_counts == NULL)
		return 0;
	if (get_hash_item (res_name_counts, AS_HASHABLE (res_name), &hdata.vptr)
			== ASH_Success) {
		int val = *(hdata.iptr);
		++val;
		*(hdata.iptr) = val;
		return val;
	} else {
		hdata.iptr = safemalloc (sizeof (int));
		*(hdata.iptr) = 1;
		add_hash_item (res_name_counts, AS_HASHABLE (mystrdup (res_name)),
									 hdata.vptr);
	}
	return 1;
}

Bool check_aswindow_name_unique (char *name, ASWindow * asw)
{
	ASBiDirElem *e = LIST_START (Scr.Windows->clients);
	while (e != NULL) {
		ASWindow *curr = (ASWindow *) LISTELEM_DATA (e);
		if (curr != asw && strcmp (ASWIN_NAME (curr), name) == 0)
			return False;
		LIST_GOTO_NEXT (e);
	}
	return True;
}

// ask for program command line
char *pid2cmd (int pid)
{
#define MAX_CMDLINE_SIZE 2048
	FILE *f;
	static char buf[MAX_CMDLINE_SIZE];
	static char path[128];
	char *cmd = NULL;

	sprintf (path, "/proc/%d/cmdline", pid);
	if ((f = fopen (path, "r")) != NULL) {
		if (fgets (buf, MAX_CMDLINE_SIZE, f) != NULL) {
			buf[MAX_CMDLINE_SIZE - 1] = '\0';
			cmd = mystrdup (&buf[0]);
		}
		fclose (f);
	}
	return cmd;
}

static
char *filter_out_geometry (char *cmd_args, char **geom_keyword,
													 char **original_size)
{
	char *clean = mystrdup (cmd_args);
	char *token_start;
	char *token_end = clean;

	do {
		token_start = token_end;

		if (token_start[0] == '-') {
			if (strncmp (token_start, "-g ", 3) == 0 ||
					strncmp (token_start, "-geometry ", 10) == 0 ||
					strncmp (token_start, "--geometry ", 11) == 0) {
				unsigned int width = 0;
				unsigned int height = 0;
				int flags = 0;
				char *geom_start = token_start;
				if (geom_keyword)
					*geom_keyword = tokencpy (geom_start);
				token_end = tokenskip (token_start, 1);
				if (token_end > token_start) {
					token_start = token_end;
					token_end =
							parse_geometry (token_start, NULL, NULL, &width, &height,
															&flags);
					if (token_end > token_start) {
						int i = 0;
						for (i = 0; token_end[i] != '\0'; ++i)
							geom_start[i] = token_end[i];
						geom_start[i] = '\0';
					}
				}
					if (original_size && get_flags (flags, WidthValue | HeightValue)) {
						char *tmp = *original_size = safemalloc (64);
						tmp[0] = '\0';
						if (get_flags (flags, WidthValue) && width > 0) {
							sprintf (tmp, "%d", width);
							while (*tmp)
								++tmp;
					}
					if (get_flags (flags, HeightValue) && height > 0)
						sprintf (tmp, "x%d", height);
				}
				break;
			}
		}
		token_end = tokenskip (token_start, 1);
	} while (token_end > token_start);
	return clean;
}

static void stripreplace_geometry_size (char **pgeom, char *original_size)
{
	char *geom = *pgeom;
	if (geom) {

		int i = 0;
		if (isdigit (geom[0]))
			while (geom[++i])
				if (geom[i] == '+' || geom[i] == '-')
					break;
		if (original_size != NULL || i > 0) {
			int size = strlen (&geom[i]);
			if (original_size)
				size += strlen (original_size);
			*pgeom = safemalloc (size + 1);
			if (original_size)
				sprintf (*pgeom, "%s%s", original_size, &geom[i]);
			else
				strcpy (*pgeom, &geom[i]);
			free (geom);
		}
	}
}

static char *make_application_name (ASWindow * asw, char *rclass,
																		char *rname)
{
	char *name = ASWIN_NAME (asw);
	/* need to check if we can use window name in the pattern. It has to be :
	 * 1) Not changed since window initial mapping
	 * 2) all ASCII
	 * 3) shorter then 80 chars
	 * 4) must not match class or res_name
	 * 5) Unique
	 */
	if (name == rname || name == rclass)
		name = NULL;
	else if (name != NULL && get_flags (asw->internal_flags, ASWF_NameChanged)) {	/* allow changed names only for terms, as those could launch sub app inside */
		int rclass_len = rclass ? strlen (rclass) : 0;
		if (rclass_len != 5)
			name = NULL;
		else if (strstr (rclass + 1, "term") != 0)
			name = NULL;
	}
	if (name) {
		int i = 0;
		while (name[i] != '\0') {		/* we do not want to have path in names as well */
			if (!isalnum (name[i]) && !isspace (name[i]))
				break;
			if (++i >= 80)
				break;
		}
		if (name[i] != '\0')
			name = NULL;
		else {
			if (strcmp (rclass, name) == 0 || strcmp (rname, name) == 0)
				name = NULL;
			else /* check that its unique */
				if (!check_aswindow_name_unique (name, asw))
				name = NULL;
		}
	}
	return name;
}

static void do_save_aswindow (ASWindow * asw,
															struct SaveWindowAuxData *swad)
{
	Bool same_host = (asw->hints->client_host == NULL
										|| mystrcasecmp (asw->hints->client_host, swad->this_host) == 0);
	if (asw->hints->client_cmd == NULL && same_host) {
		if (ASWIN_HFLAGS (asw, AS_PID) && asw->hints->pid > 0)
			asw->hints->client_cmd = pid2cmd (asw->hints->pid);

	}
	LOCAL_DEBUG_OUT ("same_host = %d, client_smd = \"%s\"", same_host,
									 asw->hints->client_cmd ? asw->hints->
									 client_cmd : "(null)");

	if (asw->hints->client_cmd != NULL && same_host) {
		char *pure_geometry = NULL;
		char *geom =
				make_client_geometry_string (ASDefaultScr, asw->hints, asw->status,
																		 &(asw->anchor), Scr.Vx, Scr.Vy,
																		 &pure_geometry);
		/* format :   [<res_class>]:[<res_name>]:[[#<seq_no>]|<name>]  */
		int app_no =
				get_res_name_count (swad->res_name_counts, asw->hints->res_name);
		char *rname = asw->hints->res_name ? asw->hints->res_name : "*";
		char *rclass = asw->hints->res_class ? asw->hints->res_class : "*";
		char *name = ASWIN_NAME (asw);
		char *app_name = "*";
		char *cmd_app = NULL, *cmd_args;
		Bool supports_geometry = False;
		char *geometry_keyword = NULL;
		char *clean_cmd_args = NULL;
		char *original_size = NULL;

		name = make_application_name (asw, rclass, rname);

		if (name == NULL) {
			app_name =
					safemalloc (strlen (rclass) + 1 + strlen (rname) + 1 + 1 + 15 +
											1);
			sprintf (app_name, "%s:%s:#%d", rclass, rname, app_no);
		} else {
			app_name =
					safemalloc (strlen (rclass) + 1 + strlen (rname) + 1 +
											strlen (name) + 1);
			sprintf (app_name, "%s:%s:%s", rclass, rname, name);
		}

		cmd_args = parse_token (asw->hints->client_cmd, &cmd_app);
		if (cmd_app != NULL)				/* we want -geometry to be the first arg, so that terms could correctly launch app with -e arg */
			clean_cmd_args =
					filter_out_geometry (cmd_args, &geometry_keyword,
															 &original_size);

		if (geometry_keyword == NULL && ASWIN_HFLAGS (asw, AS_Module))
			geometry_keyword = mystrdup ("--geometry");

		if (geometry_keyword == NULL) {
#if 0														/* this does more bad then good */
			geometry_keyword = mystrdup ("-geometry");
#endif
		} else
			supports_geometry = True;

		if (!ASWIN_HFLAGS (asw, AS_Handles)) {	/* we want to remove size from geometry here,
																						 * unless it was requested in original cmd-line geometry */
			stripreplace_geometry_size (&geom, original_size);
		}

		fprintf (swad->f, "\tExec \"I:%s\" %s", app_name,
						 cmd_app ? cmd_app : asw->hints->client_cmd);
		if (geometry_keyword)
			fprintf (swad->f, " %s %s", geometry_keyword, geom);
#if 0
		if (asw->group && asw->group->sm_client_id)
			fprintf (swad->f, " --sm-client-id %s", asw->group->sm_client_id);
#endif
		if (cmd_app != NULL) {
			fprintf (swad->f, " %s", clean_cmd_args);
			destroy_string (&cmd_app);
		}
		fprintf (swad->f, " &\n");


		destroy_string (&clean_cmd_args);
		destroy_string (&geometry_keyword);
		destroy_string (&original_size);

		if (ASWIN_HFLAGS (asw, AS_Module)) {
			fprintf (swad->f, "\tWait \"I:%s\" Layer %d\n", app_name,
							 ASWIN_LAYER (asw));
		} else if (ASWIN_GET_FLAGS (asw, AS_Sticky)) {
			if (supports_geometry)
				fprintf (swad->f, "\tWait \"I:%s\" Layer %d"
								 ", Sticky"
								 ", StartsOnDesk %d"
								 ", %s"
								 "\n",
								 app_name, ASWIN_LAYER (asw),
								 ASWIN_DESK (asw),
								 ASWIN_GET_FLAGS (asw,
																	AS_Iconic) ? "StartIconic" :
								 "StartNormal");
			else
				fprintf (swad->f, "\tWait \"I:%s\" DefaultGeometry %s"
								 ", Layer %d"
								 ", Sticky"
								 ", StartsOnDesk %d"
								 ", %s"
								 "\n",
								 app_name, pure_geometry,
								 ASWIN_LAYER (asw),
								 ASWIN_DESK (asw),
								 ASWIN_GET_FLAGS (asw,
																	AS_Iconic) ? "StartIconic" :
								 "StartNormal");
		} else {
			if (supports_geometry)
				fprintf (swad->f, "\tWait \"I:%s\" Layer %d"
								 ", Slippery"
								 ", StartsOnDesk %d"
								 ", ViewportX %d"
								 ", ViewportY %d"
								 ", %s"
								 "\n",
								 app_name, ASWIN_LAYER (asw),
								 ASWIN_DESK (asw),
								 ((asw->status->x +
									 asw->status->viewport_x) / Scr.MyDisplayWidth) *
								 Scr.MyDisplayWidth,
								 ((asw->status->y +
									 asw->status->viewport_y) / Scr.MyDisplayHeight) *
								 Scr.MyDisplayHeight, ASWIN_GET_FLAGS (asw,
																											 AS_Iconic) ?
								 "StartIconic" : "StartNormal");
			else
				fprintf (swad->f, "\tWait \"I:%s\" DefaultGeometry %s"
								 ", Layer %d"
								 ", Slippery"
								 ", StartsOnDesk %d"
								 ", ViewportX %d"
								 ", ViewportY %d"
								 ", %s"
								 "\n",
								 app_name, pure_geometry,
								 ASWIN_LAYER (asw),
								 ASWIN_DESK (asw),
								 ((asw->status->x +
									 asw->status->viewport_x) / Scr.MyDisplayWidth) *
								 Scr.MyDisplayWidth,
								 ((asw->status->y +
									 asw->status->viewport_y) / Scr.MyDisplayHeight) *
								 Scr.MyDisplayHeight, ASWIN_GET_FLAGS (asw,
																											 AS_Iconic) ?
								 "StartIconic" : "StartNormal");
		}
		destroy_string (&pure_geometry);
		destroy_string (&geom);
		destroy_string (&app_name);
	}
}

Bool make_aswindow_cmd_iter_func (void *data, void *aux_data)
{
	struct SaveWindowAuxData *swad = (struct SaveWindowAuxData *)aux_data;
	ASWindow *asw = (ASWindow *) data;
	LOCAL_DEBUG_OUT ("window \"%s\", is a %smodule", ASWIN_NAME (asw),
									 ASWIN_HFLAGS (asw, AS_Module) ? " " : "non ");
	if (asw && swad) {

		/* don't want to save windows with short life span - wharf subfolders, menus, dialogs, etc. */
		LOCAL_DEBUG_OUT ("short lived? %s", ASWIN_HFLAGS (asw, AS_ShortLived) ? "yes" : "no");
		if (ASWIN_HFLAGS (asw, AS_ShortLived))
			return True;

		/* don't want to save modules - those are started from autoexec anyways */
		if (ASWIN_HFLAGS (asw, AS_Module)) {
			if (!swad->only_modules)
				return True;
		} else if (swad->only_modules)
			return True;
		LOCAL_DEBUG_OUT ("group = %p", asw->group);
		/* we only restart the group leader, then let it worry about its children */
		if (asw->group != swad->group)
			return True;

		do_save_aswindow (asw, swad);
		return (swad->group == NULL);
	}
	return False;
}

void save_aswindow_list (ASWindowList * list, char *file, Bool skip_session_managed)
{
	struct SaveWindowAuxData swad;

	if (list == NULL)
		return;

	if (!mygethostname (swad.this_host, MAXHOSTNAME)) {
		show_error ("Could not get HOST environment variable!");
		return;
	}

	if (file != NULL) {
		char *realfilename = PutHome (file);
		if (realfilename == NULL)
			return;

		swad.f = fopen (realfilename, "w+");
		if (swad.f == NULL)
			show_error
					("Unable to save your workspace state into the %s - cannot open file for writing!",
					 realfilename);
		free (realfilename);
	} else {
		swad.f = fopen (get_session_ws_file (Session, False), "w+");
		if (swad.f == NULL)
			show_error
					("Unable to save your workspace state into the default file %s - cannot open file for writing!",
					 get_session_ws_file (Session, False));
	}

	if (swad.f) {
		ASHashIterator it;
		fprintf (swad.f, "Function \"WorkspaceState\"\n");
		swad.group = NULL;
		swad.only_modules = False;
		swad.res_name_counts = create_ashash (0, string_hash_value, string_compare, string_destroy);
		iterate_asbidirlist (list->clients, make_aswindow_cmd_iter_func, &swad, NULL, False);

		if (start_hash_iteration (list->window_groups, &it)) {
			do {
				ASWindowGroup *g = (ASWindowGroup *) curr_hash_data (&it);
				if (g) {
					LOCAL_DEBUG_OUT ("group \"%lx\", sm-client-id = \"%s\"",
													 (unsigned long)g->leader,
													 g->sm_client_id ? g->sm_client_id : "none");
					if (!skip_session_managed || !g->sm_client_id) {
						swad.group = g;
						iterate_asbidirlist (list->clients, make_aswindow_cmd_iter_func,
																 &swad, NULL, False);
					}
				}
			} while (next_hash_item (&it));
			swad.group = NULL;
		}

		destroy_ashash (&(swad.res_name_counts));
		fprintf (swad.f, "EndFunction\n\n");

		fprintf (swad.f, "Function \"WorkspaceModules\"\n");
		swad.only_modules = True;
		swad.res_name_counts =
				create_ashash (0, string_hash_value, string_compare,
											 string_destroy);
		iterate_asbidirlist (list->clients, make_aswindow_cmd_iter_func, &swad,
												 NULL, False);
		destroy_ashash (&(swad.res_name_counts));
		fprintf (swad.f, "EndFunction\n");
		fclose (swad.f);
	}
}

typedef struct ASCloseWindowAuxData {
	Bool skipSessionManaged;
}ASCloseWindowAuxData;

Bool close_aswindow_iter_func (void *data, void *aux_data)
{
	ASWindow *asw = (ASWindow *) data;
	struct ASCloseWindowAuxData *auxd = (struct ASCloseWindowAuxData*)aux_data;
	if (asw == NULL)
		return False;

	if (!ASWIN_HFLAGS (asw, AS_Module)
			&& get_flags (asw->hints->protocols, AS_DoesWmDeleteWindow)) {
		if (auxd->skipSessionManaged && asw->group && asw->group->sm_client_id)
			return True;
		LOCAL_DEBUG_OUT ("sending delete window request to 0x%lX", (unsigned long)asw->w);
		send_wm_protocol_request (asw->w, _XA_WM_DELETE_WINDOW, CurrentTime);
	}
	return True;
}

void close_aswindow_list (ASWindowList * list, Bool skip_session_managed)
{
	ASCloseWindowAuxData aux;
	aux.skipSessionManaged = skip_session_managed;
	iterate_asbidirlist (list->clients, close_aswindow_iter_func, &aux, NULL, False);
}

