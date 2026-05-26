/*
 * Copyright (c) 2002 Sasha Vasko
 * module has been entirely rewritten to use ASCanvas from original menus.c
 * that was implemented by :
 *      Copyright (C) 1993 Rob Nation
 *      Copyright (C) 1995 Bo Yang
 *      Copyright (C) 1996 Frank Fejes
 *      Copyright (C) 1996 Alfredo Kojima
 *      Copyright (C) 1998 Guylhem Aznar
 *      Copyright (C) 1998, 1999 Ethan Fischer
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

/***********************************************************************
 * afterstep menu code
 ***********************************************************************/

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include "menus_internal.h"

ASHints *make_menu_hints (ASMenu * menu)
{

	ASHints *hints = NULL;

	if (menu == NULL)
		return NULL;

	hints = safecalloc (1, sizeof (ASHints));

	/* normal hints : */
	if (menu->title) {
		hints->names[0] = mystrdup (menu->title);
		hints->names_encoding[0] =
				get_flags (menu->state,
									 AS_MenuTitleIsUTF8) ? AS_Text_UTF8 : AS_Text_ASCII;
	} else {
		hints->names[0] = mystrdup (menu->name);
		hints->names_encoding[0] =
				get_flags (menu->state,
									 AS_MenuNameIsUTF8) ? AS_Text_UTF8 : AS_Text_ASCII;
	}
	hints->names[1] = mystrdup (ASMENU_RES_CLASS);
	/* these are merely shortcuts to the above list DON'T FREE THEM !!! */
	hints->res_name = hints->names[1];
	hints->res_class = hints->names[1];
	hints->icon_name = hints->names[0];

	hints->flags = AS_DontCirculate | AS_SkipWinList | AS_Titlebar | AS_Border | AS_Handles | AS_AcceptsFocus | AS_Gravity | AS_MinSize | AS_MaxSize | AS_SizeInc;	/*|AS_VerticalTitle ; */
	hints->protocols = AS_DoesWmTakeFocus;
	hints->function_mask = ~(AS_FuncPopup |	/* everything else is allowed ! */
													 AS_FuncMinimize | AS_FuncMaximize);

	hints->max_width = MAX_MENU_WIDTH;
	hints->max_height =
			MIN (MAX_MENU_HEIGHT, menu->items_num * menu->item_height);
	hints->width_inc = 0;
	hints->height_inc = menu->item_height;
	hints->gravity = StaticGravity;
	hints->border_width = BW;
	hints->handle_width = BOUNDARY_WIDTH;

	hints->frame_name = mystrdup ("ASMenuFrame");
	hints->mystyle_names[BACK_FOCUSED] =
			mystrdup (Scr.Look.MSMenu[MENU_BACK_HITITLE]->name);
	hints->mystyle_names[BACK_UNFOCUSED] =
			mystrdup (Scr.Look.MSMenu[MENU_BACK_TITLE]->name);
	hints->mystyle_names[BACK_STICKY] =
			mystrdup (Scr.Look.MSMenu[MENU_BACK_TITLE]->name);

	hints->disabled_buttons = 0;

	hints->min_width = menu->optimal_width;
	hints->min_height = menu->item_height;

	return hints;
}

/*************************************************************************/
/* End of Menu event handlers:                                           */
/*************************************************************************/
void show_asmenu (ASMenu * menu, int x, int y)
{
	ASStatusHints status;
	ASHints *hints = make_menu_hints (menu);
	ASInternalWindow *asiw = safecalloc (1, sizeof (ASInternalWindow));
	int gravity = StaticGravity;
	unsigned int tbar_width = 0, tbar_height = 0;
	ASRawHints raw;
	static char *ASMenuStyleNames[2] = { "ASMenu", NULL };
	ASDatabaseRecord *db_rec;
	int my_width, my_height;

	LOCAL_DEBUG_OUT ("menu(%s) - encoding set in hints is %d",
									 hints->names[0], hints->names_encoding[0]);

	asiw->data = (ASMagic *) menu;

	asiw->register_subwindows = menu_register_subwindows;
	asiw->on_moveresize = on_menu_moveresize;
	asiw->on_hilite_changed = on_menu_hilite_changed;
	asiw->on_pressure_changed = on_menu_pressure_changed;
	asiw->on_pointer_event = on_menu_pointer_event;
	asiw->on_scroll_event = on_menu_scroll_event;
	asiw->on_keyboard_event = on_menu_keyboard_event;
	asiw->on_look_feel_changed = on_menu_look_feel_changed;
	asiw->on_root_background_changed = on_menu_root_background_changed;
	asiw->destroy = menu_destroy;

	db_rec =
			fill_asdb_record (Database, &(ASMenuStyleNames[0]), NULL, False);
	merge_asdb_hints (hints, NULL, db_rec, NULL, HINT_GENERAL);

	estimate_titlebar_size (hints, &tbar_width, &tbar_height);

	if (tbar_width > MAX_MENU_WIDTH)
		tbar_width = MAX_MENU_WIDTH;

	if (tbar_width > menu->optimal_width) {
		menu->optimal_width = tbar_width;
		hints->min_width = tbar_width;
	}

	my_width = menu->optimal_width + tbar_height;
	my_height = menu->optimal_height + tbar_height;

	y -= tbar_height;
	x -= menu->optimal_width / 3;

	if (menu->supermenu) {				/* we need to make sure we would not overlay our parent completely ! */
		ASCanvas *pc = menu->supermenu->main_canvas;
		int requested_x = x, requested_y = y;
		if (x + my_width > Scr.MyDisplayWidth)
			x = Scr.MyDisplayWidth - my_width;
		if (y + my_height > Scr.MyDisplayHeight)
			y = Scr.MyDisplayHeight - my_height;
		if (requested_x != x || requested_y != y) {
			if (x <= pc->root_x + 10
					&& x + my_width >= pc->root_x + pc->width
					&& y <= pc->root_y + 10
					&& y + my_height >= pc->root_y + pc->height) {
				if (requested_y - pc->root_y > tbar_height)
					y = (requested_y + pc->root_y) / 2;
				else
					y = requested_y;
				if (y + my_height >= Scr.MyDisplayHeight) {
					if (pc->root_y + pc->height - requested_y < menu->item_height)
						y = requested_y - my_height;
					else
						y = (pc->root_y + pc->height + requested_y) / 2 - my_height;
				}
			}
			if (requested_y != y) {
				if (requested_x != x)
					gravity = SouthEastGravity;
				else
					gravity = SouthWestGravity;
			} else if (requested_x != x)
				gravity = NorthEastGravity;
		}
	} else {
		if (x <= MIN_MENU_X)
			x = MIN_MENU_X;
		else if (x + menu->optimal_width + tbar_height > MAX_MENU_X) {
			x = MAX_MENU_X - menu->optimal_width;
			gravity = EastGravity;
		} else if (x + menu->optimal_width + tbar_height +
							 menu->optimal_width > MAX_MENU_X)
			gravity = EastGravity;

		if (y <= MIN_MENU_Y)
			y = MIN_MENU_Y;
		else if (y + menu->optimal_height + tbar_height > MAX_MENU_Y) {
			y = MAX_MENU_Y - menu->optimal_height;
			gravity =
					(gravity == StaticGravity) ? SouthGravity : SouthEastGravity;
		} else if (y + menu->optimal_height + tbar_height +
							 menu->optimal_height > MAX_MENU_Y)
			gravity =
					(gravity == StaticGravity) ? SouthGravity : SouthEastGravity;
	}
	hints->gravity = gravity;


	/* status hints : */
	memset (&status, 0x00, sizeof (ASStatusHints));
	status.flags = AS_StartPosition |
			AS_StartPositionUser |
			AS_StartSize |
			AS_StartSizeUser |
			AS_StartViewportX |
			AS_StartViewportY |
			AS_StartDesktop | AS_StartLayer | AS_StartsSticky;

	status.x = x;
	status.y = y;
	status.width = menu->optimal_width;
	status.height = menu->optimal_height;
	status.viewport_x = Scr.Vx;
	status.viewport_y = Scr.Vy;
	status.desktop = Scr.CurrentDesk;
	status.layer = AS_LayerMenu;

	/* now lets merge it with database record for ASMenu style - if it exists : */
	if (db_rec) {
		memset (&raw, 0x00, sizeof (raw));
		raw.placement.x = status.x;
		raw.placement.y = status.y;
		raw.placement.width = menu->optimal_width;
		raw.placement.height = menu->optimal_height;
		raw.scr = ASDefaultScr;
		raw.border_width = 0;

/*		LOCAL_DEBUG_OUT( "printing db record %p for names %p and db %p", pdb_rec, clean->names, db );
		print_asdb_matched_rec (NULL, NULL, Database, db_rec);
  */
		merge_asdb_hints (hints, &raw, db_rec, &status, ASFLAGS_EVERYTHING);
		destroy_asdb_record (db_rec, False);
	}
	/* lets make sure we got everything right : */
	LOCAL_DEBUG_OUT ("menu(%s) - encoding set in hints is %d",
									 hints->names[0], hints->names_encoding[0]);
	check_hints_sanity (ASDefaultScr, hints, &status, menu->main_canvas->w);
	check_status_sanity (ASDefaultScr, &status);

#if 0
	{
		int pointer_x = 0, pointer_y = 0;
		if (ASQueryPointerRootXY (&pointer_x, &pointer_y)) {
			if (pointer_x < status.x || pointer_y < status.y || pointer_x > status.x + status.width || pointer_y > status.y + status.height) {	/* not likely to happen:  */
				XWarpPointer (dpy, Scr.Root, Scr.Root, pointer_x, pointer_y, 0, 0,
											status.x + 5, status.y + 5);
			}
		}
	}
#endif
	LOCAL_DEBUG_OUT ("menu(%s) - encoding set in hints is %d",
									 hints->names[0], hints->names_encoding[0]);

	menu->owner =
			AddInternalWindow (menu->main_canvas->w, &asiw, &hints, &status);

	/* need to cleanup if we failed : */
	if (asiw) {
		if (asiw->data)
			destroy_asmenu (&menu);
		free (asiw);
	}
	if (hints)
		destroy_hints (hints, False);
}

/*************************************************************************/
/* high level ASMenu functionality :                                     */
/*************************************************************************/
ASMenu *run_submenu (ASMenu * supermenu, MenuData * md, int x, int y)
{
	ASMenu *menu = NULL;
	if (md) {
		menu = create_asmenu (md->name);
		clear_flags (menu->state, AS_MenuRendered);
		set_asmenu_data (menu, md, True,
										 get_flags (Scr.Look.flags, MenuShowUnavailable),
										 Scr.Feel.recent_submenu_items);
		set_asmenu_look (menu, &Scr.Look);
		/* will set scroll position when ConfigureNotify arrives */
		menu->supermenu = supermenu;
		show_asmenu (menu, x, y);
		MapConfigureNotifyLoop ();
	}
	return menu;
}


ASMenu *run_menu_data (MenuData * md)
{
	ASMenu *menu = NULL;
	int x = 0, y = 0;
	Bool persistent = (get_flags (Scr.Feel.flags, PersistentMenus)
										 || ASTopmostMenu == NULL);

	close_asmenu (&ASTopmostMenu);

	if (persistent && md != NULL) {
		if (!ASQueryPointerRootXY (&x, &y)) {
			x = (Scr.MyDisplayWidth * 2) / 3;
			y = (Scr.MyDisplayHeight * 3) / 4;
		}
		ASTopmostMenu = run_submenu (NULL, md, x, y);
	}
	return menu;
}

void run_menu (const char *name, Window client_window)
{
	MenuData *md = FindPopup (name, False);
	run_menu_data (md);
	if (ASTopmostMenu)
		ASTopmostMenu->client_window = client_window;
}
