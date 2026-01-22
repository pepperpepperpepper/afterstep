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

static ASFlagsXref extwm_state_xref[] = {	/*Flag                    Set if Set,  Clear if Set, Set if Clear, Clear if Clear  */
	{EXTWM_StateSticky, AS_StartsSticky, 0, 0, 0},
	{EXTWM_StateMaximizedV, AS_StartsMaximizedY, 0, 0, 0},
	{EXTWM_StateMaximizedH, AS_StartsMaximizedX, 0, 0, 0},
	{EXTWM_StateShaded, AS_StartsShaded, 0, 0, 0},
	{EXTWM_StateFullscreen, AS_Fullscreen, 0, 0, 0},
	{EXTWM_StateDemandsAttention, AS_Urgent, 0, 0, 0},
	{0, 0, 0, 0, 0}
};

static ASFlagType extwm_types_start_properties[][3] = {
	{EXTWM_TypeDesktop, AS_LayerOtherDesktop, AS_StartsSticky},
	{EXTWM_TypeDock, AS_LayerService, AS_StartsSticky},
	{EXTWM_TypeToolbar, AS_LayerNormal, 0},
	{EXTWM_TypeMenu, AS_LayerUrgent, AS_StartsSticky},
	{EXTWM_TypeDialog, AS_LayerTop, 0},
	{EXTWM_TypeNormal, AS_LayerNormal, 0},
	{EXTWM_TypeUtility, AS_LayerTop, 0},
	{EXTWM_TypeSplash, AS_LayerTop, AS_ShortLived},
	{EXTWM_TypeASModule, AS_LayerNormal, AS_StartsSticky},
	{0, 0, 0}
};

static ASFlagType extwm_states_start_properties[][3] = {
	{EXTWM_StateFullscreen, AS_LayerUrgent, AS_Fullscreen},
	{EXTWM_StateAbove, AS_LayerTop, 0},
	{EXTWM_StateBelow, AS_LayerBack, 0},
	{EXTWM_StateDemandsAttention, AS_LayerUrgent, 0},
	{0, 0, 0}
};

static ASFlagsXref extwm_type_xref[] = {	/*Flag              Set if Set,      Clear if Set,     Set if Clear,    Clear if Clear  */
	{EXTWM_TypeDesktop, AS_SkipWinList | AS_DontCirculate,
	 AS_Titlebar | AS_Handles | AS_Frame, 0, 0},
	{EXTWM_TypeDock, AS_AvoidCover, AS_Handles | AS_Frame | AS_Titlebar, 0,
	 0},
	{EXTWM_TypeToolbar, 0, AS_Handles | AS_Frame, 0, 0},
	{EXTWM_TypeMenu, AS_SkipWinList | AS_DontCirculate,
	 AS_Handles | AS_Frame, 0, 0},
	{EXTWM_TypeDialog, AS_ShortLived, 0 /*may need to remove handles */ , 0,
	 0},
	{EXTWM_TypeASModule, AS_Module, 0, 0, AS_Module},
	{0, 0, 0, 0, 0}
};

static ASFlagsXref extwm_type_func_mask[] = {	/*Flag             Set if Set,  Clear if Set,     Set if Clear  , Clear if Clear  */
	{EXTWM_TypeDesktop, 0,
	 AS_FuncResize | AS_FuncMinimize | AS_FuncMaximize | AS_FuncMove, 0, 0},
	{EXTWM_TypeDock, 0, AS_FuncResize | AS_FuncMinimize | AS_FuncMaximize, 0,
	 0},
	{EXTWM_TypeToolbar, 0, AS_FuncResize, 0, 0},
	{EXTWM_TypeMenu, 0, AS_FuncResize | AS_FuncMinimize | AS_FuncMaximize, 0,
	 0},
	{0, 0, 0, 0, 0}
};

ASFlagType extwm_state2as_state_flags (ASFlagType extwm_flags)
{
	ASFlagType as_flags = 0;

	decode_simple_flags (&as_flags, extwm_state_xref, extwm_flags);
	return as_flags;
}

static CARD32 *select_client_icon_argb (CARD32 * icon, int icon_length)
{
	int offset = 0;
	CARD32 *res = NULL;

	if (icon == NULL || icon_length <= 2)
		return NULL;
	while (res == NULL && offset + 2 < icon_length) {
		int width = icon[offset];
		int height = icon[offset + 1];
		int len = width * height;

		if (len < 0)
			break;
		if (len <= (icon_length - offset - 2))
			if (width == 48 && height == 48) {
				res = safemalloc ((2 + len) * sizeof (CARD32));
				memcpy (res, &(icon[offset]), (2 + len) * sizeof (CARD32));
			}
		offset += 2 + len;
	}
	offset = 0;
	while (res == NULL && offset + 2 < icon_length) {
		int width = icon[offset];
		int height = icon[offset + 1];
		int len = width * height;

		if (len < 0)
			break;
		if (len <= (icon_length - offset - 2))
			if (width >= 32 && height >= 32) {
				res = safemalloc ((2 + len) * sizeof (CARD32));
				memcpy (res, &(icon[offset]), (2 + len) * sizeof (CARD32));
			}
		offset += 2 + len;
	}
	if (res == NULL) {
		int width = icon[0];
		int height = icon[1];
		int size = width * height;

		icon_length -= 2;
		if (size > 0) {
			if (size + 2 > icon_length) {
				for (width = 128; width > 0; width -= 8)
					if (icon_length > width * width) {
						height = width;
						size = width * width;
						break;
					}
			}
			res = safecalloc (size + 2, sizeof (CARD32));
			memcpy (res + 2, &(icon[2]), size * sizeof (CARD32));
			res[0] = width;
			res[1] = height;
		}
	}
	return res;
}

void
merge_extwm_hints (ASHints * clean, ASRawHints * raw,
									 ASDatabaseRecord * db_rec, ASStatusHints * status,
									 ASFlagType what)
{

	register ExtendedWMHints *eh;

	if (raw == NULL)
		return;
	eh = &(raw->extwm_hints);

	if (get_flags (what, HINT_NAME)) {
		if (eh->name)
			add_name_to_list (clean, stripcpy ((const char *)(eh->name->value)),
												AS_Text_UTF8, True);
		if (eh->visible_name)
			add_name_to_list (clean,
												stripcpy ((const char *)(eh->visible_name->value)),
												AS_Text_UTF8, True);
		if (eh->icon_name)
			clean->icon_name_idx =
					add_name_to_list (clean,
														stripcpy ((const char *)(eh->
																										 icon_name->value)),
														AS_Text_UTF8, False);
		if (eh->visible_icon_name)
			clean->icon_name_idx =
					add_name_to_list (clean,
														stripcpy ((const char
																			 *)(eh->visible_icon_name->value)),
														AS_Text_UTF8, False);
		clean->icon_name =
				(clean->icon_name_idx <
				 0) ? NULL : clean->names[clean->icon_name_idx];
	}

	if (get_flags (what, HINT_STARTUP) && status != NULL) {
		if (get_flags (eh->flags, EXTWM_DESKTOP)) {
			LOCAL_DEBUG_OUT ("EXTWM Hints Desktop = 0x%X", eh->desktop);
			if (eh->desktop == 0xFFFFFFFF)
				set_flags (status->flags, AS_StartsSticky);
			else {
				status->desktop = eh->desktop;
				set_flags (status->flags, AS_StartDesktop);
			}
		}
		/* window state hints : */
		if (get_flags (eh->flags, EXTWM_StateSet)) {
			if (get_flags (eh->state_flags, EXTWM_StateModal)) {
				status->layer = AS_LayerTop;
				set_flags (status->flags, AS_StartLayer);
			}
			decode_simple_flags (&(status->flags), extwm_state_xref,
													 eh->state_flags);
		}
		/* window type hints : */
		if (get_flags
				(eh->type_flags, (EXTWM_TypeEverything & (~EXTWM_TypeNormal)))) {
			register int i;

			for (i = 0; extwm_types_start_properties[i][0] != 0; i++)
				if (get_flags (eh->type_flags, extwm_types_start_properties[i][0])) {
					if (!get_flags (status->flags, AS_StartLayer)
							|| status->layer < extwm_types_start_properties[i][1])
						status->layer = extwm_types_start_properties[i][1];
					set_flags (status->flags, AS_StartLayer);
					set_flags (status->flags, extwm_types_start_properties[i][2]);
				}
		}
		if (get_flags
				(eh->state_flags,
				 EXTWM_StateFullscreen | EXTWM_StateAbove | EXTWM_StateBelow)) {
			register int i;

			for (i = 0; extwm_states_start_properties[i][0] != 0; i++)
				if (get_flags
						(eh->state_flags, extwm_states_start_properties[i][0])) {
					if (!get_flags (status->flags, AS_StartLayer)
							|| status->layer < extwm_states_start_properties[i][1])
						status->layer = extwm_states_start_properties[i][1];
					set_flags (status->flags, AS_StartLayer);
					set_flags (status->flags, extwm_states_start_properties[i][2]);
				}
		}
	}
	if (get_flags (what, HINT_GENERAL)) {
		if (get_flags (eh->flags, EXTWM_PID)) {
			clean->pid = eh->pid;
			set_flags (clean->flags, AS_PID);
		}
		if (get_flags (eh->flags, EXTWM_WINDOW_OPACITY)) {
			clean->window_opacity = eh->window_opacity;
			set_flags (clean->flags, AS_WindowOpacity);
		}
		if (get_flags (eh->flags, EXTWM_ICON)) {
			if (clean->icon_argb)
				free (clean->icon_argb);
			clean->icon_argb =
					select_client_icon_argb (eh->icon, eh->icon_length);
			set_flags (clean->flags, AS_Icon);
			set_flags (clean->client_icon_flags,
								 AS_ClientIcon | AS_ClientIconARGB);
		}
		if (get_flags (eh->state_flags, EXTWM_StateSkipTaskbar))
			set_flags (clean->flags, AS_SkipWinList);

		if (get_flags (eh->flags, EXTWM_TypeSet)) {
			clean->extwm_window_type = eh->type_flags;
			decode_simple_flags (&(clean->flags), extwm_type_xref,
													 eh->type_flags);
		}
	}
	if (get_flags (what, HINT_PROTOCOL)) {
		if (get_flags (eh->flags, EXTWM_DoesWMPing))
			set_flags (clean->protocols, AS_DoesWmPing);
		if (get_flags (eh->flags, EXTWM_NAME) && eh->name != NULL)
			set_flags (clean->protocols, AS_NeedsVisibleName);
		if (get_flags (eh->flags, EXTWM_VISIBLE_NAME)
				&& eh->visible_name != NULL)
			set_flags (clean->protocols, AS_NeedsVisibleName);
		if (get_flags (eh->flags, EXTWM_TypeSet))
			decode_simple_flags (&(clean->function_mask), extwm_type_func_mask,
													 eh->type_flags);
	}
}

CARD32
set_hints_window_opacity_percent (ASHints * clean, int opaque_percent)
{
	CARD32 res = NET_WM_WINDOW_OPACITY_OPAQUE;

	if (opaque_percent <= 0)
		res = 0;
	else if (opaque_percent < 100)
		res = (NET_WM_WINDOW_OPACITY_OPAQUE / 100) * opaque_percent;
	if (clean) {
		clean->window_opacity = res;
		set_flags (clean->flags, AS_WindowOpacity);
	}
	return res;
}

Bool
client_hints2extwm_hints (ExtendedWMHints * extwm_hints, ASHints * hints,
													ASStatusHints * status)
{
	memset (extwm_hints, 0x00, sizeof (ExtendedWMHints));

	if (status) {
		if (get_flags (status->flags, AS_StartsSticky)) {
			extwm_hints->desktop = 0xFFFFFFFF;
			set_flags (extwm_hints->flags, EXTWM_DESKTOP);
		} else if (get_flags (status->flags, AS_StartDesktop)) {
			extwm_hints->desktop = status->desktop;
			set_flags (extwm_hints->flags, EXTWM_DESKTOP);
		}
		/* window state hints : */
		if (get_flags (status->flags, AS_StartLayer)
				&& status->layer >= AS_LayerTop) {
			set_flags (extwm_hints->state_flags, EXTWM_StateModal);
			set_flags (extwm_hints->flags, EXTWM_StateSet);
			encode_simple_flags (&(extwm_hints->state_flags), extwm_state_xref,
													 status->flags);
		}
		if (get_flags (status->flags, AS_SkipWinList)) {
			set_flags (extwm_hints->state_flags, EXTWM_StateSkipTaskbar);
			set_flags (extwm_hints->flags, EXTWM_StateSet);
		}

		/* window type hints : */
		if (get_flags (status->flags, AS_StartLayer)
				&& status->layer != AS_LayerNormal) {
			register int i;

			for (i = 0; extwm_types_start_properties[i][0] != 0; i++)
				if (status->layer == extwm_types_start_properties[i][1] &&
						(get_flags (status->flags, extwm_types_start_properties[i][2])
						 || extwm_types_start_properties[i][2] == 0)) {
					set_flags (extwm_hints->type_flags,
										 extwm_types_start_properties[i][0]);
				}
		}
	}

	encode_simple_flags (&(extwm_hints->type_flags), extwm_type_xref,
											 hints->flags);
	encode_simple_flags (&(extwm_hints->type_flags), extwm_type_func_mask,
											 hints->function_mask);
	if (extwm_hints->type_flags != 0)
		set_flags (extwm_hints->flags, EXTWM_TypeSet);

	if (hints->pid >= 0 && get_flags (hints->flags, AS_PID)) {
		set_flags (extwm_hints->flags, EXTWM_PID);
		extwm_hints->pid = hints->pid;
	}
	if (hints->icon_argb != NULL
			&& get_flags (hints->client_icon_flags, AS_ClientIcon)) {
		set_flags (extwm_hints->flags, EXTWM_ICON);
		extwm_hints->icon_length =
				hints->icon_argb[0] * hints->icon_argb[1] + 2;
		extwm_hints->icon =
				safemalloc (extwm_hints->icon_length * sizeof (CARD32));
		memcpy (extwm_hints->icon, hints->icon_argb,
						extwm_hints->icon_length * sizeof (CARD32));
	}
	if (get_flags (hints->protocols, AS_DoesWmPing))
		set_flags (extwm_hints->flags, EXTWM_DoesWMPing);

	return (extwm_hints->flags != 0);
}

