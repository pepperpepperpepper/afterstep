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
#include "freestor.h"
#include "hints.h"
#include "hints_private.h"

ASFlagType get_asdb_hint_mask (ASDatabaseRecord * db_rec)
{
	ASFlagType hint_mask = ASFLAGS_EVERYTHING;

	if (db_rec) {
		if (get_flags (db_rec->set_flags, STYLE_GROUP_HINTS)
				&& !get_flags (db_rec->flags, STYLE_GROUP_HINTS))
			clear_flags (hint_mask, (0x01 << HINTS_GroupLead));
		if (get_flags (db_rec->set_flags, STYLE_TRANSIENT_HINTS)
				&& !get_flags (db_rec->flags, STYLE_TRANSIENT_HINTS))
			clear_flags (hint_mask, (0x01 << HINTS_Transient));
		if (get_flags (db_rec->set_flags, STYLE_MOTIF_HINTS)
				&& !get_flags (db_rec->flags, STYLE_MOTIF_HINTS))
			clear_flags (hint_mask, (0x01 << HINTS_Motif));
		if (get_flags (db_rec->set_flags, STYLE_GNOME_HINTS)
				&& !get_flags (db_rec->flags, STYLE_GNOME_HINTS))
			clear_flags (hint_mask, (0x01 << HINTS_Gnome));
		if (get_flags (db_rec->set_flags, STYLE_EXTWM_HINTS)
				&& !get_flags (db_rec->flags, STYLE_EXTWM_HINTS))
			clear_flags (hint_mask, (0x01 << HINTS_ExtendedWM));
		if (get_flags (db_rec->set_flags, STYLE_KDE_HINTS)
				&& !get_flags (db_rec->flags, STYLE_KDE_HINTS))
			clear_flags (hint_mask, (0x01 << HINTS_KDE));
		if (get_flags (db_rec->set_flags, STYLE_XRESOURCES_HINTS)
				&& !get_flags (db_rec->flags, STYLE_XRESOURCES_HINTS))
			clear_flags (hint_mask, (0x01 << HINTS_XResources));
	}
	return hint_mask;
}

void
merge_asdb_hints (ASHints * clean, ASRawHints * raw,
									ASDatabaseRecord * db_rec, ASStatusHints * status,
									ASFlagType what)
{
	static ASFlagsXref asdb_startup_xref[] = {	/*Flag                    Set if Set    ,Clear if Set    , Set if Clear, Clear if Clear  */
		{STYLE_STICKY, AS_StartsSticky, 0, 0, AS_StartsSticky},
		{STYLE_START_ICONIC, AS_StartsIconic, 0, 0, AS_StartsIconic},
		{STYLE_PPOSITION, AS_StartPositionUser, 0, 0, AS_StartPosition},
		{STYLE_FULLSCREEN, AS_Fullscreen, 0, 0, AS_Fullscreen},
		{0, 0, 0, 0, 0}
	};
	static ASFlagsXref asdb_hints_xref[] = {	/*Flag                  Set if Set      ,Clear if Set    ,Set if Clear       ,Clear if Clear  */
		{STYLE_TITLE, AS_Titlebar, 0, 0, AS_Titlebar},
		{STYLE_CIRCULATE, 0, AS_DontCirculate, AS_DontCirculate, 0},
		{STYLE_WINLIST, 0, AS_SkipWinList, AS_SkipWinList, 0},
		{STYLE_ICON_TITLE, AS_IconTitle, 0, 0, AS_IconTitle},
		{STYLE_FOCUS, AS_AcceptsFocus, 0, 0, AS_AcceptsFocus},
		{STYLE_AVOID_COVER, AS_AvoidCover, 0, 0, AS_AvoidCover},
		{STYLE_VERTICAL_TITLE, AS_VerticalTitle, 0, 0, AS_VerticalTitle},
		{STYLE_HANDLES, AS_Handles, 0, 0, AS_Handles},
		{STYLE_FOCUS_ON_MAP, AS_FocusOnMap, 0, 0, AS_FocusOnMap},
		{STYLE_LONG_LIVING, 0, AS_ShortLived, AS_ShortLived, 0},
		{STYLE_IGNORE_CONFIG, AS_IgnoreConfigRequest, 0, 0,
		 AS_IgnoreConfigRequest},
		{STYLE_IGNORE_RESTACK, AS_IgnoreRestackRequest, 0, 0,
		 AS_IgnoreRestackRequest},
		{STYLE_CURRENT_VIEWPORT, AS_UseCurrentViewport, 0, 0,
		 AS_UseCurrentViewport},
		{STYLE_PAGER, AS_HitPager, 0, 0, AS_HitPager},
		{0, 0, 0, 0, 0}
	};

	LOCAL_DEBUG_CALLER_OUT ("0x%lX", what);

	if (db_rec == NULL)
		return;
	if (get_flags (what, HINT_STARTUP) && status != NULL) {
		if (get_flags (db_rec->set_data_flags, STYLE_STARTUP_DESK)) {
			status->desktop = db_rec->desk;
			set_flags (status->flags, AS_StartDesktop);
		}
		if (get_flags (db_rec->set_data_flags, STYLE_VIEWPORTX)) {
			status->viewport_x = db_rec->viewport_x;
			LOCAL_DEBUG_OUT ("viewport_x = %d", status->viewport_x);
			set_flags (status->flags, AS_StartViewportX);
		}
		if (get_flags (db_rec->set_data_flags, STYLE_VIEWPORTY)) {
			status->viewport_y = db_rec->viewport_y;
			set_flags (status->flags, AS_StartViewportY);
		}
		if (get_flags (db_rec->set_data_flags, STYLE_LAYER)) {
			status->layer = db_rec->layer;
			set_flags (status->flags, AS_StartLayer);
		}

		/*not exactly clean solution for the default geometry, but I don't see any other way : */
		if (raw != NULL
				&& get_flags (db_rec->set_data_flags, STYLE_DEFAULT_GEOMETRY)
				&& !get_flags (status->flags, AS_StartPositionUser)) {
			register ASGeometry *g = &(db_rec->default_geometry);

			if (get_flags (g->flags, WidthValue))
				status->width = g->width;
			else if (!get_flags (status->flags, AS_StartSize))	/* defaulting to the startup width */
				status->width = raw->placement.width;

			if (get_flags (g->flags, HeightValue))
				status->height = g->height;
			else if (!get_flags (status->flags, AS_StartSize))	/* defaulting to the startup height */
				status->height = raw->placement.height;

			set_flags (status->flags, AS_StartSize);

			if (get_flags (g->flags, XValue))
				status->x = get_flags (g->flags, XNegative) ?
						raw->scr->MyDisplayWidth - raw->border_width * 2 -
						status->width - g->x : g->x;
			else if (!get_flags (status->flags, AS_StartPosition))
				status->x = raw->placement.x;

			if (get_flags (g->flags, YValue))
				status->y = get_flags (g->flags, YNegative) ?
						raw->scr->MyDisplayHeight - raw->border_width * 2 -
						status->height - g->y : g->y;
			else if (!get_flags (status->flags, AS_StartPosition))
				status->y = raw->placement.y;

			if (get_flags (g->flags, YValue | XValue))
				set_flags (status->flags, AS_StartPosition | AS_StartPositionUser);
		}
		/* taking care of startup status flags : */
		decode_flags (&(status->flags), asdb_startup_xref, db_rec->set_flags,
									db_rec->flags);
	}
	if (get_flags (what, HINT_GENERAL)) {
		if (get_flags (db_rec->set_data_flags, STYLE_ICON))
			set_string_value (&(clean->icon_file), mystrdup (db_rec->icon_file),
												&(clean->flags), AS_Icon);
		/* the following should fix NoIcon handling in the database : */
		if (get_flags (db_rec->set_flags, STYLE_ICON)) {
			if (get_flags (db_rec->flags, STYLE_ICON))
				set_flags (clean->flags, AS_Icon);
			else
				clear_flags (clean->flags, AS_Icon);
		}
		if (get_flags (db_rec->set_data_flags, STYLE_BORDER_WIDTH)) {
			clean->border_width = db_rec->border_width;
			set_flags (clean->flags, AS_Border);
		}
		if (get_flags (db_rec->set_data_flags, STYLE_HANDLE_WIDTH)) {
			clean->handle_width = db_rec->resize_width;
			set_flags (clean->flags, AS_Handles);
		}
		if (get_flags (db_rec->set_data_flags, STYLE_GRAVITY)) {
			clean->gravity = db_rec->gravity;
			set_flags (clean->flags, AS_Gravity);
		}
		if (get_flags (db_rec->set_data_flags, STYLE_WINDOW_OPACITY))
			set_hints_window_opacity_percent (clean, db_rec->window_opacity);

		if (get_flags (db_rec->set_data_flags, STYLE_FRAME))
			set_string_value (&(clean->frame_name),
												mystrdup (db_rec->frame_name), &(clean->flags),
												AS_Frame);
		if (get_flags (db_rec->set_data_flags, STYLE_WINDOWBOX))
			set_string_value (&(clean->windowbox_name),
												mystrdup (db_rec->windowbox_name), &(clean->flags),
												AS_Windowbox);
		if (get_flags (db_rec->set_data_flags, STYLE_MYSTYLES)) {
			register int i;

			for (i = 0; i < BACK_STYLES; i++)
				if (db_rec->window_styles[i])
					set_string (&(clean->mystyle_names[i]),
											mystrdup (db_rec->window_styles[i]));
		}
		/* taking care of flags : */
		decode_flags (&(clean->flags), asdb_hints_xref, db_rec->set_flags,
									db_rec->flags);
		if (get_flags (db_rec->set_flags, STYLE_FOCUS)
				&& !get_flags (db_rec->flags, STYLE_FOCUS))
			clear_flags (clean->protocols, AS_DoesWmTakeFocus);

		clean->disabled_buttons = (~(db_rec->buttons)) & (db_rec->set_buttons);
	}
}
