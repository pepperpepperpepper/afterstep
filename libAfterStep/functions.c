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

#define LOCAL_DEBUG
#include "../configure.h"
#include "asapp.h"
#include "session.h"
#include "afterstep.h"
#include "screen.h"
#include "parser.h"
#include "functions.h"
#include "freestor.h"
#include "../libAfterImage/afterimage.h"


FunctionCode change_func_code (const char *func_name,
															 FunctionCode new_code)
{
	TermDef *fterm;
	if (IsValidFunc (new_code)
			&& (fterm = txt2fterm (func_name, True)) != NULL)
		fterm->id = new_code;
}

/*************************************************************************/
/* parsing code :
 */
TermDef *txt2fterm (const char *txt, int quiet)
{
	TermDef *fterm;

	if (pFuncSyntax->term_hash == NULL)
		PrepareSyntax ((SyntaxDef *) pFuncSyntax);
	if ((fterm =
			 FindStatementTerm ((char *)txt, (SyntaxDef *) pFuncSyntax)) == NULL
			&& !quiet)
		show_error ("unknown function name in function specification [%s].\n",
								txt);

	return fterm;
}

int txt2func_code (const char *text)
{
	TermDef *fterm;

	for (; isspace (*text); text++) ;
	fterm = txt2fterm (text, True);
	return (fterm != NULL) ? fterm->id : F_FUNCTIONS_NUM;
}

int txt2func (const char *text, FunctionData * fdata, int quiet)
{
	TermDef *fterm;

	for (; isspace (*text); text++) ;
	fterm = txt2fterm (text, quiet);
	if (fterm != NULL) {
		init_func_data (fdata);
		fdata->func = fterm->id;
		for (; !isspace (*text) && *text; text++) ;
		for (; isspace (*text); text++) ;
		if (*text) {
			const char *ptr = text + strlen ((char *)text);

			for (; isspace (*(ptr - 1)); ptr--) ;
			fdata->text = mystrndup (text, ptr - text);
		}
	}
	return (fterm != NULL);
}

int parse_func (const char *text, FunctionData * data, int quiet)
{
	TermDef *fterm;
	char *ptr;
	int curr_arg = 0;
	int sign = 0;

	init_func_data (data);
	for (ptr = (char *)text; isspace (*ptr); ptr++) ;
	if (*ptr == '\0') {
		if (!quiet)
			show_error ("empty function specification encountered.%s");
		return -1;
	}

	if ((fterm = txt2fterm (ptr, quiet)) == NULL)
		return -2;

	if (IsInternFunc (fterm->id))
		return 0;

	while (!isspace (*ptr) && *ptr)
		ptr++;
	data->func = fterm->id;
	if (fterm->flags & TF_SYNTAX_TERMINATOR)
		return 0;

	set_func_val (data, -1, default_func_val (data->func));

	/* now let's do actual parsing */
	if (!(fterm->flags & NEED_CMD))
		ptr = stripcomments (ptr);
	else {												/* we still want to strip trailing whitespaces */
		char *tail = ptr + strlen (ptr) - 1;

		for (; isspace (*tail) && tail > ptr; tail--) ;
		*(tail + 1) = '\0';
	}
	/* this function is very often called so we try to use as little
	   calls to other function as possible */
	for (; *ptr; ptr++) {
		if (!isspace (*ptr)) {
			int is_text = 0;

			if (*ptr == '"') {
				char *tail = ptr;
				char *text;

				while (*(tail + 1) && *(tail + 1) != '"')
					tail++;
				if (*(tail + 1) == '\0') {
					show_error ("impaired doublequotes encountered in [%s].", ptr);
					return -3;
				}
				text = mystrndup (ptr + 1, (tail - ptr));
				if (data->name == NULL)
					data->name = text;
				else if (data->text == NULL)
					data->text = text;
				ptr = tail + 1;
			} else if (isdigit (*ptr)) {
				int count;
				char unit = '\0';
				int val = 0;

				for (count = 1; isdigit (*(ptr + count)); count++) ;
				if (*(ptr + count) != '\0' && !isspace (*(ptr + count)))
					is_text = (!isspace (*(ptr + count + 1))
										 && *(ptr + count + 1) != '\0') ? 1 : 0;
				if (is_text == 0)
					ptr = parse_func_args (ptr, &unit, &val) - 1;
				if (curr_arg < MAX_FUNC_ARGS) {
					data->func_val[curr_arg] = (sign != 0) ? val * sign : val;
					data->unit[curr_arg] = unit;
					curr_arg++;
				}
			} else if (*ptr == '-') {
				if (sign == 0) {
					sign--;
					continue;
				} else
					is_text = 1;
			} else if (*ptr == '+') {
				if (sign == 0) {
					sign++;
					continue;
				} else
					is_text = 1;
			} else
				is_text = 1;

			if (is_text) {
				if (sign != 0)
					ptr--;
				if (data->text == NULL) {
					if (fterm->flags & NEED_CMD) {
						data->text = mystrdup (ptr);
						break;
					}
					ptr = parse_token (ptr, &(data->text)) - 1;
				} else
					while (*(ptr + 1) && !isspace (*(ptr + 1)))
						ptr++;
			}
			sign = 0;
		}
	}

	decode_func_units (data);
	data->hotkey = scan_for_hotkey (data->name);

	/* now let's check for valid number of arguments */
	if ((fterm->flags & NEED_NAME) && data->name == NULL) {
		show_error ("function specification requires \"name\" in [%s].", text);
		return FUNC_ERR_NO_NAME;
	}
	if (data->text == NULL) {
		if ((fterm->flags & NEED_WINDOW)
				|| ((fterm->flags & NEED_WINIFNAME) && data->name != NULL)) {
			show_error ("function specification requires window name in [%s].",
									text);
			return FUNC_ERR_NO_TEXT;
		}
		if (fterm->flags & NEED_CMD) {
			show_error
					("function specification requires shell command or full file name in [%s].",
					 text);
			return FUNC_ERR_NO_TEXT;
		}
	}
/*
   if( data->func == F_SCROLL )
   {
   fprintf( stderr,"Function parsed: [%s] [%s] [%s] [%d] [%d] [%c]\n",fterm->keyword,data->name,data->text,data->func_val[0], data->func_val[1],data->hotkey );
   fprintf( stderr,"from: [%s]\n", text );
   }
 */
	return 0;
}

#if 0
FunctionData *String2Func (const char *string, FunctionData * p_fdata,
													 Bool quiet)
{
	if (p_fdata)
		free_func_data (p_fdata);
	else
		p_fdata = safecalloc (1, sizeof (FunctionData));

	LOCAL_DEBUG_OUT ("parsing message \"%s\"", string);
	if (parse_func (string, p_fdata, quiet) < 0) {
		LOCAL_DEBUG_OUT ("parsing failed%s", "");
		free_func_data (p_fdata);
		free (p_fdata);
		p_fdata = NULL;
	} else
		LOCAL_DEBUG_OUT ("parsing success with func = %d", p_fdata->func);
	return p_fdata;
}

#endif
/****************************************************************************/
/* FunctionData - related code 	                                            */
void init_func_data (FunctionData * data)
{
	int i;

	if (data) {
		data->func = F_NOP;
		for (i = 0; i < MAX_FUNC_ARGS; i++) {
			data->func_val[i] = DEFAULT_OTHERS;
			data->unit_val[i] = 0;
			data->unit[i] = '\0';
		}
		data->hotkey = '\0';
		data->name = data->text = NULL;
		data->name_encoding = 0;
		data->popup = NULL;
	}
}

void copy_func_data (FunctionData * dst, FunctionData * src)
{
	if (dst && src) {
		register int i;

		dst->func = src->func;
		dst->name = src->name;
		dst->name_encoding = src->name_encoding;
		dst->text = src->text;
		for (i = 0; i < MAX_FUNC_ARGS; i++) {
			dst->func_val[i] = src->func_val[i];
			dst->unit[i] = src->unit[i];
			dst->unit_val[i] = src->unit_val[i];
		}
		dst->hotkey = src->hotkey;
		dst->popup = src->popup;
	}
}

void dup_func_data (FunctionData * dst, FunctionData * src)
{
	if (dst && src) {
		register int i;

		dst->func = src->func;
		dst->name = mystrdup (src->name);
		dst->text = mystrdup (src->text);
		for (i = 0; i < MAX_FUNC_ARGS; i++) {
			dst->func_val[i] = src->func_val[i];
			dst->unit[i] = src->unit[i];
			dst->unit_val[i] = src->unit_val[i];
		}
		dst->hotkey = src->hotkey;
		dst->popup = src->popup;
	}
}

extern FunctionData *create_named_function (int func, char *name)
{
	FunctionData *fdata = safecalloc (1, sizeof (FunctionData));

	init_func_data (fdata);
	fdata->func = func;
	if (name)
		fdata->name = mystrdup (name);
	return fdata;
}



void set_func_val (FunctionData * data, int arg, int value)
{
	int i;

	if (arg >= 0 && arg < MAX_FUNC_ARGS)
		data->func_val[arg] = value;
	else
		for (i = 0; i < MAX_FUNC_ARGS; i++)
			data->func_val[i] = value;
}

int free_func_data (FunctionData * data)
{
	if (data) {
#ifdef DEBUG_ALLOCS
		LOCAL_DEBUG_OUT ("freeing func = \"%s\"",
										 data->name ? data->name : "(null)");
#endif
		if (data->name) {
			free (data->name);
			data->name = NULL;
		}
		if (data->text) {
#ifdef DEBUG_ALLOCS
			LOCAL_DEBUG_OUT ("func->text = \"%s\"", data->text);
#endif
			free (data->text);
			data->text = NULL;
		}
		return data->func;
	}
	return F_NOP;
}

void destroy_func_data (FunctionData ** pdata)
{
	if (pdata && *pdata) {
		free_func_data (*pdata);
		free (*pdata);
		*pdata = NULL;
	}
}

long default_func_val (FunctionCode func)
{
	long val = 0;

	switch (func) {
	case F_MAXIMIZE:
		val = DEFAULT_MAXIMIZE;
		break;
	case F_MOVE:
	case F_RESIZE:
		val = INVALID_POSITION;
		break;
	default:
		break;
	}
	return val;
}

void decode_func_units (FunctionData * data)
{
	register int i;

	for (i = 0; i < MAX_FUNC_ARGS; i++)
		switch (data->unit[i]) {
		case 'p':
		case 'P':
			data->unit_val[i] = 1;
			break;
		default:
			data->unit_val[i] = 0 /*defaults[i] */ ;
		}
}

