/*
 * Copyright (C) 1998 Eric Tremblay <deltax@pragma.net>
 * Copyright (c) 1998 Michal Vitecek <fuf@fuf.sh.cvut.cz>
 * Copyright (c) 1998 Doug Alcorn <alcornd@earthlink.net>
 * Copyright (c) 2002,1998 Sasha Vasko <sasha at aftercode.net>
 * Copyright (c) 1997 ric@giccs.georgetown.edu
 * Copyright (C) 1998 Makoto Kato <m_kato@ga2.so-net.ne.jp>
 * Copyright (c) 1997 Guylhem Aznar <guylhem@oeil.qc.ca>
 * Copyright (C) 1996 Rainer M. Canavan (canavan@Zeus.cs.bonn.edu)
 * Copyright (C) 1996 Dan Weeks
 * Copyright (C) 1994 Rob Nation
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "pager_internal.h"

ASPagerState PagerState;
#define DEFAULT_BORDER_COLOR 0xFF808080

#define PAGE_MOVE_THRESHOLD     15	/* precent */

/* Storing window list as hash table hashed by client window ID :     */
ASHashTable *PagerClients = NULL;

PagerConfig *Config = NULL;
int Rows_override = 0;
int Columns_override = 0;

CommandLineOpts Pager_cmdl_options[3] = {
	{NULL, "rows", "Overrides module's layout rows", NULL, handler_set_int,
	 &Rows_override, 0, CMO_HasArgs},
	{NULL, "cols", "Overrides module's layout cols", NULL, handler_set_int,
	 &Columns_override, 0, CMO_HasArgs},
	{NULL, NULL, NULL, NULL, NULL, NULL, 0}
};


void pager_usage (void)
{
	printf (OPTION_USAGE_FORMAT " [--rows rows] [--cols cols] n m\n",
					MyName);
	print_command_line_opt ("standard_options are ",
													as_standard_cmdl_options, ASS_Restarting);
	print_command_line_opt ("additional options are ", Pager_cmdl_options,
													0);
	printf
			("The last two numbers n and m define a range of desktops to be displayed.\n");
	exit (0);
}

void HandleEvents ();
void GetOptions (const char *filename);
void GetBaseOptions (const char *filename);


/***********************************************************************
 *   main - start of module
 ***********************************************************************/
int main (int argc, char **argv)
{
	int i;
	INT32 desk1 = 0, desk2 = 0;
	int desk_cnt = 0;

	/* Save our program name - for error messages */
	set_DeadPipe_handler (DeadPipe);
	InitMyApp (CLASS_PAGER, argc, argv, NULL, pager_usage, ASS_Restarting);
	LinkAfterStepConfig ();

	memset (&PagerState, 0x00, sizeof (PagerState));
	PagerState.page_rows = PagerState.page_columns = 1;

	for (i = 1; i < argc; ++i) {
		LOCAL_DEBUG_OUT ("argv[%d] = \"%s\", original argv[%d] = \"%s\"", i,
										 argv[i], i, MyArgs.saved_argv[i]);
		if (argv[i] != NULL) {
			if (isdigit (argv[i][0])) {
				++desk_cnt;
				if (desk_cnt == 1)
					desk1 = atoi (argv[i]);
				else if (desk_cnt == 2)
					desk2 = atoi (argv[i]);
				else
					break;
			} else if (strcmp (argv[i], "--rows") == 0 && i + 1 < argc
								 && argv[i + 1] != NULL) {
				Rows_override = atoi (argv[++i]);
			} else if (strcmp (argv[i], "--cols") == 0 && i + 1 < argc
								 && argv[i + 1] != NULL) {
				Columns_override = atoi (argv[++i]);
			}
		}
	}
	LOCAL_DEBUG_OUT
			("desk1 = %ld, desk2 = %ld, desks = %ld, start_desk = %ld", desk1,
			 desk2, PagerState.desks_num, PagerState.start_desk);
	if (desk2 < desk1) {
		PagerState.desks_num = (desk1 - desk2) + 1;
		PagerState.start_desk = desk2;
	} else {
		PagerState.desks_num = (desk2 - desk1) + 1;
		PagerState.start_desk = desk1;
	}

	PagerState.desks =
			safecalloc (PagerState.desks_num, sizeof (ASPagerDesk));
	for (i = 0; i < PagerState.desks_num; ++i)
		PagerState.desks[i].desk = PagerState.start_desk + i;

	ConnectX (ASDefaultScr, EnterWindowMask);
	if (ConnectAfterStep (M_ADD_WINDOW |
												M_CONFIGURE_WINDOW |
												M_STATUS_CHANGE |
												M_DESTROY_WINDOW |
												M_FOCUS_CHANGE |
												M_NEW_DESKVIEWPORT |
												M_NEW_BACKGROUND |
												M_WINDOW_NAME |
												M_ICON_NAME |
												M_END_WINDOWLIST | M_STACKING_ORDER, 0) < 0)
		exit (1);										/* no AfterStep */

	Config = CreatePagerConfig (PagerState.desks_num);

	LOCAL_DEBUG_OUT ("parsing Options for \"%s\"", MyName);
	LoadBaseConfig (GetBaseOptions);
	LoadColorScheme ();
	LoadConfig ("pager", GetOptions);

	CheckConfigSanity ();

	/* Create a list of all windows */
	/* Request a list of all windows,
	 * wait for ConfigureWindow packets */
	SendInfo ("Send_WindowList", 0);
	set_flags (PagerState.flags, ASP_ReceivingWindowList);

	PagerState.main_canvas = create_ascanvas (make_pager_window ());
	redecorate_pager_desks ();
	rearrange_pager_desks (False);

	LOCAL_DEBUG_OUT ("starting The Loop ...%s", "");
	HandleEvents ();

	return 0;
}

void DeadPipe (int nonsense)
{
	static int already_dead = False;
	int i;
	MyStyle **ms_desk_back = NULL;

	if (already_dead)
		return;
	already_dead = True;

	window_data_cleanup ();
	destroy_ashash (&PagerClients);
	for (i = 0; i < PagerState.desks_num; ++i) {
		ASPagerDesk *d = &PagerState.desks[i];
		destroy_astbar (&(d->title));
		destroy_astbar (&(d->background));
		destroy_ascanvas (&(d->desk_canvas));
		if (d->separator_bars) {
			int p;
			for (p = 0; p < d->separator_bars_num; ++p)
				if (d->separator_bars[p])
					XDestroyWindow (dpy, d->separator_bars[p]);
			free (d->separator_bars);
			free (d->separator_bar_rects);
		}
		if (d->clients)
			free (d->clients);
		if (d->back)
			safe_asimage_destroy (d->back);
	}
	destroy_ascanvas (&PagerState.main_canvas);
	destroy_ascanvas (&PagerState.icon_canvas);

	for (i = 0; i < 4; ++i) {
		if (PagerState.selection_bars[i])
			XDestroyWindow (dpy, PagerState.selection_bars[i]);
	}

	if (Config)
		ms_desk_back = Config->MSDeskBack;
	if (Config)
		DestroyPagerConfig (Config);
	Config = NULL;
	destroy_astbar_props (&(PagerState.tbar_props));
	free_button_resources (&PagerState.shade_button);

	if (ms_desk_back)
		free (ms_desk_back);
	free (PagerState.desks);

	FreeMyAppResources ();
#ifdef DEBUG_ALLOCS
	print_unfreed_mem ();
#endif													/* DEBUG_ALLOCS */

	XFlush (dpy);
	XCloseDisplay (dpy);
	exit (0);
}
