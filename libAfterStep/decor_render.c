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

static inline Bool
render_astbar_int (ASTBarData * tbar, ASCanvas * pc, ASImage ** pcache,
									 ASCanvas * origin_canvas)
{
	START_TIME (started);
	ASImage *back = NULL;
	MyStyle *style;
	ASImageBevel bevel;
	ASImageLayer *layers;
	ASImage **scrap_images = NULL;
	ASImage *merged_im = NULL;
	int state;
	ASAltImFormats fmt = ASA_ScratchXImageAndAlpha;
	int l;
	short col_width[AS_TileColumns] = { 0 };
	short row_height[AS_TileRows] = { 0 };
	short col_x[AS_TileColumns] = { 0 };
	short row_y[AS_TileRows] = { 0 };
	short floating_cols[AS_TileColumns] = { 0 };
	short floating_rows[AS_TileRows] = { 0 };
	int floating_cols_count = 0, floating_rows_count = 0;
	short space_left_x, space_left_y;
	int x = 0, y = 0;
	int good_layers = 0;
	Bool res = False;
	Bool render_mask = False;
	merge_scanlines_func merge_func = alphablend_scanlines;
	int h_bevel_size = 0, v_bevel_size = 0;

	/* input control : */
	LOCAL_DEBUG_CALLER_OUT ("tbar(%p)->pc(%p)", tbar, pc);
	if (tbar == NULL || pc == NULL || pc->w == None)
		return -3;
	state = get_flags (tbar->state, BAR_STATE_FOCUS_MASK);
	style = tbar->style[state];
	LOCAL_DEBUG_OUT ("style(%p)->geom(%ux%u%+d%+d)->hilite(0x%X)", style,
									 tbar->width, tbar->height, tbar->root_x, tbar->root_y,
									 tbar->hilite[state]);
	if (tbar->width == 0 || tbar->height == 0)
		return True;								/* nothing to draw anyways */

	if (style == NULL)
		return -2;

	if (origin_canvas == NULL)
		origin_canvas = pc;

	mystyle_make_bevel (style, &bevel, tbar->hilite[state],
											get_flags (tbar->state, BAR_STATE_PRESSED_MASK));
	h_bevel_size = bevel.left_outline + bevel.right_outline;
	v_bevel_size = bevel.top_outline + bevel.bottom_outline;

	/* validating our images : */
	if (tbar->back[state] != NULL) {
		if (tbar->root_x != pc->root_x + (int)pc->bw + tbar->win_x ||
				tbar->root_y != pc->root_y + (int)pc->bw + tbar->win_y) {
			update_astbar_transparency (tbar, pc, False);
		}
	}
	if ((back = tbar->back[state]) != NULL) {
		if (back->width != tbar->width || back->height != tbar->height ||
				((tbar->rendered_root_x != tbar->root_x
					|| tbar->rendered_root_y != tbar->root_y)
				 && style->texture_type > TEXTURE_PIXMAP)) {
			flush_tbar_state_backs (tbar, state);
			back = NULL;
		}
	}
	LOCAL_DEBUG_OUT ("back(%p), vertical?%s", back,
									 get_flags (tbar->state,
															BAR_FLAGS_VERTICAL) ? "Yes" : "No");
	if (back == NULL) {
		if ((get_flags (tbar->state, BAR_FLAGS_CROP_UNFOCUSED_BACK)
				 && !IsASTBarFocused (tbar))
				|| (get_flags (tbar->state, BAR_FLAGS_CROP_FOCUSED_BACK)
						&& IsASTBarFocused (tbar))) {
			if (pcache) {
				if (*pcache == NULL) {
					*pcache = mystyle_make_image (style,
																				origin_canvas->root_x,
																				origin_canvas->root_y,
																				origin_canvas->width +
																				origin_canvas->bw,
																				origin_canvas->height +
																				origin_canvas->bw,
																				get_flags (tbar->state,
																									 BAR_FLAGS_VERTICAL)
																				? FLIP_VERTICAL : 0);
				}
					if (*pcache)
						back = tile_asimage (ASDefaultVisual, *pcache,
																 tbar->root_x - origin_canvas->root_x +
																 bevel.left_outline,
																 tbar->root_y - origin_canvas->root_y +
																 bevel.top_outline, tbar->width,
																 tbar->height, TINT_LEAVE_SAME, ASA_ASImage,
																 0, ASIMAGE_QUALITY_DEFAULT);
				}

				if (back == NULL)
					back = mystyle_crop_image (style,
																		 pc->root_x,
																		 pc->root_y,
																		 tbar->root_x - origin_canvas->root_x +
																		 bevel.left_outline,
																		 tbar->root_y - origin_canvas->root_y +
																		 bevel.top_outline, tbar->width,
																		 tbar->height,
																		 origin_canvas->width +
																		 (int)origin_canvas->bw,
																	 origin_canvas->height +
																	 (int)origin_canvas->bw,
																	 get_flags (tbar->state,
																							BAR_FLAGS_VERTICAL) ?
																	 FLIP_VERTICAL : 0);
		} else
			back = mystyle_make_image (style,
																 tbar->root_x + bevel.left_outline,
																 tbar->root_y + bevel.top_outline,
																 tbar->width, tbar->height,
																 get_flags (tbar->state,
																						BAR_FLAGS_VERTICAL) ?
																 FLIP_VERTICAL : 0);
		tbar->back[state] = back;
		LOCAL_DEBUG_OUT ("back-try2(%p)", back);
		if (back == NULL)
			return -1;
	}

	/*mystyle_make_bevel (style, &bevel, 0, get_flags (tbar->state, BAR_STATE_PRESSED_MASK));
	 * in unfocused and unpressed state we render pixmap and set
	 * window's background to it
	 * in focused state or in pressed state we render to
	 * the window directly, and we'll need to be handling the expose
	 * events
	 */
	/* very complicated layout code : */
	/* pass 1: first we determine width/height of each row/column, as well as count of layers : */
	for (l = 0; l < tbar->tiles_num; ++l)
		if (ASTileType (tbar->tiles[l]) != AS_TileFreed) {
			register int pos = ASTileCol (tbar->tiles[l]);

			good_layers += ASTileSublayers (tbar->tiles[l]);
			if (!ASTileIgnoreWidth (tbar->tiles[l])) {
				int w = tbar->tiles[l].width;

				if (w > tbar->width)
					w = tbar->width;
				if (col_width[pos] < w)
					col_width[pos] = w;
				/* floating column has at least one element padded/resizable in horizontal dir,
				 * and no fixed elements */
				if (!ASTileHFloating (tbar->tiles[l]))
					floating_cols[pos] = -1;
				else if (floating_cols[pos] >= 0)
					++floating_cols[pos];
			}

			pos = ASTileRow (tbar->tiles[l]);
			if (!ASTileIgnoreHeight (tbar->tiles[l])) {
				int h = tbar->tiles[l].height;

				if (h > tbar->height)
					h = tbar->height;
				if (row_height[pos] < h)
					row_height[pos] = h;
				/* floating row has at least one element padded/resizable in vertical dir,
				 * and no fixed elements */
				if (!ASTileVFloating (tbar->tiles[l]))
					floating_rows[pos] = -1;
				else if (floating_rows[pos] >= 0)
					++floating_rows[pos];
			}
		}
	/* pass 2: see how much space we have left that needs to be floating to some rows/columns : */
	LOCAL_DEBUG_OUT ("bar_size = %dx%d", tbar->width, tbar->height);

	space_left_x =
			tbar->width - (h_bevel_size + tbar->h_border * 2 +
										 bevel.left_inline + bevel.right_inline);
	space_left_y =
			tbar->height - (v_bevel_size + tbar->v_border * 2 +
											bevel.top_inline + bevel.bottom_inline);
	LOCAL_DEBUG_OUT ("from: space_left_x = %d, space_left_y = %d",
									 space_left_x, space_left_y);
	for (l = 0; l < AS_TileColumns; ++l) {
		if (col_width[l] > 0)
			space_left_x -= col_width[l] + tbar->h_spacing;
		if (row_height[l] > 0)
			space_left_y -= row_height[l] + tbar->v_spacing;
		if (floating_cols[l] > 0)
			++floating_cols_count;
		else
			floating_cols[l] = 0;
		if (floating_rows[l] > 0)
			++floating_rows_count;
		else
			floating_rows[l] = 0;
	}
	space_left_x += tbar->h_spacing;
	space_left_y += tbar->v_spacing;
	LOCAL_DEBUG_OUT
			("to  : space_left_x = %d, space_left_y = %d, floating_cols = %d, floating_rows = %d",
			 space_left_x, space_left_y, floating_cols_count,
			 floating_rows_count);
	LOCAL_DEBUG_OUT ("h_spacing = %d, v_spacing = %d", tbar->h_spacing,
									 tbar->v_spacing);
	/* pass 3: now we determine spread padding among affected cols : */
	if (floating_cols_count > 0 && space_left_x != 0)
		for (l = 0; l < AS_TileColumns; ++l) {
			if (floating_cols[l] > 0) {
				register int change = space_left_x / floating_cols_count;

				if (change == 0)
					change = (space_left_x > 0) ? 1 : -1;
				col_width[l] += change;
				if (col_width[l] < 0) {
					change -= col_width[l];
					col_width[l] = 0;
				}
				--floating_cols_count;
				space_left_x -= change;
				if (space_left_x == 0)
					break;
			}
		}
	if (space_left_x < -1)
		space_left_x =
				trim_astbar_grid_dim (&(col_width[0]), tbar->width, space_left_x);

	/* pass 4: now we determine spread padding among affected rows : */
	if (floating_rows_count > 0 && space_left_y != 0)
		for (l = 0; l < AS_TileRows; ++l) {
			if (floating_rows[l] > 0) {
				register int change = space_left_y / floating_rows_count;

				if (change == 0)
					change = (space_left_y > 0) ? 1 : -1;
				row_height[l] += change;
				if (row_height[l] < 0) {
					change -= row_height[l];
					row_height[l] = 0;
				}
				--floating_rows_count;
				space_left_y -= change;
				if (space_left_y == 0)
					break;
			}
		}
	if (space_left_y < -1)
		space_left_y =
				trim_astbar_grid_dim (&(row_height[0]), tbar->height,
															space_left_y);


	/* pass 5: now we determine offset of each row/column : */
	x = bevel.left_outline + bevel.left_inline + tbar->h_border;
	y = bevel.top_outline + bevel.top_inline + tbar->v_border;
	for (l = 0; l < AS_TileColumns; ++l) {
		col_x[l] = x;
		row_y[l] = y;
		if (col_width[l] > 0)
			x += col_width[l] + tbar->h_spacing;
		if (row_height[l] > 0)
			y += row_height[l] + tbar->v_spacing;
	}
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	for (l = 0; l < AS_TileColumns; ++l)
		show_progress ("\tcolumn[%d] = %d%+d floating?%d", l, col_width[l],
									 col_x[l], floating_cols[l]);
	for (l = 0; l < AS_TileRows; ++l)
		show_progress ("\trow[%d] = x%d%+d floating?%d", l, row_height[l],
									 row_y[l], floating_rows[l]);
#endif
	/* Done with layout */

	layers = create_image_layers (good_layers + 1);
	scrap_images = safecalloc (good_layers + 1, sizeof (ASImage *));
	layers[0].im = back;
	layers[0].bevel = &bevel;
	if (tbar->width > h_bevel_size)
		layers[0].clip_width = tbar->width - h_bevel_size;
	else
		layers[0].clip_width = 1;

	if (tbar->height > v_bevel_size)
		layers[0].clip_height = tbar->height - v_bevel_size;
	else
		layers[0].clip_height = 1;

	/* now we need to loop through tiles and add them to the layers list at correct locations */
	good_layers = 1;
	for (l = 0; l < tbar->tiles_num; ++l) {
		int type = ASTileType (tbar->tiles[l]);

		if (ASTileTypeHandlers[type].set_layer_handler) {
			int row = ASTileRow (tbar->tiles[l]);
			int col = ASTileCol (tbar->tiles[l]);
			int pad_x = 0, pad_y = 0;

			if (!ASTileHResizeable (tbar->tiles[l]))
				pad_x =
						make_tile_pad (get_flags
													 (tbar->tiles[l].flags, AS_TilePadLeft),
													 get_flags (tbar->tiles[l].flags,
																			AS_TilePadRight), col_width[col],
													 tbar->tiles[l].width);
			tbar->tiles[l].x = col_x[col] + pad_x;

			if (!ASTileVResizeable (tbar->tiles[l]))
				pad_y =
						make_tile_pad (get_flags (tbar->tiles[l].flags, AS_TilePadTop),
													 get_flags (tbar->tiles[l].flags,
																			AS_TilePadBottom), row_height[row],
													 tbar->tiles[l].height);
			tbar->tiles[l].y = row_y[row] + pad_y;
			good_layers +=
					ASTileTypeHandlers[type].set_layer_handler (&(tbar->tiles[l]),
																											&(layers
																												[good_layers]),
																											state,
																											&(scrap_images
																												[good_layers]),
																											col_width[col] -
																											pad_x,
																											row_height[row] -
																											pad_y);
		}
	}
	merge_func =
			mystyle_translate_texture_type (tbar->composition_method[state]);
	for (l = 1; l < good_layers; ++l)
		layers[l].merge_scanlines = merge_func;

#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	show_progress ("MERGING TBAR %p image %dx%d using merge_func %d FROM:",
								 tbar, tbar->width, tbar->height,
								 tbar->composition_method[state]);
	print_astbar_tiles (tbar);
	show_progress ("USING %d layers:", good_layers);
	for (l = 0; l < good_layers; ++l) {
		show_progress ("\t %3.3d: %p %+d%+d %ux%u%+d%+d", l, layers[l].im,
									 layers[l].dst_x, layers[l].dst_y,
									 layers[l].clip_width, layers[l].clip_height,
									 layers[l].clip_x, layers[l].clip_y);
	}
#endif
	render_mask = (style->texture_type == TEXTURE_SHAPED_PIXMAP ||
								 style->texture_type == TEXTURE_SHAPED_SCALED_PIXMAP
								 || get_flags (pc->state, CANVAS_FORCE_MASK));
#ifdef SHAPE
	LOCAL_DEBUG_OUT ("render_mask = %d, shape = %p", render_mask, pc->shape);
	if (render_mask) {
		/*     fmt = ASA_ASImage; */
	} else if (pc->shape)
		fill_canvas_mask (pc, tbar->win_x, tbar->win_y, tbar->width,
											tbar->height);
#endif
	if (get_flags (ASDefaultVisual->glx_support, ASGLX_UseForImageTx))
		fmt = ASA_ASImage;

	LOCAL_DEBUG_OUT ("fmt = %d, hue = %d, sat = %d", fmt, tbar->hue[state],
									 tbar->sat[state]);
	if (tbar->hue[state] > 0 || tbar->sat[state] >= 0) {
		ASImage *tmp_im =
				merge_layers (ASDefaultVisual, &layers[0], good_layers,
											tbar->width, tbar->height, ASA_ASImage, 0,
											ASIMAGE_QUALITY_DEFAULT);
		if (tmp_im) {
			merged_im = adjust_asimage_hsv (ASDefaultVisual, tmp_im,
																			0, 0,
																			tmp_im->width, tmp_im->height,
																			0, 360,
																			tbar->hue[state] <
																			0 ? 0 : tbar->hue[state],
																			tbar->sat[state] <
																			0 ? 0 : tbar->sat[state], 0, fmt, 0,
																			ASIMAGE_QUALITY_DEFAULT);
			destroy_asimage (&tmp_im);
		}
	} else
		merged_im =
				merge_layers (ASDefaultVisual, &layers[0], good_layers,
											tbar->width, tbar->height, fmt, 0,
											ASIMAGE_QUALITY_DEFAULT);
	for (l = 0; l < good_layers; ++l)
		if (scrap_images[l])
			safe_asimage_destroy (scrap_images[l]);
	free (scrap_images);
	free (layers);

	if (merged_im) {
		res = draw_canvas_image (pc, merged_im, tbar->win_x, tbar->win_y);

#ifdef SHAPE
		if (render_mask)
			draw_canvas_mask (pc, merged_im, tbar->win_x, tbar->win_y);
#endif
		destroy_asimage (&merged_im);
		if (res)
			clear_flags (tbar->state, BAR_FLAGS_REND_PENDING);
	}
	SHOW_TIME ("rendering", started);
	return res;
}

#ifdef TRACE_render_astbar
#undef render_astbar
Bool
trace_render_astbar (ASTBarData * tbar, ASCanvas * pc, const char *file,
										 int line)
{
	Bool res;

	fprintf (stderr, "D>%s(%d):render_astbar(%p,%p)\n", file, line, tbar,
					 pc);
	res = render_astbar_int (tbar, pc, NULL, NULL);
	fprintf (stderr, "D>%s(%d):render_astbar(%p,%p) returned %d\n", file,
					 line, tbar, pc, res);
	if (tbar && res <= 0)
		fprintf (stderr,
						 "D>%s(%d):render_astbar tbar data: state %lX, %ux%u%+d%+d, root %+d%+d, styles %p,%p, tiles_num %d, tiles %p, canvas %X\n",
						 file, line, tbar->state, tbar->width, tbar->height,
						 tbar->win_x, tbar->win_y, tbar->root_x, tbar->root_y,
						 tbar->style[0], tbar->style[1], tbar->tiles_num, tbar->tiles,
						 pc->canvas);
	return res;
}
#else
Bool render_astbar (ASTBarData * tbar, ASCanvas * pc)
{
	return render_astbar_int (tbar, pc, NULL, NULL);
}
#endif

Bool
render_astbar_cached_back (ASTBarData * tbar, ASCanvas * pc,
													 ASImage ** cache, ASCanvas * origin_canvas)
{
	return render_astbar_int (tbar, pc, cache, origin_canvas);
}


int check_astbar_point (ASTBarData * tbar, int root_x, int root_y)
{
	int context = C_NO_CONTEXT;

	if (tbar) {
		LOCAL_DEBUG_OUT
				("bar's geometry = %dx%d%+d%+d, pointer posish = %+d%+d",
				 tbar->width, tbar->height, tbar->root_x, tbar->root_y, root_x,
				 root_y);
		root_x -= tbar->root_x;
		root_y -= tbar->root_y;
		if (0 <= root_x && tbar->width > root_x && 0 <= root_y
				&& tbar->height > root_y) {
			int tmp_context;
			int i = tbar->tiles_num;

			context = tbar->context;
			while (--i >= 0) {
				int type = ASTileType (tbar->tiles[i]);

				if (ASTileTypeHandlers[type].check_point_handler) {
					int tile_x = root_x - tbar->tiles[i].x;
					int tile_y = root_y - tbar->tiles[i].y;

					LOCAL_DEBUG_OUT ("checking tile %d, %dx%d%+d%+d", i,
													 tbar->tiles[i].width, tbar->tiles[i].height,
													 tbar->tiles[i].x, tbar->tiles[i].y);
					if (tile_x >= 0 && tile_y >= 0 && tile_x < tbar->tiles[i].width
							&& tile_y < tbar->tiles[i].height)
						if ((tmp_context =
								 ASTileTypeHandlers[type].check_point_handler (&
																															 (tbar->
																																tiles[i]),
																															 tile_x,
																															 tile_y)) !=
								C_NO_CONTEXT) {
							context = tmp_context;
							break;
						}
				}
			}
		}
	}
	return context;
}

void
on_astbar_pointer_action (ASTBarData * tbar, int context, Bool leave,
													Bool pointer_moved)
{
	static ASBalloon *last_balloon = NULL;

	LOCAL_DEBUG_CALLER_OUT ("%p, %s, %d", tbar, context2text (context),
													leave);

	if (tbar == NULL) {
		tbar = FocusedBar;
		leave = True;
	}
	if (pointer_moved)
		last_balloon = NULL;
	LOCAL_DEBUG_OUT ("%p, %s, %d", tbar, context2text (context), leave);
	if (tbar) {
		ASBalloon *balloon = tbar->balloon;

		if (context != 0 && context != C_TITLE && context != tbar->context) {
			int i = tbar->tiles_num;

			LOCAL_DEBUG_OUT
					("looking for a tile with context %X in the set of %d tiles",
					 context, i);
			while (--i >= 0) {
				if (ASTileType (tbar->tiles[i]) == AS_TileBtnBlock) {
					ASBtnBlock *bb = (ASBtnBlock *) & (tbar->tiles[i].data.bblock);
					int k = bb->buttons_num;

					LOCAL_DEBUG_OUT
							("tile %d is a button block - lets see if any of %d buttons have context",
							 i, k);
					while (--k >= 0)
						if (bb->buttons[k].context == context) {
							balloon = bb->buttons[k].balloon;
							LOCAL_DEBUG_OUT
									("button %d has required contex. balloon = %p", k,
									 balloon);
							break;
						}
					if (k >= 0)
						break;
				}
			}
		}
		if (leave || balloon == NULL) {
			withdraw_balloon (balloon);
			if (tbar == FocusedBar)
				FocusedBar = NULL;
		} else if (balloon != last_balloon) {
			display_balloon (balloon);
			FocusedBar = tbar;
		}
		last_balloon = balloon;
	}
}

void
set_astbar_balloon2 (ASTBarData * tbar, ASBalloonState * balloon_state,
										 int context, const char *text, unsigned long encoding)
{
	if (tbar != NULL) {
		if (context != 0 && context != C_TITLE && context != tbar->context) {
			int i = tbar->tiles_num;

			LOCAL_DEBUG_OUT
					("looking for a tile with context %X in the set of %d tiles",
					 context, i);
			while (--i >= 0) {
				if (ASTileType (tbar->tiles[i]) == AS_TileBtnBlock) {
					ASBtnBlock *bb = (ASBtnBlock *) & (tbar->tiles[i].data.bblock);
					int k = bb->buttons_num;

					LOCAL_DEBUG_OUT
							("tile %d is a button block - lets see if any of %d buttons have context",
							 i, k);
					while (--k >= 0)
						if (bb->buttons[k].context == context) {
							if (bb->buttons[k].balloon != NULL) {
								balloon_set_text (bb->buttons[k].balloon, text, encoding);
								LOCAL_DEBUG_OUT
										("changed balloon for tbar(%p)->context(0x%X)->button(%d)->encoding(%ld)->text(%s)->balloon(%p)",
										 tbar, context, k, encoding, text,
										 bb->buttons[k].balloon);
							} else {
								bb->buttons[k].balloon =
										create_asballoon_with_text_for_state (balloon_state,
																													tbar, text,
																													encoding);
								LOCAL_DEBUG_OUT
										("created balloon for tbar(%p)->ct(0x%X)->btn(%d)->enc(%ld)->text(%s)->balloon(%p)",
										 tbar, context, k, encoding, text,
										 bb->buttons[k].balloon);
							}
							return;
						}
				}
			}
		} else {
			if (tbar->balloon != NULL) {
				balloon_set_text (tbar->balloon, text, encoding);
				LOCAL_DEBUG_OUT
						("changed tbar balloon for tbar(%p)->context(0x%X)->text(%s)->balloon(%p)",
						 tbar, context, text, tbar->balloon);
			} else {
				tbar->balloon =
						create_asballoon_with_text_for_state (balloon_state, tbar,
																									text, encoding);
				LOCAL_DEBUG_OUT
						("created tbar balloon for tbar(%p)->context(0x%X)->text(%s)->balloon(%p)",
						 tbar, context, text, tbar->balloon);
			}

		}
	}
}

void
set_astbar_balloon (ASTBarData * tbar, int context, const char *text,
										unsigned long encoding)
{
	if (tbar != NULL)
		set_astbar_balloon2 (tbar, NULL, context, text, encoding);
}
