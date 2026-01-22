#include "wintabs_internal.h"

/**************************************************************************
 * Swallowing code
 **************************************************************************/
void
check_wm_protocols( ASWinTab *aswt )
{
	if (aswt->client)
	{
		ASRawHints hints ;
		memset( &hints, 0x00, sizeof(ASRawHints));
	 	read_wm_protocols ( &hints, aswt->client);
		aswt->wm_protocols = hints.wm_protocols ;
	}
}

void
do_swallow_window( ASWindowData *wd )
{
	Window w;
    int try_num = 0 ;
    ASCanvas *nc ;
    char *name = NULL ;
	INT32 encoding ;
	ASWinTab *aswt = NULL ;
	int gravity = NorthWestGravity ;

	if( wd->client == WinTabsState.main_window || wd->client == None)
		return;

	/* we have a match */
	/* now we actually swallow the window : */
    grab_server();
    /* first lets check if window is still not swallowed : it should have no more then 2 parents before root */
    w = get_parent_window( wd->client );
    LOCAL_DEBUG_OUT( "first parent %lX, root %lX", w, Scr.Root );
	while( w == Scr.Root && ++try_num  < 10 )
	{/* we should wait for AfterSTep to complete AddWindow protocol */
	    /* do the actuall swallowing here : */
    	ungrab_server();
		sleep_a_millisec(200*try_num);
		grab_server();
		w = get_parent_window( wd->client );
		LOCAL_DEBUG_OUT( "attempt %d:first parent %lX, root %lX", try_num, w, Scr.Root );
	}
	if( w == Scr.Root )
	{
		ungrab_server();
		return ;
	}
    if( w != None )
        w = get_parent_window( w );
    LOCAL_DEBUG_OUT( "second parent %lX, root %lX", w, Scr.Root );
    if( w != Scr.Root )
	{
		ungrab_server();
		return ;
	}
    /* its ok - we can swallow it now : */
    /* create swallow object : */
	name = get_window_name( wd, ASN_Name, &encoding );
	aswt = add_tab( wd->client, name, encoding );
    LOCAL_DEBUG_OUT( "crerated new #%d for window \"%s\" client = %8.8lX", PVECTOR_USED(WinTabsState.tabs), name, wd->client );

	if( aswt == NULL )
	{
		ungrab_server();
		return ;
	}

	/* if the window was previously unswallowed by us - we may get UnMapNotify
	   after reparenting, which will make us immediately unswallow this client.
	   To prevent this :  */
    XSelectInput (dpy, wd->client, NoEventMask);

    /* first thing - we reparent window and its icon if there is any */
    nc = aswt->client_canvas = create_ascanvas_container( wd->client );
	aswt->frame_canvas = create_ascanvas_container( make_frame_window(WinTabsState.main_window) );
	set_frame_background(aswt);

	aswt->swallow_location.width = nc->width ;
	aswt->swallow_location.height = nc->height ;
	if( get_flags( wd->hints.flags, PWinGravity )  )
		gravity = wd->hints.win_gravity ;
#if 0
	aswt->swallow_location.x = nc->root_x ;
	aswt->swallow_location.y = nc->root_y ;
#else
	if( gravity == CenterGravity )
	{
		aswt->swallow_location.x = (wd->frame_rect.x + (int)wd->frame_rect.width/2) - (int)nc->width/2 ;
		aswt->swallow_location.y = (wd->frame_rect.y + (int)wd->frame_rect.height/2) - (int)nc->height/2 ;
	}else if( gravity == StaticGravity )
	{
		aswt->swallow_location.x = nc->root_x ;
		aswt->swallow_location.y = nc->root_y ;
	}else
	{
		aswt->swallow_location.x = wd->frame_rect.x ;
		aswt->swallow_location.y = wd->frame_rect.y ;
		if( gravity == NorthEastGravity || gravity == SouthEastGravity )
			aswt->swallow_location.x += (int)wd->frame_rect.width - (int)nc->width ;
		if( gravity == SouthWestGravity || gravity == SouthEastGravity )
			aswt->swallow_location.y += (int)wd->frame_rect.height - (int)nc->height ;
	}
#endif
	aswt->hints = wd->hints ;
	aswt->hints.flags = wd->flags ;
	check_wm_protocols( aswt );
    XReparentWindow( dpy, wd->client, aswt->frame_canvas->w, WinTabsState.win_width - nc->width, WinTabsState.win_height - nc->height );
    XSelectInput (dpy, wd->client, StructureNotifyMask|PropertyChangeMask|FocusChangeMask);
    XAddToSaveSet (dpy, wd->client);
	register_unswallowed_client( wd->client );

	XGrabKey( dpy, WINTABS_SWITCH_KEYCODE, WINTABS_SWITCH_MOD, wd->client, True, GrabModeAsync, GrabModeAsync);
	set_client_desktop (wd->client, WinTabsState.my_desktop );

#if 0   /* TODO : implement support for icons : */
    if( get_flags( wd->client_icon_flags, AS_ClientIcon ) && !get_flags( wd->client_icon_flags, AS_ClientIconPixmap) &&
		wd->icon != None )
    {
        ASCanvas *ic = create_ascanvas_container( wd->icon );
        aswb->swallowed->iconic = ic;
        XReparentWindow( dpy, wd->icon, aswb->canvas->w, (aswb->canvas->width-ic->width)/2, (aswb->canvas->height-ic->height)/2 );
        register_object( wd->icon, (ASMagic*)aswb );
        XSelectInput (dpy, wd->icon, StructureNotifyMask);
        grab_swallowed_canvas_btns(  ic, (aswb->folder!=NULL), withdraw_btn && aswb->parent == WharfState.root_folder);
    }
    aswb->swallowed->current = ( get_flags( wd->state_flags, AS_Iconic ) &&
                                    aswb->swallowed->iconic != NULL )?
                                aswb->swallowed->iconic:aswb->swallowed->normal;
    LOCAL_DEBUG_OUT( "client(%lX)->icon(%lX)->current(%lX)", wd->client, wd->icon, aswb->swallowed->current->w );

#endif
    handle_canvas_config( nc );

	map_canvas_window( aswt->frame_canvas, True );
	map_canvas_window( nc, True );
    send_swallowed_configure_notify(aswt);

	if( !handle_tab_name_change( wd->client ) )
	{
		check_tab_grouping (PVECTOR_USED(WinTabsState.tabs)-1);
	    rearrange_tabs( False );
	}

	/* have to do that AFTER we done with grouping,
	 * so that group owner would get selected :
	 */
    select_tab (find_tab_for_client (wd->client));

    ASSync(False);
    ungrab_server();
}


Bool check_swallow_name( char *name )
{
	if( name )
	{
		if( match_wild_reg_exp( name, WinTabsState.pattern_wrexp) == 0 )
			return True;
	}
	return False ;
}

Bool check_no_swallow_name( char *name )
{
	LOCAL_DEBUG_OUT( "name = \"%s\", excl_wrexp = %p", name, WinTabsState.exclude_pattern_wrexp );
	if( name && WinTabsState.exclude_pattern_wrexp)
	{
		if( match_wild_reg_exp( name, WinTabsState.exclude_pattern_wrexp) == 0 )
			return True;
	}
	return False ;
}

void
check_swallow_window( ASWindowData *wd )
{
	INT32 encoding ;
	ASWinTab *aswt = NULL ;
	int i = 0;

    if( wd == NULL || !get_flags( wd->state_flags, AS_Mapped))
        return;

	if( wd->client == WinTabsState.main_window )
		return;
	if( get_flags( WinTabsState.flags, ASWT_SkipTransients ) )
	{
		if( get_flags( wd->flags, AS_Transient ) )
			return;
	}

 	if (get_flags( WinTabsState.flags, ASWT_Desktops)) {
		ASRawHints    raw ;
 		if (collect_hints( ASDefaultScr, wd->client, HINT_ANY, &raw))	{
			if (get_flags (raw.extwm_hints.type_flags, EXTWM_TypeDesktop)) {
				do_swallow_window( wd );
				return;
			}
			destroy_raw_hints (&raw, True);
		}
	}

	/* first lets check if we have already swallowed this one : */
	i = PVECTOR_USED(WinTabsState.tabs);
	aswt = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
	while( --i >= 0 )
		if( aswt[i].client == wd->client )
			return ;

	if( !get_flags( WinTabsState.flags, ASWT_AllDesks ) )
		if( read_extwm_desktop_val( wd->client ) != WinTabsState.my_desktop )
			return ;

	/* now lets try and match its name : */
	LOCAL_DEBUG_OUT( "name(\"%s\")->icon_name(\"%s\")->res_class(\"%s\")->res_name(\"%s\")",
                     wd->window_name, wd->icon_name, wd->res_class, wd->res_name );

	if( check_unswallowed_client( wd->client ) )
		return;
	if( get_flags( Config->set_flags, WINTABS_PatternType ) )
	{
		if( !check_swallow_name(get_window_name( wd, Config->pattern_type, &encoding )) )
			return ;
	}else
	{
		if( !check_swallow_name( wd->window_name ) &&
			!check_swallow_name( wd->icon_name ) &&
			!check_swallow_name( wd->res_class ) &&
			!check_swallow_name( wd->res_name ) )
		{
			return;
		}
	}
	if( get_flags( Config->set_flags, WINTABS_ExcludePatternType ) )
	{
		if( check_no_swallow_name(get_window_name( wd, Config->exclude_pattern_type, &encoding )) )
			return ;
	}else
	{
		if( check_no_swallow_name( wd->window_name ) ||
			check_no_swallow_name( wd->icon_name ) ||
			check_no_swallow_name( wd->res_class ) ||
			check_no_swallow_name( wd->res_name ) )
		{
			return;
		}
	}

	do_swallow_window( wd );
}

void unregister_client( Window client );

void
on_destroy_notify(Window w)
{
    int i = find_tab_for_client (w);

	unregister_client(w);
	if (i >= 0)
    {
    	delete_tab( i );
        rearrange_tabs( False );
    }
}

void
on_unmap_notify(Window w)
{
	int i = find_tab_for_client (w);
	if (i >= 0)
		unswallow_tab( i );
}


int
find_tab_by_position( int root_x, int root_y )
{
/*    int col = WinListState.columns_num ; */
    int tabs_num  =  PVECTOR_USED(WinTabsState.tabs);
    int i = tabs_num ;
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
    ASCanvas *tc = WinTabsState.tabs_canvas ;

    root_x -= tc->root_x ;
    root_y -= tc->root_y ;
    LOCAL_DEBUG_OUT( "pointer win( %d,%d), tc.size(%dx%d)", root_x, root_y, tc->width, tc->height );
    if( root_x  >= 0 && root_y >= 0 &&
        root_x < tc->width && root_y < tc->height )
    {
		register ASTBarData *bar = WinTabsState.banner.bar ;
        if( bar->win_x <= root_x && bar->win_x+bar->width > root_x &&
            bar->win_y <= root_y && bar->win_y+bar->height > root_y )
			return BANNER_TAB_INDEX;

		for( i = 0 ; i < tabs_num ; ++i )
        {
            bar = tabs[i].bar ;
            LOCAL_DEBUG_OUT( "Checking tab %d at %dx%d%+d%+d", i, bar->width, bar->height, bar->win_x, bar->win_y );
            if( bar->win_x <= root_x && bar->win_x+bar->width > root_x &&
                bar->win_y <= root_y && bar->win_y+bar->height > root_y )
                break;
        }
    }

    return i>= tabs_num ? INVALID_TAB_INDEX : i;
}

void send_swallow_command()
{
	SendTextCommand ( F_SWALLOW_WINDOW, "-", MyName, 0);
}

void close_current_tab()
{
	int curr = find_tab_for_client(WinTabsState.selected_client);
    int tabs_num  =  PVECTOR_USED(WinTabsState.tabs);
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);

	if( tabs_num > 0 && curr >= 0 && tabs[curr].client)
	{
		if( tabs[curr].closed )
			XKillClient( dpy, tabs[curr].client);
		else
		{
			send_wm_protocol_request(tabs[curr].client, _XA_WM_DELETE_WINDOW, CurrentTime);
			tabs[curr].closed = True ;
		}
	}
}

Bool unswallow_tab(int t)
{
    int tabs_num  =  PVECTOR_USED(WinTabsState.tabs);
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
	LOCAL_DEBUG_OUT( "tab = %d, tabs_num = %d", t, tabs_num );
	if( t >= 0 && t < tabs_num && tabs[t].client)
	{
		/* XGrabKey( dpy, WINTABS_SWITCH_KEYCODE, Mod1Mask|Mod2Mask, w, True, GrabModeAsync, GrabModeAsync); */
		XDeleteProperty( dpy, tabs[t].client, _XA_NET_WM_STATE );
		XDeleteProperty( dpy, tabs[t].client, _XA_WIN_STATE );
		XDeleteProperty( dpy, tabs[t].client, _XA_NET_WM_DESKTOP );
		XDeleteProperty( dpy, tabs[t].client, _XA_WIN_WORKSPACE );
		XResizeWindow( dpy, tabs[t].client, tabs[t].swallow_location.width, tabs[t].swallow_location.height );
		XReparentWindow( dpy, tabs[t].client, Scr.Root, tabs[t].swallow_location.x, tabs[t].swallow_location.y );
		delete_tab( t );
		rearrange_tabs( False );
		return True;
	}
	return False;
}

Bool unswallow_current_tab()
{
	return unswallow_tab(find_tab_for_client(WinTabsState.selected_client));
}

void
update_focus()
{
	int curr = find_tab_for_client(WinTabsState.selected_client);
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);

	LOCAL_DEBUG_OUT( "curr = %d, tabs_num = %d, focused = %ld", curr, PVECTOR_USED(WinTabsState.tabs), get_flags(WinTabsState.flags, ASWT_StateFocused ) );
	if( curr >= 0 && tabs[curr].client && get_flags(WinTabsState.flags, ASWT_StateFocused ))
	{
		if( get_flags( tabs[curr].wm_protocols, AS_DoesWmTakeFocus ) )
			send_wm_protocol_request ( tabs[curr].client, _XA_WM_TAKE_FOCUS, Scr.last_Timestamp);
		else
			XSetInputFocus ( dpy, tabs[curr].client, RevertToParent, Scr.last_Timestamp );
	}
}

Bool handle_tab_name_change( Window client)
{
    int tabs_num  =  PVECTOR_USED(WinTabsState.tabs);
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
	int i = find_tab_for_client (client);
	Bool changed = True ;

	if( i >= 0 && i < tabs_num )
	{
		ASRawHints    raw;
		ASHints       clean;
		ASSupportedHints *list = create_hints_list ();

		enable_hints_support (list, HINTS_ICCCM);
		enable_hints_support (list, HINTS_KDE);
		enable_hints_support (list, HINTS_ExtendedWM);

		memset( &raw, 0x00, sizeof(ASRawHints));
		memset( &clean, 0x00, sizeof(ASHints));

		if( collect_hints (ASDefaultScr, tabs[i].client, HINT_NAME, &raw) )
		{
			if( merge_hints (&raw, NULL, NULL, list, HINT_NAME, &clean, tabs[i].client) )
			{
				if( clean.names[0] )
				{
					if( strcmp( clean.names[0], tabs[i].name ) != 0 )
					{
						free( tabs[i].name );
						tabs[i].name = mystrdup( clean.names[0] );
						tabs[i].name_encoding = clean.names_encoding[0] ;
						changed = tabs[i].client ;
					}
				}
				destroy_hints( &clean, True );
			}
			destroy_raw_hints ( &raw, True);
		}
		destroy_hints_list( &list );

LOCAL_DEBUG_OUT ("changed= %d", changed);
		if (changed)
		{
			check_tab_grouping (i);
    		tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
			i = find_tab_for_client (client);
			if( i >= 0  )
				set_tab_title( &(tabs[i]) );
			rearrange_tabs( False );
		}
	}
	return changed ;
}

void
update_tabs_desktop()
{
	int i = PVECTOR_USED(WinTabsState.tabs);
    ASWinTab *tabs = PVECTOR_HEAD( ASWinTab, WinTabsState.tabs );
	while( --i >= 0)
		if (tabs[i].client)
			set_client_desktop (tabs[i].client, WinTabsState.my_desktop );
}

void
update_tabs_state()
{
	ASStatusHints status = {0};
	int i = PVECTOR_USED(WinTabsState.tabs);
    ASWinTab *tabs = PVECTOR_HEAD( ASWinTab, WinTabsState.tabs );

	status.flags = get_flags(WinTabsState.flags,ASWT_StateShaded)?AS_Shaded:0;
	status.flags |= get_flags(WinTabsState.flags,ASWT_StateSticky)?AS_Sticky:0;

	while( --i >= 0)
		if (tabs[i].client)
		{
			if( tabs[i].client == WinTabsState.selected_client )
				clear_flags( status.flags, AS_Hidden );
			else
				set_flags( status.flags, AS_Hidden );
			set_client_state (tabs[i].client, &status);
		}
}

void
register_unswallowed_client( Window client )
{
	if (client)
	{
		if( WinTabsState.unswallowed_apps == NULL )
			WinTabsState.unswallowed_apps = create_ashash( 0, NULL, NULL, NULL );

		add_hash_item( WinTabsState.unswallowed_apps, AS_HASHABLE(client), NULL );
	}
}

void
unregister_client( Window client )
{
	if( WinTabsState.unswallowed_apps)
		remove_hash_item( WinTabsState.unswallowed_apps, AS_HASHABLE(client), NULL, False);
}


Bool
check_unswallowed_client( Window client )
{
	ASHashData hdata = {0} ;

	if( client && WinTabsState.unswallowed_apps != NULL )
		return (get_hash_item( WinTabsState.unswallowed_apps, AS_HASHABLE(client), &hdata.vptr ) == ASH_Success );
	return False;
}
