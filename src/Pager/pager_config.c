#include "pager_internal.h"


void retrieve_pager_astbar_props ()
{
	destroy_astbar_props (&(PagerState.tbar_props));

	PagerState.tbar_props = get_astbar_props (Scr.wmprops);

	free_button_resources (&PagerState.shade_button);
	PagerState.shade_button.context = C_NO_CONTEXT;
	if (get_flags (Config->set_flags, PAGER_SET_SHADE_BUTTON)) {
		if (Config->shade_button[0])
			if (load_button
					(&PagerState.shade_button, Config->shade_button,
					 Scr.image_manager))
				PagerState.shade_button.context = C_ShadeButton;
	} else
		button_from_astbar_props (PagerState.tbar_props,
															&PagerState.shade_button, C_ShadeButton,
															_AS_BUTTON_SHADE, _AS_BUTTON_SHADE_PRESSED);
}


/*****************************************************************************
 * This routine is responsible for reading and parsing the config file
 ****************************************************************************/

void CheckConfigSanity ()
{
	int i;
	char buf[256];

	if (Config == NULL)
		Config = CreatePagerConfig (PagerState.desks_num);

	if (Rows_override > 0)
		Config->rows = Rows_override;
	if (Columns_override > 0)
		Config->columns = Columns_override;
	LOCAL_DEBUG_OUT
			("columns = %d, rows = %d, desks = %ld, start_desk = %ld",
			 Config->columns, Config->rows, PagerState.desks_num,
			 PagerState.start_desk);
	if (Config->rows > PagerState.desks_num)
		Config->rows = PagerState.desks_num;
	if (Config->rows == 0)
		Config->rows = 1;

	if (Config->columns == 0 ||
			Config->rows * Config->columns != PagerState.desks_num)
		Config->columns = PagerState.desks_num / Config->rows;

	if (Config->rows * Config->columns < PagerState.desks_num)
		++(Config->columns);

	LOCAL_DEBUG_OUT
			("columns = %d, rows = %d, desks = %ld, start_desk = %ld",
			 Config->columns, Config->rows, PagerState.desks_num,
			 PagerState.start_desk);

	if (MyArgs.geometry.flags != 0)
		Config->geometry = MyArgs.geometry;

	LOCAL_DEBUG_OUT ("geometry = %dx%d%+d%+d", Config->geometry.width,
									 Config->geometry.height, Config->geometry.x,
									 Config->geometry.y);

	if (get_flags (Config->geometry.flags, XNegative))
		Config->gravity =
				get_flags (Config->geometry.flags,
									 YNegative) ? SouthEastGravity : NorthEastGravity;
	else if (get_flags (Config->geometry.flags, YNegative))
		Config->gravity = SouthWestGravity;

	if (MyArgs.gravity != ForgetGravity)
		Config->gravity = MyArgs.gravity;

	if (Config->geometry.width <= Config->columns)
		clear_flags (Config->geometry.flags, WidthValue);
	if (!get_flags (Config->geometry.flags, WidthValue))
		Config->geometry.width =
				(PagerState.vscreen_width * Config->columns) / Scr.VScale;

	PagerState.desk_width = Config->geometry.width / Config->columns;
	Config->geometry.width = PagerState.desk_width * Config->columns;

	if (Config->geometry.height <= Config->rows)
		clear_flags (Config->geometry.flags, HeightValue);
	if (!get_flags (Config->geometry.flags, HeightValue)
			|| Config->geometry.height <= Config->rows)
		Config->geometry.height =
				(PagerState.vscreen_height * Config->rows) / Scr.VScale;

	PagerState.desk_height = Config->geometry.height / Config->rows;
	Config->geometry.height = PagerState.desk_height * Config->rows;

	PagerState.wasted_width = PagerState.wasted_height = 0;

	if (!get_flags (Config->geometry.flags, XValue)) {
		Config->geometry.x = 0;
	} else {
		int real_x = Config->geometry.x;
		if (get_flags (Config->geometry.flags, XNegative)) {
			Config->gravity = NorthEastGravity;
			real_x += (int)Scr.MyDisplayWidth;
		}
		if (real_x + (int)Config->geometry.width < 0)
			Config->geometry.x = get_flags (Config->geometry.flags, XNegative) ?
					(int)Config->geometry.width - (int)Scr.MyDisplayWidth : 0;
		else if (real_x > (int)Scr.MyDisplayWidth)
			Config->geometry.x = get_flags (Config->geometry.flags, XNegative) ?
					0 : (int)Scr.MyDisplayWidth - (int)Config->geometry.width;
	}
	if (!get_flags (Config->geometry.flags, YValue)) {
		Config->geometry.y = 0;
	} else {
		int real_y = Config->geometry.y;
		if (get_flags (Config->geometry.flags, YNegative)) {
			Config->gravity =
					(Config->gravity ==
					 NorthEastGravity) ? SouthEastGravity : SouthWestGravity;
			real_y += (int)Scr.MyDisplayHeight;
		}
		if (real_y + (int)Config->geometry.height < 0)
			Config->geometry.y = get_flags (Config->geometry.flags, YNegative) ?
					(int)Config->geometry.height - (int)Scr.MyDisplayHeight : 0;
		else if (real_y > (int)Scr.MyDisplayHeight)
			Config->geometry.y = get_flags (Config->geometry.flags, YNegative) ?
					0 : (int)Scr.MyDisplayHeight - (int)Config->geometry.height;
	}


	if (get_flags (Config->set_flags, PAGER_SET_ICON_GEOMETRY)) {
		if (!get_flags (Config->icon_geometry.flags, WidthValue)
				|| Config->icon_geometry.width <= 0)
			Config->icon_geometry.width = 54;
		if (!get_flags (Config->icon_geometry.flags, HeightValue)
				|| Config->icon_geometry.height <= 0)
			Config->icon_geometry.height = 54;
	} else {
		Config->icon_geometry.width = 54;
		Config->icon_geometry.height = 54;
	}

	parse_argb_color (Config->border_color, &(Config->border_color_argb));
	parse_argb_color (Config->selection_color,
										&(Config->selection_color_argb));
	parse_argb_color (Config->grid_color, &(Config->grid_color_argb));


	if (!get_flags (Config->set_flags, PAGER_SET_ACTIVE_BEVEL))
		Config->active_desk_bevel = DEFAULT_TBAR_HILITE;
	if (!get_flags (Config->set_flags, PAGER_SET_INACTIVE_BEVEL))
		Config->inactive_desk_bevel = DEFAULT_TBAR_HILITE;

	if (PagerState.tbar_props == NULL)
		retrieve_pager_astbar_props ();

	LOCAL_DEBUG_OUT ("active_bevel = %lX, inactive_bevel = %lX",
									 Config->active_desk_bevel, Config->inactive_desk_bevel);
	mystyle_get_property (Scr.wmprops);

	for (i = 0; i < BACK_STYLES; ++i) {
		static char *window_style_names[BACK_STYLES] = { "*%sFWindowStyle",
			"*%sUWindowStyle",
			"*%sSWindowStyle", NULL
		};
		static char *default_window_style_name[BACK_STYLES] =
				{ "focused_window_style",
			"unfocused_window_style",
			"sticky_window_style", NULL
		};
		if (window_style_names[i]) {
			sprintf (&(buf[0]), window_style_names[i], MyName);
			if ((Scr.Look.MSWindow[i] = mystyle_find (buf)) == NULL
					&& i != BACK_URGENT)
				Scr.Look.MSWindow[i] =
						mystyle_find_or_default (default_window_style_name[i]);
		}
	}

	if (Config->small_font_name) {
		MyFont small_font = { NULL, NULL };
		if (load_font (Config->small_font_name, &small_font)) {
			mystyle_merge_font (Scr.Look.MSWindow[0], &small_font, True);
			for (i = 1; i < BACK_STYLES; ++i)
				if (Scr.Look.MSWindow[i])
					mystyle_merge_font (Scr.Look.MSWindow[i], &small_font, True);
		}
		unload_font (&small_font);
	}

	for (i = 0; i < DESK_STYLES; ++i) {
		static char *desk_style_names[DESK_STYLES] =
				{ "*%sActiveDesk", "*%sInActiveDesk" };

		sprintf (buf, desk_style_names[i], MyName);
		Config->MSDeskTitle[i] = mystyle_find_or_default (buf);
		LOCAL_DEBUG_OUT ("desk_style %d: \"%s\" ->%p(\"%s\")->colors(%lX,%lX)",
										 i, buf, Config->MSDeskTitle[i],
										 Config->MSDeskTitle[i]->name,
										 Config->MSDeskTitle[i]->colors.fore,
										 Config->MSDeskTitle[i]->colors.back);
	}

	if (Config->MSDeskBack == NULL)
		Config->MSDeskBack =
				safecalloc (PagerState.desks_num, sizeof (MyStyle *));
	for (i = 0; i < PagerState.desks_num; ++i) {
		Config->MSDeskBack[i] = NULL;
		if (Config->styles && Config->styles[i] != NULL)
			Config->MSDeskBack[i] = mystyle_find (Config->styles[i]);

		if (Config->MSDeskBack[i] == NULL) {
			sprintf (buf, "*%sDesk%d", MyName, i + (int)PagerState.start_desk);
			Config->MSDeskBack[i] = mystyle_find_or_default (buf);
		}
	}
	/* shade button : */
	Scr.Feel.EdgeResistanceMove = 5;
	Scr.Feel.EdgeAttractionScreen = 5;
	Scr.Feel.EdgeAttractionWindow = 10;
	Scr.Feel.OpaqueMove = 100;
	Scr.Feel.OpaqueResize = 100;
	Scr.Feel.no_snaping_mod = ShiftMask;

#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	Print_balloonConfig (Config->balloon_conf);
#endif
	balloon_config2look (&(Scr.Look), NULL, Config->balloon_conf,
											 "*PagerBalloon");
	set_balloon_look (Scr.Look.balloon_look);

	LOCAL_DEBUG_OUT ("geometry = %dx%d%+d%+d", Config->geometry.width,
									 Config->geometry.height, Config->geometry.x,
									 Config->geometry.y);

}

void GetOptions (const char *filename)
{
	PagerConfig *config =
			ParsePagerOptions (filename, MyName, PagerState.start_desk,
												 PagerState.start_desk + PagerState.desks_num);
	int i;
	START_TIME (option_time);

/*   WritePagerOptions( filename, MyName, Pager.desk1, Pager.desk2, config, WF_DISCARD_UNKNOWN|WF_DISCARD_COMMENTS );
 */

	/* Need to merge new config with what we have already : */
	/* now lets check the config sanity : */
	/* mixing set and default flags : */
	Config->flags =
			(config->flags & config->set_flags) | (Config->
																						 flags & (~config->set_flags));
	Config->set_flags |= config->set_flags;

	if (get_flags (config->set_flags, PAGER_SET_ROWS))
		Config->rows = config->rows;

	if (get_flags (config->set_flags, PAGER_SET_COLUMNS))
		Config->columns = config->columns;

	Config->gravity = NorthWestGravity;
	if (get_flags (config->set_flags, PAGER_SET_GEOMETRY))
		merge_geometry (&(config->geometry), &(Config->geometry));

	if (get_flags (config->set_flags, PAGER_SET_ICON_GEOMETRY))
		merge_geometry (&(config->icon_geometry), &(Config->icon_geometry));

	if (config->labels) {
		if (Config->labels == NULL)
			Config->labels = safecalloc (PagerState.desks_num, sizeof (char *));
		for (i = 0; i < PagerState.desks_num; ++i)
			if (config->labels[i])
				set_string (&(Config->labels[i]), mystrdup (config->labels[i]));
	}
	if (config->styles) {
		if (Config->styles == NULL)
			Config->styles = safecalloc (PagerState.desks_num, sizeof (char *));
		for (i = 0; i < PagerState.desks_num; ++i)
			if (config->styles[i])
				set_string (&(Config->styles[i]), mystrdup (config->styles[i]));
	}
	if (get_flags (config->set_flags, PAGER_SET_ALIGN))
		Config->align = config->align;

	if (get_flags (config->set_flags, PAGER_SET_SMALL_FONT))
		set_string (&(Config->small_font_name),
								mystrdup (config->small_font_name));

	if (get_flags (config->set_flags, PAGER_SET_BORDER_WIDTH))
		Config->border_width = config->border_width;

	if (get_flags (config->set_flags, PAGER_SET_SELECTION_COLOR))
		set_string (&(Config->selection_color),
								mystrdup (config->selection_color));

	if (get_flags (config->set_flags, PAGER_SET_GRID_COLOR))
		set_string (&(Config->grid_color), mystrdup (config->grid_color));


	if (get_flags (config->set_flags, PAGER_SET_BORDER_COLOR))
		set_string (&(Config->border_color), mystrdup (config->border_color));

	if (config->shade_button[0])
		set_string (&(Config->shade_button[0]),
								mystrdup (config->shade_button[0]));

	if (config->shade_button[1])
		set_string (&(Config->shade_button[1]),
								mystrdup (config->shade_button[1]));

	if (get_flags (config->set_flags, PAGER_SET_ACTIVE_BEVEL)) {
		Config->active_desk_bevel = config->active_desk_bevel;
		set_flags (Config->set_flags, PAGER_SET_ACTIVE_BEVEL);
	}
	if (get_flags (config->set_flags, PAGER_SET_INACTIVE_BEVEL)) {
		Config->inactive_desk_bevel = config->inactive_desk_bevel;
		set_flags (Config->set_flags, PAGER_SET_INACTIVE_BEVEL);
	}

	if (Config->balloon_conf)
		Destroy_balloonConfig (Config->balloon_conf);
	Config->balloon_conf = config->balloon_conf;
	config->balloon_conf = NULL;

	if (config->style_defs)
		ProcessMyStyleDefinitions (&(config->style_defs));

	DestroyPagerConfig (config);
	SHOW_TIME ("Config parsing", option_time);
}

/*****************************************************************************
 *
 * This routine is responsible for reading and parsing the base file
 *
 ****************************************************************************/
void GetBaseOptions (const char *filename)
{

	START_TIME (started);

	ReloadASEnvironment (NULL, NULL, NULL, False, True);

	if (Environment->desk_pages_h > 0)
		PagerState.page_columns = Environment->desk_pages_h;
	if (Environment->desk_pages_v > 0)
		PagerState.page_rows = Environment->desk_pages_v;

	Scr.Vx = 0;
	Scr.Vy = 0;
	PagerState.vscreen_width = Scr.VxMax + Scr.MyDisplayWidth;
	PagerState.vscreen_height = Scr.VyMax + Scr.MyDisplayHeight;

	SHOW_TIME ("BaseConfigParsingTime", started);
	LOCAL_DEBUG_OUT ("desk_size(%dx%d),vscreen_size(%dx%d),vscale(%d)",
									 PagerState.desk_width, PagerState.desk_height,
									 PagerState.vscreen_width, PagerState.vscreen_height,
									 Scr.VScale);
}
