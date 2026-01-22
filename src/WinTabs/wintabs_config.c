#include "wintabs_internal.h"

void
retrieve_wintabs_astbar_props()
{
	destroy_astbar_props( &(WinTabsState.tbar_props) );

	free_button_resources( &WinTabsState.close_button );
	free_button_resources( &WinTabsState.unswallow_button );
	free_button_resources( &WinTabsState.menu_button );
	memset(&WinTabsState.close_button, 0x00, sizeof(MyButton));
	memset(&WinTabsState.unswallow_button, 0x00, sizeof(MyButton));
	memset(&WinTabsState.menu_button, 0x00, sizeof(MyButton));

	WinTabsState.tbar_props = get_astbar_props(Scr.wmprops );
	button_from_astbar_props( WinTabsState.tbar_props, &WinTabsState.close_button, 		C_CloseButton, 		_AS_BUTTON_CLOSE, _AS_BUTTON_CLOSE_PRESSED );
	if (!button_from_astbar_props( WinTabsState.tbar_props, &WinTabsState.unswallow_button, 	C_UnswallowButton, 	_AS_BUTTON_MAXIMIZE, _AS_BUTTON_MAXIMIZE_PRESSED ))
		button_from_astbar_props( WinTabsState.tbar_props, &WinTabsState.unswallow_button, 	C_UnswallowButton, 	_AS_BUTTON_MINIMIZE, _AS_BUTTON_MINIMIZE_PRESSED );
	button_from_astbar_props( WinTabsState.tbar_props, &WinTabsState.menu_button, 		C_MenuButton, 		_AS_BUTTON_MENU, _AS_BUTTON_MENU_PRESSED );
}

void
CheckConfigSanity(const char *pattern_override, const char *exclude_pattern_override,
				  const char *title_override, const char *icon_title_override)
{
    if( Config == NULL )
        Config = CreateWinTabsConfig ();

	if( MyArgs.geometry.flags != 0 )
		Config->geometry = MyArgs.geometry ;
	if( pattern_override )
		set_string(&(Config->pattern), mystrdup(pattern_override));
	if( exclude_pattern_override )
		set_string(&(Config->exclude_pattern), mystrdup(exclude_pattern_override) );
   	if( title_override )
		set_string(&(Config->title), mystrdup(title_override) );
   	if( icon_title_override )
		set_string(&(Config->icon_title), mystrdup(icon_title_override) );

	if( Config->icon_title == NULL && Config->title != NULL )
	{
		Config->icon_title = safemalloc( strlen(Config->title)+32 );
		sprintf( Config->icon_title, "%s - iconic", Config->title );
	}

	if( Config->pattern != NULL )
	{
		WinTabsState.pattern_wrexp = compile_wild_reg_exp( Config->pattern ) ;
		if( Config->exclude_pattern )
		{
LOCAL_DEBUG_OUT( "exclude_pattern = \"%s\"", Config->exclude_pattern );
			WinTabsState.exclude_pattern_wrexp = compile_wild_reg_exp( Config->exclude_pattern ) ;
		}
	}else
	{
		show_warning( "Empty Pattern requested for windows to be captured and tabbed - will wait for swallow command");
	}

	if( get_flags(Config->set_flags, WINTABS_AllDesks ) )
	{
		if( get_flags(Config->flags, WINTABS_AllDesks ) )
			set_flags( WinTabsState.flags, ASWT_AllDesks );
	}
	if( get_flags(Config->set_flags, WINTABS_SkipTransients ) )
	{
		if( get_flags(Config->flags, WINTABS_SkipTransients ) )
			set_flags( WinTabsState.flags, ASWT_SkipTransients );
	}


    if( !get_flags(Config->geometry.flags, WidthValue) )
		Config->geometry.width = 640 ;
    if( !get_flags(Config->geometry.flags, HeightValue) )
		Config->geometry.height = 480 ;

	WinTabsState.win_width = Config->geometry.width ;
	WinTabsState.win_height = Config->geometry.height ;


    Config->gravity = NorthWestGravity ;
    if( get_flags(Config->geometry.flags, XNegative) )
        Config->gravity = get_flags(Config->geometry.flags, YNegative)? SouthEastGravity:NorthEastGravity;
    else if( get_flags(Config->geometry.flags, YNegative) )
        Config->gravity = SouthWestGravity;

    Config->anchor_x = get_flags( Config->geometry.flags, XValue )?Config->geometry.x:0;
    if( get_flags(Config->geometry.flags, XNegative) )
        Config->anchor_x += Scr.MyDisplayWidth ;

    Config->anchor_y = get_flags( Config->geometry.flags, YValue )?Config->geometry.y:0;
    if( get_flags(Config->geometry.flags, YNegative) )
        Config->anchor_y += Scr.MyDisplayHeight ;
}

void
SetWinTabsLook()
{
	int i ;
    char *default_style = safemalloc( 1+strlen(MyName)+1);
	ARGB32 border_color = 0;

	default_style[0] = '*' ;
	strcpy( &(default_style[1]), MyName );

	if( WinTabsState.tbar_props == NULL )
	    retrieve_wintabs_astbar_props();
	mystyle_get_property (Scr.wmprops);

    Scr.Look.MSWindow[BACK_UNFOCUSED] = mystyle_find( Config->unfocused_style );
    Scr.Look.MSWindow[BACK_FOCUSED] = mystyle_find( Config->focused_style );
    Scr.Look.MSWindow[BACK_STICKY] = mystyle_find( Config->sticky_style );

    for( i = 0 ; i < BACK_STYLES ; ++i )
    {
        static char *default_window_style_name[BACK_STYLES] ={"focused_window_style","unfocused_window_style","sticky_window_style", NULL, NULL};
        if( Scr.Look.MSWindow[i] == NULL )
            Scr.Look.MSWindow[i] = mystyle_find( default_window_style_name[i] );
        if( Scr.Look.MSWindow[i] == NULL && i != BACK_URGENT )
            Scr.Look.MSWindow[i] = mystyle_find_or_default( default_style );
		if( Scr.Look.MSWindow[i] )
		{
			if( Scr.Look.MSWindow[i]->texture_type == TEXTURE_SHAPED_PIXMAP )
				Scr.Look.MSWindow[i]->texture_type = TEXTURE_PIXMAP ;
			else if( Scr.Look.MSWindow[i]->texture_type == TEXTURE_SHAPED_SCALED_PIXMAP )
				Scr.Look.MSWindow[i]->texture_type = TEXTURE_SCALED_PIXMAP ;

		}
    }
    free( default_style );
	if( get_flags( WinTabsState.flags, ASWT_WantTransparent ) )
		set_flags( WinTabsState.flags, ASWT_Transparent );
	else
	{
		clear_flags( WinTabsState.flags, ASWT_Transparent );
		border_color = Scr.Look.MSWindow[BACK_FOCUSED]->colors.back;
		if( border_color_override != NULL )
			parse_argb_color( border_color_override, &border_color );
		else if( TransparentMS(Scr.Look.MSWindow[BACK_FOCUSED]) )
			set_flags( WinTabsState.flags, ASWT_Transparent );
		ARGB2PIXEL(Scr.asv,border_color,&WinTabsState.border_color);
		XSetForeground(dpy, Scr.DrawGC, WinTabsState.border_color);
	}


	balloon_config2look( &(Scr.Look), NULL, Config->balloon_conf, "*WinTabsBalloon" );
    set_balloon_look( Scr.Look.balloon_look );
}

void
GetBaseOptions (const char *filename)
{
	ReloadASEnvironment( NULL, NULL, NULL, False, True );
}

void
GetOptions (const char *filename)
{
    START_TIME(option_time);
    WinTabsConfig *config = ParseWinTabsOptions( filename, MyName );

#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
    PrintWinTabsConfig (config);
#endif
    /* Need to merge new config with what we have already :*/
    /* now lets check the config sanity : */
    /* mixing set and default flags : */
    Config->flags = (config->flags&config->set_flags)|(Config->flags & (~config->set_flags));
    Config->set_flags |= config->set_flags;

    Config->gravity = NorthWestGravity ;
    if( get_flags(config->set_flags, WINTABS_Geometry) )
        merge_geometry(&(config->geometry), &(Config->geometry) );

    if( config->pattern )
    {
        set_string( &(Config->pattern), mystrdup(config->pattern) );
        Config->pattern_type = config->pattern_type ;
    }
    if( get_flags(config->set_flags, WINTABS_MaxRows) )
        Config->max_rows = config->max_rows;
    if( get_flags(config->set_flags, WINTABS_MaxColumns) )
        Config->max_columns = config->max_columns;
    if( get_flags(config->set_flags, WINTABS_MinTabWidth) )
        Config->min_tab_width = config->min_tab_width;
    if( get_flags(config->set_flags, WINTABS_MaxTabWidth) )
        Config->max_tab_width = config->max_tab_width;

    if( config->unfocused_style )
        set_string( &(Config->unfocused_style), mystrdup(config->unfocused_style) );
    if( config->focused_style )
        set_string( &(Config->focused_style), mystrdup(config->focused_style) );
    if( config->sticky_style )
        set_string( &(Config->sticky_style), mystrdup(config->sticky_style) );

    if( get_flags(config->set_flags, WINTABS_Align) )
        Config->name_aligment = config->name_aligment;
    if( get_flags(config->set_flags, WINTABS_FBevel) )
        Config->fbevel = config->fbevel;
    if( get_flags(config->set_flags, WINTABS_UBevel) )
        Config->ubevel = config->ubevel;
    if( get_flags(config->set_flags, WINTABS_SBevel) )
        Config->sbevel = config->sbevel;

    if( get_flags(config->set_flags, WINTABS_FCM) )
        Config->fcm = config->fcm;
    if( get_flags(config->set_flags, WINTABS_UCM) )
        Config->ucm = config->ucm;
    if( get_flags(config->set_flags, WINTABS_SCM) )
        Config->scm = config->scm;

    if( get_flags(config->set_flags, WINTABS_H_SPACING) )
        Config->h_spacing = config->h_spacing;
    if( get_flags(config->set_flags, WINTABS_V_SPACING) )
        Config->v_spacing = config->v_spacing;

    if( config->GroupNameSeparator )
        set_string( &(Config->GroupNameSeparator), mystrdup(config->GroupNameSeparator) );

    if( Config->balloon_conf )
        Destroy_balloonConfig( Config->balloon_conf );
    Config->balloon_conf = config->balloon_conf ;
    config->balloon_conf = NULL ;

    if (config->style_defs)
        ProcessMyStyleDefinitions (&(config->style_defs));
    SHOW_TIME("Config parsing",option_time);
}
