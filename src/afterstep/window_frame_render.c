/*
 * Copyright (c) 2002,2003 Sasha Vasko <sasha@aftercode.net>
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

/* Frame render-glue (transparency, hilite, pressure, tbar size) split out
 * from window_frame.c. */

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"
#include "../../libAfterStep/wmprops.h"
#include "window_frame_internal.h"

/* this gets called when Root background changes : */
void update_window_transparency (ASWindow * asw, Bool force)
{
	ASOrientation *od = get_orientation_data (asw);
	int i;
	ASCanvas *changed_canvases[6] = { NULL, NULL, NULL, NULL, NULL, NULL };

	if (!ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		ASCanvas *fc;
		for (i = 0; i < FRAME_PARTS; ++i)
			if (asw->frame_bars[i]) {
				fc = asw->frame_sides[od->tbar2canvas_xref[i]];
				if (!check_canvas_offscreen (fc)) {
					update_astbar_transparency (asw->frame_bars[i], fc, force);
					if (DoesBarNeedsRendering (asw->frame_bars[i])) {
						changed_canvases[od->tbar2canvas_xref[i]] = fc;
						render_astbar (asw->frame_bars[i], fc);
					}
				}
			}

		if (asw->tbar) {
			fc = asw->frame_sides[od->tbar_side];
			if (!check_canvas_offscreen (fc)) {
				update_astbar_transparency (asw->tbar, fc, force);
				if (DoesBarNeedsRendering (asw->tbar)) {
					changed_canvases[od->tbar_side] = fc;
					render_astbar (asw->tbar, fc);
				}
			}
		}
	} else {
		if (asw->icon_button) {
			update_astbar_transparency (asw->icon_button, asw->icon_canvas,
																	force);
			if (DoesBarNeedsRendering (asw->icon_button)) {
				changed_canvases[4] = asw->icon_canvas;
				render_astbar (asw->icon_button, asw->icon_canvas);
			}
		}
		if (asw->icon_title) {
			update_astbar_transparency (asw->icon_title,
																	asw->icon_title_canvas ? asw->
																	icon_title_canvas : asw->icon_canvas,
																	force);
			if (DoesBarNeedsRendering (asw->icon_title)) {
				if (asw->icon_title_canvas != NULL
						&& asw->icon_title_canvas != asw->icon_canvas)
					changed_canvases[5] = asw->icon_title_canvas;
				else
					changed_canvases[4] = asw->icon_canvas;
				render_astbar (asw->icon_title,
											 asw->icon_title_canvas ? asw->
											 icon_title_canvas : asw->icon_canvas);
			}
		}
	}
	for (i = 0; i < 6; ++i)
		if (changed_canvases[i]) {
			invalidate_canvas_save (changed_canvases[i]);
			update_canvas_display (changed_canvases[i]);
		}

}

static void
on_frame_bars_moved (ASWindow * asw, unsigned int side, ASOrientation * od)
{
	ASCanvas *canvas = asw->frame_sides[side];

	update_astbar_transparency (asw->frame_bars[side], canvas, False);
	if (side == od->tbar_side) {
		update_astbar_transparency (asw->tbar, canvas, False);
		update_astbar_transparency (asw->
																frame_bars[od->tbar_mirror_corners[0]],
																canvas, False);
		update_astbar_transparency (asw->
																frame_bars[od->tbar_mirror_corners[1]],
																canvas, False);
	} else if (side == od->sbar_side) {
		update_astbar_transparency (asw->
																frame_bars[od->sbar_mirror_corners[0]],
																canvas, False);
		update_astbar_transparency (asw->
																frame_bars[od->sbar_mirror_corners[1]],
																canvas, False);
	}
}

void update_window_frame_moved (ASWindow * asw, ASOrientation * od)
{
	int i;

	if (ASWIN_GET_FLAGS (asw, AS_Dead | AS_MoveresizeInProgress))
		return;

	handle_canvas_config (asw->client_canvas);

	if (!check_window_offscreen (asw))
		if (asw->internal && asw->internal->on_moveresize)
			asw->internal->on_moveresize (asw->internal, None);

	if (!check_frame_offscreen (asw))
		for (i = 0; i < FRAME_SIDES; ++i)
			if (asw->frame_sides[i]) {
				handle_canvas_config (asw->frame_sides[i]);
				if (!check_frame_side_offscreen (asw, i)) {	/* canvas has been resized - resize tbars!!! */
					on_frame_bars_moved (asw, i, od);
				}
			}
}

void update_window_frame_pos (ASWindow * asw)
{
	if (ASWIN_GET_FLAGS (asw, AS_Dead | AS_MoveresizeInProgress))
		return;

	handle_canvas_config (asw->client_canvas);

/*
	if (!check_window_offscreen( asw ))
		if( asw->internal && asw->internal->on_moveresize )
			asw->internal->on_moveresize( asw->internal, None );
*/

	if (!check_frame_offscreen (asw)) {
		int i;
		ASFlagType changes = 0;
		for (i = 0; i < FRAME_SIDES; ++i)
			if (asw->frame_sides[i])
				changes |= handle_canvas_config (asw->frame_sides[i]);

		if (changes != 0)
			update_window_transparency (asw, False);
	}
}

int update_window_tbar_size (ASWindow * asw)
{
	int tbar_size = 0;
	if (asw->tbar) {
		unsigned int tbar_width = 0;
		unsigned int tbar_height = 0;
		int x_offset = 0, y_offset = 0;
		LOCAL_DEBUG_OUT ("IsVertical = %lX",
										 ASWIN_HFLAGS (asw, AS_VerticalTitle));

		if (ASWIN_HFLAGS (asw, AS_VerticalTitle)) {
			tbar_size = calculate_astbar_width (asw->tbar);
			tbar_width = tbar_size;
			tbar_height = asw->frame_canvas->height;
#ifdef SHAPE
			if (get_flags
					(asw->frame_data->condense_titlebar, ALIGN_LEFT | ALIGN_RIGHT)) {
				int condensed = calculate_astbar_height (asw->tbar);
				if (condensed < tbar_height) {
					if (get_flags (asw->frame_data->condense_titlebar, ALIGN_LEFT)) {
						y_offset = tbar_height - condensed;
						if (get_flags
								(asw->frame_data->condense_titlebar, ALIGN_RIGHT))
							y_offset /= 2;
					}
					tbar_height = condensed;
				}
			}
#endif
		} else {
			tbar_size = calculate_astbar_height (asw->tbar);
			tbar_width = asw->frame_canvas->width;
			tbar_height = tbar_size;
#ifdef SHAPE
			if (get_flags
					(asw->frame_data->condense_titlebar, ALIGN_LEFT | ALIGN_RIGHT)) {
				int condensed = calculate_astbar_width (asw->tbar);
				if (condensed < tbar_width) {
					if (get_flags (asw->frame_data->condense_titlebar, ALIGN_RIGHT)) {
						x_offset = tbar_width - condensed;
						if (get_flags (asw->frame_data->condense_titlebar, ALIGN_LEFT))
							x_offset /= 2;
					}
					tbar_width = condensed;
				}
			}
#endif
		}
		/* we need that to set up tbar size : */
		set_astbar_size (asw->tbar, tbar_width, tbar_height);
		/* does not matter if we use frame canvas, since part's
		 * canvas resizes at frame canvas origin anyway */
		move_astbar (asw->tbar, asw->frame_canvas, x_offset, y_offset);

	}
	return tbar_size;
}

void on_window_hilite_changed (ASWindow * asw, Bool focused)
{
	ASOrientation *od = get_orientation_data (asw);
	int i;

	LOCAL_DEBUG_CALLER_OUT ("(%p,%s focused)", asw, focused ? "" : "not");
	if (AS_ASSERT (asw))
		return;

	for (i = FRAME_SIDES; --i >= 0;) {
		ASCanvas *update_canvas = asw->frame_sides[i];
		int k;
		if (swap_save_canvas (update_canvas)) {
			LOCAL_DEBUG_OUT ("canvas save swapped for side %d", i);
			update_canvas = NULL;
		}
		if (i == od->tbar_side)
			set_astbar_focused (asw->tbar, update_canvas, focused);

		for (k = 0; k < FRAME_PARTS; ++k)
			if (od->tbar2canvas_xref[k] == i)
				set_astbar_focused (asw->frame_bars[k], update_canvas, focused);
	}
	/* now posting all the changes on display : */
	for (i = FRAME_SIDES; --i >= 0;)
		if (is_canvas_dirty (asw->frame_sides[i]))
			update_canvas_display (asw->frame_sides[i]);
	if (asw->internal && asw->internal->on_hilite_changed)
		asw->internal->on_hilite_changed (asw->internal, NULL, focused);
	if (ASWIN_GET_FLAGS (asw, AS_ShapedDecor))
		SetShape (asw, 0);
	if (ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		set_astbar_focused (asw->icon_button, asw->icon_canvas, focused);
		set_astbar_focused (asw->icon_title, asw->icon_title_canvas, focused);
		if (is_canvas_dirty (asw->icon_canvas))
			update_canvas_display (asw->icon_canvas);
		if (is_canvas_dirty (asw->icon_title_canvas))
			update_canvas_display (asw->icon_title_canvas);
	}
}

void on_window_pressure_changed (ASWindow * asw, int pressed_context)
{
	ASOrientation *od = get_orientation_data (asw);
	LOCAL_DEBUG_CALLER_OUT ("(%p,%s)", asw, context2text (pressed_context));

	if (AS_ASSERT (asw) || asw->status == NULL)
		return;

	if (!ASWIN_GET_FLAGS (asw, AS_Iconic)) {
		register int i = FRAME_PARTS;
		/* Titlebar */
		set_astbar_btn_pressed (asw->tbar, pressed_context);	/* must go before next call to properly redraw :  */
		set_astbar_pressed (asw->tbar, asw->frame_sides[od->tbar_side],
												pressed_context & C_TITLE);
		/* frame decor : */
		for (i = FRAME_PARTS; --i >= 0;)
			set_astbar_pressed (asw->frame_bars[i],
													asw->frame_sides[od->tbar2canvas_xref[i]],
													pressed_context & (C_FrameN << i));
		/* now posting all the changes on display : */
		for (i = FRAME_SIDES; --i >= 0;)
			if (is_canvas_dirty (asw->frame_sides[i])) {
				update_canvas_display (asw->frame_sides[i]);
			}
		if (asw->internal && asw->internal->on_pressure_changed)
			asw->internal->on_pressure_changed (asw->internal,
																					pressed_context & C_CLIENT);
	} else {											/* Iconic !!! */

		set_astbar_pressed (asw->icon_button, asw->icon_canvas,
												pressed_context & C_IconButton);
		set_astbar_pressed (asw->icon_title, asw->icon_title_canvas,
												pressed_context & C_IconTitle);
		if (is_canvas_dirty (asw->icon_canvas))
			update_canvas_display (asw->icon_canvas);
		if (is_canvas_dirty (asw->icon_title_canvas))
			update_canvas_display (asw->icon_title_canvas);
	}
}
