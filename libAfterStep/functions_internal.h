#ifndef FUNCTIONS_INTERNAL_H
#define FUNCTIONS_INTERNAL_H

/* Internal (non-public) surface of functions_terms.c, split out of
 * functions.c. These have external linkage but are not part of the
 * public functions.h API; include this only where needed. The parse
 * half (functions.c) and the terms half communicate otherwise solely
 * through the public functions.h declarations. */

/* From functions_terms.c */
void free_minipixmap_data (MinipixmapData * minipixmap);
void assign_minipixmaps (MenuDataItem * mdi, MinipixmapData * minipixmaps);
MenuDataItem *add_menu_data_item (MenuData * menu, int func, char *name,
																	MinipixmapData * minipixmaps);
void reload_menuitem_pmap (MenuDataItem * mdi, MinipixmapTypes type,
													 Bool force);

#endif /* FUNCTIONS_INTERNAL_H */
