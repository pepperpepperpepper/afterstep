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
#include "freestor_internal.h"
#include "functions.h"
#include "../libAfterImage/asimagexml.h"

extern char *_disabled_keyword;

struct {
	char *label;
	int value;
} FixedEnumValues[] = {
#define FEV

	{
	NULL, 0}
};

TermDef *FindTerm (SyntaxDef * syntax, int type, int id)
{
	register int i;

	if (syntax)
		for (i = 0; syntax->terms[i].keyword; i++)
			if (type == TT_ANY || type == syntax->terms[i].type)
				if (id == ID_ANY || id == syntax->terms[i].id)
					return &(syntax->terms[i]);

	return NULL;
}


/* debugging stuff */
void
freestorage_print (char *myname, SyntaxDef * syntax,
									 FreeStorageElem * storage, int level)
{
	if (myname == NULL)
		myname = MyName;
	while (storage) {
		char sub_terminator = ' ';

		if (level > 0)
			if (syntax->terminator == '\n' || syntax->terminator == '\0')
				fprintf (stderr, "%*s", level * 4, " ");

		if (!get_flags (storage->term->flags, TF_NO_MYNAME_PREPENDING)) {
			fprintf (stderr, "*%s", myname);
			if (storage->term->keyword[0] == '~')
				fputc (' ', stderr);
		}
		fprintf (stderr, "%s", storage->term->keyword);
		{
			register int i;
			int token_count = storage->argc;

			if (storage->term->sub_syntax) {
				token_count = GetTermUseTokensCount (storage->term);
				if (get_flags
						(storage->term->flags,
						 TF_NAMED | TF_INDEXED | TF_DIRECTION_INDEXED))
					++token_count;
			}

			for (i = 0; i < token_count; i++)
				fprintf (stderr, " %s", storage->argv[i]);
			if (token_count == 0 && storage->sub && storage->term->sub_syntax)
				fputc (' ', stderr);
		}
		if (storage->sub && storage->term->sub_syntax) {
			if (storage->term->sub_syntax->terminator == '\n')
				fputc ('\n', stderr);
			freestorage_print (myname, storage->term->sub_syntax, storage->sub,
												 level + 1);
			sub_terminator = storage->term->sub_syntax->file_terminator;
			if (sub_terminator == '\0')
				sub_terminator = '\n';
		}
		if (storage->next && syntax->terminator != '\0' &&
				(sub_terminator == ' ' || sub_terminator != syntax->terminator))
			fputc (syntax->terminator, stderr);
		storage = storage->next;
	}
	if (syntax->file_terminator != '\0')
		fputc (syntax->file_terminator, stderr);
	else
		fputc ('\n', stderr);
}

/* StringArray functionality */
char **CreateStringArray (size_t elem_num)
{
	char **array = (char **)safemalloc (elem_num * sizeof (char *));
	int i;

	for (i = 0; i < elem_num; i++)
		array[i] = NULL;
	return array;
}

size_t GetStringArraySize (int argc, char **argv)
{
	size_t size = 0;

	for (argc--; argc >= 0; argc--)
		if (argv[argc])
			size += strlen (argv[argc]) + 1;
	return size;
}

char *CompressStringArray (int argc, char **argv)
{
	size_t size = 0;
	register char *end = argv[argc - 1], *cur = argv[0], *cur2;
	char *dst;

	while (*end)
		end++;
	size = end - cur + 1;

	cur2 = dst = (char *)safemalloc (size);

	for (; cur < end; cur++, cur2++)
		if (*cur)
			*cur2 = *cur;
		else
			*cur2 = ' ';

	*cur2 = '\0';

	return dst;
}

char **DupStringArray (int argc, char **argv)
{
	int i;
	size_t data_size;
	char **array = CreateStringArray (argc);

	data_size = GetStringArraySize (argc, argv);
	if (data_size && array) {
		array[0] = (char *)safemalloc (data_size);
		for (i = 0; i < data_size; i++)
			array[0][i] = argv[0][i];
		for (i = 1; i < argc; i++)
			if (argv[i])
				array[i] = array[0] + (argv[i] - argv[0]);
	}
	return array;
}

void AddStringToArray (int *argc, char ***argv, char *new_string)
{
	int i = 0;
	size_t data_size;
	char **array;

	if (new_string == NULL)
		return;

	array = CreateStringArray (*argc + 1);
	if (array) {
		data_size = GetStringArraySize (*argc, *argv);
		array[0] = (char *)safemalloc (data_size + strlen (new_string) + 1);
		if (data_size > 0) {
			memcpy (array[0], (*argv)[0], data_size);
			for (i = 1; i < *argc; i++)
				if ((*argv)[i])
					array[i] = array[0] + ((*argv)[i] - (*argv)[0]);
			array[i] = array[0] + data_size;
			free (*argv[0]);
		}
		if (*argv)
			free (*argv);
		strcpy (array[i], new_string);
		(*argc)++;
		*argv = array;
	}
}

/* end StringArray functionality */


/*********************************************************************************************/
/*                                     FreeStorage management                                */
/*********************************************************************************************/
/* Create new FreeStorage Elem and add it to the supplied storage's tail */
static FreeStorageElem *CreateFreeStorageElem (SyntaxDef * syntax,
																							 FreeStorageElem ** tail,
																							 TermDef * pterm, int id)
{
	FreeStorageElem *fs = NULL;

	if (pterm == NULL)
		if ((pterm = FindTerm (syntax, TT_ANY, id)) == NULL)
			return NULL;

	fs = (FreeStorageElem *) safecalloc (1, sizeof (FreeStorageElem));
	if (fs) {
		fs->term = pterm;
		if (tail) {
			fs->next = *tail;
			*tail = fs;
		}
	}
	return fs;
}


FreeStorageElem *AddFreeStorageElem (SyntaxDef * syntax,
																		 FreeStorageElem ** tail,
																		 TermDef * pterm, int id, ...)
{
	FreeStorageElem *fs = CreateFreeStorageElem (syntax, tail, pterm, id);

	if (fs) {
		va_list ap;
		int len = 0;
		char *v;

		va_start (ap, id);
		while ((v = va_arg (ap, char *)) != NULL) {
			fs->argc++;
			len += strlen (v) + 1;
		}
		va_end (ap);

		if (fs->argc > 0) {
			char *dst;
			int pos = 0;

			fs->argv = safecalloc (fs->argc, sizeof (char *));
			dst = safemalloc (len);

			va_start (ap, id);
			while ((v = va_arg (ap, char *)) != NULL) {
				int i = 0;

				do {
					dst[i] = v[i];
				}
				while (v[i++]);
				fs->argv[pos++] = dst;
				dst += i;
			}
			va_end (ap);
		}
	}
	return fs;
}

FreeStorageElem *AddFreeStorageElem_sa (SyntaxDef * syntax,
																				FreeStorageElem ** tail,
																				TermDef * pterm, int id,
																				char **strings, int count)
{
	FreeStorageElem *fs = CreateFreeStorageElem (syntax, tail, pterm, id);

	if (fs) {
		int si = 0;
		int len = 0;

		for (si = 0; strings[si] != NULL && si < count; ++si) {
			fs->argc++;
			len += strlen (strings[si]) + 1;
		}

		if (fs->argc > 0) {
			char *dst;
			int pos = 0;

			fs->argv = safecalloc (fs->argc, sizeof (char *));
			dst = safemalloc (len);

			for (si = 0; strings[si] != NULL && si < count; ++si) {
				int i = 0;
				char *v = strings[si];

				do {
					dst[i] = v[i];
				}
				while (v[i++]);
				fs->argv[pos++] = dst;
				dst += i;
			}
		}
	}
	return fs;
}


/* Duplicate existing FreeStorage Elem */
FreeStorageElem *DupFreeStorageElem (FreeStorageElem * source)
{
	FreeStorageElem *new_elem = NULL;

	if (source) {
		new_elem =
				(FreeStorageElem *) safecalloc (1, sizeof (FreeStorageElem));
		new_elem->term = source->term;
		new_elem->argc = source->argc;
		/* duplicating argv here */
		new_elem->argv = DupStringArray (source->argc, source->argv);
		new_elem->flags = source->flags;
		if (new_elem->sub) {
			FreeStorageElem *psub, **pnew_sub = &(new_elem->sub);

			for (psub = new_elem->sub; psub; psub = psub->next) {
				*pnew_sub = DupFreeStorageElem (psub);
				pnew_sub = &((*pnew_sub)->next);
			}
		}
	}
	return new_elem;
}

void ReverseFreeStorageOrder (FreeStorageElem ** storage)
{
	FreeStorageElem *pNewHead = NULL, *pNext;

	for (; *storage; *storage = pNext) {
		pNext = (*storage)->next;
		(*storage)->next = pNewHead;
		pNewHead = *storage;
	}
	*storage = pNewHead;
}

void CopyFreeStorage (FreeStorageElem ** to, FreeStorageElem * from)
{
	FreeStorageElem *pNew;

	if (to == NULL)
		return;

	for (; from; from = from->next) {
		if ((pNew = DupFreeStorageElem (from)) == NULL)
			continue;
		pNew->next = *to;
		*to = pNew;
		to = &((*to)->next);
	}
}

int CountFreeStorageElems (FreeStorageElem * storage)
{
	int count = 0;

	for (; storage; storage = storage->next)
		count++;
	return count;
}

/* this one will scan list of FreeStorage elements and will move all elements with
   specifyed flags mask into the garbadge_bin
 */

void
StorageCleanUp (FreeStorageElem ** storage,
								FreeStorageElem ** garbadge_bin, unsigned long mask)
{
	FreeStorageElem **ppCurr, *pToRem;

	for (ppCurr = storage; *ppCurr; ppCurr = &((*ppCurr)->next)) {
		while ((*ppCurr)->flags & mask) {
			pToRem = *ppCurr;
			*ppCurr = pToRem->next;
			pToRem->next = *garbadge_bin;
			*garbadge_bin = pToRem;
			if (*ppCurr == NULL)
				return;
		}
		if (*ppCurr && (*ppCurr)->sub)
			StorageCleanUp (&((*ppCurr)->sub), garbadge_bin, mask);
	}
}

/* memory deallocation */
void DestroyFreeStorage (FreeStorageElem ** storage)
{
	if (storage)
		if (*storage) {
			DestroyFreeStorage (&((*storage)->next));
			DestroyFreeStorage (&((*storage)->sub));
			/* that will deallocate everything */
			if ((*storage)->argc && (*storage)->argv) {
				if ((*storage)->argv[0])
#ifdef DEBUG_PARSER
				{
					fprintf (stderr, "\n DestroyFreeStorage: deallocating [%s].",
									 (*storage)->argv[0]);
#endif
					free ((*storage)->argv[0]);
#ifdef DEBUG_PARSER
				} else
					fprintf (stderr,
									 "\n DestroyFreeStorage: no data to deallocate.");
#endif
				free ((*storage)->argv);
			}
			free (*storage);
			*storage = NULL;
		}
}

