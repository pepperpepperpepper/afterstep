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

/********************************************************************************/
/* window list menus regeneration :                                             */
/********************************************************************************/
static inline void
ASWindow2func_data (FunctionCode func, ASWindow * asw,
										FunctionData * fdata, char *scut, Bool icon_name)
{
	fdata->func = F_RAISE_IT;
	fdata->name =
			mystrdup (icon_name ? ASWIN_ICON_NAME (asw) : ASWIN_NAME (asw));
	if (!icon_name)
		fdata->name_encoding = ASWIN_NAME_ENCODING (asw);
	fdata->func_val[0] = (long)asw;
	fdata->func_val[1] = (long)asw->w;
	if (++(*scut) == ('9' + 1))
		(*scut) = 'A';							/* Next shortcut key */
	fdata->hotkey = (*scut);
}

struct ASSortMenu_Aux {
	FunctionData fdata;
	void *ref_data;
};

int compare_menu_func_data_name (const void *a, const void *b)
{
	struct ASSortMenu_Aux *aa = *(struct ASSortMenu_Aux **)a;
	struct ASSortMenu_Aux *ab = *(struct ASSortMenu_Aux **)b;

/*	LOCAL_DEBUG_OUT( "aa = %p, ab = %p", aa, ab ); */
	return strcmp (aa->fdata.name ? aa->fdata.name : "",
								 ab->fdata.name ? ab->fdata.name : "");
}


MenuData *make_desk_winlist_menu (ASWindowList * list, int desk,
																	int sort_order, Bool icon_name)
{
	char menu_name[256];
	MenuData *md;
	MenuDataItem *mdi;
	FunctionData fdata;
	char scut = '0';							/* Current short cut key */
	ASWindow **clients;
	int i, max_i;
	MinipixmapData minipixmaps[MINIPIXMAP_TypesNum] = { {0}, {0} };

	if (list == NULL)
		return NULL;;

	clients = PVECTOR_HEAD (ASWindow *, list->circulate_list);
	max_i = PVECTOR_USED (list->circulate_list);

	if (IsValidDesk (desk))
		sprintf (menu_name, "Windows on Desktop #%d", desk);
	else
		sprintf (menu_name, "Windows on All Desktops");

	if ((md = create_menu_data (menu_name)) == NULL)
		return NULL;

	memset (&fdata, 0x00, sizeof (FunctionData));
	fdata.func = F_TITLE;
	fdata.name = mystrdup (menu_name);
	add_menu_fdata_item (md, &fdata, NULL);

	if (sort_order == ASO_Alpha) {
		struct ASSortMenu_Aux **menuitems =
				safecalloc (max_i, sizeof (struct ASSortMenu_Aux *));
		int numitems = 0;

		for (i = 0; i < max_i; ++i) {
			if ((ASWIN_DESK (clients[i]) == desk || !IsValidDesk (desk))
					&& !ASWIN_HFLAGS (clients[i], AS_SkipWinList)) {
				menuitems[numitems] =
						safecalloc (1, sizeof (struct ASSortMenu_Aux));
/*				LOCAL_DEBUG_OUT( "menuitems[%d] = %p", numitems, menuitems[numitems] ); */
				menuitems[numitems]->ref_data = clients[i];
				ASWindow2func_data (F_RAISE_IT, clients[i],
														&(menuitems[numitems]->fdata), &scut,
														icon_name);
				++numitems;
			}
		}
		qsort (menuitems, numitems, sizeof (struct ASSortMenu_Aux *),
					 compare_menu_func_data_name);
		for (i = 0; i < numitems; ++i) {
			if (!get_flags (Scr.Feel.flags, WinListHideIcons))
				minipixmaps[MINIPIXMAP_Icon].image =
						get_client_icon_image (ASDefaultScr,
																	 ((ASWindow *) (menuitems[i]->ref_data))->hints, 32);
			else
				minipixmaps[MINIPIXMAP_Icon].image = NULL;
			if ((mdi =
					 add_menu_fdata_item (md, &(menuitems[i]->fdata),
																&(minipixmaps[0]))) != NULL)
				set_flags (mdi->flags, MD_ScaleMinipixmapDown);
			safefree (menuitems[i]);	/* scrubba-dub-dub */
		}
		safefree (menuitems);
	} else {											/* if( sort_order == ASO_Circulation || sort_order == ASO_Stacking ) */

		for (i = 0; i < max_i; ++i) {
			MinipixmapData minipixmaps[MINIPIXMAP_TypesNum] = { {0}
			, {0}
			};
			if ((ASWIN_DESK (clients[i]) == desk || !IsValidDesk (desk))
					&& !ASWIN_HFLAGS (clients[i], AS_SkipWinList)) {
				ASWindow2func_data (F_RAISE_IT, clients[i], &fdata, &scut,
														icon_name);
				if (!get_flags (Scr.Feel.flags, WinListHideIcons))
					minipixmaps[MINIPIXMAP_Icon].image =
							get_client_icon_image (ASDefaultScr, clients[i]->hints, 32);
				else
					minipixmaps[MINIPIXMAP_Icon].image = NULL;
				if ((mdi =
						 add_menu_fdata_item (md, &fdata, &(minipixmaps[0]))) != NULL)
					set_flags (mdi->flags, MD_ScaleMinipixmapDown);
			}
		}
	}
	return md;
}

