#ifndef DESKTOP_CATEGORY_INTERNAL_H
#define DESKTOP_CATEGORY_INTERNAL_H

/* Shared between desktop_category.c and desktop_category_entries.c.
 * These are hash callbacks defined in desktop_category_entries.c and
 * registered/used from the category-tree code in desktop_category.c.
 * They are not part of the public desktop_category.h surface. */

/* From desktop_category_entries.c */
void desktop_entry_destroy (ASHashableValue value, void *data);
void desktop_entry_print (ASHashableValue value, void *data);

#endif /* DESKTOP_CATEGORY_INTERNAL_H */
