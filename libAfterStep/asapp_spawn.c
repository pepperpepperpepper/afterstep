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
#include "asapp_internal.h"

/* Child-process spawning (singleton tracking, spawn_child) and URL
 * download helpers, split out of asapp.c. Uses the shared AS_environ
 * vector declared in asapp_internal.h. */

/************ child process spawning ***************************/
/***********************************************************************
 *  Procedure:
 *  general purpose child launcher - spawn child.
 *  Pass along all the cmd line args if needed.
 *  returns PID of the spawned process
 ************************************************************************/
static int as_singletons[MAX_SINGLETONS_NUM];
static Bool as_init_singletons = True;

void as_sigchild_handler (int signum)
{
	int pid;
	int status;

	signal (SIGCHLD, as_sigchild_handler);
	DEBUG_OUT ("Entering SigChild_handler(%lu)", time (NULL));
	if (as_init_singletons)
		return;
	while ((pid = WAIT_CHILDREN (&status)) > 0) {
		register int i;

		LOCAL_DEBUG_OUT ("pid = %d", pid);
		for (i = 0; i < MAX_SINGLETONS_NUM; i++)
			if (pid == as_singletons[i]) {
				as_singletons[i] = 0;
				break;
			}
	}
	DEBUG_OUT ("Exiting SigChild_handler(%lu)", time (NULL));
}

/*
 * This should return 0 if process of running external app to draw background completed or killed.
 * otherwise it returns > 0
 */
int check_singleton_child (int singleton_id, Bool kill_it_to_death)
{
	int i;
	int pid, status;

	if (as_init_singletons || singleton_id < 0)
		return -1;

	if (singleton_id >= MAX_SINGLETONS_NUM)
		singleton_id = MAX_SINGLETONS_NUM - 1;

	DEBUG_OUT ("CheckingForDrawChild(%lu)....", time (NULL));
	if ((pid = as_singletons[singleton_id]) > 0) {
		DEBUG_OUT ("checking on singleton child #%d started with PID (%d)",
							 singleton_id, pid);
		if (kill_it_to_death) {
			kill (pid, SIGTERM);
			for (i = 0; i < 100; i++) {	/* give it 10 sec to terminate */
				sleep_a_millisec (100);
				if (WAIT_CHILDREN (&status) == pid
						|| as_singletons[singleton_id] <= 0)
					break;
			}
			if (i >= 100)
				kill (pid, SIGKILL);		/* no more mercy */
			as_singletons[singleton_id] = 0;
		}
	} else if (as_singletons[singleton_id] < 0)
		as_singletons[singleton_id] = 0;

	DEBUG_OUT ("Done(%lu). Child PID on exit = %d.", time (NULL),
						 as_singletons[singleton_id]);
	return as_singletons[singleton_id];
}

int
spawn_child (const char *cmd, int singleton_id, int screen,
						 const char *orig_display, Window w, int context, Bool do_fork,
						 Bool pass_args, ...)
{
	int pid = 0;

	if (cmd == NULL)
		return 0;
	if (as_init_singletons) {
		register int i;

		for (i = 0; i < MAX_SINGLETONS_NUM; i++)
			as_singletons[i] = 0;
		signal (SIGCHLD, as_sigchild_handler);
		as_init_singletons = False;
	}

	if (singleton_id >= 0) {
		if (singleton_id >= MAX_SINGLETONS_NUM)
			singleton_id = MAX_SINGLETONS_NUM - 1;
		if (as_singletons[singleton_id] > 0)
			check_singleton_child (singleton_id, True);
	}

	if (do_fork)
		pid = fork ();

	if (pid != 0) {
		/* there is a possibility of the race condition here
		 * but it really is not worse the trouble to try and avoid it.
		 */
		if (singleton_id >= 0)
			as_singletons[singleton_id] = pid;
		return pid;
	} else {											/* we get here only in child process. We now need to spawn new proggy here: */
		int len;
		char *display = mystrdup (XDisplayString (dpy));
		char **envp;
		register char *ptr;

		char *cmdl;
		char *arg, *screen_str = NULL, *w_str = NULL, *context_str = NULL;
		int env_s = 0;
		char **envvars = AS_environ;
		int font_path_slot = -1, image_path_slot = -1;

		va_list ap;

		LOCAL_DEBUG_OUT ("dpy = %p, DisplayString = \"%s\"", dpy, display);
		LOCAL_DEBUG_OUT ("pid(%d), entered child process to spawn ...", pid);

#if HAVE_DECL_ENVIRON
		if (envvars == NULL) {
			envvars = environ;
		}
#else
/* how the hell could we get environment otherwise ? */
#endif
		if (envvars) {
			int font_path_len = strlen (ASFONT_PATH_ENVVAR);
			int image_path_len = strlen (ASIMAGE_PATH_ENVVAR);

			for (env_s = 0; envvars[env_s] != NULL; ++env_s) {
				if (font_path_slot < 0 && strlen (envvars[env_s]) > font_path_len)
					if (strncmp (envvars[env_s], ASFONT_PATH_ENVVAR, font_path_len)
							== 0)
						font_path_slot = env_s;
				if (image_path_slot < 0
						&& strlen (envvars[env_s]) > image_path_len)
					if (strncmp (envvars[env_s], ASIMAGE_PATH_ENVVAR, image_path_len)
							== 0)
						image_path_slot = env_s;
			}
		}
		if (font_path_slot < 0)
			++env_s;
		if (image_path_slot < 0)
			++env_s;
		envp = safecalloc (env_s + 2, sizeof (char *));

		/* environment variabless to pass to child process */
		if (envvars) {
			int dst = 0;

			for (env_s = 0; envvars[env_s] != NULL; ++env_s) {
				/* don't want to path DESKTOP_AUTOSTART_ID to our children -
				   its set by gnome-session for AfterStep proper specifically,
				   otherwise children will attempt to re-use it for SessionManagement registration, failing miserably */
				if (strlen (envvars[env_s]) < sizeof (SESSION_ID_ENVVAR)
						|| strncmp (envvars[env_s], SESSION_ID_ENVVAR,
												sizeof (SESSION_ID_ENVVAR) - 1) != 0)
					envp[dst++] = envvars[env_s];
			}
			env_s = dst;
		}

		envp[env_s] =
				safemalloc (8 + strlen (orig_display ? orig_display : display) +
										1);
		sprintf (envp[env_s], "DISPLAY=%s",
						 orig_display ? orig_display : display);
		++env_s;
		if (Environment) {
			if (Environment->pixmap_path != NULL) {
				int slot_no = image_path_slot;

				if (slot_no < 0)
					slot_no = env_s++;

				envp[slot_no] =
						safemalloc (strlen (ASIMAGE_PATH_ENVVAR) + 1 +
												strlen (Environment->pixmap_path) + 1);
				sprintf (envp[slot_no], "%s=%s", ASIMAGE_PATH_ENVVAR,
								 Environment->pixmap_path);
			}
			if (Environment->font_path) {
				int slot_no = font_path_slot;

				if (slot_no < 0)
					slot_no = env_s++;
				envp[slot_no] =
						safemalloc (strlen (ASFONT_PATH_ENVVAR) + 1 +
												strlen (Environment->font_path) + 1);
				sprintf (envp[slot_no], "%s=%s", ASFONT_PATH_ENVVAR,
								 Environment->font_path);
			}
		}

		len = strlen ((char *)cmd);
		if (pass_args) {
/*
			This bit of code seems to break AS restarting
			on Fedora 8. causing DISPLAY=":0.0" to
			become DISPLAY=":0.".  -- Jeremy
			Fixed by moving code under if(screen_str) -- Sasha Vasko
*/
			if (screen >= 0)
				screen_str = string_from_int (screen);

			if (screen_str) {
				register int i = 0;

				while (display[i])	++i;
				while (i > 0 && isdigit (display[--i])) ;
				if (display[i] == '.')
					display[i] = '\0';
			}

			if (w != None)
				w_str = string_from_int (w);
			if (context != C_NO_CONTEXT)
				context_str = string_from_int (context);

				len += 1 + 2 + 1 + strlen (orig_display ? orig_display : display);
				if (screen_str)
					len += strlen (screen_str) + (orig_display ? 0 : 1);
			len += 3;									/* for "-s " */
			if (get_flags (as_app_args.flags, ASS_Debugging))
				len += 8;
			if (get_flags (as_app_args.flags, ASS_Restarting))
				len += 3;
			if (as_app_args.override_config)
				len += 4 + strlen (as_app_args.override_config);
			if (as_app_args.override_home)
				len += 4 + strlen (as_app_args.override_home);
			if (as_app_args.override_share)
				len += 4 + strlen (as_app_args.override_share);

			if (as_app_args.locale)
				len += 4 + strlen (as_app_args.locale);

			if (as_app_args.verbosity_level != OUTPUT_DEFAULT_THRESHOLD)
				len += 4 + 32;
#ifdef DEBUG_TRACE_X
			if (as_app_args.trace_calls)
				len += 13 + strlen (as_app_args.trace_calls);
#endif
			if (w_str)
				len += 1 + 8 + 1 + strlen (w_str);
			if (context_str)
				len += 1 + 9 + 1 + strlen (context_str);
		}
		/* now we want to append arbitrary number of arguments to the end of command line : */
		va_start (ap, pass_args);
		while ((arg = va_arg (ap, char *)) != NULL)
			 len += 1 + strlen (arg);

		va_end (ap);

		len += 4;

		ptr = cmdl = safemalloc (len);
		strcpy (cmdl, (char *)cmd);
		while (*ptr)
			ptr++;
		if (pass_args) {
			if (orig_display)
				ptr += sprintf (ptr, " -d %s -s", orig_display);
			else if (screen_str)
				ptr += sprintf (ptr, " -d %s.%s -s", display, screen_str);
			else
				ptr += sprintf (ptr, " -d %s -s", display);


			if (get_flags (as_app_args.flags, ASS_Debugging)) {
				strcpy (ptr, " --debug");
				ptr += 8;
			}
			if (get_flags (as_app_args.flags, ASS_Restarting)) {
				strcpy (ptr, " -r");
				ptr += 3;
			}
			if (as_app_args.override_config)
				ptr += sprintf (ptr, " -f %s", as_app_args.override_config);
			if (as_app_args.override_home)
				ptr += sprintf (ptr, " -p %s", as_app_args.override_home);
			if (as_app_args.override_share)
				ptr += sprintf (ptr, " -g %s", as_app_args.override_share);
			if (as_app_args.verbosity_level != OUTPUT_DEFAULT_THRESHOLD)
				ptr += sprintf (ptr, " -V %d", as_app_args.verbosity_level);
			LOCAL_DEBUG_OUT
					("len = %d, cmdl = \"%s\" strlen = %d, locale = \"%s\", ptr-cmdl = %d",
					 len, cmdl, (int)strlen (cmdl), as_app_args.locale,
					 (int)(ptr - cmdl));
			if (as_app_args.locale && as_app_args.locale[0]
					&& !isspace (as_app_args.locale[0]))
				ptr += sprintf (ptr, " -L %s", as_app_args.locale);

#ifdef DEBUG_TRACE_X
			if (as_app_args.trace_calls)
				ptr += sprintf (ptr, " --trace-func %s", as_app_args.trace_calls);
#endif
			if (w_str)
				ptr += sprintf (ptr, " --window %s", w_str);
			if (context_str)
				ptr += sprintf (ptr, " --context %s", context_str);
		}

		va_start (ap, pass_args);
		while ((arg = va_arg (ap, char *)) != NULL) {
			*(ptr++) = ' ';
			strcpy (ptr, arg);
			while (*ptr)
				ptr++;
			LOCAL_DEBUG_OUT ("len = %d, cmdl = \"%s\" strlen = %d", len, cmdl,
											 (int)strlen (cmdl));

		}
		va_end (ap);
		if (do_fork) {
			int i = ptr - cmdl;

			while (--i >= 0)
				if (!isspace (cmdl[i]))
					break;
			do_fork = (i < 0 || cmdl[i] != '&');
		}
		strcpy (ptr, do_fork ? " &\n" : "\n");

		LOCAL_DEBUG_OUT ("len = %d, cmdl = \"%s\" strlen = %d", len, cmdl,
										 (int)strlen (cmdl));
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
		{
			FILE *fff = fopen ("/tmp/afterstep.exec.log", "a");

			if (fff) {
				fprintf (fff, "%s:%ld: [%s]", MyName, time (NULL), cmdl);
				fclose (fff);
			}
		}
#endif

		LOCAL_DEBUG_OUT ("execle(\"%s\")", cmdl);
		/* fprintf( stderr, "len=%d: execl(\"%s\")", len, cmdl ); */

		/* CYGWIN does not handle close-on-exec gracefully - whave to do it ourselves : */
		if (CloseOnExec)
			CloseOnExec ();

		{
			const char *shell;
			char *argv0;

			if ((shell = getenv ("SHELL")) == NULL || *shell == '\0')
				shell = mystrdup ("/bin/sh");

			parse_file_name (shell, NULL, &argv0);
			/* argv0 = basename(shell); */

			execle (shell, argv0, "-c", cmdl, (char *)0, envp);
		}

		if (screen >= 0)
			show_error ("failed to start %s on the screen %d", cmd, screen);
		else
			show_error ("failed to start %s", cmd);
		show_system_error (" complete command line: \"%s\"\n", cmdl);
		exit (128);
	}
}

/******************************************************************************/
/* web download functions : */
/******************************************************************************/
Bool is_url (const char *url)
{
	return (strncmp (url, "http://", 7) == 0
					|| strncmp (url, "ftp://", 6) == 0);
}

char *make_log_name (const char *cachedFileName)
{
	char *logName = safemalloc (strlen (cachedFileName) + 4 + 1);

	sprintf (logName, "%s.log", cachedFileName);
	return logName;
}

int spawn_download (const char *url, const char *cachedFileName)
{
	/* cmdl: wget -b -nv -o <escaped_url>.log -O <cache_file> <url> */
#define WGET_CMDL_HEADER "wget -b -nv -o "
	int cacheFNLen = strlen (cachedFileName);
	char *wget_cmdl =
			safemalloc (sizeof (WGET_CMDL_HEADER) + cacheFNLen + 4 + 1 + 2 + 1 +
									cacheFNLen + 1 + strlen (url) + 1);
	int res;

	sprintf (wget_cmdl, WGET_CMDL_HEADER "%s.log -O %s %s", cachedFileName,
					 cachedFileName, url);
	LOCAL_DEBUG_OUT ("spawning \"%s\"", wget_cmdl);
	res =
			spawn_child (wget_cmdl, -1, -1, NULL, None, C_NO_CONTEXT, True,
									 False, NULL);
	free (wget_cmdl);
	return res;
}

Bool
check_download_complete (int pid, const char *cachedFileName,
												 int *sizeDownloaded, int *size)
{
	Bool complete = False;

	if (pid != 0) {
		/* TODO : possibly check if process exited ? */

	}
	/* verify log file */
	{
		char *logName = make_log_name (cachedFileName);
		char *log = logName ? load_file (logName) : NULL;

		if (log && log[0] != '\0') {
			int s1 = 0, s2 = 1;

			char *openBracket = strchr (log, '[');

			LOCAL_DEBUG_OUT ("Open Bracket:%s", openBracket);
			if (openBracket) {
				sscanf (openBracket + 1, "%d/%d", &s1, &s2);
				LOCAL_DEBUG_OUT ("s1 = %d, s2 = %d", s1, s2);
			}
			if (sizeDownloaded)
				*sizeDownloaded = s1;
			if (size)
				*size = s2;
			complete = True;
		}
		if (log)
			free (log);
		if (logName)
			free (logName);
	}
	return complete;
}


/************ end child process spawning ***************************/
