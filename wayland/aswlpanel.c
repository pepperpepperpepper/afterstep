#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_LIBPNG
#include <png.h>
#endif

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#if HAVE_WLR_LAYER_SHELL
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#endif

/* Avoid pulling in linux headers just for BTN_LEFT. */
#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif

struct as_button {
	char *label;
	char *command;
	char *icon_path;
	uint32_t *icon_argb;
	int icon_w;
	int icon_h;
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

	struct xdg_wm_base *xdg_wm_base;

#if HAVE_WLR_LAYER_SHELL
	struct zwlr_layer_shell_v1 *layer_shell;
#endif

	struct wl_surface *surface;
	struct wl_callback *frame_cb;

	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

#if HAVE_WLR_LAYER_SHELL
	struct zwlr_layer_surface_v1 *layer_surface;
#endif

	struct as_buffer *buffers[2];

	int width;
	int height;
	bool configured;
	bool running;

	bool needs_redraw;
	int pointer_x;
	int pointer_y;
	bool pointer_in_surface;
	int hover_index;
	int pressed_index;

	struct as_button *buttons;
	size_t button_count;
	bool buttons_owned;
};

static void schedule_redraw(struct as_state *state);
static void draw_and_commit(struct as_state *state);
static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms);

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

static int create_tmpfile(size_t size)
{
	int fd = -1;

#ifdef __linux__
	fd = memfd_create("aswlpanel", MFD_CLOEXEC);
	if (fd >= 0) {
		if (ftruncate(fd, (off_t)size) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}
#endif

	char template[] = "/tmp/aswlpanel-XXXXXX";
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
	                                           WL_SHM_FORMAT_XRGB8888);
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

static void as_buffer_paint_solid(struct as_buffer *buf, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	uint32_t *pixels = buf->data;
	size_t count = (size_t)buf->width * (size_t)buf->height;
	for (size_t i = 0; i < count; i++)
		pixels[i] = argb;
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

	for (int yy = y1; yy < y2; yy++) {
		uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)yy * (size_t)buf->stride);
		for (int xx = x1; xx < x2; xx++)
			row[xx] = argb;
	}
}

static void as_buffer_blend_pixel(struct as_buffer *buf, int x, int y, uint32_t src_argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	if (x < 0 || y < 0 || x >= buf->width || y >= buf->height)
		return;

	uint8_t sa = (uint8_t)(src_argb >> 24);
	if (sa == 0)
		return;

	uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)y * (size_t)buf->stride);

	if (sa == 255) {
		row[x] = 0xFF000000u | (src_argb & 0x00FFFFFFu);
		return;
	}

	uint32_t dst = row[x];
	uint8_t dr = (uint8_t)(dst >> 16);
	uint8_t dg = (uint8_t)(dst >> 8);
	uint8_t db = (uint8_t)dst;

	uint8_t sr = (uint8_t)(src_argb >> 16);
	uint8_t sg = (uint8_t)(src_argb >> 8);
	uint8_t sb = (uint8_t)src_argb;

	uint16_t inv = (uint16_t)(255 - sa);
	uint8_t or_ = (uint8_t)((sa * sr + inv * dr) / 255);
	uint8_t og = (uint8_t)((sa * sg + inv * dg) / 255);
	uint8_t ob = (uint8_t)((sa * sb + inv * db) / 255);

	row[x] = 0xFF000000u | ((uint32_t)or_ << 16) | ((uint32_t)og << 8) | (uint32_t)ob;
}

static void as_buffer_draw_image_nearest(struct as_buffer *buf,
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

	for (int y = 0; y < dh; y++) {
		int sy = (int)((int64_t)y * sh / dh);
		if (sy < 0)
			sy = 0;
		if (sy >= sh)
			sy = sh - 1;
		for (int x = 0; x < dw; x++) {
			int sx = (int)((int64_t)x * sw / dw);
			if (sx < 0)
				sx = 0;
			if (sx >= sw)
				sx = sw - 1;
			as_buffer_blend_pixel(buf,
			                      dx + x,
			                      dy + y,
			                      src_argb[(size_t)sy * (size_t)sw + (size_t)sx]);
		}
	}
}

static int clamp_int(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static void as_button_destroy_icon(struct as_button *button)
{
	if (button == NULL)
		return;
	free(button->icon_argb);
	button->icon_argb = NULL;
	button->icon_w = 0;
	button->icon_h = 0;
}

static void as_button_destroy(struct as_button *button)
{
	if (button == NULL)
		return;
	free(button->label);
	free(button->command);
	free(button->icon_path);
	as_button_destroy_icon(button);
	memset(button, 0, sizeof(*button));
}

static char *as_expand_tilde(const char *path)
{
	if (path == NULL)
		return NULL;
	if (path[0] != '~' || path[1] != '/')
		return strdup(path);

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		return strdup(path);

	char *out = NULL;
	if (asprintf(&out, "%s/%s", home, path + 2) < 0)
		return NULL;
	return out;
}

#ifdef HAVE_LIBPNG
static bool as_load_png_argb(const char *path, uint32_t **out_argb, int *out_w, int *out_h)
{
	if (out_argb == NULL || out_w == NULL || out_h == NULL)
		return false;

	*out_argb = NULL;
	*out_w = 0;
	*out_h = 0;

	if (path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return false;

	uint8_t header[8];
	if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
		fclose(fp);
		return false;
	}
	if (png_sig_cmp(header, 0, sizeof(header)) != 0) {
		fclose(fp);
		return false;
	}

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png == NULL) {
		fclose(fp);
		return false;
	}

	png_infop info = png_create_info_struct(png);
	if (info == NULL) {
		png_destroy_read_struct(&png, NULL, NULL);
		fclose(fp);
		return false;
	}

	uint32_t *argb = NULL;
	uint8_t *rgba = NULL;
	png_bytep *rows = NULL;

	if (setjmp(png_jmpbuf(png)) != 0)
		goto fail;

	png_init_io(png, fp);
	png_set_sig_bytes(png, sizeof(header));
	png_read_info(png, info);

	png_uint_32 w = png_get_image_width(png, info);
	png_uint_32 h = png_get_image_height(png, info);
	int color_type = png_get_color_type(png, info);
	int bit_depth = png_get_bit_depth(png, info);

	if (w == 0 || h == 0)
		goto fail;
	if (w > 4096 || h > 4096)
		goto fail;

	if (bit_depth == 16)
		png_set_strip_16(png);
	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png);
	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);
	if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

	png_read_update_info(png, info);

	png_size_t rowbytes = png_get_rowbytes(png, info);
	if (rowbytes == 0)
		goto fail;
	if (rowbytes / 4 != w)
		goto fail;

	rgba = malloc((size_t)rowbytes * (size_t)h);
	rows = malloc(sizeof(*rows) * (size_t)h);
	if (rgba == NULL || rows == NULL)
		goto fail;

	for (png_uint_32 y = 0; y < h; y++)
		rows[y] = (png_bytep)(rgba + (size_t)y * (size_t)rowbytes);

	png_read_image(png, rows);
	png_read_end(png, NULL);

	argb = malloc((size_t)w * (size_t)h * sizeof(*argb));
	if (argb == NULL)
		goto fail;

	for (png_uint_32 y = 0; y < h; y++) {
		const uint8_t *src = rgba + (size_t)y * (size_t)rowbytes;
		for (png_uint_32 x = 0; x < w; x++) {
			uint8_t r = src[x * 4 + 0];
			uint8_t g = src[x * 4 + 1];
			uint8_t b = src[x * 4 + 2];
			uint8_t a = src[x * 4 + 3];
			argb[y * (size_t)w + x] = ((uint32_t)a << 24) |
			                         ((uint32_t)r << 16) |
			                         ((uint32_t)g << 8) |
			                         (uint32_t)b;
		}
	}

	free(rgba);
	free(rows);
	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);

	*out_argb = argb;
	*out_w = (int)w;
	*out_h = (int)h;
	return true;

fail:
	free(argb);
	free(rgba);
	free(rows);
	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);
	return false;
}
#endif

static void as_button_try_load_icon(struct as_button *button)
{
	if (button == NULL)
		return;

	as_button_destroy_icon(button);

	if (button->icon_path == NULL || button->icon_path[0] == '\0')
		return;

#ifdef HAVE_LIBPNG
	char *path = as_expand_tilde(button->icon_path);
	if (path == NULL)
		return;

	uint32_t *pixels = NULL;
	int w = 0;
	int h = 0;
	if (as_load_png_argb(path, &pixels, &w, &h)) {
		button->icon_argb = pixels;
		button->icon_w = w;
		button->icon_h = h;
	} else {
		free(pixels);
	}
	free(path);
#endif
}

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
		static const uint8_t rows[7] = { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
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
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0E };
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
		static const uint8_t rows[7] = { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E };
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
		static const uint8_t rows[7] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
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
	return scale;
}

static int as_font5x7_text_width_n(const char *s, size_t n, int scale)
{
	if (s == NULL || n == 0)
		return 0;

	int w = 0;
	int glyph_w = as_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '\0')
			break;
		if (w > 0)
			w += spacing;
		w += glyph_w;
	}
	return w;
}

static int as_font5x7_text_width(const char *s, int scale)
{
	if (s == NULL)
		return 0;
	return as_font5x7_text_width_n(s, strlen(s), scale);
}

static void as_buffer_draw_glyph5x7(struct as_buffer *buf, int x, int y, char c, int scale, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL)
		return;
	if (scale <= 0)
		return;

	const uint8_t *rows = as_font5x7_rows(c);
	int gw = as_font5x7_glyph_w(scale);
	int gh = as_font5x7_glyph_h(scale);

	for (int row = 0; row < 7; row++) {
		uint8_t bits = rows[row] & 0x1F;
		for (int col = 0; col < 5; col++) {
			bool on = ((bits >> (4 - col)) & 0x1) != 0;
			if (!on)
				continue;
			as_buffer_fill_rect(buf,
			                    x + col * scale,
			                    y + row * scale,
			                    scale,
			                    scale,
			                    argb);
		}
	}

	(void)gw;
	(void)gh;
}

static size_t as_font5x7_fit_chars(const char *s, int scale, int max_w)
{
	if (s == NULL || max_w <= 0)
		return 0;

	int glyph_w = as_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);

	size_t count = 0;
	int w = 0;
	for (; s[count] != '\0'; count++) {
		int add = glyph_w;
		if (w > 0)
			add += spacing;
		if (w + add > max_w)
			break;
		w += add;
	}
	return count;
}

static void as_buffer_draw_text5x7(struct as_buffer *buf, int x, int y, const char *s, int max_w, int scale, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL)
		return;
	if (s == NULL || s[0] == '\0')
		return;
	if (scale <= 0)
		return;

	size_t n = strlen(s);

	size_t draw_n = n;
	bool need_ellipsis = false;
	int full_w = as_font5x7_text_width_n(s, n, scale);
	if (max_w > 0 && full_w > max_w) {
		int ell_w = as_font5x7_text_width_n("...", 3, scale);
		if (ell_w < max_w) {
			draw_n = as_font5x7_fit_chars(s, scale, max_w - ell_w);
			need_ellipsis = true;
		} else {
			draw_n = as_font5x7_fit_chars(s, scale, max_w);
		}
	}

	int cx = x;
	int glyph_w = as_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);

	for (size_t i = 0; i < draw_n; i++) {
		if (i > 0)
			cx += spacing;
		as_buffer_draw_glyph5x7(buf, cx, y, s[i], scale, argb);
		cx += glyph_w;
	}

	if (need_ellipsis) {
		if (draw_n > 0)
			cx += spacing;
		as_buffer_draw_text5x7(buf, cx, y, "...", max_w > 0 ? max_w - (cx - x) : 0, scale, argb);
	}
}

static void as_state_free_buttons(struct as_state *state)
{
	if (!state->buttons_owned)
		return;

	for (size_t i = 0; i < state->button_count; i++) {
		as_button_destroy(&state->buttons[i]);
	}
	free(state->buttons);
	state->buttons = NULL;
	state->button_count = 0;
	state->buttons_owned = false;
}

static bool load_buttons_from_file(struct as_state *state, const char *path)
{
	if (path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	struct as_button *buttons = NULL;
	size_t count = 0;
	size_t cap = 0;

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len;

	while ((line_len = getline(&line, &line_cap, fp)) != -1) {
		while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
			line[--line_len] = '\0';

		char *s = line;
		while (*s == ' ' || *s == '\t')
			s++;

		if (*s == '\0' || *s == '#')
			continue;

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';

		char *label = s;
		char *command = eq + 1;
		char *icon_path = NULL;

		while (*label == ' ' || *label == '\t')
			label++;
		while (*command == ' ' || *command == '\t')
			command++;

		for (char *end = label + strlen(label); end > label && (end[-1] == ' ' || end[-1] == '\t'); end--)
			end[-1] = '\0';
		for (char *end = command + strlen(command); end > command && (end[-1] == ' ' || end[-1] == '\t'); end--)
			end[-1] = '\0';

		char *bar = strchr(label, '|');
		if (bar != NULL) {
			*bar = '\0';
			icon_path = bar + 1;
			while (*icon_path == ' ' || *icon_path == '\t')
				icon_path++;
			for (char *end = icon_path + strlen(icon_path);
			     end > icon_path && (end[-1] == ' ' || end[-1] == '\t');
			     end--)
				end[-1] = '\0';
			for (char *end = label + strlen(label);
			     end > label && (end[-1] == ' ' || end[-1] == '\t');
			     end--)
				end[-1] = '\0';
			if (icon_path[0] == '\0')
				icon_path = NULL;
		}

		if (label[0] == '\0' || command[0] == '\0')
			continue;

		if (count == cap) {
			size_t next = cap == 0 ? 8 : cap * 2;
			struct as_button *tmp = realloc(buttons, next * sizeof(*buttons));
			if (tmp == NULL)
				goto fail;
			buttons = tmp;
			cap = next;
		}

		buttons[count] = (struct as_button){ 0 };
		buttons[count].label = strdup(label);
		buttons[count].command = strdup(command);
		if (icon_path != NULL)
			buttons[count].icon_path = strdup(icon_path);

		if (buttons[count].label == NULL || buttons[count].command == NULL ||
		    (icon_path != NULL && buttons[count].icon_path == NULL)) {
			as_button_destroy(&buttons[count]);
			goto fail;
		}

		as_button_try_load_icon(&buttons[count]);
		count++;
	}

	free(line);
	fclose(fp);

	if (count == 0) {
		free(buttons);
		return false;
	}

	as_state_free_buttons(state);
	state->buttons = buttons;
	state->button_count = count;
	state->buttons_owned = true;
	return true;

fail:
	free(line);
	fclose(fp);
	for (size_t i = 0; i < count; i++) {
		as_button_destroy(&buttons[i]);
	}
	free(buttons);
	return false;
}

static void as_state_load_buttons(struct as_state *state)
{
	const char *path = getenv("ASWLPANEL_CONFIG");
	if (path != NULL && load_buttons_from_file(state, path))
		return;

	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		char *xdg_path = NULL;
		if (asprintf(&xdg_path, "%s/.config/afterstep/aswlpanel.conf", home) >= 0) {
			bool ok = load_buttons_from_file(state, xdg_path);
			free(xdg_path);
			if (ok)
				return;
		}
	}

	static struct as_button defaults[] = {
		{ .label = "Terminal", .command = "foot" },
		{ .label = "Browser", .command = "firefox" },
	};

	as_state_free_buttons(state);
	state->buttons = defaults;
	state->button_count = sizeof(defaults) / sizeof(defaults[0]);
	state->buttons_owned = false;
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
			fprintf(stderr, "aswlpanel: failed to create shm buffers (%dx%d): %s\n",
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

struct as_layout {
	int pad;
	int spacing;
	int btn_h;
	int icon_pad;
	int icon_size;
	int text_gap;
	int text_scale;
	int icon_scale;
	int max_btn_w;
};

static bool as_state_get_layout(struct as_state *state, struct as_layout *layout)
{
	if (state == NULL || layout == NULL)
		return false;

	layout->pad = 6;
	layout->spacing = 6;
	layout->btn_h = state->height - 2 * layout->pad;
	if (layout->btn_h <= 0)
		return false;

	layout->icon_pad = 4;
	layout->icon_size = layout->btn_h - 2 * layout->icon_pad;
	if (layout->icon_size < 0)
		layout->icon_size = 0;

	layout->text_gap = 8;
	layout->text_scale = clamp_int((layout->btn_h - 12) / 7, 1, 4);
	layout->icon_scale = clamp_int((layout->icon_size - 2) / 7, 1, 6);
	layout->max_btn_w = 260;
	return true;
}

static int as_state_button_width(struct as_state *state, const struct as_layout *layout, size_t idx)
{
	if (state == NULL || layout == NULL)
		return 0;
	if (idx >= state->button_count)
		return 0;

	int w = layout->btn_h;

	int label_w = as_font5x7_text_width(state->buttons[idx].label, layout->text_scale);
	if (label_w > 0)
		w = layout->icon_pad + layout->icon_size + layout->text_gap + label_w + layout->icon_pad;
	else
		w = layout->icon_pad + layout->icon_size + layout->icon_pad;

	if (w < layout->btn_h)
		w = layout->btn_h;
	if (w > layout->max_btn_w)
		w = layout->max_btn_w;

	return w;
}

static int as_state_hit_test(struct as_state *state, int x, int y)
{
	if (state->button_count == 0)
		return -1;

	struct as_layout layout;
	if (!as_state_get_layout(state, &layout))
		return -1;

	int rx = layout.pad;
	int ry = layout.pad;

	for (size_t i = 0; i < state->button_count; i++) {
		int rw = as_state_button_width(state, &layout, i);
		int rh = layout.btn_h;

		if (x >= rx && x < rx + rw && y >= ry && y < ry + rh)
			return (int)i;

		rx += rw + layout.spacing;
	}

	return -1;
}

static void as_state_draw(struct as_state *state, struct as_buffer *buf)
{
	as_buffer_paint_solid(buf, 0xFF202020u);

	struct as_layout layout;
	if (!as_state_get_layout(state, &layout))
		return;

	int rx = layout.pad;
	int ry = layout.pad;

	for (size_t i = 0; i < state->button_count; i++) {
		int idx = (int)i;
		uint32_t btn_bg = 0xFF3A3A3Au;
		if (idx == state->pressed_index)
			btn_bg = 0xFF6A6A6Au;
		else if (idx == state->hover_index)
			btn_bg = 0xFF505050u;

		int rw = as_state_button_width(state, &layout, i);
		int rh = layout.btn_h;

		as_buffer_fill_rect(buf, rx, ry, rw, rh, btn_bg);

		/* Cheap border. */
		as_buffer_fill_rect(buf, rx, ry, rw, 1, 0xFF101010u);
		as_buffer_fill_rect(buf, rx, ry + rh - 1, rw, 1, 0xFF101010u);
		as_buffer_fill_rect(buf, rx, ry, 1, rh, 0xFF101010u);
		as_buffer_fill_rect(buf, rx + rw - 1, ry, 1, rh, 0xFF101010u);

		/* Icon box (placeholder): big initial + small index digit. */
		int ix = rx + layout.icon_pad;
		int iy = ry + layout.icon_pad;
		int is = layout.icon_size;
		int max_is = rw - 2 * layout.icon_pad;
		if (is > max_is)
			is = max_is;
		if (is > 0) {
			uint32_t icon_bg = 0xFF2A2A2Au;
			if (idx == state->pressed_index)
				icon_bg = 0xFF3A3A3Au;
			else if (idx == state->hover_index)
				icon_bg = 0xFF333333u;

			as_buffer_fill_rect(buf, ix, iy, is, is, icon_bg);
			as_buffer_fill_rect(buf, ix, iy, is, 1, 0xFF101010u);
			as_buffer_fill_rect(buf, ix, iy + is - 1, is, 1, 0xFF101010u);
			as_buffer_fill_rect(buf, ix, iy, 1, is, 0xFF101010u);
			as_buffer_fill_rect(buf, ix + is - 1, iy, 1, is, 0xFF101010u);

			bool drew_image = false;
			if (state->buttons[i].icon_argb != NULL && state->buttons[i].icon_w > 0 && state->buttons[i].icon_h > 0) {
				int px = ix + 1;
				int py = iy + 1;
				int ps = is - 2;
				if (ps > 0) {
					as_buffer_draw_image_nearest(buf,
					                             px,
					                             py,
					                             ps,
					                             ps,
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
				int gw = as_font5x7_glyph_w(icon_scale);
				int gh = as_font5x7_glyph_h(icon_scale);
				int gx = ix + (is - gw) / 2;
				int gy = iy + (is - gh) / 2;
				as_buffer_draw_glyph5x7(buf, gx, gy, icon_c, icon_scale, 0xFFF0F0F0u);
			}

			int number = (int)i + 1;
			int digit = number % 10;
			char digit_c = (char)('0' + digit);
			int badge_scale = clamp_int(is / 16, 1, 2);
			as_buffer_draw_glyph5x7(buf, ix + 2, iy + 2, digit_c, badge_scale, 0xFFD0D0D0u);
		}

		/* Text label to the right of the icon. */
		int text_h = as_font5x7_glyph_h(layout.text_scale);
		int tx = rx + layout.icon_pad + layout.icon_size + layout.text_gap;
		int tw = rw - (layout.icon_pad + layout.icon_size + layout.text_gap + layout.icon_pad);
		int ty = ry + (rh - text_h) / 2;
		if (tw > 0)
			as_buffer_draw_text5x7(buf, tx, ty, state->buttons[i].label, tw, layout.text_scale, 0xFFE0E0E0u);

		rx += rw + layout.spacing;
	}
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

#if HAVE_WLR_LAYER_SHELL
static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial,
                                    uint32_t width,
                                    uint32_t height)
{
	struct as_state *state = data;

	zwlr_layer_surface_v1_ack_configure(surface, serial);
	state->configured = true;

	/* If the compositor chooses dimensions, respect them. */
	if ((int)width > 0)
		state->width = (int)width;
	if ((int)height > 0)
		state->height = (int)height;

	zwlr_layer_surface_v1_set_exclusive_zone(surface, state->height);
	schedule_redraw(state);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
	(void)surface;
	struct as_state *state = data;
	state->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};
#endif

static void pointer_enter(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface,
                          wl_fixed_t surface_x,
                          wl_fixed_t surface_y)
{
	(void)pointer;
	(void)serial;
	struct as_state *state = data;

	if (surface != state->surface)
		return;

	state->pointer_in_surface = true;
	state->pointer_x = (int)wl_fixed_to_double(surface_x);
	state->pointer_y = (int)wl_fixed_to_double(surface_y);

	int old = state->hover_index;
	state->hover_index = as_state_hit_test(state, state->pointer_x, state->pointer_y);
	if (state->hover_index != old)
		schedule_redraw(state);
}

static void pointer_leave(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface)
{
	(void)pointer;
	(void)serial;
	struct as_state *state = data;

	if (surface != state->surface)
		return;

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

	if ((size_t)clicked >= state->button_count)
		return;

	fprintf(stderr, "aswlpanel: launch %s: %s\n",
	        state->buttons[clicked].label,
	        state->buttons[clicked].command);
	spawn_command(state->buttons[clicked].command);
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

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
	struct as_state *state = data;

	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0) {
		if (state->pointer == NULL) {
			state->pointer = wl_seat_get_pointer(seat);
			if (state->pointer != NULL)
				wl_pointer_add_listener(state->pointer, &pointer_listener, state);
		}
		return;
	}

	if (state->pointer != NULL) {
		wl_pointer_destroy(state->pointer);
		state->pointer = NULL;
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

#if HAVE_WLR_LAYER_SHELL
	if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		uint32_t bind_version = version < 4 ? version : 4;
		state->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, bind_version);
		return;
	}
#endif
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
		fprintf(stderr, "aswlpanel: compositor does not advertise xdg_wm_base\n");
		return false;
	}

	state->xdg_surface = xdg_wm_base_get_xdg_surface(state->xdg_wm_base, state->surface);
	if (state->xdg_surface == NULL)
		return false;

	xdg_surface_add_listener(state->xdg_surface, &xdg_surface_listener, state);

	state->xdg_toplevel = xdg_surface_get_toplevel(state->xdg_surface);
	if (state->xdg_toplevel == NULL)
		return false;

	xdg_toplevel_set_title(state->xdg_toplevel, "AfterStep Wayland PoC (aswlpanel)");
	xdg_toplevel_set_app_id(state->xdg_toplevel, "afterstep.aswlpanel");
	xdg_toplevel_add_listener(state->xdg_toplevel, &xdg_toplevel_listener, state);

	wl_surface_commit(state->surface);
	return true;
}

#if HAVE_WLR_LAYER_SHELL
static bool setup_layer_shell(struct as_state *state)
{
	if (state->layer_shell == NULL)
		return false;

	state->layer_surface = zwlr_layer_shell_v1_get_layer_surface(state->layer_shell,
	                                                             state->surface,
	                                                             NULL,
	                                                             ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	                                                             "afterstep-aswlpanel");
	if (state->layer_surface == NULL)
		return false;

	zwlr_layer_surface_v1_add_listener(state->layer_surface, &layer_surface_listener, state);

	/* Initial size request; compositor may override in configure. */
	zwlr_layer_surface_v1_set_size(state->layer_surface, 0, (uint32_t)state->height);
	zwlr_layer_surface_v1_set_anchor(state->layer_surface,
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(state->layer_surface, state->height);

	wl_surface_commit(state->surface);
	return true;
}
#endif

static void cleanup(struct as_state *state)
{
	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);

	as_state_destroy_buffers(state);
	as_state_free_buttons(state);

#if HAVE_WLR_LAYER_SHELL
	if (state->layer_surface != NULL)
		zwlr_layer_surface_v1_destroy(state->layer_surface);
	if (state->layer_shell != NULL)
		zwlr_layer_shell_v1_destroy(state->layer_shell);
#endif

	if (state->xdg_toplevel != NULL)
		xdg_toplevel_destroy(state->xdg_toplevel);
	if (state->xdg_surface != NULL)
		xdg_surface_destroy(state->xdg_surface);
	if (state->xdg_wm_base != NULL)
		xdg_wm_base_destroy(state->xdg_wm_base);

	if (state->surface != NULL)
		wl_surface_destroy(state->surface);
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

int main(void)
{
	struct as_state state = {
		.width = 360,
		.height = 64,
		.running = true,
		.hover_index = -1,
		.pressed_index = -1,
	};

	as_state_load_buttons(&state);

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "aswlpanel: wl_display_connect failed: %s\n", strerror(errno));
		return 1;
	}

	state.registry = wl_display_get_registry(state.display);
	if (state.registry == NULL) {
		fprintf(stderr, "aswlpanel: wl_display_get_registry failed\n");
		cleanup(&state);
		return 1;
	}

	wl_registry_add_listener(state.registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);

	if (state.compositor == NULL || state.shm == NULL) {
		fprintf(stderr, "aswlpanel: missing required globals (compositor=%p shm=%p)\n",
		        (void *)state.compositor,
		        (void *)state.shm);
		cleanup(&state);
		return 1;
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	if (state.surface == NULL) {
		fprintf(stderr, "aswlpanel: wl_compositor_create_surface failed\n");
		cleanup(&state);
		return 1;
	}

	bool ok = false;

#if HAVE_WLR_LAYER_SHELL
	ok = setup_layer_shell(&state);
	if (!ok)
		ok = setup_xdg(&state);
#else
	ok = setup_xdg(&state);
#endif

	if (!ok) {
		fprintf(stderr, "aswlpanel: failed to set up a surface role (layer-shell/xdg-shell)\n");
		cleanup(&state);
		return 1;
	}

	while (state.running && wl_display_dispatch(state.display) != -1) {
		/* Event-driven. */
	}

	cleanup(&state);
	return 0;
}
