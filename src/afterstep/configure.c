/*
 * Copyright (c) 2002,2003 Sasha Vasko <sasha at aftercode.net>
 * Copyright (c) 1998 Rafal Wierzbicki <rafal@mcss.mcmaster.ca>
 * Copyright (c) 1998 Sasha Vasko <sasha at aftercode.net>
 * Copyright (c) 1998 Michal Vitecek <fuf@fuf.sh.cvut.cz>
 * Copyright (c) 1998 Nat Makarevitch <nat@linux-france.com>
 * Copyright (c) 1998 Mike Venaccio <venaccio@aero.und.edu>
 * Copyright (c) 1998 Ethan Fischer <allanon@crystaltokyo.com>
 * Copyright (c) 1998 Mike Venaccio <venaccio@aero.und.edu>
 * Copyright (c) 1998 Chris Ridd <c.ridd@isode.com>
 * Copyright (c) 1997 Raphael Goulais <velephys@hol.fr>
 * Copyright (c) 1997 Guylhem Aznar <guylhem@oeil.qc.ca>
 * Copyright (C) 1996 Frank Fejes
 * Copyright (C) 1995 Bo Yang
 * Copyright (C) 1993 Robert Nation
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
 *
 */

/****************************************************************************
 *
 * Configure.c: reads the configuration files, interprets them,
 * and sets up menus, bindings, colors, and fonts as specified
 *
 ***************************************************************************/

#define LOCAL_DEBUG
#include "../../configure.h"

#include "asinternals.h"

#include <unistd.h>

#include "dirtree.h"

#include "../../libAfterStep/session.h"
#include "../../libAfterStep/mystyle_property.h"
#include "../../libAfterStep/wmprops.h"
#include "../../libAfterStep/desktop_category.h"
#include "../../libAfterStep/kde.h"

#include "../../libAfterConf/afterconf.h"


typedef struct AfterStepConfig {
	ASModuleConfig asmodule_config;
	ASFlagType flags;
	ASFlagType set_flags;

} AfterStepConfig;

#define AS_AFTERSTEP_CONFIG(p) AS_MODULE_CONFIG_TYPED(p,CONFIG_AfterStep_ID,AfterStepConfig)


static void
InitAfterStepConfig (ASModuleConfig * asm_config, Bool free_resources)
{
	AfterStepConfig *config = AS_AFTERSTEP_CONFIG (asm_config);
	if (config) {
		/* TODO */
		if (free_resources) {
		}
	}
}

void
AfterStep_fs2config (ASModuleConfig * asmodule_config,
										 FreeStorageElem * Storage)
{
	FreeStorageElem *pCurr;
	ConfigItem item;
	AfterStepConfig *config = AS_AFTERSTEP_CONFIG (asmodule_config);

	if (config == NULL)
		return;

	item.memory = NULL;
	for (pCurr = Storage; pCurr; pCurr = pCurr->next) {
		if (pCurr->term == NULL)
			continue;

		if (pCurr->term->type == TT_FLAG) {
		} else {
			if (!ReadConfigItem (&item, pCurr))
				continue;

			switch (pCurr->term->id) {
			default:
				item.ok_to_free = 1;
			}
		}
	}

	ReadConfigItem (&item, NULL);
}

void
MergeAfterStepOptions (ASModuleConfig * asm_to, ASModuleConfig * asm_from)
{
	/* int i ; */
	START_TIME (option_time);

	AfterStepConfig *to = AS_AFTERSTEP_CONFIG (asm_to);
	AfterStepConfig *from = AS_AFTERSTEP_CONFIG (asm_from);
	if (to && from) {

		/* Need to merge new config with what we have already : */
		/* now lets check the config sanity : */
		/* mixing set and default flags : */
		ASCF_MERGE_FLAGS (to, from);

	}
	SHOW_TIME ("to parsing", option_time);
}


flag_options_xref AfterStepConfigFlags[] = {
/*	ASCF_DEFINE_MODULE_FLAG_XREF(WINLIST,FillRowsFirst,WinListConfig), */
	{0, 0, 0}
};


int AfterStepBalloons[] =
		{ TITLE_BALLOON_ID_START, MENU_BALLOON_ID_START, 0 };

static ASModuleConfigClass _afterstep_config_class = { CONFIG_AfterStep_ID,
	ASMC_HandlePublicLookOptions | ASMC_HandlePublicFeelOptions
			/* |ASMC_HandleLookMyStyles */ ,
	sizeof (AfterStepConfig),
	"afterstep",
	InitAfterStepConfig,
	AfterStep_fs2config,
	MergeAfterStepOptions,
	&AfterStepSyntax,
	&LookSyntax,
	&FeelSyntax,
	AfterStepConfigFlags,
	offsetof (AfterStepConfig, set_flags),

	AfterStepBalloons
};

ASModuleConfigClass *AfterStepConfigClass = &_afterstep_config_class;


void ReloadConfig (ASFlagType what)
{
	ASBalloonLook *balloon_look;
	ASModuleConfig *config =
			parse_asmodule_config_all (AfterStepConfigClass);

	/* apply it  */
	Print_balloonConfig (config->balloon_configs[0]);
	balloon_look = create_balloon_look ();
	balloon_config2look (&Scr.Look, balloon_look, config->balloon_configs[0],
											 "TitleButtonBalloon");
	set_balloon_state_look (TitlebarBalloons, balloon_look);
	destroy_balloon_look (balloon_look);

	balloon_look = create_balloon_look ();
	Print_balloonConfig (config->balloon_configs[1]);
	balloon_config2look (&Scr.Look, balloon_look, config->balloon_configs[1],
											 "MenuBalloon");
	set_balloon_state_look (MenuBalloons, balloon_look);
	destroy_balloon_look (balloon_look);

	PrintMyStyleDefinitions (config->style_defs);

	destroy_ASModule_config (config);
}

#ifdef AFTERSTEP_CONFIG_TEST

ASBalloonState *MenuBalloons = NULL;
ASBalloonState *TitlebarBalloons = NULL;

int main (int argc, char **argv)
{

	ASImageManager *old_image_manager = NULL;
	ASFontManager *old_font_manager = NULL;
	InitMyApp (CLASS_AFTERSTEP, argc, argv, NULL, NULL, 0);

	AfterStepConfigClass->flags |= ASMC_HandleLookMyStyles;

	LinkAfterStepConfig ();
	if (ConnectX (ASDefaultScr, 0) < 0) {
		show_error ("Hostile X server encountered - unable to proceed :-(");
		return 1;										/* failed to accure window management selection - other wm is running */
	}
	InitSession ();
	MenuBalloons = create_balloon_state ();
	TitlebarBalloons = create_balloon_state ();
	ReloadASEnvironment (&old_image_manager, &old_font_manager, NULL, True,
											 True);
	LoadColorScheme ();
	ReloadConfig (0xFFFFFFFF);

}

#else

#include "configure_internal.h"

/*
 * Create/destroy window titlebar/buttons as necessary.
 */
Bool redecorate_aswindow_iter_func (void *data, void *aux_data)
{
	ASWindow *asw = (ASWindow *) data;
	if (asw) {
		/* need to invalidate all MyStyles at this point ??? */

		invalidate_window_mystyles (asw);

		redecorate_window (asw, False);
		if (asw->internal && asw->internal->on_look_feel_changed)
			asw->internal->on_look_feel_changed (asw->internal, &Scr.Feel,
																					 &Scr.Look, ASFLAGS_EVERYTHING);
		on_window_status_changed (asw, True);
		set_flags (asw->internal_flags, ASWF_PendingShapeRemoval);
	}
	return True;
}

void advertise_tbar_props ()
{
	ASTBarProps props;
	MyFrame *frame = myframe_find (NULL);
	MouseButton *btn;
	int i, k;

	MyButton *close_btn = NULL;
	MyButton *maximize_btn = NULL;
	MyButton *minimize_btn = NULL;
	MyButton *shade_btn = NULL;
	MyButton *menu_btn = NULL;
	struct {
		Atom *kind, *kind_down;
		int func;
		MyButton **pbtn;
	} buttons[] = { {
	&_AS_BUTTON_CLOSE, &_AS_BUTTON_CLOSE_PRESSED, F_CLOSE, &close_btn}, {
	&_AS_BUTTON_CLOSE, &_AS_BUTTON_CLOSE_PRESSED, F_DELETE, &close_btn}, {
	&_AS_BUTTON_CLOSE, &_AS_BUTTON_CLOSE_PRESSED, F_DESTROY, &close_btn}, {
	&_AS_BUTTON_MAXIMIZE, &_AS_BUTTON_MAXIMIZE_PRESSED, F_MAXIMIZE,
				&maximize_btn}, {
	&_AS_BUTTON_MINIMIZE, &_AS_BUTTON_MINIMIZE_PRESSED, F_ICONIFY,
				&minimize_btn}, {
	&_AS_BUTTON_SHADE, &_AS_BUTTON_SHADE_PRESSED, F_SHADE, &shade_btn}, {
	&_AS_BUTTON_MENU, &_AS_BUTTON_MENU_PRESSED, F_POPUP, &menu_btn}, {
	0, 0, 0, NULL}};


	memset (&props, 0x00, sizeof (props));
	if (get_flags (frame->set_title_attr, MYFRAME_TitleAlignSet)) {
		props.align = frame->title_align;
#if (NO_ALIGN==0)
		if (props.align == 0)
			props.align = ~ALIGN_MASK;
#endif
	}
	if (get_flags
			(frame->set_title_attr,
			 MYFRAME_TitleFBevelSet | MYFRAME_TitleUBevelSet)) {
		props.bevel = frame->title_fbevel | frame->title_ubevel;
#if (NO_HILITE==0)
		if (props.bevel == 0)
			props.bevel = ~HILITE_MASK;
#endif
	}
	props.title_h_spacing = frame->title_h_spacing;
	props.title_v_spacing = frame->title_v_spacing;
	props.buttons_h_border =
			max (Scr.Look.TitleButtonXOffset[0], Scr.Look.TitleButtonXOffset[1]);
	props.buttons_v_border =
			max (Scr.Look.TitleButtonYOffset[0], Scr.Look.TitleButtonYOffset[1]);
	props.buttons_spacing =
			max (Scr.Look.TitleButtonSpacing[0], Scr.Look.TitleButtonSpacing[1]);

	for (btn = Scr.Feel.MouseButtonRoot; btn != NULL; btn = btn->NextButton)
		if ((btn->Context & C_TButtonAll)) {
			for (i = 0; buttons[i].pbtn != NULL; ++i) {
				if (btn->fdata->func == buttons[i].func && *(buttons[i].pbtn) == NULL) {	/* now lets find that button in look : */
					for (k = 0; k < TITLE_BUTTONS; ++k)
						if (Scr.Look.buttons[k].unpressed.image != NULL &&
								(btn->Context & Scr.Look.buttons[k].context) != 0) {
							*(buttons[i].pbtn) = &(Scr.Look.buttons[k]);
							break;
						}
					break;
				}
			}
		}
	props.buttons_num = 0;
	for (i = 0; buttons[i].pbtn != NULL; ++i) {
		MyButton *b = *(buttons[i].pbtn);
		if (b) {
			++props.buttons_num;
			if (b->pressed.image != NULL
					&& b->pressed.image != b->unpressed.image)
				++props.buttons_num;
		}
	}
	props.buttons =
			safemalloc (props.buttons_num * sizeof (struct ASButtonPropElem));
	k = 0;
	for (i = 0; buttons[i].pbtn != NULL; ++i) {
		MyButton *b = *(buttons[i].pbtn);
		if (b) {
			MyIcon *icon = &(b->unpressed);
			if (icon->pix == None)
				make_icon_pixmaps (icon, False);

			props.buttons[k].kind = *(buttons[i].kind);
			props.buttons[k].pmap = icon->pix;
			props.buttons[k].mask = icon->mask;
			props.buttons[k].alpha = icon->alpha;
			++k;
			if (b->pressed.image && b->pressed.image != b->unpressed.image) {
				icon = &(b->pressed);
				if (icon->pix == None)
					make_icon_pixmaps (icon, False);
				props.buttons[k].kind = *(buttons[i].kind_down);
				props.buttons[k].pmap = icon->pix;
				props.buttons[k].mask = icon->mask;
				props.buttons[k].alpha = icon->alpha;
				++k;
			}
		}
	}

	set_astbar_props (Scr.wmprops, &props);
	free (props.buttons);
}

void LoadASConfig (int thisdesktop, ASFlagType what)
{
	char *tline = NULL;
	ASImageManager *old_image_manager = NULL;
	ASFontManager *old_font_manager = NULL;

	cover_desktop ();

#ifndef DIFFERENTLOOKNFEELFOREACHDESKTOP
	/* only one look & feel should be used */
	thisdesktop = 0;
#endif													/* !DIFFERENTLOOKNFEELFOREACHDESKTOP */

	show_progress ("Loading configuration files ...");
	display_progress (True, "Loading configuration files ...");
	if (Session->overriding_file == NULL) {
		char *configfile;
		const char *const_configfile;
		if (get_flags (what, PARSE_BASE_CONFIG)) {
			if (ReloadASEnvironment
					(&old_image_manager, &old_font_manager, NULL,
					 get_flags (what, PARSE_LOOK_CONFIG), True)) {
				if (!get_flags (what, PARSE_LOOK_CONFIG)) {
					if (old_image_manager != NULL || old_font_manager != NULL) {
						InitLook (&Scr.Look, True);
						set_flags (what, PARSE_LOOK_CONFIG);
					}
				} else
					clear_flags (what, PARSE_BASE_CONFIG);
			}
		} else if (get_flags (what, PARSE_LOOK_CONFIG)) {	/* must reload Image manager so that changed images would get updated */
			reload_screen_image_manager (ASDefaultScr, &old_image_manager);
		}

		if (get_flags (what, PARSE_LOOK_CONFIG)) {
			stop_all_background_xfer ();
			LoadColorScheme ();

			/* now we can proceed to loading them look and theme */
			if ((const_configfile =
					 get_session_file (Session, thisdesktop, F_CHANGE_LOOK,
														 False)) != NULL) {
				InitLook (&Scr.Look, True);

				memset (&TmpLook, 0x00, sizeof (TmpLook));
				TmpLook.magic = MAGIC_MYLOOK;
				InitLook (&TmpLook, False);

				LOCAL_DEBUG_OUT ("desk_anime_tint = %lX",
												 TmpLook.desktop_animation_tint);
				ParseConfigFile (const_configfile, &tline);


				LOCAL_DEBUG_OUT ("desk_anime_tint = %lX",
												 TmpLook.desktop_animation_tint);
				show_progress ("LOOK configuration loaded from \"%s\" ...",
											 const_configfile);
				display_progress (True, "LOOK configuration loaded from \"%s\".",
													const_configfile);
#ifdef ASETROOT_FILE
				if (Scr.Look.desk_configs == NULL) {	/* looks like there is no background information in the look file and we should be
																							   getting it from the asetroot file : */

					if ((configfile =
							 make_session_file (Session, ASETROOT_FILE,
																	False)) != NULL) {
						ParseConfigFile (configfile, &tline);
						/* Save base filename to pass to modules */
						show_progress
								("ROOT BACKGROUND configuration loaded from \"%s\" ...",
								 configfile);
						display_progress (True,
															"ROOT BACKGROUND configuration loaded from \"%s\" .",
															configfile);
						free (configfile);
					}
				}
#endif
				merge_look (&Scr.Look, &TmpLook);
				destroy_string (&(BalloonConfig.Style));
				destroy_string (&(MenuBalloonConfig.Style));
			} else {
				show_warning ("LOOK configuration file cannot be found!");
				display_progress (True,
													"LOOK configuration file cannot be found!");
				clear_flags (what, PARSE_LOOK_CONFIG);
			}
			if (UpdateGtkRC (Environment))
				signal_reload_gtkrc_file ();
			if (!get_flags (Environment->flags, ASE_NoKDEGlobalsTheming))
				if (UpdateKCSRC ())
					signal_kde_palette_changed ();
		}
		if (get_flags (what, PARSE_FEEL_CONFIG)) {
			if ((const_configfile =
					 get_session_file (Session, thisdesktop, F_CHANGE_FEEL,
														 False)) != NULL) {
				const char *ws_file = get_session_ws_file (Session, True);
				memset (&TmpFeel, 0x00, sizeof (TmpFeel));
				InitFeel (&TmpFeel, True);
				InitFeel (&Scr.Feel, True);
				if (tline == NULL)
					tline = safemalloc (MAXLINELENGTH + 1);

				display_progress (True,
													"Reloading and merging desktop categories ...");
				ReloadCategories (False);
				UpdateCategoriesCache ();

				display_progress (True,
													"Parsing menu entries and checking availability ...");
				MeltStartMenu (tline);
				display_progress (False, "Done..");
				ParseConfigFile (const_configfile, &tline);
				show_progress ("FEEL configuration loaded from \"%s\" ...",
											 const_configfile);
				display_progress (True, "FEEL configuration loaded from \"%s\" .",
													const_configfile);
				if ((configfile =
						 make_session_file (Session, AUTOEXEC_FILE, False)) != NULL) {
					ParseConfigFile (configfile, &tline);
					show_progress ("AUTOEXEC configuration loaded from \"%s\" ...",
												 configfile);
					display_progress (True,
														"AUTOEXEC configuration loaded from \"%s\" .",
														configfile);
					free (configfile);
				} else {
					show_warning ("AUTOEXEC configuration file cannot be found!");
					display_progress (True,
														"AUTOEXEC configuration file cannot be found!");
				}
				if (ws_file != NULL) {
					ParseConfigFile (ws_file, &tline);
					show_progress
							("WORKSPACE STATE configuration loaded from \"%s\" ...",
							 ws_file);
					display_progress (True,
														"WORKSPACE STATE configuration loaded from \"%s\".",
														ws_file);
				} else {
					show_progress ("WORKSPACE STATE file cannot be found!");
					display_progress (True, "WORKSPACE STATE file cannot be found!");
				}
				merge_feel (&Scr.Feel, &TmpFeel);
			} else {
				show_warning ("FEEL configuration file cannot be found!");
				display_progress (True,
													"FEEL configuration file cannot be found!");
				clear_flags (what, PARSE_FEEL_CONFIG);
			}
		}
		if (get_flags (what, PARSE_DATABASE_CONFIG)) {
			if (!ReloadASDatabase ()) {
				display_progress (True,
													"DATABASE configuration file cannot be found!");
				clear_flags (what, PARSE_DATABASE_CONFIG);
			} else {
				configfile = make_session_file (Session, DATABASE_FILE, False);
				display_progress (True,
													"DATABASE configuration loaded from \"%s\" .",
													configfile);
				free (configfile);
			}
		}
	} else {
		ReloadASEnvironment (&old_image_manager, &old_font_manager, NULL, True,
												 True);

		LoadColorScheme ();

		memset (&TmpLook, 0x00, sizeof (TmpLook));
		InitLook (&TmpLook, False);
		InitLook (&Scr.Look, True);
		memset (&TmpFeel, 0x00, sizeof (TmpFeel));
		InitFeel (&TmpFeel, False);
		InitFeel (&Scr.Feel, True);
		ParseConfigFile (Session->overriding_file, &tline);
		merge_look (&Scr.Look, &TmpLook);
		merge_feel (&Scr.Feel, &TmpFeel);

		ReloadASDatabase ();
		show_progress ("AfterStep configuration loaded from \"%s\" ...",
									 Session->overriding_file);
		display_progress (True, "AfterStep configuration loaded from \"%s\".",
											Session->overriding_file);
			what = PARSE_EVERYTHING;
			if (UpdateGtkRC (Environment))
				signal_reload_gtkrc_file ();
			if (!get_flags (Environment->flags, ASE_NoKDEGlobalsTheming))
				UpdateKCSRC ();
		}

	/* let's free the memory used for parsing */
	if (tline)
		free (tline);
	show_progress ("Done loading configuration.");
	display_progress (True, "Done loading configuration.");

	check_desksize_sanity (ASDefaultScr);
	set_desktop_geometry_prop (Scr.wmprops, Scr.VxMax + Scr.MyDisplayWidth,
														 Scr.VyMax + Scr.MyDisplayHeight);

	if (get_flags (what, PARSE_FEEL_CONFIG)) {
		display_progress (True, "Applying Feel.");
		check_feel_sanity (&Scr.Feel);
		ApplyFeel (&Scr.Feel);
		asxml_var_insert (ASXMLVAR_MenuRecentSubmenuItems,
											Scr.Feel.recent_submenu_items);
	}

	if (get_flags (what, PARSE_LOOK_CONFIG)) {
		FixLook (&Scr.Look);

		asxml_var_insert (ASXMLVAR_IconButtonWidth, Scr.Look.ButtonWidth);
		asxml_var_insert (ASXMLVAR_IconButtonHeight, Scr.Look.ButtonHeight);

		asxml_var_insert (ASXMLVAR_MinipixmapWidth, Scr.Look.minipixmap_width);
		asxml_var_insert (ASXMLVAR_MinipixmapHeight,
											Scr.Look.minipixmap_height);

		asxml_var_insert (ASXMLVAR_MenuShowMinipixmaps,
											get_flags (Scr.Look.flags, MenuMiniPixmaps) ? 1 : 0);
		asxml_var_insert (ASXMLVAR_MenuShowUnavailable,
											get_flags (Scr.Look.flags,
																 MenuShowUnavailable) ? 1 : 0);
		asxml_var_insert (ASXMLVAR_MenuTxtItemsInd,
											get_flags (Scr.Look.flags, TxtrMenuItmInd) ? 1 : 0);

		if (thisdesktop == Scr.CurrentDesk) {
			MyBackground *new_back =
					get_desk_back_or_default (Scr.CurrentDesk, False);
			SendPacket (-1, M_NEW_BACKGROUND, 1, 1);
			if (new_back && new_back->loaded_im_name) {
				free (new_back->loaded_im_name);
				new_back->loaded_im_name = NULL;
			}
			change_desktop_background (Scr.CurrentDesk);
		}
	}

	if (get_flags (what, PARSE_LOOK_CONFIG | PARSE_FEEL_CONFIG)) {
		ReloadConfig (what);
	}

	if (get_flags
			(what, PARSE_BASE_CONFIG | PARSE_LOOK_CONFIG | PARSE_FEEL_CONFIG)) {
		int count = 0;
		ASHashIterator i;
		ARGB32 cursor_fore = ARGB32_White;
		ARGB32 cursor_back = ARGB32_Black;

		fix_menu_pin_on (&Scr.Look);

		/* also need to recolor cursors ! */
		if (Scr.Look.CursorFore)
			parse_argb_color (Scr.Look.CursorFore, &cursor_fore);
		if (Scr.Look.CursorBack)
			parse_argb_color (Scr.Look.CursorBack, &cursor_back);
		recolor_feel_cursors (&Scr.Feel, cursor_fore, cursor_back);
		XDefineCursor (dpy, Scr.Root, Scr.Feel.cursors[ASCUR_Default]);

		display_progress (True,
											get_flags (Scr.Look.flags,
																 MenuMiniPixmaps) ?
											"Reloading menu pixmaps :" :
											"Unloading menu pixmaps :");
		if (start_hash_iteration (Scr.Feel.Popups, &i))
			do {
				MenuData *md = curr_hash_data (&i);
				if (!get_flags (Scr.Look.flags, MenuMiniPixmaps))
					free_menu_pmaps (md);
				else {
					char *name = md->name;
					Bool newline = (count % 10 == 0);
					if (isdigit (name[0]))
						if (md->first != NULL && md->first->fdata->func == F_TITLE)
							name = md->first->item;
					display_progress (newline, newline ? "    %s" : "%s", name);
					++count;

					reload_menu_pmaps (md, get_flags (what, PARSE_BASE_CONFIG));
				}

			} while (next_hash_item (&i));

		display_progress (True, "Advertising titlebar properties ...");
		advertise_tbar_props ();
		display_progress (False, "Done.");
	}

	/* force update of window frames */
	if (get_flags
			(what,
			 PARSE_BASE_CONFIG | PARSE_LOOK_CONFIG | PARSE_FEEL_CONFIG |
			 PARSE_DATABASE_CONFIG)) {
		display_progress (True, "Redecorating client windows...");
		iterate_asbidirlist (Scr.Windows->clients,
												 redecorate_aswindow_iter_func, NULL, NULL, False);
		display_progress (False, "Done.");
	}

	if (old_image_manager && old_image_manager != Scr.image_manager) {
		display_progress (True, "Unloading old images...");
		if (Scr.RootImage && Scr.RootImage->imageman == old_image_manager) {
			safe_asimage_destroy (Scr.RootImage);
			Scr.RootImage = NULL;
		}
		destroy_image_manager (old_image_manager, False);
		display_progress (False, "Done.");
	}
	if (old_font_manager && old_font_manager != Scr.font_manager) {
		display_progress (True, "Unloading old fonts...");
		destroy_font_manager (old_font_manager, False);
		display_progress (False, "Done.");
	}

	ConfigureNotifyLoop ();

	remove_desktop_cover ();

	validate_rootpmap_props (Scr.wmprops);

	LOCAL_DEBUG_OUT ("TmpFeel.flags = 0x%lX, Scr.Feel.flags = 0x%lX",
									 TmpFeel.flags, Scr.Feel.flags);
}


#endif
