/*
 * Copyright (C) 2000 Sasha Vasko <sasha at aftercode.net>
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

#include "../configure.h"

#define LOCAL_DEBUG
#include "asapp.h"
#include "afterstep.h"
#include "asdatabase.h"
#include "screen.h"
#include "functions.h"
#include "clientprops.h"
#include "hints.h"
#include "hints_private.h"
#include "desktop_category.h"
#include "freestor.h"
#include "../libAfterImage/afterimage.h"


/*********************************************************************************
 * Raw Hints must be aggregated into usable forma :
 *********************************************************************************/
static void init_ashints (ASHints * hints)
{
	if (hints) {									/* some defaults to start with : */
		hints->flags =
				AS_HitPager | AS_Gravity | AS_AcceptsFocus | AS_Titlebar |
				AS_IconTitle | AS_Handles | AS_Border;
		/* can't gracefully close the window if it does not support WM_DELETE_WINDOW */
		hints->function_mask = ~(AS_FuncClose | AS_FuncPinMenu);
		hints->gravity = NorthWestGravity;
		hints->border_width = 1;
	}
}

void
check_hints_sanity (ScreenInfo * scr, ASHints * clean,
										ASStatusHints * status, Window client)
{
	if (clean) {
		if (clean->border_width == 0
				|| clean->border_width > scr->MyDisplayHeight / 2)
			clear_flags (clean->flags, AS_Border);

		if (clean->handle_width > scr->MyDisplayHeight / 2)
			clean->handle_width = 0;

		if (get_flags (clean->flags, AS_SizeInc)) {
			if (clean->width_inc == 0)
				clean->width_inc = 1;
			if (clean->height_inc == 0)
				clean->height_inc = 1;
		}

		if (get_flags (clean->flags, AS_Aspect))
			if ((clean->min_aspect.x == 0 || clean->min_aspect.y == 0) &&
					(clean->max_aspect.x == 0 || clean->max_aspect.y == 0))
				clear_flags (clean->flags, AS_Aspect);

		if (get_flags (clean->flags, AS_Icon))
			if (clean->icon_file == NULL && clean->icon.pixmap == None)
				clear_flags (clean->flags, AS_Icon);

		if (status && status->width <= 2 && status->height <= 2) {
			if (clean->res_class
					&& strcasecmp (clean->res_class, "DockApp") == 0)
				set_flags (clean->flags, AS_WMDockApp);
		}

		if (clean->icon.window == client) {
			clean->icon.window = None;
			clear_flags (clean->function_mask, AS_FuncMinimize);
			clear_flags (clean->client_icon_flags, AS_ClientIcon);
		}


		if (clean->frame_name == NULL)
			clear_flags (clean->flags, AS_Frame);
		if (clean->windowbox_name == NULL)
			clear_flags (clean->flags, AS_Windowbox);
	}
}

void check_status_sanity (ScreenInfo * scr, ASStatusHints * status)
{
	if (status) {
		/* we want to limit user specifyed layers to AS_LayerDesktop < layer < AS_LayerMenu
		 * which is reasonable, since you cannot be lower then Desktop and higher then Menu
		 */
		if (get_flags (status->flags, AS_StartPositionUser))
			set_flags (status->flags, AS_StartPosition);

		if (status->layer <= AS_LayerDesktop)
			status->layer = AS_LayerDesktop + 1;
		else if (status->layer >= AS_LayerMenu)
			status->layer = AS_LayerMenu - 1;

		if (status->desktop == INVALID_DESK)
			clear_flags (status->flags, AS_StartDesktop);
		LOCAL_DEBUG_OUT ("viewport_x = %d", status->viewport_x);
		status->viewport_x = FIT_IN_RANGE (0, status->viewport_x, scr->VxMax);
		LOCAL_DEBUG_OUT ("viewport_x = %d", status->viewport_x);
		status->viewport_y = FIT_IN_RANGE (0, status->viewport_y, scr->VyMax);
		if (status->width < 2)
			status->width = 2;
		if (status->height < 2)
			status->height = 2;
		if (status->border_width == 0
				|| status->border_width > scr->MyDisplayHeight / 2)
			clear_flags (status->flags, AS_StartBorderWidth);
	}
}

unsigned char get_hint_name_encoding (ASHints * hints, int name_idx)
{
	if (hints && name_idx >= 0 && name_idx <= MAX_WINDOW_NAMES)
		return hints->names_encoding[name_idx];
	return AS_Text_ASCII;
}

/* Hints merging functions : */
ASHints *merge_hints (ASRawHints * raw, ASDatabase * db,
											ASStatusHints * status, ASSupportedHints * list,
											ASFlagType what, ASHints * reusable_memory,
											Window client)
{
	ASHints *clean = NULL;
	ASDatabaseRecord db_rec, *pdb_rec;
	int i;
	ASFlagType hints_types = ASFLAGS_EVERYTHING;

	if (raw == NULL || list == NULL || what == 0)
		return NULL;
	if ((clean = reusable_memory) == NULL)
		clean = (ASHints *) safecalloc (1, sizeof (ASHints));
	else
		memset (clean, 0x00, sizeof (ASHints));
	if (status)
		memset (status, 0x00, sizeof (ASStatusHints));

	init_ashints (clean);

	/* collect the names first */
	if (get_flags (what, HINT_NAME)) {
		for (i = 0; i < list->hints_num; i++)
			(list->merge_funcs[i]) (clean, raw, NULL, status, HINT_NAME);

		what &= ~HINT_NAME;
	}
	if (clean->names[0] == NULL) {
		clean->names[0] = mystrdup ("");	/* must have at least one valid name string - even if empty */
		clean->names_encoding[0] = AS_Text_ASCII;
	}
	/* we want to make sure that we have Icon Name at all times, if at all possible */
	if (clean->icon_name == NULL) {
		clean->icon_name = clean->names[0];
		clean->icon_name_idx = 0;
	}
	/* we don't want to do anything else if all that was requested are names */
	if (what == 0)
		return clean;

	pdb_rec = fill_asdb_record (db, clean->names, &db_rec, False);

	if (clean->matched_name0)
		free (clean->matched_name0);
	clean->matched_name0 = mystrdup (clean->names[0]);
	clean->matched_name0_encoding = clean->names_encoding[0];


	LOCAL_DEBUG_OUT ("printing db record %p for names %p and db %p", pdb_rec,
									 clean->names, db);
	if (is_output_level_under_threshold (OUTPUT_LEVEL_DATABASE))
		print_asdb_matched_rec (NULL, NULL, db, pdb_rec);

	hints_types = get_asdb_hint_mask (pdb_rec);
	hints_types &=
			raw->hints_types | (0x01 << HINTS_ASDatabase) | (0x01 <<
																											 HINTS_XResources);

	/* now do the rest : */
	if (what != 0)
		for (i = 0; i < list->hints_num; i++) {	/* only do work if needed */
			if (get_flags (hints_types, (0x01 << list->hints_types[i]))) {
				(list->merge_funcs[i]) (clean, raw, pdb_rec, status, what);
				LOCAL_DEBUG_OUT ("merging hints %d (of %d ) - flags == 0x%lX", i,
												 list->hints_num, clean->flags);
			}
		}
	if (get_flags (what, HINT_STARTUP))
		merge_command_line (clean, status, raw);

	check_hints_sanity (raw->scr, clean, status, client);
	check_status_sanity (raw->scr, status);

	/* this is needed so if user changes the list of supported hints -
	 * we could track what was used, and if we need to remerge them
	 */
	clean->hints_types_raw = raw->hints_types;
	clean->hints_types_clean =
			get_flags (raw->hints_types, list->hints_flags);

	return clean;
}

/*
 * few function - shortcuts to implement update of selected hints :
 */
/* returns True if protocol/function hints actually changed :*/
Bool
update_protocols (ScreenInfo * scr, Window w, ASSupportedHints * list,
									ASFlagType * pprots, ASFlagType * pfuncs)
{
	ASRawHints raw;
	ASHints clean;
	Bool changed = False;

	if (w == None)
		return False;

	if (collect_hints (scr, w, HINT_PROTOCOL, &raw) != NULL) {
		if (merge_hints (&raw, NULL, NULL, list, HINT_PROTOCOL, &clean, w) !=
				NULL) {
			if (pprots)
				if ((changed = (*pprots != clean.protocols)))
					*pprots = clean.protocols;
			if (pfuncs)
				if ((changed = (*pfuncs != clean.function_mask)))
					*pfuncs = clean.function_mask;
			destroy_hints (&clean, True);
		}
		destroy_raw_hints (&raw, True);
	}
	return changed;
}

/* returns True if protocol/function hints actually changed :*/
Bool
update_colormaps (ScreenInfo * scr, Window w, ASSupportedHints * list,
									CARD32 ** pcmap_windows)
{
	ASRawHints raw;
	ASHints clean;
	Bool changed = False;
	register CARD32 *old_win, *new_win;


	if (w == None || pcmap_windows == NULL)
		return False;

	if (collect_hints (scr, w, HINT_COLORMAP, &raw) != NULL) {
		if (merge_hints (&raw, NULL, NULL, list, HINT_COLORMAP, &clean, w) !=
				NULL) {
			old_win = *pcmap_windows;
			new_win = clean.cmap_windows;
			changed = (old_win != new_win);
			if (new_win != NULL && old_win != NULL) {
				while (*old_win == *new_win && *old_win != None) {
					old_win++;
					new_win++;
				}
				changed = (*old_win != *new_win);
			}
			if (changed) {
				if (*pcmap_windows != NULL)
					free (*pcmap_windows);
				*pcmap_windows = clean.cmap_windows;
				clean.cmap_windows = NULL;
			}
			destroy_hints (&clean, True);
		}
		destroy_raw_hints (&raw, True);
	}
	return changed;
}


#define EXTWM_AFFECTED_STATE  (AS_StartsSticky|AS_StartsMaximizedY| \
							   AS_StartsMaximizedX|AS_StartsShaded|AS_Fullscreen)

Bool
update_property_hints (Window w, Atom property, ASHints * hints,
											 ASStatusHints * status)
{
	ASRawHints raw;
	Bool changed = False;

	if (status == NULL || hints == NULL)
		return False;
	memset (&raw, 0x00, sizeof (ASRawHints));
	raw.wm_state_icon_win = status->icon_window;
	raw.scr = ASDefaultScr;
	if (handle_client_property_update (w, property, &raw)) {
		/* Here we are only interested in properties updtaed by the Window Manager : */
		if (property == _XA_WM_STATE) {
			unsigned long new_state =
					(raw.wm_state == IconicState) ? AS_Iconic : 0;

			if ((changed = ((new_state ^ (status->flags & AS_Iconic)) != 0 ||
											raw.wm_state_icon_win != status->icon_window))) {
				status->icon_window = raw.wm_state_icon_win;
				status->flags = (status->flags & (~AS_Iconic)) | new_state;
			}
		} else if (property == _XA_WIN_LAYER) {
			if ((changed = (raw.gnome_hints.layer != status->layer)))
				status->layer = raw.gnome_hints.layer;
		} else if (property == _XA_WIN_STATE) {
			changed = decode_gnome_state (raw.gnome_hints.state, hints, status);
		} else if (property == _XA_WIN_WORKSPACE) {
			if ((changed = (raw.gnome_hints.workspace != status->desktop)))
				status->desktop = raw.gnome_hints.workspace;
		} else if (property == _XA_NET_WM_DESKTOP) {	/* Extended WM-Hints : */
			if ((changed = (raw.extwm_hints.desktop != status->desktop)))
				status->desktop = raw.extwm_hints.desktop;
		} else if (property == _XA_NET_WM_STATE) {
			ASFlagType new_state =
					extwm_state2as_state_flags (raw.extwm_hints.state_flags);

			if ((changed =
					 (((status->flags & EXTWM_AFFECTED_STATE) ^ new_state) != 0)))
				status->flags = new_state | (status->flags & EXTWM_AFFECTED_STATE);
		}
		destroy_raw_hints (&raw, True);

	}
	return changed;
}

/* same as above only for window manager : */
void
update_cmd_line_hints (Window w, Atom property, ASHints * hints,
											 ASStatusHints * status)
{
	ASRawHints raw;

	memset (&raw, 0x00, sizeof (ASRawHints));
	raw.scr = ASDefaultScr;
	show_debug (__FILE__, __FUNCTION__, __LINE__,
							"trying to handle property change for WM_COMMAND");
	if (handle_manager_property_update (w, property, &raw)) {
		merge_command_line (hints, status, &raw);
		destroy_raw_hints (&raw, True);
	}
}

/* same as above only for window manager : */
Bool
update_property_hints_manager (Window w, Atom property,
															 ASSupportedHints * list, ASDatabase * db,
															 ASHints * hints, ASStatusHints * status)
{
	ASRawHints raw;
	Bool changed = False;

	memset (&raw, 0x00, sizeof (ASRawHints));
	raw.scr = ASDefaultScr;
	if (status)
		raw.wm_state_icon_win = status->icon_window;
	show_debug (__FILE__, __FUNCTION__, __LINE__,
							"trying to handle property change");
	if (handle_manager_property_update (w, property, &raw)) {
		ASHints clean;

		memset (&clean, 0x00, sizeof (ASHints));
		clean.res_name_idx = clean.res_class_idx = clean.icon_name_idx = -1;

		show_debug (__FILE__, __FUNCTION__, __LINE__,
								"property update handled");
		if (property == _XA_WM_STATE) {
			if (status) {
				unsigned long new_state =
						(raw.wm_state == IconicState) ? AS_Iconic : 0;

				if ((changed = ((new_state ^ (status->flags & AS_Iconic)) != 0 ||
												raw.wm_state_icon_win != status->icon_window))) {
					status->icon_window = raw.wm_state_icon_win;
					status->flags = (status->flags & (~AS_Iconic)) | new_state;
				}
			}
			return changed;
		}
		if (hints
				&& merge_hints (&raw, db, NULL, list, HINT_ANY, &clean,
												w) != NULL) {
			show_debug (__FILE__, __FUNCTION__, __LINE__, "hints merged");
			if (IsNameProp (property)) {
				int i;

				if (hints->names_encoding[0] == clean.names_encoding[0] &&
						mystrcmp (hints->names[0], clean.names[0]) != 0)
					changed = True;
				else if (mystrcmp (hints->res_name, clean.res_name) != 0)
					changed = True;
				else if (mystrcmp (hints->res_class, clean.res_class) != 0)
					changed = True;
				else if (mystrcmp (hints->icon_name, clean.icon_name) != 0)
					changed = True;

				for (i = 0; i <= MAX_WINDOW_NAMES; ++i)
					if (hints->names[i] != NULL) {
						free (hints->names[i]);
						hints->names[i] = NULL;
					} else
						break;

				for (i = 0; i <= MAX_WINDOW_NAMES; ++i) {
					hints->names_encoding[i] = clean.names_encoding[i];
					hints->names[i] = clean.names[i];
					clean.names[i] = NULL;
				}
				hints->res_name = clean.res_name;
				hints->res_name_idx = clean.res_name_idx;
				hints->res_class = clean.res_class;
				hints->res_class_idx = clean.res_class_idx;
				hints->icon_name = clean.icon_name;
				hints->icon_name_idx = clean.icon_name_idx;
				show_debug (__FILE__, __FUNCTION__, __LINE__, "names set");

				/* Must not do that as it requires sugnificant changes in decorations and stuff :
				 * and should be done only when FollowTitleChanges is requested */
				/* hints->flags = clean.flags ;
				   hints->function_mask = clean.function_mask ;
				 */
			} else if (property == XA_WM_HINTS) {

			} else if (property == XA_WM_NORMAL_HINTS) {

			} else if (property == _XA_WM_PROTOCOLS) {

			} else if (property == _XA_WM_COLORMAP_WINDOWS) {
			}
		}
		destroy_hints (&clean, True);
		destroy_raw_hints (&raw, True);
	} else
		show_debug (__FILE__, __FUNCTION__, __LINE__,
								"failed to handle property update");

	return changed;
}


/*********************************************************************************
 *                  List of Supported Hints management                           *
 *********************************************************************************/
ASSupportedHints *create_hints_list ()
{
	ASSupportedHints *list;

	list = (ASSupportedHints *) safecalloc (1, sizeof (ASSupportedHints));
	return list;
}

void destroy_hints_list (ASSupportedHints ** plist)
{
	if (*plist) {
		free (*plist);
		*plist = NULL;
	}
}

static hints_merge_func HintsTypes2Func (HintsTypes type)
{
	switch (type) {
	case HINTS_ICCCM:
		return merge_icccm_hints;
	case HINTS_GroupLead:
		return merge_group_hints;
	case HINTS_Transient:
		return merge_transient_hints;
	case HINTS_Motif:
		return merge_motif_hints;
	case HINTS_Gnome:
		return merge_gnome_hints;
	case HINTS_KDE:
		return merge_kde_hints;
	case HINTS_ExtendedWM:
		return merge_extwm_hints;
	case HINTS_XResources:
		return merge_xresources_hints;
	case HINTS_ASDatabase:
		return merge_asdb_hints;
	case HINTS_Supported:
		break;
	}
	return NULL;
}

HintsTypes Func2HintsTypes (hints_merge_func func)
{
	if (func == merge_group_hints)
		return HINTS_GroupLead;
	else if (func == merge_transient_hints)
		return HINTS_Transient;
	else if (func == merge_motif_hints)
		return HINTS_Motif;
	else if (func == merge_gnome_hints)
		return HINTS_Gnome;
	else if (func == merge_extwm_hints)
		return HINTS_ExtendedWM;
	else if (func == merge_kde_hints)
		return HINTS_KDE;
	else if (func == merge_xresources_hints)
		return HINTS_XResources;
	else if (func == merge_asdb_hints)
		return HINTS_ASDatabase;
	return HINTS_ICCCM;
}

Bool enable_hints_support (ASSupportedHints * list, HintsTypes type)
{
	if (list) {
		if (list->hints_num >= HINTS_Supported)
			list->hints_num = HINTS_Supported - 1;	/* we are being paranoid */

		if (get_flags (list->hints_flags, (0x01 << type)))	/* checking for duplicates */
			return False;

		if ((list->merge_funcs[list->hints_num] =
				 HintsTypes2Func (type)) == NULL)
			return False;

		list->hints_types[list->hints_num] = type;
		set_flags (list->hints_flags, (0x01 << type));
		list->hints_num++;
		return True;
	}
	return False;
}

Bool disable_hints_support (ASSupportedHints * list, HintsTypes type)
{
	if (list) {
		register int i;

		if (list->hints_num > HINTS_Supported)
			list->hints_num = HINTS_Supported;	/* we are being paranoid */
		for (i = 0; i < list->hints_num; i++)
			if (list->hints_types[i] == type) {
				list->hints_num--;
				for (; i < list->hints_num; i++) {
					list->merge_funcs[i] = list->merge_funcs[i + 1];
					list->hints_types[i] = list->hints_types[i + 1];
				}
				list->merge_funcs[i] = NULL;
				list->hints_types[i] = HINTS_Supported;
				clear_flags (list->hints_flags, (0x01 << type));
				return True;
			}
	}
	return False;
}

HintsTypes *supported_hints_types (ASSupportedHints * list,
																	 int *num_return)
{
	HintsTypes *types = NULL;
	int curr = 0;

	if (list)
		if ((curr = list->hints_num) > 0)
			types = list->hints_types;

	if (num_return)
		*num_return = curr;
	return types;
}
