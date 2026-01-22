/*
 * This is the complete from scratch rewrite of the original WinList
 * module.
 *
 * Copyright (C) 2003 Sasha Vasko <sasha at aftercode.net>
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "wintabs_internal.h"

/**********************************************************************/
/*  AfterStep specific global variables :                             */
/**********************************************************************/
/**********************************************************************/
/*  Gadget local variables :                                         */
/**********************************************************************/
ASWinTabsState WinTabsState = { 0 };
/**********************************************************************/
/**********************************************************************/
/* Our configuration options :                                        */
/**********************************************************************/
/*char *default_unfocused_style = "unfocused_window_style";
 char *default_focused_style = "focused_window_style";
 char *default_sticky_style = "sticky_window_style";
 */

WinTabsConfig *Config = NULL ;
/**********************************************************************/

char *pattern_override = NULL ;
char *exclude_pattern_override = NULL ;
char *title_override = NULL, *icon_title_override = NULL ;
char *border_color_override = NULL ;

CommandLineOpts WinTabs_cmdl_options[] =
{
	{NULL, "pattern","Overrides module's inclusion pattern", NULL,
	 handler_set_string, &pattern_override, 0, CMO_HasArgs },

	{NULL, "exclude-pattern","Overrides module's exclusion pattern", NULL,
	 handler_set_string, &exclude_pattern_override, 0, CMO_HasArgs },

	{NULL, "all-desks","Swallow windows from any desktop", NULL, handler_set_flag, &(WinTabsState.flags),
	 ASWT_AllDesks, 0 },

	{NULL, "title","Overrides module's main window title", NULL, handler_set_string,
	 &title_override, 0, CMO_HasArgs },

	{NULL, "icon-title","Overrides module's title in iconic state", NULL, handler_set_string,
	 &icon_title_override, 0, CMO_HasArgs },

	{NULL, "skip-transients","Disregard any transient window(dialog)", NULL, handler_set_flag,
	 &(WinTabsState.flags), ASWT_SkipTransients, 0 },

	{NULL, "desktops","Swallow any Desktop window (from GNOME/KDE etc.", NULL, handler_set_flag,
	 &(WinTabsState.flags), ASWT_Desktops, 0 },

	{"tr", "transparent","keep window border transparent", NULL, handler_set_flag,
	 &(WinTabsState.flags), ASWT_WantTransparent, 0 },

	{"bc", "border-color","use color to fill border around windows", NULL, handler_set_string,
	 &border_color_override, 0, CMO_HasArgs },

	{NULL, NULL, NULL, NULL, NULL, NULL, 0, 0 }
};


void
WinTabs_usage (void)
{
	printf (OPTION_USAGE_FORMAT " [--pattern <pattern>] \n\t[--exclude-pattern <pattern>]\n\t[--all-desks] [--title <title>] [--icon-title <icon_title>]\n", MyName );
	print_command_line_opt("standard_options are ", as_standard_cmdl_options, ASS_Restarting);
	print_command_line_opt("additional options are ", WinTabs_cmdl_options, 0);
	exit (0);
}

void OnDisconnect( int nonsense )
{
   	int i = PVECTOR_USED(WinTabsState.tabs);
LOCAL_DEBUG_OUT( "remaining clients %d", i );
    if( i > 0 )
	{
		clear_flags( WinTabsState.flags, ASWT_ShutDownInProgress);
		SetAfterStepDisconnected();
		window_data_cleanup();
		XSelectInput( dpy, Scr.Root, EnterWindowMask|PropertyChangeMask );
		/* Scr.wmprops->selection_window = None ; */
		/* don't really want to terminate as that will kill our clients.
		 * running without AfterStep is not really bad, except that we won't be
		 * able to swallow any new windows */
	}else
		DeadPipe(0);
}


int
main( int argc, char **argv )
{
    int i ;
    register int opt;
	/* Save our program name - for error messages */
	set_DeadPipe_handler(DeadPipe);
    InitMyApp (CLASS_GADGET, argc, argv, NULL, WinTabs_usage, ASS_Restarting );
	LinkAfterStepConfig();

    set_signal_handler( SIGSEGV );

	for( i = 1 ; i< argc ; ++i)
	{
		LOCAL_DEBUG_OUT( "argv[%d] = \"%s\", original argv[%d] = \"%s\"", i, argv[i], i, MyArgs.saved_argv[i]);
		if( argv[i] != NULL )
		{
			if( ( opt = match_command_line_opt( &(argv[i][0]), WinTabs_cmdl_options ) ) < 0)
			 	continue;

			/* command-line-option 'opt' has been matched */

			if( get_flags ( WinTabs_cmdl_options[opt].flags, CMO_HasArgs) )
			 	if( ++i >= argc)
				 	continue;

			WinTabs_cmdl_options[opt].handler( argv[i], WinTabs_cmdl_options[opt].trg,
							WinTabs_cmdl_options[opt].param);

		}
	}

    ConnectX( ASDefaultScr, EnterWindowMask );
    ConnectAfterStep ( WINTABS_MESSAGE_MASK, 0 );               /* no AfterStep */

	WinTabsState.border_color = Scr.asv->black_pixel; /* default */

	signal (SIGPIPE, OnDisconnect);

	signal (SIGTERM, DeadPipe);
    signal (SIGKILL, DeadPipe);

    Config = CreateWinTabsConfig ();

    LoadBaseConfig ( GetBaseOptions);
	LoadColorScheme();
	LoadConfig ("wintabs", GetOptions);
    CheckConfigSanity(pattern_override, exclude_pattern_override, title_override, icon_title_override);
	SetWinTabsLook();

	SendInfo ("Send_WindowList", 0);

    WinTabsState.selected_client = None ;
	WinTabsState.tabs = create_asvector( sizeof(ASWinTab) );

    WinTabsState.main_window = make_wintabs_window();
	set_frame_background(NULL);
    WinTabsState.main_canvas = create_ascanvas_container( WinTabsState.main_window );
    WinTabsState.tabs_window = make_tabs_window( WinTabsState.main_window );
    WinTabsState.tabs_canvas = create_ascanvas( WinTabsState.tabs_window );
    map_canvas_window( WinTabsState.tabs_canvas, True );
    set_root_clip_area(WinTabsState.main_canvas );

	/* delay mapping main canvas untill we actually swallowed something ! */
	if( WinTabsState.pattern_wrexp == NULL || !get_flags(Config->flags, ASWT_HideWhenEmpty ))
	{
		map_canvas_window( WinTabsState.main_canvas, True );
		set_flags( WinTabsState.flags, ASWT_StateMapped );
		rearrange_tabs( False );
	}

    /* map_canvas_window( WinTabsState.main_canvas, True ); */
    /* final cleanup */
	XFlush (dpy);
	sleep (1);								   /* we have to give AS a chance to spot us */

	/* And at long last our main loop : */
    HandleEvents();
	return 0 ;
}


void
DeadPipe (int nonsense)
{
	{
		static int already_dead = False ;
		if( already_dead ) 	return;/* non-reentrant function ! */
		already_dead = True ;
	}

LOCAL_DEBUG_OUT( "DeadPipe%s", "" );
	if( !get_flags( WinTabsState.flags, ASWT_ShutDownInProgress) )
	{
		LOCAL_DEBUG_OUT( "reparenting clients back to the Root%s","" );

		while( unswallow_current_tab() );

		ASSync(False );
    }

	while( PVECTOR_USED( WinTabsState.tabs ) )
		delete_tab(0);
	destroy_string( &(WinTabsState.banner.name) );
   	destroy_astbar( &(WinTabsState.banner.bar) );
   	destroy_ascanvas( &(WinTabsState.banner.client_canvas) );
   	destroy_ascanvas( &(WinTabsState.banner.frame_canvas) );

	destroy_asvector( &WinTabsState.tabs );
   	destroy_ascanvas( &WinTabsState.tabs_canvas );
   	destroy_ascanvas( &WinTabsState.main_canvas );

	destroy_astbar_props( &(WinTabsState.tbar_props) );
	free_button_resources( &WinTabsState.close_button );
	free_button_resources( &WinTabsState.unswallow_button );
	free_button_resources( &WinTabsState.menu_button );


    if( WinTabsState.main_window )
        XDestroyWindow( dpy, WinTabsState.main_window );
	ASSync(False );
	fflush(stderr);

	window_data_cleanup();
    FreeMyAppResources();

    if( Config )
        DestroyWinTabsConfig(Config);

#ifdef DEBUG_ALLOCS
    print_unfreed_mem ();
#endif /* DEBUG_ALLOCS */

    XFlush (dpy);			/* need this for SetErootPixmap to take effect */
	XCloseDisplay (dpy);		/* need this for SetErootPixmap to take effect */
    exit (0);
}
/**************************************************************************
 * add/remove a tab code
 **************************************************************************/
void
set_tab_look( ASWinTab *aswt, Bool no_bevel )
{
	set_astbar_style_ptr (aswt->bar, -1, Scr.Look.MSWindow[BACK_UNFOCUSED]);
	set_astbar_style_ptr (aswt->bar, BAR_STATE_FOCUSED,   Scr.Look.MSWindow[BACK_FOCUSED]);
	if( no_bevel )
	{
		set_astbar_hilite( aswt->bar, BAR_STATE_UNFOCUSED, NO_HILITE|NO_HILITE_OUTLINE );
		set_astbar_hilite( aswt->bar, BAR_STATE_FOCUSED,   NO_HILITE|NO_HILITE_OUTLINE );
	}else
	{
		if( get_flags( Config->set_flags, WINTABS_FBevel ) || WinTabsState.tbar_props == NULL )
			set_astbar_hilite( aswt->bar, BAR_STATE_FOCUSED,   Config->fbevel );
		else
			set_astbar_hilite( aswt->bar, BAR_STATE_FOCUSED,   WinTabsState.tbar_props->bevel );
		if( get_flags( Config->set_flags, WINTABS_UBevel ) || WinTabsState.tbar_props == NULL)
			set_astbar_hilite( aswt->bar, BAR_STATE_UNFOCUSED, Config->ubevel );
		else
			set_astbar_hilite( aswt->bar, BAR_STATE_UNFOCUSED, WinTabsState.tbar_props->bevel );
	}
	set_astbar_composition_method( aswt->bar, BAR_STATE_UNFOCUSED, Config->ucm );
	set_astbar_composition_method( aswt->bar, BAR_STATE_FOCUSED,   Config->fcm );
}

void
set_tab_title( ASWinTab *aswt )
{
	int align = Config->name_aligment ;
	int h_spacing = Config->h_spacing ;
	int v_spacing = Config->v_spacing ;
	char *display_name = aswt->name;

	if( WinTabsState.tbar_props )
	{
		if( !get_flags( Config->set_flags, WINTABS_Align ) )
			align = WinTabsState.tbar_props->align ;
		if( !get_flags( Config->set_flags, WINTABS_H_SPACING ) )
			h_spacing = WinTabsState.tbar_props->title_h_spacing ;
		if( !get_flags( Config->set_flags, WINTABS_V_SPACING ) )
			v_spacing = WinTabsState.tbar_props->title_v_spacing ;
	}

	delete_astbar_tile( aswt->bar, 0 );
	if (aswt->group && !aswt->group_owner)
	{
		int name_len = strlen (aswt->name);
		display_name = safemalloc (name_len + 16);
		name_len -= aswt->group->pattern_length;
		if (name_len == 0)
		{
			sprintf (display_name, "#%d", aswt->group_seqno);
		}else
		{
			if (aswt->group->pattern_is_tail)
				strncpy (display_name, aswt->name, name_len);
			else
				strcpy (display_name, aswt->name + aswt->group->pattern_length);
			display_name[name_len] = '\0';
		}
	}

	add_astbar_label( aswt->bar, 0, 0, 0, align, h_spacing, v_spacing, display_name, aswt->name_encoding);
	if (display_name != aswt->name)
		free (display_name);
}

ASWinTab *
add_tab( Window client, const char *name, INT32 encoding )
{
	ASWinTab aswt ;

	memset (&aswt, 0x00, sizeof(aswt));
	aswt.client = client ;
	aswt.name = mystrdup(name);
	aswt.name_encoding = encoding;

	aswt.bar = create_astbar();
	set_tab_look( &aswt, False );
	set_tab_title( &aswt );
	append_vector( WinTabsState.tabs, &aswt, 1 );

	delete_astbar_tile( WinTabsState.banner.bar, BANNER_LABEL_IDX );

    return PVECTOR_TAIL(ASWinTab,WinTabsState.tabs)-1;
}

void
delete_tab( int index )
{
	int tabs_num = PVECTOR_USED(WinTabsState.tabs) ;
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
	Window client_to_select = None ;
	int owner_index = -1;

	if( index >= tabs_num )
		return ;

    if ( WinTabsState.selected_client == tabs[index].client)
    {
		int i, tab_to_select = -1;

        WinTabsState.selected_client = None ;
		for( i = 0 ; i < tabs_num ; ++i )
			if( i != index )
			{
				if( tab_to_select >= 0 )
					if( tabs[tab_to_select].last_selected >= tabs[i].last_selected )
						continue;
				tab_to_select = i ;
			}
		client_to_select = tabs[tab_to_select].client;
    }
    destroy_astbar( &(tabs[index].bar) );
	/* we still want to receive DestroyNotify events */
	if (tabs[index].client)
	{
    	XSelectInput (dpy, tabs[index].client, StructureNotifyMask);
	    XRemoveFromSaveSet (dpy, tabs[index].client);
	}
	if (tabs[index].client_canvas)
	    destroy_ascanvas( &(tabs[index].client_canvas) );

	if (tabs[index].frame_canvas)
	{
		XDestroyWindow(dpy, tabs[index].frame_canvas->w );
		destroy_ascanvas( &(tabs[index].frame_canvas) );
	}
    if (tabs[index].name)
        free (tabs[index].name);

	if (tabs[index].group && !tabs[index].group_owner)
	{
		int i;
		int members_left = 0;
		for (i = 0 ; i < tabs_num ; ++i)
			if ( i != index && tabs[i].group == tabs[index].group)
			{
				if (tabs[i].group_owner)
					owner_index = i;
				else
					++members_left;
			}
		if (members_left)
		{
			if (client_to_select)
		        set_astbar_focused(tabs[owner_index].bar, WinTabsState.tabs_canvas, False);
			owner_index = -1;
		}

	}

    vector_remove_index (WinTabsState.tabs, index);
	if (owner_index > index)
		--owner_index;

	if (owner_index >= 0)
	    vector_remove_index (WinTabsState.tabs, owner_index);

	if( PVECTOR_USED(WinTabsState.tabs) == 0 )
	{
		if (get_module_out_fd () < 0)
			DeadPipe (0);
		show_hint (False);
	}else if (client_to_select != None)
		select_tab (find_tab_for_client (client_to_select));
}

void
select_tab (int tab)
{
	ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
	ASStatusHints status = {0};
	ASFlagType default_status_flags = get_flags(WinTabsState.flags,ASWT_StateShaded)?AS_Shaded:0;
	int current_selection;

	default_status_flags |= get_flags(WinTabsState.flags,ASWT_StateSticky)?AS_Sticky:0;

    if( tab < 0 || tab >= PVECTOR_USED( WinTabsState.tabs ) )
		tab = 0 ;

	if (tabs[tab].group_owner)
	{
		if ((tab = find_tab_for_client (tabs[tab].group->selected_client)) < 0)
			if ((tab = find_tab_for_group (tabs[tab].group, 0))<0)
				return;
	}

    if( tabs[tab].client == WinTabsState.selected_client )
        return ;

	current_selection = find_tab_for_client (WinTabsState.selected_client);

    if (current_selection >= 0)
    {
		status.flags = AS_Hidden|default_status_flags ;
		set_client_state (WinTabsState.selected_client, &status);
        set_astbar_focused(tabs[current_selection].bar, WinTabsState.tabs_canvas, False);
        WinTabsState.selected_client = None ;

		if (tabs[current_selection].group && tabs[current_selection].group != tabs[tab].group)
		{
			int old_group_owner = find_group_owner (tabs[current_selection].group);
			if (old_group_owner >= 0)
		        set_astbar_focused(tabs[old_group_owner].bar, WinTabsState.tabs_canvas, False);
		}
    }

	status.flags = default_status_flags ;
	set_client_state (tabs[tab].client, &status);
    set_astbar_focused(tabs[tab].bar, WinTabsState.tabs_canvas, True);
	if (tabs[tab].frame_canvas)
	    XRaiseWindow( dpy, tabs[tab].frame_canvas->w );

    WinTabsState.selected_client = tabs[tab].client;

	if (tabs[tab].group)
	{
		int new_group_owner = find_group_owner (tabs[tab].group);
		tabs[tab].group->selected_client = tabs[tab].client;
        set_astbar_focused(tabs[new_group_owner].bar, WinTabsState.tabs_canvas, True);
	}

	tabs[tab].last_selected = time(NULL);
	ASSync(False);
	update_focus();
}

void
press_tab( int tab )
{
	int curr = find_tab_pressed ();

    if (tab == curr)
        return ;
    if (curr >= 0)
    {
        set_astbar_pressed(PVECTOR_HEAD(ASWinTab,WinTabsState.tabs)[curr].bar, WinTabsState.tabs_canvas, False);
    }
    if( tab >= 0 && tab < PVECTOR_USED( WinTabsState.tabs ) )
	    {
	        ASWinTab *new_tab = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs)+tab ;
	        set_astbar_pressed(new_tab->bar, WinTabsState.tabs_canvas, True);
	    }
}
