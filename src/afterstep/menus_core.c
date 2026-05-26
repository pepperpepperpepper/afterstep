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


ASMenu *ASTopmostMenu = NULL;

/*************************************************************************/
/* low level ASMenu functionality :                                      */
/*************************************************************************/
static Window make_menu_window (Window parent)
{
	Window w;
	XSetWindowAttributes attr;
	if (parent == None)
		parent = Scr.Root;

	attr.cursor = Scr.Feel.cursors[ASCUR_Menu];
	attr.event_mask = AS_MENU_EVENT_MASK;
	w = create_visual_window (Scr.asv, parent, -10, -10, 1, 1, 0,
														InputOutput, CWEventMask | CWCursor, &attr);

	if (w && parent != Scr.Root)
		XMapRaised (dpy, w);
	return w;
}


ASMenu *create_asmenu (const char *name)
{
	ASMenu *menu = safecalloc (1, sizeof (ASMenu));
	Window w;
	menu->magic = MAGIC_ASMENU;
	w = make_menu_window (None);
	menu->main_canvas = create_ascanvas (w);
	menu->name = mystrdup (name);
	menu->scroll_up_bar = create_astbar ();
	menu->scroll_down_bar = create_astbar ();
	return menu;
}

void free_asmenu_item (ASMenuItem * item)
{
	if (item->bar)
		destroy_astbar (&(item->bar));
	if (item->icon)
		safe_asimage_destroy (item->icon);
	free_func_data (&(item->fdata));
}

void close_asmenu_submenu (ASMenu * menu)
{
	LOCAL_DEBUG_CALLER_OUT ("top(%p)->supermenu(%p)->menu(%p)->submenu(%p)",
													ASTopmostMenu, menu->supermenu, menu,
													menu->submenu);
	if (menu->submenu) {
		if (menu->submenu->supermenu == menu)
			menu->submenu->supermenu = NULL;
		if (menu->submenu->owner) {
			/* cannot use Destroy directly - must go through the normal channel: */
			unmap_canvas_window (menu->submenu->main_canvas);
			ASWIN_SET_FLAGS (menu->submenu->owner, AS_Hidden);
			ASWIN_SET_FLAGS (menu->submenu->owner, AS_UnMapPending);
			menu->submenu = NULL;
		} else
			destroy_asmenu (&(menu->submenu));
	}
}

void destroy_asmenu (ASMenu ** pmenu)
{
	if (pmenu) {
		ASMenu *menu = *pmenu;
		if (menu && menu->magic == MAGIC_ASMENU) {
			Window w = menu->main_canvas->w;
			LOCAL_DEBUG_CALLER_OUT
					("top(%p)->supermenu(%p)->menu(%p)->submenu(%p)", ASTopmostMenu,
					 menu->supermenu, menu, menu->submenu);

			while (timer_remove_by_data (menu)) ;

			if (menu->supermenu && menu->supermenu->submenu == menu)
				menu->supermenu->submenu = NULL;
			else if (ASTopmostMenu == menu)
				ASTopmostMenu = NULL;

			close_asmenu_submenu (menu);

			if (menu->main_canvas)
				destroy_ascanvas (&(menu->main_canvas));
			if (menu->owner) {
				ASWIN_SET_FLAGS (menu->owner, AS_Dead);
				ASWIN_SET_FLAGS (menu->owner, AS_Hidden);
				ASWIN_SET_FLAGS (menu->owner, AS_UnMapPending);
			}
			destroy_registered_window (w);

			if (menu->items) {
				register int i = menu->items_num;
				while (--i >= 0)
					free_asmenu_item (&(menu->items[i]));
				free (menu->items);
				menu->items = NULL;
			}

			if (menu->scroll_up_bar)
				destroy_astbar (&(menu->scroll_up_bar));
			if (menu->scroll_down_bar)
				destroy_astbar (&(menu->scroll_down_bar));

			if (menu->name)
				free (menu->name);
			if (menu->title)
				free (menu->title);

			destroy_asballoon (&(menu->comment_balloon));
			destroy_asballoon (&(menu->item_balloon));

			if (menu->volitile_menu_data != NULL)
				destroy_menu_data (&(menu->volitile_menu_data));


			menu->magic = 0;
			free (menu);
			*pmenu = NULL;
		}
	}
}

void close_asmenu (ASMenu ** pmenu)
{
	if (pmenu) {
		ASMenu *menu = *pmenu;
		if (menu) {
			LOCAL_DEBUG_CALLER_OUT
					("top(%p)->supermenu(%p)->menu(%p)->submenu(%p)", ASTopmostMenu,
					 menu->supermenu, menu, menu->submenu);
			if (menu->submenu) {
				if (menu->submenu->supermenu == menu)
					menu->submenu->supermenu = NULL;
				close_asmenu (&(menu->submenu));
			}
			if (menu->owner) {
				/* cannot use Destroy directly - must go through the normal channel: */
				unmap_canvas_window (menu->main_canvas);
				ASWIN_SET_FLAGS (menu->owner, AS_Hidden);
				ASWIN_SET_FLAGS (menu->owner, AS_UnMapPending);
			} else
				destroy_asmenu (&(menu));
			*pmenu = NULL;
		}
	}
}

static void set_asmenu_item_data (ASMenuItem * item, MenuDataItem * mdi)
{
	ASImage *icon_im = NULL;

	item->source = mdi;

	if (item->bar == NULL) {
		item->bar = create_astbar ();
	} else
		delete_astbar_tile (item->bar, -1);	/* delete all tiles */


	if (item->icon) {
		safe_asimage_destroy (item->icon);
		item->icon = NULL;
	}
	/* we can only use images that are reference counted */
	if (mdi->minipixmap[MINIPIXMAP_Icon].image)
		icon_im = mdi->minipixmap[MINIPIXMAP_Icon].image;

#if 0
	else if (mdi->minipixmap[MINIPIXMAP_Icon].filename)
		icon_im =
				GetASImageFromFile (mdi->minipixmap[MINIPIXMAP_Icon].filename);
	else if (mdi->fdata->func == F_CHANGE_BACKGROUND_FOREIGN
					 && !is_web_background (mdi->fdata))
		icon_im = GetASImageFromFile (mdi->fdata->text);
#endif

	if (icon_im) {
		item->icon = check_scale_menu_pmap (icon_im, mdi->flags);
		if (item->icon != icon_im
				&& icon_im != mdi->minipixmap[MINIPIXMAP_Icon].image)
			safe_asimage_destroy (icon_im);
		if (item->icon && item->icon == mdi->minipixmap[MINIPIXMAP_Icon].image) {
			if (item->icon->imageman != NULL)
				item->icon = dup_asimage (item->icon);
			else
				item->icon = clone_asimage (item->icon, 0xFFFFFFFF);
		}
	}

	/* reserve space for minipixmap */
#define MI_LEFT_SPACER_IDX  0
	add_astbar_spacer (item->bar, 0, 0, 0, NO_ALIGN, 1, 1);	/*0 */
#define MI_LEFT_ARROW_IDX   1
	add_astbar_spacer (item->bar, 1, 0, 0, NO_ALIGN, 1, 1);	/*1 */
#define MI_LEFT_ICON_IDX    2
	add_astbar_spacer (item->bar, 2, 0, 0, NO_ALIGN, 1, 1);	/*2 */
	/* reserve space for popup icon : */
#define MI_POPUP_IDX        3
	add_astbar_spacer (item->bar, 7, 0, 0, NO_ALIGN, 1, 1);	/*3 */

	/* optional menu items : */
	/* add label */
	{
		int encoding =
				get_flags (mdi->flags,
									 MD_NameIsUTF8) ? AS_Text_UTF8 : mdi->fdata->
				name_encoding;
		if (mdi->item) {
			add_astbar_label (item->bar, 3, 0, 0, ALIGN_LEFT | ALIGN_VCENTER, 0,
												0, mdi->item, encoding);
			item->first_sym = mdi->item[0];
		}
		/* add hotkey */
		if (mdi->item2)
			add_astbar_label (item->bar, 4, 0, 0, ALIGN_RIGHT | ALIGN_VCENTER, 0,
												0, mdi->item2, encoding);
	}
	item->flags = 0;

	if (IsDynamicPopup (mdi->fdata->func)) {
		set_flags (item->flags, AS_MenuItemHasSubmenu);
	} else if (mdi->fdata->func == F_POPUP) {
		MenuData *submenu = FindPopup (FDataPopupName (*(mdi->fdata)), True);
		if (submenu == NULL)
			set_flags (item->flags, AS_MenuItemDisabled);
		else {
			MenuDataItem *smdi = submenu->first;

			set_flags (item->flags, AS_MenuItemHasSubmenu);
			while (smdi) {
				if (smdi->fdata->func != F_TITLE)
					break;
				smdi = smdi->next;
			}
			if (smdi == NULL)
				set_flags (item->flags, AS_MenuItemDisabled);
		}
	} else if (get_flags (mdi->flags, MD_Disabled)
						 || mdi->fdata->func == F_NOP)
		set_flags (item->flags, AS_MenuItemDisabled);

	dup_func_data (&(item->fdata), mdi->fdata);
}

/*************************************************************************/
/* midium level ASMenu functionality :                                   */
/*************************************************************************/
static unsigned int
extract_recent_subitems (char *submenu_name, MenuDataItem ** subitems,
												 unsigned int max_subitems)
{
	MenuData *md = FindPopup (submenu_name, True);
	int used = 0;
	MenuDataItem *smdi = md->first;
	while (smdi != NULL) {
		if (smdi->last_used_time > 0) {
			if (used < max_subitems) {
				subitems[used] = smdi;
				++used;
			} else {
				register int i = used;
				while (--i >= 0)
					if (subitems[i]->last_used_time > smdi->last_used_time) {
						subitems[i] = smdi;
						break;
					}
			}
		}
		smdi = smdi->next;
	}															/* while smdi */
	return used;
}

void
set_asmenu_data (ASMenu * menu, MenuData * md, Bool first_time,
								 Bool show_unavailable, int recent_items)
{
	int items_num = md->items_num;
	int i = 0;
	int real_items_num = 0;
	int max_icon_size = 0;
	MenuDataItem **subitems = NULL;
	MenuDataItem *title_mdi = NULL;


	if (menu->items_num < items_num) {
		menu->items = realloc (menu->items, items_num * (sizeof (ASMenuItem)));
		memset (&(menu->items[menu->items_num]), 0x00,
						(items_num - menu->items_num) * sizeof (ASMenuItem));
	}

	if (menu->title) {
		free (menu->title);
		menu->title = NULL;
	}
	clear_flags (menu->state, AS_MenuTitleIsUTF8 | AS_MenuNameIsUTF8);

	if (get_flags (md->flags, MD_NameIsUTF8))
		set_flags (menu->state, AS_MenuNameIsUTF8);

	if (items_num > 0) {
		MenuDataItem *mdi = md->first;
		if (md->recent_items >= 0)
			recent_items = md->recent_items;
		if (recent_items > 0)
			subitems = safecalloc (recent_items, sizeof (MenuDataItem *));

		LOCAL_DEBUG_OUT ("show_unavailable = %d", show_unavailable);

		for (mdi = md->first; real_items_num < items_num && mdi != NULL;
				 mdi = mdi->next)
			if (mdi->fdata->func == F_TITLE && menu->title == NULL) {
				title_mdi = mdi;
				menu->title = mystrdup (mdi->item);
				if (get_flags (mdi->flags, MD_NameIsUTF8))
					set_flags (menu->state, AS_MenuTitleIsUTF8);
			} else {
				ASMenuItem *item = &(menu->items[real_items_num]);

				if (!show_unavailable) {
					if (get_flags (mdi->flags, MD_Disabled))
						continue;
					if (mdi->fdata->func == F_NOP) {
						int len = mdi->item ? strlen (mdi->item) : 0;
						if (mdi->item2)
							len += strlen (mdi->item2);
						if (len > 0)
							continue;
					}
				}

				set_asmenu_item_data (item, mdi);
				++real_items_num;

				if (item->fdata.func == F_POPUP
						&& !get_flags (item->flags, AS_MenuItemDisabled)
						&& subitems != NULL) {
					int used =
							extract_recent_subitems (FDataPopupName (item->fdata),
																			 subitems, recent_items);
					if (used > 0) {
						items_num += used;
						if (menu->items_num < items_num) {
							int to_zero = max (real_items_num, menu->items_num);
							menu->items =
									realloc (menu->items, items_num * (sizeof (ASMenuItem)));
							memset (&(menu->items[to_zero]), 0x00,
											(items_num - to_zero) * sizeof (ASMenuItem));
						}
						for (i = 0; i < used; ++i) {
							set_asmenu_item_data (&(menu->items[real_items_num]),
																		subitems[i]);
							subitems[i] = NULL;
							set_flags (menu->items[real_items_num].flags,
												 AS_MenuItemSubitem);
							++real_items_num;
						}
					}
				}
			}
		if (real_items_num > 0) {
			for (i = 0; i < real_items_num; ++i) {
				register ASMenuItem *item = &(menu->items[i]);
				if (item->icon)
					if (item->icon->width > max_icon_size)
						max_icon_size = item->icon->width;
			}
			set_flags (menu->items[0].flags, AS_MenuItemFirst);
			set_flags (menu->items[real_items_num - 1].flags, AS_MenuItemLast);
		}
		if (subitems)
			free (subitems);
	}
	menu->icon_space = MIN (max_icon_size, (Scr.MyDisplayWidth >> 3));
	/* if we had more then needed tbars - destroy the rest : */
	if (menu->items) {
		i = menu->items_num;
		while (--i >= real_items_num)
			free_asmenu_item (&(menu->items[i]));
	}
	menu->items_num = real_items_num;
	menu->top_item = 0;
	if (first_time) {
		menu->selected_item = 0;
		if (menu->items && menu->items[0].bar)
			set_astbar_focused (menu->items[0].bar, menu->main_canvas, True);
	} else
		menu->selected_item = -1;
	menu->pressed_item = -1;

	{
		char *comment = md->comment;
		int encoding =
				get_flags (md->flags,
									 MD_CommentIsUTF8) ? AS_Text_UTF8 : AS_Text_ASCII;
		if (title_mdi != NULL && title_mdi->comment != NULL) {
			encoding =
					get_flags (title_mdi->flags,
										 MD_CommentIsUTF8) ? AS_Text_UTF8 : AS_Text_ASCII;
			comment = title_mdi->comment;
		}
		if (comment) {
			if (menu->comment_balloon == NULL)
				menu->comment_balloon =
						create_asballoon_with_text_for_state (MenuBalloons, NULL,
																									comment, encoding);
			else {
				balloon_set_text (menu->comment_balloon, comment, encoding);
				clear_flags (menu->state, AS_MenuBalloonShown);
			}
		} else
			destroy_asballoon (&(menu->comment_balloon));
	}
}

/*************************************************************************/
/* stack helpers :                                                      */
/*************************************************************************/
ASMenu *find_asmenu (const char *name)
{
	if (name) {
		ASMenu *menu = ASTopmostMenu;
		while (menu) {
			if (mystrcasecmp (menu->name, name) == 0 ||
					mystrcasecmp (menu->title, name) == 0)
				return menu;
			menu = menu->submenu;
		}
	}
	return NULL;
}

static ASMenu *find_topmost_transient_menu (ASMenu * menu)
{
	if (menu && menu->supermenu && menu->supermenu->magic == MAGIC_ASMENU)
		if (!get_flags (menu->supermenu->state, AS_MenuPinned))
			return find_topmost_transient_menu (menu->supermenu);
	return menu;
}

void pin_asmenu (ASMenu * menu)
{
	if (menu) {
		ASMenu *menu_to_close = find_topmost_transient_menu (menu);

		close_asmenu_submenu (menu);
		if (menu == ASTopmostMenu)
			ASTopmostMenu = NULL;
		else if (menu->supermenu &&
						 menu->supermenu->magic == MAGIC_ASMENU &&
						 menu->supermenu->submenu == menu)
			menu->supermenu->submenu = NULL;
		if (menu_to_close != menu)
			close_asmenu (&menu_to_close);

		set_flags (menu->state, AS_MenuPinned);
		if (menu->owner) {
			clear_flags (menu->owner->hints->function_mask, AS_FuncPinMenu);
			redecorate_window (menu->owner, False);
			on_window_status_changed (menu->owner, True);
			if (Scr.Windows->hilited == menu->owner)
				on_window_hilite_changed (menu->owner, True);
		}
		close_asmenu_submenu (menu);
	}
}

Bool is_menu_pinnable (ASMenu * menu)
{
	if (menu && menu->magic == MAGIC_ASMENU)
		return !get_flags (menu->state, AS_MenuPinned);
	return False;
}
