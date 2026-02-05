#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_XKBCOMMON
#include <xkbcommon/xkbcommon.h>
#endif

#include <wayland-client.h>

#include "afterstep-control-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "aswlicon.h"
#include "aswltheme.h"
#include "aswlfont.h"

/* Avoid pulling in linux headers just for BTN_LEFT/KEY_* values. */
#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif

#ifndef KEY_ESC
#define KEY_ESC 1
#endif
#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE 14
#endif
#ifndef KEY_ENTER
#define KEY_ENTER 28
#endif
#ifndef KEY_UP
#define KEY_UP 103
#endif
#ifndef KEY_PAGEUP
#define KEY_PAGEUP 104
#endif
#ifndef KEY_DOWN
#define KEY_DOWN 108
#endif
#ifndef KEY_PAGEDOWN
#define KEY_PAGEDOWN 109
#endif

enum {
	ASWL_WINDOW_FLAG_MAPPED = 1u << 0,
	ASWL_WINDOW_FLAG_FOCUSED = 1u << 1,
	ASWL_WINDOW_FLAG_XWAYLAND = 1u << 2,
};

struct as_menu_entry {
	char *label;
	char *icon_spec;
	char *command;
	bool pinned;
	bool icon_tried;
	uint32_t *icon_argb;
	int icon_w;
	int icon_h;
};

struct as_menu_stack_entry {
	char *section; /* NULL for root menu (outside @menu blocks) */
	char *title;   /* default title for this menu level (may be overridden by @title) */
	char *filter;
	int selected_index;
	int scroll;
};

struct as_buffer {
	struct wl_buffer *wl_buffer;
	void *data;
	int width;
	int height;
	int stride;
	size_t size;
	bool busy;
	struct as_state *state;
};

struct as_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;

	struct xdg_wm_base *xdg_wm_base;
	struct afterstep_control_v1 *control;
	uint32_t control_version;

#ifdef HAVE_XKBCOMMON
	struct xkb_context *xkb_context;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;
#endif

	struct wl_surface *surface;
	struct wl_callback *frame_cb;

	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

	struct as_buffer *buffers[2];

	int width;
	int height;
	bool configured;
	bool running;

	bool needs_redraw;
	int pointer_x;
	int pointer_y;
	bool pointer_in_surface;

	int hover_index;   /* index in filtered list */
	int pressed_index; /* index in filtered list */
	int selected_index; /* index in filtered list */
	int scroll;        /* first visible filtered index */

	char *filter;
	size_t filter_len;
	size_t filter_cap;

	struct as_menu_entry *entries;
	size_t entry_count;
	size_t entry_cap;
	size_t pinned_count;

	size_t *filtered;
	size_t filtered_count;
	size_t filtered_cap;

	bool include_desktop_entries;
	char *menu_config_path;
	char *menu_section;
	struct as_menu_stack_entry *menu_stack;
	size_t menu_stack_len;
	size_t menu_stack_cap;
	char *title;
	bool title_fixed;
	bool show_help;
	bool window_list_mode;
	bool window_list_in_progress;
	uint32_t current_workspace;

	struct aswl_theme theme;
	struct aswl_font font;
};

static void schedule_redraw(struct as_state *state);
static void draw_and_commit(struct as_state *state);
static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms);
static void rstrip(char *s);
static char *lstrip(char *s);
static bool as_state_go_back(struct as_state *state);
static void as_state_activate_entry(struct as_state *state, size_t entry_idx);

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

static int env_int(const char *name, int def, int lo, int hi)
{
	const char *s = name != NULL ? getenv(name) : NULL;
	if (s != NULL && s[0] != '\0') {
		char *end = NULL;
		long v = strtol(s, &end, 10);
		if (end != s && *end == '\0')
			def = (int)v;
	}
	return clamp_int(def, lo, hi);
}

static int create_tmpfile(size_t size)
{
	int fd = -1;

#ifdef __linux__
	fd = memfd_create("aswlmenu", MFD_CLOEXEC);
	if (fd >= 0) {
		if (ftruncate(fd, (off_t)size) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}
#endif

	char template[] = "/tmp/aswlmenu-XXXXXX";
	fd = mkstemp(template);
	if (fd < 0)
		return -1;

	unlink(template);
	if (ftruncate(fd, (off_t)size) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	(void)wl_buffer;
	struct as_buffer *buf = data;
	buf->busy = false;

	if (buf->state != NULL && buf->state->needs_redraw && buf->state->frame_cb == NULL)
		draw_and_commit(buf->state);
}

static void as_buffer_destroy(struct as_buffer *buf)
{
	if (buf == NULL)
		return;
	if (buf->wl_buffer != NULL)
		wl_buffer_destroy(buf->wl_buffer);
	if (buf->data != NULL && buf->size > 0)
		munmap(buf->data, buf->size);
	free(buf);
}

static struct as_buffer *as_buffer_create(struct as_state *state, int width, int height)
{
	if (state->shm == NULL)
		return NULL;

	struct as_buffer *buf = calloc(1, sizeof(*buf));
	if (buf == NULL)
		return NULL;

	buf->width = width;
	buf->height = height;
	buf->stride = width * 4;
	buf->size = (size_t)buf->stride * (size_t)height;

	int fd = create_tmpfile(buf->size);
	if (fd < 0) {
		as_buffer_destroy(buf);
		return NULL;
	}

	buf->data = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf->data == MAP_FAILED) {
		close(fd);
		as_buffer_destroy(buf);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, (int)buf->size);
	buf->wl_buffer = wl_shm_pool_create_buffer(pool,
	                                           0,
	                                           width,
	                                           height,
	                                           buf->stride,
	                                           WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	if (buf->wl_buffer == NULL) {
		as_buffer_destroy(buf);
		return NULL;
	}

	buf->state = state;
	static const struct wl_buffer_listener wl_buf_listener = {
		.release = buffer_release,
	};
	wl_buffer_add_listener(buf->wl_buffer, &wl_buf_listener, buf);
	return buf;
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

	(void)base_argb;

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

	/* Top edge */
	{
		uint32_t *row = pixels + (size_t)top * (size_t)stride_px;
		for (int xx = left; xx <= right; xx++) {
			uint32_t base = as_unpremul_argb(row[xx]);
			uint32_t c = sunken ? aswl_color_darken(base, 120) : aswl_color_lighten(base, 64);
			row[xx] = as_premul_argb(c);
		}
	}

	/* Bottom edge */
	{
		uint32_t *row = pixels + (size_t)bottom * (size_t)stride_px;
		for (int xx = left; xx <= right; xx++) {
			uint32_t base = as_unpremul_argb(row[xx]);
			uint32_t c = sunken ? aswl_color_lighten(base, 64) : aswl_color_darken(base, 120);
			row[xx] = as_premul_argb(c);
		}
	}

	/* Left/right edges (excluding corners to avoid double-darkening). */
	for (int yy = top + 1; yy <= bottom - 1; yy++) {
		uint32_t *row = pixels + (size_t)yy * (size_t)stride_px;
		uint32_t base_l = as_unpremul_argb(row[left]);
		uint32_t base_r = as_unpremul_argb(row[right]);
		uint32_t c_l = sunken ? aswl_color_darken(base_l, 120) : aswl_color_lighten(base_l, 64);
		uint32_t c_r = sunken ? aswl_color_lighten(base_r, 64) : aswl_color_darken(base_r, 120);
		row[left] = as_premul_argb(c_l);
		row[right] = as_premul_argb(c_r);
	}
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
	{
		double denom = fw + fh;
		if (denom <= 0.0)
			return 0.0;
		return (fx + fy) / denom;
	}
	case 7:
	{
		double denom = fw + fh;
		if (denom <= 0.0)
			return 0.0;
		return (fx + (fh - fy)) / denom;
	}
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

#if 0 /* legacy 5x7 font (replaced by aswlfont) */
static const uint8_t *as_font5x7_rows(char c)
{
	if (c >= 'a' && c <= 'z')
		c = (char)('A' + (c - 'a'));

	switch (c) {
	case ' ':
	{
		static const uint8_t rows[7] = { 0, 0, 0, 0, 0, 0, 0 };
		return rows;
	}
	case '-':
	{
		static const uint8_t rows[7] = { 0, 0, 0, 0x1F, 0, 0, 0 };
		return rows;
	}
	case '_':
	{
		static const uint8_t rows[7] = { 0, 0, 0, 0, 0, 0, 0x1F };
		return rows;
	}
	case '.':
	{
		static const uint8_t rows[7] = { 0, 0, 0, 0, 0, 0, 0x04 };
		return rows;
	}
	case ':':
	{
		static const uint8_t rows[7] = { 0, 0x04, 0, 0, 0x04, 0, 0 };
		return rows;
	}
	case '+':
	{
		static const uint8_t rows[7] = { 0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0 };
		return rows;
	}
	case '/':
	{
		static const uint8_t rows[7] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0, 0 };
		return rows;
	}
	case '?':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0, 0x04 };
		return rows;
	}
	case '0':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
		return rows;
	}
	case '1':
	{
		static const uint8_t rows[7] = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
		return rows;
	}
	case '2':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
		return rows;
	}
	case '3':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E };
		return rows;
	}
	case '4':
	{
		static const uint8_t rows[7] = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
		return rows;
	}
	case '5':
	{
		static const uint8_t rows[7] = { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
		return rows;
	}
	case '6':
	{
		static const uint8_t rows[7] = { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
		return rows;
	}
	case '7':
	{
		static const uint8_t rows[7] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
		return rows;
	}
	case '8':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
		return rows;
	}
	case '9':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
		return rows;
	}
	case 'A':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
		return rows;
	}
	case 'B':
	{
		static const uint8_t rows[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
		return rows;
	}
	case 'C':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
		return rows;
	}
	case 'D':
	{
		static const uint8_t rows[7] = { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C };
		return rows;
	}
	case 'E':
	{
		static const uint8_t rows[7] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
		return rows;
	}
	case 'F':
	{
		static const uint8_t rows[7] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
		return rows;
	}
	case 'G':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
		return rows;
	}
	case 'H':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
		return rows;
	}
	case 'I':
	{
		static const uint8_t rows[7] = { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
		return rows;
	}
	case 'J':
	{
		static const uint8_t rows[7] = { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C };
		return rows;
	}
	case 'K':
	{
		static const uint8_t rows[7] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
		return rows;
	}
	case 'L':
	{
		static const uint8_t rows[7] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
		return rows;
	}
	case 'M':
	{
		static const uint8_t rows[7] = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
		return rows;
	}
	case 'N':
	{
		static const uint8_t rows[7] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
		return rows;
	}
	case 'O':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
		return rows;
	}
	case 'P':
	{
		static const uint8_t rows[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
		return rows;
	}
	case 'Q':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
		return rows;
	}
	case 'R':
	{
		static const uint8_t rows[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
		return rows;
	}
	case 'S':
	{
		static const uint8_t rows[7] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
		return rows;
	}
	case 'T':
	{
		static const uint8_t rows[7] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
		return rows;
	}
	case 'U':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
		return rows;
	}
	case 'V':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
		return rows;
	}
	case 'W':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A };
		return rows;
	}
	case 'X':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
		return rows;
	}
	case 'Y':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
		return rows;
	}
	case 'Z':
	{
		static const uint8_t rows[7] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
		return rows;
	}
	case '!':
	{
		static const uint8_t rows[7] = { 0x04, 0x04, 0x04, 0x04, 0x04, 0, 0x04 };
		return rows;
	}
	case '(':
	{
		static const uint8_t rows[7] = { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
		return rows;
	}
	case ')':
	{
		static const uint8_t rows[7] = { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
		return rows;
	}
	case ',':
	{
		static const uint8_t rows[7] = { 0, 0, 0, 0, 0, 0x04, 0x08 };
		return rows;
	}
	case '=':
	{
		static const uint8_t rows[7] = { 0, 0, 0x1F, 0, 0x1F, 0, 0 };
		return rows;
	}
	case '\'':
	{
		static const uint8_t rows[7] = { 0x04, 0x04, 0, 0, 0, 0, 0 };
		return rows;
	}
	case '"':
	{
		static const uint8_t rows[7] = { 0x0A, 0x0A, 0, 0, 0, 0, 0 };
		return rows;
	}
	case '#':
	{
		static const uint8_t rows[7] = { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A };
		return rows;
	}
	case '$':
	{
		static const uint8_t rows[7] = { 0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04 };
		return rows;
	}
	case '%':
	{
		static const uint8_t rows[7] = { 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13 };
		return rows;
	}
	case '&':
	{
		static const uint8_t rows[7] = { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D };
		return rows;
	}
	case '*':
	{
		static const uint8_t rows[7] = { 0, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0 };
		return rows;
	}
	case '<':
	{
		static const uint8_t rows[7] = { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 };
		return rows;
	}
	case '>':
	{
		static const uint8_t rows[7] = { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 };
		return rows;
	}
	case '[':
	{
		static const uint8_t rows[7] = { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E };
		return rows;
	}
	case ']':
	{
		static const uint8_t rows[7] = { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E };
		return rows;
	}
	default:
		return as_font5x7_rows('?');
	}
}

static int as_font5x7_glyph_w(int scale)
{
	return 5 * scale;
}

static int as_font5x7_glyph_h(int scale)
{
	return 7 * scale;
}

static int as_font5x7_spacing(int scale)
{
	return 1 * scale;
}

static int as_font5x7_text_width_n(const char *s, size_t n, int scale)
{
	if (s == NULL || n == 0)
		return 0;

	int glyph_w = as_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);
	int w = 0;

	for (size_t i = 0; i < n && s[i] != '\0'; i++) {
		if (i > 0)
			w += spacing;
		w += glyph_w;
	}
	return w;
}

static void as_buffer_draw_glyph5x7(struct as_buffer *buf, int x, int y, char c, int scale, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	const uint8_t *rows = as_font5x7_rows(c);
	int gw = as_font5x7_glyph_w(scale);
	int gh = as_font5x7_glyph_h(scale);

	for (int yy = 0; yy < gh; yy++) {
		uint8_t mask = rows[yy / scale];
		for (int xx = 0; xx < gw; xx++) {
			int bit = xx / scale;
			if ((mask & (1u << (4 - bit))) == 0)
				continue;
			int px = x + xx;
			int py = y + yy;
			if (px < 0 || py < 0 || px >= buf->width || py >= buf->height)
				continue;
			uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)py * (size_t)buf->stride);
			row[px] = argb;
		}
	}
}

static size_t as_font5x7_fit_chars(const char *s, int scale, int max_w)
{
	if (s == NULL || max_w <= 0)
		return 0;

	int glyph_w = as_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);
	int w = 0;
	size_t i = 0;

	for (; s[i] != '\0'; i++) {
		int add = glyph_w;
		if (i > 0)
			add += spacing;
		if (w + add > max_w)
			break;
		w += add;
	}
	return i;
}

static void as_buffer_draw_text5x7(struct as_buffer *buf, int x, int y, const char *s, int max_w, int scale, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL || s == NULL)
		return;

	size_t n = strlen(s);
	if (n == 0)
		return;

	int full_w = as_font5x7_text_width_n(s, n, scale);
	size_t draw_n = n;

	if (max_w > 0 && full_w > max_w) {
		int ell_w = as_font5x7_text_width_n("...", 3, scale);
		if (max_w > ell_w)
			draw_n = as_font5x7_fit_chars(s, scale, max_w - ell_w);
		else
			draw_n = as_font5x7_fit_chars(s, scale, max_w);
	}

	int glyph_w = as_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);
	int cx = x;

	for (size_t i = 0; i < draw_n; i++) {
		if (i > 0)
			cx += spacing;
		as_buffer_draw_glyph5x7(buf, cx, y, s[i], scale, argb);
		cx += glyph_w;
	}

	if (draw_n < n && max_w > 0) {
		if (cx > x)
			cx += spacing;
		as_buffer_draw_text5x7(buf, cx, y, "...", max_w > 0 ? max_w - (cx - x) : 0, scale, argb);
	}
}

#endif

static void spawn_command(const char *command)
{
	if (command == NULL || command[0] == '\0')
		return;

	pid_t pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}
}

static bool menu_command_is_submenu(const char *command)
{
	if (command == NULL)
		return false;
	if (command[0] != '@')
		return false;

	const char *action = command + 1;
	while (*action != '\0' && isspace((unsigned char)*action))
		action++;

	if (strncmp(action, "submenu", 6) != 0)
		return false;

	char c = action[6];
	return c == '\0' || isspace((unsigned char)c) || c == ':' || c == '=';
}

static void as_state_filter_clear_silent(struct as_state *state)
{
	if (state == NULL)
		return;
	if (state->filter != NULL)
		state->filter[0] = '\0';
	state->filter_len = 0;
}

static void as_state_menu_stack_clear(struct as_state *state)
{
	if (state == NULL)
		return;

	for (size_t i = 0; i < state->menu_stack_len; i++) {
		free(state->menu_stack[i].section);
		free(state->menu_stack[i].title);
		free(state->menu_stack[i].filter);
	}
	free(state->menu_stack);
	state->menu_stack = NULL;
	state->menu_stack_len = 0;
	state->menu_stack_cap = 0;
}

static bool as_state_menu_stack_push(struct as_state *state)
{
	if (state == NULL)
		return false;

	if (state->menu_stack_len == state->menu_stack_cap) {
		size_t next = state->menu_stack_cap == 0 ? 8 : state->menu_stack_cap * 2;
		struct as_menu_stack_entry *tmp = realloc(state->menu_stack, next * sizeof(*tmp));
		if (tmp == NULL)
			return false;
		state->menu_stack = tmp;
		state->menu_stack_cap = next;
	}

	struct as_menu_stack_entry ent = { 0 };
	ent.section = state->menu_section;
	state->menu_section = NULL;
	ent.title = state->title;
	state->title = NULL;
	ent.selected_index = state->selected_index;
	ent.scroll = state->scroll;
	if (state->filter != NULL && state->filter_len > 0)
		ent.filter = strdup(state->filter);

	state->menu_stack[state->menu_stack_len++] = ent;
	return true;
}

static bool menu_config_section_exists(const char *path, const char *section)
{
	if (path == NULL || path[0] == '\0' || section == NULL || section[0] == '\0')
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	char *line = NULL;
	size_t cap = 0;
	ssize_t len;
	bool found = false;

	while ((len = getline(&line, &cap, fp)) != -1) {
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		char *s = lstrip(line);
		if (*s == '\0' || *s == '#')
			continue;
		if (*s != '@')
			continue;
		s++;

		char *arg = strchr(s, ' ');
		if (arg != NULL) {
			*arg = '\0';
			arg = lstrip(arg + 1);
			rstrip(arg);
		}

		if (strcmp(s, "menu") != 0)
			continue;
		if (arg == NULL || arg[0] == '\0')
			continue;
		if (strcmp(arg, section) == 0) {
			found = true;
			break;
		}
	}

	free(line);
	fclose(fp);
	return found;
}

static char *menu_command_submenu_target(const char *command, const char *fallback_label)
{
	if (!menu_command_is_submenu(command))
		return NULL;

	const char *action = command + 1;
	while (*action != '\0' && isspace((unsigned char)*action))
		action++;
	if (strncmp(action, "submenu", 6) != 0)
		return NULL;

	const char *arg = action + 6;
	while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
		arg++;

	const char *picked = arg;
	if (picked[0] == '\0')
		picked = fallback_label != NULL ? fallback_label : "";

	while (*picked != '\0' && isspace((unsigned char)*picked))
		picked++;

	char *out = strdup(picked);
	if (out == NULL)
		return NULL;
	rstrip(out);
	if (out[0] == '\0') {
		free(out);
		return NULL;
	}
	return out;
}

static void as_state_launch_command(struct as_state *state, const char *command)
{
	if (command == NULL || command[0] == '\0')
		return;

	if (menu_command_is_submenu(command))
		return;

	if (state != NULL && state->control != NULL) {
		if (command[0] == '@') {
			const char *action = command + 1;
			if (strcmp(action, "quit") == 0 || strcmp(action, "exit") == 0) {
				fprintf(stderr, "aswlmenu: compositor quit\n");
				afterstep_control_v1_quit(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "close") == 0 || strcmp(action, "close_focused") == 0) {
				fprintf(stderr, "aswlmenu: compositor close_focused\n");
				afterstep_control_v1_close_focused(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "focus_next") == 0 || strcmp(action, "next") == 0) {
				fprintf(stderr, "aswlmenu: compositor focus_next\n");
				afterstep_control_v1_focus_next(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "focus_prev") == 0 || strcmp(action, "prev") == 0) {
				fprintf(stderr, "aswlmenu: compositor focus_prev\n");
				afterstep_control_v1_focus_prev(state->control);
				(void)wl_display_flush(state->display);
				return;
			}

			const char *id_arg = NULL;
			if (strncmp(action, "focus_window", 11) == 0) {
				id_arg = action + 11;
			} else if (strncmp(action, "focus", 5) == 0) {
				/* Avoid matching focus_next/focus_prev. */
				if (action[5] != '_')
					id_arg = action + 5;
			}
			if (id_arg != NULL) {
				while (*id_arg == ':' || *id_arg == '=' || isspace((unsigned char)*id_arg))
					id_arg++;

				if (state->control_version < 4) {
					fprintf(stderr, "aswlmenu: focus_window requires control protocol v4\n");
					return;
				}

				char *end = NULL;
				unsigned long id = strtoul(id_arg, &end, 10);
				while (end != NULL && isspace((unsigned char)*end))
					end++;

				if (end != id_arg && end != NULL && *end == '\0' && id >= 1 && id <= UINT32_MAX) {
					fprintf(stderr, "aswlmenu: compositor focus_window=%lu\n", id);
					afterstep_control_v1_focus_window(state->control, (uint32_t)id);
					(void)wl_display_flush(state->display);
					return;
				}

				fprintf(stderr, "aswlmenu: invalid focus_window id: %s\n", id_arg);
				return;
			}
			if ((strcmp(action, "fullscreen") == 0 || strcmp(action, "toggle_fullscreen") == 0) &&
			    state->control_version >= 5) {
				fprintf(stderr, "aswlmenu: compositor toggle_fullscreen\n");
				afterstep_control_v1_toggle_fullscreen(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if ((strcmp(action, "maximize") == 0 || strcmp(action, "maximized") == 0 ||
			     strcmp(action, "toggle_maximized") == 0 || strcmp(action, "toggle_maximize") == 0) &&
			    state->control_version >= 5) {
				fprintf(stderr, "aswlmenu: compositor toggle_maximized\n");
				afterstep_control_v1_toggle_maximized(state->control);
				(void)wl_display_flush(state->display);
				return;
			}

			if ((strcmp(action, "workspace_next") == 0 || strcmp(action, "ws_next") == 0 || strcmp(action, "ws+") == 0) &&
			    state->control_version >= 2) {
				fprintf(stderr, "aswlmenu: compositor workspace_next\n");
				afterstep_control_v1_workspace_next(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if ((strcmp(action, "workspace_prev") == 0 || strcmp(action, "ws_prev") == 0 || strcmp(action, "ws-") == 0) &&
			    state->control_version >= 2) {
				fprintf(stderr, "aswlmenu: compositor workspace_prev\n");
				afterstep_control_v1_workspace_prev(state->control);
				(void)wl_display_flush(state->display);
				return;
			}

			const char *ws_arg = NULL;
			if (strncmp(action, "workspace", 9) == 0) {
				ws_arg = action + 9;
			} else if (strncmp(action, "ws", 2) == 0) {
				ws_arg = action + 2;
			}
			if (ws_arg != NULL) {
				while (*ws_arg == ':' || *ws_arg == '=' || isspace((unsigned char)*ws_arg))
					ws_arg++;

				if (*ws_arg != '\0' && state->control_version >= 2) {
					char *end = NULL;
					unsigned long ws = strtoul(ws_arg, &end, 10);
					while (end != NULL && isspace((unsigned char)*end))
						end++;

					if (end != ws_arg && end != NULL && *end == '\0' && ws >= 1 && ws <= 1000) {
						fprintf(stderr, "aswlmenu: compositor set_workspace=%lu\n", ws);
						afterstep_control_v1_set_workspace(state->control, (uint32_t)ws);
						(void)wl_display_flush(state->display);
						return;
					}
				}
			}
		}

		fprintf(stderr, "aswlmenu: compositor exec %s\n", command);
		afterstep_control_v1_exec(state->control, command);
		(void)wl_display_flush(state->display);
		return;
	}

	spawn_command(command);
}

static void rstrip(char *s)
{
	if (s == NULL)
		return;
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r'))
		s[--n] = '\0';
}

static char *lstrip(char *s)
{
	if (s == NULL)
		return NULL;
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

static bool str_case_contains(const char *haystack, const char *needle)
{
	if (haystack == NULL || needle == NULL)
		return false;
	if (needle[0] == '\0')
		return true;

	for (size_t i = 0; haystack[i] != '\0'; i++) {
		size_t j = 0;
		while (needle[j] != '\0' && haystack[i + j] != '\0') {
			char a = (char)tolower((unsigned char)haystack[i + j]);
			char b = (char)tolower((unsigned char)needle[j]);
			if (a != b)
				break;
			j++;
		}
		if (needle[j] == '\0')
			return true;
	}
	return false;
}

static void as_menu_entry_destroy_icon(struct as_menu_entry *e)
{
	if (e == NULL)
		return;
	free(e->icon_argb);
	e->icon_argb = NULL;
	e->icon_w = 0;
	e->icon_h = 0;
	e->icon_tried = false;
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

static void as_state_free_entries(struct as_state *state)
{
	if (state == NULL)
		return;
	for (size_t i = 0; i < state->entry_count; i++) {
		free(state->entries[i].label);
		free(state->entries[i].icon_spec);
		free(state->entries[i].command);
		as_menu_entry_destroy_icon(&state->entries[i]);
	}
	free(state->entries);
	state->entries = NULL;
	state->entry_count = 0;
	state->entry_cap = 0;
	state->pinned_count = 0;
}

static bool as_state_append_entry(struct as_state *state, const char *label, const char *command, const char *icon_spec, bool pinned)
{
	if (state == NULL || label == NULL || label[0] == '\0' || command == NULL || command[0] == '\0')
		return false;

	if (state->entry_count == state->entry_cap) {
		size_t next = state->entry_cap == 0 ? 64 : state->entry_cap * 2;
		struct as_menu_entry *tmp = realloc(state->entries, next * sizeof(*tmp));
		if (tmp == NULL)
			return false;
		state->entries = tmp;
		state->entry_cap = next;
	}

	state->entries[state->entry_count] = (struct as_menu_entry){ 0 };
	state->entries[state->entry_count].label = strdup(label);
	state->entries[state->entry_count].command = strdup(command);
	if (icon_spec != NULL && icon_spec[0] != '\0')
		state->entries[state->entry_count].icon_spec = strdup(icon_spec);
	state->entries[state->entry_count].pinned = pinned;
	if (state->entries[state->entry_count].label == NULL ||
	    state->entries[state->entry_count].command == NULL ||
	    (icon_spec != NULL && icon_spec[0] != '\0' && state->entries[state->entry_count].icon_spec == NULL)) {
		free(state->entries[state->entry_count].label);
		free(state->entries[state->entry_count].icon_spec);
		free(state->entries[state->entry_count].command);
		state->entries[state->entry_count] = (struct as_menu_entry){ 0 };
		return false;
	}

	state->entry_count++;
	if (pinned)
		state->pinned_count++;
	return true;
}

static void as_state_free_filtered(struct as_state *state)
{
	if (state == NULL)
		return;
	free(state->filtered);
	state->filtered = NULL;
	state->filtered_count = 0;
	state->filtered_cap = 0;
}

static bool as_state_ensure_filtered(struct as_state *state, size_t cap)
{
	if (state == NULL)
		return false;
	if (cap <= state->filtered_cap)
		return true;

	size_t next = state->filtered_cap == 0 ? 128 : state->filtered_cap;
	while (next < cap)
		next *= 2;

	size_t *tmp = realloc(state->filtered, next * sizeof(*tmp));
	if (tmp == NULL)
		return false;
	state->filtered = tmp;
	state->filtered_cap = next;
	return true;
}

static void as_state_rebuild_filtered(struct as_state *state)
{
	if (state == NULL)
		return;

	state->filtered_count = 0;
	if (!as_state_ensure_filtered(state, state->entry_count))
		return;

	const char *needle = state->filter != NULL ? state->filter : "";
	for (size_t i = 0; i < state->entry_count; i++) {
		const struct as_menu_entry *e = &state->entries[i];
		if (needle[0] == '\0' || str_case_contains(e->label, needle) || str_case_contains(e->command, needle)) {
			state->filtered[state->filtered_count++] = i;
		}
	}

	if (state->filtered_count == 0) {
		state->selected_index = -1;
		state->hover_index = -1;
		state->pressed_index = -1;
		state->scroll = 0;
	} else {
		if (state->selected_index < 0)
			state->selected_index = 0;
		if ((size_t)state->selected_index >= state->filtered_count)
			state->selected_index = (int)(state->filtered_count - 1);
		if (state->scroll < 0)
			state->scroll = 0;
		if ((size_t)state->scroll > state->filtered_count)
			state->scroll = 0;
	}

	schedule_redraw(state);
}

static bool as_state_filter_set(struct as_state *state, const char *text)
{
	if (state == NULL)
		return false;
	if (text == NULL)
		text = "";

	size_t n = strlen(text);
	if (n + 1 > state->filter_cap) {
		size_t next = state->filter_cap == 0 ? 64 : state->filter_cap;
		while (next < n + 1)
			next *= 2;
		char *tmp = realloc(state->filter, next);
		if (tmp == NULL)
			return false;
		state->filter = tmp;
		state->filter_cap = next;
	}

	memcpy(state->filter, text, n + 1);
	state->filter_len = n;
	as_state_rebuild_filtered(state);
	return true;
}

static bool as_state_filter_append_utf8(struct as_state *state, const char *utf8)
{
	if (state == NULL || utf8 == NULL || utf8[0] == '\0')
		return false;

	size_t add = strlen(utf8);
	if (state->filter_len + add + 1 > 256)
		return false;

	if (state->filter_len + add + 1 > state->filter_cap) {
		size_t next = state->filter_cap == 0 ? 64 : state->filter_cap;
		while (next < state->filter_len + add + 1)
			next *= 2;
		char *tmp = realloc(state->filter, next);
		if (tmp == NULL)
			return false;
		state->filter = tmp;
		state->filter_cap = next;
	}

	memcpy(state->filter + state->filter_len, utf8, add);
	state->filter_len += add;
	state->filter[state->filter_len] = '\0';
	as_state_rebuild_filtered(state);
	return true;
}

static void as_state_filter_backspace(struct as_state *state)
{
	if (state == NULL || state->filter == NULL || state->filter_len == 0)
		return;

	/* Remove last UTF-8 byte sequence (best-effort). */
	size_t i = state->filter_len;
	while (i > 0 && ((unsigned char)state->filter[i - 1] & 0xC0u) == 0x80u)
		i--;
	if (i == 0)
		i = state->filter_len - 1;
	state->filter[i] = '\0';
	state->filter_len = i;
	as_state_rebuild_filtered(state);
}

static void as_state_destroy_buffers(struct as_state *state)
{
	for (size_t i = 0; i < sizeof(state->buffers) / sizeof(state->buffers[0]); i++) {
		as_buffer_destroy(state->buffers[i]);
		state->buffers[i] = NULL;
	}
}

static bool as_state_ensure_buffers(struct as_state *state)
{
	if (state->width <= 0 || state->height <= 0)
		return false;

	if (state->buffers[0] != NULL && state->buffers[0]->width == state->width && state->buffers[0]->height == state->height)
		return true;

	as_state_destroy_buffers(state);

	for (size_t i = 0; i < sizeof(state->buffers) / sizeof(state->buffers[0]); i++) {
		state->buffers[i] = as_buffer_create(state, state->width, state->height);
		if (state->buffers[i] == NULL) {
			fprintf(stderr, "aswlmenu: failed to create shm buffers (%dx%d): %s\n",
			        state->width,
			        state->height,
			        strerror(errno));
			as_state_destroy_buffers(state);
			return false;
		}
	}

	return true;
}

static struct as_buffer *as_state_acquire_buffer(struct as_state *state)
{
	for (size_t i = 0; i < sizeof(state->buffers) / sizeof(state->buffers[0]); i++) {
		struct as_buffer *buf = state->buffers[i];
		if (buf != NULL && !buf->busy)
			return buf;
	}
	return NULL;
}

struct as_menu_layout {
	int pad;
	int header_h;
	int row_h;
	int text_scale;
	int help_scale;
	int icon_size;
	int icon_col_w;
};

static bool as_state_get_layout(struct as_state *state, struct as_menu_layout *layout)
{
	if (state == NULL || layout == NULL)
		return false;

	layout->pad = 10;
	layout->text_scale = 2;
	if (state->font.use_freetype && state->font.base_px > 0)
		layout->text_scale = 1;
	layout->help_scale = 1;

	(void)aswl_font_set_scale(&state->font, layout->text_scale);
	int text_h = aswl_font_height(&state->font);
	layout->header_h = text_h + 2 * 8;
	layout->row_h = text_h + 2 * 6;
	if (layout->row_h < text_h + 4)
		layout->row_h = text_h + 4;

	layout->icon_size = layout->row_h - 8;
	if (layout->icon_size < 0)
		layout->icon_size = 0;
	if (layout->icon_size > 64)
		layout->icon_size = 64;
	layout->icon_col_w = layout->icon_size > 0 ? layout->icon_size + 10 : 0;

	return true;
}

static void as_state_autosize(struct as_state *state)
{
	if (state == NULL)
		return;

	struct as_menu_layout layout;
	if (!as_state_get_layout(state, &layout))
		return;

	(void)aswl_font_set_scale(&state->font, layout.text_scale);

	/* Size to fit the menu content, similar to AfterStep's classic root menu. */
	size_t max_rows = (size_t)env_int("ASWLMENU_ROWS", 12, 1, 64);
	size_t rows = state->filtered_count;
	if (rows < 1)
		rows = 1;
	if (rows > max_rows)
		rows = max_rows;

	int min_h = layout.header_h + layout.pad * 2 + layout.row_h;
	int desired_h = layout.header_h + layout.pad * 2 + (int)rows * layout.row_h;
	desired_h = clamp_int(desired_h, min_h, 4096);

	int max_label_w = 0;
	bool any_submenu = false;
	size_t sample = state->filtered_count;
	if (sample > 512)
		sample = 512;
	for (size_t i = 0; i < sample; i++) {
		size_t idx = state->filtered[i];
		if (idx >= state->entry_count)
			continue;
		const struct as_menu_entry *e = &state->entries[idx];
		const char *label = e->label;
		if (label == NULL)
			continue;
		int w = aswl_font_text_width(&state->font, label);
		if (w > max_label_w)
			max_label_w = w;
		if (!any_submenu && menu_command_is_submenu(e->command))
			any_submenu = true;
	}

	const char *header = state->title != NULL ? state->title : "AfterStep";
	int header_w = aswl_font_text_width(&state->font, header);

	int arrow_w = aswl_font_text_width(&state->font, ">");
	int list_w = 8 + layout.icon_col_w + max_label_w + 8;
	if (any_submenu && arrow_w > 0)
		list_w += arrow_w + 8;
	int desired_w = layout.pad * 2 + header_w;
	int desired_list_w = layout.pad * 2 + list_w;
	if (desired_list_w > desired_w)
		desired_w = desired_list_w;
	desired_w = clamp_int(desired_w, 240, 640);

	state->width = env_int("ASWLMENU_WIDTH", desired_w, 120, 4096);
	state->height = env_int("ASWLMENU_HEIGHT", desired_h, 120, 4096);
}

static size_t as_state_visible_rows(struct as_state *state, const struct as_menu_layout *layout)
{
	if (state == NULL || layout == NULL)
		return 0;

	int usable_h = state->height - layout->header_h - layout->pad * 2;
	if (usable_h <= 0 || layout->row_h <= 0)
		return 0;
	return (size_t)(usable_h / layout->row_h);
}

static void as_state_ensure_selection_visible(struct as_state *state)
{
	if (state == NULL)
		return;
	if (state->filtered_count == 0 || state->selected_index < 0)
		return;

	struct as_menu_layout layout;
	if (!as_state_get_layout(state, &layout))
		return;

	size_t rows = as_state_visible_rows(state, &layout);
	if (rows == 0)
		return;

	int sel = state->selected_index;
	if (sel < state->scroll)
		state->scroll = sel;
	else if ((size_t)sel >= (size_t)state->scroll + rows)
		state->scroll = sel - (int)rows + 1;

	if (state->scroll < 0)
		state->scroll = 0;
	if ((size_t)state->scroll > state->filtered_count)
		state->scroll = 0;
}

static void as_state_select_delta(struct as_state *state, int delta)
{
	if (state == NULL)
		return;
	if (state->filtered_count == 0)
		return;

	int sel = state->selected_index;
	if (sel < 0)
		sel = 0;
	sel += delta;
	if (sel < 0)
		sel = 0;
	if ((size_t)sel >= state->filtered_count)
		sel = (int)(state->filtered_count - 1);

	state->selected_index = sel;
	as_state_ensure_selection_visible(state);
	schedule_redraw(state);
}

static int as_state_hit_test(struct as_state *state, int x, int y)
{
	struct as_menu_layout layout;
	if (!as_state_get_layout(state, &layout))
		return -1;

	if (y < layout.header_h)
		return -1;

	int list_y = y - layout.header_h - layout.pad;
	if (list_y < 0)
		return -1;

	int row = list_y / layout.row_h;
	if (row < 0)
		return -1;

	size_t visible = as_state_visible_rows(state, &layout);
	if ((size_t)row >= visible)
		return -1;

	int idx = state->scroll + row;
	if (idx < 0 || (size_t)idx >= state->filtered_count)
		return -1;

	int row_y = layout.header_h + layout.pad + row * layout.row_h;
	if (y < row_y || y >= row_y + layout.row_h)
		return -1;

	(void)x;
	return idx;
}

static void as_state_draw(struct as_state *state, struct as_buffer *buf)
{
	if (aswl_gradient_is_valid(&state->theme.menu_item_gradient)) {
		as_buffer_fill_style_rect(buf,
		                          0,
		                          0,
		                          buf->width,
		                          buf->height,
		                          &state->theme.menu_item_gradient,
		                          state->theme.menu_bg,
		                          0);
	} else {
		as_buffer_paint_solid(buf, state->theme.menu_bg);
	}

	struct as_menu_layout layout;
	if (!as_state_get_layout(state, &layout))
		return;

	uint32_t *pixels = (uint32_t *)buf->data;
	int stride_px = buf->stride / 4;

	/* Header/filter bar */
	as_buffer_fill_style_rect(buf,
	                          0,
	                          0,
	                          buf->width,
	                          layout.header_h,
	                          &state->theme.menu_header_gradient,
	                          state->theme.menu_header_bg,
	                          0);
	as_buffer_fill_rect(buf, 0, layout.header_h - 1, buf->width, 1, state->theme.menu_border);

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
	int tx = layout.pad;
	(void)aswl_font_set_scale(&state->font, layout.text_scale);
	int ty = (layout.header_h - aswl_font_height(&state->font)) / 2;
	aswl_font_draw_text(&state->font,
	                    pixels,
	                    buf->width,
	                    buf->height,
	                    stride_px,
	                    tx,
	                    ty,
	                    header,
	                    buf->width - 2 * layout.pad,
	                    state->theme.menu_header_fg);

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

		uint32_t bg = state->theme.menu_item_bg;
		uint32_t fg = state->theme.menu_item_fg;
		const struct aswl_gradient *grad = &state->theme.menu_item_gradient;
		uint8_t nudge = 0;
		if (idx == state->selected_index) {
			bg = state->theme.menu_item_sel_bg;
			fg = state->theme.menu_item_sel_fg;
			grad = &state->theme.menu_item_sel_gradient;
		} else if (idx == state->pressed_index) {
			nudge = 48;
		} else if (idx == state->hover_index) {
			nudge = 24;
		}

		as_buffer_fill_style_rect(buf, list_x, y, list_w, layout.row_h, grad, bg, nudge);
		as_buffer_fill_rect(buf, list_x, y + layout.row_h - 1, list_w, 1, state->theme.menu_border);

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
		int arrow_w = submenu ? aswl_font_text_width(&state->font, ">") : 0;
		int text_w = list_w - (text_x - list_x) - 8;
		if (submenu)
			text_w -= arrow_w + 8;
		if (text_w < 0)
			text_w = 0;
		int ly = y + (layout.row_h - aswl_font_height(&state->font)) / 2;
		aswl_font_draw_text(&state->font,
		                    pixels,
		                    buf->width,
		                    buf->height,
		                    stride_px,
		                    text_x,
		                    ly,
		                    line,
		                    text_w,
		                    fg);

		if (submenu && arrow_w > 0) {
			int ax = list_x + list_w - 8 - arrow_w;
			int aw = arrow_w;
			if (ax < text_x) {
				ax = text_x;
				aw = list_x + list_w - 8 - ax;
			}
			if (aw > 0) {
				aswl_font_draw_text(&state->font,
				                    pixels,
				                    buf->width,
				                    buf->height,
				                    stride_px,
				                    ax,
				                    ly,
				                    ">",
				                    aw,
				                    fg);
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

	/* Border + inner bevel (AfterStep-ish). Draw last so fills don't overwrite edges. */
	if (buf->width >= 2 && buf->height >= 2) {
		as_buffer_fill_rect(buf, 0, 0, buf->width, 1, state->theme.menu_border);
		as_buffer_fill_rect(buf, 0, buf->height - 1, buf->width, 1, state->theme.menu_border);
		as_buffer_fill_rect(buf, 0, 0, 1, buf->height, state->theme.menu_border);
		as_buffer_fill_rect(buf, buf->width - 1, 0, 1, buf->height, state->theme.menu_border);
	}
	if (buf->width >= 4 && buf->height >= 4)
		as_buffer_draw_bevel_rect(buf, 1, 1, buf->width - 2, buf->height - 2, state->theme.menu_bg, false);
}

static void draw_and_commit(struct as_state *state)
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

static void schedule_redraw(struct as_state *state)
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

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	.ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
	struct as_state *state = data;
	xdg_surface_ack_configure(surface, serial);
	state->configured = true;
	schedule_redraw(state);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data,
                                   struct xdg_toplevel *toplevel,
                                   int32_t width,
                                   int32_t height,
                                   struct wl_array *states)
{
	(void)toplevel;
	(void)states;

	struct as_state *state = data;

	/* Compositor may send 0x0 to mean “no preference”. */
	if (width > 0)
		state->width = width;
	if (height > 0)
		state->height = height;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
	(void)toplevel;
	struct as_state *state = data;
	state->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure,
	.close = xdg_toplevel_close,
};

static void pointer_enter(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface,
                          wl_fixed_t surface_x,
                          wl_fixed_t surface_y)
{
	(void)pointer;
	(void)serial;
	(void)surface;

	struct as_state *state = data;
	state->pointer_in_surface = true;
	state->pointer_x = (int)wl_fixed_to_double(surface_x);
	state->pointer_y = (int)wl_fixed_to_double(surface_y);
	state->hover_index = as_state_hit_test(state, state->pointer_x, state->pointer_y);
	schedule_redraw(state);
}

static void pointer_leave(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface)
{
	(void)pointer;
	(void)serial;
	(void)surface;

	struct as_state *state = data;
	state->pointer_in_surface = false;
	state->hover_index = -1;
	state->pressed_index = -1;
	schedule_redraw(state);
}

static void pointer_motion(void *data,
                           struct wl_pointer *pointer,
                           uint32_t time,
                           wl_fixed_t surface_x,
                           wl_fixed_t surface_y)
{
	(void)pointer;
	(void)time;
	struct as_state *state = data;

	state->pointer_x = (int)wl_fixed_to_double(surface_x);
	state->pointer_y = (int)wl_fixed_to_double(surface_y);

	int old = state->hover_index;
	state->hover_index = state->pointer_in_surface ? as_state_hit_test(state, state->pointer_x, state->pointer_y) : -1;
	if (state->hover_index != old)
		schedule_redraw(state);
}

static void pointer_button(void *data,
                           struct wl_pointer *pointer,
                           uint32_t serial,
                           uint32_t time,
                           uint32_t button,
                           uint32_t state_w)
{
	(void)pointer;
	(void)serial;
	(void)time;
	struct as_state *state = data;

	if (button != BTN_LEFT)
		return;

	if (state_w == WL_POINTER_BUTTON_STATE_PRESSED) {
		state->pressed_index = state->hover_index;
		schedule_redraw(state);
		return;
	}

	if (state_w != WL_POINTER_BUTTON_STATE_RELEASED)
		return;

	int clicked = state->pressed_index;
	state->pressed_index = -1;
	schedule_redraw(state);

	if (clicked < 0 || clicked != state->hover_index)
		return;
	if ((size_t)clicked >= state->filtered_count)
		return;

	state->selected_index = clicked;
	as_state_ensure_selection_visible(state);

	size_t entry_idx = state->filtered[clicked];
	if (entry_idx >= state->entry_count)
		return;

	as_state_activate_entry(state, entry_idx);
}

static void pointer_axis(void *data,
                         struct wl_pointer *pointer,
                         uint32_t time,
                         uint32_t axis,
                         wl_fixed_t value)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
	(void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
	(void)data;
	(void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source)
{
	(void)data;
	(void)pointer;
	(void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete)
{
	(void)data;
	(void)pointer;
	(void)axis;
	(void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

#ifdef HAVE_XKBCOMMON
static void keyboard_keymap(void *data,
                            struct wl_keyboard *keyboard,
                            uint32_t format,
                            int32_t fd,
                            uint32_t size)
{
	(void)keyboard;
	struct as_state *state = data;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}

	char *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return;
	}

	if (state->xkb_context == NULL)
		state->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	struct xkb_keymap *keymap = NULL;
	if (state->xkb_context != NULL) {
		keymap = xkb_keymap_new_from_string(state->xkb_context,
		                                    map,
		                                    XKB_KEYMAP_FORMAT_TEXT_V1,
		                                    XKB_KEYMAP_COMPILE_NO_FLAGS);
	}

	munmap(map, size);
	close(fd);

	if (keymap == NULL)
		return;

	struct xkb_state *xkb_state = xkb_state_new(keymap);
	if (xkb_state == NULL) {
		xkb_keymap_unref(keymap);
		return;
	}

	if (state->xkb_state != NULL)
		xkb_state_unref(state->xkb_state);
	if (state->xkb_keymap != NULL)
		xkb_keymap_unref(state->xkb_keymap);

	state->xkb_keymap = keymap;
	state->xkb_state = xkb_state;
}
#else
static void keyboard_keymap(void *data,
                            struct wl_keyboard *keyboard,
                            uint32_t format,
                            int32_t fd,
                            uint32_t size)
{
	(void)data;
	(void)keyboard;
	(void)format;
	(void)size;
	close(fd);
}
#endif

static void keyboard_enter(void *data,
                           struct wl_keyboard *keyboard,
                           uint32_t serial,
                           struct wl_surface *surface,
                           struct wl_array *keys)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
	(void)keys;
}

static void keyboard_leave(void *data,
                           struct wl_keyboard *keyboard,
                           uint32_t serial,
                           struct wl_surface *surface)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
}

static void keyboard_key(void *data,
                         struct wl_keyboard *keyboard,
                         uint32_t serial,
                         uint32_t time,
                         uint32_t key,
                         uint32_t state_w)
{
	(void)keyboard;
	(void)serial;
	(void)time;
	struct as_state *state = data;

	if (state_w != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	if (key == KEY_ESC) {
		if (state->filter_len > 0)
			(void)as_state_filter_set(state, "");
		else if (state->menu_stack_len > 0)
			(void)as_state_go_back(state);
		else
			state->running = false;
		return;
	}

	if (key == KEY_BACKSPACE) {
		as_state_filter_backspace(state);
		return;
	}

	if (key == KEY_ENTER) {
		if (state->selected_index < 0 || (size_t)state->selected_index >= state->filtered_count)
			return;
		size_t entry_idx = state->filtered[state->selected_index];
		if (entry_idx >= state->entry_count)
			return;
		as_state_activate_entry(state, entry_idx);
		return;
	}

	if (key == KEY_UP) {
		as_state_select_delta(state, -1);
		return;
	}
	if (key == KEY_DOWN) {
		as_state_select_delta(state, +1);
		return;
	}
	if (key == KEY_PAGEUP) {
		as_state_select_delta(state, -10);
		return;
	}
	if (key == KEY_PAGEDOWN) {
		as_state_select_delta(state, +10);
		return;
	}

#ifdef HAVE_XKBCOMMON
	if (state->xkb_state != NULL) {
		uint32_t keycode = key + 8;
		char utf8[64] = { 0 };
		int n = xkb_state_key_get_utf8(state->xkb_state, keycode, utf8, (int)sizeof(utf8));
		if (n > 0) {
			utf8[(size_t)n] = '\0';
			bool any_printable = false;
			for (int i = 0; i < n; i++) {
				unsigned char ch = (unsigned char)utf8[i];
				if (ch >= 0x20 && ch != 0x7F)
					any_printable = true;
			}
			if (any_printable)
				(void)as_state_filter_append_utf8(state, utf8);
		}
	}
#else
	(void)state;
#endif
}

static void keyboard_modifiers(void *data,
                               struct wl_keyboard *keyboard,
                               uint32_t serial,
                               uint32_t mods_depressed,
                               uint32_t mods_latched,
                               uint32_t mods_locked,
                               uint32_t group)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)mods_depressed;
	(void)mods_latched;
	(void)mods_locked;
	(void)group;

#ifdef HAVE_XKBCOMMON
	struct as_state *state = data;
	if (state == NULL || state->xkb_state == NULL)
		return;

	xkb_state_update_mask(state->xkb_state,
	                      mods_depressed,
	                      mods_latched,
	                      mods_locked,
	                      0,
	                      0,
	                      group);
#endif
}

static void keyboard_repeat_info(void *data,
                                 struct wl_keyboard *keyboard,
                                 int32_t rate,
                                 int32_t delay)
{
	(void)data;
	(void)keyboard;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
	struct as_state *state = data;

	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0) {
		if (state->pointer == NULL) {
			state->pointer = wl_seat_get_pointer(seat);
			if (state->pointer != NULL)
				wl_pointer_add_listener(state->pointer, &pointer_listener, state);
		}
	} else if (state->pointer != NULL) {
		wl_pointer_destroy(state->pointer);
		state->pointer = NULL;
	}

	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0) {
		if (state->keyboard == NULL) {
			state->keyboard = wl_seat_get_keyboard(seat);
			if (state->keyboard != NULL)
				wl_keyboard_add_listener(state->keyboard, &keyboard_listener, state);
		}
	} else if (state->keyboard != NULL) {
		wl_keyboard_destroy(state->keyboard);
		state->keyboard = NULL;
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static char *desktop_exec_sanitize(const char *exec)
{
	if (exec == NULL)
		return NULL;

	size_t n = strlen(exec);
	char *out = malloc(n + 1);
	if (out == NULL)
		return NULL;

	char *d = out;
	bool prev_space = true;
	for (const char *p = exec; *p != '\0'; p++) {
		if (*p == '%') {
			p++;
			if (*p == '\0')
				break;
			if (*p == '%') {
				*d++ = '%';
				prev_space = false;
			}
			continue;
		}

		if (*p == '\t' || *p == '\n' || *p == '\r')
			continue;

		if (*p == ' ') {
			if (prev_space)
				continue;
			*d++ = ' ';
			prev_space = true;
			continue;
		}

		*d++ = *p;
		prev_space = false;
	}

	*d = '\0';
	rstrip(out);
	char *s = lstrip(out);
	if (s != out)
		memmove(out, s, strlen(s) + 1);
	if (out[0] == '\0') {
		free(out);
		return NULL;
	}
	return out;
}

static bool parse_bool(const char *s)
{
	if (s == NULL)
		return false;
	if (strcasecmp(s, "true") == 0 || strcmp(s, "1") == 0 || strcasecmp(s, "yes") == 0)
		return true;
	return false;
}

struct desktop_tmp_entry {
	bool in_entry;
	char *name;
	char *exec;
	char *icon;
	char *type;
	bool hidden;
	bool nodisplay;
};

static void desktop_tmp_finalize(struct as_state *state, const struct desktop_tmp_entry *tmp)
{
	if (state == NULL || tmp == NULL)
		return;
	if (!tmp->in_entry)
		return;
	if (tmp->name == NULL || tmp->exec == NULL)
		return;
	if (tmp->hidden || tmp->nodisplay)
		return;
	if (tmp->type != NULL && tmp->type[0] != '\0' && strcasecmp(tmp->type, "Application") != 0)
		return;
	(void)as_state_append_entry(state, tmp->name, tmp->exec, tmp->icon, false);
}

static void desktop_tmp_reset(struct desktop_tmp_entry *tmp)
{
	if (tmp == NULL)
		return;
	free(tmp->name);
	free(tmp->exec);
	free(tmp->icon);
	free(tmp->type);
	*tmp = (struct desktop_tmp_entry){ 0 };
}

static void as_state_add_desktop_file(struct as_state *state, const char *path)
{
	if (state == NULL || path == NULL || path[0] == '\0')
		return;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return;

	char *line = NULL;
	size_t cap = 0;
	ssize_t len;

	struct desktop_tmp_entry tmp = { 0 };

	while ((len = getline(&line, &cap, fp)) != -1) {
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		char *s = lstrip(line);
		if (*s == '\0' || *s == '#')
			continue;

		if (*s == '[') {
			char *end = strchr(s, ']');
			if (end == NULL)
				continue;
			*end = '\0';

			/* New group: flush previous Desktop Entry group if any. */
			desktop_tmp_finalize(state, &tmp);
			desktop_tmp_reset(&tmp);

			tmp.in_entry = strcmp(s + 1, "Desktop Entry") == 0;
			continue;
		}

		if (!tmp.in_entry)
			continue;

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';
		char *key = s;
		char *val = eq + 1;
		rstrip(key);
		val = lstrip(val);
		rstrip(val);

		if (strcmp(key, "Name") == 0) {
			free(tmp.name);
			tmp.name = strdup(val);
			continue;
		}
		if (strcmp(key, "Exec") == 0) {
			free(tmp.exec);
			tmp.exec = desktop_exec_sanitize(val);
			continue;
		}
		if (strcmp(key, "Type") == 0) {
			free(tmp.type);
			tmp.type = strdup(val);
			continue;
		}
		if (strcmp(key, "Icon") == 0) {
			free(tmp.icon);
			tmp.icon = strdup(val);
			continue;
		}
		if (strcmp(key, "Hidden") == 0) {
			tmp.hidden = parse_bool(val);
			continue;
		}
		if (strcmp(key, "NoDisplay") == 0) {
			tmp.nodisplay = parse_bool(val);
			continue;
		}
	}

	/* Flush last group. */
	desktop_tmp_finalize(state, &tmp);
	desktop_tmp_reset(&tmp);

	free(line);
	fclose(fp);
}

static void as_state_scan_desktop_dir(struct as_state *state, const char *dir_path)
{
	if (state == NULL || dir_path == NULL || dir_path[0] == '\0')
		return;

	DIR *dir = opendir(dir_path);
	if (dir == NULL)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		const char *name = ent->d_name;
		size_t n = strlen(name);
		if (n < 8)
			continue;
		if (strcmp(name + (n - 8), ".desktop") != 0)
			continue;

		char *path = NULL;
		if (asprintf(&path, "%s/%s", dir_path, name) < 0)
			continue;
		as_state_add_desktop_file(state, path);
		free(path);
	}

	closedir(dir);
}

static void as_state_add_desktop_entries(struct as_state *state)
{
	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		char *as_home = NULL;
		if (asprintf(&as_home, "%s/.afterstep/applications", home) >= 0) {
			as_state_scan_desktop_dir(state, as_home);
			free(as_home);
		}
	}

	/* Dev convenience: in-tree applications database. */
	as_state_scan_desktop_dir(state, "afterstep/applications");

	/* System AfterStep apps DB (when installed). */
	as_state_scan_desktop_dir(state, "/usr/share/afterstep/applications");

	/* Standard XDG app dirs. */
	const char *xdg_data_home = getenv("XDG_DATA_HOME");
	char *default_data_home = NULL;
	if ((xdg_data_home == NULL || xdg_data_home[0] == '\0') && home != NULL && home[0] != '\0') {
		if (asprintf(&default_data_home, "%s/.local/share", home) >= 0)
			xdg_data_home = default_data_home;
	}
	if (xdg_data_home != NULL && xdg_data_home[0] != '\0') {
		char *appdir = NULL;
		if (asprintf(&appdir, "%s/applications", xdg_data_home) >= 0) {
			as_state_scan_desktop_dir(state, appdir);
			free(appdir);
		}
	}

	const char *xdg_dirs = getenv("XDG_DATA_DIRS");
	if (xdg_dirs == NULL || xdg_dirs[0] == '\0')
		xdg_dirs = "/usr/local/share:/usr/share";

	char *dirs = strdup(xdg_dirs);
	if (dirs != NULL) {
		char *saveptr = NULL;
		for (char *tok = strtok_r(dirs, ":", &saveptr); tok != NULL; tok = strtok_r(NULL, ":", &saveptr)) {
			if (tok[0] == '\0')
				continue;
			char *appdir = NULL;
			if (asprintf(&appdir, "%s/applications", tok) >= 0) {
				as_state_scan_desktop_dir(state, appdir);
				free(appdir);
			}
		}
		free(dirs);
	}

	free(default_data_home);
}

static int cmp_entry_label_ci(const void *a, const void *b)
{
	const struct as_menu_entry *ea = a;
	const struct as_menu_entry *eb = b;
	if (ea->label == NULL && eb->label == NULL)
		return 0;
	if (ea->label == NULL)
		return -1;
	if (eb->label == NULL)
		return 1;
	return strcasecmp(ea->label, eb->label);
}

static bool load_menu_from_file_section(struct as_state *state, const char *path, const char *section)
{
	if (state == NULL || path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len;
	bool any = false;
	bool in_section = section == NULL;
	bool saw_section = section == NULL;

	while ((line_len = getline(&line, &line_cap, fp)) != -1) {
		while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
			line[--line_len] = '\0';

		char *s = lstrip(line);
		if (*s == '\0' || *s == '#')
			continue;

		if (*s == '@') {
			s++;
			char *arg = strchr(s, ' ');
			if (arg != NULL) {
				*arg = '\0';
				arg = lstrip(arg + 1);
				rstrip(arg);
			}

			if (strcmp(s, "menu") == 0) {
				in_section = false;
				if (section != NULL && arg != NULL && arg[0] != '\0' && strcmp(arg, section) == 0) {
					in_section = true;
					saw_section = true;
				}
				continue;
			}
			if (strcmp(s, "endmenu") == 0 || strcmp(s, "end_menu") == 0) {
				in_section = section == NULL;
				continue;
			}

			if (!in_section)
				continue;

			if (strcmp(s, "desktop_entries") == 0 || strcmp(s, "desktop") == 0) {
				state->include_desktop_entries = true;
				any = true;
				continue;
			}
			if (strcmp(s, "no_desktop_entries") == 0 || strcmp(s, "no_desktop") == 0) {
				state->include_desktop_entries = false;
				any = true;
				continue;
			}
			if (strcmp(s, "show_help") == 0 || strcmp(s, "help") == 0) {
				state->show_help = true;
				any = true;
				continue;
			}
			if (strcmp(s, "no_help") == 0 || strcmp(s, "hide_help") == 0) {
				state->show_help = false;
				any = true;
				continue;
			}
			if (strcmp(s, "title") == 0) {
				if (arg != NULL && arg[0] != '\0') {
					free(state->title);
					state->title = strdup(arg);
					any = true;
				}
				continue;
			}
		}

		if (!in_section)
			continue;

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';

		char *label = s;
		char *command = eq + 1;
		char *icon_spec = NULL;
		rstrip(label);
		label = lstrip(label);
		command = lstrip(command);
		rstrip(command);

		char *bar = strchr(label, '|');
		if (bar != NULL) {
			*bar = '\0';
			icon_spec = lstrip(bar + 1);
			rstrip(icon_spec);
			rstrip(label);
			if (icon_spec[0] == '\0')
				icon_spec = NULL;
		}

		if (label[0] == '\0' || command[0] == '\0')
			continue;

		any |= as_state_append_entry(state, label, command, icon_spec, true);
	}

	free(line);
	fclose(fp);
	if (section != NULL)
		return saw_section || any;
	return any;
}

static bool load_menu_from_file(struct as_state *state, const char *path)
{
	return load_menu_from_file_section(state, path, NULL);
}

static void as_state_load_menu(struct as_state *state)
{
	state->include_desktop_entries = true;
	free(state->menu_config_path);
	state->menu_config_path = NULL;

	const char *path = getenv("ASWLMENU_CONFIG");
	if (path != NULL && load_menu_from_file(state, path)) {
		state->menu_config_path = strdup(path);
		return;
	}

	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg != NULL && xdg[0] != '\0') {
		char *xdg_path = NULL;
		if (asprintf(&xdg_path, "%s/afterstep/aswlmenu.conf", xdg) >= 0) {
			bool ok = load_menu_from_file(state, xdg_path);
			if (ok)
				state->menu_config_path = strdup(xdg_path);
			free(xdg_path);
			if (ok)
				return;
		}
	}

	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		char *home_path = NULL;
		if (asprintf(&home_path, "%s/.config/afterstep/aswlmenu.conf", home) >= 0) {
			bool ok = load_menu_from_file(state, home_path);
			if (ok)
				state->menu_config_path = strdup(home_path);
			free(home_path);
			if (ok)
				return;
		}
	}

	/* Defaults if no config exists. */
	(void)as_state_append_entry(state, "Terminal", "${TERMINAL:-foot}", NULL, true);
	(void)as_state_append_entry(state, "Close focused", "@close", NULL, true);
	(void)as_state_append_entry(state, "Quit compositor", "@quit", NULL, true);
}

static void as_state_finalize_menu(struct as_state *state)
{
	if (state == NULL)
		return;

	if (state->include_desktop_entries)
		as_state_add_desktop_entries(state);

	if (state->entry_count > state->pinned_count) {
		qsort(state->entries + state->pinned_count,
		      state->entry_count - state->pinned_count,
		      sizeof(state->entries[0]),
		      cmp_entry_label_ci);
	}

	as_state_rebuild_filtered(state);
}

static bool as_state_reload_menu_from_config(struct as_state *state)
{
	if (state == NULL)
		return false;
	if (state->menu_config_path == NULL || state->menu_config_path[0] == '\0')
		return false;

	as_state_free_entries(state);
	as_state_free_filtered(state);
	state->selected_index = 0;
	state->hover_index = -1;
	state->pressed_index = -1;
	state->scroll = 0;
	as_state_filter_clear_silent(state);

	state->include_desktop_entries = state->menu_section == NULL;
	if (!load_menu_from_file_section(state, state->menu_config_path, state->menu_section))
		return false;

	as_state_finalize_menu(state);
	as_state_autosize(state);
	return true;
}

static bool as_state_go_back(struct as_state *state)
{
	if (state == NULL)
		return false;
	if (state->menu_stack_len == 0)
		return false;

	struct as_menu_stack_entry ent = state->menu_stack[--state->menu_stack_len];

	free(state->menu_section);
	state->menu_section = ent.section;
	ent.section = NULL;

	free(state->title);
	state->title = ent.title;
	ent.title = NULL;

	int restore_selected = ent.selected_index;
	int restore_scroll = ent.scroll;
	char *restore_filter = ent.filter;
	ent.filter = NULL;

	bool ok = as_state_reload_menu_from_config(state);
	if (ok) {
		if (restore_filter != NULL)
			(void)as_state_filter_set(state, restore_filter);

		if (state->filtered_count > 0) {
			if (restore_selected < 0)
				restore_selected = 0;
			if ((size_t)restore_selected >= state->filtered_count)
				restore_selected = (int)(state->filtered_count - 1);

			if (restore_scroll < 0)
				restore_scroll = 0;
			if ((size_t)restore_scroll > state->filtered_count)
				restore_scroll = 0;

			state->selected_index = restore_selected;
			state->scroll = restore_scroll;
			as_state_ensure_selection_visible(state);
		}
	}

	free(restore_filter);
	free(ent.section);
	free(ent.title);
	free(ent.filter);
	return ok;
}

static void as_state_open_submenu(struct as_state *state, size_t entry_idx)
{
	if (state == NULL)
		return;
	if (entry_idx >= state->entry_count)
		return;
	if (state->menu_config_path == NULL || state->menu_config_path[0] == '\0')
		return;

	const struct as_menu_entry *e = &state->entries[entry_idx];
	char *target = menu_command_submenu_target(e->command, e->label);
	if (target == NULL)
		return;

	if (!menu_config_section_exists(state->menu_config_path, target)) {
		fprintf(stderr, "aswlmenu: submenu section not found: %s\n", target);
		free(target);
		return;
	}

	char *submenu_title = NULL;
	if (e->label != NULL && e->label[0] != '\0')
		submenu_title = strdup(e->label);
	if (submenu_title == NULL)
		submenu_title = strdup(target);
	if (submenu_title == NULL) {
		free(target);
		return;
	}

	if (!as_state_menu_stack_push(state)) {
		free(submenu_title);
		free(target);
		return;
	}

	free(state->menu_section);
	state->menu_section = target;
	target = NULL;

	free(state->title);
	state->title = submenu_title;
	submenu_title = NULL;

	state->include_desktop_entries = false;
	if (!as_state_reload_menu_from_config(state)) {
		fprintf(stderr, "aswlmenu: failed to load submenu '%s'\n",
		        state->menu_section != NULL ? state->menu_section : "(null)");
		(void)as_state_go_back(state);
	}
}

static void as_state_activate_entry(struct as_state *state, size_t entry_idx)
{
	if (state == NULL)
		return;
	if (entry_idx >= state->entry_count)
		return;

	const char *cmd = state->entries[entry_idx].command;
	if (menu_command_is_submenu(cmd)) {
		const char *label = state->entries[entry_idx].label != NULL ? state->entries[entry_idx].label : "(null)";
		fprintf(stderr, "aswlmenu: open submenu %s\n", label);
		as_state_open_submenu(state, entry_idx);
		return;
	}

	const char *label = state->entries[entry_idx].label != NULL ? state->entries[entry_idx].label : "(null)";
	const char *command = state->entries[entry_idx].command != NULL ? state->entries[entry_idx].command : "(null)";
	fprintf(stderr, "aswlmenu: launch %s: %s\n",
	        label, command);
	as_state_launch_command(state, cmd);
	state->running = false;
}

static void update_window_list_title(struct as_state *state)
{
	if (state == NULL)
		return;
	if (!state->window_list_mode)
		return;
	if (state->title_fixed)
		return;

	uint32_t desk = state->current_workspace > 0 ? state->current_workspace - 1 : 0;
	char *t = NULL;
	if (asprintf(&t, "Windows on Desktop %u", desk) < 0)
		return;
	free(state->title);
	state->title = t;
}

static void control_workspace_state(void *data,
                                    struct afterstep_control_v1 *control,
                                    uint32_t current,
                                    uint32_t count)
{
	(void)control;
	(void)count;
	struct as_state *state = data;
	if (state == NULL)
		return;

	state->current_workspace = current;
	update_window_list_title(state);
	if (state->window_list_mode) {
		as_state_autosize(state);
		schedule_redraw(state);
	}
}

static void control_window_list_begin(void *data, struct afterstep_control_v1 *control)
{
	(void)control;
	struct as_state *state = data;
	if (state == NULL)
		return;
	if (!state->window_list_mode)
		return;

	state->window_list_in_progress = true;
	as_state_free_entries(state);
	as_state_free_filtered(state);
	state->selected_index = 0;
	state->hover_index = -1;
	state->pressed_index = -1;
	state->scroll = 0;
}

static void control_window(void *data,
                           struct afterstep_control_v1 *control,
                           uint32_t id,
                           uint32_t workspace,
                           uint32_t flags,
                           const char *title,
                           const char *app_id)
{
	(void)control;
	struct as_state *state = data;
	if (state == NULL)
		return;
	if (!state->window_list_mode)
		return;
	if (!state->window_list_in_progress)
		return;

	if ((flags & ASWL_WINDOW_FLAG_MAPPED) == 0)
		return;
	if (state->current_workspace != 0 && workspace != state->current_workspace)
		return;
	if (app_id != NULL && strcmp(app_id, "afterstep.aswlmenu") == 0)
		return;

	const char *label = NULL;
	if (title != NULL && title[0] != '\0')
		label = title;
	else if (app_id != NULL && app_id[0] != '\0')
		label = app_id;

	char fallback[64];
	if (label == NULL) {
		(void)snprintf(fallback, sizeof(fallback), "Window %u", id);
		label = fallback;
	}

	char *cmd = NULL;
	if (asprintf(&cmd, "@focus_window %u", id) < 0)
		return;
	(void)as_state_append_entry(state, label, cmd, NULL, true);
	free(cmd);
}

static void control_window_list_end(void *data, struct afterstep_control_v1 *control)
{
	(void)control;
	struct as_state *state = data;
	if (state == NULL)
		return;
	if (!state->window_list_mode)
		return;

	state->window_list_in_progress = false;
	as_state_finalize_menu(state);
	as_state_autosize(state);
	schedule_redraw(state);
}

static void control_window_closed(void *data, struct afterstep_control_v1 *control, uint32_t id)
{
	(void)control;
	(void)id;
	struct as_state *state = data;
	if (state == NULL)
		return;
	/* TODO: dynamic updates for window-list mode (not needed for screenshots). */
}

static void control_output_state(void *data,
                                 struct afterstep_control_v1 *control,
                                 uint32_t width,
                                 uint32_t height)
{
	(void)data;
	(void)control;
	(void)width;
	(void)height;
}

static void control_window_geometry(void *data,
                                    struct afterstep_control_v1 *control,
                                    uint32_t id,
                                    int32_t x,
                                    int32_t y,
                                    int32_t width,
                                    int32_t height)
{
	(void)data;
	(void)control;
	(void)id;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
}

static const struct afterstep_control_v1_listener control_listener = {
	.workspace_state = control_workspace_state,
	.output_state = control_output_state,
	.window_list_begin = control_window_list_begin,
	.window = control_window,
	.window_geometry = control_window_geometry,
	.window_list_end = control_window_list_end,
	.window_closed = control_window_closed,
};

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
	struct as_state *state = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		uint32_t bind_version = version < 4 ? version : 4;
		state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, bind_version);
		return;
	}

	if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		return;
	}

	if (strcmp(interface, afterstep_control_v1_interface.name) == 0) {
		uint32_t bind_version = version < 5 ? version : 5;
		if (bind_version < 1)
			bind_version = 1;
		state->control_version = bind_version;
		state->control = wl_registry_bind(registry, name, &afterstep_control_v1_interface, bind_version);
		if (state->control != NULL && state->window_list_mode && bind_version >= 3)
			afterstep_control_v1_add_listener(state->control, &control_listener, state);
		return;
	}

	if (strcmp(interface, wl_seat_interface.name) == 0) {
		uint32_t bind_version = version < 5 ? version : 5;
		state->seat = wl_registry_bind(registry, name, &wl_seat_interface, bind_version);
		wl_seat_add_listener(state->seat, &seat_listener, state);
		return;
	}

	if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		state->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
		return;
	}
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static bool setup_xdg(struct as_state *state)
{
	if (state->xdg_wm_base == NULL) {
		fprintf(stderr, "aswlmenu: compositor does not advertise xdg_wm_base\n");
		return false;
	}

	state->xdg_surface = xdg_wm_base_get_xdg_surface(state->xdg_wm_base, state->surface);
	if (state->xdg_surface == NULL)
		return false;

	xdg_surface_add_listener(state->xdg_surface, &xdg_surface_listener, state);

	state->xdg_toplevel = xdg_surface_get_toplevel(state->xdg_surface);
	if (state->xdg_toplevel == NULL)
		return false;

	xdg_toplevel_set_title(state->xdg_toplevel, "AfterStep Launcher (aswlmenu)");
	xdg_toplevel_set_app_id(state->xdg_toplevel, "afterstep.aswlmenu");
	xdg_toplevel_add_listener(state->xdg_toplevel, &xdg_toplevel_listener, state);

	wl_surface_commit(state->surface);
	return true;
}

static void cleanup(struct as_state *state)
{
	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);

	as_state_destroy_buffers(state);
	as_state_free_filtered(state);
	as_state_free_entries(state);
	free(state->menu_config_path);
	state->menu_config_path = NULL;
	as_state_menu_stack_clear(state);
	free(state->menu_section);
	state->menu_section = NULL;
	free(state->title);
	state->title = NULL;
	free(state->filter);
	state->filter = NULL;
	state->filter_len = 0;
	state->filter_cap = 0;
	aswl_font_destroy(&state->font);
	aswl_theme_destroy(&state->theme);

#ifdef HAVE_XKBCOMMON
	if (state->xkb_state != NULL)
		xkb_state_unref(state->xkb_state);
	if (state->xkb_keymap != NULL)
		xkb_keymap_unref(state->xkb_keymap);
	if (state->xkb_context != NULL)
		xkb_context_unref(state->xkb_context);
	state->xkb_state = NULL;
	state->xkb_keymap = NULL;
	state->xkb_context = NULL;
#endif

	if (state->xdg_toplevel != NULL)
		xdg_toplevel_destroy(state->xdg_toplevel);
	if (state->xdg_surface != NULL)
		xdg_surface_destroy(state->xdg_surface);
	if (state->xdg_wm_base != NULL)
		xdg_wm_base_destroy(state->xdg_wm_base);

	if (state->control != NULL)
		afterstep_control_v1_destroy(state->control);

	if (state->surface != NULL)
		wl_surface_destroy(state->surface);
	if (state->keyboard != NULL)
		wl_keyboard_destroy(state->keyboard);
	if (state->pointer != NULL)
		wl_pointer_destroy(state->pointer);
	if (state->seat != NULL)
		wl_seat_destroy(state->seat);
	if (state->shm != NULL)
		wl_shm_destroy(state->shm);
	if (state->compositor != NULL)
		wl_compositor_destroy(state->compositor);
	if (state->registry != NULL)
		wl_registry_destroy(state->registry);
	if (state->display != NULL)
		wl_display_disconnect(state->display);
}

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--windows]\n", prog);
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  --windows, --window-list   Show a simple window list (focus on selection)\n");
	fprintf(stderr, "  --help, -h                 Show this help\n");
}

int main(int argc, char **argv)
{
	struct as_state state = {
		.width = 640,
		.height = 520,
		.running = true,
		.hover_index = -1,
		.pressed_index = -1,
		.selected_index = 0,
		.scroll = 0,
	};

	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (strcmp(arg, "--windows") == 0 || strcmp(arg, "--window-list") == 0) {
			state.window_list_mode = true;
			continue;
		}
		if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
			usage(argv[0]);
			return 0;
		}

		fprintf(stderr, "aswlmenu: unknown argument: %s\n", arg);
		usage(argv[0]);
		return 2;
	}

	aswl_theme_init_default(&state.theme);
	(void)aswl_theme_load(&state.theme);

	const char *title_env = getenv("ASWLMENU_TITLE");
	if (title_env != NULL && title_env[0] != '\0') {
		state.title = strdup(title_env);
		state.title_fixed = true;
	} else if (state.window_list_mode) {
		state.title = strdup("Windows");
	} else {
		state.title = strdup("AfterStep");
	}

	const char *show_help = getenv("ASWLMENU_SHOW_HELP");
	if (show_help != NULL && show_help[0] != '\0' && strcmp(show_help, "0") != 0)
		state.show_help = true;

	aswl_font_init(&state.font);
	const char *font_spec = getenv("ASWLMENU_FONT");
	if (font_spec == NULL || font_spec[0] == '\0')
		font_spec = getenv("ASWL_FONT");
	if ((font_spec == NULL || font_spec[0] == '\0') && state.theme.menu_font != NULL && state.theme.menu_font[0] != '\0')
		font_spec = state.theme.menu_font;
	if (!aswl_font_load(&state.font, font_spec) && font_spec != NULL && font_spec[0] != '\0')
		fprintf(stderr, "aswlmenu: failed to load font '%s', using builtin 5x7\n", font_spec);

	if (!state.window_list_mode) {
		as_state_load_menu(&state);
		as_state_finalize_menu(&state);
		as_state_autosize(&state);
	}

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "aswlmenu: wl_display_connect failed: %s\n", strerror(errno));
		cleanup(&state);
		return 1;
	}

	state.registry = wl_display_get_registry(state.display);
	if (state.registry == NULL) {
		fprintf(stderr, "aswlmenu: wl_display_get_registry failed\n");
		cleanup(&state);
		return 1;
	}

	wl_registry_add_listener(state.registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);

	if (state.compositor == NULL || state.shm == NULL) {
		fprintf(stderr, "aswlmenu: missing required globals (compositor=%p shm=%p)\n",
		        (void *)state.compositor,
		        (void *)state.shm);
		cleanup(&state);
		return 1;
	}

	if (state.window_list_mode) {
		update_window_list_title(&state);
		as_state_finalize_menu(&state);
		as_state_autosize(&state);
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	if (state.surface == NULL) {
		fprintf(stderr, "aswlmenu: wl_compositor_create_surface failed\n");
		cleanup(&state);
		return 1;
	}

	if (!setup_xdg(&state)) {
		fprintf(stderr, "aswlmenu: failed to set up xdg-shell surface\n");
		cleanup(&state);
		return 1;
	}

	/* Ensure listeners see initial seat/keymap events. */
	wl_display_roundtrip(state.display);

	while (state.running && wl_display_dispatch(state.display) != -1) {
		if (state.needs_redraw && state.frame_cb == NULL)
			draw_and_commit(&state);
	}

	cleanup(&state);
	return 0;
}
