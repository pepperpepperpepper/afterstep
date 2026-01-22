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

static ASFlagsXref gnome_state_xref[] = {	/*Flag                    Set if Set,  Clear if Set, Set if Clear, Clear if Clear  */
	{WIN_STATE_STICKY, AS_StartsSticky, 0, 0, 0},
	{WIN_STATE_MINIMIZED, AS_StartsIconic, 0, 0, 0},
	{WIN_STATE_MAXIMIZED_VERT, AS_StartsMaximizedY, 0, 0, 0},
	{WIN_STATE_MAXIMIZED_HORIZ, AS_StartsMaximizedX, 0, 0, 0},
	{WIN_STATE_HIDDEN, 0, 0, 0, 0},
	{WIN_STATE_SHADED, AS_StartsShaded, 0, 0, 0},
	{WIN_STATE_HID_WORKSPACE, 0, 0, 0, 0},
	{WIN_STATE_HID_TRANSIENT, 0, 0, 0, 0},
	{WIN_STATE_FIXED_POSITION, 0, 0, 0, 0},
	{WIN_STATE_ARRANGE_IGNORE, 0, 0, 0, 0},
	{0, 0, 0, 0, 0}
};

static ASFlagsXref gnome_hints_xref[] = {	/*Flag                    Set if Set,  Clear if Set, Set if Clear, Clear if Clear  */
	{WIN_HINTS_SKIP_FOCUS, 0, AS_AcceptsFocus, 0, 0},
	{WIN_HINTS_SKIP_WINLIST, AS_SkipWinList, 0, 0, 0},
	{WIN_HINTS_SKIP_TASKBAR, AS_SkipWinList, 0, 0, 0},
	{WIN_HINTS_GROUP_TRANSIENT, 0, 0, 0, 0},
	{WIN_HINTS_FOCUS_ON_CLICK, AS_ClickToFocus, 0, 0, 0},
	{0, 0, 0, 0, 0}
};

#define GNOME_AFFECTED_STATE  (AS_StartsSticky|AS_StartsIconic| \
							   AS_StartsMaximizedY|AS_StartsMaximizedX| \
							   AS_StartsShaded)

Bool
decode_gnome_state (ASFlagType state, ASHints * clean,
										ASStatusHints * status)
{
	Bool changed = False;
	ASFlagType new_state = 0, disable_func = 0;

	decode_simple_flags (&new_state, gnome_state_xref, state);
	if ((new_state ^ (status->flags & GNOME_AFFECTED_STATE)) != 0) {
		changed = True;
		status->flags = new_state | (status->flags & ~GNOME_AFFECTED_STATE);
	}
	if (get_flags (new_state, WIN_STATE_FIXED_POSITION))
		disable_func = AS_FuncMove;

	if ((disable_func ^ (clean->function_mask & AS_FuncMove)) != 0) {
		changed = True;
		clean->function_mask =
				disable_func | (clean->function_mask & ~AS_FuncMove);
	}
	return changed;
}

static ASFlagType
encode_gnome_state (ASHints * clean, ASStatusHints * status)
{
	ASFlagType new_state = 0;

	if (status)
		encode_simple_flags (&new_state, gnome_state_xref, status->flags);

	if (get_flags (clean->function_mask, AS_FuncMove))
		set_flags (new_state, WIN_STATE_FIXED_POSITION);

	return new_state;
}

void
merge_gnome_hints (ASHints * clean, ASRawHints * raw,
									 ASDatabaseRecord * db_rec, ASStatusHints * status,
									 ASFlagType what)
{
	register GnomeHints *gh;

	if (raw == NULL)
		return;
	gh = &(raw->gnome_hints);
	if (gh->flags == 0)
		return;

	if (get_flags (what, HINT_STARTUP) && status != NULL) {
		if (get_flags (gh->flags, GNOME_LAYER)) {
			status->layer = gh->layer;	/*No clue why we used to do that  :  (gh->layer - WIN_LAYER_NORMAL) >> 1; */
			set_flags (status->flags, AS_StartLayer);
		}
		if (get_flags (gh->flags, GNOME_WORKSPACE)) {
			status->desktop = gh->workspace;
			set_flags (status->flags, AS_StartDesktop);
		}
		if (get_flags (gh->flags, GNOME_STATE) && gh->state != 0) {
			decode_gnome_state (gh->state, clean, status);
		}
	}

	if (get_flags (what, HINT_GENERAL)) {
		if (get_flags (gh->flags, GNOME_HINTS) && gh->hints != 0) {
			decode_simple_flags (&(clean->flags), gnome_hints_xref, gh->hints);
			if (get_flags (gh->hints, WIN_HINTS_SKIP_FOCUS))
				clear_flags (clean->protocols, AS_DoesWmTakeFocus);
		}
	}
}

Bool
client_hints2gnome_hints (GnomeHints * gnome_hints, ASHints * hints,
													ASStatusHints * status)
{
	ASFlagType tmp = 0;

	memset (gnome_hints, 0x00, sizeof (GnomeHints));

	if (status) {
		if (get_flags (status->flags, AS_StartLayer)) {
			gnome_hints->layer = (status->layer << 1) + WIN_LAYER_NORMAL;
			set_flags (gnome_hints->flags, GNOME_LAYER);
		}

		if (get_flags (status->flags, AS_StartDesktop)) {
			gnome_hints->workspace = status->desktop;
			set_flags (gnome_hints->flags, GNOME_WORKSPACE);
		}
	}
	gnome_hints->state = encode_gnome_state (hints, status);
	if (gnome_hints->state != 0)
		set_flags (gnome_hints->flags, GNOME_STATE);

	tmp = gnome_hints->hints;
	encode_simple_flags (&tmp, gnome_hints_xref, hints->flags);
	gnome_hints->hints = tmp;
	if (gnome_hints->hints != 0)
		set_flags (gnome_hints->flags, GNOME_HINTS);

	return (gnome_hints->flags != 0);
}

