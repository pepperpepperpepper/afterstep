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

#define START_LONG_DRAW_OPERATION   grab_server()
#define STOP_LONG_DRAW_OPERATION    ungrab_server()

/* ASTBar tile indexes shared between menu item creation and rendering. */
#define MI_LEFT_SPACER_IDX  0
#define MI_LEFT_ARROW_IDX   1
#define MI_LEFT_ICON_IDX    2
#define MI_POPUP_IDX        3

static Bool
set_asmenu_item_look (ASMenuItem * item, MyLook * look,
											unsigned int icon_space, unsigned int arrow_space)
{
	ASFlagType hilite = NO_HILITE, fhilite = NO_HILITE;
	int subitem_offset = 1;
	LOCAL_DEBUG_OUT ("item.bar(%p)->look(%p)", item->bar, look);

	if (item->bar == NULL)
		return False;

	if (get_flags (look->flags, TxtrMenuItmInd))
		clear_flags (item->bar->state, BAR_FLAGS_CROP_BACK);
	else
		set_flags (item->bar->state, BAR_FLAGS_CROP_UNFOCUSED_BACK);


	item->bar->h_spacing = DEFAULT_MENU_SPACING;
	item->bar->h_border = DEFAULT_MENU_ITEM_HBORDER;
	item->bar->v_border = DEFAULT_MENU_ITEM_VBORDER;

	delete_astbar_tile (item->bar, MI_LEFT_SPACER_IDX);
	if (get_flags (item->flags, AS_MenuItemSubitem)) {
		subitem_offset =
				icon_space + 2 + item->bar->h_spacing + item->bar->h_spacing;
		add_astbar_spacer (item->bar, 0, 0, 0, NO_ALIGN, subitem_offset, 1);
	}
	delete_astbar_tile (item->bar, MI_LEFT_ARROW_IDX);
#if 1
	if (get_flags (item->flags, AS_MenuItemSubitem)) {
		if (look->MenuArrow)
			add_astbar_icon (item->bar, 1, 0, 0, ALIGN_VCENTER,
											 look->MenuArrow->image);
		else
			add_astbar_spacer (item->bar, 1, 0, 0, NO_ALIGN, arrow_space, 1);
	}
#endif
/*        add_astbar_spacer( item->bar, 1, 0, 0, NO_ALIGN, 1, 1 ); */

	if (get_flags (look->flags, MenuMiniPixmaps) && icon_space > 0) {
		/*set_astbar_tile_size( item->bar, MI_LEFT_SPACER_IDX, icon_space, 1 ); */
		delete_astbar_tile (item->bar, MI_LEFT_ICON_IDX);
		/* now readd it as minipixmap : */
		if (item->icon)
			add_astbar_icon (item->bar, 2, 0, 0, ALIGN_VCENTER, item->icon);
		else
			add_astbar_spacer (item->bar, 2, 0, 0, NO_ALIGN, icon_space, 1);
	}

	/* delete tile from Popup arrow cell : */
	delete_astbar_tile (item->bar, MI_POPUP_IDX);
	/* now readd it as proper type : */
	if (look->MenuArrow && get_flags (item->flags, AS_MenuItemHasSubmenu))
		add_astbar_icon (item->bar, 7, 0, 0, ALIGN_VCENTER,
										 look->MenuArrow->image);
	else
		add_astbar_spacer (item->bar, 7, 0, 0, NO_ALIGN, arrow_space, 1);


	if (get_flags (item->flags, AS_MenuItemDisabled)) {
		set_astbar_style_ptr (item->bar, -1, look->MSMenu[MENU_BACK_STIPPLE]);
		set_astbar_style_ptr (item->bar, BAR_STATE_FOCUSED,
													look->MSMenu[MENU_BACK_STIPPLE]);
	} else {
		set_astbar_style_ptr (item->bar, -1,
													look->
													MSMenu[get_flags
																 (item->flags,
																	AS_MenuItemSubitem) ? MENU_BACK_SUBITEM :
																 MENU_BACK_ITEM]);
		set_astbar_style_ptr (item->bar, BAR_STATE_FOCUSED,
													look->MSMenu[MENU_BACK_HILITE]);
	}
	if (look->DrawMenuBorders == DRAW_MENU_BORDERS_ITEM)
		fhilite = hilite = DEFAULT_MENU_HILITE;
	else if (Scr.Look.DrawMenuBorders == DRAW_MENU_BORDERS_OVERALL) {
		hilite = NO_HILITE_OUTLINE | LEFT_HILITE | RIGHT_HILITE;
		if (get_flags (item->flags, AS_MenuItemFirst))
			hilite |= TOP_HILITE;
		if (get_flags (item->flags, AS_MenuItemLast))
			hilite |= BOTTOM_HILITE;
		fhilite = hilite;
	} else if (look->DrawMenuBorders == DRAW_MENU_BORDERS_FOCUSED_ITEM)
		fhilite = DEFAULT_MENU_HILITE;
	else if (Scr.Look.DrawMenuBorders == DRAW_MENU_BORDERS_O_AND_F) {
		hilite = NO_HILITE_OUTLINE | LEFT_HILITE | RIGHT_HILITE;
		if (get_flags (item->flags, AS_MenuItemFirst))
			hilite |= TOP_HILITE;
		if (get_flags (item->flags, AS_MenuItemLast))
			hilite |= BOTTOM_HILITE;
		fhilite = DEFAULT_MENU_HILITE;
	}

	set_astbar_hilite (item->bar, BAR_STATE_UNFOCUSED, hilite);
	set_astbar_hilite (item->bar, BAR_STATE_FOCUSED, fhilite);

	if (get_flags (item->flags, AS_MenuItemDisabled)) {
		set_astbar_composition_method (item->bar, BAR_STATE_UNFOCUSED,
																	 Scr.Look.menu_scm);
		set_astbar_composition_method (item->bar, BAR_STATE_FOCUSED,
																	 Scr.Look.menu_scm);
	} else {
		set_astbar_composition_method (item->bar, BAR_STATE_UNFOCUSED,
																	 Scr.Look.menu_icm);
		set_astbar_composition_method (item->bar, BAR_STATE_FOCUSED,
																	 Scr.Look.menu_hcm);
	}


	return True;
}

void render_asmenu_bars (ASMenu * menu, Bool force)
{
	int i = menu->items_num;
	Bool rendered = False;
	if (menu->main_canvas->height > 1 && menu->main_canvas->width > 1) {
		ASImage *cache = NULL;
		START_LONG_DRAW_OPERATION;
		for (i = 0; i < menu->items_num; ++i) {
			int prev_y = -10000;
			register ASTBarData *bar = menu->items[i].bar;
			if (bar->win_y >= (int)(menu->main_canvas->height))
				continue;

/*			update_astbar_transparency (bar, menu->main_canvas, False); */

			LOCAL_DEBUG_OUT
					("bar %p needs_rendering? %s, visible? %s, lower ? %s", bar,
					 DoesBarNeedsRendering (bar) ? "yes" : "no",
					 (bar->win_y + (int)(bar->height) > 0) ? "yes" : "no",
					 ((int)(bar->win_y) > prev_y) ? "yes" : "no");
			if ((force || DoesBarNeedsRendering (bar))
					&& bar->win_y + (int)(bar->height) > 0
					&& (int)(bar->win_y) > prev_y) {
				if (render_astbar_cached_back
						(bar, menu->main_canvas, &cache, NULL))
					rendered = True;
			}
			prev_y = bar->win_y;
		}
		if (menu->visible_items_num < menu->items_num) {
			if (force || DoesBarNeedsRendering (menu->scroll_up_bar))
				if (render_astbar_cached_back
						(menu->scroll_up_bar, menu->main_canvas, &cache, NULL))
					rendered = True;
			if (force || DoesBarNeedsRendering (menu->scroll_down_bar))
				if (render_astbar_cached_back
						(menu->scroll_down_bar, menu->main_canvas, &cache, NULL))
					rendered = True;
		}
		STOP_LONG_DRAW_OPERATION;
		LOCAL_DEBUG_OUT ("%s menu items rendered!",
										 rendered ? "some" : "none");
		if (rendered) {
			update_canvas_display (menu->main_canvas);
			ASSync (False);
			set_flags (menu->state, AS_MenuRendered);
		}
		if (cache)
			safe_asimage_destroy (cache);
	}
}

static void set_menu_scroll_bar_look (ASTBarData * bar, MyLook * look,
																			Bool up)
{
	ASFlagType hilite = 0, fhilite = 0;

	bar->h_spacing = DEFAULT_MENU_SPACING;
	bar->h_border = DEFAULT_MENU_ITEM_HBORDER;
	bar->v_border = DEFAULT_MENU_ITEM_VBORDER;

	delete_astbar_tile (bar, -1);
	/* now readd it as proper type : */
	if (look->MenuArrow)
		add_astbar_icon (bar, 7, 0,
										 up ? FLIP_VERTICAL : FLIP_VERTICAL | FLIP_UPSIDEDOWN,
										 ALIGN_CENTER | RESIZE_H | RESIZE_H_SCALE,
										 look->MenuArrow->image);
	else
		add_astbar_label (bar, 7, 0, 0, ALIGN_CENTER, 5, 5, "...",
											AS_Text_ASCII);

	set_astbar_style_ptr (bar, -1, look->MSMenu[MENU_BACK_ITEM]);
	set_astbar_style_ptr (bar, BAR_STATE_FOCUSED,
												look->MSMenu[MENU_BACK_HILITE]);

	if (look->DrawMenuBorders == DRAW_MENU_BORDERS_ITEM)
		fhilite = hilite = DEFAULT_MENU_HILITE;
	else if (Scr.Look.DrawMenuBorders == DRAW_MENU_BORDERS_OVERALL) {
		hilite =
				NO_HILITE_OUTLINE | LEFT_HILITE | RIGHT_HILITE | (up ? TOP_HILITE :
																													BOTTOM_HILITE);
		fhilite = hilite;
	} else if (look->DrawMenuBorders == DRAW_MENU_BORDERS_FOCUSED_ITEM)
		fhilite = DEFAULT_MENU_HILITE;
	else if (Scr.Look.DrawMenuBorders == DRAW_MENU_BORDERS_O_AND_F) {
		hilite =
				NO_HILITE_OUTLINE | LEFT_HILITE | RIGHT_HILITE | (up ? TOP_HILITE :
																													BOTTOM_HILITE);
		fhilite = DEFAULT_MENU_HILITE;
	}

	set_astbar_hilite (bar, BAR_STATE_UNFOCUSED, hilite);
	set_astbar_hilite (bar, BAR_STATE_FOCUSED, fhilite);

	set_astbar_composition_method (bar, BAR_STATE_UNFOCUSED,
																 Scr.Look.menu_icm);
	set_astbar_composition_method (bar, BAR_STATE_FOCUSED,
																 Scr.Look.menu_hcm);
}

void set_asmenu_look (ASMenu * menu, MyLook * look)
{
	int i;
	unsigned int max_width = 0, max_height = 0;
	int display_size;

	menu->arrow_space =
			look->MenuArrow ? look->MenuArrow->width : DEFAULT_ARROW_SIZE;

	set_menu_scroll_bar_look (menu->scroll_up_bar, look, True);
	set_menu_scroll_bar_look (menu->scroll_down_bar, look, False);

	i = menu->items_num;
	while (--i >= 0) {
		unsigned int width, height;
		register ASTBarData *bar;
		set_asmenu_item_look (&(menu->items[i]), look, menu->icon_space,
													menu->arrow_space);
		if ((bar = menu->items[i].bar) != NULL) {
			width = calculate_astbar_width (bar);
			if (get_flags (menu->items[i].flags, AS_MenuItemSubitem))
				height = max_height;
			else
				height = calculate_astbar_height (bar);
			LOCAL_DEBUG_OUT ("i(%d)->bar(%p)->size(%ux%u)", i, bar, width,
											 height);
			if (width > max_width)
				max_width = width;
			if (height > max_height)
				max_height = height;
		}
	}
	/* some sanity checks : */

	if (max_height > MAX_MENU_ITEM_HEIGHT)
		max_height = MAX_MENU_ITEM_HEIGHT;
	if (max_height == 0)
		max_height = 1;
	/* we want height to be even at all times */
	max_height = ((max_height + 1) / 2) * 2;

	if (max_width > MAX_MENU_WIDTH)
		max_width = MAX_MENU_WIDTH;
	if (max_width == 0)
		max_width = 1;
	menu->item_width = max_width;
	menu->item_height = max_height;

	display_size = max_height * menu->items_num;
	menu->scroll_bar_size = max_height / 2;

	if (display_size > MAX_MENU_HEIGHT) {
		menu->visible_items_num = MAX_MENU_HEIGHT / max_height;
		display_size = menu->visible_items_num * max_height;	/* important! */
		display_size += 2 * menu->scroll_bar_size;	/* we'll need to render two more scroll bars */
	} else
		menu->visible_items_num = display_size / max_height;

	/* setting up desired size  - may or maynot be overriden by user actions -
	 * so we'll use ConfigureNotify events later on to keep ourselves up to date*/
	if (menu->owner != NULL)
		resize_canvas (menu->main_canvas, max_width, display_size);
	menu->optimal_width = max_width;
	menu->optimal_height = display_size;

	if (menu->top_item > menu->items_num - menu->visible_items_num)
		menu->top_item = menu->items_num - menu->visible_items_num;

	set_astbar_size (menu->scroll_up_bar, max_width, menu->scroll_bar_size);
	set_astbar_size (menu->scroll_down_bar, max_width,
									 menu->scroll_bar_size);

	i = menu->items_num;
	while (--i >= 0)
		set_astbar_size (menu->items[i].bar, max_width, max_height);
	ASSync (False);
}

