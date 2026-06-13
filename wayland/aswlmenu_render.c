#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlmenu_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aswlicon.h"

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

static uint32_t as_premul_argb(uint32_t argb)
{
	uint32_t a = (argb >> 24) & 0xFFu;
	if (a == 0)
		return 0;
	if (a == 255u)
		return argb;

	uint32_t r = (argb >> 16) & 0xFFu;
	uint32_t g = (argb >> 8) & 0xFFu;
	uint32_t b = argb & 0xFFu;

	r = (r * a + 127u) / 255u;
	g = (g * a + 127u) / 255u;
	b = (b * a + 127u) / 255u;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static uint32_t as_unpremul_argb(uint32_t argb)
{
	uint32_t a = (argb >> 24) & 0xFFu;
	if (a == 0)
		return 0;
	if (a == 255u)
		return argb;

	uint32_t r = (argb >> 16) & 0xFFu;
	uint32_t g = (argb >> 8) & 0xFFu;
	uint32_t b = argb & 0xFFu;

	r = (r * 255u + a / 2u) / a;
	g = (g * 255u + a / 2u) / a;
	b = (b * 255u + a / 2u) / a;

	if (r > 255u)
		r = 255u;
	if (g > 255u)
		g = 255u;
	if (b > 255u)
		b = 255u;

	return (a << 24) | (r << 16) | (g << 8) | b;
}

static void as_buffer_paint_solid(struct as_buffer *buf, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	uint32_t premul = as_premul_argb(argb);
	uint32_t *pixels = buf->data;
	size_t count = (size_t)buf->width * (size_t)buf->height;
	for (size_t i = 0; i < count; i++)
		pixels[i] = premul;
}

static void as_buffer_fill_rect(struct as_buffer *buf, int x, int y, int w, int h, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	if (w <= 0 || h <= 0)
		return;

	int x1 = x;
	int y1 = y;
	int x2 = x + w;
	int y2 = y + h;

	if (x1 < 0)
		x1 = 0;
	if (y1 < 0)
		y1 = 0;
	if (x2 > buf->width)
		x2 = buf->width;
	if (y2 > buf->height)
		y2 = buf->height;

	if (x2 <= x1 || y2 <= y1)
		return;

	uint32_t premul = as_premul_argb(argb);
	for (int yy = y1; yy < y2; yy++) {
		uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)yy * (size_t)buf->stride);
		for (int xx = x1; xx < x2; xx++)
			row[xx] = premul;
	}
}

static void as_buffer_draw_bevel_rect(struct as_buffer *buf, int x, int y, int w, int h, uint32_t base_argb, bool sunken)
{
	if (buf == NULL || buf->data == NULL)
		return;
	if (w <= 1 || h <= 1)
		return;

	int x1 = x;
	int y1 = y;
	int x2 = x + w;
	int y2 = y + h;
	if (x1 < 0)
		x1 = 0;
	if (y1 < 0)
		y1 = 0;
	if (x2 > buf->width)
		x2 = buf->width;
	if (y2 > buf->height)
		y2 = buf->height;
	if (x2 - x1 <= 1 || y2 - y1 <= 1)
		return;

	uint32_t *pixels = (uint32_t *)buf->data;
	int stride_px = buf->stride / 4;

	int top = y1;
	int bottom = y2 - 1;
	int left = x1;
	int right = x2 - 1;

	uint32_t relief_fore = aswl_color_hilite(base_argb);
	uint32_t relief_back = aswl_color_shadow(base_argb);

	uint32_t hi_color = sunken ? relief_back : relief_fore;
	uint32_t lo_color = sunken ? relief_fore : relief_back;
	uint32_t hihi_color = aswl_color_hilite(relief_fore);
	uint32_t lolo_color = relief_back;
	uint32_t hilo_color = aswl_color_average(hi_color, lo_color);

	uint32_t hi_premul = as_premul_argb(hi_color);
	uint32_t lo_premul = as_premul_argb(lo_color);

	/* Top/bottom edges */
	uint32_t *row_top = pixels + (size_t)top * (size_t)stride_px;
	uint32_t *row_bot = pixels + (size_t)bottom * (size_t)stride_px;
	for (int xx = left; xx <= right; xx++) {
		row_top[xx] = hi_premul;
		row_bot[xx] = lo_premul;
	}

	/* Left/right edges (excluding corners). */
	for (int yy = top + 1; yy <= bottom - 1; yy++) {
		uint32_t *row = pixels + (size_t)yy * (size_t)stride_px;
		row[left] = hi_premul;
		row[right] = lo_premul;
	}

	/* Corners. */
	row_top[left] = as_premul_argb(sunken ? lolo_color : hihi_color);
	row_top[right] = as_premul_argb(hilo_color);
	row_bot[left] = as_premul_argb(hilo_color);
	row_bot[right] = as_premul_argb(sunken ? hihi_color : lolo_color);
}

static double as_gradient_t(int type, int x, int y, int w, int h)
{
	if (w <= 1)
		w = 1;
	if (h <= 1)
		h = 1;

	switch (type) {
	case 1:
		type = 6;
		break;
	case 2:
		type = 8;
		break;
	case 4:
		type = 9;
		break;
	default:
		break;
	}

	double fx = (double)x;
	double fy = (double)y;
	double fw = (double)(w - 1);
	double fh = (double)(h - 1);

	switch (type) {
	case 6:
		if (fw <= 0.0 && fh <= 0.0)
			return 0.0;
		if (fw <= 0.0)
			return fy / fh;
		if (fh <= 0.0)
			return fx / fw;
		return 0.5 * ((fx / fw) + (fy / fh));
	case 7:
		if (fw <= 0.0 && fh <= 0.0)
			return 0.0;
		if (fw <= 0.0)
			return (fh - fy) / fh;
		if (fh <= 0.0)
			return fx / fw;
		return 0.5 * ((fx / fw) + ((fh - fy) / fh));
	case 8:
		if (fh <= 0.0)
			return 0.0;
		return fy / fh;
	case 9:
		if (fw <= 0.0)
			return 0.0;
		return fx / fw;
	case 3:
	{
		double mid = fh / 2.0;
		if (mid <= 0.0)
			return 0.0;
		double d = (fy > mid) ? (fy - mid) : (mid - fy);
		double t = 1.0 - (d / mid);
		return t < 0.0 ? 0.0 : t;
	}
	case 5:
	{
		double mid = fw / 2.0;
		if (mid <= 0.0)
			return 0.0;
		double d = (fx > mid) ? (fx - mid) : (mid - fx);
		double t = 1.0 - (d / mid);
		return t < 0.0 ? 0.0 : t;
	}
	default:
		return 0.0;
	}
}

static uint32_t as_gradient_sample(const struct aswl_gradient *grad, double t)
{
	if (!aswl_gradient_is_valid(grad))
		return 0;

	if (t <= grad->offsets[0])
		return grad->colors[0];
	if (t >= grad->offsets[grad->count - 1])
		return grad->colors[grad->count - 1];

	for (size_t i = 0; i + 1 < grad->count; i++) {
		double a = grad->offsets[i];
		double b = grad->offsets[i + 1];
		if (t > b)
			continue;

		double span = b - a;
		if (span <= 0.0)
			return grad->colors[i + 1];

		double local = (t - a) / span;
		if (local < 0.0)
			local = 0.0;
		if (local > 1.0)
			local = 1.0;

		uint8_t tt = (uint8_t)(local * 255.0 + 0.5);
		return aswl_color_blend(grad->colors[i], grad->colors[i + 1], tt);
	}

	return grad->colors[grad->count - 1];
}

static void as_buffer_fill_style_rect(struct as_buffer *buf,
                                      int x,
                                      int y,
                                      int w,
                                      int h,
                                      const struct aswl_gradient *grad,
                                      uint32_t base_argb,
                                      uint8_t nudge)
{
	if (buf == NULL || buf->data == NULL)
		return;
	if (w <= 0 || h <= 0)
		return;

	if (!aswl_gradient_is_valid(grad)) {
		uint32_t c = base_argb;
		if (nudge != 0)
			c = aswl_color_nudge(c, nudge);
		as_buffer_fill_rect(buf, x, y, w, h, c);
		return;
	}

	int x1 = x;
	int y1 = y;
	int x2 = x + w;
	int y2 = y + h;
	if (x1 < 0)
		x1 = 0;
	if (y1 < 0)
		y1 = 0;
	if (x2 > buf->width)
		x2 = buf->width;
	if (y2 > buf->height)
		y2 = buf->height;
	if (x2 <= x1 || y2 <= y1)
		return;

	for (int yy = y1; yy < y2; yy++) {
		uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)yy * (size_t)buf->stride);
		for (int xx = x1; xx < x2; xx++) {
			double t = as_gradient_t(grad->type, xx - x, yy - y, w, h);
			uint32_t c = as_gradient_sample(grad, t);
			if (nudge != 0)
				c = aswl_color_nudge(c, nudge);
			row[xx] = as_premul_argb(c);
		}
	}
}

static void as_buffer_blend_pixel(struct as_buffer *buf, int x, int y, uint32_t src_argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	if (x < 0 || y < 0 || x >= buf->width || y >= buf->height)
		return;

	uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)y * (size_t)buf->stride);
	uint32_t src = as_premul_argb(src_argb);
	uint32_t sa = (src >> 24) & 0xFFu;
	if (sa == 0)
		return;
	if (sa == 255u) {
		row[x] = src;
		return;
	}

	uint32_t dst = row[x];
	uint32_t da = (dst >> 24) & 0xFFu;
	uint32_t inv = 255u - sa;

	uint32_t out_a = sa + (da * inv + 127u) / 255u;
	uint32_t dr = (dst >> 16) & 0xFFu;
	uint32_t dg = (dst >> 8) & 0xFFu;
	uint32_t db = dst & 0xFFu;
	uint32_t sr = (src >> 16) & 0xFFu;
	uint32_t sg = (src >> 8) & 0xFFu;
	uint32_t sb = src & 0xFFu;

	uint32_t out_r = sr + (dr * inv + 127u) / 255u;
	uint32_t out_g = sg + (dg * inv + 127u) / 255u;
	uint32_t out_b = sb + (db * inv + 127u) / 255u;

	row[x] = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

static uint32_t as_sample_image_bilinear_unpremul(const uint32_t *src_argb,
                                                  int sw,
                                                  int sh,
                                                  double gx,
                                                  double gy)
{
	if (src_argb == NULL || sw <= 0 || sh <= 0)
		return 0;

	if (gx < 0.0)
		gx = 0.0;
	if (gy < 0.0)
		gy = 0.0;

	double max_x = (double)(sw - 1);
	double max_y = (double)(sh - 1);
	if (gx > max_x)
		gx = max_x;
	if (gy > max_y)
		gy = max_y;

	int x0 = (int)gx;
	int y0 = (int)gy;
	int x1 = x0 + 1;
	int y1 = y0 + 1;
	if (x1 >= sw)
		x1 = sw - 1;
	if (y1 >= sh)
		y1 = sh - 1;

	double tx = gx - (double)x0;
	double ty = gy - (double)y0;
	if (tx < 0.0)
		tx = 0.0;
	if (ty < 0.0)
		ty = 0.0;
	if (tx > 1.0)
		tx = 1.0;
	if (ty > 1.0)
		ty = 1.0;

	uint32_t p00 = as_premul_argb(src_argb[(size_t)y0 * (size_t)sw + (size_t)x0]);
	uint32_t p10 = as_premul_argb(src_argb[(size_t)y0 * (size_t)sw + (size_t)x1]);
	uint32_t p01 = as_premul_argb(src_argb[(size_t)y1 * (size_t)sw + (size_t)x0]);
	uint32_t p11 = as_premul_argb(src_argb[(size_t)y1 * (size_t)sw + (size_t)x1]);

	double w00 = (1.0 - tx) * (1.0 - ty);
	double w10 = tx * (1.0 - ty);
	double w01 = (1.0 - tx) * ty;
	double w11 = tx * ty;

	double a = (double)((p00 >> 24) & 0xFFu) * w00 +
	           (double)((p10 >> 24) & 0xFFu) * w10 +
	           (double)((p01 >> 24) & 0xFFu) * w01 +
	           (double)((p11 >> 24) & 0xFFu) * w11;
	double r = (double)((p00 >> 16) & 0xFFu) * w00 +
	           (double)((p10 >> 16) & 0xFFu) * w10 +
	           (double)((p01 >> 16) & 0xFFu) * w01 +
	           (double)((p11 >> 16) & 0xFFu) * w11;
	double g = (double)((p00 >> 8) & 0xFFu) * w00 +
	           (double)((p10 >> 8) & 0xFFu) * w10 +
	           (double)((p01 >> 8) & 0xFFu) * w01 +
	           (double)((p11 >> 8) & 0xFFu) * w11;
	double b = (double)(p00 & 0xFFu) * w00 +
	           (double)(p10 & 0xFFu) * w10 +
	           (double)(p01 & 0xFFu) * w01 +
	           (double)(p11 & 0xFFu) * w11;

	uint32_t ia = (uint32_t)(a + 0.5);
	uint32_t ir = (uint32_t)(r + 0.5);
	uint32_t ig = (uint32_t)(g + 0.5);
	uint32_t ib = (uint32_t)(b + 0.5);
	if (ia > 255u)
		ia = 255u;
	if (ir > 255u)
		ir = 255u;
	if (ig > 255u)
		ig = 255u;
	if (ib > 255u)
		ib = 255u;

	uint32_t premul = (ia << 24) | (ir << 16) | (ig << 8) | ib;
	return as_unpremul_argb(premul);
}

static void as_buffer_draw_image_bilinear(struct as_buffer *buf,
                                          int dx,
                                          int dy,
                                          int dw,
                                          int dh,
                                          const uint32_t *src_argb,
                                          int sw,
                                          int sh)
{
	if (buf == NULL || buf->data == NULL)
		return;
	if (src_argb == NULL || sw <= 0 || sh <= 0)
		return;
	if (dw <= 0 || dh <= 0)
		return;

	double sx_scale = 0.0;
	double sy_scale = 0.0;
	if (dw > 1 && sw > 1)
		sx_scale = (double)(sw - 1) / (double)(dw - 1);
	if (dh > 1 && sh > 1)
		sy_scale = (double)(sh - 1) / (double)(dh - 1);

	for (int y = 0; y < dh; y++) {
		double gy = (double)y * sy_scale;
		for (int x = 0; x < dw; x++) {
			double gx = (double)x * sx_scale;
			uint32_t c = as_sample_image_bilinear_unpremul(src_argb, sw, sh, gx, gy);
			as_buffer_blend_pixel(buf, dx + x, dy + y, c);
		}
	}
}

static void as_menu_entry_try_load_icon(struct as_menu_entry *e)
{
	if (e == NULL)
		return;

	if (e->icon_tried)
		return;
	e->icon_tried = true;

	if (e->icon_spec == NULL || e->icon_spec[0] == '\0')
		return;

	uint32_t *pixels = NULL;
	int w = 0;
	int h = 0;
	if (aswl_icon_load_argb(e->icon_spec, &pixels, &w, &h)) {
		e->icon_argb = pixels;
		e->icon_w = w;
		e->icon_h = h;
	} else {
			free(pixels);
		}
	}

static bool as_state_try_load_icon(struct as_state *state,
                                  bool *tried,
                                  uint32_t **out_argb,
                                  int *out_w,
                                  int *out_h,
                                  const char *const specs[])
{
	if (state == NULL || tried == NULL || out_argb == NULL || out_w == NULL || out_h == NULL)
		return false;
	if (*out_argb != NULL && *out_w > 0 && *out_h > 0)
		return true;
	if (*tried)
		return false;
	*tried = true;

	if (specs == NULL)
		return false;

	for (size_t i = 0; specs[i] != NULL; i++) {
		uint32_t *argb = NULL;
		int w = 0;
		int h = 0;
		if (aswl_icon_load_argb(specs[i], &argb, &w, &h) && argb != NULL && w > 0 && h > 0) {
			free(*out_argb);
			*out_argb = argb;
			*out_w = w;
			*out_h = h;
			return true;
		}
		free(argb);
	}

	return false;
}

static void as_state_ensure_close_button_icons(struct as_state *state)
{
	if (state == NULL)
		return;

	static const char *const close_dark_specs[] = { "default-kill-dark", "dots/abi-close", NULL };
	static const char *const close_light_specs[] = { "default-kill-light", "dots/abi-close", NULL };
	static const char *const close_dark_pressed_specs[] = { "default-kill-dark-pressed", "dots/abi-close-push", NULL };
	static const char *const close_light_pressed_specs[] = { "default-kill-light-pressed", "dots/abi-close-push", NULL };

	bool prefer_dark = state->window_list_mode || aswl_color_is_light(state->theme.menu_header_bg);
	const char *const *normal_specs = prefer_dark ? close_dark_specs : close_light_specs;
	const char *const *pressed_specs = prefer_dark ? close_dark_pressed_specs : close_light_pressed_specs;

	(void)as_state_try_load_icon(state,
	                            &state->close_icon_tried,
	                            &state->close_icon_argb,
	                            &state->close_icon_w,
	                            &state->close_icon_h,
	                            normal_specs);
	(void)as_state_try_load_icon(state,
	                            &state->close_icon_pressed_tried,
	                            &state->close_icon_pressed_argb,
	                            &state->close_icon_pressed_w,
	                            &state->close_icon_pressed_h,
	                            pressed_specs);
}

static void as_state_ensure_iconize_button_icons(struct as_state *state)
{
	if (state == NULL)
		return;

	/* look.DEFAULT TitleButton 3 (iconize). */
	static const char *const iconize_dark_specs[] = { "default-iconize-dark", NULL };
	static const char *const iconize_light_specs[] = { "default-iconize-light", NULL };
	static const char *const iconize_dark_pressed_specs[] = { "default-iconize-dark-pressed", NULL };
	static const char *const iconize_light_pressed_specs[] = { "default-iconize-light-pressed", NULL };

	bool prefer_dark = state->window_list_mode || aswl_color_is_light(state->theme.menu_header_bg);
	const char *const *normal_specs = prefer_dark ? iconize_dark_specs : iconize_light_specs;
	const char *const *pressed_specs = prefer_dark ? iconize_dark_pressed_specs : iconize_light_pressed_specs;

	(void)as_state_try_load_icon(state,
	                            &state->iconize_icon_tried,
	                            &state->iconize_icon_argb,
	                            &state->iconize_icon_w,
	                            &state->iconize_icon_h,
	                            normal_specs);
	(void)as_state_try_load_icon(state,
	                            &state->iconize_icon_pressed_tried,
	                            &state->iconize_icon_pressed_argb,
	                            &state->iconize_icon_pressed_w,
	                            &state->iconize_icon_pressed_h,
	                            pressed_specs);
}

static void as_state_ensure_pin_button_icons(struct as_state *state)
{
	if (state == NULL)
		return;

	/* look.DEFAULT TitleButton 5 (pin). */
	static const char *const pin_specs[] = { "default-pin-light", NULL };
	static const char *const pin_pressed_specs[] = { "default-pin-light-pressed", NULL };

	(void)as_state_try_load_icon(state,
	                            &state->pin_icon_tried,
	                            &state->pin_icon_argb,
	                            &state->pin_icon_w,
	                            &state->pin_icon_h,
	                            pin_specs);
	(void)as_state_try_load_icon(state,
	                            &state->pin_icon_pressed_tried,
	                            &state->pin_icon_pressed_argb,
	                            &state->pin_icon_pressed_w,
	                            &state->pin_icon_pressed_h,
	                            pin_pressed_specs);
}

/* Fill the menu body from an image-backed MyStyle BackPixmap (MenuItemStyle):
 * type 128 tiles the pixmap, 127 scales it to fill. Typical AfterStep
 * BackPixmap images are opaque, so a straight ARGB copy matches the buffer. */
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
			row[x] = as_premul_argb(srow[sx]);
		}
	}
	free(img);
	return true;
}

static void as_state_draw(struct as_state *state, struct as_buffer *buf)
{
	const struct aswl_gradient *bg_grad = &state->theme.menu_item_gradient;
	uint32_t bg_color = state->theme.menu_bg;
	uint32_t bevel_base = state->theme.menu_bg;

	/*
	 * The classic "Windows on Desktop N" WinList popup uses the menu hilite
	 * (active) background for its list area in look.DEFAULT. Use that style in
	 * `--windows` mode so the list texture/colors match the X11 reference.
	 */
	if (state->window_list_mode) {
		bg_grad = &state->theme.menu_item_sel_gradient;
		bg_color = state->theme.menu_item_sel_bg;
		bevel_base = bg_color;
	}

	bool menu_backpix_filled = false;
	if (!state->window_list_mode &&
	    (state->theme.menu_back_pixmap_type == 128 || state->theme.menu_back_pixmap_type == 127) &&
	    state->theme.menu_back_pixmap_path != NULL) {
		menu_backpix_filled = as_buffer_fill_backpixmap_image(
		        buf, state->theme.menu_back_pixmap_path,
		        state->theme.menu_back_pixmap_type == 127);
	}

	if (!menu_backpix_filled) {
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
			as_buffer_paint_solid(buf, bg_color);
		}
	}

	struct as_menu_layout layout;
	if (!as_state_get_layout(state, &layout))
		return;

	as_state_update_close_button_metrics(state, &layout);
	as_state_update_pin_button_metrics(state, &layout);
	as_state_update_iconize_button_metrics(state, &layout);
	as_state_ensure_close_button_icons(state);
	as_state_ensure_iconize_button_icons(state);
	as_state_ensure_pin_button_icons(state);

	const struct aswl_gradient *header_grad = &state->theme.menu_header_gradient;
	uint32_t header_bg = state->theme.menu_header_bg;
	uint32_t header_fg = state->theme.menu_header_fg;
	uint32_t border = state->theme.menu_border;

	/*
	 * The X11 "Windows on Desktop N" pop-up (WinList-ish window list) uses the
	 * focused menu title styling (MenuHiTitleStyle). Match that look in
	 * `--windows` mode so the screenshot gallery aligns with classic AfterStep.
	 */
	if (state->window_list_mode) {
		header_grad = &state->theme.menu_hititle_gradient;
		header_bg = state->theme.menu_hititle_bg;
		header_fg = state->theme.menu_hititle_fg;
		border = state->theme.menu_border;
	}

	uint32_t *pixels = (uint32_t *)buf->data;
	int stride_px = buf->stride / 4;

	/* Header/filter bar */
	as_buffer_fill_style_rect(buf,
	                          0,
	                          0,
	                          buf->width,
	                          layout.header_h,
	                          header_grad,
	                          header_bg,
	                          0);
	as_buffer_fill_rect(buf, 0, layout.header_h - 1, buf->width, 1, border);

	/* Outer border (matches X11 AfterStep menus/window lists). */
	if (border != 0 && buf->width > 1 && buf->height > 1) {
		as_buffer_fill_rect(buf, 0, 0, buf->width, 1, border);
		as_buffer_fill_rect(buf, 0, buf->height - 1, buf->width, 1, border);
		as_buffer_fill_rect(buf, 0, 0, 1, buf->height, border);
		as_buffer_fill_rect(buf, buf->width - 1, 0, 1, buf->height, border);
	}

	bool bevel_buttons = !state->window_list_mode;
	int icon_box = clamp_int(layout.header_h - 8, 10, 16);

	/* Iconize button (top-right, WinList mode). */
	if (state->iconize_w > 0 && state->iconize_h > 0) {
		if (bevel_buttons) {
			as_buffer_draw_bevel_rect(buf,
			                          state->iconize_x,
			                          state->iconize_y,
			                          state->iconize_w,
			                          state->iconize_h,
			                          header_bg,
			                          state->pressed_iconize);
		}

		const uint32_t *icon = state->iconize_icon_argb;
		int iw = state->iconize_icon_w;
		int ih = state->iconize_icon_h;
		if (state->pressed_iconize && state->iconize_icon_pressed_argb != NULL) {
			icon = state->iconize_icon_pressed_argb;
			iw = state->iconize_icon_pressed_w;
			ih = state->iconize_icon_pressed_h;
		}

		if (icon != NULL && iw > 0 && ih > 0) {
			int dw = iw;
			int dh = ih;
			if (dw > icon_box || dh > icon_box) {
				double sx = (double)icon_box / (double)dw;
				double sy = (double)icon_box / (double)dh;
				double s = sx < sy ? sx : sy;
				dw = (int)((double)dw * s + 0.5);
				dh = (int)((double)dh * s + 0.5);
				if (dw < 1)
					dw = 1;
				if (dh < 1)
					dh = 1;
			}

			int px = state->iconize_x + (state->iconize_w - dw) / 2;
			int py = state->iconize_y + (state->iconize_h - dh) / 2;
			as_buffer_draw_image_bilinear(buf, px, py, dw, dh, icon, iw, ih);
		} else {
			(void)aswl_font_set_scale(&state->font, 1);
			int fh = aswl_font_height(&state->font);
			int fx = state->iconize_x + 2;
			int fy = state->iconize_y + (state->iconize_h - fh) / 2;
			aswl_font_draw_text(&state->font,
			                    pixels,
			                    buf->width,
			                    buf->height,
			                    stride_px,
			                    fx,
			                    fy,
			                    "_",
			                    state->iconize_w - 4,
			                    header_fg);
		}
	}

	/* Close button (top-right). */
	if (state->close_w > 0 && state->close_h > 0) {
		if (bevel_buttons) {
			as_buffer_draw_bevel_rect(buf,
			                          state->close_x,
			                          state->close_y,
			                          state->close_w,
			                          state->close_h,
			                          header_bg,
			                          state->pressed_close);
		}

		const uint32_t *icon = state->close_icon_argb;
		int iw = state->close_icon_w;
		int ih = state->close_icon_h;
		if (state->pressed_close && state->close_icon_pressed_argb != NULL) {
			icon = state->close_icon_pressed_argb;
			iw = state->close_icon_pressed_w;
			ih = state->close_icon_pressed_h;
		}

		if (icon != NULL && iw > 0 && ih > 0) {
			int dw = iw;
			int dh = ih;
			if (dw > icon_box || dh > icon_box) {
				double sx = (double)icon_box / (double)dw;
				double sy = (double)icon_box / (double)dh;
				double s = sx < sy ? sx : sy;
				dw = (int)((double)dw * s + 0.5);
				dh = (int)((double)dh * s + 0.5);
				if (dw < 1)
					dw = 1;
				if (dh < 1)
					dh = 1;
			}

			int px = state->close_x + (state->close_w - dw) / 2;
			int py = state->close_y + (state->close_h - dh) / 2;
			as_buffer_draw_image_bilinear(buf, px, py, dw, dh, icon, iw, ih);
		} else {
			(void)aswl_font_set_scale(&state->font, 1);
			int fh = aswl_font_height(&state->font);
			int fx = state->close_x + 2;
			int fy = state->close_y + (state->close_h - fh) / 2;
			aswl_font_draw_text(&state->font,
			                    pixels,
			                    buf->width,
			                    buf->height,
			                    stride_px,
			                    fx,
			                    fy,
			                    "X",
			                    state->close_w - 4,
			                    header_fg);
		}
	}

	/* Pin button (top-left, WinList mode). */
	if (state->pin_w > 0 && state->pin_h > 0) {
		bool pressed = state->pressed_pin || state->pinned_open;
		if (bevel_buttons) {
			as_buffer_draw_bevel_rect(buf,
			                          state->pin_x,
			                          state->pin_y,
			                          state->pin_w,
			                          state->pin_h,
			                          header_bg,
			                          pressed);
		}

		const uint32_t *icon = state->pin_icon_argb;
		int iw = state->pin_icon_w;
		int ih = state->pin_icon_h;
		if (pressed && state->pin_icon_pressed_argb != NULL) {
			icon = state->pin_icon_pressed_argb;
			iw = state->pin_icon_pressed_w;
			ih = state->pin_icon_pressed_h;
		}

		if (icon != NULL && iw > 0 && ih > 0) {
			int dw = iw;
			int dh = ih;
			if (dw > icon_box || dh > icon_box) {
				double sx = (double)icon_box / (double)dw;
				double sy = (double)icon_box / (double)dh;
				double s = sx < sy ? sx : sy;
				dw = (int)((double)dw * s + 0.5);
				dh = (int)((double)dh * s + 0.5);
				if (dw < 1)
					dw = 1;
				if (dh < 1)
					dh = 1;
			}

			int px = state->pin_x + (state->pin_w - dw) / 2;
			int py = state->pin_y + (state->pin_h - dh) / 2;
			as_buffer_draw_image_bilinear(buf, px, py, dw, dh, icon, iw, ih);
		} else {
			(void)aswl_font_set_scale(&state->font, 1);
			int fh = aswl_font_height(&state->font);
			int fx = state->pin_x + 2;
			int fy = state->pin_y + (state->pin_h - fh) / 2;
			aswl_font_draw_text(&state->font,
			                    pixels,
			                    buf->width,
			                    buf->height,
			                    stride_px,
			                    fx,
			                    fy,
			                    "P",
			                    state->pin_w - 4,
			                    header_fg);
		}
	}

	char header[512];
	const char *filter = state->filter != NULL ? state->filter : "";
	if (filter[0] != '\0') {
		(void)snprintf(header,
		               sizeof(header),
		               "%s%s",
		               state->keyboard != NULL ? "> " : "",
		               filter);
	} else if (state->title != NULL && state->title[0] != '\0') {
		(void)snprintf(header, sizeof(header), "%s", state->title);
	} else {
		(void)snprintf(header, sizeof(header), "AfterStep");
	}

	/* Reset font scales after any temporary draws (e.g. close-button fallback glyph). */
	(void)aswl_font_set_scale(&state->font, layout.text_scale);
	(void)aswl_font_set_scale(&state->header_font, layout.text_scale);
	(void)aswl_font_set_scale(&state->hilite_font, layout.text_scale);

	struct aswl_font *header_font = filter[0] != '\0' ? &state->font : &state->header_font;
	int header_style = state->theme.menu_title_text_style;
	if (filter[0] != '\0')
		header_style = state->theme.menu_item_text_style;
	else if (state->window_list_mode)
		header_style = state->theme.menu_hititle_text_style;
	int text_h = aswl_font_height_styled(header_font, header_style);
	int ty = (layout.header_h - text_h) / 2;

	int text_left = layout.pad;
	if (state->pin_w > 0)
		text_left = state->pin_x + state->pin_w + layout.pad;
	int right_btn_x = buf->width;
	if (state->close_w > 0)
		right_btn_x = state->close_x;
	if (state->iconize_w > 0 && state->iconize_x < right_btn_x)
		right_btn_x = state->iconize_x;
	int text_right = right_btn_x - layout.pad;
	int header_text_w = text_right - text_left;
	if (header_text_w < 1)
		header_text_w = 1;

	int tx = text_left;
	if (filter[0] == '\0' && !state->window_list_mode) {
		int hw = aswl_font_text_width_styled(header_font, header, header_style);
		if (hw > 0 && hw < header_text_w)
			tx = text_left + (header_text_w - hw) / 2;
	}

	aswl_font_draw_text_styled(header_font,
	                           pixels,
	                           buf->width,
	                           buf->height,
	                           stride_px,
	                           tx,
	                           ty,
	                           header,
	                           header_text_w,
	                           header_fg,
	                           header_style);

	/* List */
	size_t rows = as_state_visible_rows(state, &layout);
	int list_x = layout.pad;
	int list_w = buf->width - 2 * layout.pad;
	int row_y0 = layout.header_h + layout.pad;

	for (size_t row = 0; row < rows; row++) {
		int idx = state->scroll + (int)row;
		if (idx < 0 || (size_t)idx >= state->filtered_count)
			break;

		size_t entry_idx = state->filtered[idx];
		if (entry_idx >= state->entry_count)
			continue;
		struct as_menu_entry *e = &state->entries[entry_idx];

		int y = row_y0 + (int)row * layout.row_h;

		bool submenu = menu_command_is_submenu(e->command);

		/*
		 * AfterStep's look.DEFAULT uses `TextureMenuItemsIndividually 0`, which
		 * means the menu background should be a single continuous texture/
		 * gradient rather than restarting the gradient for each row. We paint
		 * the full menu background once at the top of `as_state_draw()`; only
		 * paint per-row backgrounds for highlighted rows.
		 */
		/*
		 * AfterStep menus typically highlight based on pointer hover/press.
		 * Only show a keyboard-selection hilite when the user is actively
		 * navigating via keyboard (arrows/page) or typing a filter.
		 */
		int hilite_idx = -1;
		if (state->pressed_index >= 0)
			hilite_idx = state->pressed_index;
		else if (state->hover_index >= 0)
			hilite_idx = state->hover_index;
		else if ((state->keyboard_nav_active || state->filter_len > 0) && state->selected_index >= 0)
			hilite_idx = state->selected_index;

		bool highlight = (idx == hilite_idx);

			uint32_t bg = state->theme.menu_item_bg;
			uint32_t fg = state->theme.menu_item_fg;
			struct aswl_font *row_font = &state->font;
			int row_style = state->theme.menu_item_text_style;

			if (highlight) {
				uint8_t nudge = (idx == state->pressed_index) ? 48 : 0;
				bg = state->theme.menu_item_sel_bg;
				fg = state->theme.menu_item_sel_fg;
				row_font = state->window_list_mode ? &state->font : &state->hilite_font;
				if (!state->window_list_mode)
					row_style = state->theme.menu_hilite_text_style;
				as_buffer_fill_style_rect(buf,
				                          list_x,
				                          y,
				                          list_w,
			                          layout.row_h,
			                          &state->theme.menu_item_sel_gradient,
			                          bg,
			                          nudge);
		}

			if (layout.icon_size > 0) {
				as_menu_entry_try_load_icon(e);
				if (e->icon_argb != NULL && e->icon_w > 0 && e->icon_h > 0) {
					int ix = list_x + 8;
					int iy = y + (layout.row_h - layout.icon_size) / 2;
					int box = layout.icon_size;
					int dw = e->icon_w;
					int dh = e->icon_h;
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

						int px = ix + (box - dw) / 2;
						int py = iy + (box - dh) / 2;
						as_buffer_draw_image_bilinear(buf, px, py, dw, dh, e->icon_argb, e->icon_w, e->icon_h);
					}
				}
				}

				char line[512];
				(void)snprintf(line, sizeof(line), "%s", e->label);

			int text_x = list_x + 8 + layout.icon_col_w;
			int arrow_w = submenu ? aswl_font_text_width_styled(row_font, ">", row_style) : 0;
			int text_w = list_w - (text_x - list_x) - 8;
			if (submenu)
				text_w -= arrow_w + 8;
			if (text_w < 0)
				text_w = 0;
			int ly = y + (layout.row_h - aswl_font_height_styled(row_font, row_style)) / 2;
			aswl_font_draw_text_styled(row_font,
			                           pixels,
			                           buf->width,
			                           buf->height,
			                           stride_px,
			                           text_x,
			                           ly,
			                           line,
			                           text_w,
			                           fg,
			                           row_style);

			if (submenu && arrow_w > 0) {
				int ax = list_x + list_w - 8 - arrow_w;
				int aw = arrow_w;
			if (ax < text_x) {
				ax = text_x;
				aw = list_x + list_w - 8 - ax;
				}
				if (aw > 0) {
					aswl_font_draw_text_styled(row_font,
					                           pixels,
					                           buf->width,
					                           buf->height,
					                           stride_px,
					                           ax,
					                           ly,
					                           ">",
					                           aw,
					                           fg,
					                           row_style);
				}
			}
		}

	/* Footer/help (small). */
	if (state->show_help && state->keyboard != NULL) {
		char footer[256];
		(void)snprintf(footer,
		               sizeof(footer),
		               "Enter: run   Esc: clear/close   Up/Down: select   Backspace: delete   (%zu items)",
		               state->filtered_count);
		(void)aswl_font_set_scale(&state->font, layout.help_scale);
		int fh = aswl_font_height(&state->font);
		int fy = buf->height - fh - 6;
		if (fy > layout.header_h) {
			as_buffer_fill_rect(buf, 0, fy - 6, buf->width, fh + 12, state->theme.menu_footer_bg);
			aswl_font_draw_text(&state->font,
			                    pixels,
			                    buf->width,
			                    buf->height,
			                    stride_px,
			                    layout.pad,
			                    fy,
			                    footer,
			                    buf->width - 2 * layout.pad,
				                    state->theme.menu_footer_fg);
		}
	}

	/* AfterStep bevel provides the visual "border" (no extra solid outline). */
	if (buf->width >= 2 && buf->height >= 2)
		as_buffer_draw_bevel_rect(buf, 0, 0, buf->width, buf->height, bevel_base, false);
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
	if (state == NULL)
		return;
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
