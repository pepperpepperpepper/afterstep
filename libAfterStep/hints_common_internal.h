#ifndef HINTS_COMMON_INTERNAL_H
#define HINTS_COMMON_INTERNAL_H

/* Shared private symbols for the hints_common.c <-> hints_common_io.c split.
 * Neither is part of the public hints.h API:
 *   pointer_name_to_index_in_list - name-table lookup defined (de-static'd) in
 *     hints_common.c, used by serialize_names in hints_common_io.c.
 *   gravitate_position - gravity helper defined in hints_common.c, called by
 *     the client geometry-string builder in hints_common_io.c (it is a global
 *     function but was never declared in any header). */

int pointer_name_to_index_in_list (char **list, char *name);
int gravitate_position (int pos, unsigned int size, unsigned int scr_size,
												int grav, unsigned int bw);

#endif /* HINTS_COMMON_INTERNAL_H */
