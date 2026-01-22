/* WinTabs event loop and dispatch extracted from WinTabs.c */

#include "wintabs_internal.h"

void HandleEvents()
{
    ASEvent event;
    Bool has_x_events = False ;
    while (True)
    {
        while((has_x_events = XPending (dpy)))
        {
            if( ASNextEvent (&(event.x), True) )
            {
                event.client = NULL ;
                setup_asevent_from_xevent( &event );
                DispatchEvent( &event );
            }
        }
        module_wait_pipes_input ( process_message );
    }
}

/****************************************************************************/
/* PROCESSING OF AFTERSTEP MESSAGES :                                       */
/****************************************************************************/
void
send_swallowed_configure_notify(ASWinTab *aswt)
{
    if( aswt->client_canvas )
    {
		send_canvas_configure_notify(WinTabsState.main_canvas, aswt->client_canvas );
    }
}

void
process_message (send_data_type type, send_data_type *body)
{
    LOCAL_DEBUG_OUT( "received message %lX", type );
	if( (type&WINDOW_PACKET_MASK) != 0 )
	{
		struct ASWindowData *wd = fetch_window_by_id( body[0] );
        WindowPacketResult res ;
        /* saving relevant client info since handle_window_packet could destroy the actuall structure */
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
        Window               saved_w = (wd && wd->canvas)?wd->canvas->w:None;
        int                  saved_desk = wd?wd->desk:INVALID_DESK;
        struct ASWindowData *saved_wd = wd ;
#endif
        LOCAL_DEBUG_OUT( "message %lX window %lX data %p", type, body[0], wd );
        res = handle_window_packet( type, body, &wd );
        LOCAL_DEBUG_OUT( "\t res = %d, data %p", res, wd );
        if( (res == WP_DataCreated || res == WP_DataChanged) && WinTabsState.pattern_wrexp != NULL )
        {
			if( wd->window_name != NULL )  /* must wait for all the names transferred */
            	check_swallow_window( wd );
        }else if( res == WP_DataDeleted )
        {
            LOCAL_DEBUG_OUT( "client deleted (%p)->window(%lX)->desk(%d)", saved_wd, saved_w, saved_desk );
        }
    }else if( type == M_SWALLOW_WINDOW )
	{
        struct ASWindowData *wd = fetch_window_by_id( body[0] );
		LOCAL_DEBUG_OUT( "SwallowWindow requested for window %lX/frame %lX, wd = %p ", body[0], body[1], wd );
		if( wd )
			do_swallow_window( wd );
	}else if( type == M_SHUTDOWN )
	{
		set_flags( WinTabsState.flags, ASWT_ShutDownInProgress);
		if( get_module_out_fd() >= 0 )
			close( get_module_out_fd() );
		OnDisconnect(0);
	}

}

Bool
on_tabs_canvas_config()
{
	int tabs_num  = PVECTOR_USED(WinTabsState.tabs);
    ASWinTab *tabs = PVECTOR_HEAD( ASWinTab, WinTabsState.tabs );
	int tabs_changes = handle_canvas_config( WinTabsState.tabs_canvas );
	ASCanvas *tc = WinTabsState.tabs_canvas;

    if( tabs_changes != 0 )
	{
        int i = tabs_num ;
		Bool rerender_tabs = False ;

		/* no need to redraw if we were moved outside of the visible viewport */
		if (tc->root_x > Scr.MyDisplayWidth || tc->root_y > Scr.MyDisplayHeight
			|| tc->root_x + (int)tc->width <= 0 ||	tc->root_y + (int)tc->height <= 0)
			return 0;

		safe_asimage_destroy (Scr.RootImage);
		Scr.RootImage = NULL;
        set_root_clip_area(WinTabsState.tabs_canvas );

		rerender_tabs = update_astbar_transparency(WinTabsState.banner.bar, WinTabsState.tabs_canvas, True);
        while( --i >= 0 )
            if( update_astbar_transparency(tabs[i].bar, WinTabsState.tabs_canvas, True) )
                rerender_tabs = True ;
		if( !rerender_tabs )
			clear_flags(tabs_changes, CANVAS_MOVED);
    }
	return tabs_changes;
}

Bool
recheck_swallow_windows(void *data, void *aux_data)
{
	ASWindowData *wd = (ASWindowData *)data;
	check_swallow_window( wd );
	return True;
}


void
DispatchEvent (ASEvent * event)
{
/*    ASWindowData *pointer_wd = NULL ; */
    ASCanvas *mc = WinTabsState.main_canvas ;
    int pointer_tab = -1 ;

    SHOW_EVENT_TRACE(event);

    if( (event->eclass & ASE_POINTER_EVENTS) != 0 )
    {
		int pointer_root_x = event->x.xkey.x_root;
		int pointer_root_y = event->x.xkey.y_root;
		static int last_pointer_root_x = -1, last_pointer_root_y = -1;

		if( (event->eclass & ASE_POINTER_EVENTS) != 0 && is_balloon_click( &(event->x) ) )
		{
			withdraw_balloon(NULL);
			return;
		}

		pointer_tab  = find_tab_by_position( pointer_root_x, pointer_root_y );
        LOCAL_DEBUG_OUT( "pointer at %d,%d - pointer_tab = %d", event->x.xmotion.x_root, event->x.xmotion.y_root, pointer_tab );
		if( pointer_tab == BANNER_TAB_INDEX )
		{
            int tbar_context ;
			if( (tbar_context = check_astbar_point( WinTabsState.banner.bar, pointer_root_x, pointer_root_y )) != C_NO_CONTEXT )
			{
	            event->context = tbar_context ;
	        	on_astbar_pointer_action( WinTabsState.banner.bar, tbar_context,
								  		 (event->x.type == LeaveNotify),
								  		 (last_pointer_root_x != pointer_root_x || last_pointer_root_y != pointer_root_y) );
			}
		}
		last_pointer_root_x = pointer_root_x ;
		last_pointer_root_y = pointer_root_y ;

	}
    LOCAL_DEBUG_OUT( "mc.geom = %dx%d%+d%+d", mc->width, mc->height, mc->root_x, mc->root_y );
    switch (event->x.type)
    {
		case ReparentNotify :
	    case ConfigureNotify:
            {
                int tabs_num  = PVECTOR_USED(WinTabsState.tabs);
                Bool rerender_tabs = False ;

                if( event->w == WinTabsState.main_window )
                {
                    ASFlagType changes = handle_canvas_config( mc );
                    if( get_flags( changes, CANVAS_RESIZED ) )
					{
						if( tabs_num > 0 )
						{
							WinTabsState.win_width = mc->width ;
							WinTabsState.win_height = mc->height ;
						}
                        rearrange_tabs( True );
                    }else if( get_flags( changes, CANVAS_MOVED ) )
                    {
                        int i  = tabs_num;
                        ASWinTab *tabs = PVECTOR_HEAD( ASWinTab, WinTabsState.tabs );

						rerender_tabs = on_tabs_canvas_config();

                        while( --i >= 0 )
							if (!tabs[i].group_owner)
							{
	                            handle_canvas_config( tabs[i].client_canvas );
    	                        send_swallowed_configure_notify(&(tabs[i]));
								if (get_flags (WinTabsState.flags, ASWT_Transparent) && tabs[i].frame_canvas)
									XClearArea (dpy, tabs[i].frame_canvas->w, 0, 0, 0, 0, True);
							}
                    }
                }else if( event->w == WinTabsState.tabs_window )
                {
					rerender_tabs = on_tabs_canvas_config();
                }
                if( rerender_tabs )
                    render_tabs( get_flags( rerender_tabs, CANVAS_RESIZED ));
            }
	        break;
        case KeyPress:
			{
				XKeyEvent *xk = &(event->x.xkey);
				int selected_tab = find_tab_for_client (WinTabsState.selected_client);
				if( xk->keycode == WINTABS_SWITCH_KEYCODE && xk->state == WINTABS_SWITCH_MOD )
				{
					if( selected_tab+1 < PVECTOR_USED( WinTabsState.tabs ) )
				 		select_tab( selected_tab+1 );
					else
						select_tab( 0 );
					/* XBell (dpy, event->scr->screen); */
				}else if( xk->window != WinTabsState.tabs_window )
				{
					if (selected_tab >= 0)
    				{
						xk->window = WinTabsState.selected_client;
        	    		XSendEvent (dpy, xk->window, False, KeyPressMask, &(event->x));
					}
				}

			}
			break;
        case ButtonPress:
            if( pointer_tab >= 0 )
                press_tab( pointer_tab );
			else if( pointer_tab == BANNER_TAB_INDEX )
			{
				switch( event->context )
				{
					case C_MenuButton :		send_swallow_command();		break ;
					case C_CloseButton : 	close_current_tab();    	break ;
					case C_UnswallowButton: unswallow_current_tab();   	break ;
				}
			}
            break;
        case ButtonRelease:
            press_tab( -1 );
            if( pointer_tab >= 0 )
            {
                select_tab( pointer_tab );
            }
			break;
        case EnterNotify :
			if( event->x.xcrossing.window == Scr.Root )
			{
				withdraw_active_balloon();
            }
            break;
        case LeaveNotify :
            break;
		case FocusIn :
  			set_flags(WinTabsState.flags, ASWT_StateFocused );
		    break ;
		case FocusOut :
        clear_flags(WinTabsState.flags, ASWT_StateFocused );
		    break ;
        case MotionNotify :
            if( pointer_tab >= 0 && (event->x.xmotion.state&AllButtonMask) != 0)
            {
                press_tab( pointer_tab );
            }
            break ;
        case UnmapNotify:
            on_unmap_notify(event->w);
            break;
        case DestroyNotify:
            on_destroy_notify(event->w);
            break;
		case Expose :
#if 1
			if ( !get_flags( WinTabsState.flags, ASWT_Transparent ) )
			{
                int i = PVECTOR_USED(WinTabsState.tabs);
                ASWinTab *tabs = PVECTOR_HEAD( ASWinTab, WinTabsState.tabs );
				while( --i >= 0 )
					if( tabs[i].frame_canvas && event->w == tabs[i].frame_canvas->w )
					{
						XExposeEvent *xexp = &(event->x.xexpose);
						XFillRectangle(dpy, tabs[i].frame_canvas->w, Scr.DrawGC, xexp->x, xexp->y, xexp->width, xexp->height);
						break;
					}
			}
#endif
		    break ;
        case ClientMessage:
            if ( event->x.xclient.format == 32 )
			{
				if( event->x.xclient.data.l[0] == _XA_WM_DELETE_WINDOW )
			    	DeadPipe(0);
				else if( event->x.xclient.data.l[0] == _XA_WM_TAKE_FOCUS )
				{
					set_flags(WinTabsState.flags, ASWT_StateFocused);
					update_focus();
				}
			}
	        break;
	    case PropertyNotify:
			if( event->w == Scr.Root || event->w == Scr.wmprops->selection_window )
			{
				if( event->w == Scr.Root && event->x.xproperty.atom == _XA_WIN_SUPPORTING_WM_CHECK )
				{
			    	destroy_wmprops( Scr.wmprops, False );
					Scr.wmprops = setup_wmprops( &Scr, 0, 0xFFFFFFFF, NULL );
					retrieve_wintabs_astbar_props();
					show_banner_buttons();
				}else
					handle_wmprop_event (Scr.wmprops, &(event->x));

				if( event->x.xproperty.atom == _AS_BACKGROUND )
            	{
                	LOCAL_DEBUG_OUT( "root background updated!%s","");
                	safe_asimage_destroy( Scr.RootImage );
                	Scr.RootImage = NULL ;
            	}else if( event->x.xproperty.atom == _AS_STYLE )
				{
                	int i  = PVECTOR_USED(WinTabsState.tabs);
                	ASWinTab *tabs = PVECTOR_HEAD( ASWinTab, WinTabsState.tabs );

                	LOCAL_DEBUG_OUT( "AS Styles updated!%s","");
					mystyle_list_destroy_all(&(Scr.Look.styles_list));
					LoadColorScheme();
					SetWinTabsLook();
					/* now we need to update everything */
					set_frame_background(NULL);
	            	while( --i >= 0 )
					{
						set_frame_background(&(tabs[i]));
                    	set_tab_look( &(tabs[i]), False);
					}
					set_tab_look( &(WinTabsState.banner), True);
                	rearrange_tabs(False );
            	}else if( event->x.xproperty.atom == _AS_TBAR_PROPS )
				{
                	int i = PVECTOR_USED(WinTabsState.tabs);
                	ASWinTab *tabs = PVECTOR_HEAD( ASWinTab, WinTabsState.tabs );
		 			retrieve_wintabs_astbar_props();

					show_banner_buttons();

					set_tab_look( &(WinTabsState.banner), False);
					if( i == 0 )
						show_hint(True);

					while( --i >= 0 )
					{
                    	set_tab_look( &(tabs[i]), False);
						set_tab_title( &(tabs[i]) );
					}

                	rearrange_tabs(False );
            	}else if( event->x.xproperty.atom == _AS_MODULE_SOCKET && get_module_out_fd() < 0 )
				{
					if( ConnectAfterStep(WINTABS_MESSAGE_MASK, 0) >= 0)
					{

						SendInfo ("Send_WindowList", 0);
						XSelectInput( dpy, Scr.Root, EnterWindowMask );
					}
				}
			}else if(   event->w == WinTabsState.main_window )
			{
			 	if( event->x.xproperty.atom == _XA_NET_WM_DESKTOP )
				{
					CARD32 new_desk = read_extwm_desktop_val(WinTabsState.main_window);

					if( !get_flags( WinTabsState.flags, ASWT_AllDesks ))
					{
						if( WinTabsState.my_desktop != new_desk )
						{
							WinTabsState.my_desktop = new_desk ;
							if( WinTabsState.pattern_wrexp != NULL )
								iterate_window_data( recheck_swallow_windows, NULL);
						}
					}else
						WinTabsState.my_desktop = new_desk ;

					update_tabs_desktop();
				}else if( event->x.xproperty.atom == _XA_NET_WM_STATE )
				{
					ASFlagType extwm_flags = 0 ;
					if( get_extwm_state_flags (WinTabsState.main_window, &extwm_flags) )
					{
						ASFlagType new_state = get_flags(extwm_flags, EXTWM_StateShaded)?ASWT_StateShaded:0;

						new_state |= get_flags(extwm_flags, EXTWM_StateSticky)?ASWT_StateSticky:0;
						LOCAL_DEBUG_OUT( "old_state = %lX, new_state = %lX", (WinTabsState.flags&(ASWT_StateShaded|ASWT_StateSticky)), new_state );
						if( (WinTabsState.flags&(ASWT_StateShaded|ASWT_StateSticky)) != new_state )
						{
							clear_flags( WinTabsState.flags, ASWT_StateShaded|ASWT_StateSticky );
							set_flags( WinTabsState.flags, new_state );
							update_tabs_state();
						}
					}

				}
			}else if( IsNameProp(event->x.xproperty.atom) )
			{                  /* Maybe name change on the client !!! */
				handle_tab_name_change( event->w );
			}
			break;
		default:
#ifdef XSHMIMAGE
			LOCAL_DEBUG_OUT( "XSHMIMAGE> EVENT : completion_type = %d, event->type = %d ", Scr.ShmCompletionEventType, event->x.type );
			if( event->x.type == Scr.ShmCompletionEventType )
				handle_ShmCompletion( event );
#endif /* SHAPE */
			break;
    }
}

