/*
 * Copyright (c) 2000 Sasha Vasko <sashav@sprintmail.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

Bool bookmark_aswindow (ASWindow * asw, char *bookmark)
{
	Bool success = False;
	if (bookmark) {
		remove_hash_item (Scr.Windows->bookmarks, AS_HASHABLE (bookmark), NULL,
											False);
		LOCAL_DEBUG_OUT ("Bookmark \"%s\" cleared", bookmark);
		if (asw) {
			ASHashData hd;
			char *tmp = mystrdup (bookmark);
			hd.c32 = asw->w;
			success =
					(add_hash_item
					 (Scr.Windows->bookmarks, AS_HASHABLE (tmp),
						hd.vptr) == ASH_Success);
			if (!success)
				free (tmp);
			LOCAL_DEBUG_OUT
					("Added Bookmark for window %p, ID=%8.8lX, -> \"%s\"", asw,
					 asw->w, bookmark);
		}
	}
	return success;
}


ASWindow *bookmark2ASWindow (const char *bookmark)
{
	ASWindow *asw = NULL;
	Bool success = False;
	ASHashData hd;
	hd.c32 = None;
	if (bookmark) {
		if (get_hash_item
				(Scr.Windows->bookmarks, AS_HASHABLE (bookmark),
				 &(hd.vptr)) == ASH_Success) {
			success = True;
			asw = window2ASWindow (hd.c32);
		}
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
		print_ashash (Scr.Windows->bookmarks, string_print);
#endif
	}
	LOCAL_DEBUG_OUT ("Window %p, ID=%8.8lX, %sfetched for bookmark \"%s\"",
									 asw, hd.c32, success ? "" : "not ", bookmark);
	return asw;
}

ASWindow *pattern2ASWindow (const char *pattern)
{
	ASWindow *asw = bookmark2ASWindow (pattern);
	if (asw == NULL) {
		wild_reg_exp *wrexp = compile_wild_reg_exp (pattern);
		if (wrexp != NULL) {
			ASBiDirElem *e = LIST_START (Scr.Windows->clients);
			while (e != NULL) {
				asw = (ASWindow *) LISTELEM_DATA (e);
				if (match_string_list (asw->hints->names, MAX_WINDOW_NAMES, wrexp)
						== 0)
					break;
				else
					asw = NULL;
				LIST_GOTO_NEXT (e);
			}
		}
		destroy_wild_reg_exp (wrexp);
	}
	return asw;
}

char *parse_semicolon_token (char *src, char *dst, int *len)
{
	int i = 0;
	while (*src != '\0') {
		if (*src == ':') {
			if (*(src + 1) == ':')
				++src;
			else
				break;
		}
		dst[i] = *src;
		++i;
		++src;
	}
	dst[i] = '\0';
	*len = i;
	return (*src == ':') ? src + 1 : src;
}

ASWindow *complex_pattern2ASWindow (char *pattern)
{
	ASWindow *asw = NULL;
	/* format :   [<res_class>]:[<res_name>]:[[#<seq_no>]|<name>]  */
	LOCAL_DEBUG_OUT ("looking for window matchng pattern \"%s\"", pattern);
	if (pattern && pattern[0]) {
		wild_reg_exp *res_class_wrexp = NULL;
		wild_reg_exp *res_name_wrexp = NULL;
		int res_name_no = 1;
		wild_reg_exp *name_wrexp = NULL;
		ASBiDirElem *e = LIST_START (Scr.Windows->clients);
		char *ptr = pattern;
		char *tmp = safemalloc (strlen (pattern) + 1);
		int tmp_len = 0;
		Bool matches_reqired = 0;

		ptr = parse_semicolon_token (ptr, tmp, &tmp_len);
		LOCAL_DEBUG_OUT ("res_class pattern = \"%s\"", tmp);
		if (tmp[0]) {
			res_class_wrexp = compile_wild_reg_exp_sized (tmp, tmp_len);
			++matches_reqired;
			ptr = parse_semicolon_token (ptr, tmp, &tmp_len);
			LOCAL_DEBUG_OUT ("res_name pattern = \"%s\"", tmp);
			if (tmp[0]) {
				res_name_wrexp = compile_wild_reg_exp_sized (tmp, tmp_len);
				++matches_reqired;
				ptr = parse_semicolon_token (ptr, tmp, &tmp_len);
				LOCAL_DEBUG_OUT ("final pattern = \"%s\"", tmp);
				if (tmp[0] == '#' && isdigit (tmp[1])) {
					res_name_no = atoi (tmp + 1);
					LOCAL_DEBUG_OUT ("res_name_no = %d", res_name_no);
				} else if (tmp[0]) {
					name_wrexp = compile_wild_reg_exp_sized (tmp, tmp_len);
					++matches_reqired;
				}
			} else {
				res_name_wrexp = res_class_wrexp;
				name_wrexp = res_class_wrexp;
			}
		}
		free (tmp);

		for (; e != NULL && (asw == NULL || res_name_no > 0);
				 LIST_GOTO_NEXT (e)) {
			ASWindow *curr = (ASWindow *) LISTELEM_DATA (e);
			int matches = 0;
			LOCAL_DEBUG_OUT ("matching res_class \"%s\"",
											 curr->hints->res_class);
			if (res_class_wrexp != NULL)
				if (match_wild_reg_exp (curr->hints->res_class, res_class_wrexp) ==
						0)
					++matches;
			LOCAL_DEBUG_OUT ("matching res_name \"%s\"", curr->hints->res_name);
			if (res_name_wrexp != NULL)
				if (match_wild_reg_exp (curr->hints->res_name, res_name_wrexp) ==
						0)
					++matches;
			LOCAL_DEBUG_OUT ("matching name \"%s\"", curr->hints->names[0]);
			if (name_wrexp != NULL)
				if (match_wild_reg_exp (curr->hints->names[0], name_wrexp) == 0 ||
						match_wild_reg_exp (curr->hints->icon_name, name_wrexp) == 0)
					++matches;

			if (matches < matches_reqired)
				continue;
			asw = curr;
			--res_name_no;
			LOCAL_DEBUG_OUT ("matches = %d, res_name_no = %d, asw = %p", matches,
											 res_name_no, asw);
		}

		if (res_class_wrexp)
			destroy_wild_reg_exp (res_class_wrexp);
		if (res_name_wrexp != res_class_wrexp && res_name_wrexp)
			destroy_wild_reg_exp (res_name_wrexp);
		if (name_wrexp && name_wrexp != res_class_wrexp)
			destroy_wild_reg_exp (name_wrexp);
		if (res_name_no > 0)
			asw = NULL;								/* not found with requested seq no */
	}
	return asw;
}

