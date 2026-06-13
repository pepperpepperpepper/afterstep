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

/* Statement decoding: args2FreeStorage, the free_storage2* converters,
 * context/modifier parsing and the ReadConfigItem/ReadFlagItem readers,
 * split out of freestor.c. The charkey_xref type and the context tables
 * are shared with the writers via freestor_internal.h. */

/* create and format freestorage elem from the statement found */
void args2FreeStorage (FreeStorageElem * pelem, char *data, int data_len)
{
	int argc = 0, max_argc = 0;
	char **argv = NULL;

	if (data_len > 0) {
		register char *cur, *dst;

		if (!(pelem->term->flags & TF_DONT_SPLIT)) {
			cur = data;
			while (*cur) {
				if (!isspace ((int)*cur))
					max_argc++;
				while (!isspace ((int)*cur) && *cur)
					cur++;
				while (isspace ((int)*cur) && *cur)
					cur++;
			}
		} else {
			max_argc++;
			if (get_flags
					(pelem->term->flags, (TF_INDEXED | TF_DIRECTION_INDEXED)))
				max_argc++;
		}

		if (max_argc > 0) {
			argv = CreateStringArray (max_argc);
			dst = argv[0] = (char *)safemalloc (data_len + 1);
			cur = data;
			max_argc--;
			while (argc < max_argc) {
				dst = argv[argc];
				while (!isspace ((int)*cur) && *cur) {
					if (*cur == '"') {
						register char *ptr = find_doublequotes (cur);

						if (ptr)
							while (cur != ptr)
								*(dst++) = *(cur++);
					}
					*(dst++) = *(cur++);
				}
				if (dst <= argv[argc])
					break;

				*(dst++) = '\0';
				if (++argc <= max_argc)
					argv[argc] = dst;

				while (isspace ((int)*cur) && *cur)
					cur++;
			}
			if (*cur && argc <= max_argc)
				strcpy (argv[argc++], cur);
		}
	}
	pelem->argc = argc;
	pelem->argv = argv;
}

/****************************************************************************/
/* F2D: FreeStorage to Datatype conversion routines :			    */
/****************************************************************************/
/* free storage post processing code */
FunctionData *free_storage2func (FreeStorageElem * stored, int *ppos)
{
	register struct FunctionData *pfunc;
	int pos = *ppos;

	if (stored->term == NULL)
		return NULL;

	pfunc =
			(struct FunctionData *)safecalloc (1, sizeof (struct FunctionData));

	pfunc->func = stored->term->id;

	/* setting up reasonable defaults */
	set_func_val (pfunc, -1, default_func_val (pfunc->func));
	LOCAL_DEBUG_OUT ("argv=%p, arc=%d, sub=%p", stored->argv, stored->argc,
									 stored->sub);
	if (stored->argv == NULL)
		return pfunc;

	/* first checking if we have doublequoted name */
	if (pos < stored->argc && *(stored->argv[pos]) == '"') {
		register int len = strlen (stored->argv[pos]);

		if (len > 2) {							/* we want to strip off those doubleqoutes */
			pfunc->name = mystrndup (stored->argv[pos] + 1, len - 2);
			pfunc->hotkey = scan_for_hotkey (pfunc->name);
		}
		/* otherwise - empty name, but we should still skip it */
		++pos;
	}
	/* now storing the rest of parameters as text */
	if (pos < stored->argc && stored->argv[pos]) {
		pfunc->text =
				CompressStringArray (stored->argc - pos, &(stored->argv[pos]));
	}
	while (pos < stored->argc && stored->argv[pos]) {
		register char *cur = stored->argv[pos];

		if (isdigit ((int)*cur)
				|| ((*cur == '-' || *cur == '+') && isdigit ((int)*(cur + 1))))
			break;
		pos++;
	}

	if (pos < stored->argc && stored->argv[pos]) {
		parse_func_args (stored->argv[pos], &(pfunc->unit[0]),
										 (int *)&(pfunc->func_val[0]));
		pos++;
	}
	if (pos < stored->argc && stored->argv[pos])
		parse_func_args (stored->argv[pos], &(pfunc->unit[1]),
										 (int *)&(pfunc->func_val[1]));

	decode_func_units (pfunc);

	*ppos = pos;
	return pfunc;
}

/* END of FunctionData - related code                                        */

int *free_storage2int_array (FreeStorageElem * stored, int *ppos)
{
	int pos = *ppos;
	int count = 0;
	int *array = NULL;

	if (stored) {
		if (pos < stored->argc) {
			array = safemalloc (sizeof (int) * (stored->argc - pos));

			do {
				array[count++] = atoi (stored->argv[pos]);
			}
			while (++pos < stored->argc);
		}
		*ppos = pos;
	}
	return array;
}

char *free_storage2quoted_text (FreeStorageElem * stored, int *ppos)
{
	char *ptr = stored->argv[*ppos], *tail;
	char *result = NULL;

	if (*ptr != '"')
		ptr = find_doublequotes (ptr);
	if (ptr == NULL && get_flags (stored->term->flags, TF_QUOTES_OPTIONAL)) {
		result = mystrdup (stored->argv[*ppos]);
		(*ppos)++;
	} else if (ptr) {
		ptr++;
		if ((tail = find_doublequotes (ptr)) == NULL) {
			show_error ("terminating quote missing in [%s]! Using whole line !",
									stored->argv[*ppos]);
			result = mystrdup (ptr);
		} else
			result = mystrndup (ptr, tail - ptr);
		/* stripping those backslashed doubleqoutes off of the text */
		for (ptr = result + 1; *ptr; ptr++)
			if (*ptr == '"' && *(ptr - 1) == '\\')
				strcpy (ptr - 1, ptr);
		(*ppos)++;
	} else
		show_error
				("bad quoted string [%s] (ppos == %d) for option [%s]. Ignoring!",
				 ptr ? ptr : "(null)", *ppos, stored->term->keyword);

	return result;
}

Bool free_storage2box (FreeStorageElem * stored, int *ppos, ASBox * pbox)
{
	register int i = 0;
	int *data[4];
	int flags_list[4] = { LeftValue, TopValue, RightValue, BottomValue };
	int sign_flags_list[4] =
			{ LeftNegative, TopNegative, RightNegative, BottomNegative };

	if (pbox != NULL && ppos != NULL && stored != NULL) {
		data[0] = &(pbox->left);
		data[1] = &(pbox->top);
		data[2] = &(pbox->right);
		data[3] = &(pbox->bottom);
		pbox->flags = 0;
		for (; *ppos < stored->argc && i < 4; (*ppos)++, i++) {
			*(data[i]) = atoi (stored->argv[*ppos]);
			pbox->flags |= flags_list[i];
			if (*(data[i]) < 0 || *(stored->argv[*ppos]) == '-')
				pbox->flags |= sign_flags_list[i];
		}
	}
	return (i == 0) ? False : True;
}

Bool
free_storage2geometry (FreeStorageElem * stored, int *ppos,
											 ASGeometry * pgeometry)
{
	char *geom = stored->argv[(*ppos)++];

	if (pgeometry == NULL || geom == NULL)
		return False;
	memset (pgeometry, 0x00, sizeof (ASGeometry));
	parse_geometry (geom,
									&(pgeometry->x), &(pgeometry->y), &(pgeometry->width),
									&(pgeometry->height), &(pgeometry->flags));
	return True;
}

ASButton *free_storage2button (FreeStorageElem * stored, int *ppos)
{
	register ASButton *pbutton = NULL;
	register int s;

	pbutton = safecalloc (1, sizeof (ASButton));
	for (s = 0; s < ASB_StateCount && *ppos < stored->argc; ++s) {
		pbutton->shapes[s] = mystrdup (stored->argv[(*ppos)++]);
	}
	return pbutton;
}




/* The keys must be in lower case! */
#ifdef C_TITLEBAR
charkey_xref as_contexts[] = {
	{'1', C_TButton1},
	{'2', C_TButton2},
	{'3', C_TButton3},
	{'4', C_TButton4},
	{'5', C_TButton5},
	{'6', C_TButton6},
	{'7', C_TButton7},
	{'8', C_TButton8},
	{'9', C_TButton9},
	{'0', C_TButton0},
	{'B', C_TITLEBAR},
	{'C', C_FRAME_N},
	{'D', C_FRAME_E},
	{'E', C_FRAME_W},
	{'G', C_FRAME_NW},
	{'H', C_FRAME_NE},
	{'J', C_FRAME_SW},
	{'I', C_ICON},
	{'K', C_FRAME_SE},
	{'N', C_NAME},
	{'O', C_ICON_NAME},
	{'P', C_CLASS_NAME},
	{'Q', C_RES_NAME},
	{'R', C_ROOT},
	{'S', C_FRAME_S},
	{'W', C_WINDOW},
	{'F', C_FRAME},
	{'T', C_TITLE},
	{'A', C_ALL},
	{0, 0}
};
#else
struct charkey_xref as_contexts[] = {
	{'W', C_WINDOW},
	{'T', C_TITLE},
	{'I', C_ICON},
	{'R', C_ROOT},
	{'F', C_FRAME},
	{'S', C_SIDEBAR},
	{'1', C_TButton1},
	{'2', C_TButton2},
	{'3', C_TButton3},
	{'4', C_TButton4},
	{'5', C_TButton5},
	{'6', C_TButton6},
	{'7', C_TButton7},
	{'8', C_TButton8},
	{'9', C_TButton9},
	{'0', C_TButton0},
	{'A',
	 C_WINDOW | C_TITLE | C_ICON | C_ROOT | C_FRAME | C_SIDEBAR |
	 C_TButtonAll},
	{0, 0}
};

#endif

/* The keys musat be in lower case! */
charkey_xref key_modifiers[] = {
	{'S', ShiftMask},
	{'C', ControlMask},
	{'M', Mod1Mask},
	{'1', Mod1Mask},
	{'2', Mod2Mask},
	{'3', Mod3Mask},
	{'4', Mod4Mask},
	{'5', Mod5Mask},
	{'A', AnyModifier},
	{'N', 0},
	{0, 0}
};

/*
 * Turns a  string context of context or modifier values into an array of
 * true/false values (bits)
 * returns pointer to the fisrt character after context specification
 */
static void
string2context (char *string, int *output, charkey_xref * table)
{
	register int i, j;
	int tmp1;

	*output = 0;
	for (i = 0; string[i]; i++) {
		/* in some BSD implementations, tolower(c) is not defined
		 * unless isupper(c) is true */
		tmp1 = string[i];
		if (islower (tmp1))
			tmp1 = toupper (tmp1);
		/* end of ugly BSD patch */
		for (j = 0; table[j].key != 0; j++)
			if (tmp1 == table[j].key) {
				*output |= table[j].value;
				break;
			}
		if (table[j].key == 0)
			show_error ("undefined context [%c] requested in [%s].", string[i],
									string);
	}
}

/*
 * Turns a  string context of context or modifier values into an array of
 * true/false values (bits)
 * returns pointer to the fisrt character after context specification
 */

char *parse_context (char *string, int *output, charkey_xref * table)
{
	register int j;
	register char *ptr;
	char tmp1;

	*output = 0;
	for (ptr = string; isspace (*ptr); ptr++) ;
	for (; *ptr && !isspace (*ptr); ptr++) {
		/* in some BSD implementations, tolower(c) is not defined
		 * unless isupper(c) is true */
		tmp1 = *ptr;
		if (islower (tmp1))
			tmp1 = toupper (tmp1);
		/* end of ugly BSD patch */
		for (j = 0; table[j].key != 0; j++)
			if (tmp1 == table[j].key) {
				*output |= table[j].value;
				break;
			}
		if (table[j].key == 0) {
			show_error ("undefined context specifyer [%c] encountered in [%s].",
									*ptr, string);
		}
	}
	return ptr;
}

int parse_modifier (char *tline)
{
	int mods = 0;

	parse_context (tline, &mods, key_modifiers);
	return mods;
}

int parse_win_context (char *tline)
{
	int ctxs = 0;

	parse_context (tline, &ctxs, as_contexts);
	LOCAL_DEBUG_OUT ("tline = \"%s\", ctxs = %X", tline, ctxs);
	return ctxs;
}

char *parse_modifier_str (char *tline, int *mods_ret)
{
	int mods = 0;

	LOCAL_DEBUG_OUT ("tline = \"%s\"", tline);
	tline = parse_context (tline, &mods, key_modifiers);
	LOCAL_DEBUG_OUT ("tline = \"%s\", mods = %X", tline, mods);
	*mods_ret = mods;
	return tline;
}

char *parse_win_context_str (char *tline, int *ctxs_ret)
{
	int ctxs = 0;

	LOCAL_DEBUG_OUT ("tline = \"%s\"", tline);
	tline = parse_context (tline, &ctxs, as_contexts);
	*ctxs_ret = ctxs;
	LOCAL_DEBUG_OUT ("tline = \"%s\", ctxs = %X", tline, ctxs);
	return tline;
}

void
context2string (char *string, int input, charkey_xref * table,
								Bool terminate)
{
	register int i, j = 0;

	i = 0;
	if (input != 0 || !terminate)
		for (j = 0; table[j].key != 'A'; j++)
			if (input & table[j].value)
				string[i++] = table[j].key;
	if (terminate) {
		while (string[i] != '\0' && !isspace ((int)string[i]))
			i++;
		string[i] = '\0';
	} else
		string[j] = '\0';
}

char *free_storage2binding (FreeStorageElem * stored, int *ppos,
														int *context, int *mods)
{
	char *sym = NULL;

	if (*ppos < stored->argc)
		sym = mystrdup (stored->argv[(*ppos)++]);
	if (sym) {
		if (*ppos < stored->argc && context)
			string2context (stored->argv[(*ppos)++], context, as_contexts);
		if (*ppos < stored->argc && mods)
			string2context (stored->argv[(*ppos)++], mods, key_modifiers);
	}
	return sym;
}

ASCursor *free_storage2cursor (FreeStorageElem * stored, int *ppos)
{
	register ASCursor *pcursor = safecalloc (1, sizeof (ASCursor));

	if (*ppos < stored->argc)
		pcursor->image_file = mystrdup (stored->argv[(*ppos)++]);
	if (*ppos < stored->argc)
		pcursor->mask_file = mystrdup (stored->argv[(*ppos)++]);
	return pcursor;
}

long free_storage2bitlist (FreeStorageElem * stored, int *ppos)
{
	long bits = 0;
	int bit_num;
	Bool valid;
	register int i;
	register char *ptr;

	if (*ppos == stored->argc)		/* by default - everything is set */
		return 0xFFFFFFFF;

	for (i = *ppos; i < stored->argc; i++) {
		ptr = stored->argv[i];
		while (*ptr) {
			while (*ptr && !isdigit ((int)*ptr))
				ptr++;
			bit_num = 0;
			valid = False;
			while (*ptr && isdigit ((int)*ptr)) {
				valid = True;
				bit_num = bit_num * 10 + ((*ptr) - '0');
				ptr++;
			}
			if (valid && bit_num < sizeof (long) * 8)
				bits |= (0x01 << bit_num);
		}
	}

	return bits;
}

#ifndef ASCURSOR_H_HEADER_INCLUDED
void destroy_ascursor (ASCursor ** cursor)
{
	if (cursor)
		if (*cursor) {
			register ASCursor *c = *cursor;

			if (c->image_file)
				free (c->image_file);
			if (c->mask_file)
				free (c->mask_file);
			free (*cursor);
			*cursor = NULL;
		}
}

#endif
static void FreeConfigItem (ConfigItem * item)
{
	switch (item->type) {
	case TT_INTARRAY:
		item->data.int_array.array = NULL;
		item->data.int_array.size = 0;
	case TT_BINDING:
		item->data.binding.sym = NULL;
	case TT_COLOR:
	case TT_FONT:
	case TT_FILENAME:
	case TT_TEXT:
	case TT_PATHNAME:
	case TT_QUOTED_TEXT:
	case TT_OPTIONAL_PATHNAME:
		item->data.string = NULL;
		if (item->memory)
			free (item->memory);
		break;

	case TT_FUNCTION:
		free_func_data (item->data.function);
		free (item->data.function);
		item->data.function = NULL;
		break;

	case TT_BUTTON:
		destroy_asbutton (item->data.button, False);
		break;
	case TT_CURSOR:
		destroy_ascursor (&(item->data.cursor));
		break;
	}
	item->memory = NULL;
}

static Bool check_avail_args (FreeStorageElem * stored, int pos, int need)
{
	if (stored->argv == NULL || stored->argc < pos + need)
		return False;
	while (--need >= 0)
		if (stored->argv[pos + need] == NULL)
			return False;
	return True;
}

int direction2index (const char *ptr)
{
	int index = 0;

	/* custom code to parse frame sides very fast : */

	if (ptr[0] == 'W' || ptr[0] == 'w')
		index = FR_W;
	else if (ptr[0] == 'E' || ptr[0] == 'e')
		index = FR_E;
	else if (ptr[0] == 'N' || ptr[0] == 'n') {
		if (ptr[1] == 'W' || ptr[1] == 'w')
			index = FR_NW;
		else if (ptr[1] == 'E' || ptr[1] == 'e')
			index = FR_NE;
		else {
			register int i = 0;

			while (i < 5 && ptr[i] != '\0')
				++i;
			if (ptr[i] == 'W' || ptr[i] == 'w')
				index = FR_NW;
			else if (ptr[i] == 'E' || ptr[i] == 'e')
				index = FR_NE;
			else
				index = FR_N;
		}
	} else if (ptr[0] == 'S' || ptr[0] == 's') {
		if (ptr[1] == 'W' || ptr[1] == 'w')
			index = FR_SW;
		else if (ptr[1] == 'E' || ptr[1] == 'e')
			index = FR_SE;
		else {
			register int i = 0;

			while (i < 5 && ptr[i] != '\0')
				++i;
			if (ptr[i] == 'W' || ptr[i] == 'w')
				index = FR_SW;
			else if (ptr[i] == 'E' || ptr[i] == 'e')
				index = FR_SE;
			else
				index = FR_S;
		}
	}
	return index;
}

int ReadConfigItem (ConfigItem * item, FreeStorageElem * stored)
{
	if (item->memory && item->ok_to_free)
		FreeConfigItem (item);

	if (stored) {
		int pos = 0;
		int ret_code = 0;

		item->memory = NULL;
		item->ok_to_free = 0;
		item->type = stored->term->type;
		if (item->type == TT_FLAG) {
			item->data.flag = True;
			ret_code = 1;
		}

		if (get_flags (stored->term->flags, TF_INDEXED)) {
			if (!check_avail_args (stored, pos, 1))
				return ret_code;
			item->index = atoi (stored->argv[pos++]);
		} else if (get_flags (stored->term->flags, TF_DIRECTION_INDEXED)) {
			if (!check_avail_args (stored, pos, 1))
				return ret_code;
			item->index = direction2index (stored->argv[pos++]);
		}
		switch (item->type) {
		case TT_FLAG:
			if (check_avail_args (stored, pos, 1))
				if (atol (stored->argv[pos++]) <= 0)
					item->data.flag = False;
			break;
		case TT_INTEGER:
		case TT_UINTEGER:
			if (!check_avail_args (stored, pos, 1))
				return 0;
			item->data.integer = parse_math (stored->argv[pos], NULL, 0);
			++pos;
			if (stored->term->type == TT_UINTEGER && item->data.integer < 0) {
				show_warning
						("negative value %ld assigned to %s, that accepts only positive numbers! Ignoring!\n",
						 item->data.integer, stored->term->keyword);
				return 0;
			}
			break;
		case TT_BITLIST:
			item->data.integer = free_storage2bitlist (stored, &pos);
			break;
		case TT_OPTIONAL_PATHNAME:
			if (!check_avail_args (stored, pos, 1)) {
				item->data.string = NULL;
				return 1;
			}
		case TT_COLOR:
		case TT_FONT:
		case TT_FILENAME:
		case TT_TEXT:
		case TT_PATHNAME:
			if (!check_avail_args (stored, pos, 1))
				return 0;
			if (stored->term->type != TT_TEXT && *(stored->argv[pos]) == '"') {
				item->memory = item->data.string =
						stripcpy2 (stored->argv[pos], 0);
			} else {
				item->memory = safemalloc (strlen (stored->argv[pos]) + 1);
				item->data.string = (char *)(item->memory);
				strcpy (item->data.string, stored->argv[pos]);
			}
			++pos;
			break;
		case TT_QUOTED_TEXT:
			if (!check_avail_args (stored, pos, 1))
				return 0;
			item->data.string = free_storage2quoted_text (stored, &pos);
			item->memory = (void *)item->data.string;
			break;
		case TT_BOX:
			if (!check_avail_args (stored, pos, 1))
				return 0;
			if (!free_storage2box (stored, &pos, &(item->data.box)))
				return 0;
			break;
		case TT_GEOMETRY:
			if (!check_avail_args (stored, pos, 1))
				return 0;
			if (!free_storage2geometry (stored, &pos, &(item->data.geometry)))
				return 0;
			break;
		case TT_FUNCTION:
			item->data.function = free_storage2func (stored, &pos);
			item->memory = (void *)item->data.function;
			break;
		case TT_BUTTON:
			item->data.button = free_storage2button (stored, &pos);
			item->memory = (void *)item->data.button;
			break;
		case TT_INTARRAY:
			{
				item->data.int_array.size = pos;
				item->data.int_array.array = free_storage2int_array (stored, &pos);
				item->data.int_array.size = pos - item->data.int_array.size;
				item->memory = (void *)item->data.int_array.array;
				break;
			}
		case TT_BINDING:
			item->data.binding.sym =
					free_storage2binding (stored, &pos,
																&(item->data.binding.context),
																&(item->data.binding.mods));
			item->memory = (void *)item->data.binding.sym;
			break;
		case TT_CURSOR:
			item->data.cursor = free_storage2cursor (stored, &pos);
			item->memory = (void *)item->data.cursor;
			break;
		}
		return 1;
	}
	return 0;
}

Bool
ReadCompositeFlagsConfigItem (void *struct_ptr,
															ptrdiff_t flags_field_offset,
															FreeStorageElem * stored)
{
	if (struct_ptr && stored && flags_field_offset != 0) {
		FreeStorageElem *pCurr = stored->sub;
		ASFlagType result = 0;

		while (pCurr) {
			set_flags (result, pCurr->term->flags_on);
			clear_flags (result, pCurr->term->flags_off);
			pCurr = pCurr->next;
		}
		*((ASFlagType *) (struct_ptr + flags_field_offset)) = result;
		return True;
	}
	return False;
}


Bool
ReadConfigItemToStruct (void *struct_ptr, ptrdiff_t set_flags_offset,
												FreeStorageElem * stored)
{
	void *data_ptr = struct_ptr;
	Bool handled = False;

	if (data_ptr == NULL || stored == NULL)
		return False;

	if (stored->term->type == TT_FLAG && stored->term->sub_syntax) {
		handled =
				ReadCompositeFlagsConfigItem (struct_ptr,
																			stored->term->struct_field_offset,
																			stored);
	} else if (stored->term->struct_field_offset != 0) {
		int index = 0;
		ConfigItem item;

		data_ptr += stored->term->struct_field_offset;

		item.memory = NULL;

		if (!ReadConfigItem (&item, stored))
			return False;

		if (get_flags (stored->term->flags, TF_INDEXED))
			index = item.index;

		handled = True;

		switch (item.type) {
		case TT_FLAG:
			((Bool *) data_ptr)[index] = item.data.flag;
			break;
		case TT_INTEGER:
			((int *)data_ptr)[index] = item.data.integer;
			break;
		case TT_UINTEGER:
			((unsigned int *)data_ptr)[index] = item.data.integer;
			break;
		case TT_BITLIST:
			((int *)data_ptr)[index] = item.data.integer;
			break;
		case TT_OPTIONAL_PATHNAME:
		case TT_COLOR:
		case TT_FONT:
		case TT_FILENAME:
		case TT_TEXT:
		case TT_PATHNAME:
		case TT_QUOTED_TEXT:
			set_string (&(((char **)data_ptr)[index]), item.data.string);
			item.memory = NULL;
			break;
		case TT_BOX:
			((ASBox *) data_ptr)[index] = item.data.box;
			break;
		case TT_GEOMETRY:
			((ASGeometry *) data_ptr)[index] = item.data.geometry;
			break;
		case TT_FUNCTION:
			((FunctionData **) data_ptr)[index] = item.data.function;
			item.memory = NULL;
			break;
		case TT_BUTTON:
			((ASButton **) data_ptr)[index] = item.data.button;
			item.memory = NULL;
			break;
		case TT_CURSOR:
			((ASCursor **) data_ptr)[index] = item.data.cursor;
			item.memory = NULL;
			break;

		case TT_INTARRAY:
		case TT_BINDING:
			handled = False;
			break;

		default:
			handled = False;
		}
		ReadConfigItem (&item, NULL);
	}
/* fprintf( stderr, "handled = %d, set_flags_offset = %d, field_offset = %d\n", handled, set_flags_offset, stored->term->struct_field_offset); */
	if (handled && set_flags_offset > 0) {
		ASFlagType *set_flags_ptr = struct_ptr + set_flags_offset;

		set_flags (*set_flags_ptr, stored->term->flags_on);
		clear_flags (*set_flags_ptr, stored->term->flags_off);
	}

	return handled;
}

int
ReadFlagItem (unsigned long *set_flags, unsigned long *flags,
							FreeStorageElem * stored, flag_options_xref * xref)
{
	if (stored && xref) {
		Bool value = True;

		if (stored->term->type != TT_FLAG ||
				get_flags (stored->term->flags,
									 (TF_INDEXED | TF_DIRECTION_INDEXED))
				|| stored->sub != NULL)
			return 0;

		if (stored->argc >= 1)
			value = (atol (stored->argv[0]) > 0);

		while (xref->flag != 0) {
			if (xref->id_on == stored->term->id
					|| xref->id_off == stored->term->id) {
				if (set_flags)
					set_flags (*set_flags, xref->flag);
				if (flags) {
					if ((value && xref->id_on == stored->term->id)
							|| (!value && xref->id_off == stored->term->id))
						set_flags (*flags, xref->flag);
					else
						clear_flags (*flags, xref->flag);
				}
			}
			xref++;
		}
		return 1;
	}
	return 0;
}

int
ReadFlagItemAuto (void *config_struct, ptrdiff_t default_set_flags_offset,
									FreeStorageElem * stored, flag_options_xref * xref)
{
	if (stored && xref) {
		Bool value = True;

		if (stored->term->type != TT_FLAG ||
				get_flags (stored->term->flags,
									 (TF_INDEXED | TF_DIRECTION_INDEXED))
				|| stored->sub != NULL)
			return 0;

		if (stored->argc >= 1)
			value = (atol (stored->argv[0]) > 0);

		while (xref->flag != 0) {
			if (xref->id_on == stored->term->id
					|| xref->id_off == stored->term->id) {
				unsigned long *flags =
						(xref->flag_field_offset >
						 0) ? config_struct + xref->flag_field_offset : NULL;
				unsigned long *set_flags =
						(xref->set_flag_field_offset >
						 0) ? config_struct +
						xref->set_flag_field_offset : (default_set_flags_offset >
																					 0) ? config_struct +
						default_set_flags_offset : NULL;

				if (set_flags)
					set_flags (*set_flags, xref->flag);
				if (flags) {
					if ((value && xref->id_on == stored->term->id)
							|| (!value && xref->id_off == stored->term->id))
						set_flags (*flags, xref->flag);
					else
						clear_flags (*flags, xref->flag);
				}
			}
			xref++;
		}
		return 1;
	}
	return 0;
}


/****************************************************************************/
/* D2F:  Datatype to FreeStorage conversion routines :			    */
/****************************************************************************/


