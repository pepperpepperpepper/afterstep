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

static int pointer_name_to_index_in_list (char **list, char *name)
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

/***********************************************************************************
 * we build a command line here, so we can restart an app with exactly the same
 * parameters:
 ***********************************************************************************/
static char *format_geometry_string (int x, int y, int width, int height,
																		 int unit_width, int unit_height,
																		 int screen_size_x, int screen_size_y,
																		 int gravity)
{
	char *g =
			safemalloc (15 + 1 + 15 + 1 + 15 + 1 + 15 + 1 /* large enough */ );
	char x_sign = '+';
	char y_sign = '+';
	int x2 = x + width, y2 = y + height;

#define FGS_CHECK_SIGN(d)  do{if(d<0){d = 0;/* d##_sign = '-';*/} }while(0)
#define FGS_APPLY_NEGATIVE_GRAV(d)  do{ d = screen_size_##d - d##2; if(d<0) d=0; d##_sign = '-' ;}while(0)
	if (gravity == SouthWestGravity) {
		FGS_APPLY_NEGATIVE_GRAV (y);
		FGS_CHECK_SIGN (x);
	} else if (gravity == SouthEastGravity) {
		FGS_APPLY_NEGATIVE_GRAV (x);
		FGS_APPLY_NEGATIVE_GRAV (y);
	} else if (gravity == NorthEastGravity) {
		FGS_APPLY_NEGATIVE_GRAV (x);
		FGS_CHECK_SIGN (y);
	} else {
		FGS_CHECK_SIGN (x);
		FGS_CHECK_SIGN (y);
	}
#undef FGS_CHECK_SIGN
#undef FGS_APPLY_NEGATIVE_GRAV

	sprintf (g, "%dx%d%c%d%c%d ", unit_width, unit_height, x_sign, x, y_sign,
					 y);
	return g;
}

char *make_client_geometry_string (ScreenInfo * scr, ASHints * hints,
																	 ASStatusHints * status,
																	 XRectangle * anchor, int vx, int vy,
																	 char **pure_geometry)
{
	char *geom = NULL;
	int detach_x, detach_y;
	int grav_x, grav_y;
	int bw = 0;
	int width, height, unit_width, unit_height;

	if (hints == NULL || status == NULL || anchor == NULL)
		return NULL;

	if (get_flags (status->flags, AS_StartBorderWidth))
		bw = status->border_width;

	make_detach_pos (hints, status, anchor, &detach_x, &detach_y);

	vx = calculate_viewport (&detach_x, anchor->width, vx,
													 scr->MyDisplayWidth, scr->VxMax);
	vy = calculate_viewport (&detach_y, anchor->height, vy,
													 scr->MyDisplayHeight, scr->VyMax);

	get_gravity_offsets (hints, &grav_x, &grav_y);

	detach_x =
			gravitate_position (detach_x, anchor->width, bw, scr->MyDisplayWidth,
													grav_x);
	detach_y =
			gravitate_position (detach_y, anchor->height, bw,
													scr->MyDisplayHeight, grav_y);

	width = anchor->width;
	height = anchor->height;

	unit_width =
			(hints->width_inc >
			 0) ? (width - hints->base_width) / hints->width_inc : width;
	unit_height =
			(hints->height_inc >
			 0) ? (height - hints->base_height) / hints->height_inc : height;

	if (pure_geometry) {
		*pure_geometry =
				format_geometry_string (detach_x, detach_y, width, height, width,
																height, scr->MyDisplayWidth,
																scr->MyDisplayHeight, hints->gravity);
	}
	geom =
			format_geometry_string (detach_x, detach_y, width, height,
															unit_width, unit_height, scr->MyDisplayWidth,
															scr->MyDisplayHeight, hints->gravity);;
	return geom;
}

char *make_client_command (ScreenInfo * scr, ASHints * hints,
													 ASStatusHints * status, XRectangle * anchor,
													 int vx, int vy)
{
	char *client_cmd = NULL;
	char *geom =
			make_client_geometry_string (scr, hints, status, anchor, vx, vy,
																	 NULL);

	if (hints->client_cmd == NULL || geom == NULL)
		return NULL;

	/* supplying everything as : -xrm "afterstep*desk:N" */
	client_cmd =
			safemalloc (strlen (hints->client_cmd) + 11 + strlen (geom) + 1 + 1);
	sprintf (client_cmd, "%s -geometry %s ", hints->client_cmd, geom);
	/*, status->desktop, status->layer, status->viewport_x, status->viewport_y */
	return client_cmd;
}

/***********************************************************************
 * Setrting Hints on the window :
 ***********************************************************************/
static Bool
client_hints2wm_hints (XWMHints * wm_hints, ASHints * hints,
											 ASStatusHints * status)
{
	memset (wm_hints, 0x00, sizeof (XWMHints));

	if (status) {
		wm_hints->flags = StateHint;
		if (get_flags (status->flags, AS_StartsIconic))
			wm_hints->initial_state = IconicState;
		else
			wm_hints->initial_state = WithdrawnState;
	}
	/* does this application rely on the window manager to get keyboard input? */
	if (get_flags (hints->flags, AS_AcceptsFocus)) {
		set_flags (wm_hints->flags, InputHint);
		wm_hints->input = True;
	}

	/* window to be used as icon */
	if (get_flags (hints->client_icon_flags, AS_ClientIcon)) {
		if (!get_flags (hints->client_icon_flags, AS_ClientIconPixmap)) {
			wm_hints->icon_window = hints->icon.window;
			set_flags (wm_hints->flags, IconWindowHint);
		} else {										/* pixmap to be used as icon */
			set_flags (wm_hints->flags, IconPixmapHint);
			wm_hints->icon_pixmap = hints->icon.pixmap;

			if (hints->icon_mask) {		/* pixmap to be used as mask for icon_pixmap */
				set_flags (wm_hints->flags, IconMaskHint);
				wm_hints->icon_mask = hints->icon_mask;
			}
		}
	}

	/* initial position of icon */
	if (get_flags (hints->client_icon_flags, AS_ClientIconPosition)) {
		set_flags (wm_hints->flags, IconPositionHint);
		wm_hints->icon_x = hints->icon_x;
		wm_hints->icon_y = hints->icon_y;
	}

	if (hints->group_lead) {
		set_flags (wm_hints->flags, WindowGroupHint);
		wm_hints->window_group = hints->group_lead;
	}

/*	if( get_flags( hints->flags, AS_AvoidCover ) )
		set_flags( wm_hints->flags, UrgencyHint );
 */
	return (wm_hints->flags != StateHint
					|| wm_hints->initial_state == IconicState);
}

static Bool
client_hints2size_hints (XSizeHints * size_hints, ASHints * hints,
												 ASStatusHints * status)
{
	memset (size_hints, 0x00, sizeof (XSizeHints));
	if (status) {
		if (get_flags (status->flags, AS_StartPosition | AS_StartPositionUser)) {
			if (get_flags (status->flags, AS_StartPositionUser))
				set_flags (size_hints->flags, USPosition);
			else
				set_flags (size_hints->flags, PPosition);
		}

		if (get_flags (status->flags, AS_StartSize | AS_StartSizeUser)) {
			if (get_flags (status->flags, AS_StartSizeUser))
				set_flags (size_hints->flags, USSize);
			else
				set_flags (size_hints->flags, PSize);
		}
	}
	if (get_flags (hints->flags, AS_MinSize)) {
		size_hints->min_width = hints->min_width;
		size_hints->min_height = hints->min_height;
		set_flags (size_hints->flags, PMinSize);
	}
	if (get_flags (hints->flags, AS_MaxSize)) {
		size_hints->max_width = hints->max_width;
		size_hints->max_height = hints->max_height;
		set_flags (size_hints->flags, PMaxSize);
	}
	if (get_flags (hints->flags, AS_SizeInc)) {
		size_hints->width_inc = hints->width_inc;
		size_hints->height_inc = hints->height_inc;
		set_flags (size_hints->flags, PResizeInc);
	}
	if (get_flags (hints->flags, AS_Aspect)) {
		size_hints->min_aspect.x = hints->min_aspect.x;
		size_hints->min_aspect.y = hints->min_aspect.y;
		size_hints->max_aspect.x = hints->max_aspect.x;
		size_hints->max_aspect.y = hints->max_aspect.y;
		set_flags (size_hints->flags, PAspect);
	}
	if (get_flags (hints->flags, AS_BaseSize)) {
		size_hints->base_width = hints->base_width;
		size_hints->base_height = hints->base_height;
		set_flags (size_hints->flags, PBaseSize);
	}
	if (get_flags (hints->flags, AS_Gravity)) {
		size_hints->win_gravity = hints->gravity;
		set_flags (size_hints->flags, PWinGravity);
	}
	return (size_hints->flags != 0);
}

static Bool
client_hints2wm_protocols (ASFlagType * protocols, ASHints * hints)
{
	if (protocols == NULL || hints == NULL)
		return False;

	*protocols =
			hints->protocols & (AS_DoesWmDeleteWindow | AS_DoesWmTakeFocus);
	return (*protocols != 0);
}

Bool
set_all_client_hints (Window w, ASHints * hints, ASStatusHints * status,
											Bool set_command)
{
	XWMHints wm_hints;
	XSizeHints size_hints;
	ASFlagType protocols = 0;
	MwmHints mwm_hints;
	GnomeHints gnome_hints;
	ExtendedWMHints extwm_hints;

	if (w == None || hints == NULL)
		return False;

	set_client_names (w, hints->names[0], hints->icon_name, hints->res_class,
										hints->res_name);

	if (client_hints2wm_hints (&wm_hints, hints, status))
		XSetWMHints (dpy, w, &wm_hints);

	if (get_flags (hints->flags, AS_Transient))
		XSetTransientForHint (dpy, w, hints->transient_for);

	if (client_hints2size_hints (&size_hints, hints, status))
		XSetWMNormalHints (dpy, w, &size_hints);

	if (client_hints2extwm_hints (&extwm_hints, hints, status))
		set_extwm_hints (w, &extwm_hints);

	if (client_hints2wm_protocols (&protocols, hints)
			|| get_flags (extwm_hints.flags, EXTWM_DoesWMPing))
		set_client_protocols (w, protocols, extwm_hints.flags);

	if (client_hints2motif_hints (&mwm_hints, hints, status))
		set_multi32bit_property (w, _XA_MwmAtom, XA_CARDINAL, 4,
														 mwm_hints.flags, mwm_hints.functions,
														 mwm_hints.decorations, mwm_hints.inputMode);

	if (client_hints2gnome_hints (&gnome_hints, hints, status))
		set_gnome_hints (w, &gnome_hints);

	return True;
}


ASImage *get_client_icon_image (ScreenInfo * scr, ASHints * hints,
																int desired_size)
{
	ASImage *im = NULL;

	if (hints) {
		char *icon_file = hints->icon_file;
		ASImage *icon_file_im = NULL;
		Bool icon_file_isDefault = False;

		if (icon_file && Database) {
			if (Database->style_default.icon_file != NULL
					&& strcmp (icon_file, Database->style_default.icon_file) == 0)
				icon_file_isDefault = True;
		}
		if (get_flags (hints->client_icon_flags, AS_ClientIcon)) {
			Bool use_client_icon = (hints->icon_file == NULL
															|| Database == NULL);

			if (!use_client_icon) {
				if (icon_file_isDefault)
					use_client_icon = True;
				else {
					icon_file_im =
							get_asimage (scr->image_manager, icon_file, 0xFFFFFFFF, 100);
					if (icon_file_im == NULL)
						use_client_icon = True;
					LOCAL_DEBUG_OUT ("loaded icon from file \"%s\" into %p",
													 icon_file ? icon_file : "(null)", im);
				}

			}
			if (use_client_icon) {
				/* first try ARGB icon If provided by the application : */
				if (get_flags (hints->client_icon_flags, AS_ClientIconARGB)
						&& hints->icon_argb != NULL) {
					/* TODO: we also need to check for newfashioned ARGB icon from
					 * extended WM specs here
					 */
					int width = hints->icon_argb[0];
					int height = hints->icon_argb[1];

					im = convert_argb2ASImage (scr->asv, width, height,
																		 hints->icon_argb + 2, NULL);
					LOCAL_DEBUG_OUT ("converted client's ARGB into an icon %dx%d %p",
													 width, height, im);

				}
				if (im == NULL && get_flags (hints->client_icon_flags, AS_ClientIconPixmap) && hints->icon.pixmap != None) {	/* convert client's icon into ASImage */
					unsigned int width, height;

					get_drawable_size (hints->icon.pixmap, &width, &height);
					im = picture2asimage (scr->asv, hints->icon.pixmap,
																hints->icon_mask, 0, 0, width, height,
																0xFFFFFFFF, False, 100);

					LOCAL_DEBUG_OUT
							("converted client's pixmap into an icon %dx%d %p", width,
							 height, im);
				}
			}
		}
		LOCAL_DEBUG_OUT ("im =  %p", im);
		if (im == NULL) {
			if (CombinedCategories != NULL) {
				ASDesktopEntry *de = NULL;

				if (hints->names[0]) {
					char *name = hints->names[0];
					int i = 0;
					char old;

					while (name[i] && !isspace (name[i]))
						++i;
					if (i > 0) {
						old = name[i];
						name[i] = '\0';
						de = fetch_desktop_entry (CombinedCategories, name);
						LOCAL_DEBUG_OUT
								("found desktop entry %p, for name[0] = \"%s\"", de, name);
						name[i] = old;
					}
				}
				LOCAL_DEBUG_OUT ("icon file = %p, default = %d", icon_file,
												 icon_file_isDefault);
				if (de == NULL && (icon_file == NULL || icon_file_isDefault)) {
					LOCAL_DEBUG_OUT ("CombinedCategories = %p", CombinedCategories);

					de = fetch_desktop_entry (CombinedCategories, hints->res_name);
					LOCAL_DEBUG_OUT ("found desktop entry %p, for res_name = \"%s\"",
													 de, hints->res_name);
					if (de == NULL)
						de = fetch_desktop_entry (CombinedCategories,
																			hints->res_class);
					LOCAL_DEBUG_OUT
							("found desktop entry %p, for res_class = \"%s\"", de,
							 hints->res_class);
				}
				if (de) {
					if (de && de->ref_count > 0 && de->Icon) {
						if (icon_file_im) {
							safe_asimage_destroy (icon_file_im);
							icon_file_im = NULL;
						}
						icon_file = de->Icon;
					}
				}

			}
			if (icon_file) {
				if (icon_file_im)
					im = icon_file_im;
				else
					im = load_environment_icon_any (icon_file, desired_size);
				/*get_asimage (scr->image_manager, icon_file, 0xFFFFFFFF, 100); */
				LOCAL_DEBUG_OUT ("loaded icon from \"%s\" into %dx%d %p",
												 icon_file, im ? im->width : 0,
												 im ? im->height : 0, im);
			} else {
				LOCAL_DEBUG_OUT ("no icon to use %s", "");
			}
		}
	}
	return im;
}


/***********************************************************************************
 * Hints printing functions :
 ***********************************************************************************/

void print_clean_hints (stream_func func, void *stream, ASHints * clean)
{
	register int i;

	if (!pre_print_check
			(&func, &stream, clean, "No hints available(NULL)."))
		return;
	for (i = 0; i < MAX_WINDOW_NAMES && clean->names[i]; i++) {
		func (stream, "CLEAN.NAMES[%d] = \"%s\";\n", i, clean->names[i]);
		func (stream, "CLEAN.NAMES_ENCODING[%d] = %d;\n", i,
					clean->names_encoding[i]);
	}
	if (clean->icon_name) {
		func (stream, "CLEAN.icon_name = \"%s\";\n", clean->icon_name);
		func (stream, "CLEAN.icon_name_encoding = %d;\n",
					clean->names_encoding[clean->icon_name_idx]);
	}
	if (clean->res_name) {
		func (stream, "CLEAN.res_name = \"%s\";\n", clean->res_name);
		func (stream, "CLEAN.res_name_encoding = %d;\n",
					clean->names_encoding[clean->res_name_idx]);
	}
	if (clean->res_class) {
		func (stream, "CLEAN.res_class = \"%s\";\n", clean->res_class);
		func (stream, "CLEAN.res_class_encoding = %d;\n",
					clean->names_encoding[clean->res_class_idx]);
	}
	func (stream, "CLEAN.flags = 0x%lX;\n", clean->flags);
	func (stream, "CLEAN.protocols = 0x%lX;\n", clean->protocols);
	func (stream, "CLEAN.function_mask = 0x%lX;\n", clean->function_mask);

	if (get_flags (clean->flags, AS_Icon)) {
		if (get_flags (clean->client_icon_flags, AS_ClientIcon)) {
			if (get_flags (clean->client_icon_flags, AS_ClientIconARGB)
					&& clean->icon_argb) {
				func (stream, "CLEAN.icon.argb.width = 0x%lX;\n",
							clean->icon_argb[0]);
				func (stream, "CLEAN.icon.argb.height = 0x%lX;\n",
							clean->icon_argb[1]);
			} else if (get_flags (clean->client_icon_flags, AS_ClientIconPixmap))
				func (stream, "CLEAN.icon.pixmap = 0x%lX;\n", clean->icon.pixmap);
			else
				func (stream, "CLEAN.icon.window = 0x%lX;\n", clean->icon.window);
			func (stream, "CLEAN.icon_mask = 0x%lX;\n", clean->icon_mask);
			if (get_flags (clean->client_icon_flags, AS_ClientIconPosition))
				func (stream, "CLEAN.icon_x = %d;\nCLEAN.icon_y = %d;\n",
							clean->icon_x, clean->icon_y);
		} else if (clean->icon_file)
			func (stream, "CLEAN.icon_file = \"%s\";\n", clean->icon_file);
	}
	if (get_flags (clean->flags, AS_MinSize))
		func (stream, "CLEAN.min_width = %u;\nCLEAN.min_height = %u;\n",
					clean->min_width, clean->min_height);
	if (get_flags (clean->flags, AS_MaxSize))
		func (stream, "CLEAN.max_width = %u;\nCLEAN.max_height = %u;\n",
					clean->max_width, clean->max_height);
	if (get_flags (clean->flags, AS_SizeInc))
		func (stream, "CLEAN.width_inc = %u;\nCLEAN.height_inc = %u;\n",
					clean->width_inc, clean->height_inc);
	if (get_flags (clean->flags, AS_Aspect)) {
		func (stream, "CLEAN.min_aspect.x = %d;\nCLEAN.min_aspect.y = %d;\n",
					clean->min_aspect.x, clean->min_aspect.y);
		func (stream, "CLEAN.max_aspect.x = %d;\nCLEAN.max_aspect.y = %d;\n",
					clean->max_aspect.x, clean->max_aspect.y);
	}
	if (get_flags (clean->flags, AS_BaseSize))
		func (stream, "CLEAN.base_width = %u;\nCLEAN.base_height = %u;\n",
					clean->base_width, clean->base_height);
	if (get_flags (clean->flags, AS_Gravity))
		func (stream, "CLEAN.gravity = %d;\n", clean->gravity);
	if (get_flags (clean->flags, AS_Border))
		func (stream, "CLEAN.border_width = %u;\n", clean->border_width);
	if (get_flags (clean->flags, AS_Handles))
		func (stream, "CLEAN.handle_width = %u;\n", clean->handle_width);

	if (clean->group_lead)
		func (stream, "CLEAN.group_lead = 0x%lX;\n", clean->group_lead);
	if (get_flags (clean->flags, AS_Transient))
		func (stream, "CLEAN.transient_for = 0x%lX;\n", clean->transient_for);

	if (clean->cmap_windows)
		for (i = 0; clean->cmap_windows[i] != None; i++)
			func (stream, "CLEAN.cmap_windows[%d] = 0x%lX;\n", i,
						clean->cmap_windows[i]);

	if (get_flags (clean->flags, AS_PID))
		func (stream, "CLEAN.pid = %d;\n", clean->pid);
	if (clean->frame_name && get_flags (clean->flags, AS_Frame))
		func (stream, "CLEAN.frame_name = \"%s\";\n", clean->frame_name);
	if (clean->windowbox_name && get_flags (clean->flags, AS_Windowbox))
		func (stream, "CLEAN.windowbox_name = \"%s\";\n",
					clean->windowbox_name);

	for (i = 0; i < BACK_STYLES; i++)
		if (clean->mystyle_names[i])
			func (stream, "CLEAN.mystyle_names[%d] = \"%s\";\n", i,
						clean->mystyle_names[i]);

	func (stream, "CLEAN.disabled_buttons = 0x%lX;\n",
				clean->disabled_buttons);

	func (stream, "CLEAN.hints_types_raw = 0x%lX;\n",
				clean->hints_types_raw);
	func (stream, "CLEAN.hints_types_clean = 0x%lX;\n",
				clean->hints_types_clean);

	if (clean->client_host)
		func (stream, "CLEAN.client_host = \"%s\";\n", clean->client_host);
	if (clean->client_cmd)
		func (stream, "CLEAN.client_cmd = \"%s\";\n", clean->client_cmd);	

    func (stream, "CLEAN.extwm_window_type = \"0x%lX\";\n", clean->extwm_window_type);
}

void
print_status_hints (stream_func func, void *stream, ASStatusHints * status)
{
	if (!pre_print_check
			(&func, &stream, status, "No status available(NULL)."))
		return;

	func (stream, "STATUS.flags = 0x%lX;\n", status->flags);

	if (get_flags (status->flags, AS_StartPositionUser)) {
		func (stream, "STATUS.user_x = %d;\n", status->x);
		func (stream, "STATUS.user_y = %d;\n", status->y);
	} else {
		func (stream, "STATUS.x = %d;\n", status->x);
		func (stream, "STATUS.y = %d;\n", status->y);
	}
	if (get_flags (status->flags, AS_Size)) {
		func (stream, "STATUS.width = %d;\n", status->width);
		func (stream, "STATUS.height = %d;\n", status->height);
	}
	if (get_flags (status->flags, AS_Desktop))
		func (stream, "STATUS.desktop = %d;\n", status->desktop);
	if (get_flags (status->flags, AS_Layer))
		func (stream, "STATUS.layer = %d;\n", status->layer);
	if (get_flags (status->flags, AS_StartViewportX | AS_StartViewportX)) {
		func (stream, "STATUS.viewport_x = %d;\n", status->viewport_x);
		func (stream, "STATUS.viewport_y = %d;\n", status->viewport_y);
	}
}

/*********************************************************************************
 *  serialization for purpose of inter-module communications                     *
 *********************************************************************************/
void serialize_string (char *string, ASVector * buf)
{
	if (buf) {
		register CARD32 *ptr;
		register char *src = string;
		register int i = string ? strlen (string) >> 2 : 0;	/* assume CARD32 == 4*CARD8 :)) */

		append_vector (buf, NULL, 1 + i + 1);
		ptr = VECTOR_TAIL (CARD32, *buf);
		VECTOR_USED (*buf) += i + 1;
		ptr[0] = i + 1;
		++ptr;
		if (string == NULL) {
			ptr[0] = 0;
			return;
		}
		src = &(string[i << 2]);
		/* unrolling loop here : */
		ptr[i] = src[0] & 0x0FF;
		if (src[0]) {
			if (src[1]) {							/* we don't really want to use bitwise operations */
				/* so we get "true" number and later can do ENDIANNES transformations */
				ptr[i] |= (((CARD32) src[1]) << 8) & 0x0FF00;
				if (src[2])
					ptr[i] |= (((CARD32) src[2]) << 16) & 0x0FF0000;
			}
		}
		while (--i >= 0) {
			src -= 4;
			ptr[i] =
					(((CARD32) src[0]) & 0x0FF) | (((CARD32) src[1] << 8) & 0x0FF00)
					| (((CARD32) src[2] << 16) & 0x0FF0000)
					| (((CARD32) src[3] << 24) & 0xFF000000);
		}
	}
}

void serialize_CARD32_zarray (CARD32 * array, ASVector * buf)
{
	register int i = 0;
	register CARD32 *ptr;

	if (array)
		while (array[i])
			i++;
	i++;
	append_vector (buf, NULL, 1 + i);
	ptr = VECTOR_TAIL (CARD32, *buf);
	VECTOR_USED (*buf) += i;
	ptr[0] = i;
	ptr++;
	if (array == NULL)
		ptr[0] = 0;
	else
		while (--i >= 0)
			ptr[i] = array[i];
}

Bool serialize_clean_hints (ASHints * clean, ASVector * buf)
{
	register CARD32 *ptr;
	register int i = 0;

	if (clean == NULL || buf == NULL)
		return False;

	/* we expect CARD32 vector here : */
	if (VECTOR_UNIT (*buf) != sizeof (CARD32))
		return False;

	append_vector (buf, NULL, ASHINTS_STATIC_DATA);
	ptr = VECTOR_TAIL (CARD32, *buf);
	ptr[i++] = clean->flags;
	ptr[i++] = clean->protocols;
	ptr[i++] = clean->function_mask;
	ptr[i++] = clean->icon.window;
	ptr[i++] = clean->icon_mask;
	ptr[i++] = clean->icon_x;
	ptr[i++] = clean->icon_y;

	ptr[i++] = clean->min_width;
	ptr[i++] = clean->min_height;
	ptr[i++] = clean->max_width;
	ptr[i++] = clean->max_height;
	ptr[i++] = clean->width_inc;
	ptr[i++] = clean->height_inc;
	ptr[i++] = clean->min_aspect.x;
	ptr[i++] = clean->min_aspect.y;
	ptr[i++] = clean->max_aspect.x;
	ptr[i++] = clean->max_aspect.y;
	ptr[i++] = clean->base_width;
	ptr[i++] = clean->base_height;

	ptr[i++] = clean->gravity;
	ptr[i++] = clean->border_width;
	ptr[i++] = clean->handle_width;

	ptr[i++] = clean->group_lead;
	ptr[i++] = clean->transient_for;

	ptr[i++] = clean->pid;

	ptr[i++] = clean->disabled_buttons;

	ptr[i++] = clean->hints_types_raw;
	ptr[i++] = clean->hints_types_clean;
	VECTOR_USED (*buf) += i;

	serialize_CARD32_zarray (clean->cmap_windows, buf);

	serialize_string (clean->icon_file, buf);
	serialize_string (clean->frame_name, buf);
	serialize_string (clean->windowbox_name, buf);

	for (i = 0; i < BACK_STYLES; i++)
		serialize_string (clean->mystyle_names[i], buf);

	serialize_string (clean->client_host, buf);
	serialize_string (clean->client_cmd, buf);

	return True;
}

Bool serialize_names (ASHints * clean, ASVector * buf)
{
	CARD32 header[4];
	register int i;

	if (clean == NULL || buf == NULL)
		return False;
	header[0] =
			pointer_name_to_index_in_list (clean->names, clean->res_name);
	header[1] =
			pointer_name_to_index_in_list (clean->names, clean->res_class);
	header[2] =
			pointer_name_to_index_in_list (clean->names, clean->icon_name);
	for (i = 0; clean->names[i] != NULL && i < MAX_WINDOW_NAMES; i++) ;
	header[3] = i;
	append_vector (buf, &(header[0]), 4);

	for (i = 0; clean->names[i] != NULL && i < MAX_WINDOW_NAMES; i++)
		serialize_string (clean->names[i], buf);

	return True;
}

Bool serialize_status_hints (ASStatusHints * status, ASVector * buf)
{
	register CARD32 *ptr;
	register int i = 0;

	if (status == NULL || buf == NULL)
		return False;

	/* we expect CARD32 vector here : */
	if (VECTOR_UNIT (*buf) != sizeof (CARD32))
		return False;

	append_vector (buf, NULL, ASSTATUSHINTS_STATIC_DATA);
	ptr = VECTOR_TAIL (CARD32, *buf);
	ptr[i++] = status->flags;

	ptr[i++] = status->x;
	ptr[i++] = status->y;
	ptr[i++] = status->width;
	ptr[i++] = status->height;
	ptr[i++] = status->border_width;
	ptr[i++] = status->viewport_x;
	ptr[i++] = status->viewport_y;
	ptr[i++] = status->desktop;
	ptr[i++] = status->layer;
	ptr[i++] = status->icon_window;
	VECTOR_USED (*buf) += i;

	return True;
}

/*********************************************************************************
 *  deserialization so that module can read out communications 	                 *
 *********************************************************************************/
char *deserialize_string (CARD32 ** pbuf, size_t * buf_size)
{
	char *string;
	CARD32 *buf = *pbuf;
	size_t len;
	register int i;
	register char *str;

	if (*pbuf == NULL)
		return NULL;
	if (buf_size && *buf_size < 2)
		return NULL;
	len = buf[0];
	if (buf_size && len > *buf_size + 1)
		return NULL;
	buf++;
	str = string = safemalloc (len << 2);
	for (i = 0; i < len; i++) {
		str[0] = (buf[i] & 0x0FF);
		str[1] = (buf[i] >> 8) & 0x0FF;
		str[2] = (buf[i] >> 16) & 0x0FF;
		str[3] = (buf[i] >> 24) & 0x0FF;
		str += 4;
	}

	if (buf_size)
		*buf_size -= len;
	*pbuf += len;

	return string;
}

CARD32 *deserialize_CARD32_zarray (CARD32 ** pbuf, size_t * buf_size)
{
	CARD32 *array;
	CARD32 *buf = *pbuf;
	size_t len;
	register int i;

	if (*pbuf == NULL || *buf_size < 2)
		return NULL;
	len = buf[0];
	if (len > *buf_size + 1)
		return NULL;
	buf++;
	array = safemalloc (len * sizeof (CARD32));
	for (i = 0; i < len; i++)
		array[i] = buf[i];

	*buf_size -= len;
	*pbuf += len;

	return array;
}

ASHints *deserialize_clean_hints (CARD32 ** pbuf, size_t * buf_size,
																	ASHints * reusable_memory)
{
	ASHints *clean = reusable_memory;
	register int i = 0;
	register CARD32 *buf = *pbuf;

	if (buf == NULL || *buf_size < ASHINTS_STATIC_DATA)
		return False;

	if (clean == NULL)
		clean = safecalloc (1, sizeof (ASHints));

	clean->flags = buf[i++];
	clean->protocols = buf[i++];
	clean->function_mask = buf[i++];

	clean->icon.window = buf[i++];
	clean->icon_mask = buf[i++];
	clean->icon_x = buf[i++];
	clean->icon_y = buf[i++];

	clean->min_width = buf[i++];
	clean->min_height = buf[i++];
	clean->max_width = buf[i++];
	clean->max_height = buf[i++];
	clean->width_inc = buf[i++];
	clean->height_inc = buf[i++];
	clean->min_aspect.x = buf[i++];
	clean->min_aspect.y = buf[i++];
	clean->max_aspect.x = buf[i++];
	clean->max_aspect.y = buf[i++];
	clean->base_width = buf[i++];
	clean->base_height = buf[i++];
	clean->gravity = buf[i++];
	clean->border_width = buf[i++];
	clean->handle_width = buf[i++];
	clean->group_lead = buf[i++];
	clean->transient_for = buf[i++];
	clean->pid = buf[i++];
	clean->disabled_buttons = buf[i++];

	clean->hints_types_raw = buf[i++];
	clean->hints_types_clean = buf[i++];

	*buf_size -= i;
	*pbuf += i;
	if (clean->cmap_windows)
		free (clean->cmap_windows);
	clean->cmap_windows = deserialize_CARD32_zarray (pbuf, buf_size);

	if (clean->icon_file)
		free (clean->icon_file);
	clean->icon_file = deserialize_string (pbuf, buf_size);
	if (clean->frame_name)
		free (clean->frame_name);
	clean->frame_name = deserialize_string (pbuf, buf_size);
	if (clean->windowbox_name)
		free (clean->windowbox_name);
	clean->windowbox_name = deserialize_string (pbuf, buf_size);

	for (i = 0; i < BACK_STYLES; i++) {
		if (clean->mystyle_names[i])
			free (clean->mystyle_names[i]);
		clean->mystyle_names[i] = deserialize_string (pbuf, buf_size);
	}
	if (clean->client_host)
		free (clean->client_host);
	clean->client_host = deserialize_string (pbuf, buf_size);
	if (clean->client_cmd)
		free (clean->client_cmd);
	clean->client_cmd = deserialize_string (pbuf, buf_size);

	return clean;
}

Bool deserialize_names (ASHints * clean, CARD32 ** pbuf, size_t * buf_size)
{
	CARD32 header[4];
	CARD32 *buf = *pbuf;
	register int i;

	if (clean == NULL || buf == NULL || *buf_size < 4)
		return False;

	header[0] = buf[0];
	header[1] = buf[1];
	header[2] = buf[2];
	header[3] = buf[3];

	if (header[3] <= 0 || header[3] >= MAX_WINDOW_NAMES)
		return False;

	*buf_size -= 4;
	*pbuf += 4;

	for (i = 0; i < header[3]; i++) {
		if (clean->names[i])
			free (clean->names[i]);
		clean->names[i] = deserialize_string (pbuf, buf_size);
	}
	clean->res_name =
			(header[0] < MAX_WINDOW_NAMES) ? clean->names[header[0]] : NULL;
	clean->res_name_idx = 0;
	clean->res_class =
			(header[1] < MAX_WINDOW_NAMES) ? clean->names[header[1]] : NULL;
	clean->res_class_idx = 0;
	clean->icon_name =
			(header[2] < MAX_WINDOW_NAMES) ? clean->names[header[2]] : NULL;
	clean->icon_name_idx = 0;

	return True;
}


ASStatusHints *deserialize_status_hints (CARD32 ** pbuf, size_t * buf_size,
																				 ASStatusHints * reusable_memory)
{
	ASStatusHints *status = reusable_memory;
	register int i = 0;
	register CARD32 *buf = *pbuf;

	if (buf == NULL || *buf_size < ASSTATUSHINTS_STATIC_DATA)
		return False;

	if (status == NULL)
		status = safecalloc (1, sizeof (ASStatusHints));

	status->flags = buf[i++];
	status->x = buf[i++];
	status->y = buf[i++];
	status->width = buf[i++];
	status->height = buf[i++];
	status->border_width = buf[i++];
	status->viewport_x = buf[i++];
	status->viewport_y = buf[i++];
	status->desktop = buf[i++];
	status->layer = buf[i++];
	status->icon_window = buf[i++];
	*buf_size -= i;
	*pbuf += i;

	return status;
}
