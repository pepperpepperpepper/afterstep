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



/****************************************************************************
 * The following functions actually implement hints merging :
 ****************************************************************************/
int
add_name_to_list (ASHints * hints, char *name, unsigned char encoding,
									Bool to_front)
{
	register int i;
	char **list = hints->names;
	unsigned char *encoding_list = hints->names_encoding;

	if (name == NULL)
		return -1;

	for (i = 0; i < MAX_WINDOW_NAMES; i++) {
		if (list[i] == NULL)
			break;
		if (encoding_list[i] == encoding && strcmp (name, list[i]) == 0) {
			free (name);
			return i;
		}
	}
	if (i >= MAX_WINDOW_NAMES) {	/* tough luck - no more space */
		free (name);
		return -1;
	}

	if (to_front) {
		for (; i > 0; i--) {
			list[i] = list[i - 1];
			encoding_list[i] = encoding_list[i - 1];
		}
		if (hints->res_name_idx >= 0)
			++(hints->res_name_idx);
		if (hints->res_class_idx >= 0)
			++(hints->res_class_idx);
		if (hints->icon_name_idx >= 0)
			++(hints->icon_name_idx);
	}
	list[i] = name;
	encoding_list[i] = encoding;
	return i;
}

int pointer_name_to_index_in_list (char **list, char *name)
{
	register int i;

	if (name)
		for (i = 0; i < MAX_WINDOW_NAMES; i++) {
			if (list[i] == NULL)
				break;
			if (name == list[i])
				return i;
		}
	return MAX_WINDOW_NAMES;
}

void
decode_flags (ASFlagType * dst_flags, ASFlagsXref * xref,
							ASFlagType set_flags, ASFlagType flags)
{
	if (dst_flags == NULL || set_flags == 0)
		return;
	LOCAL_DEBUG_CALLER_OUT
			("dst_flags = %lX, set_flags = %lX, flags = 0x%lX", *dst_flags,
			 set_flags, flags);
	while ((*xref)[0] != 0) {
		ASFlagType to_set;
		ASFlagType to_clear;
		int point;

		if (get_flags (set_flags, (*xref)[0])) {
			point = (get_flags (flags, (*xref)[0])) ? 1 : 3;
			to_set = (*xref)[point];
			to_clear = (*xref)[point + 1];
			if (to_set != 0)
				set_flags (*dst_flags, to_set);
			if (to_clear != 0)
				clear_flags (*dst_flags, to_clear);
		}
		xref++;
	}
}

void
encode_flags (ASFlagType * dst_flags, ASFlagsXref * xref,
							ASFlagType set_flags, ASFlagType flags)
{
	if (dst_flags == NULL || set_flags == 0)
		return;

	while ((*xref)[0] != 0) {
		if (get_flags (set_flags, (*xref)[0])) {
			if (((flags & (*xref)[1]) == (*xref)[1])
					&& (flags & (*xref)[2]) == 0)
				set_flags (*dst_flags, (*xref)[0]);
			else if (((flags & (*xref)[3]) == (*xref)[3])
							 && (flags & (*xref)[4]) == 0)
				clear_flags (*dst_flags, (*xref)[0]);
		}
		xref++;
	}
}

/* Don't forget to Cleanup after yourself : */
void
destroy_hints (ASHints * clean, Bool reusable)
{
	if (clean) {
		register int i;

		for (i = 0; i < MAX_WINDOW_NAMES; i++)
			if (clean->names[i] == NULL)
				break;
			else
				free (clean->names[i]);

		if (clean->matched_name0)
			free (clean->matched_name0);

		if (clean->cmap_windows)
			free (clean->cmap_windows);
		if (clean->icon_file)
			free (clean->icon_file);
		if (clean->icon_argb)
			free (clean->icon_argb);
		if (clean->frame_name)
			free (clean->frame_name);
		if (clean->windowbox_name)
			free (clean->windowbox_name);
		for (i = 0; i < BACK_STYLES; i++)
			if (clean->mystyle_names[i])
				free (clean->mystyle_names[i]);

		if (clean->client_host)
			free (clean->client_host);
		if (clean->client_cmd)
			free (clean->client_cmd);

		if (reusable)								/* we are being paranoid */
			memset (clean, 0x00, sizeof (ASHints));
		else
			free (clean);
	}
}

static Bool compare_strings (char *str1, char *str2)
{
	if (str1 != NULL && str2 != NULL)
		return (strcmp (str1, str2) != 0);
	return (str1 != str2);
}

Bool compare_names (ASHints * old, ASHints * hints)
{
	register int i;

	if (old != NULL && hints != NULL) {
		for (i = 0; i < MAX_WINDOW_NAMES; i++)
			if (compare_strings (old->names[i], hints->names[i]))
				return True;
			else if (old->names[i] == NULL)
				break;
	} else if (old != hints)
		return True;

	return False;
}

ASFlagType compare_hints (ASHints * old, ASHints * hints)
{
	ASFlagType changed = 0;

	if (old != NULL && hints != NULL) {
		if (compare_strings (old->names[0], hints->names[0]))
			set_flags (changed, AS_HintChangeName);
		if (compare_strings (old->res_class, hints->res_class))
			set_flags (changed, AS_HintChangeClass);
		if (compare_strings (old->res_name, hints->res_name))
			set_flags (changed, AS_HintChangeResName);
		if (compare_strings (old->icon_name, hints->icon_name))
			set_flags (changed, AS_HintChangeIconName);
	} else if (old != hints)
		changed = AS_HintChangeEverything;
	return changed;
}

ASFlagType function2mask (int function)
{
	static ASFlagType as_function_masks[F_PIN_MENU + 1 -
																			F_WINDOW_FUNC_START] = {
		AS_FuncMove,								/* F_MOVE,               30 */
		AS_FuncResize,							/* F_RESIZE,             */
		0,													/* F_RAISE,              */
		0,													/* F_LOWER,              */
		0,													/* F_RAISELOWER,         */
		0,													/* F_PUTONTOP,           */
		0,													/* F_PUTONBACK,          */
		0,													/* F_SETLAYER,           */
		0,													/* F_TOGGLELAYER,        */
		0,													/* F_SHADE,              */
		AS_FuncKill,								/* F_DELETE,             */
		0,													/* F_DESTROY,            */
		AS_FuncClose,								/* F_CLOSE,              */
		AS_FuncMinimize,						/* F_ICONIFY,            */
		AS_FuncMaximize,						/* F_MAXIMIZE,           */
		AS_FuncMaximize,						/* F_FULLSCREEN,         */
		0,													/* F_STICK,              */
		0,													/* F_FOCUS,              */
		0,													/* F_CHANGEWINDOW_UP     */
		0,													/* F_CHANGEWINDOW_DOWN   */
		0,													/* F_GOTO_BOOKMARK     */
		0,													/* F_GETHELP,            */
		0,													/* F_PASTE_SELECTION,    */
		0,													/* F_CHANGE_WINDOWS_DESK, */
		0,													/* F_BOOKMARK_WINDOW,  */
		AS_FuncPinMenu							/* F_PIN_MENU           */
	};

	if (function == F_POPUP)
		return AS_FuncPopup;
	if (function <= F_WINDOW_FUNC_START || function > F_PIN_MENU)
		return 0;
	return as_function_masks[function - (F_WINDOW_FUNC_START + 1)];
}

/****************************************************************************
 * Initial placement/anchor management code
 ****************************************************************************/

void
constrain_size (ASHints * hints, ASStatusHints * status, int max_width,
								int max_height)
{
	int minWidth = 1, minHeight = 1;
	int xinc = 1, yinc = 1, delta;
	int baseWidth = 0, baseHeight = 0;
	int clean_width =
			status->width - (status->frame_size[FR_W] +
											 status->frame_size[FR_E]);
	int clean_height =
			status->height - (status->frame_size[FR_N] +
												status->frame_size[FR_S]);

	if (get_flags (hints->flags, AS_MinSize)) {
		if (minWidth < hints->min_width)
			minWidth = hints->min_width;
		if (minHeight < hints->min_height)
			minHeight = hints->min_height;
	} else if (get_flags (hints->flags, AS_BaseSize)) {
		if (minWidth < hints->base_width)
			minWidth = hints->base_width;
		if (minHeight < hints->base_height)
			minHeight = hints->base_height;
	}

	if (get_flags (hints->flags, AS_BaseSize)) {
		baseWidth = hints->base_width;
		baseHeight = hints->base_height;
	} else if (get_flags (hints->flags, AS_MinSize)) {
		baseWidth = minWidth;
		baseHeight = minHeight;
	}

	if (get_flags (hints->flags, AS_MaxSize)
			&& !get_flags (status->flags, AS_Fullscreen)) {
		if (max_width == 0 || max_width > hints->max_width)
			max_width = hints->max_width;
		if (max_height == 0 || max_height > hints->max_height)
			max_height = hints->max_height;
	} else {
		if (max_width == 0)
			max_width = MAX ((unsigned int)minWidth, clean_width);
		if (max_height == 0)
			max_height = MAX ((unsigned int)minHeight, clean_height);
	}
	LOCAL_DEBUG_OUT
			("base_size = %dx%d, min_size = %dx%d, curr_size = %dx%d, max_size = %dx%d",
			 baseWidth, baseHeight, minWidth, minHeight, clean_width,
			 clean_height, max_width, max_height);
	/* First, clamp to min and max values  */
	clean_width = FIT_IN_RANGE (minWidth, clean_width, max_width);
	clean_height = FIT_IN_RANGE (minHeight, clean_height, max_height);
	LOCAL_DEBUG_OUT ("clumped_size = %dx%d", clean_width, clean_height);

	/* Second, fit to base + N * inc */
	if (get_flags (hints->flags, AS_SizeInc)) {
		xinc = hints->width_inc;
		yinc = hints->height_inc;
		clean_width = (((clean_width - baseWidth) / xinc) * xinc) + baseWidth;
		clean_height =
				(((clean_height - baseHeight) / yinc) * yinc) + baseHeight;
		LOCAL_DEBUG_OUT ("inced_size = %dx%d", clean_width, clean_height);
	}

	/* Third, adjust for aspect ratio */
#define maxAspectX hints->max_aspect.x
#define maxAspectY hints->max_aspect.y
#define minAspectX hints->min_aspect.x
#define minAspectY hints->min_aspect.y
#define makemult(a,b) ((b==1) ? (a) : (((int)((a)/(b))) * (b)) )

	/* The math looks like this:

	 * minAspectX    dwidth     maxAspectX
	 * ---------- <= ------- <= ----------
	 * minAspectY    dheight    maxAspectY
	 *
	 * If that is multiplied out, then the width and height are
	 * invalid in the following situations:
	 *
	 * minAspectX * dheight > minAspectY * dwidth
	 * maxAspectX * dheight < maxAspectY * dwidth
	 *
	 */

	if (get_flags (hints->flags, AS_Aspect)) {
		if ((minAspectX * clean_height > minAspectY * clean_width)) {
			delta =
					makemult (minAspectX * clean_height / minAspectY - clean_width,
										xinc);
			if (clean_width + delta <= max_width)
				clean_width += delta;
			else {
				delta =
						makemult (clean_height - clean_width * minAspectY / minAspectX,
											yinc);
				if (clean_height - delta >= minHeight)
					clean_height -= delta;
			}
		}
		if (maxAspectX * clean_height < maxAspectY * clean_width) {
			delta =
					makemult (clean_width * maxAspectY / maxAspectX - clean_height,
										yinc);
			if (clean_height + delta <= max_height)
				clean_height += delta;
			else {
				delta =
						makemult (clean_width - maxAspectX * clean_height / maxAspectY,
											xinc);
				if (clean_width - delta >= minWidth)
					clean_width -= delta;
			}
		}
		LOCAL_DEBUG_OUT ("aspected_size = %dx%d", clean_width, clean_height);
	}
	status->width =
			clean_width + status->frame_size[FR_W] + status->frame_size[FR_E];
	status->height =
			clean_height + status->frame_size[FR_N] + status->frame_size[FR_S];
}

static int _as_gravity_offsets[11][2] = {
	{0, 0},												/* ForgetGravity */
	{-1, -1},											/* NorthWestGravity */
	{2, -1},											/* NorthGravity */
	{1, -1},											/* NorthEastGravity */
	{-1, 2},											/* WestGravity */
	{0, 0},												/* CenterGravity */
	{1, 2},												/* EastGravity */
	{-1, 1},											/* SouthWestGravity */
	{2, 1},												/* SouthGravity */
	{1, 1},												/* SouthEastGravity */
	{2, 2}												/* StaticGravity */
};

void get_gravity_offsets (ASHints * hints, int *xp, int *yp)
{
	register int g = NorthWestGravity;

	if (get_flags (hints->flags, AS_Gravity))
		g = hints->gravity;

	if (g < ForgetGravity || g > StaticGravity)
		*xp = *yp = 0;
	else {
		*xp = (int)_as_gravity_offsets[g][0];
		*yp = (int)_as_gravity_offsets[g][1];
	}
}

int
translate_asgeometry (ScreenInfo * scr, ASGeometry * asg, int *px, int *py,
											unsigned int *pwidth, unsigned int *pheight)
{
	int grav = NorthWestGravity;
	unsigned int width = 1, height = 1;

	if (scr == NULL)
		scr = ASDefaultScr;

	if (asg) {
		if (get_flags (asg->flags, XNegative)) {
			if (get_flags (asg->flags, YNegative))
				grav = SouthEastGravity;
			else
				grav = NorthEastGravity;
		} else if (get_flags (asg->flags, YNegative))
			grav = SouthWestGravity;
	}
	if (asg && get_flags (asg->flags, WidthValue)) {
		width = asg->width;
		if (pwidth)
			*pwidth = width;
	} else if (pwidth)
		width = *pwidth;

	if (asg && get_flags (asg->flags, HeightValue)) {
		height = asg->height;
		if (pheight)
			*pheight = height;
	} else if (pheight)
		height = *pheight;
	if (asg) {
		if (get_flags (asg->flags, XValue) && px) {
			if (get_flags (asg->flags, XNegative)) {
				if (asg->x <= 0)
					*px = scr->MyDisplayWidth + asg->x;
				else
					*px = scr->MyDisplayWidth - asg->x;
				*px -= width;
			} else
				*px = asg->x;
		}
		if (get_flags (asg->flags, YValue) && py) {
			if (get_flags (asg->flags, YNegative)) {
				if (asg->x <= 0)
					*py = scr->MyDisplayHeight + asg->y;
				else
					*py = scr->MyDisplayHeight - asg->y;
				*py -= height;
			} else
				*py = asg->y;
		}
	} else {
		if (px)
			*px = 0;
		if (py)
			*py = 0;
	}

	return grav;
}

int
make_anchor_pos (ASStatusHints * status, int pos, int size, int vpos,
								 int grav, int max_pos)
{																/* anchor position is always in virtual coordinates */
	int bw = 0;

	if (get_flags (status->flags, AS_StartBorderWidth))
		bw = status->border_width;

	/* position of the sticky window is stored in real coordinates */
	if (!get_flags (status->flags, AS_Sticky))
		pos += vpos;

	/* trying to place window partly on screen, unless user really wants it */
	if (!get_flags (status->flags, AS_StartPositionUser)) {
		if (pos > max_pos)
			pos = max_pos;
		else if (pos + size < 16)
			pos = 16 - size;
	}

	switch (grav) {
	case 0:											/* Center */
		pos += bw + (size >> 1);
		break;
	case 1:											/* East */
		pos += bw + size + bw;
		break;
	case 2:											/* Static */
		pos += bw;
		break;
	default:											/* West */
		break;
	}
	return pos;
}

/* reverse transformation */
int
make_status_pos (ASStatusHints * status, int pos, unsigned int size,
								 int vpos, int grav)
{																/* status position is always in real coordinates */
	unsigned int bw = 0;

	if (get_flags (status->flags, AS_StartBorderWidth))
		bw = status->border_width;

	/* position of the sticky window is stored in real coordinates */
	if (!get_flags (status->flags, AS_Sticky))
		pos -= vpos;

	switch (grav) {
	case 0:											/* Center */
		pos -= bw + (size >> 1);
		break;
	case 1:											/* East */
		pos -= bw + size + bw;
		break;
	case 2:											/* Static */
		pos -= bw;
		break;
	default:											/* West */
		break;
	}
	return pos;
}

void
make_detach_pos (ASHints * hints, ASStatusHints * status,
								 XRectangle * anchor, int *detach_x, int *detach_y)
{
	unsigned int bw = 0;
	int x = 0, y = 0;
	int grav_x, grav_y;

	if (hints == NULL || status == NULL || anchor == NULL)
		return;

	if (get_flags (status->flags, AS_StartBorderWidth))
		bw = status->border_width;

	get_gravity_offsets (hints, &grav_x, &grav_y);

	/* position of the sticky window is stored in real coordinates */
	x = anchor->x;
	y = anchor->y;
	if (!get_flags (status->flags, AS_Sticky)) {
		x -= status->viewport_x;
		y -= status->viewport_y;
	}

	if (detach_x) {
		APPLY_GRAVITY (grav_x, x, anchor->width, bw, bw);
		*detach_x = x;
	}
	if (detach_y) {
		APPLY_GRAVITY (grav_y, y, anchor->height, bw, bw);
		*detach_y = y;
	}
}

/***** New Code : *****************************************************/

/*
 * frame_size[] must be set to the real frame decoration size if window
 * is being moved by us or to border_width if moved by client.
 *
 * width and height must be that of a client
 *
 * x, y are of top left corner of the client.
 * 		North  		Central 		South 			Static
 * y	-FR_N       +height/2       +height+FR_S    +0
 *
 * 		West   		Central 		East  			Static
 * x	-FR_W       +width/2       +width+FR_E      +0
 */

void
status2anchor (XRectangle * anchor, ASHints * hints,
							 ASStatusHints * status, int vwidth, int vheight)
{
	if (get_flags (status->flags, AS_Size)) {
		int w, h;

		constrain_size (hints, status, vwidth, vheight);
		w = (int)status->width - ((int)status->frame_size[FR_W] +
															(int)status->frame_size[FR_E]);
		if (w > 0)
			anchor->width = w;
		h = (int)status->height - ((int)status->frame_size[FR_N] +
															 (int)status->frame_size[FR_S]);
		if (h > 0)
			anchor->height = h;
	}

	if (get_flags (status->flags, AS_Position)) {
		int grav_x = -1, grav_y = -1;
		int offset;

		get_gravity_offsets (hints, &grav_x, &grav_y);

		LOCAL_DEBUG_OUT
				("grav_x = %d, width = %d, bw1 = %d, bw2 = %d, status_x = %d",
				 grav_x, anchor->width, status->frame_size[FR_W],
				 status->frame_size[FR_E], status->x);
		offset = 0;
		APPLY_GRAVITY (grav_x, offset, anchor->width,
									 status->frame_size[FR_W] + status->frame_border_width,
									 status->frame_size[FR_E] + status->frame_border_width);
		anchor->x = status->x - offset;

		LOCAL_DEBUG_OUT
				("grav_y = %d, height = %d, bw1 = %d, bw2 = %d, status_y = %d",
				 grav_y, anchor->height, status->frame_size[FR_N],
				 status->frame_size[FR_S], status->y);

		offset = 0;
		APPLY_GRAVITY (grav_y, offset, anchor->height,
									 status->frame_size[FR_N] + status->frame_border_width,
									 status->frame_size[FR_S] + status->frame_border_width);
		anchor->y = status->y - offset;

		LOCAL_DEBUG_OUT ("anchor = %+d%+d", anchor->x, anchor->y);
		if (!get_flags (status->flags, AS_Sticky)) {
			anchor->x += (int)status->viewport_x;
			anchor->y += (int)status->viewport_y;
		}
		LOCAL_DEBUG_OUT ("anchor = %+d%+d", anchor->x, anchor->y);
	}
}

void
anchor2status (ASStatusHints * status, ASHints * hints,
							 XRectangle * anchor)
{
	int grav_x = -1, grav_y = -1;
	int offset;

	status->width =
			anchor->width + status->frame_size[FR_W] + status->frame_size[FR_E];
	status->height =
			anchor->height + status->frame_size[FR_N] + status->frame_size[FR_S];
	set_flags (status->flags, AS_Size);

	get_gravity_offsets (hints, &grav_x, &grav_y);

	LOCAL_DEBUG_OUT
			("grav_x = %d, width = %d, bw1 = %d, bw2 = %d, anchor_x = %d",
			 grav_x, anchor->width, status->frame_size[FR_W],
			 status->frame_size[FR_E], anchor->x);
	offset = 0;
	APPLY_GRAVITY (grav_x, offset, anchor->width,
								 status->frame_size[FR_W] + status->frame_border_width,
								 status->frame_size[FR_E] + status->frame_border_width);
	status->x = anchor->x + offset;

	LOCAL_DEBUG_OUT
			("grav_y = %d, height = %d, bw1 = %d, bw2 = %d, anchor_y = %d",
			 grav_y, anchor->height, status->frame_size[FR_N],
			 status->frame_size[FR_S], anchor->y);

	offset = 0;
	APPLY_GRAVITY (grav_y, offset, anchor->height,
								 status->frame_size[FR_N] + status->frame_border_width,
								 status->frame_size[FR_S] + status->frame_border_width);
	status->y = anchor->y + offset;

	LOCAL_DEBUG_OUT ("status = %+d%+d", status->x, status->y);
	if (!get_flags (status->flags, AS_Sticky)) {
		status->x -= (int)status->viewport_x;
		status->y -= (int)status->viewport_y;
	}
	LOCAL_DEBUG_OUT ("status = %+d%+d", status->x, status->y);
	set_flags (status->flags, AS_Position);
}


/***** Old Code : *****************************************************/

ASFlagType
change_placement (ScreenInfo * scr, ASHints * hints,
									ASStatusHints * status, XPoint * anchor,
									ASStatusHints * new_status, int vx, int vy,
									ASFlagType what)
{
	ASFlagType todo = 0;
	int grav_x, grav_y;
	register int max_x = scr->MyDisplayWidth - 16;
	register int max_y = scr->MyDisplayHeight - 16;
	int new_x, new_y;

	if (!get_flags (what, AS_Size | AS_Position) || status == NULL
			|| new_status == NULL)
		return 0;

	new_x = status->x - (vx - status->viewport_x);
	new_y = status->y - (vy - status->viewport_y);
	get_gravity_offsets (hints, &grav_x, &grav_y);
	if (!get_flags (status->flags, AS_Sticky)) {
		max_x += scr->VxMax;
		max_y += scr->VyMax;
	}

	if (get_flags (what, AS_Size)) {
		constrain_size (hints, new_status, scr->VxMax + scr->MyDisplayWidth,
										scr->VyMax + scr->MyDisplayHeight);
		if (new_status->width != status->width) {
			status->width = new_status->width;
			set_flags (todo, TODO_RESIZE_X);
			if (grav_x == 0 || grav_x == 1)
				set_flags (todo, TODO_MOVE_X);
		}
		if (new_status->height != status->height) {
			status->height = new_status->height;
			set_flags (todo, TODO_RESIZE_Y);
			if (grav_y == 0 || grav_y == 1)
				set_flags (todo, TODO_MOVE_Y);
		}
	}

	if (!get_flags (what, AS_BorderWidth)
			&& new_status->border_width != status->border_width) {
		status->border_width = new_status->border_width;
		set_flags (todo, TODO_MOVE);
	}

	if (!get_flags (status->flags, AS_Sticky)
			&& (status->viewport_x != vx || status->viewport_y != vy))
		set_flags (todo, TODO_MOVE);

	if (get_flags (what, AS_Position)) {
		if (status->x != new_status->x) {
			new_x = new_status->x;
			set_flags (todo, TODO_MOVE_X);
		} else
				if (make_anchor_pos
						(status, new_x, status->width, vx, max_x, grav_x) != anchor->x)
			set_flags (todo, TODO_MOVE_X);

		if (status->y != new_status->y) {
			new_y = new_status->y;
			set_flags (todo, TODO_MOVE_Y);
		} else
				if (make_anchor_pos
						(status, new_y, status->height, vy, max_y,
						 grav_y) != anchor->y)
			set_flags (todo, TODO_MOVE_Y);
	}
	if (get_flags (todo, TODO_MOVE_X)) {
		anchor->x =
				make_anchor_pos (status, new_x, status->width, vx, max_x, grav_x);
		status->x =
				make_status_pos (status, anchor->x, status->width, vx, grav_x);
	}
	if (get_flags (todo, TODO_MOVE_Y)) {
		anchor->y =
				make_anchor_pos (status, new_y, status->height, vy, max_y, grav_y);
		status->y =
				make_status_pos (status, anchor->y, status->height, vy, grav_y);
	}
	if (!get_flags (status->flags, AS_Sticky)) {
		status->viewport_x = vx;
		status->viewport_y = vy;
	}

	return what;
}

int
calculate_viewport (int *ppos, int size, int scr_vpos, int scr_size,
										int max_viewport)
{
	int viewport = -1;
	int pos = ppos ? *ppos : 0;

	if (pos >= scr_size)
		viewport = pos / scr_size;
	else if (pos + size < 0) {
		if (pos + scr_vpos > 0)
			viewport = (scr_vpos + pos) / scr_size;
		else
			viewport = 0;
	} else
		return scr_vpos;

	viewport *= scr_size;
	viewport = MIN (viewport, max_viewport);
	if (ppos)
		*ppos = pos + (scr_vpos - viewport);
	return viewport;
}

int
gravitate_position (int pos, unsigned int size, unsigned int scr_size,
										int grav, unsigned int bw)
{
	if (pos < 0 || pos + size > scr_size)
		return pos;
	if (grav == 1)								/* East or South gravity */
		pos = (int)scr_size - (int)(pos + bw + size + bw);

	return pos;
}

