/*
 * Copyright (c) 2000 Andrew Ferguson <andrew@owsla.cjb.net>
 * Copyright (c) 1998 Sasha Vasko <sasha at aftercode.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#undef DO_CLOCKING
#undef UNKNOWN_KEYWORD_WARNING
#define LOCAL_DEBUG

#include "../configure.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>

#ifdef DO_CLOCKING
#if TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif
#endif

#include "asapp.h"
#include "afterstep.h"
#include "parser.h"
#include "screen.h"
#include "freestor.h"
#include "functions.h"
#include "../libAfterImage/asimagexml.h"

extern char *_disabled_keyword;
#include "freestor_internal.h"

/* Config-writing helpers: the *2FreeStorage converters and the format_AS*
 * helpers, split out of freestor.c. Uses the shared FindTerm /
 * AddFreeStorageElem_sa and the context tables via freestor_internal.h. */

/* helper functions for writing config */
FreeStorageElem **Integer2FreeStorage (SyntaxDef * syntax,
																			 FreeStorageElem ** tail, int *index,
																			 int value, int id)
{
	FreeStorageElem *new_elem = NULL;
	char *str_v = NULL, *str_i = NULL;

	if (index)
		new_elem = AddFreeStorageElem (syntax, tail, NULL, id,
																	 (str_i =
																		string_from_int (*index)), (str_v =
																																string_from_int
																																(value)),
																	 NULL);
	else
		new_elem =
				AddFreeStorageElem (syntax, tail, NULL, id,
														(str_v = string_from_int (value)), NULL);

	if (new_elem)
		tail = &(new_elem->next);

	destroy_string (&str_i);
	destroy_string (&str_v);

	return tail;
}

FreeStorageElem **Flag2FreeStorage (SyntaxDef * syntax,
																		FreeStorageElem ** tail, int id)
{
	FreeStorageElem *new_elem =
			AddFreeStorageElem (syntax, tail, NULL, id, NULL);

	if (new_elem)
		tail = &(new_elem->next);

	return tail;
}


FreeStorageElem **Flags2FreeStorage (SyntaxDef * syntax,
																		 FreeStorageElem ** tail,
																		 flag_options_xref * xref,
																		 unsigned long set_flags,
																		 unsigned long flags)
{
	if (xref) {
		while (xref->flag) {
			if (get_flags (set_flags, xref->flag))
				tail =
						Flag2FreeStorage (syntax, tail,
															get_flags (flags,
																				 xref->flag) ? xref->
															id_on : xref->id_off);

			xref++;
		}
	}
	return tail;
}


FreeStorageElem **Strings2FreeStorage (SyntaxDef * syntax,
																			 FreeStorageElem ** tail,
																			 char **strings, unsigned int num,
																			 int id)
{
	FreeStorageElem *new_elem;

	if (num == 0 || strings == NULL)
		return tail;
	if ((new_elem =
			 AddFreeStorageElem_sa (syntax, tail, NULL, id, strings,
															num)) != NULL)
		tail = &(new_elem->next);
	return tail;
}


FreeStorageElem **QuotedString2FreeStorage (SyntaxDef * syntax,
																						FreeStorageElem ** tail,
																						int *index, char *string,
																						int id)
{
	FreeStorageElem *new_elem = NULL;
	char *str_v = string, *str_i = NULL;

	if (string == NULL)
		return tail;

	str_v = quote_str (string);

	if (index)
		new_elem =
				AddFreeStorageElem (syntax, tail, NULL, id,
														(str_i =
														 string_from_int (*index)), str_v, NULL);
	else
		new_elem = AddFreeStorageElem (syntax, tail, NULL, id, str_v, NULL);

	destroy_string (&str_i);
	destroy_string (&str_v);

	if (new_elem)
		tail = &(new_elem->next);

	return tail;
}

FreeStorageElem **Geometry2FreeStorage (SyntaxDef * syntax,
																				FreeStorageElem ** tail,
																				ASGeometry * geometry, int id)
{
	char *geom_string;
	FreeStorageElem *new_elem;

	if (geometry == NULL)
		return tail;

	geom_string =
			format_geometry (geometry->x, geometry->y, geometry->width,
											 geometry->height, geometry->flags);

	if ((new_elem =
			 AddFreeStorageElem (syntax, tail, NULL, id, geom_string,
													 NULL)) != NULL)
		tail = &(new_elem->next);
	free (geom_string);
	return tail;
}

FreeStorageElem **StringArray2FreeStorage (SyntaxDef * syntax,
																					 FreeStorageElem ** tail,
																					 char **strings, int index1,
																					 int index2, int id,
																					 char *iformat)
{
	FreeStorageElem *new_elem = NULL;
	int i;
	TermDef *pterm = FindTerm (syntax, TT_ANY, id);

	if (strings && pterm)
		for (i = 0; i < index2 - index1 + 1; i++)
			if (strings[i]) {
				char *str_i = string_from_int (i + index1);

				if ((new_elem =
						 AddFreeStorageElem (syntax, tail, pterm, id, str_i,
																 strings[i], NULL)) != NULL)
					tail = &(new_elem->next);
				free (str_i);
			}
	return tail;
}

FreeStorageElem **Path2FreeStorage (SyntaxDef * syntax,
																		FreeStorageElem ** tail, int *index,
																		char *path, int id)
{
	if (path) {
		register char *ptr;

		for (ptr = path; *ptr; ptr++)
			if (isspace ((int)*ptr))
				return QuotedString2FreeStorage (syntax, tail, index, path, id);
	}
	if (index)
		return StringArray2FreeStorage (syntax, tail, &path, *index, *index,
																		id, NULL);
	return Strings2FreeStorage (syntax, tail, &path, 1, id);
}



FreeStorageElem **ASButton2FreeStorage (SyntaxDef * syntax,
																				FreeStorageElem ** tail, int index,
																				ASButton * b, int id)
{
	return tail;
}


char *format_ASBox (ASBox * box)
{
	char buffer[256];
	int pos = 0;
	int val_pos;

#define FORMAT_BOX_VALUE(val,neg_flag) \
		do{ buffer[pos++] = (get_flags(box->flags,(neg_flag)) || (val) < 0)?'-':'+'; \
		val_pos = unsigned_int2buffer_end (&buffer[pos], sizeof(buffer)-pos, (val) < 0? -(val) : (val)); \
		while (buffer[val_pos])	buffer[pos++] = buffer[val_pos++];}while(0)

	if (get_flags (box->flags, LeftValue)) {
		FORMAT_BOX_VALUE (box->left, LeftNegative);
		if (get_flags (box->flags, TopValue)) {
			buffer[pos++] = ' ';
			FORMAT_BOX_VALUE (box->top, TopNegative);
			if (get_flags (box->flags, RightValue)) {
				buffer[pos++] = ' ';
				FORMAT_BOX_VALUE (box->right, RightNegative);
				if (get_flags (box->flags, BottomValue)) {
					buffer[pos++] = ' ';
					FORMAT_BOX_VALUE (box->bottom, BottomNegative);
				}
			}
		}
	}
#undef FORMAT_BOX_VALUE
	buffer[pos++] = '\0';
	return strdup (buffer);
}

char *format_ASButton (ASButton * button)
{
	return NULL;
}

char *format_ASCursor (ASCursor * cursor)
{
	return NULL;
}

FreeStorageElem **Box2FreeStorage (SyntaxDef * syntax,
																	 FreeStorageElem ** tail, ASBox * box,
																	 int id)
{
	if (box) {
		char *str_v = format_ASBox (box);
		FreeStorageElem *new_elem =
				AddFreeStorageElem (syntax, tail, NULL, id, str_v, NULL);

		if (new_elem != NULL)
			tail = &(new_elem->next);;
		free (str_v);
	}
	return tail;
}

FreeStorageElem **Binding2FreeStorage (SyntaxDef * syntax,
																			 FreeStorageElem ** tail, char *sym,
																			 int context, int mods, int id)
{
	char *strings[3];
	char context_string[sizeof (int) * 8 + 1 + 1];
	char mods_string[sizeof (int) * 8 + 1 + 1];

	if (sym) {
		strings[0] = sym;
		memset (context_string, ' ', sizeof (int) * 8 + 1);
		context_string[sizeof (int) * 8 + 1] = '\0';
		memset (mods_string, ' ', sizeof (int) * 8 + 1);
		mods_string[sizeof (int) * 8 + 1] = '\0';
		if (context == C_ALL) {
			context_string[0] = 'A';
			context = 0;
		}
		context2string (&(context_string[0]), context, as_contexts, True);

		strings[1] = &(context_string[0]);
		if (mods == 0) {
			mods_string[0] = 'N';
		} else if (mods == AnyModifier) {
			mods_string[0] = 'A';
			mods = 0;
		}
		context2string (&(mods_string[0]), mods, key_modifiers, True);
		strings[2] = &(mods_string[0]);

		tail = Strings2FreeStorage (syntax, tail, strings, 3, id);
	}
	return tail;
}

FreeStorageElem **ASCursor2FreeStorage (SyntaxDef * syntax,
																				FreeStorageElem ** tail, int index,
																				ASCursor * c, int id)
{
	TermDef *pterm = FindTerm (syntax, TT_ANY, id);
	char *ind_str;

	if (pterm && c) {
		if (c->image_file == NULL || c->mask_file == NULL)
			return tail;

		ind_str = string_from_int (index);

		if (ind_str != NULL) {
			FreeStorageElem *new_elem = NULL;

			if ((new_elem = AddFreeStorageElem (syntax, tail, pterm, id, ind_str,
																					c->image_file, c->mask_file,
																					NULL)) != NULL) {
				tail = &(new_elem->next);
			}
			free (ind_str);
		}
	}
	return tail;
}

FreeStorageElem **Bitlist2FreeStorage (SyntaxDef * syntax,
																			 FreeStorageElem ** tail, long bits,
																			 int id)
{
	/* TODO */
	return NULL;
}

void init_asgeometry (ASGeometry * geometry)
{
	geometry->flags = XValue | YValue;
	geometry->x = geometry->y = 0;
	geometry->width = geometry->height = 1;
}

FreeStorageElem *CompositeFlags2FreeStorage (ASFlagType flags,
																						 SyntaxDef * syntax)
{
	FreeStorageElem *storage = NULL;
	FreeStorageElem **tail = &storage;

	if (syntax && flags != 0) {
		int i;

		for (i = 0; syntax->terms[i].keyword; ++i) {
			TermDef *T = &(syntax->terms[i]);

			if (get_flags (flags, T->flags_on)
					&& !get_flags (flags, T->flags_off)) {
				FreeStorageElem *new_elem =
						AddFreeStorageElem (syntax, tail, T, T->id, NULL);

				if (new_elem)
					tail = &(new_elem->next);
			}
		}
	}

	return storage;
}

FreeStorageElem *StructFlags2FreeStorage (void *struct_ptr,
																					ptrdiff_t
																					default_set_flags_offset,
																					SyntaxDef * syntax,
																					flag_options_xref * xref,
																					ASFlagType * handled_return)
{
	FreeStorageElem *storage = NULL;
	FreeStorageElem **tail = &storage;
	ASFlagType handled = 0;

	if (struct_ptr == NULL || syntax == NULL || xref == NULL)
		return NULL;

	while (xref->flag != 0) {
		unsigned long *flags =
				(xref->flag_field_offset >
				 0) ? struct_ptr + xref->flag_field_offset : NULL;
		unsigned long *set_flags =
				(xref->set_flag_field_offset >
				 0) ? struct_ptr +
				xref->set_flag_field_offset : (default_set_flags_offset >
																			 0) ? struct_ptr +
				default_set_flags_offset : NULL;

		if (set_flags == NULL || get_flags (*set_flags, xref->flag)) {
			if (flags && get_flags (*flags, xref->flag) == xref->flag) {
				FreeStorageElem *new_elem =
						AddFreeStorageElem (syntax, tail, NULL, xref->id_on, NULL);

				if (new_elem) {
					tail = &(new_elem->next);
					set_flags (handled, xref->flag);
				}
			}
			/* TODO some handling of the flags that are off ? */
		}
		xref++;
	}

	if (handled_return)
		*handled_return = handled;

	return storage;
}

FreeStorageElem *StructToFreeStorage (void *struct_ptr,
																			ptrdiff_t set_flags_offset,
																			SyntaxDef * syntax,
																			ASFlagType * handled_return)
{
	FreeStorageElem *storage = NULL;
	FreeStorageElem **tail = &storage;
	ASFlagType set_flags = *((ASFlagType *) (struct_ptr + set_flags_offset));
	ASFlagType handled = 0;
	int i;

	if (struct_ptr == NULL || syntax == NULL)
		return NULL;

	for (i = 0; syntax->terms[i].id; ++i) {
		TermDef *T = &(syntax->terms[i]);
		void *data_ptr = struct_ptr + T->struct_field_offset;
		char *val_str = NULL;

		if (T->struct_field_offset == 0 || get_flags (T->flags, TF_INDEXED)
				|| !get_flags (set_flags, T->flags_on)
				|| get_flags (set_flags, T->flags_off))
			continue;

		if (T->type == TT_FLAG) {
			if (T->sub_syntax) {
				*tail =
						CompositeFlags2FreeStorage (*((ASFlagType *) data_ptr),
																				T->sub_syntax);
				if (*tail)
					tail = &((*tail)->next);
				set_flags (handled, T->flags_on);
				continue;
			}
		}

		switch (T->type) {
		case TT_FLAG:
			val_str = string_from_int (*((Bool *) data_ptr) ? 1 : 0);
			break;
		case TT_INTEGER:
			val_str = string_from_int (*((int *)data_ptr));
			break;
		case TT_UINTEGER:
			val_str = string_from_int (*((unsigned int *)data_ptr));
			break;
		case TT_BITLIST:
			val_str = string_from_int (*((int *)data_ptr));
			break;
		case TT_OPTIONAL_PATHNAME:
		case TT_COLOR:
		case TT_FONT:
		case TT_FILENAME:
		case TT_TEXT:
		case TT_PATHNAME:
			val_str = mystrdup (*((char **)data_ptr));
			break;
		case TT_QUOTED_TEXT:
			val_str = quote_str (*((char **)data_ptr));
			break;
		case TT_BOX:
			val_str = format_ASBox (((ASBox *) data_ptr));
			break;
		case TT_GEOMETRY:
			{
				ASGeometry *g = (ASGeometry *) data_ptr;

				val_str =
						format_geometry (g->x, g->y, g->width, g->height, g->flags);
			}
			break;
		case TT_FUNCTION:
			val_str = format_FunctionData (*((FunctionData **) data_ptr));
			break;
		case TT_BUTTON:
			val_str = format_ASButton (*((ASButton **) data_ptr));
			break;
		case TT_CURSOR:
			val_str = format_ASCursor (*((ASCursor **) data_ptr));
			break;

		case TT_INTARRAY:
		case TT_BINDING:
			break;

		default:;
		}

		if (val_str) {
			FreeStorageElem *new_elem =
					AddFreeStorageElem (syntax, tail, T, T->id, val_str, NULL);

			if (new_elem) {
				set_flags (handled, T->flags_on);
				tail = &(new_elem->next);
			}
			free (val_str);
		}

	}

	if (handled_return)
		*handled_return = handled;

	return storage;
}



/* Note: Func2FreeStorage has been moved into libASConfig as it is too
 * dependant on FuncSyntax, defined there
 */
