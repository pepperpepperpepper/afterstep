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

#include <X11/keysym.h>

static void set_menu_item_used (ASMenu * menu, MenuDataItem * mdi)
{
	MenuData *md = FindPopup (menu->name, False);
	if (md && md->magic == MAGIC_MENU_DATA) {
		register MenuDataItem *i = md->first;
		while (i != NULL) {
			if (i == mdi) {
				i->last_used_time = time (NULL);
				break;
			}
			i = i->next;
		}
	}

}



void set_asmenu_scroll_position (ASMenu * menu, int pos);
static void menu_item_balloon_timer_handler (void *data);

void select_menu_item (ASMenu * menu, int selection, Bool render)
{
	LOCAL_DEBUG_CALLER_OUT ("%p,%d", menu, selection);

	if (AS_ASSERT (menu) || menu->items_num == 0)
		return;

	while (timer_remove_by_data (menu)) ;

	if (selection < 0)
		selection = 0;
	else if (selection >= (int)menu->items_num)
		selection = menu->items_num - 1;

	if (selection != menu->selected_item) {
		if (menu->item_balloon) {
			while (timer_remove_by_data (menu)) ;
			withdraw_balloon (menu->item_balloon);
		}
		close_asmenu_submenu (menu);
		if (menu->selected_item >= 0)
			set_astbar_focused (menu->items[menu->selected_item].bar,
													NULL /*needs_scrolling?NULL:menu->main_canvas */
													, False);
	}
	set_astbar_focused (menu->items[selection].bar,
											NULL /*needs_scrolling?NULL:menu->main_canvas */ ,
											True);
	menu->selected_item = selection;

	if (selection < menu->top_item)
		set_asmenu_scroll_position (menu, MAX (selection, 0));
	else if (selection >= menu->top_item + menu->visible_items_num)
		set_asmenu_scroll_position (menu,
																(selection - menu->visible_items_num) + 1);
	else if (render)
		render_asmenu_bars (menu, False);

	if (menu->items[selection].source) {
		long balloon_delay = -1;
		LOCAL_DEBUG_OUT ("selection func = %d",
										 menu->items[selection].fdata.func);
		if (menu->items[selection].source->comment)
			balloon_delay = MenuBalloons->look->Delay;
		else if (menu->items[selection].fdata.func ==
						 F_CHANGE_BACKGROUND_FOREIGN
						 || menu->items[selection].fdata.func == F_CHANGE_BACKGROUND)
			balloon_delay =
					MenuBalloons->look->Delay >
					1000 ? MenuBalloons->look->Delay -
					1000 : MenuBalloons->look->Delay;

		if (balloon_delay >= 0) {
			if (menu->item_balloon == NULL)
				menu->item_balloon =
						create_asballoon_for_state (MenuBalloons, NULL);

			timer_new (balloon_delay, &menu_item_balloon_timer_handler,
								 (void *)menu);
		}
	}
}

static void menu_item_balloon_timer_handler (void *data)
{
	ASMenu *menu = (ASMenu *) data;

	if (menu && menu->item_balloon) {
		int selection = menu->selected_item;
		ASMenuItem *item;
		if (selection < 0 || selection >= (int)menu->items_num)
			return;
		item = &(menu->items[selection]);
		if (item->source->comment) {
			int encoding =
					get_flags (item->source->flags,
										 MD_CommentIsUTF8) ? AS_Text_UTF8 : AS_Text_ASCII;
			balloon_set_text (menu->item_balloon, item->source->comment,
												encoding);
		} else if (item->fdata.func == F_CHANGE_BACKGROUND_FOREIGN
							 || item->fdata.func == F_CHANGE_BACKGROUND) {

			load_menuitem_pmap (item->source, MINIPIXMAP_Preview, True);
			LOCAL_DEBUG_OUT ("change background func = \"%s\", preview = %p",
											 item->fdata.text,
											 item->source->minipixmap[MINIPIXMAP_Preview].image);
			if (item->source->minipixmap[MINIPIXMAP_Preview].image == NULL) {
				if (is_web_background (&(item->fdata))
						&& item->source->minipixmap[MINIPIXMAP_Preview].loadCount == 1)
					timer_new (500, &menu_item_balloon_timer_handler, (void *)menu);	/* give it another chance */
				return;
			}
			balloon_set_image (menu->item_balloon,
												 dup_asimage (item->source->
																			minipixmap[MINIPIXMAP_Preview].
																			image));
		}
		display_balloon_nodelay (menu->item_balloon);
	}
}

void set_asmenu_scroll_position (ASMenu * menu, int pos)
{
	int curr_y;
	int i;
	int first_item, last_item;

	LOCAL_DEBUG_CALLER_OUT ("%p,%d", menu, pos);
	if (AS_ASSERT (menu) || menu->items_num == 0)
		return;
	first_item = (pos < 0) ? 0 : pos;

	last_item = first_item + menu->visible_items_num - 1;
	if (last_item >= menu->items_num) {
		last_item = menu->items_num - 1;
		first_item = max (0, menu->items_num - menu->visible_items_num);
	}

	for (i = 0; i < first_item; ++i)
		move_astbar (menu->items[i].bar, menu->main_canvas, 0,
								 -menu->item_height);
	for (i = last_item + 1; i < menu->items_num; ++i)
		move_astbar (menu->items[i].bar, menu->main_canvas, 0,
								 -menu->item_height);

	curr_y = 0;
	if (first_item > 0 || last_item < menu->items_num - 1) {
		move_astbar (menu->scroll_up_bar, menu->main_canvas, 0, curr_y);
		curr_y += menu->scroll_bar_size;
	} else {
		move_astbar (menu->scroll_up_bar, menu->main_canvas, 0,
								 -menu->scroll_bar_size);
		move_astbar (menu->scroll_up_bar, menu->main_canvas, 0,
								 -menu->scroll_bar_size);
	}

	for (i = first_item; i <= last_item; ++i) {
		move_astbar (menu->items[i].bar, menu->main_canvas, 0, curr_y);
		curr_y += menu->item_height;
	}
	if (first_item > 0 || last_item < menu->items_num - 1)
		move_astbar (menu->scroll_down_bar, menu->main_canvas, 0, curr_y);

	LOCAL_DEBUG_OUT
			("adj_pos(%d)->curr_y(%d)->items_num(%d)->vis_items_num(%d)->sel_item(%d)",
			 first_item, curr_y, menu->items_num, menu->visible_items_num,
			 menu->selected_item);

	menu->top_item = first_item;

	if (menu->selected_item < menu->top_item)
		select_menu_item (menu, menu->top_item, False);
	else if (menu->selected_item >= 0) {
		if (menu->visible_items_num > 0
				&& menu->selected_item >=
				(int)menu->top_item + (int)menu->visible_items_num)
			select_menu_item (menu, menu->top_item + menu->visible_items_num - 1,
												False);
	}
	render_asmenu_bars (menu, False);
}

static inline void run_item_submenu (ASMenu * menu, int item_no)
{
	ASMenuItem *item = &(menu->items[item_no]);
	LOCAL_DEBUG_CALLER_OUT ("%p, %d", menu, item_no);
	if (!get_flags (item->flags, AS_MenuItemDisabled)) {
		MenuData *submenu = NULL;
		if (item->fdata.func == F_POPUP)
			submenu = FindPopup (FDataPopupName (item->fdata), True);
		else if (item->fdata.func == F_STOPMODULELIST)
			submenu = make_stop_module_menu (Scr.Feel.winlist_sort_order);
		else if (item->fdata.func == F_RESTARTMODULELIST)
			submenu = make_restart_module_menu (Scr.Feel.winlist_sort_order);
#ifndef NO_WINDOWLIST
		else if (item->fdata.func == F_WINDOWLIST) {
			submenu = make_desk_winlist_menu (Scr.Windows,
																				(item->fdata.text == NULL) ?
																				Scr.CurrentDesk : item->fdata.
																				func_val[0],
																				Scr.Feel.winlist_sort_order,
																				False);
		}
#endif
		if (submenu) {
			int x = menu->main_canvas->root_x + (int)menu->main_canvas->bw;
			int y;
			ASQueryPointerRootXY (&x, &y);
#if 0
			if (x < menu->main_canvas->root_x + (int)(menu->item_width / 3)) {
				int max_dx = Scr.MyDisplayWidth / 40;
				if (menu->main_canvas->root_x + (int)(menu->item_width / 3) - x <
						max_dx)
					x = menu->main_canvas->root_x + (int)(menu->item_width / 3);
				else
					x += max_dx;
			}
#endif
			y = menu->main_canvas->root_y +
					(menu->item_height * (item_no - (int)menu->top_item));
			/*  if( x > menu->main_canvas->root_x+menu->item_width-5 )
			   x = menu->main_canvas->root_x+menu->item_width-5 ;
			 */ close_asmenu_submenu (menu);
			menu->submenu = run_submenu (menu, submenu, x, y);
			if (item->fdata.func != F_POPUP) {
				if (menu->submenu == NULL)
					destroy_menu_data (&submenu);
				else
					menu->submenu->volitile_menu_data = submenu;
			}
		}
	}
}

void press_menu_item (ASMenu * menu, int pressed, Bool keyboard_event)
{
	LOCAL_DEBUG_CALLER_OUT ("%p,%d", menu, pressed);

	if (AS_ASSERT (menu) || menu->items_num == 0)
		return;

	if (pressed >= (int)menu->items_num)
		pressed = menu->items_num - 1;

	if (menu->pressed_item >= 0)
		set_astbar_pressed (menu->items[menu->pressed_item].bar,
												menu->main_canvas, False);

	if (pressed >= 0) {
		if (get_flags (menu->items[pressed].flags, AS_MenuItemDisabled))
			pressed = -1;
		else {
			if (keyboard_event) {			/* don't press if keyboard selection */
				if (pressed != menu->selected_item)
					select_menu_item (menu, pressed, False);

			} else if (pressed != menu->selected_item) {
				set_astbar_pressed (menu->items[pressed].bar, NULL, True);	/* don't redraw yet */
				select_menu_item (menu, pressed, False);	/* this one updates display already */
			} else
				set_astbar_pressed (menu->items[pressed].bar, menu->main_canvas,
														True);
			set_menu_item_used (menu, menu->items[pressed].source);

			if (get_flags (menu->items[pressed].flags, AS_MenuItemHasSubmenu)) {
				if (keyboard_event) {
					int x =
							menu->main_canvas->root_x + (int)menu->main_canvas->bw +
							menu->main_canvas->width - (menu->arrow_space +
																					DEFAULT_MENU_SPACING);
					int y = menu->main_canvas->root_y + (int)menu->main_canvas->bw;
					y += menu->items[pressed].bar->win_y +
							menu->items[pressed].bar->height - 5;
					XWarpPointer (dpy, None, Scr.Root, 0, 0, 0, 0, x, y);
				}
				run_item_submenu (menu, pressed);
			}
		}
	}
	render_asmenu_bars (menu, False);
	LOCAL_DEBUG_OUT ("pressed(%d)->old_pressed(%d)->focused(%d)", pressed,
									 menu->pressed_item, get_flags (menu->state,
																									AS_MenuFocused) ? 1 : 0);
	if (get_flags (menu->state, AS_MenuFocused)) {
		int item_no = keyboard_event ? pressed : menu->pressed_item;

		/* if keyboard event we exec item on press event, and on release event otherwise */
		if (item_no >= 0 && (pressed < 0 || keyboard_event)) {
			ASMenuItem *item = &(menu->items[item_no]);
			if (get_flags
					(item->flags,
					 AS_MenuItemDisabled | AS_MenuItemHasSubmenu) == 0) {
				set_menu_item_used (menu, item->source);
				ExecuteFunctionForClient (&(item->fdata), menu->client_window);
				if (!get_flags (menu->state, AS_MenuPinned))
					close_asmenu (&ASTopmostMenu);
			}
		}
	}
	if (keyboard_event)
		menu->pressed_item = -1;
	else
		menu->pressed_item = pressed;
}

/*************************************************************************/
/* Menu event handlers  - ASInternalWindow interface :                   */
/*************************************************************************/
void menu_register_subwindows (struct ASInternalWindow *asiw)
{
	ASMenu *menu = (ASMenu *) (asiw->data);
	if (menu != NULL && menu->magic == MAGIC_ASMENU) {
		register_aswindow (menu->main_canvas->w, asiw->owner);
	}
}

void on_menu_moveresize (ASInternalWindow * asiw, Window w)
{
	ASMenu *menu = (ASMenu *) (asiw->data);
	if (menu != NULL && menu->magic == MAGIC_ASMENU) {	/* handle config change */
		ASFlagType changed = handle_canvas_config (menu->main_canvas);
		register int i = menu->items_num;
		LOCAL_DEBUG_OUT
				("changed(%lX)->main_width(%d)->main_height(%d)->item_height(%d)",
				 changed, menu->main_canvas->width, menu->main_canvas->height,
				 menu->item_height);
		if (get_flags (changed, CANVAS_RESIZED)) {
			if (get_flags (changed, CANVAS_WIDTH_CHANGED)) {
				while (--i >= 0)
					set_astbar_size (menu->items[i].bar, menu->main_canvas->width,
													 menu->item_height);
				set_astbar_size (menu->scroll_up_bar, menu->main_canvas->width,
												 menu->scroll_bar_size);
				set_astbar_size (menu->scroll_down_bar, menu->main_canvas->width,
												 menu->scroll_bar_size);
			}
			if (get_flags (changed, CANVAS_HEIGHT_CHANGED)) {
				menu->visible_items_num =
						menu->main_canvas->height / menu->item_height;
				if (menu->visible_items_num < menu->items_num)
					menu->visible_items_num =
							(menu->main_canvas->height -
							 (menu->scroll_bar_size * 2)) / menu->item_height;
				LOCAL_DEBUG_OUT
						("update_canvas_display via set_asmenu_scroll_position from move_resize %s",
						 "");
				set_asmenu_scroll_position (menu, menu->top_item);
			}
		} else if (get_flags (changed, CANVAS_MOVED)) {
			while (--i >= 0)
				update_astbar_transparency (menu->items[i].bar, menu->main_canvas,
																		False);
			update_astbar_transparency (menu->scroll_up_bar, menu->main_canvas,
																	False);
			update_astbar_transparency (menu->scroll_down_bar, menu->main_canvas,
																	False);
		}
		if (changed != 0)
			render_asmenu_bars (menu, get_flags (changed, CANVAS_RESIZED));
	}
}


/* fwindow looses/gains focus : */
void
on_menu_hilite_changed (ASInternalWindow * asiw, ASMagic * data,
												Bool focused)
{
	ASMenu *menu = (ASMenu *) (asiw->data);
	LOCAL_DEBUG_CALLER_OUT ("%p, %p, %d", asiw, data, focused);
	if (menu != NULL && menu->magic == MAGIC_ASMENU) {
		/* TODO : hilite/unhilite selected item, and
		 * withdraw non-pinned menu if it has no submenus */
		if (focused)
			set_flags (menu->state, AS_MenuFocused);
		else
			clear_flags (menu->state, AS_MenuFocused);
	}
}

/* ButtonPress/Release event on one of the contexts : */
void
on_menu_pressure_changed (ASInternalWindow * asiw, int pressed_context)
{
	ASMenu *menu = (ASMenu *) (asiw->data);
	LOCAL_DEBUG_CALLER_OUT ("%p,%p,0x%X", asiw, menu, pressed_context);
	if (menu != NULL && menu->magic == MAGIC_ASMENU) {
		/* press/depress menu item, possibly change the selection,
		 * and run the function when item is depressed */
		if (pressed_context) {
			int px = 0, py = 0;
			ASQueryPointerWinXY (menu->main_canvas->w, &px, &py);
			LOCAL_DEBUG_OUT ("pointer(%d,%d)", px, py);
			if (px >= 0 && px < menu->main_canvas->width && py >= 0
					&& py < menu->main_canvas->height) {
				int pressed = -1;
				if (menu->visible_items_num < menu->items_num) {
					if (py < menu->scroll_bar_size) {
						set_asmenu_scroll_position (menu, menu->top_item - 1);
					} else if (py > menu->scroll_down_bar->win_y) {
						set_asmenu_scroll_position (menu, menu->top_item + 1);
					} else {
						pressed = (py - menu->scroll_bar_size) / menu->item_height;
					}
					if (pressed < 0)
						return;
				} else
					pressed = py / menu->item_height;
				pressed += menu->top_item;
				if (pressed != menu->pressed_item)
					press_menu_item (menu, pressed, False);
			}
		} else if (menu->pressed_item >= 0) {
			press_menu_item (menu, -1, False);
		}
	}
}

static void
handle_menu_selection (ASMenu * menu, int button_state, int px,
											 int selection, Bool render)
{
	if (selection != menu->selected_item) {
		if (button_state & (Button1Mask | Button2Mask | Button3Mask))
			press_menu_item (menu, selection, False);
		else
			select_menu_item (menu, selection, render);
	}
	if (px >
			menu->main_canvas->width - (menu->arrow_space + DEFAULT_MENU_SPACING)
			&& menu->submenu == NULL)
		run_item_submenu (menu, selection);

}

void on_menu_scroll_event (ASInternalWindow * asiw, ASEvent * event)
{
	ASMenu *menu = (ASMenu *) (asiw->data);
	if (menu != NULL && menu->magic == MAGIC_ASMENU && event) {
		if (menu->items_num >= menu->visible_items_num) {
			XButtonEvent *xbtn = &(event->x.xbutton);
			int selection = menu->selected_item;
			int top = menu->top_item;
			int px_offset =
					menu->main_canvas->root_x + (int)menu->main_canvas->bw;
			int px, py;
			Bool needs_scrolling;

			ASQueryPointerRootXY (&px, &py);
			if (xbtn->button == Button4) {
				if (selection > 0) {
					--selection;
					py -= menu->item_height;
				}
				if (selection < menu->top_item)
					--top;
			} else {
				if (selection + 1 < menu->items_num) {
					++selection;
					py += menu->item_height;
				}
				if (selection > menu->top_item + menu->visible_items_num - 1)
					++top;
			}
			needs_scrolling = (top != menu->top_item);
			if (selection != menu->selected_item) {
				handle_menu_selection (menu, xbtn->state, px - px_offset,
															 selection, !needs_scrolling);
				if (!needs_scrolling)
					XWarpPointer (dpy, None, Scr.Root, 0, 0, 0, 0, px, py);
			}
//      if( needs_scrolling ) 
//        set_asmenu_scroll_position( menu, top );
		}
	}
}

/* Motion notify : */
void on_menu_pointer_event (ASInternalWindow * asiw, ASEvent * event)
{
	ASMenu *menu = (ASMenu *) (asiw->data);
	if (menu != NULL && menu->magic == MAGIC_ASMENU && event) {
		/* change selection and maybe pop a submenu */
		XMotionEvent *xmev = &(event->x.xmotion);
		ASCanvas *canvas = menu->main_canvas;
		int pointer_x = event->x.xmotion.x_root, pointer_y =
				event->x.xmotion.y_root;
		int px, py;
		XEvent tmp_e;
		if (ASCheckTypedWindowEvent (canvas->w, LeaveNotify, &tmp_e)) {
			if (menu->comment_balloon)
				withdraw_balloon (menu->comment_balloon);
			XPutBackEvent (dpy, &tmp_e);
			return;										/* pointer has moved into other window - ignore this event! */
		}
		if (menu->comment_balloon
				&& !get_flags (menu->state, AS_MenuBalloonShown)) {
			set_flags (menu->state, AS_MenuBalloonShown);
			display_balloon (menu->comment_balloon);
		}
		/* must get current pointer position as MotionNotify events
		   tend to accumulate while we are drawing and we start getting
		   late with menu selection, creating an illusion of slowness */
		ASQueryPointerRootXY (&pointer_x, &pointer_y);
		px = pointer_x - (canvas->root_x + (int)canvas->bw);
		py = pointer_y - (canvas->root_y + (int)canvas->bw);

		if (px >= 0 && px < canvas->width && py >= 0 && py < canvas->height) {
			int selection = -1;
			if (menu->items_num > menu->visible_items_num) {
				Bool render = False;
				if (py < menu->scroll_bar_size) {
					if (set_astbar_focused
							(menu->scroll_up_bar, menu->main_canvas, True))
						render = True;
					if (set_astbar_focused
							(menu->scroll_down_bar, menu->main_canvas, False))
						render = True;
				} else {
					if (set_astbar_focused
							(menu->scroll_up_bar, menu->main_canvas, False))
						render = True;
					if (py > menu->scroll_down_bar->win_y) {
						if (set_astbar_focused
								(menu->scroll_down_bar, menu->main_canvas, True))
							render = True;
					} else {
						if (set_astbar_focused
								(menu->scroll_down_bar, menu->main_canvas, False))
							render = True;
						else
							selection = (py - menu->scroll_bar_size) / menu->item_height;
					}
				}
				if (selection < 0) {
					if (render)
						render_asmenu_bars (menu, False);
					return;
				}
			} else
				selection = py / menu->item_height;
			selection += menu->top_item;

			handle_menu_selection (menu, xmev->state, px, selection, True);
		}
	}
}

void menu_destroy (ASInternalWindow * asiw);

/* KeyPress/Release : */
void on_menu_keyboard_event (ASInternalWindow * asiw, ASEvent * event)
{
	ASMenu *menu = (ASMenu *) (asiw->data);
	if (menu != NULL && menu->magic == MAGIC_ASMENU) {
		KeySym keysym = XLookupKeysym (&(event->x.xkey), 0);

		if (keysym == XK_Escape ||
				keysym == XK_BackSpace ||
				keysym == XK_Delete || (keysym == XK_Left && menu->supermenu)) {
			if (menu->supermenu != NULL) {
				ASMenu *sm = menu->supermenu;
				int x = sm->main_canvas->root_x + sm->main_canvas->bw;
				int y = sm->main_canvas->root_y + sm->main_canvas->bw;
				if (sm->selected_item >= 0 && sm->selected_item < sm->items_num) {
					ASMenuItem *item = &(sm->items[sm->selected_item]);
					x += item->bar->win_x + item->bar->width - (menu->arrow_space +
																											DEFAULT_MENU_SPACING);
					y += item->bar->win_y + item->bar->height / 2;
				}
				XWarpPointer (dpy, None, Scr.Root, 0, 0, 0, 0, x, y);
			}
			close_asmenu (&menu);
/*            menu_destroy( asiw ); */
			return;
		} else if ((keysym >= XK_A && keysym <= XK_Z) ||	/* Only consider alphabetic */
							 (keysym >= XK_a && keysym <= XK_z) || (keysym >= XK_0 && keysym <= XK_9)) {	/* ...or numeric keys     */
			int i;
			if (islower (keysym))
				keysym = toupper (keysym);
			LOCAL_DEBUG_OUT ("processing keysym [%c]", (char)keysym);
			/* Search menu for matching hotkey */
			for (i = 0; i < menu->items_num; i++)
				if (menu->items[i].fdata.hotkey == keysym) {
					press_menu_item (menu, i, True);
					return;
				}
			for (i = 0; i < menu->items_num; i++) {
				if (menu->items[i].first_sym == keysym) {
					press_menu_item (menu, i, True);
					return;
				}
			}
		} else {
			switch (keysym) {
			case XK_Page_Up:
				break;
			case XK_Page_Down:
				break;
			case XK_Up:
			case XK_k:
			case XK_p:
				if (menu->selected_item > 0)
					select_menu_item (menu, menu->selected_item - 1, True);
				break;
			case XK_Tab:
			case XK_Down:
			case XK_n:
			case XK_j:
				if (menu->selected_item < menu->items_num)
					select_menu_item (menu, menu->selected_item + 1, True);
				break;
			case XK_Right:
			case XK_Return:
			case XK_space:
				if (menu->selected_item >= 0) {
					if (get_flags
							(menu->items[menu->selected_item].flags,
							 AS_MenuItemHasSubmenu) || keysym != XK_Right)
						press_menu_item (menu, menu->selected_item, True);
				}
			}
		}
	}
}

/* reconfiguration : */

void
on_menu_look_feel_changed (ASInternalWindow * asiw, ASFeel * feel,
													 MyLook * look, ASFlagType what)
{
	ASMenu *menu = (ASMenu *) (asiw->data);
	if (menu != NULL && menu->magic == MAGIC_ASMENU) {
		ASHints *hints;
		unsigned int tbar_width;
		MenuData *md;

		/*      if( get_flags( what, FEEL_CONFIG ) )       */
		if (menu->items) {
			register int i = menu->items_num;
			while (--i >= 0)
				free_asmenu_item (&(menu->items[i]));
			free (menu->items);
			menu->items = NULL;
			menu->items_num = 0;
		}

		md = FindPopup (menu->name, False);
		if (md == NULL) {
			menu_destroy (asiw);
			return;
		}
		set_asmenu_data (menu, md, False,
										 get_flags (look->flags, MenuShowUnavailable),
										 feel->recent_submenu_items);
		set_asmenu_look (menu, look);
		hints = make_menu_hints (menu);
		estimate_titlebar_size (hints, &tbar_width, NULL);
		destroy_hints (hints, False);

		if (tbar_width > MAX_MENU_WIDTH)
			tbar_width = MAX_MENU_WIDTH;
		if (tbar_width > menu->optimal_width) {
			int i = menu->items_num;
			menu->optimal_width = tbar_width;
			LOCAL_DEBUG_OUT ("menu_tbar_width = %d - resizing items!",
											 tbar_width);
			resize_canvas (menu->main_canvas, tbar_width, menu->optimal_height);
			while (--i >= 0)
				set_astbar_size (menu->items[i].bar, tbar_width,
												 menu->item_height);
		}
		if (menu->items && menu->items[0].bar)
			set_astbar_focused (menu->items[0].bar, menu->main_canvas, False);
		menu->selected_item = -1;
		set_asmenu_scroll_position (menu, 0);

//        render_asmenu_bars(menu);
	}
}

void on_menu_root_background_changed (ASInternalWindow * asiw)
{
	ASMenu *menu = (ASMenu *) (asiw->data);
	if (menu != NULL && menu->magic == MAGIC_ASMENU) {	/* update transparency here */
		register int i = menu->items_num;
		while (--i >= 0)
			update_astbar_transparency (menu->items[i].bar, menu->main_canvas,
																	True);
		update_astbar_transparency (menu->scroll_up_bar, menu->main_canvas,
																True);
		update_astbar_transparency (menu->scroll_down_bar, menu->main_canvas,
																True);
		render_asmenu_bars (menu, False);
	}
}

/* destruction */
void menu_destroy (ASInternalWindow * asiw)
{
	destroy_asmenu ((ASMenu **) & (asiw->data));
}
