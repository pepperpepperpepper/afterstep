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


void check_motif_hints_sanity (MwmHints * motif_hints)
{
	if (get_flags (motif_hints->decorations, MWM_DECOR_ALL))
		motif_hints->decorations =
				MWM_DECOR_EVERYTHING & ~(motif_hints->decorations);

	/* Now I have the un-altered decor and functions, but with the
	 * ALL attribute cleared and interpreted. I need to modify the
	 * decorations that are affected by the functions */
	if (get_flags (motif_hints->flags, MWM_HINTS_FUNCTIONS)) {
		if (get_flags (motif_hints->functions, MWM_FUNC_ALL))
			motif_hints->functions =
					MWM_FUNC_EVERYTHING & ~(motif_hints->functions);
		if (!get_flags (motif_hints->functions, MWM_FUNC_RESIZE))
			motif_hints->decorations &= ~MWM_DECOR_RESIZEH;
		/* MWM_FUNC_MOVE has no impact on decorations. */
		if (!get_flags (motif_hints->functions, MWM_FUNC_MINIMIZE))
			motif_hints->decorations &= ~MWM_DECOR_MINIMIZE;
		if (!get_flags (motif_hints->functions, MWM_FUNC_MAXIMIZE))
			motif_hints->decorations &= ~MWM_DECOR_MAXIMIZE;
		/* MWM_FUNC_CLOSE has no impact on decorations. */
	}

	/* This rule is implicit, but its easier to deal with if
	 * I take care of it now */
	if (motif_hints->decorations & (MWM_DECOR_MENU | MWM_DECOR_MINIMIZE |
																	MWM_DECOR_MAXIMIZE))
		motif_hints->decorations |= MWM_DECOR_TITLE;
}

static ASFlagsXref mwm_decor_xref[] = {	/*Flag              Set if Set    ,Clear if Set,Set if Clear, Clear if Clear  */
	{MWM_DECOR_BORDER, AS_Border, 0, 0, AS_Border | AS_Frame},
	{MWM_DECOR_RESIZEH, AS_Handles, 0, 0, AS_Handles},
	{MWM_DECOR_TITLE, AS_Titlebar, 0, 0, AS_Titlebar},
	{0, 0, 0, 0, 0}
};

static ASFlagsXref mwm_decor_func_xref[] = {	/*Flag              Set if Set,  Clear if Set, Set if Clear, Clear if Clear  */
	{MWM_DECOR_RESIZEH, AS_FuncResize, 0, 0, AS_Handles},
	{MWM_DECOR_MENU, AS_FuncPopup, 0, 0, AS_FuncPopup},
	{MWM_DECOR_MINIMIZE, AS_FuncMinimize, 0, 0, AS_FuncMinimize},
	{MWM_DECOR_MAXIMIZE, AS_FuncMaximize, 0, 0, AS_FuncMaximize},
	{0, 0, 0, 0, 0}
};

static ASFlagsXref mwm_func_xref[] = {	/*Flag              Set if Set,  Clear if Set, Set if Clear, Clear if Clear  */
	{MWM_FUNC_RESIZE, AS_FuncResize, 0, 0, AS_FuncResize},
	{MWM_FUNC_MOVE, AS_FuncMove, 0, 0, AS_FuncMove},
	{MWM_FUNC_MINIMIZE, AS_FuncMinimize, 0, 0, AS_FuncMinimize},
	{MWM_FUNC_MAXIMIZE, AS_FuncMaximize, 0, 0, AS_FuncMaximize},
	{MWM_FUNC_CLOSE, AS_FuncClose | AS_FuncKill, 0, 0,
	 AS_FuncClose | AS_FuncKill},
	{0, 0, 0, 0, 0}
};

void
merge_motif_hints (ASHints * clean, ASRawHints * raw,
									 ASDatabaseRecord * db_rec, ASStatusHints * status,
									 ASFlagType what)
{
	CARD32 decor = MWM_DECOR_EVERYTHING;
	CARD32 funcs = MWM_FUNC_EVERYTHING;

	if (raw == NULL)
		return;
	if (raw->motif_hints) {
		INT32 input_mode = 0;

		if (get_flags (raw->motif_hints->flags, MWM_HINTS_INPUT_MODE))
			input_mode = raw->motif_hints->inputMode;

		if (get_flags (what, HINT_STARTUP) && status && input_mode != 0) {
			if (input_mode == MWM_INPUT_SYSTEM_MODAL) {
				status->layer = AS_LayerUrgent;
				set_flags (status->flags, AS_StartLayer);
				set_flags (status->flags, AS_StartsSticky);
			}
			if (input_mode == MWM_INPUT_FULL_APPLICATION_MODAL) {
				status->layer = AS_LayerTop;
				set_flags (status->flags, AS_StartLayer);
			}
		}

		check_motif_hints_sanity (raw->motif_hints);

		if (get_flags (raw->motif_hints->flags, MWM_HINTS_FUNCTIONS))
			funcs = raw->motif_hints->functions;
		if (get_flags (raw->motif_hints->flags, MWM_HINTS_DECORATIONS))
			decor = raw->motif_hints->decorations;

		/* finally we can apply conglomerated hints to our flags : */
		if (get_flags (what, HINT_GENERAL)) {
			decode_simple_flags (&(clean->flags), mwm_decor_xref, decor);
			LOCAL_DEBUG_OUT ("motif decor = 0x%lX, clean_flags = 0x%lX", decor,
											 clean->flags);
		}
		if (get_flags (what, HINT_PROTOCOL)) {
			decode_simple_flags (&(clean->function_mask), mwm_decor_func_xref,
													 decor);
			decode_simple_flags (&(clean->function_mask), mwm_func_xref, funcs);
		}
	}
}

Bool
client_hints2motif_hints (MwmHints * motif_hints, ASHints * hints,
													ASStatusHints * status)
{
	ASFlagType tmp;

	memset (motif_hints, 0x00, sizeof (MwmHints));

	if (status) {
		if (get_flags (status->flags, AS_StartLayer)) {
			if (status->layer == AS_LayerUrgent
					&& get_flags (status->flags, AS_StartsSticky)) {
				motif_hints->inputMode = MWM_INPUT_SYSTEM_MODAL;
				set_flags (motif_hints->flags, MWM_HINTS_INPUT_MODE);
			} else if (status->layer == AS_LayerTop) {
				motif_hints->inputMode = MWM_INPUT_SYSTEM_MODAL;
				set_flags (motif_hints->flags, MWM_INPUT_FULL_APPLICATION_MODAL);
			}
		}
	}
	/* finally we can apply conglomerated hints to our flags : */
	tmp = motif_hints->decorations;
	encode_simple_flags (&tmp, mwm_decor_xref, hints->flags);
	encode_simple_flags (&tmp, mwm_decor_func_xref, hints->function_mask);
	motif_hints->decorations = tmp;
	tmp = motif_hints->functions;
	encode_simple_flags (&tmp, mwm_func_xref, hints->function_mask);
	motif_hints->functions = tmp;

	check_motif_hints_sanity (motif_hints);

	if (motif_hints->functions != 0)
		set_flags (motif_hints->flags, MWM_HINTS_FUNCTIONS);
	if (motif_hints->decorations != 0)
		set_flags (motif_hints->flags, MWM_HINTS_DECORATIONS);

	return (motif_hints->flags != 0);
}
