/* WinTabs UI/window helpers extracted from WinTabs.c */

#include "wintabs_internal.h"

/********************************************************************/
/* showing our main window :                                        */
/********************************************************************/
void
show_hint( Bool redraw )
{
	char *banner_text ;
	int align = Config->name_aligment ;
	int h_spacing = Config->h_spacing ;
	int v_spacing = Config->v_spacing ;

	if( WinTabsState.tbar_props )
	{
		if( !get_flags( Config->set_flags, WINTABS_Align ) )
			align = WinTabsState.tbar_props->align ;
		if( !get_flags( Config->set_flags, WINTABS_H_SPACING ) )
			h_spacing = WinTabsState.tbar_props->title_h_spacing ;
		if( !get_flags( Config->set_flags, WINTABS_V_SPACING ) )
			v_spacing = WinTabsState.tbar_props->title_v_spacing ;
	}
	if( Config->pattern )
	{
		banner_text = safemalloc( 16 + strlen(Config->pattern) + 1 );
		sprintf( banner_text, "Waiting for %s", Config->pattern );
	}else if (get_flags( WinTabsState.flags, ASWT_Desktops)) {
		banner_text = safemalloc( 64 );
		sprintf( banner_text, "Waiting for Desktop window" );
	}else
	{
		banner_text = safemalloc( 64 );
		sprintf( banner_text, "Waiting for SwallowWindow command" );
	}
	delete_astbar_tile( WinTabsState.banner.bar, BANNER_LABEL_IDX );
	add_astbar_label( WinTabsState.banner.bar, 0, 0, 0, align, h_spacing, v_spacing, banner_text, 0);
	free( banner_text );

	if( redraw )
		rearrange_tabs( False );
}

void
show_banner_buttons()
{
	MyButton *buttons[3] ;
	int buttons_num = 0;
	int h_border = 1 ;
	int v_border = 1 ;
	int spacing = 1 ;

	if( WinTabsState.menu_button.width > 0 )
		buttons[buttons_num++] = &WinTabsState.menu_button ;
	if( WinTabsState.unswallow_button.width > 0 )
		buttons[buttons_num++] = &WinTabsState.unswallow_button ;
	if( WinTabsState.close_button.width > 0 )
		buttons[buttons_num++] = &WinTabsState.close_button ;
	if( WinTabsState.tbar_props )
	{
      	h_border = WinTabsState.tbar_props->buttons_h_border ;
		v_border = WinTabsState.tbar_props->buttons_v_border ;
		spacing = WinTabsState.tbar_props->buttons_spacing ;
	}

	delete_astbar_tile( WinTabsState.banner.bar, -1 );
	if( buttons_num > 0 )
	{
		add_astbar_btnblock(WinTabsState.banner.bar,
  			            	1, 0, 0, ALIGN_CENTER, &buttons[0], 0xFFFFFFFF, buttons_num,
            	        	h_border, v_border, spacing, TBTN_ORDER_L2R );
    	set_astbar_balloon( WinTabsState.banner.bar, C_CloseButton, "Close window in current tab", AS_Text_ASCII );
		set_astbar_balloon( WinTabsState.banner.bar, C_MenuButton, "Select new window to be swallowed", AS_Text_ASCII );
		set_astbar_balloon( WinTabsState.banner.bar, C_UnswallowButton, "Unswallow (release) window in current tab", AS_Text_ASCII );
	}
   	if( PVECTOR_USED(WinTabsState.tabs) == 0 )
		show_hint(False);

}

void
set_frame_background( ASWinTab *aswt )
{
	Window w = (aswt && aswt->frame_canvas)?aswt->frame_canvas->w:WinTabsState.main_window;
	if( get_flags( WinTabsState.flags, ASWT_Transparent ) )
	{
		XSetWindowBackgroundPixmap( dpy, w, ParentRelative);
		LOCAL_DEBUG_OUT( "Is transparent %s", "" );
	}/*else
		XSetWindowBackground( dpy, w, WinTabsState.border_color);*/
	XClearArea (dpy, w, 0, 0, 0, 0, True); /* generate expose events ! */
}

Window
make_wintabs_window()
{
	Window        w;
	XSizeHints    shints;
	ExtendedWMHints extwm_hints ;
	int x, y ;
    unsigned int width = max(Config->geometry.width,1);
    unsigned int height = max(Config->geometry.height,1);
	char *iconic_name ;


	memset( &extwm_hints, 0x00, sizeof(extwm_hints));
    switch( Config->gravity )
	{
		case NorthEastGravity :
            x = Config->anchor_x - width ;
			y = Config->anchor_y ;
			break;
		case SouthEastGravity :
            x = Config->anchor_x - width;
            y = Config->anchor_y - height;
			break;
		case SouthWestGravity :
			x = Config->anchor_x ;
            y = Config->anchor_y - height;
			break;
		case NorthWestGravity :
		default :
			x = Config->anchor_x ;
			y = Config->anchor_y ;
			break;
	}
    LOCAL_DEBUG_OUT( "creating main window with geometry %dx%d%+d%+d", width, height, x, y );

    w = create_visual_window( Scr.asv, Scr.Root, x, y, 1, 1, 0, InputOutput, 0, NULL);
    LOCAL_DEBUG_OUT( "main window created with Id %lX", w);

	iconic_name = Config->icon_title ;
	if( iconic_name == NULL )
	{
		iconic_name = safemalloc( strlen(MyName)+32 );
		sprintf( iconic_name, "%s iconic", MyName );
	}
    set_client_names( w, Config->title?Config->title:MyName,
						 iconic_name,
					  	 AS_MODULE_CLASS, CLASS_WINTABS );
	if( iconic_name != Config->icon_title )
		free( iconic_name );

    shints.flags = USPosition|USSize|PWinGravity;
    shints.win_gravity = Config->gravity ;

	extwm_hints.pid = getpid();
    extwm_hints.flags = EXTWM_PID|EXTWM_TypeSet ;
	extwm_hints.type_flags = EXTWM_TypeASModule|EXTWM_TypeNormal ;


	set_client_hints( w, NULL, &shints, AS_DoesWmDeleteWindow|AS_DoesWmTakeFocus, &extwm_hints );
	set_client_cmd (w);

    /* we will need to wait for PropertyNotify event indicating transition
	   into Withdrawn state, so selecting event mask: */
    XSelectInput (dpy, w, PropertyChangeMask|StructureNotifyMask|FocusChangeMask|KeyPressMask
                          /*|ButtonReleaseMask | ButtonPressMask */
                  );

	WinTabsState.banner.bar = create_astbar();

	set_tab_look( &WinTabsState.banner, True );
	show_banner_buttons();
	/* this must be added after buttons, so that it will have index of 1 */
	show_hint( False );


	return w ;
}

Window
make_tabs_window( Window parent )
{
	static XSetWindowAttributes attr ;
    Window w ;
	attr.event_mask = WINTABS_TAB_EVENT_MASK ;
    w = create_visual_window( Scr.asv, parent, 0, 0, 1, 1, 0, InputOutput, CWEventMask, &attr );
	XGrabKey( dpy, WINTABS_SWITCH_KEYCODE, WINTABS_SWITCH_MOD, w, True, GrabModeAsync, GrabModeAsync);
    return w;
}

Window
make_frame_window( Window parent )
{
	static XSetWindowAttributes attr ;
	ASFlagType attr_mask ;
    Window w ;
	attr.event_mask = SubstructureRedirectMask|FocusChangeMask|KeyPressMask|ExposureMask ;
	attr_mask = CWEventMask ;
    w = create_visual_window( Scr.asv, parent, 0, 0, WinTabsState.win_width, WinTabsState.win_height, 0, InputOutput, attr_mask, &attr );
	XGrabKey( dpy, WINTABS_SWITCH_KEYCODE, WINTABS_SWITCH_MOD, w, True, GrabModeAsync, GrabModeAsync);
    return w;
}

