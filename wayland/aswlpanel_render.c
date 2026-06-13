#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlpanel_internal.h"
#include "aswlicon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms);

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

static int clamp_int(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static bool as_panel_edge_is_vertical(enum as_panel_edge edge)
{
	return edge == ASWL_PANEL_EDGE_LEFT || edge == ASWL_PANEL_EDGE_RIGHT;
}

/* Fill the panel background from an image-backed MyStyle BackPixmap: type 128
 * tiles the pixmap, type 127 scales it to fill. Typical AfterStep BackPixmap
 * images are opaque, so a straight ARGB copy matches the wl_shm buffer. */
static bool as_buffer_fill_backpixmap_image(struct as_buffer *buf,
                                            const char *path, bool scaled)
{
	if (buf == NULL || buf->data == NULL || path == NULL || path[0] == '\0')
		return false;
	if (buf->width <= 0 || buf->height <= 0)
		return false;

	uint32_t *img = NULL;
	int iw = 0;
	int ih = 0;
	if (!aswl_icon_load_argb(path, &img, &iw, &ih) || img == NULL || iw <= 0 || ih <= 0) {
		free(img);
		return false;
	}

	uint32_t *dst = (uint32_t *)buf->data;
	int stride_px = buf->stride / 4;
	for (int y = 0; y < buf->height; y++) {
		uint32_t *row = dst + (size_t)y * stride_px;
		int sy = scaled ? (int)((int64_t)y * ih / buf->height) : (y % ih);
		if (sy < 0)
			sy = 0;
		else if (sy >= ih)
			sy = ih - 1;
		const uint32_t *srow = img + (size_t)sy * iw;
		for (int x = 0; x < buf->width; x++) {
			int sx = scaled ? (int)((int64_t)x * iw / buf->width) : (x % iw);
			if (sx < 0)
				sx = 0;
			else if (sx >= iw)
				sx = iw - 1;
			row[x] = srow[sx];
		}
	}
	free(img);
	return true;
}

static void as_state_draw(struct as_state *state, struct as_buffer *buf)
{
	bool pager_panel = (state->pager_mode && as_panel_edge_is_vertical(state->edge) && !state->dock_mode);
	bool winlist_strip = (!state->dock_mode && state->button_count == 0 &&
	                      (state->window_list_focused_only || state->window_list_topmost_only));
	bool winlist_has_window = winlist_strip && (as_state_visible_window_nth(state, 0) != NULL);

	if (winlist_strip && !winlist_has_window) {
		/*
		 * X11's WinList module effectively disappears when there's no focused
		 * window to show, leaving only a 1px strip behind. Match that "empty"
		 * look by making the surface fully transparent except for a single
		 * top border line.
		 */
		as_buffer_fill_rect(buf, 0, 0, buf->width, buf->height, 0x00000000u);

		uint32_t border_color = state->theme.panel_border;
		if ((border_color >> 24) == 0)
			border_color = 0xFF000000u;
		as_buffer_fill_rect(buf, 0, 0, buf->width, 1, border_color);
		return;
	}

	const struct aswl_gradient *bg_grad = &state->theme.panel_bg_gradient;
	uint32_t bg_color = state->theme.panel_bg;
	int bg_backpix_type = state->theme.panel_back_pixmap_type;
	uint32_t bg_backpix_tint = state->theme.panel_back_pixmap_tint;
	bool bg_backpix_filled = false;
	if (winlist_has_window) {
		/* WinList uses window styles by default; match that look when we're a WinList-only strip. */
		bg_grad = &state->theme.frame_inactive_gradient;
		bg_color = state->theme.frame_inactive_bg;
		bg_backpix_type = 0;
		bg_backpix_tint = 0;
	}

	if (pager_panel) {
		/* Pager tiles can be translucent; start with a fully transparent surface so the wallpaper shows through. */
		as_buffer_fill_rect(buf, 0, 0, buf->width, buf->height, 0x00000000u);
	} else {
		if (bg_backpix_type == 129 || bg_backpix_type == 149) {
			int ox = 0;
			int oy = 0;
			bool snap_ok = as_state_ensure_bg_snapshot(state);
			int ow = state->output_width;
			int oh = state->output_height;
			if (snap_ok && (ow <= 0 || oh <= 0)) {
				ow = state->bg_snapshot.width;
				oh = state->bg_snapshot.height;
			}

			bool origin_ok = false;
			bool dims_ok = false;
			if (snap_ok) {
				origin_ok = as_state_compute_surface_origin_for_output(state, ow, oh, &ox, &oy);
				dims_ok = (state->bg_snapshot.width == ow && state->bg_snapshot.height == oh);
				if (origin_ok && dims_ok)
					bg_backpix_filled = as_buffer_fill_backpixmap_tint(buf, &state->bg_snapshot, ox, oy, bg_backpix_tint);
			}

			if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
				static bool printed = false;
				if (!printed) {
					printed = true;
					fprintf(stderr,
					        "aswlpanel: backpix: type=%d tint=0x%08X dock=%d edge=%d output=%dx%d surface=%dx%d margins t=%d r=%d b=%d l=%d\n",
					        bg_backpix_type,
					        bg_backpix_tint,
					        state->dock_mode,
					        state->edge,
					        state->output_width,
					        state->output_height,
					        state->width,
					        state->height,
					        state->margins.top,
					        state->margins.right,
					        state->margins.bottom,
					        state->margins.left);
					fprintf(stderr,
					        "aswlpanel: backpix: snapshot_ok=%d path=%s snap=%dx%d stride=%d origin_ok=%d origin=%d,%d dims_ok=%d fill_ok=%d\n",
					        snap_ok,
					        state->bg_snapshot.path != NULL ? state->bg_snapshot.path : "(null)",
					        state->bg_snapshot.width,
					        state->bg_snapshot.height,
					        state->bg_snapshot.stride,
					        origin_ok,
					        ox,
					        oy,
					        dims_ok,
					        bg_backpix_filled);
				}
			}
		}

		if (!bg_backpix_filled &&
		    (bg_backpix_type == 128 || bg_backpix_type == 127) &&
		    state->theme.panel_back_pixmap_path != NULL) {
			bg_backpix_filled = as_buffer_fill_backpixmap_image(
			        buf, state->theme.panel_back_pixmap_path,
			        bg_backpix_type == 127);
		}

		if (!bg_backpix_filled) {
			if (aswl_gradient_is_valid(bg_grad)) {
				as_buffer_fill_style_rect(buf,
				                          0,
				                          0,
				                          buf->width,
				                          buf->height,
				                          bg_grad,
				                          bg_color,
				                          0);
			} else {
				uint32_t grad_top = aswl_color_lighten(bg_color, 24);
				uint32_t grad_bot = aswl_color_darken(bg_color, 24);
				as_buffer_paint_vertical_gradient(buf, grad_top, grad_bot);
			}
		}
	}

	struct as_layout layout;
	if (!as_state_get_layout(state, &layout))
		return;

	uint32_t *pixels = (uint32_t *)buf->data;
	int stride_px = buf->stride / 4;
	(void)aswl_font_set_scale(&state->font, layout.text_scale);
	int text_h = layout.text_h;

	int rx = layout.pad;
	int ry = layout.pad;

	if (state->pager_mode && layout.vertical && !state->dock_mode) {
		int cols = state->pager_columns > 0 ? state->pager_columns : 2;
		int rows = state->pager_rows > 0 ? state->pager_rows : 2;
		if (cols < 1)
			cols = 1;
		if (rows < 1)
			rows = 1;

		/* X11 pager look: tight stacked desks with black borders, a styled title bar, translucent desk background, grid,
		 * and a viewport selection frame.
		 */
		const int desk_border = 1;
		uint32_t border_color = state->theme.pager_border;
		if ((border_color >> 24) == 0)
			border_color = 0xFF000000u;
		uint32_t grid_color = state->theme.pager_grid;
		if ((grid_color >> 24) == 0)
			grid_color = 0xFF2D3332u;
		uint32_t selection_color = state->theme.pager_selection;
		if ((selection_color >> 24) == 0)
			selection_color = 0xFFCCAD8Du;

		size_t ws_count = 0;
		for (size_t i = 0; i < state->button_count; i++) {
			if (as_command_parse_workspace_target(state->buttons[i].command, NULL))
				ws_count++;
		}
		if (ws_count == 0)
			return;

		int total_span = buf->height - 1;
		int w = buf->width;
		int right = w - 1;

		size_t ws_pos = 0;
		for (size_t i = 0; i < state->button_count; i++) {
			uint32_t ws_target = 0;
			if (!as_command_parse_workspace_target(state->buttons[i].command, &ws_target))
				continue;

			bool active_ws = ws_target == state->current_workspace;

			int y0 = (int)((int64_t)total_span * (int64_t)ws_pos / (int64_t)ws_count);
			int y1 = (int)((int64_t)total_span * (int64_t)(ws_pos + 1) / (int64_t)ws_count);
			int desk_h = y1 - y0 + 1;

			/* Desk border. */
			as_buffer_fill_rect(buf, 0, y0, w, 1, border_color);
			as_buffer_fill_rect(buf, 0, y1, w, 1, border_color);
			as_buffer_fill_rect(buf, 0, y0, 1, desk_h, border_color);
			as_buffer_fill_rect(buf, right, y0, 1, desk_h, border_color);

			int inner_x = desk_border;
			int inner_y = y0 + desk_border;
			int inner_w = w - 2 * desk_border;
			int inner_h = desk_h - 2 * desk_border;
			if (inner_w <= 0 || inner_h <= 0) {
				ws_pos++;
				continue;
			}

			int min_title_h = text_h + 2 * layout.icon_pad;
			int title_h = clamp_int(21, min_title_h, inner_h);

				const struct aswl_gradient *title_grad =
					active_ws ? &state->theme.panel_ws_active_gradient : &state->theme.panel_ws_inactive_gradient;
				uint32_t title_bg = active_ws ? state->theme.panel_ws_active_bg : state->theme.panel_ws_inactive_bg;
				uint32_t title_fg = active_ws ? state->theme.panel_ws_active_fg : state->theme.panel_ws_inactive_fg;
				as_buffer_fill_style_rect(buf, inner_x, inner_y, inner_w, title_h, title_grad, title_bg, 0);
				as_buffer_draw_bevel_rect(buf, inner_x, inner_y, inner_w, title_h, title_bg, false);

				const char *label = state->buttons[i].label != NULL ? state->buttons[i].label : "";
				int label_w = aswl_font_text_width(&state->font, label);
				int tx_pad = layout.icon_pad;
				int label_tw = inner_w - 2 * tx_pad;
			int tx = inner_x + tx_pad;
			int tw = label_tw;
			if (label_w > 0 && label_w < label_tw) {
				tx = inner_x + inner_w - tx_pad - label_w;
				tw = label_w;
			}
			int ty = inner_y + (title_h - text_h) / 2;
			if (tw > 0) {
				aswl_font_draw_text(&state->font,
				                    pixels,
				                    buf->width,
				                    buf->height,
				                    stride_px,
				                    tx,
				                    ty,
				                    label,
				                    tw,
				                    title_fg);
			}

			int bg_x = inner_x;
			int bg_y = inner_y + title_h;
			int bg_w = inner_w;
			int bg_h = inner_h - title_h;

			if (bg_w > 0 && bg_h > 0) {
				const struct aswl_gradient *desk_grad =
					aswl_gradient_is_valid(&state->theme.desk_gradient) ? &state->theme.desk_gradient : NULL;
				uint32_t desk_bg = state->theme.desk_bg != 0 ? state->theme.desk_bg : 0x77222222u;
				as_buffer_fill_style_rect(buf, bg_x, bg_y, bg_w, bg_h, desk_grad, desk_bg, 0);

				/* Mini-window markers (scale against full virtual screen size: output × pages). */
				int ow = state->output_width;
				int oh = state->output_height;
				int64_t vsw = (ow > 0) ? (int64_t)ow * (int64_t)cols : 0;
				int64_t vsh = (oh > 0) ? (int64_t)oh * (int64_t)rows : 0;

				if (ow > 0 && oh > 0 && vsw > 0 && vsh > 0) {
					for (size_t widx = 0; widx < state->window_count; widx++) {
						const struct as_window *win = &state->windows[widx];
						if ((win->flags & ASWL_WINDOW_FLAG_MAPPED) == 0)
							continue;
						if (win->workspace != ws_target)
							continue;
						if (win->w <= 0 || win->h <= 0)
							continue;

						int sx = bg_x + (int)((int64_t)win->x * (int64_t)bg_w / vsw);
						int sy = bg_y + (int)((int64_t)win->y * (int64_t)bg_h / vsh);
						int sw = (int)((int64_t)win->w * (int64_t)bg_w / vsw);
						int sh = (int)((int64_t)win->h * (int64_t)bg_h / vsh);
						if (sw < 2)
							sw = 2;
						if (sh < 2)
							sh = 2;

						int x0 = sx;
						int y0w = sy;
						int x1w = sx + sw;
						int y1w = sy + sh;

						int cx0 = x0 < bg_x ? bg_x : x0;
						int cy0 = y0w < bg_y ? bg_y : y0w;
						int cx1 = x1w > bg_x + bg_w ? bg_x + bg_w : x1w;
						int cy1 = y1w > bg_y + bg_h ? bg_y + bg_h : y1w;
						if (cx1 <= cx0 || cy1 <= cy0)
							continue;

						uint32_t win_bg = state->theme.frame_inactive_bg;
						if ((win->flags & ASWL_WINDOW_FLAG_FOCUSED) != 0)
							win_bg = state->theme.frame_active_bg;
						if ((win_bg >> 24) == 0)
							win_bg = aswl_color_lighten(desk_bg, 24);

						int rw = cx1 - cx0;
						int rh = cy1 - cy0;
						as_buffer_fill_rect(buf, cx0, cy0, rw, rh, win_bg);

						if (rh >= 6) {
							int th = 2;
							if (th > rh)
								th = rh;
							uint32_t win_title_bg = aswl_color_darken(win_bg, 48);
							as_buffer_fill_rect(buf, cx0, cy0, rw, th, win_title_bg);
						}
					}
				}

				/* Grid lines (drawn above client markers). */
				if (cols > 1) {
					for (int c = 1; c < cols; c++) {
						int gx = bg_x + (int)((int64_t)bg_w * (int64_t)c / (int64_t)cols);
						as_buffer_fill_rect(buf, gx, bg_y, 1, bg_h, grid_color);
					}
				}
				if (rows > 1) {
					for (int r = 1; r < rows; r++) {
						int gy = bg_y + (int)((int64_t)bg_h * (int64_t)r / (int64_t)rows);
						as_buffer_fill_rect(buf, bg_x, gy, bg_w, 1, grid_color);
					}
				}

				/* Viewport selection frame (drawn on top). */
				if (active_ws && cols >= 1 && rows >= 1) {
					int page_w = bg_w / cols;
					int page_h = bg_h / rows;
					if (page_w < 1)
						page_w = 1;
					if (page_h < 1)
						page_h = 1;

					int sel_x = bg_x;
					int sel_y = bg_y;

					struct {
						int x, y, w, h;
					} bars[4] = {
						{ sel_x - 1, sel_y - 1, page_w + 2, 1 },                 /* top */
						{ sel_x - 1, sel_y - 1, 1, page_h + 2 },                 /* left */
						{ sel_x - 1, sel_y + page_h + 1, page_w + 2, 1 },        /* bottom */
						{ sel_x + page_w + 1, sel_y - 1, 1, page_h + 2 },        /* right */
					};

					/* Clip to desk interior so the black border stays intact. */
					int clip_x0 = inner_x;
					int clip_y0 = inner_y;
					int clip_x1 = inner_x + inner_w;
					int clip_y1 = y1 - desk_border + 1;
					for (size_t b = 0; b < 4; b++) {
						int bx0 = bars[b].x;
						int by0 = bars[b].y;
						int bx1 = bars[b].x + bars[b].w;
						int by1 = bars[b].y + bars[b].h;

						if (bx0 < clip_x0)
							bx0 = clip_x0;
						if (by0 < clip_y0)
							by0 = clip_y0;
						if (bx1 > clip_x1)
							bx1 = clip_x1;
						if (by1 > clip_y1)
							by1 = clip_y1;

						int bw = bx1 - bx0;
						int bh = by1 - by0;
						if (bw > 0 && bh > 0)
							as_buffer_fill_rect(buf, bx0, by0, bw, bh, selection_color);
					}
				}
			}

			ws_pos++;
		}

		return;
	}

	for (size_t i = 0; i < state->button_count; i++) {
		int idx = (int)i;
		uint32_t ws_target = 0;
		bool is_ws = as_command_parse_workspace_target(state->buttons[i].command, &ws_target);
		bool active_ws = is_ws && ws_target == state->current_workspace;

		uint32_t base_bg = state->theme.panel_button_bg;
		uint32_t base_fg = state->theme.panel_button_fg;
		if (is_ws) {
			if (active_ws) {
				base_bg = state->theme.panel_ws_active_bg;
				base_fg = state->theme.panel_ws_active_fg;
			} else {
				base_bg = state->theme.panel_ws_inactive_bg;
				base_fg = state->theme.panel_ws_inactive_fg;
			}
		}

		uint8_t nudge = 0;
		if (idx == state->pressed_index)
			nudge = 48;
		else if (idx == state->hover_index)
			nudge = 24;

		int main = as_state_button_main_size(state, &layout, i);
		int rw = layout.vertical ? layout.cross : main;
		int rh = layout.vertical ? main : layout.cross;

		const struct aswl_gradient *grad = &state->theme.panel_button_gradient;
		if (is_ws)
			grad = active_ws ? &state->theme.panel_ws_active_gradient : &state->theme.panel_ws_inactive_gradient;

		if (!(bg_backpix_filled && state->dock_mode))
			as_buffer_fill_style_rect(buf, rx, ry, rw, rh, grad, base_bg, nudge);
		uint32_t bevel_bg = nudge != 0 ? aswl_color_nudge(base_bg, nudge) : base_bg;
		as_buffer_draw_bevel_rect(buf, rx, ry, rw, rh, bevel_bg, idx == state->pressed_index);

		/* Icon box (placeholder): big initial + small index digit. */
		int is = layout.icon_size;
		int max_is = rw - 2 * layout.icon_pad;
		if (is > max_is)
			is = max_is;
		if (is > rh - 2 * layout.icon_pad)
			is = rh - 2 * layout.icon_pad;
		if (is < 0)
			is = 0;

		int ix = rx + layout.icon_pad;
		int iy = ry + layout.icon_pad;
		if (is > 0) {
				if (state->dock_mode) {
					/*
					 * Match the X11 baseline icon placement.
					 *
					 * - Wharf tiles (top dock) sit slightly up/left.
					 * - MonitorWharf tiles (right strip) are left-biased but vertically centered.
					 *
					 * For the screenshot baseline layout this maps cleanly to panel edge:
					 * top-dock Wharf on `top`, MonitorWharf strip on `right`.
					 */
					int dock_nudge_x = -2;
					int dock_nudge_y = -2;
					if (state->edge == ASWL_PANEL_EDGE_RIGHT) {
						dock_nudge_x = -1;
						dock_nudge_y = 0;
					}
					ix = rx + (rw - is) / 2 + dock_nudge_x;
					iy = ry + (rh - is) / 2 + dock_nudge_y;
				} else if (layout.vertical) {
					ix = rx + layout.icon_pad;
					iy = ry + (rh - is) / 2;
				}
		}

		if (is > 0) {
			const bool draw_icon_frame = !state->dock_mode;
			if (draw_icon_frame) {
				uint32_t icon_bg = aswl_color_darken(bevel_bg, 32);

				as_buffer_fill_rect(buf, ix, iy, is, is, icon_bg);
				as_buffer_draw_bevel_rect(buf, ix, iy, is, is, icon_bg, idx == state->pressed_index);
			}

			bool drew_image = false;
			const char *cmd = state->buttons[i].command;
			if (state->dock_mode && cmd != NULL && cmd[0] == '@' && strncasecmp(cmd + 1, "clock", 5) == 0) {
				char time_buf[16] = { 0 };
				const char *clock_override = getenv("ASWLPANEL_CLOCK_OVERRIDE");
				if (clock_override != NULL && clock_override[0] != '\0') {
					snprintf(time_buf, sizeof(time_buf), "%s", clock_override);
				} else {
					time_t now = time(NULL);
					struct tm tm_now;
					if (localtime_r(&now, &tm_now) != NULL)
						(void)strftime(time_buf, sizeof(time_buf), "%H:%M", &tm_now);
				}

				if (time_buf[0] == '\0')
					strcpy(time_buf, "--:--");

				/*
				 * In classic AfterStep MonitorWharf, the clock is typically a
				 * swallowed xclock (dark background + cyan digits). Mimic that
				 * by letting the clock fully cover the dock tile.
				 */
				uint32_t clock_bg = 0xFF1A1A1A;
				uint32_t clock_fg = 0xFF00FFFF;
				as_buffer_fill_rect(buf, rx, ry, rw, rh, clock_bg);

				(void)aswl_font_set_scale(&state->font, layout.text_scale);
				int tw = aswl_font_text_width(&state->font, time_buf);
				int maxw = rw - 4;
				if (maxw < 1)
					maxw = rw;
				int draw_w = tw;
				if (draw_w < 1 || draw_w > maxw)
					draw_w = maxw;

				int tx = rx + (rw - draw_w) / 2 + 2;
				int ty = ry + (rh - text_h) / 4;
				aswl_font_draw_text(&state->font,
				                    pixels,
				                    buf->width,
				                    buf->height,
				                    stride_px,
				                    tx,
				                    ty,
				                    time_buf,
				                    draw_w,
				                    clock_fg);
				drew_image = true;
			} else if (state->dock_mode && cmd != NULL && cmd[0] == '@' && strncasecmp(cmd + 1, "xeyes", 5) == 0) {
				/*
				 * Classic MonitorWharf commonly swallows `xeyes` into a 64x64 tile.
				 * We can't actually swallow Xwayland clients into this layer-shell
				 * surface, but we can render a deterministic “xeyes-ish” tile.
				 */
				const uint32_t white = as_premul_argb(0xFFFFFFFF);
				const uint32_t black = as_premul_argb(0xFF000000);

				int bx = rx;
				int by = ry;
				int bw = rw;
				int bh = rh;

				int eye_area_w = bw - 6;
				int eye_area_h = bh - 14;
				if (eye_area_w > 0 && eye_area_h > 0) {
					int eye_area_x = bx + (bw - eye_area_w) / 2;
					int eye_area_y = by + (bh - eye_area_h) / 2;

					int gap = clamp_int(bw / 6, 8, 14); /* 64px tile -> 10px gap */
					int eye_w = (eye_area_w - gap) / 2;
					int eye_h = eye_area_h;
					if (eye_w > 0 && eye_h > 0) {
						int cy = eye_area_y + eye_h / 2;
						int left_x = eye_area_x;
						int right_x = eye_area_x + eye_w + gap;
						int left_cx = left_x + eye_w / 2;
						int right_cx = right_x + eye_w / 2;

						double erx = (double)eye_w / 2.0;
						double ery = (double)eye_h / 2.0;
						double outline_t = 0.85;

						for (int eye_idx = 0; eye_idx < 2; eye_idx++) {
							int ex = eye_idx == 0 ? left_x : right_x;
							int ecx = eye_idx == 0 ? left_cx : right_cx;
							for (int yy = eye_area_y; yy < eye_area_y + eye_h; yy++) {
								uint32_t *row = pixels + yy * stride_px;
								for (int xx = ex; xx < ex + eye_w; xx++) {
									double dx = ((double)xx + 0.5 - (double)ecx) / erx;
									double dy = ((double)yy + 0.5 - (double)cy) / ery;
									double d = dx * dx + dy * dy;
									if (d > 1.0)
										continue;
									row[xx] = (d >= outline_t) ? black : white;
								}
							}

							int pupil_r = clamp_int(eye_w / 5, 3, 8);
							int pupil_dy = clamp_int((eye_h + 15) / 18, 2, 4); /* 50px eye -> ~3px */
							int pcx = ecx;
							int pcy = cy + pupil_dy;
							for (int yy = pcy - pupil_r; yy <= pcy + pupil_r; yy++) {
								if (yy < 0 || yy >= buf->height)
									continue;
								int dy = yy - pcy;
								uint32_t *row = pixels + yy * stride_px;
								for (int xx = pcx - pupil_r; xx <= pcx + pupil_r; xx++) {
									if (xx < 0 || xx >= buf->width)
										continue;
									int dx = xx - pcx;
									if (dx * dx + dy * dy <= pupil_r * pupil_r)
										row[xx] = black;
								}
							}
						}
					}
				}

				drew_image = true;
			} else if (state->buttons[i].icon_argb != NULL && state->buttons[i].icon_w > 0 && state->buttons[i].icon_h > 0) {
				int box = is - (draw_icon_frame ? 2 : 0);
				int dw = state->buttons[i].icon_w;
				int dh = state->buttons[i].icon_h;

				if (box > 0 && dw > 0 && dh > 0) {
					if (dw > box || dh > box) {
						double sx = (double)box / (double)dw;
						double sy = (double)box / (double)dh;
						double s = sx < sy ? sx : sy;
						dw = (int)((double)dw * s + 0.5);
						dh = (int)((double)dh * s + 0.5);
						if (dw < 1)
							dw = 1;
						if (dh < 1)
							dh = 1;
					}

					int px = ix + (draw_icon_frame ? 1 : 0) + (box - dw) / 2;
					int py = iy + (draw_icon_frame ? 1 : 0) + (box - dh) / 2;
					as_buffer_draw_image_bilinear(buf,
					                             px,
					                             py,
					                             dw,
					                             dh,
					                             state->buttons[i].icon_argb,
					                             state->buttons[i].icon_w,
					                             state->buttons[i].icon_h);
					drew_image = true;
				}
			}

			if (!drew_image) {
				char icon_c = '?';
				const char *label = state->buttons[i].label;
				if (label != NULL) {
					while (*label == ' ' || *label == '\t')
						label++;
					if (*label != '\0')
						icon_c = *label;
				}

				int icon_scale = clamp_int((is - 4) / 7, 1, layout.icon_scale);
				int gw = aswl_font5x7_glyph_w(icon_scale);
				int gh = aswl_font5x7_glyph_h(icon_scale);
				int gx = ix + (is - gw) / 2;
				int gy = iy + (is - gh) / 2;
				aswl_font5x7_draw_glyph(pixels, buf->width, buf->height, stride_px, gx, gy, icon_c, icon_scale, base_fg);
				if (!state->dock_mode) {
					int number = (int)i + 1;
					int digit = number % 10;
					char digit_c = (char)('0' + digit);
					int badge_scale = clamp_int(is / 16, 1, 2);
					aswl_font5x7_draw_glyph(pixels,
					                        buf->width,
					                        buf->height,
					                        stride_px,
					                        ix + 2,
					                        iy + 2,
					                        digit_c,
					                        badge_scale,
					                        aswl_color_nudge(base_fg, 64));
				}
			}
		}

		/* Text label. */
		if (!state->dock_mode) {
			if (layout.vertical) {
				int tx = rx + layout.icon_pad + is + layout.text_gap;
				int tw = rw - (layout.icon_pad + is + layout.text_gap + layout.icon_pad);
				if (tw > 0) {
					int ty = ry + (rh - text_h) / 2;
					aswl_font_draw_text(&state->font,
					                    pixels,
					                    buf->width,
					                    buf->height,
					                    stride_px,
					                    tx,
					                    ty,
					                    state->buttons[i].label,
					                    tw,
					                    base_fg);
				}
			} else {
				int tx = rx + layout.icon_pad + layout.icon_size + layout.text_gap;
				int tw = rw - (layout.icon_pad + layout.icon_size + layout.text_gap + layout.icon_pad);
				int ty = ry + (rh - text_h) / 2;
				if (tw > 0)
					aswl_font_draw_text(&state->font,
					                    pixels,
					                    buf->width,
					                    buf->height,
					                    stride_px,
					                    tx,
					                    ty,
					                    state->buttons[i].label,
					                    tw,
					                    base_fg);
			}
		}

		if (layout.vertical)
			ry += rh + layout.spacing;
		else
			rx += rw + layout.spacing;
	}

	/* AfterStep Wharf/MonitorWharf often has a one-sided bevel on the outer edge. */
		if (state->dock_mode && layout.dock_gutter > 0 && state->edge == ASWL_PANEL_EDGE_RIGHT && layout.vertical) {
			int gx = layout.pad + layout.cross;
			int gy = layout.pad;
			int gw = layout.dock_gutter;
			int gh = buf->height - 2 * layout.pad;
			if (gw > 0 && gh > 0 && gx >= 0 && gx + gw <= buf->width) {
				uint32_t bg = state->theme.panel_bg;
				uint32_t hi = aswl_color_hilite(bg);
				uint32_t lo = aswl_color_shadow(bg);
				as_buffer_fill_rect(buf, gx, gy, 1, gh, hi);
				as_buffer_fill_rect(buf, gx + gw - 1, gy, 1, gh, lo);
			}
		}

	if (state->dock_mode)
		goto draw_border;

	int win_x = layout.pad;
	int win_y = layout.vertical ? (ry + layout.spacing) : layout.pad;
	bool winlist_frame_style = winlist_has_window;

	for (size_t vis_idx = 0;; vis_idx++) {
		struct as_window *win = as_state_visible_window_nth(state, vis_idx);
		if (win == NULL)
			break;

		int idx = (int)state->button_count + (int)vis_idx;

		uint32_t base_bg = winlist_frame_style ? state->theme.frame_inactive_bg : state->theme.panel_button_bg;
		uint32_t base_fg = winlist_frame_style ? state->theme.frame_inactive_fg : state->theme.panel_button_fg;
		bool is_focused = (win->flags & ASWL_WINDOW_FLAG_FOCUSED) != 0;
		if (winlist_strip)
			is_focused = false;
		if (is_focused) {
			base_bg = winlist_frame_style ? state->theme.frame_active_bg : state->theme.panel_ws_active_bg;
			base_fg = winlist_frame_style ? state->theme.frame_active_fg : state->theme.panel_ws_active_fg;
		}

		uint8_t nudge = 0;
		if (idx == state->pressed_index)
			nudge = 48;
		else if (idx == state->hover_index)
			nudge = 24;

		int main = as_state_window_main_size(state, &layout, win);
		int rw = layout.vertical ? layout.cross : main;
		int rh = layout.vertical ? main : layout.cross;
		if (layout.vertical) {
			if (win_y + rh > state->height - layout.pad)
				break;
		} else {
			if (rx + rw > state->width - layout.pad)
				break;
		}

		int wx = layout.vertical ? win_x : rx;
		int wy = layout.vertical ? win_y : ry;

		const struct aswl_gradient *grad =
			winlist_frame_style ? &state->theme.frame_inactive_gradient : &state->theme.panel_button_gradient;
		if (is_focused)
			grad = winlist_frame_style ? &state->theme.frame_active_gradient : &state->theme.panel_ws_active_gradient;

		as_buffer_fill_style_rect(buf, wx, wy, rw, rh, grad, base_bg, nudge);
		uint32_t bevel_bg = nudge != 0 ? aswl_color_nudge(base_bg, nudge) : base_bg;
		as_buffer_draw_bevel_rect(buf, wx, wy, rw, rh, bevel_bg, idx == state->pressed_index);

		int tx = wx + layout.icon_pad;
		int tw = rw - 2 * layout.icon_pad;
		const char *label = as_window_label(win);
		if ((state->window_list_focused_only || state->window_list_topmost_only) &&
		    !layout.vertical && state->button_count == 0) {
			int label_w = aswl_font_text_width(&state->font, label);
			if (label_w > 0 && label_w < tw) {
				tx = wx + rw - layout.icon_pad - label_w;
				tw = label_w;
			}
		}
		int ty = wy + (rh - text_h) / 2;
		if (tw > 0)
			aswl_font_draw_text(&state->font, pixels, buf->width, buf->height, stride_px, tx, ty, label, tw, base_fg);

		if (layout.vertical)
			win_y += rh + layout.spacing;
		else
			rx += rw + layout.spacing;
	}

draw_border:
	{
		if (state->dock_mode)
			return;

		(void)winlist_has_window;
		/* AfterStep bevel provides the visual "border" (no extra solid outline). */
		if (buf->width >= 2 && buf->height >= 2)
			as_buffer_draw_bevel_rect(buf, 0, 0, buf->width, buf->height, bg_color, false);
	}
}

void draw_and_commit(struct as_state *state)
{
	if (state->surface == NULL)
		return;
	if (!state->configured)
		return;
	if (state->width <= 0 || state->height <= 0)
		return;

	if (!as_state_ensure_buffers(state)) {
		state->running = false;
		return;
	}

	struct as_buffer *buf = as_state_acquire_buffer(state);
	if (buf == NULL) {
		state->needs_redraw = true;
		return;
	}

	state->needs_redraw = false;
	as_state_draw(state, buf);

	buf->busy = true;
	wl_surface_attach(state->surface, buf->wl_buffer, 0, 0);
	wl_surface_damage(state->surface, 0, 0, buf->width, buf->height);
	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);
	state->frame_cb = wl_surface_frame(state->surface);
	wl_callback_add_listener(state->frame_cb, &frame_listener, state);
	wl_surface_commit(state->surface);
}

void schedule_redraw(struct as_state *state)
{
	state->needs_redraw = true;
	if (state->frame_cb == NULL)
		draw_and_commit(state);
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms)
{
	(void)time_ms;
	struct as_state *state = data;

	if (cb != NULL)
		wl_callback_destroy(cb);
	if (state->frame_cb == cb)
		state->frame_cb = NULL;

	if (state->needs_redraw)
		draw_and_commit(state);
}
