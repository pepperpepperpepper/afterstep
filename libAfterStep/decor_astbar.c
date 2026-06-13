/*
 * Copyright (C) 2002 Sasha Vasko <sasha at aftercode.net>
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#undef LOCAL_DEBUG
#undef DO_CLOCKING

#include "../configure.h"
#include "asapp.h"
#include "afterstep.h"
#include "mystyle.h"
#include "screen.h"
#include "../libAfterImage/afterimage.h"
#include "myicon.h"
#include "canvas.h"
#include "decor.h"
#include "event.h"
#include "balloon.h"
#include "shape.h"
#include "decor_internal.h"

void print_astbar_tiles (ASTBarData * tbar)
{
	register int l;

	show_progress ("tbar %p has %d tiles :", tbar, tbar->tiles_num);
	for (l = 0; l < tbar->tiles_num; ++l) {
		show_progress
				("\t %3.3d: [%2.2d][%2.2d] %s flags(%X) %ux%u%+d%+d data.raw = (0x%lx, 0x%lx, 0x%lx)",
				 l, ASTileCol (tbar->tiles[l]), ASTileRow (tbar->tiles[l]),
				 ASTileTypeHandlers[ASTileType (tbar->tiles[l])].name,
				 tbar->tiles[l].flags, tbar->tiles[l].width, tbar->tiles[l].height,
				 tbar->tiles[l].x, tbar->tiles[l].y, tbar->tiles[l].data.raw[0],
				 tbar->tiles[l].data.raw[1], tbar->tiles[l].data.raw[2]);
	}
}

ASTBarData *create_astbar ()
{
	ASTBarData *tbar = safecalloc (1, sizeof (ASTBarData));

	set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
	LOCAL_DEBUG_CALLER_OUT ("<<#########>>created tbar %p", tbar);
	tbar->rendered_root_x = tbar->rendered_root_y = 0xFFFF;
	tbar->composition_method[0] = TEXTURE_TRANSPIXMAP_ALPHA;
	tbar->composition_method[1] = TEXTURE_TRANSPIXMAP_ALPHA;
	/* since saturation is allowed to take values from 0 to 100 
	   inclusive - we use -1 to indicate absence : */
	tbar->sat[0] = -1;
	tbar->sat[1] = -1;
	tbar->hue[0] = -1;
	tbar->hue[1] = -1;
	return tbar;
}

static inline void flush_tbar_backs (ASTBarData * tbar)
{
	register int i;

	for (i = 0; i < BAR_STATE_NUM; ++i)
		if (tbar->back[i]) {
			LOCAL_DEBUG_OUT ("tbar %p destroy back %d, %p", tbar, i,
											 tbar->back[i]);
			destroy_asimage (&(tbar->back[i]));
		}
	set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
}

void flush_tbar_state_backs (ASTBarData * tbar, int state)
{
	if (state < 0 || state > BAR_STATE_NUM)
		flush_tbar_backs (tbar);
	else {
		if (tbar->back[state])
			destroy_asimage (&(tbar->back[state]));
		set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
	}
}

void destroy_astbar (ASTBarData ** ptbar)
{
	if (ptbar)
		if (*ptbar) {
			ASTBarData *tbar = *ptbar;
			register int i;

			LOCAL_DEBUG_CALLER_OUT ("<<#########>>destroying tbar %p", tbar);
			if (tbar->tiles) {
				for (i = 0; i < tbar->tiles_num; ++i) {
					int type = ASTileType (tbar->tiles[i]);

					if (ASTileTypeHandlers[type].free_astile_handler)
						ASTileTypeHandlers[type].free_astile_handler (&
																													(tbar->
																													 tiles[i]));
				}
				free (tbar->tiles);
			}

			LOCAL_DEBUG_CALLER_OUT ("<<#########>>flashing tbar %p backs", tbar);
			flush_tbar_backs (tbar);

			if (tbar == FocusedBar) {
				LOCAL_DEBUG_CALLER_OUT
						("<<#########>>withdrawing tbar balloon  %p", tbar->balloon);
				withdraw_balloon (NULL);
				FocusedBar = NULL;
			}
			if (tbar->balloon) {
				LOCAL_DEBUG_CALLER_OUT ("<<#########>>freeing tbar balloon  %p",
																tbar->balloon);
				destroy_asballoon (&(tbar->balloon));
			}

			memset (tbar, 0x00, sizeof (ASTBarData));
			LOCAL_DEBUG_CALLER_OUT ("<<#########>>freeing tbar %p memory", tbar);
			free (tbar);
			LOCAL_DEBUG_CALLER_OUT ("<<#########>>all done for tbar %p", tbar);
			*ptbar = NULL;
		}
}

unsigned int calculate_astbar_height (ASTBarData * tbar)
{
	int height = 0;
	int row_height[AS_TileRows] = { 0 };

	if (tbar) {
		register int i = tbar->tiles_num;

#if	defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
		print_astbar_tiles (tbar);
#endif
		while (--i >= 0)
			if (ASTileType (tbar->tiles[i]) != AS_TileFreed
					&& !ASTileIgnoreHeight (tbar->tiles[i])) {
				register int row = ASTileRow (tbar->tiles[i]);

				if (row_height[row] < tbar->tiles[i].height)
					row_height[row] = tbar->tiles[i].height;
			}

		for (i = 0; i < AS_TileRows; ++i)
			if (row_height[i] > 0) {
				if (height > 0)
					height += tbar->v_spacing;
				height += row_height[i];
			}
		if (height > 0)
			height += tbar->v_border << 1;

		height += tbar->top_bevel + tbar->bottom_bevel;
	}
	return height;
}

unsigned int calculate_astbar_width (ASTBarData * tbar)
{
	int width = 0;
	int col_width[AS_TileColumns] = { 0 };

	if (tbar) {
		register int i = tbar->tiles_num;

#if	defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
		print_astbar_tiles (tbar);
#endif
		while (--i >= 0)
			if (ASTileType (tbar->tiles[i]) != AS_TileFreed
					&& !ASTileIgnoreWidth (tbar->tiles[i])) {
				register int col = ASTileCol (tbar->tiles[i]);

				if (col_width[col] < tbar->tiles[i].width)
					col_width[col] = tbar->tiles[i].width;
			}

		for (i = 0; i < AS_TileColumns; ++i)
			if (col_width[i] > 0) {
				if (width > 0)
					width += tbar->h_spacing;
				width += col_width[i];
			}
		if (width > 0)
			width += tbar->h_border << 1;

		width += tbar->left_bevel + tbar->right_bevel;
	}
	return width;
}

Bool
set_astbar_size (ASTBarData * tbar, unsigned int width,
								 unsigned int height)
{
	Bool changed = False;

	if (tbar) {
		unsigned int w = width;
		unsigned int h = height;

		if (w >= MAX_POSITION)
			w = tbar->width;
		else if (w == 0)
			w = 1;
		if (h >= MAX_POSITION)
			h = tbar->height;
		else if (h == 0)
			h = 1;

		changed = (w != tbar->width || h != tbar->height);
		LOCAL_DEBUG_CALLER_OUT ("resizing TBAR %p from %dx%d to %dx%d", tbar,
														tbar->width, tbar->height, w, h);
		tbar->width = w;
		tbar->height = h;
		if (changed)
			flush_tbar_backs (tbar);
	}
	return changed;
}

static void update_astbar_bevel_size (ASTBarData * tbar)
{
	ASImageBevel bevel;
	int i;

	tbar->left_bevel = tbar->top_bevel = tbar->right_bevel =
			tbar->bottom_bevel = 0;
	for (i = 0; i < BAR_STATE_NUM; ++i)
		if (tbar->style[i]) {
			mystyle_make_bevel (tbar->style[i], &bevel, tbar->hilite[i], False);
			if (tbar->left_bevel < bevel.left_outline + bevel.left_inline)
				tbar->left_bevel = bevel.left_outline + bevel.left_inline;
			if (tbar->top_bevel < bevel.top_outline + bevel.top_inline)
				tbar->top_bevel = bevel.top_outline + bevel.top_inline;
			if (tbar->right_bevel < bevel.right_outline + bevel.right_inline)
				tbar->right_bevel = bevel.right_outline + bevel.right_inline;
			if (tbar->bottom_bevel < bevel.bottom_outline + bevel.bottom_inline)
				tbar->bottom_bevel = bevel.bottom_outline + bevel.bottom_inline;
		}
}

Bool
set_astbar_hilite (ASTBarData * tbar, unsigned int state,
									 ASFlagType hilite)
{
	Bool changed = False;

	LOCAL_DEBUG_CALLER_OUT ("%p,%d,0x%lX", tbar, state, hilite);
	if (tbar && state < BAR_STATE_NUM) {
		changed = (tbar->hilite[state] != (hilite & HILITE_MASK));
		tbar->hilite[state] = (hilite & HILITE_MASK);
		if (changed) {
			update_astbar_bevel_size (tbar);
			set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
		}
	}
	return changed;
}

Bool
set_astbar_composition_method (ASTBarData * tbar, unsigned int state,
															 unsigned char method)
{
	Bool changed = False;

	if (tbar && state < BAR_STATE_NUM) {
		changed = (tbar->composition_method[state] != method);
		tbar->composition_method[state] = method;
		if (changed)
			set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
	}
	return changed;
}

Bool
set_astbar_huesat (ASTBarData * tbar, unsigned int state, int hue, int sat)
{
	Bool changed = False;

	if (tbar && state < BAR_STATE_NUM) {
		changed = (tbar->hue[state] != hue || tbar->sat[state] != sat);
		tbar->hue[state] = hue;
		tbar->sat[state] = sat;
		if (changed)
			set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
	}
	return changed;
}


static inline void
set_astile_styles (ASTBarData * tbar, ASTile * tile, int state)
{
	register int i;

	for (i = 0; i < BAR_STATE_NUM; ++i)
		if ((i == state || state == -1) && tbar->style[i])
			if (ASTileTypeHandlers[ASTileType (*tile)].on_style_changed_handler
					!= NULL)
				ASTileTypeHandlers[ASTileType (*tile)].on_style_changed_handler
						(tile, tbar->style[i], i);
}

Bool set_astbar_style_ptr (ASTBarData * tbar, int state, MyStyle * style)
{
	Bool changed = False;

	LOCAL_DEBUG_OUT ("bar(%p)->state(%d)->style_name(\"%s\")", tbar, state,
									 style ? style->name : "");
	if (tbar && state < BAR_STATE_NUM) {
		register int i;

		for (i = 0; i < BAR_STATE_NUM; ++i)
			if (i == state || state == -1) {
				changed = changed || (style != tbar->style[i]);
				tbar->style[i] = style;
			}
/*LOCAL_DEBUG_OUT( "style(%p)->changed(%d)->tiles_num(%d)", style, changed, tbar->tiles_num );*/
		if (changed) {
			register int i = tbar->tiles_num;

			while (--i >= 0)
				set_astile_styles (tbar, &(tbar->tiles[i]), state);

			flush_tbar_state_backs (tbar, state);
			update_astbar_bevel_size (tbar);
		}
	}
	return changed;
}


Bool set_astbar_flip (ASTBarData * tbar, int flip)
{
	Bool changed = get_flags (flip, FLIP_VERTICAL);

	if (tbar) {
		ASFlagType vert_flag = changed ? BAR_FLAGS_VERTICAL : 0;

		if (get_flags (tbar->state, BAR_FLAGS_VERTICAL) == vert_flag)
			changed = False;
/*LOCAL_DEBUG_OUT( "style(%p)->changed(%d)->tiles_num(%d)", style, changed, tbar->tiles_num );*/
		if (changed) {
			flush_tbar_backs (tbar);
			set_flags (tbar->state, vert_flag);
		}
	}
	return changed;
}

Bool
set_astbar_style (ASTBarData * tbar, unsigned int state,
									const char *style_name)
{
	Bool changed = False;

	if (tbar && state < BAR_STATE_NUM)
		return set_astbar_style_ptr (tbar, state,
																 mystyle_find_or_default (style_name));
	return changed;
}

Bool invalidate_astbar_style (ASTBarData * tbar, int state)
{
	Bool changed = False;

	if (tbar && state < BAR_STATE_NUM) {
		if (state < 0) {
			int i;

			for (i = 0; i < BAR_STATE_NUM; ++i)
				if (tbar->back[i])
					changed = invalidate_astbar_style (tbar, i);
		} else if (tbar->style[state] != NULL) {
			changed = True;
			tbar->style[state] = NULL;
			flush_tbar_state_backs (tbar, state);
		}
	}
	return changed;
}


static int
add_astbar_tile (ASTBarData * tbar, int type, unsigned char col,
								 unsigned char row, int flip, int align)
{
	int new_idx = -1;
	ASFlagType align_flags = align;

	/* try 1: see if we have any tiles that has been freed */
	while (++new_idx < tbar->tiles_num)
		if (ASTileType (tbar->tiles[new_idx]) == AS_TileFreed)
			break;

	/* try 2: allocate new memory : */
	/* allocating memory if 4 tiles increments for more efficient memory management: */
	if (new_idx == tbar->tiles_num) {
		if ((tbar->tiles_num & 0x0003) == 0)
			tbar->tiles =
					realloc (tbar->tiles,
									 (((tbar->tiles_num >> 2) + 1) << 2) * sizeof (ASTile));
		++(tbar->tiles_num);
	}
	LOCAL_DEBUG_CALLER_OUT
			("type = %d, col = %d, row = %d, flip = %d, align_flags = 0x%lX, new_idx = %d",
			 type, col, row, flip, align_flags, new_idx);

	if (get_flags (flip, FLIP_VERTICAL) && get_flags (flip, FLIP_UPSIDEDOWN)) {
		clear_flags (align_flags, PAD_MASK);
		if (get_flags (align, PAD_LEFT))
			set_flags (align_flags, PAD_TOP);
		if (get_flags (align, PAD_RIGHT))
			set_flags (align_flags, PAD_BOTTOM);
		if (get_flags (align, PAD_TOP))
			set_flags (align_flags, PAD_RIGHT);
		if (get_flags (align, PAD_BOTTOM))
			set_flags (align_flags, PAD_LEFT);
	} else if (get_flags (flip, FLIP_VERTICAL)) {
		clear_flags (align_flags, PAD_MASK);
		if (get_flags (align, PAD_LEFT))
			set_flags (align_flags, PAD_BOTTOM);
		if (get_flags (align, PAD_RIGHT))
			set_flags (align_flags, PAD_TOP);
		if (get_flags (align, PAD_TOP))
			set_flags (align_flags, PAD_LEFT);
		if (get_flags (align, PAD_BOTTOM))
			set_flags (align_flags, PAD_RIGHT);
	} else if (get_flags (flip, FLIP_UPSIDEDOWN)) {
		clear_flags (align_flags, PAD_MASK);
		if (get_flags (align, PAD_LEFT))
			set_flags (align_flags, PAD_RIGHT);
		if (get_flags (align, PAD_RIGHT))
			set_flags (align_flags, PAD_LEFT);
		if (get_flags (align, PAD_TOP))
			set_flags (align_flags, PAD_BOTTOM);
		if (get_flags (align, PAD_BOTTOM))
			set_flags (align_flags, PAD_TOP);
	}

	if (get_flags (flip, FLIP_VERTICAL)) {
		clear_flags (align_flags, RESIZE_MASK | FIT_LABEL_SIZE);
		if (get_flags (align, RESIZE_H))
			set_flags (align_flags, RESIZE_V);
		if (get_flags (align, RESIZE_V))
			set_flags (align_flags, RESIZE_H);
		if (get_flags (align, RESIZE_H_SCALE))
			set_flags (align_flags, RESIZE_V_SCALE);
		if (get_flags (align, RESIZE_V_SCALE))
			set_flags (align_flags, RESIZE_H_SCALE);
		if (get_flags (align, FIT_LABEL_WIDTH))
			set_flags (align_flags, FIT_LABEL_HEIGHT);
		if (get_flags (align, FIT_LABEL_HEIGHT))
			set_flags (align_flags, FIT_LABEL_WIDTH);
	}

	LOCAL_DEBUG_OUT
			("type = %d, flip = %d, align = 0x%X, align_flags = 0x%lX, tbar->tiles = %p",
			 type, flip, align, align_flags, tbar->tiles);
	align_flags &= (PAD_MASK | RESIZE_MASK | FIT_LABEL_SIZE);

	memset (&(tbar->tiles[new_idx]), 0x00, sizeof (ASTile));
	tbar->tiles[new_idx].flags = (type & AS_TileTypeMask) |
			((col << AS_TileColOffset) & AS_TileColMask) |
			((row << AS_TileRowOffset) & AS_TileRowMask) |
			((flip << AS_TileFlipOffset) & AS_TileFlipMask) |
			((align_flags << AS_TileFloatingOffset));
	set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
	return new_idx;
}

Bool delete_astbar_tile (ASTBarData * tbar, int idx)
{

	if (tbar != NULL && idx < (int)(tbar->tiles_num)) {
		register int i;

		for (i = 0; i < tbar->tiles_num; ++i)
			if (i == idx || idx < 0) {
				int type = ASTileType (tbar->tiles[i]);

				if (ASTileTypeHandlers[type].free_astile_handler)
					ASTileTypeHandlers[type].free_astile_handler (&(tbar->tiles[i]));
				else if (type != AS_TileFreed) {
					memset (&(tbar->tiles[i]), 0x00, sizeof (ASTile));
					tbar->tiles[i].flags = AS_TileFreed;
				}
			}
		set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
		return True;
	}
	return False;
}

int
add_astbar_spacer (ASTBarData * tbar, unsigned char col, unsigned char row,
									 int flip, int align, unsigned short width,
									 unsigned short height)
{
	if (tbar) {
		int idx = add_astbar_tile (tbar, AS_TileSpacer, col, row, flip, align);
		ASTile *tile = &(tbar->tiles[idx]);

		tile->width = width;
		tile->height = height;
		return idx;
	}
	return -1;
}

int
add_astbar_btnblock (ASTBarData * tbar, unsigned char col,
										 unsigned char row, int flip, int align,
										 struct button_t **from_list, ASFlagType context_mask,
										 unsigned int count, int left_margin, int top_margin,
										 int spacing, int order)
{
	if (tbar) {
		int idx =
				add_astbar_tile (tbar, AS_TileBtnBlock, col, row, flip, align);
		ASTile *tile = &(tbar->tiles[idx]);

/*      if( get_flags( flip, FLIP_VERTICAL ) )
		{
			int tmp = top_margin ; 
			top_margin = left_margin ; 
			left_margin = tmp ;    
	   }    
 */
		build_btn_block (tile, from_list, context_mask, count, left_margin,
										 top_margin, spacing, order);
		return idx;
	}
	return -1;
}

int
add_astbar_icon (ASTBarData * tbar, unsigned char col, unsigned char row,
								 int flip, int align, ASImage * icon)
{
	if (tbar && icon) {
		int idx = add_astbar_tile (tbar, AS_TileIcon, col, row, flip, align);
		ASTile *tile = &(tbar->tiles[idx]);

		LOCAL_DEBUG_CALLER_OUT
				("col = %d, row = %d, flip = %d, align = 0x%X, im = %p, size = %dx%d",
				 col, row, flip, align, icon, icon->width, icon->height);
		if (flip == 0) {
			if (icon->imageman == NULL
					|| (tile->data.image.im = dup_asimage (icon)) == NULL)
				tile->data.image.im = clone_asimage (icon, 0xFFFFFFFF);
		} else {
			int dst_width = icon->width, dst_height = icon->height;

			if (get_flags (flip, FLIP_VERTICAL)) {
				dst_width = icon->height;
				dst_height = icon->width;
			}
			tile->data.image.im =
					flip_asimage (ASDefaultVisual, icon, 0, 0, dst_width, dst_height,
												flip, ASA_ASImage, 100, ASIMAGE_QUALITY_DEFAULT);
		}
		tile->width = tile->data.image.im->width;
		tile->height = tile->data.image.im->height;
		ASSetTileSublayers (*tile, 1);
		return idx;
	}
	return -1;
}

int
add_astbar_image (ASTBarData * tbar, unsigned char col, unsigned char row,
									int flip, int align, ASImage * im,
									unsigned short slice_x_start, unsigned short slice_x_end,
									unsigned short slice_y_start, unsigned short slice_y_end)
{
	int idx = add_astbar_icon (tbar, col, row, flip, align, im);

	if (idx >= 0) {
		ASImageTile *imtile = &(tbar->tiles[idx].data.image);

		imtile->slice_x_start = slice_x_start;
		imtile->slice_x_end = slice_x_end;
		imtile->slice_y_start = slice_y_start;
		imtile->slice_y_end = slice_y_end;

		return idx;
	}
	return -1;
}


int
add_astbar_label (ASTBarData * tbar, unsigned char col, unsigned char row,
									int flip, int align, short h_padding, short v_padding,
									const char *text, unsigned long encoding)
{
	LOCAL_DEBUG_CALLER_OUT ("encoding = %ld, label \"%s\"", encoding,
													text ? text : "null");
	if (tbar) {

		int idx = add_astbar_tile (tbar, AS_TileLabel, col, row, flip, align);
		ASTile *tile = &(tbar->tiles[idx]);

		ASLabel *lbl = &(tile->data.label);

		lbl->text = mystrdup (text);
		lbl->encoding = encoding;
		lbl->h_padding = h_padding;
		lbl->v_padding = v_padding;
		set_astile_styles (tbar, tile, -1);

		ASSetTileSublayers (*tile, 1);
		return idx;
	}
	return -1;
}

Bool
change_astbar_label (ASTBarData * tbar, int index, const char *label,
										 unsigned long encoding)
{
	Bool changed = False;

	LOCAL_DEBUG_CALLER_OUT
			("tbar(%p)->index(%d)->label(ENC = %ld, TXT=\"%s\")", tbar, index,
			 encoding, label ? label : "null");
	if (tbar && tbar->tiles) {
		ASLabel *lbl = &(tbar->tiles[index].data.label);

		if (ASTileType (tbar->tiles[index]) != AS_TileLabel)
			return False;
		LOCAL_DEBUG_OUT ("old_label(ENC = %ld, TXT=\"%s\" )", lbl->encoding,
										 lbl->text ? lbl->text : "null");


		if (label == NULL) {
			if ((changed = (lbl->text != NULL))) {
				free (lbl->text);
				lbl->text = NULL;
			}
		} else if (lbl->text == NULL) {
			changed = True;
			lbl->text = mystrdup (label);
		} else if ((changed = (strcmp ((char *)(lbl->text), label) != 0))) {
			free (lbl->text);
			lbl->text = mystrdup (label);
		}
		if (!changed && lbl->encoding != encoding)
			changed = True;
		lbl->encoding = encoding;
		if (changed) {
			set_astile_styles (tbar, &(tbar->tiles[index]), -1);
			set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
		}
	}
	return changed;
}

Bool
change_astbar_first_label (ASTBarData * tbar, const char *label,
													 unsigned long encoding)
{
	if (tbar) {
		register int i;

		for (i = 0; i < tbar->tiles_num; ++i)
			if (ASTileType (tbar->tiles[i]) == AS_TileLabel)
				return change_astbar_label (tbar, i, label, encoding);
	}
	return False;
}

Bool move_astbar (ASTBarData * tbar, ASCanvas * pc, int win_x, int win_y)
{
	Bool changed = False;

	if (tbar && pc) {
		int root_x, root_y;

		if (win_x >= MAX_POSITION || win_x <= -MAX_POSITION)
			win_x = tbar->win_x;
		if (win_y >= MAX_POSITION || win_y <= -MAX_POSITION)
			win_y = tbar->win_y;

		root_x = pc->root_x + (int)pc->bw + win_x;
		root_y = pc->root_y + (int)pc->bw + win_y;

		changed = (root_x != tbar->root_x || root_y != tbar->root_y);
		tbar->root_x = root_x;
		tbar->root_y = root_y;
		if (changed) {
			register int i = BAR_STATE_NUM;

			while (--i >= 0) {
				if (tbar->style[i] && TransparentMS (tbar->style[i]))
					flush_tbar_state_backs (tbar, i);
			}
		}
		changed = changed || (win_x != tbar->win_x || win_y != tbar->win_y);
		tbar->win_x = win_x;
		tbar->win_y = win_y;
		if (changed)
			set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
		LOCAL_DEBUG_OUT
				("tbar(%p)->root_geom(%ux%u%+d%+d)->win_pos(%+d%+d)->changed(%x)",
				 tbar, tbar->width, tbar->height, root_x, root_y, win_x, win_y,
				 changed);
	}
	return changed;
}

int
make_tile_pad (Bool pad_before, Bool pad_after, int cell_size,
							 int tile_size)
{
	if (cell_size <= tile_size)
		return 0;
	if (!pad_before)
		return 0;
	if (!pad_after)
		return cell_size - tile_size;
	return (cell_size - tile_size) >> 1;
}

Bool set_astbar_focused (ASTBarData * tbar, ASCanvas * pc, Bool focused)
{
	if (tbar) {
		int old_focused =
				get_flags (tbar->state, BAR_STATE_FOCUS_MASK) ? 1 : 0;
		int new_focused = focused ? 1 : 0;

		if (focused)
			set_flags (tbar->state, BAR_STATE_FOCUSED);
		else
			clear_flags (tbar->state, BAR_STATE_FOCUSED);

		if (old_focused != new_focused)
			set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
		if (get_flags (tbar->state, BAR_FLAGS_REND_PENDING) && pc != NULL)
			render_astbar (tbar, pc);
		return (new_focused != old_focused);
	}
	return False;
}

Bool set_astbar_pressed (ASTBarData * tbar, ASCanvas * pc, Bool pressed)
{
	if (tbar) {
		Bool old_pressed =
				get_flags (tbar->state, BAR_STATE_PRESSED_MASK) ? 1 : 0;

		if (pressed)
			set_flags (tbar->state, BAR_STATE_PRESSED);
		else
			clear_flags (tbar->state, BAR_STATE_PRESSED);


		if (old_pressed != pressed)
			set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
		if (get_flags (tbar->state, BAR_FLAGS_REND_PENDING) && pc != NULL)
			render_astbar (tbar, pc);
		return ((pressed ? 1 : 0) != old_pressed);
	}
	return False;
}

Bool set_astbar_btn_pressed (ASTBarData * tbar, int context)
{
	if (tbar) {
		int i;
		Bool changed = False;

		for (i = 0; i < tbar->tiles_num; ++i)
			if (ASTileType (tbar->tiles[i]) == AS_TileBtnBlock)
				if (set_tbtn_pressed (&(tbar->tiles[i].data.bblock), context))
					changed = True;
		if (changed)
			set_flags (tbar->state, BAR_FLAGS_REND_PENDING);
		return changed;
	}
	return False;
}


Bool
update_astbar_transparency (ASTBarData * tbar, ASCanvas * pc, Bool force)
{
	int root_x, root_y;
	Bool changed = False;

	if (tbar == NULL || pc == NULL)
		return False;;

	root_x = pc->root_x + (int)pc->bw + tbar->win_x;
	root_y = pc->root_y + (int)pc->bw + tbar->win_y;
	if ((changed =
			 (root_x != tbar->root_x || root_y != tbar->root_y
				|| ASDefaultScr->RootImage == NULL || force))) {
		register int i = BAR_STATE_NUM;

		tbar->root_x = root_x;
		tbar->root_y = root_y;

		while (--i >= 0)
			if (tbar->style[i] && TransparentMS (tbar->style[i]))
				flush_tbar_state_backs (tbar, i);
	}
	return get_flags (tbar->state, BAR_FLAGS_REND_PENDING);
}

Bool is_astbar_shaped (ASTBarData * tbar, int state)
{
	Bool shaped = False;
	MyStyle *style;

	if (state < 0 || state == BAR_STATE_UNFOCUSED) {
		if ((style = tbar->style[BAR_STATE_UNFOCUSED]) != NULL)
			if (style->texture_type == TEXTURE_SHAPED_PIXMAP
					|| style->texture_type == TEXTURE_SHAPED_SCALED_PIXMAP)
				shaped = True;
	}
	if (state < 0 || state == BAR_STATE_FOCUSED) {
		if ((style = tbar->style[BAR_STATE_FOCUSED]) != NULL)
			if (style->texture_type == TEXTURE_SHAPED_PIXMAP
					|| style->texture_type == TEXTURE_SHAPED_SCALED_PIXMAP)
				shaped = True;
	}
	return shaped;
}


int
trim_astbar_grid_dim (short *dim, int size, int space_left)
{
	int l = 0;
	int changed = 0;

	while (space_left < -1) {			/* ohh, no, we need to trim rows to fit */
		if (dim[l] > 0) {
			int to_trim = (-space_left * dim[l]) / size;

			if (dim[l] < to_trim)
				to_trim = dim[l] - 1;
			if (to_trim == 0)
				to_trim = 1;
			dim[l] -= to_trim;
			space_left += to_trim;
			++changed;
		}
		if (++l >= AS_TileRows) {
			if (changed == 0)
				break;
			l = 0;
			changed = 0;
		}
	}
	return space_left;
}


