/****************************************************************************
 * Copyright (c) 1999,2002 Sasha Vasko <sasha at aftercode.net>
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

#include "../../configure.h"
#define LOCAL_DEBUG


#include "asinternals.h"
#include "../../libAfterConf/afterconf.h"

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#include <X11/cursorfont.h>

#include "../../libAfterStep/session.h"
#include "../../libAfterStep/wmprops.h"
#include "../../libAfterStep/moveresize.h"
#include "../../libAfterBase/fs.h"

#include "afterstep_internal.h"
/*************************************************************************/
/* Our Screen initial state setup and initialization of management data :*/
/*************************************************************************/
void CreateCursors (void)
{
	/* define cursors */
	Scr.standard_cursors[ASCUR_Position] =
			XCreateFontCursor (dpy, XC_left_ptr);
/*  Scr.ASCursors[DEFAULT] = XCreateFontCursor(dpy, XC_top_left_arrow); */
	Scr.standard_cursors[ASCUR_Default] =
			XCreateFontCursor (dpy, XC_left_ptr);
	Scr.standard_cursors[ASCUR_Sys] = XCreateFontCursor (dpy, XC_left_ptr);
	Scr.standard_cursors[ASCUR_Title] = XCreateFontCursor (dpy, XC_left_ptr);
	Scr.standard_cursors[ASCUR_Move] = XCreateFontCursor (dpy, XC_fleur);
	Scr.standard_cursors[ASCUR_Menu] = XCreateFontCursor (dpy, XC_left_ptr);
	Scr.standard_cursors[ASCUR_Wait] = XCreateFontCursor (dpy, XC_watch);
	Scr.standard_cursors[ASCUR_Select] = XCreateFontCursor (dpy, XC_dot);
	Scr.standard_cursors[ASCUR_Destroy] = XCreateFontCursor (dpy, XC_pirate);
	Scr.standard_cursors[ASCUR_Left] = XCreateFontCursor (dpy, XC_left_side);
	Scr.standard_cursors[ASCUR_Right] =
			XCreateFontCursor (dpy, XC_right_side);
	Scr.standard_cursors[ASCUR_Top] = XCreateFontCursor (dpy, XC_top_side);
	Scr.standard_cursors[ASCUR_Bottom] =
			XCreateFontCursor (dpy, XC_bottom_side);
	Scr.standard_cursors[ASCUR_TopLeft] =
			XCreateFontCursor (dpy, XC_top_left_corner);
	Scr.standard_cursors[ASCUR_TopRight] =
			XCreateFontCursor (dpy, XC_top_right_corner);
	Scr.standard_cursors[ASCUR_BottomLeft] =
			XCreateFontCursor (dpy, XC_bottom_left_corner);
	Scr.standard_cursors[ASCUR_BottomRight] =
			XCreateFontCursor (dpy, XC_bottom_right_corner);
}

void CreateManagementWindows ()
{
	XSetWindowAttributes attr;		/* attributes for create windows */
	/* the SizeWindow will be moved into place in LoadASConfig() */
	attr.override_redirect = True;
	attr.bit_gravity = NorthWestGravity;
	Scr.SizeWindow =
			create_visual_window (Scr.asv, Scr.Root, -999, -999, 10, 10, 0,
														InputOutput, CWBitGravity | CWOverrideRedirect,
														&attr);
	LOCAL_DEBUG_OUT ("Scr.SizeWindow = %lX;", Scr.SizeWindow);
	/* create a window which will accept the keyboard focus when no other
	   windows have it */
	attr.event_mask = KeyPressMask | FocusChangeMask | AS_ROOT_EVENT_MASK;
	attr.override_redirect = True;
	Scr.ServiceWin = create_visual_window (Scr.asv, Scr.Root, 0, 0, 1, 1, 0,
																				 InputOutput,
																				 CWEventMask | CWOverrideRedirect,
																				 &attr);
	set_service_window_prop (Scr.wmprops, Scr.ServiceWin);
	LOCAL_DEBUG_OUT ("Scr.ServiceWin = %lX;", Scr.ServiceWin);
	XMapRaised (dpy, Scr.ServiceWin);
	XSetInputFocus (dpy, Scr.ServiceWin, RevertToParent, CurrentTime);
/*    show_progress( "Service window created with ID %lX", Scr.ServiceWin ); */
}

void DestroyManagementWindows ()
{
	if (Scr.ServiceWin)
		XDestroyWindow (dpy, Scr.ServiceWin);
	Scr.ServiceWin = None;
	if (Scr.SizeWindow)
		XDestroyWindow (dpy, Scr.SizeWindow);
	Scr.SizeWindow = None;
}

/***********************************************************************
 *  Procedure:
 *  Setup Screen - main function
 ************************************************************************/
void SetupScreen ()
{
	Scr.Look.magic = MAGIC_MYLOOK;
	InitLook (&Scr.Look, False);
	InitFeel (&Scr.Feel, False);

	Scr.Vx = Scr.Vy = 0;
	Scr.CurrentDesk = 0;

	Scr.randomx = Scr.randomy = 0;
	Scr.RootCanvas = create_ascanvas_container (Scr.Root);

	SetupColormaps ();
	CreateCursors ();
	CreateManagementWindows ();
	Scr.Windows = init_aswindow_list ();

	XSelectInput (dpy, Scr.Root, AS_ROOT_EVENT_MASK);

	MenuBalloons = create_balloon_state ();
	TitlebarBalloons = create_balloon_state ();
}

void CleanupScreen ()
{
	int i;

	if (Scr.Windows) {
		grab_server ();
		destroy_aswindow_list (&(Scr.Windows), True);
		ungrab_server ();
	}

	destroy_balloon_state (&TitlebarBalloons);
	destroy_balloon_state (&MenuBalloons);

	release_all_old_background (True);

	DestroyManagementWindows ();
	CleanupColormaps ();

	if (Scr.RootCanvas)
		destroy_ascanvas (&(Scr.RootCanvas));

	XSetInputFocus (dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XSync (dpy, 0);

	if (Scr.xinerama_screens) {
		free (Scr.xinerama_screens);
		Scr.xinerama_screens_num = 0;
		Scr.xinerama_screens = NULL;
	}


	for (i = 0; i < MAX_CURSORS; ++i)
		if (Scr.standard_cursors[i]) {
			XFreeCursor (dpy, Scr.standard_cursors[i]);
			Scr.standard_cursors[i] = None;
		}

	InitLook (&Scr.Look, True);
	InitFeel (&Scr.Feel, True);


	/* free display strings; can't do this in main(), because some OS's
	 * don't copy the environment variables properly */
	if (Scr.display_string) {
		free (Scr.display_string);
		Scr.display_string = NULL;
	}
	if (Scr.rdisplay_string) {
		free (Scr.rdisplay_string);
		Scr.rdisplay_string = NULL;
	}

	if (Scr.RootBackground) {
		if (Scr.RootBackground->pmap) {
			if (Scr.wmprops->root_pixmap == Scr.RootBackground->pmap) {
				set_xrootpmap_id (Scr.wmprops, None);
				set_as_background (Scr.wmprops, None);
			}
			XFreePixmap (dpy, Scr.RootBackground->pmap);
			ASSync (False);
			LOCAL_DEBUG_OUT ("root pixmap with id %lX destroyed",
											 Scr.RootBackground->pmap);
			Scr.RootBackground->pmap = None;
		}
		free (Scr.RootBackground);
	}
	LOCAL_DEBUG_OUT ("destroying image manager : %p", Scr.image_manager);
	destroy_image_manager (Scr.image_manager, False);
	LOCAL_DEBUG_OUT ("destroying font manager : %p", Scr.font_manager);
	destroy_font_manager (Scr.font_manager, False);

	LOCAL_DEBUG_OUT ("destroying visual : %p", Scr.asv);
	destroy_screen_gcs (ASDefaultScr);
	destroy_asvisual (Scr.asv, False);

	LOCAL_DEBUG_OUT ("selecting input mask for Root window to 0 : %s", "");
	/* Must release SubstructureRedirectMask prior to releasing wm selection in
	 * destroy_wmprops() : */
	XSelectInput (dpy, Scr.Root, 0);
	XUngrabPointer (dpy, CurrentTime);
	XUngrabButton (dpy, AnyButton, AnyModifier, Scr.Root);

	LOCAL_DEBUG_OUT ("destroying wmprops : %p", Scr.wmprops);
	/* this must be done at the very end !!!! */
	destroy_wmprops (Scr.wmprops, False);
	LOCAL_DEBUG_OUT ("screen cleanup complete.%s", "");
}

/*************************************************************************/
/* populating windowlist with presently available windows :
 * (backported from as-devel)
 */
/*************************************************************************/
void CaptureAllWindows (ScreenInfo * scr)
{
	int i;
	unsigned int nchildren;
	Window root, parent, *children;
	Window focused = None;
	int revert_to = RevertToNone;
	XWindowAttributes attr;

	if (scr == NULL)
		return;
	focused = XGetInputFocus (dpy, &focused, &revert_to);

	if (!XQueryTree (dpy, scr->Root, &root, &parent, &children, &nchildren))
		return;

	/* weed out icon windows : */
	for (i = 0; i < nchildren; i++)
		if (children[i]) {
			XWMHints *wmhintsp = NULL;
			if ((wmhintsp = XGetWMHints (dpy, children[i])) != NULL) {
				if (get_flags (wmhintsp->flags, IconWindowHint)) {
					register int j;
					for (j = 0; j < nchildren; j++)
						if (children[j] == wmhintsp->icon_window) {
							children[j] = None;
							break;
						}
				}
				XFree ((char *)wmhintsp);
			}
		}

	/* map all the rest of the windows : */
	for (i = 0; i < nchildren; i++)
		if (children[i]) {
			long nitems = 0;
			CARD32 *state_prop = NULL;
			int wm_state = DontCareState;
			int k;
			for (k = 0; k < 4; ++k)
				if (children[i] == Scr.PanFrame[k].win) {
					children[i] = None;
					break;
				}

			if (children[i] == None)
				continue;

			if (children[i] == Scr.SizeWindow || children[i] == Scr.ServiceWin)
				continue;

			if (window2ASWindow (children[i]))
				continue;
			/* weed out override redirect windows and unmapped windows : */
			if (!XGetWindowAttributes (dpy, children[i], &attr))
				continue;
			if (attr.override_redirect)
				continue;
			if (read_32bit_proplist
					(children[i], _XA_WM_STATE, 2, &state_prop, &nitems)) {
				wm_state = state_prop[0];
				free (state_prop);
			}
			if ((wm_state == IconicState) || (attr.map_state != IsUnmapped)) {
				LOCAL_DEBUG_OUT ("adding window %lX", children[i]);
				AddWindow (children[i], False);
			}
		}

	if (children)
		XFree ((char *)children);

	if (focused != None && focused != PointerRoot) {
		ASWindow *t = window2ASWindow (focused);
		if (t)
			activate_aswindow (t, False, False);
	}
}

void CloseAllWindows ()
{

}


/***********************************************************************
 * running Autoexec code ( if any ) :
 ************************************************************************/
void DoAutoexec (Bool restarting)
{
	FunctionData func;
	ASEvent event = { 0 };
	char screen_func_name[128];

	init_func_data (&func);
	func.func = F_FUNCTION;
	func.name = restarting ? "RestartFunction" : "InitFunction";
	if (Scr.screen > 0) {
		sprintf (screen_func_name,
						 restarting ? "RestartScreen%ldFunction" :
						 "InitScreen%ldFunction", Scr.screen);
		if (find_complex_func
				(Scr.Feel.ComplexFunctions, &(screen_func_name[0])) != NULL)
			func.name = &(screen_func_name[0]);
	}
	ExecuteFunction (&func, &event, -1);
	HandleEventsWhileFunctionsPending ();
}

/***********************************************************************
 * our signal handlers :
 ************************************************************************/
void IgnoreSignal (int sig)
{
	if (signal (sig, SIG_IGN) != SIG_IGN)
		signal (sig, SigDone);
}

void Restart (int nonsense)
{
	Done (True, NULL);
	SIGNAL_RETURN;
}

void SigDone (int nonsense)
{
	Done (False, NULL);
	SIGNAL_RETURN;
}

/*************************************************************************/
/* our shutdown function :                                               */
/*************************************************************************/

Bool RequestLogout ()
{
	return asdbus_Logout (0, 500);
}

Bool CanShutdown ()
{
	return (is_executable_in_path ("loginctl") || is_executable_in_path ("systemctl") ||
					is_executable_in_path ("shutdown") || is_executable_in_path ("poweroff") ||
					asdbus_GetCanShutdown ());
}

Bool CanSuspend ()
{
	return (is_executable_in_path ("loginctl") || is_executable_in_path ("systemctl") ||
					asdbus_GetCanSuspend ());
}
Bool CanHibernate ()
{
	return (is_executable_in_path ("loginctl") || is_executable_in_path ("systemctl") ||
					asdbus_GetCanHibernate ());
}

Bool CanLogout ()
{
	return (asdbus_GetCanLogout ());
}

Bool CanRestart ()
{
	return (GnomeSessionClientID == NULL);
}

Bool CanQuit ()
{
	return (GnomeSessionClientID == NULL);
}

void RemapFunctions()
{
	char *fname = make_session_data_file (Session, False, 0, AFTER_FUNC_REMAP, NULL);
	char *realfilename = PutHome (fname);
	FILE *fp = fopen (realfilename != NULL?realfilename : fname, "w");
	if (fp)	fprintf (fp, "Function \"RemapFunctions\"\n");

#define REMAP_FUNC(f) 	do { \
		change_func_code (#f, F_NOP); \
		if (fp)	fprintf (fp, "\tRemap \"" #f "\" Nop\n"); \
	} while (0)

	if (!CanRestart())
		REMAP_FUNC(Restart);
	if (!CanQuit())
		REMAP_FUNC(QuitWM);

	if (!CanLogout()) {
		/* If we can't perform a session-manager logout, fall back to quitting the
		 * WM so UI elements like MonitorWharf's "EndSession" don't disappear. */
		if (CanQuit()) {
			change_func_code ("Logout", F_QUIT_WM);
			if (fp)	fprintf (fp, "\tRemap \"Logout\" QuitWM\n");
		} else {
			REMAP_FUNC(Logout);
		}
	}

	if (!CanShutdown())
		REMAP_FUNC(SystemShutdown);

	/* these use UPower which is sitting on system bus, independent from ASDBus_fd */
	if (!CanSuspend())
		REMAP_FUNC(Suspend);
	if (!CanHibernate())
		REMAP_FUNC(Hibernate);


#undef REMAP_FUNC
	if (fp)	{
		fprintf (fp, "EndFunction\n");
		fclose (fp);
	}

	if (realfilename != NULL)
		free (realfilename);
	free (fname);
}


Bool RequestShutdown (FunctionCode kind)
{
	Bool requested = False;
	pid_t pid;

	/* Prefer systemd/logind tooling when available (modern Linux distros), and
	 * fall back to old UPower/gnome-session DBus code paths only if needed. */
	char *const loginctl_suspend[] = { "loginctl", "suspend", NULL };
	char *const loginctl_hibernate[] = { "loginctl", "hibernate", NULL };
	char *const loginctl_poweroff[] = { "loginctl", "poweroff", NULL };
	char *const systemctl_suspend[] = { "systemctl", "suspend", NULL };
	char *const systemctl_hibernate[] = { "systemctl", "hibernate", NULL };
	char *const systemctl_poweroff[] = { "systemctl", "poweroff", NULL };
	char *const shutdown_halt[] = { "shutdown", "-h", "now", NULL };
	char *const poweroff_cmd[] = { "poweroff", NULL };

	char *const *argv = NULL;
	const char *cmd = NULL;

	switch (kind) {
		case F_SYSTEM_SHUTDOWN:
			/* If we are registered with a session manager that can shutdown,
			 * use it (lets the session manager prompt/coordinate). */
			if (GnomeSessionClientID && asdbus_GetCanShutdown ())
				requested = asdbus_Shutdown (500);

			if (!requested) {
				if (is_executable_in_path ("loginctl")) {
					cmd = "loginctl";
					argv = loginctl_poweroff;
				} else if (is_executable_in_path ("systemctl")) {
					cmd = "systemctl";
					argv = systemctl_poweroff;
				} else if (is_executable_in_path ("shutdown")) {
					cmd = "shutdown";
					argv = shutdown_halt;
				} else if (is_executable_in_path ("poweroff")) {
					cmd = "poweroff";
					argv = poweroff_cmd;
				}
			}
			break;
		case F_SUSPEND:
			if (is_executable_in_path ("loginctl")) {
				cmd = "loginctl";
				argv = loginctl_suspend;
			} else if (is_executable_in_path ("systemctl")) {
				cmd = "systemctl";
				argv = systemctl_suspend;
			}
			break;
		case F_HIBERNATE:
			if (is_executable_in_path ("loginctl")) {
				cmd = "loginctl";
				argv = loginctl_hibernate;
			} else if (is_executable_in_path ("systemctl")) {
				cmd = "systemctl";
				argv = systemctl_hibernate;
			}
			break;
		default:
			break;
	}

	if (!requested && cmd && argv) {
		pid = fork ();
		if (pid == 0) {
			setsid ();
			execvp (cmd, argv);
			_exit (127);
		}
		requested = (pid > 0);
	}

	/* Old fallbacks (non-systemd, legacy setups). */
	if (!requested) {
		switch (kind) {
			case F_SUSPEND:
				if (asdbus_GetCanSuspend ())
					requested = asdbus_Suspend (500);
				break;
			case F_HIBERNATE:
				if (asdbus_GetCanHibernate ())
					requested = asdbus_Hibernate (500);
				break;
			default:
				break;
		}
	}
	return requested;
}

void SaveSession (Bool force)
{
#ifndef NO_SAVEWINDOWS
	static Bool saved = False;
	if (!saved || force) {
		char *fname = make_session_data_file (Session, False, 0, AFTER_SAVE, NULL);
		save_aswindow_list (Scr.Windows, fname, get_gnome_autosave ());
		free (fname);
		saved = True;
	}
#endif
}

static void CloseSessionRetryHandler (void *data)
{
	CloseSessionClients (data != NULL);
}


void CloseSessionClients (Bool only_modules)
{
	int modules_killed;
	/* Its the end of the session and we better close all non-module windows
	   that support the protocol. Otherwise they'll just crash when X connection goes down.
	 */
	show_progress ("Closing down all modules ...");
	display_progress (True, "Closing down all modules ...");
	/* keep datastructures operational since we could still be inside event loop */
	modules_killed = KillAllModules ();

	if (!only_modules) {
		show_progress ("Session end: Closing down all remaining windows ...");
		display_progress (True,
											"Session end: Closing down all remaining windows ...");
		close_aswindow_list (Scr.Windows, True);
		ASFlushAndSync ();
		sleep_a_millisec (100);
	}
/*	if (modules_killed > 0) */
	ASHashData d;
	d.i = only_modules;
	timer_new (100, &CloseSessionRetryHandler, d.vptr);
}

void Done (Bool restart, char *command)
{
	int restart_screen =
			get_flags (AfterStepState, ASS_SingleScreen) ? Scr.screen : -1;
	Bool restart_self = False;
	char *local_command = NULL;
	{
		static int already_dead = False;
		if (already_dead)
			return;										/* non-reentrant function ! */
		already_dead = True;
	}

	/* lets duplicate the string so we don't accidentally delete it while closing self down */
	if (restart) {
		int my_name_len = strlen (MyName);
		if (command) {
			if (strncmp (command, MyName, my_name_len) == 0)
				restart_self = (command[my_name_len] == ' '
												|| command[my_name_len] == '\0');
			local_command = mystrdup (command);
		} else {
			local_command = mystrdup (MyName);
			restart_self = True;
		}
		if (!is_executable_in_path (local_command)) {
			if (!restart_self || MyArgs.saved_argv[0] == NULL) {
				show_error
						("Cannot restart with command \"%s\" - application is not in PATH!",
						 local_command);
				return;
			}
			free (local_command);
			if (command) {
				local_command =
						safemalloc (strlen (command) + 1 +
												strlen (MyArgs.saved_argv[0]) + 1);
				sprintf (local_command, "%s %s", MyArgs.saved_argv[0],
								 command + my_name_len);
			} else
				local_command = mystrdup (MyArgs.saved_argv[0]);
		}
	}

#ifdef XSHMIMAGE
	/* may not need to do that as server may still have some of the shared
	 * memory and work in it */
	flush_shm_cache ();
#endif

	LOCAL_DEBUG_CALLER_OUT ("%s restart, cmd=\"%s\"",
													restart ? "Do" : "Don't",
													command ? command : "");

	XSelectInput (dpy, Scr.Root, 0);
	SendPacket (-1, M_SHUTDOWN, 0);
	FlushAllQueues ();
	sleep_a_millisec (1000);

	LOCAL_DEBUG_OUT ("local_command = \"%s\", restart_self = %s",
									 local_command, restart_self ? "Yes" : "No");
	set_flags (AfterStepState, ASS_Shutdown);
	if (restart)
		set_flags (AfterStepState, ASS_Restarting);
	clear_flags (AfterStepState, ASS_NormalOperation);
#ifndef NO_VIRTUAL
	MoveViewport (0, 0, False);
#endif

	if (!restart)
		SaveSession (False);

	/* Close all my pipes */
	show_progress ("Shuting down Modules subsystem ...");
	ShutdownModules (False);

	CloseSessionClients (GnomeSessionClientID == NULL || restart);

	desktop_cover_cleanup ();

	/* remove window frames */
	CleanupScreen ();
	/* Really make sure that the connection is closed and cleared! */
	XSync (dpy, 0);

	if (ASDBusConnected) {
		if (GnomeSessionClientID != NULL)
			asdbus_UnregisterSMClient (GnomeSessionClientID);
		asdbus_shutdown ();
	}
#ifdef XSHMIMAGE
	flush_shm_cache ();
#endif
	if (restart) {
		set_flags (MyArgs.flags, ASS_Restarting);
		spawn_child (local_command, -1, restart_screen,
								 original_DISPLAY_string, None, C_NO_CONTEXT, False,
								 restart_self, NULL);
	} else {

		XCloseDisplay (dpy);
		dpy = NULL;

		/* freeing up memory */
		DestroyPendingFunctionsQueue ();
		DestroyCategories ();

		cleanup_default_balloons ();
		destroy_asdatabase ();
		destroy_assession (Session);
		destroy_asenvironment (&Environment);
		/* pixmap references */
		build_xpm_colormap (NULL);

		free_scratch_ids_vector ();
		free_scratch_layers_vector ();
		clientprops_cleanup ();
		wmprops_cleanup ();

		free_func_hash ();
		flush_keyword_ids ();
		purge_asimage_registry ();

		asxml_var_cleanup ();
		custom_color_cleanup ();

		free_as_app_args ();
		free (ASDefaultScr);

		flush_default_asstorage ();
		flush_asbidirlist_memory_pool ();
		flush_ashash_memory_pool ();
#ifdef DEBUG_ALLOCS
		print_unfreed_mem ();
#endif													/*DEBUG_ALLOCS */
#ifdef XSHMIMAGE
		flush_shm_cache ();
#endif

	}
	exit (0);
}
