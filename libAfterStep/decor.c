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

ASTBarData *FocusedBar = NULL;	/* currently focused bar with balloon shown for it */

/********************************************************************/
/* ASTBtnData :                                                     */
/********************************************************************/
ASTBtnData *create_astbtn ()
{
	return safecalloc (1, sizeof (ASTBtnData));
}

static void free_tbtn_images (ASTBtnData * btn)
{
	if (btn->pressed) {
/*        safe_asimage_destroy (btn->pressed); */
		btn->pressed = NULL;
	}

	if (btn->unpressed) {
/*        safe_asimage_destroy (btn->unpressed); */
		btn->unpressed = NULL;
	}
	btn->current = NULL;
}

void set_tbtn_images (ASTBtnData * btn, struct button_t *from)
{
	Bool pressed = False;

	if (AS_ASSERT (btn) || from == NULL)
		return;

	if (btn->current != NULL)
		pressed = (btn->current == btn->pressed);
	free_tbtn_images (btn);

	btn->pressed = from->pressed.image;
	btn->unpressed = from->unpressed.image;
	btn->current = (pressed && btn->pressed) ? btn->pressed : btn->unpressed;
	btn->width = from->width;
	btn->height = from->height;
}

ASTBtnData *make_tbtn (struct button_t *from)
{
	ASTBtnData *btn = NULL;

	if (from) {
		btn = safecalloc (1, sizeof (ASTBtnData));
		set_tbtn_images (btn, from);
	}
	return btn;
}

void destroy_astbtn (ASTBtnData ** ptbtn)
{
	if (!AS_ASSERT (ptbtn)) {
		ASTBtnData *btn = *ptbtn;

		if (btn) {
			free_tbtn_images (btn);
			if (btn->balloon)
				destroy_asballoon (&(btn->balloon));
			memset (btn, 0x00, sizeof (ASTBtnData));
			free (btn);
		}
		*ptbtn = NULL;
	}
}

/********************************************************************/
/* ASBtnBlock :                                                    */
/********************************************************************/
void free_asbtn_block (ASTile * tile)
{
	register ASBtnBlock *blk = &(tile->data.bblock);

	if (blk->buttons) {
		register int i = blk->buttons_num;

		while (--i >= 0) {
			free_tbtn_images (&(blk->buttons[i]));
			if (blk->buttons[i].balloon)
				destroy_asballoon (&(blk->buttons[i].balloon));
		}
		free (blk->buttons);
	}

	memset (blk, 0x00, sizeof (ASBtnBlock));
	ASSetTileType (tile, AS_TileFreed);
}

void
build_btn_block (ASTile * tile,
								 struct button_t **from_list, ASFlagType context_mask,
								 unsigned int count, int left_margin, int top_margin,
								 int spacing, int order)
{

	unsigned int real_count = 0;
	unsigned short max_width = 0, max_height = 0;
	register int i = count;
	ASBtnBlock *blk = &(tile->data.bblock);

	LOCAL_DEBUG_CALLER_OUT ("count(%d),lm(%d),tm(%d),sp(%d),or(%d)",
													left_margin, top_margin, spacing, order);
	if (count > 0)
		if (!AS_ASSERT (from_list))
			while (--i >= 0)
				if (from_list[i] != NULL &&
						(context_mask & from_list[i]->context) != 0 &&
						(from_list[i]->unpressed.image || from_list[i]->pressed.image))
				{
					++real_count;
					if (from_list[i]->width > max_width)
						max_width = from_list[i]->width;
					if (from_list[i]->height > max_height)
						max_height = from_list[i]->height;
				}
	LOCAL_DEBUG_OUT ("realcount = %d, max_size = %dx%d", real_count,
									 max_width, max_height);
	if (real_count > 0) {
		int k = real_count - 1;
		int pos =
				get_flags (order, TBTN_ORDER_REVERSE) ? -left_margin : left_margin;

		blk->buttons = safecalloc (real_count, sizeof (ASTBtnData));
		blk->buttons_num = real_count;
		i = count - 1;
		while (i >= 0 && k >= 0) {
			if (from_list[i] != NULL &&
					(context_mask & from_list[i]->context) != 0 &&
					(from_list[i]->unpressed.image || from_list[i]->pressed.image)) {
				set_tbtn_images (&(blk->buttons[k]), from_list[i]);
				blk->buttons[k].context = from_list[i]->context;
				--k;
			}
			--i;
		}

		/* top_margin surrounds button block from both sides ! */
		if (get_flags (order, TBTN_ORDER_VERTICAL)) {
			tile->width = max_width + top_margin * 2;
			tile->height = left_margin * 2;
			/*pos = get_flags(order, TBTN_ORDER_REVERSE)?-top_margin:top_margin ; */
		} else {
			tile->width = left_margin * 2;
			tile->height = max_height + top_margin * 2;
		}

		k = 0;
		while (k < real_count) {
			if (get_flags (order, TBTN_ORDER_VERTICAL)) {
				blk->buttons[k].x =
						top_margin + ((max_width - blk->buttons[k].width) >> 1);
				tile->height += blk->buttons[k].height + spacing;
				if (get_flags (order, TBTN_ORDER_REVERSE)) {
					pos -= blk->buttons[k].height;
					blk->buttons[k].y = pos;
					pos -= spacing;
				} else {
					blk->buttons[k].y = pos;
					pos += blk->buttons[k].height;
					pos += spacing;
				}
			} else {
				blk->buttons[k].y =
						top_margin + ((max_height - blk->buttons[k].height) >> 1);
				tile->width += blk->buttons[k].width + spacing;
				if (get_flags (order, TBTN_ORDER_REVERSE)) {
					pos -= blk->buttons[k].width;
					blk->buttons[k].x = pos;
					pos -= spacing;
				} else {
					blk->buttons[k].x = pos;
					pos += blk->buttons[k].width;
					pos += spacing;
				}
			}
			++k;
		}
		if (get_flags (order, TBTN_ORDER_VERTICAL)) {
			if (tile->height > left_margin * 2)
				tile->height -= spacing;
		} else {
			if (tile->width > left_margin * 2)
				tile->width -= spacing;
		}
		if (get_flags (order, TBTN_ORDER_REVERSE)) {
			k = real_count;
			if (get_flags (order, TBTN_ORDER_VERTICAL))
				while (--k >= 0)
					blk->buttons[k].y += tile->height;
			else
				while (--k >= 0)
					blk->buttons[k].x += tile->width;
		}
		LOCAL_DEBUG_OUT ("real_count=%d", real_count);
	} else {
		tile->width = 1;
		tile->height = 1;
	}
	ASSetTileSublayers (*tile, real_count);
}

static int check_btn_point (ASTile * tile, int x, int y)
{
	ASBtnBlock *bb = &(tile->data.bblock);
	register int i = bb->buttons_num;

	while (--i >= 0) {
		register ASTBtnData *btn = &(bb->buttons[i]);
		int tmp = x - btn->x;

		if (tmp >= 0 && tmp < btn->width) {
			tmp = y - btn->y;
			if (tmp >= 0 && tmp < btn->height)
				return btn->context;
		}
	}
	return C_NO_CONTEXT;
}

Bool set_tbtn_pressed (ASBtnBlock * bb, int context)
{
	register int i = bb->buttons_num;
	Bool changed = False;

	while (--i >= 0) {
		register ASTBtnData *btn = &(bb->buttons[i]);
		ASImage *new_current;

		new_current =
				(context & (btn->context)) ? btn->pressed : btn->unpressed;
		if (new_current == NULL)
			new_current = (btn->pressed == NULL) ? btn->unpressed : btn->pressed;
		if (new_current != btn->current) {
			changed = True;
			btn->current = new_current;
		}
	}
	return changed;
}


static int
set_asbtn_block_layer (ASTile * tile, ASImageLayer * layer,
											 unsigned int state, ASImage ** scrap_images,
											 int max_width, int max_height)
{
	register ASBtnBlock *bb = &(tile->data.bblock);
	register int i = bb->buttons_num;

	while (--i >= 0) {
		register ASTBtnData *btn = &(bb->buttons[i]);

		if (btn && btn->current) {
			layer[i].im = btn->current;
			layer[i].dst_x = tile->x + btn->x;
			layer[i].dst_y = tile->y + btn->y;
			layer[i].clip_width = layer[i].im->width;
			layer[i].clip_height = layer[i].im->height;
		}
	}
	return bb->buttons_num;
}


/********************************************************************/
/* ASIcon :                                                         */
/********************************************************************/
static void free_asimage_tile (ASTile * tile)
{
	if (tile->data.image.im) {
		safe_asimage_destroy (tile->data.image.im);
		tile->data.image.im = NULL;
	}
	ASSetTileType (tile, AS_TileFreed);
}

static int
set_asimage_layer (ASTile * tile, ASImageLayer * layer, unsigned int state,
									 ASImage ** scrap_images, int max_width, int max_height)
{
	int dst_width, dst_height;
	ASImage *im = tile->data.image.im;
	unsigned int slice_x_start = tile->data.image.slice_x_start;
	unsigned int slice_x_end = tile->data.image.slice_x_end;
	unsigned int slice_y_start = tile->data.image.slice_y_start;
	unsigned int slice_y_end = tile->data.image.slice_y_end;
	Bool do_slice_x = ((slice_x_start != 0 || slice_x_end != 0)
										 && get_flags (tile->flags, AS_TileHResize));
	Bool do_slice_y = ((slice_y_start != 0 || slice_y_end != 0)
										 && get_flags (tile->flags, AS_TileVResize));

	if (im == NULL)
		return 0;

	dst_width = im->width;
	dst_height = im->height;

	/* if( ASTileResizeable( *tile ) ) */
	{
		if (get_flags (tile->flags, AS_TileHScale) || do_slice_x)
			dst_width = max_width;
		else if (get_flags (tile->flags, AS_TileHResize)
						 && max_width < im->width) {
			if (get_flags (tile->flags, AS_TilePadLeft)) {
				if (get_flags (tile->flags, AS_TilePadRight))
					layer->clip_x = ((int)im->width - max_width) / 2;
				else
					layer->clip_x = ((int)im->width - max_width);
			}
		}
		if (get_flags (tile->flags, AS_TileVScale) || do_slice_y)
			dst_height = max_height;
		else if (get_flags (tile->flags, AS_TileVResize)
						 && max_height < im->height) {
			if (get_flags (tile->flags, AS_TilePadTop)) {
				if (get_flags (tile->flags, AS_TilePadBottom))
					layer->clip_y = ((int)im->height - max_height) / 2;
				else
					layer->clip_y = ((int)im->height - max_height);
			}
		}
	}
	LOCAL_DEBUG_OUT
			("flags = %lX, dst_size = %dx%d, im_size = %dx%d, max_size = %dx%d, clip = %+d%+d",
			 tile->flags, dst_width, dst_height, im->width, im->height,
			 max_width, max_height, layer->clip_x, layer->clip_y);
	if (im->width != dst_width || im->height != dst_height) {
		if (do_slice_x || do_slice_y)
			im = slice_asimage2 (ASDefaultVisual, im, slice_x_start, slice_x_end,
													 slice_y_start, slice_y_end, dst_width,
													 dst_height, get_flags (tile->flags,
																									AS_TileHScale |
																									AS_TileVScale),
													 ASA_ASImage, 0, ASIMAGE_QUALITY_DEFAULT);
		else
			im = scale_asimage (ASDefaultVisual, im, dst_width, dst_height,
													ASA_ASImage, 0, ASIMAGE_QUALITY_DEFAULT);

		if (im == NULL)
			im = tile->data.image.im;
		else
			*scrap_images = im;
	}

	layer->im = im;
	layer->dst_x = tile->x - layer->clip_x;
	layer->dst_y = tile->y - layer->clip_y;
	if (ASTileHResizeable (*tile))
		layer->clip_width = max_width;
	else
		layer->clip_width = layer->im->width;

	if (ASTileVResizeable (*tile))
		layer->clip_height = max_height;
	else
		layer->clip_height = layer->im->height;

	return 1;
}

/********************************************************************/
/* ASLabel :                                                        */
/********************************************************************/
static void free_aslabel (ASTile * tile)
{
	register ASLabel *lbl = &(tile->data.label);
	register int i;

	for (i = 0; i < BAR_STATE_NUM; ++i) {
		if (lbl->rendered[i])
			safe_asimage_destroy (lbl->rendered[i]);
		lbl->rendered[i] = NULL;
	}
	if (lbl->text) {
		free (lbl->text);
		lbl->text = NULL;
	}
	ASSetTileType (tile, AS_TileFreed);
}

static void
aslabel_style_changed (ASTile * tile, MyStyle * style, unsigned int state)
{
	register ASLabel *lbl = &(tile->data.label);
	register int i;
	ASImage *im;
	int flip = ASTileFlip (*tile);

	if (lbl->rendered[state] != NULL) {
		safe_asimage_destroy (lbl->rendered[state]);
		lbl->rendered[state] = NULL;
	}

	im = mystyle_draw_text_image (style, lbl->text, lbl->encoding);
	LOCAL_DEBUG_OUT
			("state(%d)->style(\"%s\")->text(\"%s\")->image(%p)->flip(%d)",
			 state, style ? style->name : "none", lbl->text, im, flip);
	if (flip != 0) {
		int w = im->width;
		int h = im->height;

		if (get_flags (flip, FLIP_VERTICAL)) {
			w = im->height;
			h = im->width;
		}
		lbl->rendered[state] = flip_asimage (ASDefaultVisual,
																				 im, 0, 0, w, h, flip, ASA_ASImage,
																				 100, ASIMAGE_QUALITY_DEFAULT);
		safe_asimage_destroy (im);
	} else
		lbl->rendered[state] = im;

	tile->width = 0;
	tile->height = 0;
	for (i = 0; i < BAR_STATE_NUM; ++i)
		if (lbl->rendered[i]) {
			if (tile->width < lbl->rendered[i]->width)
				tile->width = lbl->rendered[i]->width;
			if (tile->height < lbl->rendered[i]->height)
				tile->height = lbl->rendered[i]->height;
		}
	if (get_flags (flip, FLIP_VERTICAL)) {
		tile->width += lbl->v_padding * 2;
		tile->height += lbl->h_padding * 2;
	} else {
		tile->width += lbl->h_padding * 2;
		tile->height += lbl->v_padding * 2;
	}
}

static int
set_aslabel_layer (ASTile * tile, ASImageLayer * layer, unsigned int state,
									 ASImage ** scrap_images, int max_width, int max_height)
{
	register ASLabel *lbl = &(tile->data.label);
	CARD32 alpha;
	short h_pad = lbl->h_padding;
	short v_pad = lbl->v_padding;

	layer->im = lbl->rendered[state];
	if (layer->im == NULL)
		if ((layer->im =
				 lbl->rendered[(~state) & BAR_STATE_FOCUS_MASK]) == NULL)
			return 0;

	if (get_flags (ASTileFlip (*tile), FLIP_VERTICAL)) {
		h_pad = lbl->v_padding;
		v_pad = lbl->h_padding;
	}

	layer->dst_x = tile->x + h_pad;
	layer->dst_y = tile->y + v_pad;
	if (layer->im->width < max_width) {
		int cell_size = tile->width - h_pad * 2;

		if (cell_size > max_width)
			cell_size = max_width;
		if (layer->im->width < tile->width)
			layer->dst_x +=
					make_tile_pad (get_flags (tile->flags, AS_TilePadLeft),
												 get_flags (tile->flags, AS_TilePadRight),
												 cell_size, layer->im->width);
		layer->clip_width = layer->im->width;
	} else
		layer->clip_width = (max_width > h_pad) ? max_width - h_pad : 1;
	if (layer->im->height < max_height) {
		int cell_size = tile->height - v_pad * 2;

		if (cell_size > max_height)
			cell_size = max_height;
		if (layer->im->height < tile->height)
			layer->dst_y +=
					make_tile_pad (get_flags (tile->flags, AS_TilePadTop),
												 get_flags (tile->flags, AS_TilePadBottom),
												 cell_size, layer->im->height);
		layer->clip_height = layer->im->height;
	} else
		layer->clip_height = (max_height > v_pad) ? max_height - v_pad : 1;

	layer->clip_height = min (layer->im->height, max_height);
	alpha = ARGB32_ALPHA8 (layer->im->back_color);
	if (alpha < 0x00FF && alpha > 1)
		layer->tint = MAKE_ARGB32 ((alpha >> 1), 0x007F, 0x007F, 0x007F);
	return 1;
}


/********************************************************************/
/* ASTBarData :                                                     */
/********************************************************************/
ASTileTypeHandler ASTileTypeHandlers[AS_TileTypes] = {
	{
	"Spacer", NULL, NULL, NULL}, {
	"Buttons", free_asbtn_block, check_btn_point, NULL,
				set_asbtn_block_layer}, {
	"Image", free_asimage_tile, NULL, NULL, set_asimage_layer}, {
	"Label", free_aslabel, NULL, aslabel_style_changed, set_aslabel_layer}, {
	"none", NULL, NULL, NULL}, {
	"none", NULL, NULL, NULL}, {
	"none", NULL, NULL, NULL}, {
	"freed", NULL, NULL, NULL}
};

