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

static void merge_parent_hints (ASParentHints * hints, ASStatusHints * status)
{
	if (get_flags (hints->flags, AS_StartDesktop))
		status->desktop = hints->desktop;
	if (get_flags (hints->flags, AS_StartViewportX))
		status->viewport_x = hints->viewport_x;
	if (get_flags (hints->flags, AS_StartViewportY))
		status->viewport_y = hints->viewport_y;
	set_flags (status->flags, hints->flags);
}

void
merge_icccm_hints (ASHints * clean, ASRawHints * raw,
									 ASDatabaseRecord * db_rec, ASStatusHints * status,
									 ASFlagType what)
{
	if (raw == NULL)
		return;
	if (get_flags (what, HINT_NAME)) {	/* adding all the possible window names in order of increasing importance */
		if (raw->wm_name)
			add_name_to_list (clean, text_property2string (raw->wm_name),
												AS_Text_ASCII, True);
		if (raw->wm_icon_name) {
			clean->icon_name_idx =
					add_name_to_list (clean,
														text_property2string (raw->wm_icon_name),
														AS_Text_ASCII, False);
			clean->icon_name =
					(clean->icon_name_idx <
					 0) ? NULL : clean->names[clean->icon_name_idx];
		}
		if (raw->wm_class) {
			if (raw->wm_class->res_class) {
				clean->res_class_idx =
						add_name_to_list (clean, stripcpy (raw->wm_class->res_class),
															AS_Text_ASCII, False);
				clean->res_class =
						(clean->res_class_idx <
						 0) ? NULL : clean->names[clean->res_class_idx];
			}
			if (raw->wm_class->res_name) {
				clean->res_name_idx =
						add_name_to_list (clean, stripcpy (raw->wm_class->res_name),
															AS_Text_ASCII, False);
				clean->res_name =
						(clean->res_name_idx <
						 0) ? NULL : clean->names[clean->res_name_idx];
			}
		}
	}

	if (get_flags (what, HINT_STARTUP) && status != NULL) {
		Bool position_user = False;

		if (raw->wm_hints) {
			if (get_flags (raw->wm_hints->flags, StateHint)) {
				if (raw->wm_hints->input == IconicState)
					set_flags (status->flags, AS_StartsIconic);
				else
					clear_flags (status->flags, AS_StartsIconic);
			}
			if (get_flags (raw->wm_hints->flags, UrgencyHint)) {
				status->layer = AS_LayerUrgent;
				set_flags (status->flags, AS_StartLayer);
				set_flags (status->flags, AS_Urgent);
			}
		}

		if (raw->wm_normal_hints)
			position_user =
					(get_flags (raw->wm_normal_hints->flags, USPosition));

		if (!get_flags (status->flags, AS_StartPositionUser) || position_user) {
			status->x = raw->placement.x;
			status->y = raw->placement.y;
			set_flags (status->flags, AS_StartPosition);
			if (position_user)
				set_flags (status->flags, AS_StartPositionUser);
		}
		status->width = raw->placement.width;
		status->height = raw->placement.height;
		set_flags (status->flags, AS_StartSize);
		status->border_width = raw->border_width;
		set_flags (status->flags, AS_StartBorderWidth);
		if (status->border_width > 0) {
			clean->border_width = status->border_width;
			set_flags (clean->flags, AS_Border);
		}

		if (raw->wm_state == IconicState)
			set_flags (status->flags, AS_StartsIconic);
		if (raw->wm_state_icon_win != None)
			status->icon_window = raw->wm_state_icon_win;
	}

	if (get_flags (what, HINT_GENERAL)) {
		if (raw->wm_hints) {
			XWMHints *wmh = raw->wm_hints;

			if (get_flags (wmh->flags, InputHint)) {
				if (wmh->input)
					set_flags (clean->flags, AS_AcceptsFocus);
				else
					clear_flags (clean->flags, AS_AcceptsFocus);
			}

			if (get_flags (wmh->flags, IconWindowHint)
					&& wmh->icon_window != None) {
				clean->icon.window = wmh->icon_window;
				set_flags (clean->client_icon_flags, AS_ClientIcon);
				clear_flags (clean->client_icon_flags, AS_ClientIconPixmap);
				if (get_flags (wmh->flags, IconWindowIsChildHint))
					set_flags (clean->flags, AS_WMDockApp);
			} else if (get_flags (wmh->flags, IconPixmapHint)
								 && wmh->icon_pixmap != None) {
				clean->icon.pixmap = wmh->icon_pixmap;
				if (get_flags (wmh->flags, IconMaskHint) && wmh->icon_mask != None)
					clean->icon_mask = wmh->icon_mask;
				set_flags (clean->client_icon_flags, AS_ClientIcon);
				set_flags (clean->client_icon_flags, AS_ClientIconPixmap);
			}

			if (get_flags (wmh->flags, IconPositionHint)) {
				clean->icon_x = wmh->icon_x;
				clean->icon_y = wmh->icon_y;
				set_flags (clean->client_icon_flags, AS_ClientIconPosition);
			}
		}

		if (raw->wm_normal_hints) {
			XSizeHints *nh = raw->wm_normal_hints;

			if (get_flags (nh->flags, PMinSize)) {
				clean->min_width = nh->min_width;
				clean->min_height = nh->min_height;
				set_flags (clean->flags, AS_MinSize);
			}
			if (get_flags (nh->flags, PMaxSize)) {
				clean->max_width = nh->max_width;
				clean->max_height = nh->max_height;
				set_flags (clean->flags, AS_MaxSize);
			}
			if (get_flags (nh->flags, PResizeInc)) {
				clean->width_inc = nh->width_inc;
				clean->height_inc = nh->height_inc;
				set_flags (clean->flags, AS_SizeInc);
			}
			if (get_flags (nh->flags, PAspect)) {
				clean->min_aspect.x = nh->min_aspect.x;
				clean->min_aspect.y = nh->min_aspect.y;
				clean->max_aspect.x = nh->max_aspect.x;
				clean->max_aspect.y = nh->max_aspect.y;
				set_flags (clean->flags, AS_Aspect);
			}
			if (get_flags (nh->flags, PBaseSize)) {
				clean->base_width = nh->base_width;
				clean->base_height = nh->base_height;
				set_flags (clean->flags, AS_BaseSize);
			}
			if (get_flags (nh->flags, PWinGravity)) {
				clean->gravity = nh->win_gravity;
				set_flags (clean->flags, AS_Gravity);
			}
		}

	}

	if (get_flags (what, HINT_COLORMAP)) {
		if (raw->wm_cmap_windows && raw->wm_cmap_win_count > 0) {
			register int i;

			if (clean->cmap_windows)
				free (clean->cmap_windows);
			clean->cmap_windows =
					safecalloc (raw->wm_cmap_win_count + 1, sizeof (CARD32));
			for (i = 0; i < raw->wm_cmap_win_count; i++)
				clean->cmap_windows[i] = raw->wm_cmap_windows[i];
			clean->cmap_windows[i] = None;
		}
	}

	if (get_flags (what, HINT_PROTOCOL)) {
		clean->protocols |= raw->wm_protocols;
		if (get_flags (clean->protocols, AS_DoesWmDeleteWindow))
			set_flags (clean->function_mask, AS_FuncClose);
	}
}

void
merge_group_hints (ASHints * clean, ASRawHints * raw,
									 ASDatabaseRecord * db_rec, ASStatusHints * status,
									 ASFlagType what)
{
	if (raw == NULL)
		return;
	if (raw->group_leader) {
		if (get_flags (what, HINT_STARTUP) && status != NULL)
			merge_parent_hints (raw->group_leader, status);

		if (get_flags (what, HINT_GENERAL))
			clean->group_lead = raw->group_leader->parent;
	}
}

void
merge_transient_hints (ASHints * clean, ASRawHints * raw,
											 ASDatabaseRecord * db_rec, ASStatusHints * status,
											 ASFlagType what)
{
	if (raw == NULL)
		return;
	if (raw->transient_for) {
		if (get_flags (what, HINT_STARTUP) && status != NULL)
			merge_parent_hints (raw->transient_for, status);

		if (get_flags (what, HINT_GENERAL)) {
			clean->transient_for = raw->transient_for->parent;
			set_flags (clean->flags, AS_Transient);
			set_flags (clean->flags, AS_ShortLived);
		}
	}
}

