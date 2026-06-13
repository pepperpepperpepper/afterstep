#ifndef MYSTYLE_INTERNAL_H
#define MYSTYLE_INTERNAL_H

/* Shared between the mystyle.c family (mystyle.c / mystyle_core.c /
 * mystyle_parse.c). DefaultMyStyleName is the fallback style name: it is
 * defined in mystyle.c and read by the list lookup code in mystyle_core.c.
 * It is not part of the public mystyle.h API. */

extern char *DefaultMyStyleName;

#endif /* MYSTYLE_INTERNAL_H */
