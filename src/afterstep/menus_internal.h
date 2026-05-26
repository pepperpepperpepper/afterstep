#ifndef AFTERSTEP_MENUS_INTERNAL_H
#define AFTERSTEP_MENUS_INTERNAL_H

#include "asinternals.h"

#define FDataPopupName(fdata)   ((fdata).text)

/* Shared internal state for split translation units. */
extern ASMenu *ASTopmostMenu;

/* menus_core.c */
ASMenu *create_asmenu (const char *name);
void free_asmenu_item (ASMenuItem * item);
void destroy_asmenu (ASMenu ** pmenu);
void close_asmenu (ASMenu ** pmenu);
void close_asmenu_submenu (ASMenu * menu);
void set_asmenu_data (ASMenu * menu, MenuData * md, Bool first_time,
											Bool show_unavailable, int recent_items);

/* menus_render.c */
void set_asmenu_look (ASMenu * menu, MyLook * look);
void render_asmenu_bars (ASMenu * menu, Bool force);

/* menus_events.c */
void menu_register_subwindows (struct ASInternalWindow *asiw);
void on_menu_moveresize (ASInternalWindow * asiw, Window w);
void on_menu_hilite_changed (ASInternalWindow * asiw, ASMagic * data,
														 Bool focused);
void on_menu_pressure_changed (ASInternalWindow * asiw,
															 int pressed_context);
void on_menu_scroll_event (ASInternalWindow * asiw, ASEvent * event);
void on_menu_pointer_event (ASInternalWindow * asiw, ASEvent * event);
void on_menu_keyboard_event (ASInternalWindow * asiw, ASEvent * event);
void on_menu_look_feel_changed (ASInternalWindow * asiw, ASFeel * feel,
																MyLook * look, ASFlagType what);
void on_menu_root_background_changed (ASInternalWindow * asiw);
void menu_destroy (ASInternalWindow * asiw);

/* menus_run.c */
ASHints *make_menu_hints (ASMenu * menu);

#endif /* AFTERSTEP_MENUS_INTERNAL_H */
