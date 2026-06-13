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
/* Desktop Category functionality                                        */
/*************************************************************************/

ASDesktopCategory *create_desktop_category (const char *name)
{
	ASDesktopCategory *dc = safecalloc (1, sizeof (ASDesktopCategory));

	dc->ref_count = 1;
	dc->index_name = mystrdup (name);
	dc->entries = create_asvector (sizeof (char *));
	return dc;
}

static void destroy_desktop_category (ASDesktopCategory ** pdc)
{
	if (pdc) {
		ASDesktopCategory *dc = *pdc;

		if (dc) {
			if (dc->name)
				free (dc->name);
			if (dc->index_name)
				free (dc->index_name);
			if (dc->entries) {
				char **pe = PVECTOR_HEAD (char *, dc->entries);
				int i = PVECTOR_USED (dc->entries);

				while (--i >= 0)
					if (pe[i])
						free (pe[i]);
				destroy_asvector (&(dc->entries));
			}
			LOCAL_DEBUG_OUT ("dc = %p, ref_count = %d", dc, dc->ref_count);
			free (dc);
			*pdc = NULL;
		}
	}
}

int ref_desktop_category (ASDesktopCategory * dc)
{
	if (dc)
		return ++dc->ref_count;
	return 0;
}

int unref_desktop_category (ASDesktopCategory * dc)
{
	if (dc) {
		LOCAL_DEBUG_OUT ("dc = %p, ref_count = %d", dc, dc->ref_count);
		if ((--dc->ref_count) > 0)
			return dc->ref_count;
		LOCAL_DEBUG_OUT ("dc = %p, ref_count = %d", dc, dc->ref_count);
		destroy_desktop_category (&dc);
	}
	return 0;
}

static void desktop_category_destroy (ASHashableValue value, void *data)
{
	char *alias = (char *)value;
	ASDesktopCategory *dc = (ASDesktopCategory *) data;

	if (alias != dc->name && alias != dc->index_name)
		free (alias);

	unref_desktop_category (dc);
}


void
add_desktop_category_entry (ASDesktopCategory * dc, const char *entry_name)
{
	if (dc && entry_name) {
		LOCAL_DEBUG_OUT ("adding \"%s\" to category \"%s\"", entry_name,
										 dc->index_name);
		if (mystrcasecmp (dc->name, entry_name) != 0) {	/* to prevent cyclic hierarchy !!! */
			char **existing = PVECTOR_HEAD (char *, dc->entries);
			int num = PVECTOR_USED (dc->entries);
			char *entry_name_copy;

			while (--num >= 0) {
				if (strcmp (existing[num], entry_name) == 0)
					return;
			}
			entry_name_copy = mystrdup (entry_name);
			append_vector (dc->entries, &entry_name_copy, 1);
			LOCAL_DEBUG_OUT ("\"%s\" added to category \"%s\"", entry_name,
											 dc->index_name);
		}
	}
}

void
remove_desktop_category_entry (ASDesktopCategory * dc,
															 const char *entry_name)
{
	char **existing = PVECTOR_HEAD (char *, dc->entries);
	int num = PVECTOR_USED (dc->entries);

	LOCAL_DEBUG_OUT ("removing \"%s\" from category \"%s\"", entry_name,
									 dc->name);
	while (--num >= 0) {
		if (strcmp (existing[num], entry_name) == 0) {
			free (existing[num]);
			vector_remove_index (dc->entries, num);
			return;
		}
	}
}

void print_desktop_category (ASDesktopCategory * dc)
{
	if (dc) {
		char **entries = PVECTOR_HEAD (char *, dc->entries);
		int num = PVECTOR_USED (dc->entries);

		fprintf (stderr, "category(%p).index_name=\"%s\";\n", dc,
						 dc->index_name);
		fprintf (stderr, "category(%p).name=\"%s\";\n", dc,
						 dc->name ? dc->name : "(null)");
		fprintf (stderr, "category(%p).entries_num=%d;\n", dc, num);
		if (num > 0) {
			int i;

			fprintf (stderr, "category(%p).entries=", dc);
			for (i = 0; i < num; ++i)
				fprintf (stderr, "%c\"%s\"", (i == 0) ? '{' : ';', entries[i]);
			fputs ("};\n", stderr);
		}
	}
}

void desktop_category_print (ASHashableValue value, void *data)
{
	print_desktop_category ((ASDesktopCategory *) data);
}



static Bool check_de_categories_excluded (const ASDesktopEntry * de,
																					const ASHashTable * exclusions)
{
	int k;
#ifdef DEBUG_GET_ENTRIES
	LOCAL_DEBUG_OUT ("DE \"%s\" - Checking %d categories for exclusion",
									 de->Name, de->categories_num);
#endif
	for (k = 0; k < de->categories_num; ++k) {
#ifdef DEBUG_GET_ENTRIES
		LOCAL_DEBUG_OUT ("Checking \"%s\" for exclusion",
										 de->categories_shortcuts[k]);
#endif
		if (get_hash_item
				(exclusions, AS_HASHABLE (de->categories_shortcuts[k]),
				 NULL) == ASH_Success)
			return True;
	}
	return False;
}

ASDesktopEntryInfo *desktop_category_get_entries (const ASCategoryTree *
																									ct,
																									const ASDesktopCategory *
																									dc, int max_depth,
																									const ASHashTable *
																									exclusions, int *nitems)
{
	int i, entries_num, valid_entries_num = 0;
	char **entries_names;
	ASDesktopEntryInfo *entries;

	if (nitems)
		*nitems = 0;

	if (ct == NULL || dc == NULL)
		return NULL;

	entries_num = PVECTOR_USED (dc->entries);
#ifdef DEBUG_GET_ENTRIES
	LOCAL_DEBUG_OUT ("DesktopCategory \"%s\", has %d entries", dc->name,
									 entries_num);
	if (exclusions)
		print_ashash (exclusions, string_print);
#endif
	if (entries_num == 0)
		return NULL;
	entries_names = PVECTOR_HEAD (char *, dc->entries);
	entries = safecalloc (entries_num + 1, sizeof (ASDesktopEntryInfo));

	for (i = 0; i < entries_num; ++i) {
		ASDesktopEntry *de;

		if (exclusions)
			if (get_hash_item (exclusions, AS_HASHABLE (entries_names[i]), NULL)
					== ASH_Success)
				continue;

		de = fetch_desktop_entry (ct, entries_names[i]);
#ifdef DEBUG_GET_ENTRIES
		LOCAL_DEBUG_OUT ("entry \"%s\", de = %p", entries_names[i], de);
#endif
		if (de == NULL)
			continue;
		if (get_flags (de->flags, ASDE_Hidden | ASDE_NoDisplay))
			continue;

		if (de->type == ASDE_TypeDirectory) {
			if (max_depth <= 0)
				continue;

			entries[valid_entries_num].dc =
					fetch_desktop_category (ct,
																	de->IndexName ? de->
																	IndexName : de->Name);
			if (entries[valid_entries_num].dc == NULL
					|| PVECTOR_USED (entries[valid_entries_num].dc->entries) == 0)
				continue;
		} else if (exclusions && check_de_categories_excluded (de, exclusions))
			continue;

		entries[valid_entries_num].de = de;
		++valid_entries_num;
	}

	if (nitems)
		*nitems = valid_entries_num;

	return entries;
}

Bool
desktop_entry_in_subcategory (ASCategoryTree * ct, ASDesktopEntry * de,
															ASDesktopEntryInfo * entries,
															int entries_num)
{
	int k;

	for (k = 0; k < entries_num; ++k)
		if (entries[k].de->type == ASDE_TypeDirectory)
			if (desktop_entry_belongs_to (ct, de, entries[k].de))
				return True;
	return False;
}

/*************************************************************************
 * Desktop Category Tree functionality :
 *************************************************************************/

ASDesktopCategory *fetch_desktop_category (const ASCategoryTree * ct,
																					 const char *cname)
{
	void *tmp = NULL;
	int res;

/*	LOCAL_DEBUG_OUT( "fetching \"%s\"", cname?cname:"(null)" );	*/
	if (ct == NULL)
		return NULL;

	if (cname == NULL)
		return ct->default_category;

	res = get_hash_item (ct->categories, AS_HASHABLE (cname), &tmp);

	if (res != ASH_Success)
		return NULL;

/*	LOCAL_DEBUG_OUT( "found \"%s\" with ptr %p", cname, tmp );	  */
	return (ASDesktopCategory *) tmp;
}

ASDesktopEntry *fetch_desktop_entry (const ASCategoryTree * ct,
																		 const char *name)
{
	void *tmp = NULL;

/*	LOCAL_DEBUG_OUT( "fetching \"%s\"", cname?cname:"(null)" );	*/
	if (ct == NULL)
		return NULL;

	if (name != NULL)
		if (get_hash_item (ct->entries, AS_HASHABLE (name), &tmp) ==
				ASH_Success)
			return (ASDesktopEntry *) tmp;
	return NULL;
}

Bool
desktop_entry_belongs_to (ASCategoryTree * ct, ASDesktopEntry * de,
													ASDesktopEntry * category_de)
{
	if (ct && de && category_de)
		if (de->categories_num > 0) {
			int i;

			for (i = 0; i < de->categories_num; ++i) {
				if (fetch_desktop_entry (ct, de->categories_shortcuts[i]) ==
						category_de)
					return True;
			}
		}
	return False;
}

ASDesktopCategory *obtain_category (ASCategoryTree * ct, const char *cname,
																		Bool dont_add_to_default)
{
	ASDesktopCategory *dc = fetch_desktop_category (ct, cname);

	if (dc == NULL) {
		dc = create_desktop_category (cname);
		LOCAL_DEBUG_OUT ("creating category using index_name \"%s\"", cname);
		if (add_hash_item (ct->categories, AS_HASHABLE (dc->index_name), dc) !=
				ASH_Success) {
			unref_desktop_category (dc);
			dc = NULL;
		} else if (!dont_add_to_default
							 && mystrcasecmp (cname, DEFAULT_DESKTOP_CATEGORY_NAME) != 0)
			add_desktop_category_entry (ct->default_category, cname);
	}
	return dc;
}

Bool register_desktop_entry (ASCategoryTree * ct, ASDesktopEntry * de)
{
	int i;
	Bool top_level = False;
	char *index_name;
	int exclude_reason = 0;

#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	print_desktop_entry (de);
#endif

	if (de == NULL || de->Name == NULL)
		return False;

	if (get_flags (ct->flags, ASCT_ConstrainCategory)) {
		exclude_reason = __LINE__;
		for (i = 0; i < de->categories_num; ++i)
			if (mystrcasecmp (ct->name, de->categories_shortcuts[i]) == 0) {
				exclude_reason = 0;
				break;
			}
	} else if (get_flags (ct->flags, ASCT_ExcludeGNOME)
						 && get_flags (de->flags, ASDE_GNOME))
		exclude_reason = __LINE__;
	else if (get_flags (ct->flags, ASCT_ExcludeKDE)
					 && get_flags (de->flags, ASDE_KDE))
		exclude_reason = __LINE__;
	else if (get_flags (ct->flags, ASCT_OnlyGNOME)
					 && !get_flags (de->flags, ASDE_GNOME))
		exclude_reason = __LINE__;
	else if (get_flags (ct->flags, ASCT_OnlyKDE)
					 && !get_flags (de->flags, ASDE_KDE))
		exclude_reason = __LINE__;
	LOCAL_DEBUG_OUT ("excl_reason = %d for de(%p) ", exclude_reason, de);
	if (exclude_reason > 0)
		return False;

	index_name = de->IndexName ? de->IndexName : de->Name;

	i = 0;
	if (add_hash_item (ct->entries, AS_HASHABLE (index_name), de) ==
			ASH_Success) {
		ref_desktop_entry (de);
		++i;
	}
	if (de->Name != index_name)
		if (add_hash_item (ct->entries, AS_HASHABLE (de->Name), de) ==
				ASH_Success) {
			ref_desktop_entry (de);
			++i;
		}
	if (i == 0 && de->type == ASDE_TypeApplication) {
		ASDesktopEntry *existing_de = fetch_desktop_entry (ct, de->Name);

		if (existing_de != NULL && existing_de->type == ASDE_TypeDirectory) {
			index_name = NULL;
			if (de->GenericName)
				if (add_hash_item (ct->entries, AS_HASHABLE (de->GenericName), de)
						== ASH_Success)
					index_name = mystrdup (de->GenericName);

			if (index_name == NULL && de->Comment != NULL
					&& strlen (de->Comment) < 32)
				if (add_hash_item (ct->entries, AS_HASHABLE (de->Comment), de) ==
						ASH_Success)
					index_name = mystrdup (de->Comment);

			if (index_name == NULL) {
				index_name =
						safemalloc (strlen (de->Name) + sizeof (" application") + 1);
				sprintf (index_name, "%s application", de->Name);
				if (add_hash_item (ct->entries, AS_HASHABLE (index_name), de) !=
						ASH_Success) {
					free (index_name);
					index_name = NULL;
				}
			}
			if (index_name) {
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
				{
					ASDesktopEntry *ttt = fetch_desktop_entry (ct, index_name);

					LOCAL_DEBUG_OUT
							("entry %p added under non-standard index_name = \"%s\", fetch returned = %p",
							 de, index_name, ttt);
				}
#endif
				ref_desktop_entry (de);
				if (de->IndexName)
					free (de->IndexName);
				de->IndexName = index_name;
			} else
				return False;
		}
	}

	if (de->categories_num == 0
			|| mystrcasecmp (de->categories_shortcuts[0],
											 DEFAULT_DESKTOP_CATEGORY_NAME) == 0) {
		top_level = True;
	}

	if (de->type == ASDE_TypeDirectory) {
		ASDesktopCategory *dc = NULL;
		void *tmp = NULL;

		LOCAL_DEBUG_OUT ("desktop entry is a directory \"%s\"", de->Name);
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
		print_desktop_entry (de);
#endif

		if (get_hash_item (ct->categories, AS_HASHABLE (de->Name), &tmp) ==
				ASH_Success)
			dc = (ASDesktopCategory *) tmp;

		if (dc == NULL && de->IndexName) {
			if (get_hash_item (ct->categories, AS_HASHABLE (de->IndexName), &tmp)
					== ASH_Success)
				dc = (ASDesktopCategory *) tmp;
		}

		if (dc == NULL && de->aliases_num) {
			for (i = 0; i < de->aliases_num; ++i)
				if (get_hash_item
						(ct->categories, AS_HASHABLE (de->aliases_shortcuts[i]),
						 &tmp) == ASH_Success)
					dc = (ASDesktopCategory *) tmp;
		}

		if (dc != NULL) {
			LOCAL_DEBUG_OUT
					("category already exists with index_name \"%s\" and name \"%s\"",
					 dc->index_name, dc->name ? dc->name : "(null)");
			if (!top_level)
				remove_desktop_category_entry (ct->default_category,
																			 dc->index_name);
			index_name = dc->index_name;
		} else {
			dc = create_desktop_category (index_name);
			LOCAL_DEBUG_OUT ("creating category using index_name \"%s\"",
											 index_name);
			if (add_hash_item (ct->categories, AS_HASHABLE (dc->index_name), dc)
					!= ASH_Success) {
				unref_desktop_category (dc);
				dc = NULL;
			}
		}
		if (dc->name == NULL) {
			LOCAL_DEBUG_OUT ("setting category name to \"%s\"", de->Name);
			dc->name = mystrdup (de->Name);
		}

		if (de->aliases_num) {
			for (i = 0; i < de->aliases_num; ++i) {
				char *tmp = mystrdup (de->aliases_shortcuts[i]);

				if (add_hash_item (ct->categories, AS_HASHABLE (tmp), dc) !=
						ASH_Success)
					free (tmp);
				else {
					LOCAL_DEBUG_OUT ("adding category alias to \"%s\"", tmp);
					ref_desktop_category (dc);
				}
			}
		}
		{
			char *tmp = mystrdup (de->Name);

			if (add_hash_item (ct->categories, AS_HASHABLE (tmp), dc) !=
					ASH_Success)
				free (tmp);
			else {
				LOCAL_DEBUG_OUT
						("adding category reference using common name \"%s\"",
						 de->Name);
				ref_desktop_category (dc);
			}
		}
	}

	if (top_level)
		add_desktop_category_entry (ct->default_category, index_name);

	for (i = top_level ? 1 : 0; i < de->categories_num; ++i) {
		ASDesktopCategory *dc = NULL;

		if (de->categories_num == 1
				|| mystrcasecmp (ct->name, de->categories_shortcuts[i]) != 0)
			dc = obtain_category (ct, de->categories_shortcuts[i], False);

		if (dc)
			add_desktop_category_entry (dc, index_name);
	}
	LOCAL_DEBUG_OUT ("%s belongs to %d categories", index_name, i);
	return True;
}

ASCategoryTree *create_category_tree (const char *name, const char *path,
																			const char *icon_path,
																			ASFlagType flags, int max_depth)
{
	ASCategoryTree *ct = safecalloc (1, sizeof (ASCategoryTree));

	ct->max_depth = max_depth;
	ct->flags = flags;
	if (path) {
		int i = 0;
		char *clean_path = copy_replace_envvar (path);

		while (clean_path[i]) {
			if (clean_path[i] == ':' && i > 0 && clean_path[i - 1] != ':')
				++(ct->dir_num);
			++i;
		}
		if (i > 1 && clean_path[i] != ':')
			++(ct->dir_num);
		if (ct->dir_num > 0) {
			int dir = 0;

			ct->dir_list = safecalloc (ct->dir_num, sizeof (char *));
			i = 0;
			do {
				while (clean_path[i] == ':')
					++i;
				if (clean_path[i]) {
					int start = i;

					while (clean_path[i] != ':' && clean_path[i])
						++i;
					if (i - start > 0)
						ct->dir_list[dir++] =
								mystrndup (&clean_path[start], i - start);
				}
			}
			while (clean_path[i]);
		}
		free (clean_path);
	}
	ct->name = mystrdup (name);
	ct->icon_path = copy_replace_envvar (icon_path);
	ct->entries =
			create_ashash (0, casestring_hash_value, casestring_compare,
										 desktop_entry_destroy);
	ct->categories =
			create_ashash (0, casestring_hash_value, casestring_compare,
										 desktop_category_destroy);
	ct->default_category =
			obtain_category (ct, DEFAULT_DESKTOP_CATEGORY_NAME, True);
	return ct;
}

void destroy_category_tree (ASCategoryTree ** pct)
{
	if (pct) {
		ASCategoryTree *ct = *pct;

		if (ct) {
			int i;

			if (ct->dir_list) {
				for (i = 0; i < ct->dir_num; ++i)
					destroy_string (&(ct->dir_list[i]));
				free (ct->dir_list);
			}
			destroy_string (&(ct->name));
			destroy_string (&(ct->icon_path));

			destroy_ashash (&(ct->entries));
			destroy_ashash (&(ct->categories));

			memset (ct, 0x00, sizeof (ASCategoryTree));
			free (ct);
			*pct = NULL;
		}

	}
}

void
add_category_tree_subtree (ASCategoryTree * ct, ASCategoryTree * subtree)
{
	ASHashIterator i;

	if (ct == NULL || subtree == NULL)
		return;

	if (start_hash_iteration (subtree->entries, &i)) {
		do {
			ASDesktopEntry *de = curr_hash_data (&i);
			char *hashed_name = (char *)curr_hash_value (&i);
			ASHashResult res =
					add_hash_item (ct->entries, AS_HASHABLE (hashed_name), de);

			LOCAL_DEBUG_OUT ("adding desktop entry %p with result %d", de, res);
			if (res == ASH_Success)
				ref_desktop_entry (de);

		}
		while (next_hash_item (&i));
	}
	if (start_hash_iteration (subtree->categories, &i)) {
		do {
			char *alias = (char *)curr_hash_value (&i);
			ASDesktopCategory *subtree_dc = curr_hash_data (&i);
			ASDesktopCategory *dc =
					fetch_desktop_category (ct, subtree_dc->index_name);

			LOCAL_DEBUG_OUT
					("category with index_name \"%s\" and name \"%s\" exists as %p",
					 subtree_dc->index_name,
					 subtree_dc->name ? subtree_dc->name : "(null)", dc);

			if (dc == NULL) {
				dc = create_desktop_category (subtree_dc->index_name);
				if (subtree_dc->name != NULL)
					dc->name = mystrdup (subtree_dc->name);
				LOCAL_DEBUG_OUT
						("creating category using index_name \"%s\" and name \"%s\"",
						 dc->index_name, dc->name ? dc->name : "(null)");
				if (add_hash_item
						(ct->categories, AS_HASHABLE (dc->index_name),
						 dc) != ASH_Success) {
					unref_desktop_category (dc);
					dc = NULL;
				} else if (dc->name)
					if (add_hash_item (ct->categories, AS_HASHABLE (dc->name), dc) ==
							ASH_Success)
						ref_desktop_category (dc);
			}

			if (dc) {
				if (mystrcasecmp (alias, dc->index_name) != 0) {
					if (dc->name && mystrcasecmp (alias, dc->name) != 0) {
						char *tmp = mystrdup (alias);

						if (add_hash_item (ct->categories, AS_HASHABLE (tmp), dc) !=
								ASH_Success)
							free (tmp);
						else
							ref_desktop_category (dc);
					}
				}
			}
			if (dc) {
				char **subtree_entries =
						PVECTOR_HEAD (char *, subtree_dc->entries);
				int num = PVECTOR_USED (subtree_dc->entries);
				int i;

				for (i = 0; i < num; ++i) {
					ASDesktopCategory *sub_dc =
							fetch_desktop_category (ct, subtree_entries[i]);

					if (sub_dc)
						add_desktop_category_entry (dc, sub_dc->index_name);
					else
						add_desktop_category_entry (dc, subtree_entries[i]);
				}
			}
		}
		while (next_hash_item (&i));
	}
}

void print_category_tree (ASCategoryTree * ct)
{
	fprintf (stderr, "Printing category_tree %p :\n", ct);
	if (ct) {
		fprintf (stderr, "category_tree.flags=0x%lX;\n", ct->flags);
		fprintf (stderr, "category_tree.name=\"%s\";\n",
						 ct->name ? ct->name : "(null)");
		fprintf (stderr, "category_tree.icon_path=\"%s\";\n",
						 ct->icon_path ? ct->icon_path : "(null)");
		fprintf (stderr, "category_tree.entries_num=%ld;\n",
						 ct->entries->items_num);
		print_ashash2 (ct->entries, desktop_entry_print);
		fprintf (stderr, "category_tree.categories_num=%ld;\n",
						 ct->categories->items_num);
		print_ashash2 (ct->categories, desktop_category_print);
	}

}

void
print_category_subtree (ASCategoryTree * ct, const char *entry_name,
												int level)
{
	ASDesktopCategory *dc = fetch_desktop_category (ct, entry_name);

	if (dc && level < 40) {
		char **entries = PVECTOR_HEAD (char *, dc->entries);
		int num = PVECTOR_USED (dc->entries);
		int i, k;

		++level;
		for (i = 0; i < num; ++i) {
			ASDesktopCategory *sub_dc = fetch_desktop_category (ct, entries[i]);

			if (sub_dc == dc) {
				fprintf (stderr,
								 "%5.5d: !!! reccurent refference \"%s\" to self \"%s\"(\"%s\")!!! \n",
								 level, entries[i], dc->name, dc->index_name);
				continue;
			}
			fprintf (stderr, "%5.5d:", level);
			for (k = 0; k < level; ++k)
				fputc ('\t', stderr);
			if (sub_dc) {
				if (sub_dc->name == NULL)
					fprintf (stderr, "C: %s\n", sub_dc->index_name);
				else
					fprintf (stderr, "C: %s(%s)\n", sub_dc->name,
									 sub_dc->index_name);
			} else
				fprintf (stderr, "E: %s\n", entries[i]);
			if (sub_dc)
				print_category_subtree (ct, entries[i], level);
		}
	}
}

void print_category_tree2 (ASCategoryTree * ct, ASDesktopCategory * dc)
{
	fprintf (stderr, "Printing category_tree %p :\n", ct);
	if (ct) {
		fprintf (stderr, "category_tree.flags=0x%lX;\n", ct->flags);
		fprintf (stderr, "category_tree.name=\"%s\";\n",
						 ct->name ? ct->name : "(null)");
		fprintf (stderr, "category_tree.icon_path=\"%s\";\n",
						 ct->icon_path ? ct->icon_path : "(null)");
		fprintf (stderr, "category_tree.entries_num=%ld;\n",
						 ct->entries->items_num);
		fprintf (stderr, "category_tree.default_category_name=\"%s\";\n",
						 ct->default_category->name);
		fprintf (stderr, "category_tree.categories_num=%ld;\n",
						 ct->categories->items_num);
		print_category_subtree (ct,
														dc ? dc->index_name :
														DEFAULT_DESKTOP_CATEGORY_NAME, 0);
	}

}

void save_category_tree (ASCategoryTree * ct, FILE * fp)
{
	ASHashIterator i;

	if (ct == NULL || fp == NULL)
		return;

	if (start_hash_iteration (ct->entries, &i)) {
		do {
			ASDesktopEntry *de = curr_hash_data (&i);

			if (de->type == ASDE_TypeDirectory)
				save_desktop_entry (de, fp);
		}
		while (next_hash_item (&i));
	}
	if (start_hash_iteration (ct->entries, &i)) {
		do {
			ASDesktopEntry *de = curr_hash_data (&i);

			if (de->type != ASDE_TypeDirectory)
				save_desktop_entry (de, fp);
		}
		while (next_hash_item (&i));
	}
}


/****************** /public **********************/
