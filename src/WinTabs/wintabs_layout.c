/* WinTabs tab layout/render helpers extracted from WinTabs.c */

#include "wintabs_internal.h"

static inline int
get_restricted_width (ASWinTab *tab, int max_width)
{
    if( tab->calculated_width < Config->min_tab_width )
        return Config->min_tab_width ;
    return ( tab->calculated_width > max_width )?max_width:tab->calculated_width;
}

void
place_tabs_line( ASWinTab *tabs, int x, int y, int first, int last, int spare, int max_width, int tab_height )
{
    int i ;

    for( i = first ; i <= last ; ++i )
    {
		int delta = spare / (last+1-i) ;
        int width  = get_restricted_width (tabs+i, max_width);

LOCAL_DEBUG_OUT ("i = %d, name = \"%s\", x = %d, width = %d", i, tabs[i].name, x, width );
		if (!tabs[i].group_owner || delta < 0)
		{
        	width += delta ;
	        spare -= delta ;
		}

LOCAL_DEBUG_OUT ("i = %d, name = \"%s\", x = %d, width = %d", i, tabs[i].name, x, width );
        set_astbar_size( tabs[i].bar, width, tab_height );
        move_astbar( tabs[i].bar, WinTabsState.tabs_canvas, x, y );
        x += width ;
    }


}

void
moveresize_client( ASWinTab *aswt, int x, int y, int width, int height )
{
	int frame_width = width, frame_height = height ;
	if( get_flags( aswt->hints.flags, AS_MaxSize ) )
	{
		if( aswt->hints.max_width < width )
			width = aswt->hints.max_width ;
		if( aswt->hints.max_height < height )
			height = aswt->hints.max_height ;
	}

	if( get_flags( aswt->hints.flags, AS_SizeInc ) )
	{
		int min_w = 0, min_h = 0 ;
		if( aswt->hints.width_inc == 0 )
			aswt->hints.width_inc = 1;
		if( aswt->hints.height_inc == 0 )
			aswt->hints.height_inc = 1;
		if( get_flags( aswt->hints.flags, AS_MinSize ) )
		{
			min_w = aswt->hints.min_width ;
			min_h = aswt->hints.min_height ;
		}
		if( width > min_w && aswt->hints.width_inc < width  )
		{
			width = min_w + ((width - min_w)/aswt->hints.width_inc)*aswt->hints.width_inc ;
		}

		if( height > min_h && aswt->hints.height_inc < height  )
			height = min_h + ((height - min_h)/aswt->hints.height_inc)*aswt->hints.height_inc ;
	}

	moveresize_canvas( aswt->frame_canvas, 0, y, frame_width, frame_height );
    moveresize_canvas( aswt->client_canvas, (frame_width - width)/2, (frame_height - height)/2, width, height );
}

void
rearrange_tabs( Bool dont_resize_window )
{
	int tab_height = 0 ;
	ASCanvas *mc = WinTabsState.main_canvas ;
	int i ;
	ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
    int tabs_num = PVECTOR_USED(WinTabsState.tabs) ;
    int x = 0, y = 0 ;
    int max_x = mc->width ;
    int max_y = mc->height ;
    int max_width = Config->max_tab_width ;
    int max_group_owner_width = 0;
    int start = 0, start_x ;

	if( tabs_num == 0 )
	{
		if(	WinTabsState.pattern_wrexp != NULL || !get_flags(Config->flags, ASWT_HideWhenEmpty ) )
		{                      /* displaying banner with pattern or something else */
		}else if( get_flags( WinTabsState.flags, ASWT_StateMapped ) )  /* hiding ourselves: */
		{
			XEvent xev ;

			unmap_canvas_window( WinTabsState.main_canvas );
			/* we must follow with syntetic UnmapNotify per ICCCM :*/
			xev.xunmap.type = UnmapNotify ;
			xev.xunmap.event = Scr.Root ;
			xev.xunmap.window = WinTabsState.main_window;
			xev.xunmap.from_configure = False;
			XSendEvent( dpy, Scr.Root, False,
			            SubstructureRedirectMask|SubstructureNotifyMask,
						&xev );

			clear_flags( WinTabsState.flags, ASWT_StateMapped );
			return ;
		}
	}

	tab_height = calculate_astbar_height( WinTabsState.banner.bar );
	x = calculate_astbar_width( WinTabsState.banner.bar );

	if( tabs_num == 0 )
	{
		max_y = tab_height ;
		if( !dont_resize_window )
		{
			LOCAL_DEBUG_OUT( "resizing main canvas to %dx%d", x, max_y ) ;
			if( resize_canvas( WinTabsState.main_canvas, x, max_y ) != 0 )
				return;
		}
		max_x = x = mc->width ;
		tab_height = mc->height ;
	}else
	{
		if( !dont_resize_window )
		{
			LOCAL_DEBUG_OUT( "resizing main canvas to %dx%d", WinTabsState.win_width, WinTabsState.win_height ) ;
			if( resize_canvas( WinTabsState.main_canvas, WinTabsState.win_width, WinTabsState.win_height ) != 0 )
				return;
		}
		max_x = WinTabsState.win_width ;
		max_y = WinTabsState.win_height ;

    	i = tabs_num ;
		while( --i >= 0 )
		{
        	int height = calculate_astbar_height( tabs[i].bar );
        	if( height > tab_height )
            	tab_height = height ;
    	}
	}

    if( tab_height == 0 || max_x <= 0 || max_y <= 0 )
        return ;

	if( max_width <= 0 || max_width > max_x )
        max_width = max_x ;

    LOCAL_DEBUG_OUT( "max_x = %d, max_y = %d, max_width = %d", max_x, max_y, max_width );

    set_astbar_size( WinTabsState.banner.bar, x, tab_height );
    move_astbar( WinTabsState.banner.bar, WinTabsState.tabs_canvas, 0, 0 );

    for( i = 0 ; i < tabs_num ; ++i )
	{
		int width = calculate_astbar_width( tabs[i].bar );

		if (tabs[i].calculated_width < width)
			tabs[i].calculated_width = width;

		if (max_group_owner_width < tabs[i].calculated_width && tabs[i].group_owner)
			max_group_owner_width = tabs[i].calculated_width;
	}
#if 0
	/* having all the group tabs to be the same size turns out not to be such a great idea */
	if (max_group_owner_width > 0)
	    for (i = 0 ; i < tabs_num ; ++i)
		{
			if (tabs[i].group_owner)
				tabs[i].calculated_width = max_group_owner_width;
		}
#endif

	start = 0 ;
	start_x = x ;
    for( i = 0 ; i < tabs_num ; ++i )
    {
        int width  = get_restricted_width (tabs+i, max_width);

        if (x + width > max_x || (i > start+1 && tabs[i].group != tabs[i-1].group))
        {
			if( i  > 0 )
            	place_tabs_line( tabs, start_x, y, start, i - 1, max_x - x, max_width, tab_height );

            if( y + tab_height > max_y )
                break;

            y += tab_height ;
            x = 0 ;
			start = i ;
			start_x = 0 ;
        }

		if( i == tabs_num - 1 )
        {
            place_tabs_line( tabs, start_x, y, start, i, max_x - (x+width), max_width, tab_height );
            x = 0 ;
			start = i+1 ;
			start_x = 0 ;
        }

		x += width ;
    }
    if( i >= tabs_num )
        y += tab_height ;

    moveresize_canvas( WinTabsState.tabs_canvas, 0, 0, max_x, y );
	render_tabs(True);

    max_y -= y ;
	if( max_y <= 0 )
		max_y = 1 ;
    i = tabs_num ;
    LOCAL_DEBUG_OUT( "moveresaizing %d client canvases to %dx%d%+d%+d", i, max_x, max_y, 0, y );
    while( --i >= 0 )
    {
		moveresize_client( &(tabs[i]), 0, y, max_x, max_y );
    }

	if( !get_flags( WinTabsState.flags, ASWT_StateMapped ) )
	{
		  map_canvas_window( WinTabsState.main_canvas, True );
		  set_flags( WinTabsState.flags, ASWT_StateMapped );
	}
}

void
render_tabs( Bool canvas_resized )
{
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
    int i = PVECTOR_USED(WinTabsState.tabs) ;

    while( --i >= 0  )
    {
        register ASTBarData   *tbar = tabs[i].bar ;
        if( tbar != NULL )
            if( DoesBarNeedsRendering(tbar) || canvas_resized )
                render_astbar( tbar, WinTabsState.tabs_canvas );
    }

	if( DoesBarNeedsRendering(WinTabsState.banner.bar) || canvas_resized )
		render_astbar( WinTabsState.banner.bar, WinTabsState.tabs_canvas );

    if( is_canvas_dirty( WinTabsState.tabs_canvas ) )
	{
        update_canvas_display( WinTabsState.tabs_canvas );
	}
}

