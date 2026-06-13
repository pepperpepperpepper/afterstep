/*
 * Copyright (c) 2002 Sasha Vasko <sasha@aftercode.net>
 * Copyright (c) 1998, 1999 Ethan Fischer <allanon@crystaltokyo.com>
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

#include "../configure.h"

#undef LOCAL_DEBUG
#include "asapp.h"
#include "afterstep.h"
#include "parser.h"
#include "mystyle.h"
#include "screen.h"
#include "../libAfterImage/afterimage.h"
#include "mystyle_internal.h"

/* MyStyle creation/deletion, list lifecycle and lookups, split out of
 * mystyle.c. Uses the shared DefaultMyStyleName (mystyle_internal.h). */

/*************************************************************************/
/* Mystyle creation/deletion                                             */
/*************************************************************************/
static void mystyle_free_resources (MyStyle * style)
{
	if (style->magic == MAGIC_MYSTYLE) {
		LOCAL_DEBUG_OUT
				("style %p, style->name = \"%s\", style->font->name = \"%s\"",
				 style, style->name ? style->name : "(null)",
				 style->font.name ? style->font.name : "(null)");

		if (get_flags (style->user_flags, F_FONT)) {
			unload_font (&style->font);
		}
		if (style->user_flags & F_BACKGRADIENT) {
			free (style->gradient.color);
			free (style->gradient.offset);
		}
		if (!get_flags (style->inherit_flags, F_BACKTRANSPIXMAP)) {
			LOCAL_DEBUG_OUT ("calling mystyle_free_back_icon for style %p",
											 style);
			mystyle_free_back_icon (style);
		}
	}
}

void mystyle_init (MyStyle * style)
{
	style->user_flags = 0;
	style->inherit_flags = 0;
	style->set_flags = 0;
	style->flags = 0;
	style->name = NULL;
	style->text_style = 0;
	style->font.name = NULL;
	style->font.as_font = NULL;
	style->colors.fore = ARGB32_White;
	style->colors.back = ARGB32_Black;
	style->relief.fore = style->colors.back;
	style->relief.back = style->colors.fore;
	style->texture_type = 0;
	style->gradient.npoints = 0;
	style->gradient.color = NULL;
	style->gradient.offset = NULL;
	style->back_icon.pix = None;
	style->back_icon.mask = None;
	style->back_icon.alpha = None;
	style->tint = TINT_LEAVE_SAME;
	style->back_icon.image = NULL;
}


void mystyle_destroy (ASHashableValue value, void *data)
{
	if ((char *)value != NULL) {
/*	fprintf( stderr, "destroying mystyle [%s]\n", value.string_val ); */
		free ((char *)value);				/* destroying our name */
	}
	if (data != NULL) {
		MyStyle *style = (MyStyle *) data;

		mystyle_free_resources (style);
		style->magic = 0;						/* invalidating memory block */
		free (data);
	}
}

ASHashTable *mystyle_list_init ()
{
	ASHashTable *list = NULL;

	list =
			create_ashash (0, casestring_hash_value, casestring_compare,
										 mystyle_destroy);

	return list;
}

MyStyle *mystyle_list_new (struct ASHashTable * list, char *name)
{
	MyStyle *style = NULL;
	ASHashData hdata = { 0 };

	if (name == NULL)
		return NULL;

	if (list == NULL) {
		if (ASDefaultScr->Look.styles_list == NULL)
			if ((ASDefaultScr->Look.styles_list = mystyle_list_init ()) == NULL)
				return NULL;
		list = ASDefaultScr->Look.styles_list;
	}

	if (get_hash_item (list, AS_HASHABLE (name), &hdata.vptr) == ASH_Success) {
		if ((style = hdata.vptr) != NULL) {
			if (style->magic == MAGIC_MYSTYLE)
				return style;
			else
				remove_hash_item (list, (ASHashableValue) name, NULL, True);
		}
	}
	style = (MyStyle *) safecalloc (1, sizeof (MyStyle));

	mystyle_init (style);
	style->name = mystrdup (name);

	if (add_hash_item (list, AS_HASHABLE (style->name), style) != ASH_Success) {	/* something terrible has happen */
		if (style->name)
			free (style->name);
		free (style);
		return NULL;
	}
	style->magic = MAGIC_MYSTYLE;
	return style;
}

MyStyle *mystyle_new_with_name (char *name)
{
	if (name == NULL)
		return NULL;
	return mystyle_list_new (NULL, name);
}


/* destruction of all mystyle records : */
void mystyle_list_destroy_all (ASHashTable ** plist)
{
	if (plist == NULL)
		plist = &(ASDefaultScr->Look.styles_list);
	destroy_ashash (plist);
}

void mystyle_destroy_all ()
{
	mystyle_list_destroy_all (NULL);
}


/*
 * MyStyle Lookup functions :
 */
MyStyle *mystyle_list_find (struct ASHashTable *list, const char *name)
{
	ASHashData hdata = { 0 };
	if (list == NULL)
		list = ASDefaultScr->Look.styles_list;

	if (list && name)
		if (get_hash_item (list, AS_HASHABLE ((char *)name), &hdata.vptr) !=
				ASH_Success)
			hdata.vptr = NULL;
	return hdata.vptr;
}

MyStyle *mystyle_list_find_or_default (struct ASHashTable * list,
																			 const char *name)
{
	ASHashData hdata = { 0 };

	if (list == NULL)
		list = ASDefaultScr->Look.styles_list;

	if (name == NULL)
		name = DefaultMyStyleName;
	if (list && name)
		if (get_hash_item (list, AS_HASHABLE ((char *)name), &hdata.vptr) !=
				ASH_Success)
			if (get_hash_item
					(list, AS_HASHABLE (DefaultMyStyleName),
					 &hdata.vptr) != ASH_Success)
				hdata.vptr = NULL;
	return hdata.vptr;
}

/* find a style by name */
MyStyle *mystyle_find (const char *name)
{
	return mystyle_list_find (NULL, name);
}

/* find a style by name or return the default style */
MyStyle *mystyle_find_or_default (const char *name)
{
	return mystyle_list_find_or_default (NULL, name);
}

