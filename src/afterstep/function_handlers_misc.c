/***********************************************************************
 * Miscellaneous AfterStep function handlers.
 ***********************************************************************/

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include <sys/stat.h>
#include <unistd.h>

#include "../../libAfterStep/session.h"
#include "../../libAfterStep/moveresize.h"
#include "../../libAfterStep/mylook.h"
#include "../../libAfterStep/desktop_category.h"
#include "../../libAfterStep/wmprops.h"
#include "../../libAfterConf/afterconf.h"
#include "../../libAfterStep/kde.h"

#include "functions_internal.h"

void beep_func_handler (FunctionData * data, ASEvent * event, int module)
{
	XBell (dpy, event->scr->screen);
}

void paste_selection_func_handler (FunctionData * data, ASEvent * event,
																	 int module)
{
	PasteSelection (event->scr);
}

void pin_menu_func_handler (FunctionData * data, ASEvent * event,
														int module)
{
	ASMenu *menu = NULL;
	char *menu_name = data->text ? data->text : data->name;
	LOCAL_DEBUG_OUT ("menu_name = \"%s\", client = %p, internal = %p",
									 menu_name ? "NULL" : menu_name, event->client,
									 event->client->internal);
	if (menu_name && menu_name[0] != '\0')
		menu = find_asmenu (menu_name);
	else if (event->client && event->client->internal) {
		ASMagic *data = event->client->internal->data;
		LOCAL_DEBUG_OUT ("data = %p, magic = %lX", data, data->magic);
		if (data->magic == MAGIC_ASMENU)
			menu = (ASMenu *) data;
	}
	if (menu == NULL)
		XBell (dpy, event->scr->screen);
	else
		pin_asmenu (menu);
}

void restart_func_handler (FunctionData * data, ASEvent * event,
													 int module)
{
	if (CanRestart())
		Done (True, data->text);
}

void exec_func_handler (FunctionData * data, ASEvent * event, int module)
{
	/* no sense in grabbing Pointer here as fork will return rather fast not creating 
	   much of the delay */
	spawn_child (data->text, -1, -1, NULL, None, C_NO_CONTEXT, True, False,
							 NULL);
}

void exec_in_dir_func_handler (FunctionData * data, ASEvent * event,
															 int module)
{
	char *cmd = tokenskip (data->text, 1);

	if (cmd == NULL || cmd[0] == '\0')
		exec_func_handler (data, event, module);
	else if (fork () == 0) {
		char *dirname = NULL, *fulldirname = NULL;
		parse_token (data->text, &dirname);
		fulldirname = put_file_home (dirname);
		if (chdir (fulldirname) == 0)
			spawn_child (cmd, -1, -1, NULL, None, C_NO_CONTEXT, False, False,
									 NULL);
	}
}

static int find_escaped_chr_pos (const char *str, char c)
{
	int i;
	for (i = 0; str[i] != '\0'; ++i) {
		if (str[i] == '\\') {
			if (str[++i] == '\0')
				break;
		} else if (str[i] == c)
			break;
	}
	return i;
}

static char *parse_term_cmdl (const char *term_name, const char *term_command,
															const char *cmdl)
{
	int term_name_len, cmdl_len, curr_full, curr_cmdl;
	char *full_cmdl = NULL;
	Bool first = True;

	if (term_name == NULL || term_command == NULL || cmdl == NULL)
		return NULL;

	LOCAL_DEBUG_OUT
			("term_name = \"%s\", term_command = \"%s\", cmdl = \"%s\"",
			 term_name, term_command, cmdl);
	curr_full = strlen (term_command);
	cmdl_len = strlen (cmdl);
	term_name_len = strlen (term_name);
	curr_cmdl = 0;
	full_cmdl = safemalloc (curr_full + 4 + cmdl_len + 1);

	strcpy (full_cmdl, term_command);

	while (curr_cmdl < cmdl_len) {
		while (isspace (cmdl[curr_cmdl]))
			++curr_cmdl;
		if (mystrncasecmp (&(cmdl[curr_cmdl]), "if(", 3) == 0) {
			int tmp;

			curr_cmdl += 3;
			tmp = curr_cmdl;
			curr_cmdl += find_escaped_chr_pos (&(cmdl[curr_cmdl]), '}') + 1;
			while (isspace (cmdl[tmp]))
				++tmp;
			if (mystrncasecmp (&(cmdl[tmp]), term_name, term_name_len) == 0) {
				tmp += term_name_len;
				while (isspace (cmdl[tmp]))
					++tmp;
				if (cmdl[tmp] == ')') {
					++tmp;
					while (isspace (cmdl[tmp]))
						++tmp;
					if (cmdl[tmp] == '{')
						++tmp;
					while (isspace (cmdl[tmp]))
						++tmp;
					full_cmdl[curr_full++] = ' ';
					while (tmp < curr_cmdl - 1)
						full_cmdl[curr_full++] = cmdl[tmp++];
				}
			}
		} else {
			if (first) {
				if (cmdl[curr_cmdl] != '-') {
					if (cmdl[curr_cmdl] != '\0')
						sprintf (&(full_cmdl[curr_full]), " -e %s",
										 &(cmdl[curr_cmdl]));
					else
						full_cmdl[curr_full] = '\0';
					return full_cmdl;
				}
				first = False;
			}

			if (strncmp (&(cmdl[curr_cmdl]), "-e ", 3) == 0) {
				sprintf (&(full_cmdl[curr_full]), " %s", &(cmdl[curr_cmdl]));
				return full_cmdl;
			}

			full_cmdl[curr_full++] = ' ';
			while (curr_cmdl < cmdl_len && !isspace (cmdl[curr_cmdl]))
				full_cmdl[curr_full++] = cmdl[curr_cmdl++];
		}

	}
	full_cmdl[curr_full] = '\0';

	return full_cmdl;
}

void exec_in_term_func_handler (FunctionData * data, ASEvent * event,
																int module)
{
	if (Environment->tool_command[ASTool_Term] != NULL && data->text != NULL) {
		char *full_cmdl = NULL;
		char *term_name =
				strrchr (Environment->tool_command[ASTool_Term], '/');
		term_name =
				(term_name ==
				 NULL) ? Environment->tool_command[ASTool_Term] : term_name + 1;
		full_cmdl =
				parse_term_cmdl (term_name, Environment->tool_command[ASTool_Term],
												 data->text);
		if (full_cmdl) {
			LOCAL_DEBUG_OUT ("full_cmdl = [%s]", full_cmdl);
			/* no sense in grabbing Pointer here as fork will return rather fast not creating 
			   much of the delay */
			spawn_child (full_cmdl, -1, -1, NULL, None, C_NO_CONTEXT, True,
									 False, NULL);
			free (full_cmdl);
		}
	}
}

static int /* return -1 - undetermined, 0 - no term, 1 - terminal requiresd */
check_tool_needs_term (const char *tool)
{
	static char *known_text_mode_apps[] = {
		"vi", "vim", "joe", "jed", "ne", "le", "moe", "elvis", "jove", "dav",
				"aoeui",
		"elinks", "lynx",
		NULL
	};
	ASDesktopEntry *de;

	char *name = NULL;
	struct stat st;
	int res = -1;
	int i;

	parse_file_name (tool, NULL, &name);
	LOCAL_DEBUG_OUT ("checking if \"%s\" requires term ...", name);
	de = fetch_desktop_entry (CombinedCategories, name);
	if (de != NULL) {
		LOCAL_DEBUG_OUT ("DesktopEntry \"%s\" found", de->Name);
		res = get_flags (de->flags, ASDE_Terminal) ? 1 : 0;
	} else if (lstat (tool, &st) != -1) {	/* now we need to resolve all the symlinking */

		if ((st.st_mode & S_IFMT) == S_IFLNK) {
			char linkdst[1024];
			int len = readlink (tool, linkdst, sizeof (linkdst) - 1);
			if (len > 0) {
				linkdst[len] = '\0';
				res = check_tool_needs_term (linkdst);
				LOCAL_DEBUG_OUT ("checking symlink target \"%s\" - res = %d",
												 linkdst, res);
			}
		}
	}

	/* last resort - check hardcoded list */
	for (i = 0; res < 0 && known_text_mode_apps[i]; ++i)
		if (strcmp (name, known_text_mode_apps[i]) == 0)
			res = 1;

	LOCAL_DEBUG_OUT ("\"%s\" requires term  = %d", name, res);

	free (name);
	return res;
}

void exec_tool_func_handler (FunctionData * data, ASEvent * event,
														 int module)
{
	ASToolType tool =
			(data->func == F_ExecBrowser) ? ASTool_Browser : ASTool_Editor;
	if (Environment->tool_command[tool] != NULL && data->text != NULL) {
		char *full_cmdl =
				safemalloc (strlen (Environment->tool_command[tool]) + 1 +
										strlen (data->text) + 1);
		sprintf (full_cmdl, "%s %s", Environment->tool_command[tool],
						 data->text);
		LOCAL_DEBUG_OUT ("full_cmdl = [%s]", full_cmdl);

		if (check_tool_needs_term (Environment->tool_command[tool]) > 0) {
			char *old_text = data->text;
			data->text = full_cmdl;
			exec_in_term_func_handler (data, event, module);
			data->text = old_text;
		} else {
			/* no sense in grabbing Pointer here as fork will return rather fast not creating 
			   much of the delay */
			spawn_child (full_cmdl, -1, -1, NULL, None, C_NO_CONTEXT, True,
									 False, NULL);
		}
		free (full_cmdl);
	}
}

void desktop_entry_func_handler (FunctionData * data, ASEvent * event,
																 int module)
{
	ASDesktopEntry *de =
			name2desktop_entry (data->text ? data->text : data->name, NULL);
	if (de == NULL)
		XBell (dpy, event->scr->screen);
	else {
		FunctionData *real_data = desktop_entry2function (de, NULL);
		if (real_data) {
			ExecuteFunctionExt (real_data, event, -1, True);
			destroy_func_data (&real_data);
		}
	}
}

void refresh_func_handler (FunctionData * data, ASEvent * event,
														 int module)
{
	XSetWindowAttributes attributes;
	unsigned long valuemask;
	Window w;

	valuemask = (CWBackPixmap | CWBackingStore | CWOverrideRedirect);
	attributes.background_pixmap = None;
	attributes.backing_store = NotUseful;
	attributes.override_redirect = True;

	w = create_visual_window (Scr.asv, Scr.Root, 0, 0,
														Scr.MyDisplayWidth, Scr.MyDisplayHeight,
														0, InputOutput, valuemask, &attributes);

	XMapRaised (dpy, w);
	XSync (dpy, False);
	XDestroyWindow (dpy, w);
	XFlush (dpy);

	spool_unfreed_mem ("afterstep.allocs.refresh", NULL);
}

void gethelp_func_handler (FunctionData * data, ASEvent * event,
													 int module)
{
	if (event->client != NULL)
		if (ASWIN_RES_NAME (event->client) != NULL) {
			char *realfilename = PutHome (HELPCOMMAND);
			spawn_child (realfilename, -1, -1, NULL, None, C_NO_CONTEXT, True,
									 False, ASWIN_RES_NAME (event->client), NULL);
			free (realfilename);
		}
}

void wait_func_handler (FunctionData * data, ASEvent * event, int module)
{
	ASWindow *asw;
	char *complex_pattern = data->text;
	if (data->name && data->name[1] == ':')
		complex_pattern = &(data->name[2]);

	asw = WaitWindowLoop (complex_pattern, -1);
	LOCAL_DEBUG_OUT
			("Wait completed for \"%s\", asw = %p, data->text = \"%s\"",
			 complex_pattern, asw, data->text ? data->text : "(null)");
	if (asw && data->text) {
		/* 1. parse text into a name_list struct */
		name_list *style = string2DatabaseStyle (data->text);
		LOCAL_DEBUG_OUT
				("style = %p, set_data_flags = 0x%lX, set_flags = 0x%lX", style,
				 style ? style->set_data_flags : 0, style ? style->set_flags : 0);
		if (style) {								/* 2. apply set data from name_list to asw */
			int new_vx = Scr.Vx;
			int new_vy = Scr.Vy;
			int x, y;
			int width, height;
			ASFlagType geom_flags =
					get_flags (style->set_data_flags,
										 STYLE_DEFAULT_GEOMETRY) ? style->default_geometry.
					flags : 0;
			ASFlagType state_flags = 0;

			if (get_flags (style->set_data_flags, STYLE_VIEWPORTX))
				new_vx = style->ViewportX;
			else
				new_vx = asw->status->viewport_x;
			if (get_flags (style->set_data_flags, STYLE_VIEWPORTY))
				new_vy = style->ViewportY;
			else
				new_vy = asw->status->viewport_y;

			x = get_flags (geom_flags,
										 XValue) ? style->default_geometry.x : asw->status->x;
			y = get_flags (geom_flags,
										 YValue) ? style->default_geometry.y : asw->status->y;
			width =
					get_flags (geom_flags,
										 WidthValue) ? (style->default_geometry.width +
																		asw->status->frame_size[FR_W] +
																		asw->status->frame_size[FR_E]) : asw->
					status->width;
			height =
					get_flags (geom_flags,
										 HeightValue) ? (style->default_geometry.height +
																		 asw->status->frame_size[FR_N] +
																		 asw->status->frame_size[FR_S]) : asw->
					status->height;

			if (get_flags (style->set_flags, STYLE_STICKY)) {
				if ((get_flags (style->flags, STYLE_STICKY)
						 && !ASWIN_GET_FLAGS (asw, AS_Sticky))
						|| (!get_flags (style->flags, STYLE_STICKY)
								&& ASWIN_GET_FLAGS (asw, AS_Sticky)))
					set_flags (state_flags, AS_Sticky);
			}
			if (get_flags (style->set_flags, STYLE_FULLSCREEN)) {
				if ((get_flags (style->flags, STYLE_FULLSCREEN)
						 && !ASWIN_GET_FLAGS (asw, AS_Fullscreen))
						|| (!get_flags (style->flags, STYLE_FULLSCREEN)
								&& ASWIN_GET_FLAGS (asw, AS_Fullscreen)))
					set_flags (state_flags, AS_Fullscreen);
			}

			if (state_flags != 0)
				toggle_aswindow_status (asw, state_flags);

			if (!ASWIN_GET_FLAGS (asw, AS_Sticky)) {
				if (get_flags (style->set_data_flags, STYLE_STARTUP_DESK))
					change_aswindow_desktop (asw, style->Desk, False);
			} else {
				new_vx = Scr.Vx;
				new_vy = Scr.Vy;
			}

			LOCAL_DEBUG_OUT
					("%s -> viewport : new(%+d%+d)old(%+d%+d) ; geom : new(%dx%d%+d%+d)old(%dx%d%+d%+d)",
					 ASWIN_GET_FLAGS (asw, AS_Sticky) ? "sticky" : "slippery",
					 new_vx, new_vy, asw->status->viewport_x,
					 asw->status->viewport_y, width, height, x, y,
					 asw->status->width, asw->status->height, asw->status->x,
					 asw->status->y);


			if (get_flags
					(style->set_data_flags,
					 STYLE_DEFAULT_GEOMETRY | STYLE_VIEWPORTX | STYLE_VIEWPORTY)) {
				x += Scr.Vx - asw->status->viewport_x + new_vx;
				y += Scr.Vy - asw->status->viewport_y + new_vy;
				moveresize_aswindow_wm (asw, x, y, width, height, False);
			}

			if (get_flags (style->set_data_flags, STYLE_LAYER)
					&& ASWIN_LAYER (asw) != style->layer) {
				LOCAL_DEBUG_OUT ("new_layer = %d", style->layer);
				change_aswindow_layer (asw, style->layer);
			}

			if (get_flags (style->set_flags, STYLE_START_ICONIC)) {
				if ((get_flags (style->flags, STYLE_START_ICONIC)
						 && !ASWIN_GET_FLAGS (asw, AS_Iconic))
						|| (!get_flags (style->flags, STYLE_START_ICONIC)
								&& ASWIN_GET_FLAGS (asw, AS_Iconic)))
					set_window_wm_state (event->client,
															 get_flags (style->flags,
																					STYLE_START_ICONIC), False);
			}

			style_delete (style, NULL);
		}
	}
	XSync (dpy, 0);
}

void popup_func_handler (FunctionData * data, ASEvent * event, int module)
{
	run_menu (data->text ? data->text : data->name,
						event->client ? event->client->w : None);
}

void quit_func_handler (FunctionData * data, ASEvent * event, int module)
{
	if (!RequestLogout ())
		Done (0, NULL);
}

void quit_wm_func_handler (FunctionData * data, ASEvent * event, int module)
{
	Done (0, NULL);
}

void system_shutdown_func_handler (FunctionData * data, ASEvent * event,
																	 int module)
{
	if (!RequestShutdown (data->func))
		beep_func_handler (data, event, module);
}

void windowlist_func_handler (FunctionData * data, ASEvent * event,
															int module)
{
#ifndef NO_WINDOWLIST
	MenuData *md =
			make_desk_winlist_menu (Scr.Windows,
															data->text ==
															NULL ? event->scr->CurrentDesk : data->
															func_val[0], Scr.Feel.winlist_sort_order,
															False);
	if (md != NULL) {
		ASMenu *menu = run_menu_data (md);
		/* attaching menu data to menu, so that we can destroy it when menu closes.
		   This is to accomodate those dynamically generated menus, 
		   such as window list and module list */
		/* Crude! should implement reference counting instead  */

		if (menu)
			menu->volitile_menu_data = md;
	}
#endif													/* ! NO_WINDOWLIST */
}

void screenshot_func_handler (FunctionData * data, ASEvent * event,
															int module)
{
	ASImage *im;
	Window target = None;
	sleep_a_millisec (300);
	if (event->client && data->func != F_TAKE_SCREENSHOT)
		target =
				(data->func ==
				 F_TAKE_WINDOWSHOT) ? event->client->w : event->client->frame;
	im = grab_root_asimage (ASDefaultScr, target, True);
	LOCAL_DEBUG_OUT ("grab_root_image returned %p", im);
	if (im != NULL) {
		char *realfilename = NULL;
		Bool replace = True;
		char *type = NULL;
		char *compress = NULL;			/* default compression */
#ifdef DONT_REPLACE_SCREENSHOT_FILES
		replace = False;
#endif
		if (data->text != NULL) {
			realfilename = PutHome (data->text);
			type = strrchr (realfilename, '.');
			if (type != NULL) {
				++type;
				if (mystrcasecmp (type, "jpg") == 0
						|| mystrcasecmp (type, "jpeg") == 0)
					compress = "0";
			}
		}
		if (realfilename == NULL) {
			char *capture_file_name = DEFAULT_CAPTURE_SCREEN_FILE;
			char *default_template;
			if (data->func == F_TAKE_WINDOWSHOT)
				capture_file_name = DEFAULT_CAPTURE_WINDOW_FILE;
			else if (data->func == F_TAKE_FRAMESHOT)
				capture_file_name = DEFAULT_CAPTURE_FRAMEDWINDOW_FILE;
			default_template = safemalloc (strlen (capture_file_name) + 100);
			sprintf (default_template, "%s.%lu.png", capture_file_name,
							 time (NULL));
			realfilename = PutHome (default_template);
			free (default_template);
			compress = "9";
			type = "png";
		}

		if (save_asimage_to_file
				(realfilename, im, type, compress, NULL, 0, replace))
			show_warning ("screenshot saved as \"%s\"", realfilename);
		free (realfilename);
		destroy_asimage (&im);
	}
}

void set_func_handler (FunctionData * data, ASEvent * event, int module)
{
	if (data->text != NULL) {
		char *eq_ptr = strchr (data->text, '=');
		if (eq_ptr) {
			int val = 0;
			char *tail = eq_ptr + 1;
			val = (int)parse_math (tail, &tail, strlen (tail));
			LOCAL_DEBUG_OUT ("tail = \"%s\", val = %d", tail, val);
			if (tail > eq_ptr + 1) {
				*eq_ptr = '\0';
				asxml_var_insert (data->text, val);
				if (strncmp (data->text, "menu.", 5) == 0) {
					ASFlagType change_flag = 0;
					ASFlagType old_look_flags = Scr.Look.flags;
					if (strcmp (data->text, ASXMLVAR_MenuShowMinipixmaps) == 0)
						change_flag = MenuMiniPixmaps;
					else if (strcmp (data->text, ASXMLVAR_MenuShowUnavailable) == 0)
						change_flag = MenuShowUnavailable;
					else if (strcmp (data->text, ASXMLVAR_MenuTxtItemsInd) == 0)
						change_flag = TxtrMenuItmInd;
					if (change_flag != 0) {
						if (!val)
							clear_flags (Scr.Look.flags, change_flag);
						else
							set_flags (Scr.Look.flags, change_flag);
					} else if (strcmp (data->text, ASXMLVAR_MenuRecentSubmenuItems)
										 == 0) {
						Scr.Feel.recent_submenu_items = (val <= 0) ? 0 : val;
					}

					if (old_look_flags != Scr.Look.flags) {
						/* need to referesh menus maybe? */
					}
					LOCAL_DEBUG_OUT ("old_flags = %lX, flags = %lX", old_look_flags,
													 Scr.Look.flags);
				}
				*eq_ptr = '=';
			}
		}
	}
}

void test_func_handler (FunctionData * data, ASEvent * event, int module)
{
	/* add test command processing here : */
/*         fprintf( stderr, "Testing <do_menu_new( \"Looks\", NULL ) ...\n" ); */
/*         do_menu_new( "Look", NULL, NULL ); */
	fprintf (stderr, "Testing completed\n");
}
