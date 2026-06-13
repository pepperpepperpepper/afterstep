/*
 * Copyright (C) 2005 Sasha Vasko <sasha at aftercode.net>
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
 */

/* Desktop Entry lifecycle, printing, persistence and function mapping,
 * split out of desktop_category.c. The category-tree code in
 * desktop_category.c consumes desktop_entry_destroy/desktop_entry_print
 * (declared in desktop_category_internal.h) plus the public entry API. */

#define LOCAL_DEBUG
#define EVENT_TRACE
#undef DEBUG_GET_ENTRIES

#include "../configure.h"
#include <unistd.h>

#include "asapp.h"
#include "afterstep.h"
#include "desktop_category.h"
#include "desktop_category_internal.h"

/*************************************************************************/
/* Desktop Entry functionality                                           */
/*************************************************************************/

ASDesktopEntry *create_desktop_entry (ASDesktopEntryTypes default_type)
{
	ASDesktopEntry *de = safecalloc (1, sizeof (ASDesktopEntry));

	de->ref_count = 1;
	de->type = default_type;
	return de;
}

static void destroy_desktop_entry (ASDesktopEntry ** pde)
{
	if (pde) {
		ASDesktopEntry *de;

		if ((de = *pde) != NULL) {
#define FREE_ASDE_VAL(val)	do{if(de->val) free( de->val );}while(0)
			FREE_ASDE_VAL (Name_localized);
			FREE_ASDE_VAL (Comment_localized);

			FREE_ASDE_VAL (Name);
			FREE_ASDE_VAL (GenericName);
			FREE_ASDE_VAL (Comment);

			FREE_ASDE_VAL (Icon);

			FREE_ASDE_VAL (TryExec);
			FREE_ASDE_VAL (Exec);
			FREE_ASDE_VAL (Path);			/* work dir */


			FREE_ASDE_VAL (SwallowTitle);
			FREE_ASDE_VAL (SwallowExec);
			FREE_ASDE_VAL (SortOrder);

			FREE_ASDE_VAL (Categories);
			FREE_ASDE_VAL (Aliases);
			FREE_ASDE_VAL (OnlyShowIn);
			FREE_ASDE_VAL (NotShowIn);
			FREE_ASDE_VAL (StartupWMClass);

			FREE_ASDE_VAL (IndexName);

#define FREE_ASDE_VAL_LIST(val,num)	\
			if( de->val ){	/*for( i = 0 ; i < de->num ; ++i ) destroy_string(&(de->val[i])) ;*/ \
							free( de->val ); }

			FREE_ASDE_VAL (aliases_shortcuts);
			FREE_ASDE_VAL (categories_shortcuts);
			FREE_ASDE_VAL (show_in_shortcuts);
			FREE_ASDE_VAL (not_show_in_shortcuts);

			FREE_ASDE_VAL (clean_exec);
			FREE_ASDE_VAL (origin);
			free (de);
			*pde = NULL;
		}
	}
}

int ref_desktop_entry (ASDesktopEntry * de)
{
	if (de)
		return ++de->ref_count;
	return 0;
}

int unref_desktop_entry (ASDesktopEntry * de)
{
	if (de) {
		if ((--de->ref_count) > 0)
			return de->ref_count;

		destroy_desktop_entry (&de);
	}
	return 0;
}



void print_desktop_entry (ASDesktopEntry * de)
{
	if (de != NULL) {
		int i;

		switch (de->type) {
		case ASDE_TypeApplication:
			fprintf (stderr, "de(%p).type=Application;\n", de);
			break;
		case ASDE_TypeLink:
			fprintf (stderr, "de(%p).type=Link;\n", de);
			break;
		case ASDE_TypeFSDevice:
			fprintf (stderr, "de(%p).type=FSDevice;\n", de);
			break;
		case ASDE_TypeDirectory:
			fprintf (stderr, "de(%p).type=Directory;\n", de);
			break;
		default:
			break;
		}
		fprintf (stderr, "de(%p).flags = 0x%lX;\n", de, de->flags);
#define PRINT_ASDE_VAL(val)	do{if(de->val) fprintf(stderr, "de(%p)." #val "=\"%s\";\n", de, de->val );}while(0)
		PRINT_ASDE_VAL (Name_localized);
		PRINT_ASDE_VAL (Comment_localized);

		PRINT_ASDE_VAL (Name);
		PRINT_ASDE_VAL (GenericName);
		PRINT_ASDE_VAL (Comment);

		PRINT_ASDE_VAL (Icon);

		PRINT_ASDE_VAL (TryExec);
		PRINT_ASDE_VAL (Exec);
		PRINT_ASDE_VAL (Path);			/* work dir */


		PRINT_ASDE_VAL (SwallowTitle);
		PRINT_ASDE_VAL (SwallowExec);
		PRINT_ASDE_VAL (SortOrder);

		fprintf (stderr, "de(%p).categories_num=%d;\n", de,
						 de->categories_num);
		for (i = 0; i < de->categories_num; ++i)
			fprintf (stderr, "de(%p).category[%d]=\"%s\";\n", de, i,
							 de->categories_shortcuts[i]);
		PRINT_ASDE_VAL (Categories);
		PRINT_ASDE_VAL (OnlyShowIn);
		PRINT_ASDE_VAL (NotShowIn);
		PRINT_ASDE_VAL (StartupWMClass);

		PRINT_ASDE_VAL (IndexName);
		fprintf (stderr, "de(%p).aliases_num=%d;\n", de, de->aliases_num);
		for (i = 0; i < de->aliases_num; ++i)
			fprintf (stderr, "de(%p).alias[%d]=\"%s\";\n", de, i,
							 de->aliases_shortcuts[i]);

//      PRINT_ASDE_VAL(categories_shortcuts) ;
//      PRINT_ASDE_VAL(show_in_shortcuts) ;
//      PRINT_ASDE_VAL(not_show_in_shortcuts);
		PRINT_ASDE_VAL (clean_exec);

		PRINT_ASDE_VAL (origin);
		fprintf (stderr, "\n");
	}
}

char *make_desktop_entry_categories (ASDesktopEntry * de)
{
	char *categories = NULL;

	if (de && de->categories_num) {
		int len = 0, i;
		char *ptr;

		for (i = 0; i < de->categories_num; ++i)
			len += 1 + strlen (de->categories_shortcuts[i]);
		ptr = categories = safemalloc (len + 1);
		for (i = 0; i < de->categories_num; ++i) {
			sprintf (ptr, "%s;", de->categories_shortcuts[i]);
			while (*ptr)
				++ptr;
		}
	}
	return categories;
}

void save_desktop_entry (ASDesktopEntry * de, FILE * fp)
{
	if (de != NULL) {
		int i;

		fputs ("[Desktop Entry]\n", fp);
		switch (de->type) {
		case ASDE_TypeApplication:
			fputs ("Type=Application\n", fp);
			break;
		case ASDE_TypeLink:
			fputs ("Type=Link\n", fp);
			break;
		case ASDE_TypeFSDevice:
			fputs ("Type=FSDevice\n", fp);
			break;
		case ASDE_TypeDirectory:
			fputs ("Type=Directory\n", fp);
			break;
		default:
			break;
		}
		/* fprintf( stderr, "de(%p).flags = 0x%lX;\n", de, de->flags );         */
#undef PRINT_ASDE_VAL
#define PRINT_ASDE_VAL(val)	do{if(de->val) fprintf(fp, #val "=%s\n", de->val );}while(0)

		PRINT_ASDE_VAL (Name);
		PRINT_ASDE_VAL (GenericName);
		PRINT_ASDE_VAL (Comment);

		PRINT_ASDE_VAL (TryExec);
		PRINT_ASDE_VAL (Exec);
		PRINT_ASDE_VAL (Path);			/* work dir */

		PRINT_ASDE_VAL (SwallowTitle);
		PRINT_ASDE_VAL (SwallowExec);
		PRINT_ASDE_VAL (SortOrder);

		if (de->categories_num) {
			fputs ("Categories=", fp);
			for (i = 0; i < de->categories_num; ++i) {
				fputs (de->categories_shortcuts[i], fp);
				fputc (';', fp);
			}
			fputc ('\n', fp);
		}

		PRINT_ASDE_VAL (OnlyShowIn);
		PRINT_ASDE_VAL (NotShowIn);
		PRINT_ASDE_VAL (StartupWMClass);

		if (de->IndexName)
			fprintf (fp, "X-AfterStep-IndexName=%s\n", de->IndexName);
		if (de->aliases_num > 0) {
			fputs ("X-AfterStep-Aliases=", fp);
			for (i = 0; i < de->aliases_num; ++i) {
				fputs (de->aliases_shortcuts[i], fp);
				fputc (';', fp);
			}
			fputc ('\n', fp);
		}
		if (get_flags (de->flags, ASDE_ASModule))
			fputs ("X-AfterStep-ASModule=true\n", fp);

		if (get_flags (de->flags, ASDE_CheckAvailability))
			fputs ("X-AfterStep-CheckAvailability=true\n", fp);

		if (de->Icon)
			fprintf (fp, "Icon=%s\n", de->Icon);

#define PRINT_ASDE_FLAG(val)	do{if(get_flags(de->flags,ASDE_##val)) fputs(#val "=1\n", fp  );}while(0)
		PRINT_ASDE_FLAG (NoDisplay);
		PRINT_ASDE_FLAG (Hidden);
		PRINT_ASDE_FLAG (Terminal);
		PRINT_ASDE_FLAG (StartupNotify);

		fputc ('\n', fp);
	}
}

void desktop_entry_destroy (ASHashableValue value, void *data)
{
	unref_desktop_entry ((ASDesktopEntry *) data);
}

void desktop_entry_print (ASHashableValue value, void *data)
{
	print_desktop_entry ((ASDesktopEntry *) data);
}

#define DupDesktopEntryVal_func(val) \
Bool dup_desktop_entry_##val( ASDesktopEntry* de, char **trg ) \
{ if( de && trg ){ \
	if( *trg ) 	free( *trg ); \
	if( de->val##_localized ) \
	{	*trg = mystrdup (de->val##_localized); \
		if( ! get_flags( de->flags, ASDE_EncodingNonUTF8) ) 	return True; \
	}else	*trg = mystrdup (de->val); \
  }return False; \
}

DupDesktopEntryVal_func (Name) DupDesktopEntryVal_func (Comment)
FunctionCode desktop_entry2function_code (ASDesktopEntry * de)
{
	if (de == NULL || get_flags (de->flags, ASDE_Unavailable))
		return F_NOP;
	if (de->type == ASDE_TypeDirectory)
		return F_CATEGORY;
	if (de->type != ASDE_TypeApplication)
		return F_NOP;
	if (get_flags (de->flags, ASDE_Terminal))
		return F_ExecInTerm;
	if (get_flags (de->flags, ASDE_ASModule))
		return F_MODULE;
	return F_EXEC;
}


FunctionData *desktop_entry2function (ASDesktopEntry * de, char *name	/* defaults to de->Name */
		)
{
	FunctionData *fdata = NULL;

	if (de) {
		fdata =
				create_named_function (desktop_entry2function_code (de),
															 name ? name : de->Name);
		if (fdata && de->clean_exec)
			fdata->text = mystrdup (de->clean_exec);
	}
	return fdata;
}
