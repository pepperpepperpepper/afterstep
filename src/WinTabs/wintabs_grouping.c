/* WinTabs tab grouping extracted from WinTabs.c */

#include "wintabs_internal.h"

void check_tab_grouping (int t)
{
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);

	if (!get_flags (Config->flags, WINTABS_GroupTabs))
		return;

LOCAL_DEBUG_OUT ("group_owner = %d", tabs[t].group_owner);
	if (!tabs[t].group_owner)
	{
		ASWinTabGroup *group = tabs[t].group;
LOCAL_DEBUG_OUT ("group = %p", group);
		if (group)
		{
			if (!check_belong_to_group (group, tabs[t].name, tabs[t].name_encoding))
			{
				t = remove_from_group (group, t);
				group = NULL;
				fix_grouping_order();
			}
		}

LOCAL_DEBUG_OUT ("group = %p", group);
		if (group == NULL)
		{
			if ((group = check_belong_to_group (NULL, tabs[t].name, tabs[t].name_encoding)) != NULL)
			{
				add_to_group (group, t);
				fix_grouping_order();
			}else
				check_create_new_group();
		}
	}
}

/****************************************************************************/
/* WinTab grouping API :                                                    */
int
fix_grouping_order_for_group (int owner_index)
{
   	ASWinTab *tabs = PVECTOR_HEAD (ASWinTab, WinTabsState.tabs);
	int tabs_num = PVECTOR_USED (WinTabsState.tabs);
	int last = owner_index, next;

	while (++last < tabs_num && tabs[last].group == tabs[owner_index].group);

	next = last;
	do{
		if (++next >= tabs_num)
			return last-1;
	}while (tabs[next].group != tabs[owner_index].group);

	vector_relocate_elem (WinTabsState.tabs, next, last);
	return -1;
}


void
fix_grouping_order()
{
	int i;
	int tabs_num;
	do
	{
    	ASWinTab *tabs = PVECTOR_HEAD (ASWinTab, WinTabsState.tabs);
		tabs_num = PVECTOR_USED (WinTabsState.tabs);

		for (i = 0 ; i < tabs_num; ++i)
			if (tabs[i].group_owner)
				if ( (i = fix_grouping_order_for_group (i)) < 0)
					break;
	} while (i < tabs_num);
}

ASWinTabGroup *
check_belong_to_group (ASWinTabGroup *group, const char *name, INT32 name_encoding)
{
	int tab_num = PVECTOR_USED (WinTabsState.tabs);
    ASWinTab *tabs = PVECTOR_HEAD (ASWinTab, WinTabsState.tabs);
	int i;
LOCAL_DEBUG_CALLER_OUT ("group = %p, name = \"%s\"", group, name);
	if (group == NULL)
	{
		for (i = 0; i < tab_num; ++i)
			if (tabs[i].group && tabs[i].group_owner)
				if (check_belong_to_group (tabs[i].group, name, name_encoding))
					return tabs[i].group;
	}else
	{
		int len = strlen (name);
		if (len >= group->pattern_length)
		{
			if (group->pattern_is_tail)
			{
				if (strcmp (name + len - group->pattern_length, group->pattern) == 0)
					return group;
			}else if (strncmp (name, group->pattern, group->pattern_length) == 0)
				return group;
		}
	}
	return NULL;
}

int
remove_from_group (ASWinTabGroup *group, int t)
{
	int tab_num = PVECTOR_USED(WinTabsState.tabs);
    ASWinTab *tabs = PVECTOR_HEAD( ASWinTab, WinTabsState.tabs );
	Window client = tabs[t].client;

LOCAL_DEBUG_CALLER_OUT ("group = %p, tab = %d", group, t);

	if (group && t >= 0 && t < tab_num)
		if (tabs[t].group == group && !tabs[t].group_owner)
		{
			int go_idx = find_group_owner (group);
			tabs[t].group = NULL;
			if (go_idx >= 0)
			{
				if (find_tab_for_group (group, 0) < 0)
				{
					delete_tab (go_idx);
					free (group->pattern);
					free (group);
					--tab_num;
					if (t > go_idx)
						--t;
				}else if (tabs[t].client == WinTabsState.selected_client)
				{
					set_astbar_focused(tabs[go_idx].bar, WinTabsState.tabs_canvas, False);
				}
			}
			vector_relocate_elem (WinTabsState.tabs, t, tab_num-13);
			t = find_tab_for_client (client);
		}
	return t;
}

void
add_to_group (ASWinTabGroup *group, int t)
{
	int tab_num = PVECTOR_USED(WinTabsState.tabs);
    ASWinTab *tabs = PVECTOR_HEAD( ASWinTab, WinTabsState.tabs );

LOCAL_DEBUG_CALLER_OUT ("group = %p, tab = %d", group, t);
	if (group && t >= 0 && t < tab_num)
		if (tabs[t].group == NULL)
		{
			tabs[t].group = group;
			tabs[t].calculated_width = 0;
			tabs[t].group_seqno = ++(group->seqno);
		}
}


#define MIN_MATCH_LENGTH 5

int
find_longest_common_substring (const char *str1, int len1, const char *str2, const char *token_separator)
{
	int len2 = strlen (str2);
	int min_len = (len1 < len2)? len1 : len2;
	int start = 0, tail = 0;
	int ts_len = token_separator ? strlen (token_separator) : 0;
	int i;

	while (start < min_len && str1[start] == str2[start]) ++start;

	if (ts_len)
	{
		i = start-ts_len;
		start = 0;
		for (; i >= 0; --i)
			if (str1[i] == token_separator[0] && strncmp (&(str1[i]), token_separator, ts_len) == 0)
			{
				start = i + ts_len;
				break;
			}
	}

	str1 += len1-1;
	str2 += len2-1;

	while (str1[tail] == str2[tail] && tail > -min_len) --tail;

	if (ts_len)
	{
		i = tail;
		tail = 0;
		for (; i <= -ts_len+1; ++i)
			if (str1[i] == token_separator[0] && strncmp (&(str1[i]), token_separator, ts_len) == 0)
			{
				tail = i;
				break;
			}
	}

	return (start+tail < 0)? tail : start;
}

void
check_create_new_group()
{
	int tab_num = PVECTOR_USED(WinTabsState.tabs);
    ASWinTab *tabs = PVECTOR_HEAD( ASWinTab, WinTabsState.tabs );
	int i, k;
	int best_match = 0, bm_i = 0, bm_substr_len = 0;
	int group_owners_count = -1;

	for (i = 0 ; i < tab_num ; ++i)
		if (tabs[i].group_owner)
			group_owners_count = i;

LOCAL_DEBUG_CALLER_OUT ("group_owners_count = %d", i);

	for (i = 0; i < tab_num ; ++i)
	{
		if (!tabs[i].group)
		{
			int len_i = strlen (tabs[i].name);
			if (len_i >= MIN_MATCH_LENGTH)
			{
				for (k = i+1 ; k < tab_num ; ++k)
					if (!tabs[k].group && tabs[k].name_encoding == tabs[i].name_encoding)
					{
						int substr_len = find_longest_common_substring (tabs[i].name, len_i, tabs[k].name, Config->GroupNameSeparator);
						int nm = substr_len*substr_len;
						if (best_match < nm)
						{
							best_match = nm;
							bm_i = i;
							bm_substr_len = substr_len;
						}
					}
			}
		}
	}
LOCAL_DEBUG_OUT ( "bm_i = %d, bm_substr_len = %d", bm_i, bm_substr_len);
	if (best_match >= MIN_MATCH_LENGTH*MIN_MATCH_LENGTH)
	{
		ASWinTabGroup *group = safecalloc (1, sizeof(ASWinTabGroup));
		char *group_name;
		int i;
		ASWinTab *aswt;
		if (bm_substr_len < 0)
		{
			int len = strlen (tabs[bm_i].name);
			group->pattern_is_tail = True;
			group->pattern_length = -bm_substr_len;
LOCAL_DEBUG_OUT ( "len = %d", len);
LOCAL_DEBUG_OUT ( "name = \"%s\"", tabs[bm_i].name);
LOCAL_DEBUG_OUT ( "pattern = \"%s\"", tabs[bm_i].name + len +bm_substr_len);
			group->pattern = mystrdup (tabs[bm_i].name + len +bm_substr_len);
		}else
		{
			group->pattern_length = bm_substr_len;
			group->pattern = mystrndup (tabs[bm_i].name, bm_substr_len);
		}
		group->pattern_encoding = tabs[bm_i].name_encoding;
		group_name = safemalloc (group->pattern_length + 3);

		i = 0;
		if (group->pattern_is_tail)
			while (isspace (group->pattern[i])
					|| group->pattern[i] == '-'
					|| group->pattern[i] == ':' ) ++i;

		sprintf (group_name, "[%s", &(group->pattern[i]));

		i = group->pattern_length - i;
		if (!group->pattern_is_tail)
			while ( isspace(group_name[i])
					|| group_name[i] == '-'
					|| group_name[i] == ':'
			       ) --i;

		group_name[i+1] = ']';
		group_name[i+2] = '\0';

		aswt = add_tab(None, group_name, group->pattern_encoding);
		free (group_name);
		aswt->group = group;
		aswt->group_owner = True;

		/* somewhat crude code to have group buttons at the beginning of the list */
		vector_relocate_elem (WinTabsState.tabs, PVECTOR_USED(WinTabsState.tabs)-1, group_owners_count+1);

		tabs = PVECTOR_HEAD(ASWinTab, WinTabsState.tabs);
		tab_num = PVECTOR_USED(WinTabsState.tabs);

		/* now we need to add all applicable tabs to the new group */
		for (i = 0 ; i < tab_num ; ++i)
			if (!tabs[i].group && check_belong_to_group (group, tabs[i].name, tabs[i].name_encoding))
			{
				add_to_group (group, i);
				set_tab_title( &(tabs[i]) );
				if (tabs[i].client == WinTabsState.selected_client)
				{
					WinTabsState.selected_client = None;
					select_tab (i);
				}
			}
		fix_grouping_order();
	}
}

