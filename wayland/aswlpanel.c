#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
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
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#include "afterstep-control-v1-client-protocol.h"
#if HAVE_WLR_LAYER_SHELL
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#endif

#include "aswltheme.h"
#include "aswlicon.h"
#include "aswlfont.h"

#if HAVE_AFTERIMAGE
#include "afterimage.h"
#endif

/* Avoid pulling in linux headers just for BTN_LEFT. */
#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif
#ifndef BTN_RIGHT
#define BTN_RIGHT 0x111
#endif
#ifndef BTN_MIDDLE
#define BTN_MIDDLE 0x112
#endif

enum as_panel_edge {
	ASWL_PANEL_EDGE_TOP = 0,
	ASWL_PANEL_EDGE_BOTTOM,
	ASWL_PANEL_EDGE_LEFT,
	ASWL_PANEL_EDGE_RIGHT,
};

enum {
	ASWL_WINDOW_FLAG_MAPPED = 1u << 0,
	ASWL_WINDOW_FLAG_FOCUSED = 1u << 1,
	ASWL_WINDOW_FLAG_XWAYLAND = 1u << 2,
};

enum {
	ASWL_ANCHOR_TOP = 1u << 0,
	ASWL_ANCHOR_BOTTOM = 1u << 1,
	ASWL_ANCHOR_LEFT = 1u << 2,
	ASWL_ANCHOR_RIGHT = 1u << 3,
};

struct as_margins {
	int top;
	int right;
	int bottom;
	int left;
};

struct as_button {
	char *label;
	char *command;
	char *icon_path;
	uint32_t *icon_argb;
	int icon_w;
	int icon_h;
};

struct as_window {
	uint32_t id;
	uint32_t workspace;
	uint32_t flags;
	int x;
	int y;
	int w;
	int h;
	char *title;
	char *app_id;
};

struct aswl_bg_snapshot_header {
	char magic[8]; /* "ASWLBG1\0" */
	uint32_t width;
	uint32_t height;
	uint32_t stride; /* bytes per row */
	uint32_t format; /* reserved; currently 0 = ARGB8888 */
};

struct as_bg_snapshot {
	void *map;
	size_t size;
	uint32_t *argb; /* points into map, immediately after header */
	int width;
	int height;
	int stride;
	char *path;
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
	struct afterstep_control_v1 *control;
	uint32_t control_version;
	uint32_t current_workspace;
	uint32_t workspace_count;
	int output_width;
	int output_height;

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
	int item_height;
	bool width_override_set;
	bool height_override_set;
	bool item_height_override_set;
	bool exclusive_zone_override_set;
	int exclusive_zone_override;
	bool configured;
	bool running;

	bool needs_redraw;
	int pointer_x;
	int pointer_y;
	bool pointer_in_surface;
	int hover_index;
	int pressed_index;
	uint32_t pressed_button;

	struct as_button *buttons;
	size_t button_count;
	bool buttons_owned;
	char *buttons_config_path;
	bool buttons_has_workspaces_directive;
	bool dock_mode;
	bool pager_mode;
	bool window_list_focused_only;
	enum as_panel_edge edge;
	struct as_margins margins;
	bool anchor_override_set;
	uint32_t anchor_override;
	int pager_columns;
	int pager_rows;

	struct as_window *windows;
	size_t window_count;
	size_t window_cap;
	bool window_list_in_progress;

	struct as_bg_snapshot bg_snapshot;
	struct aswl_theme theme;
	struct aswl_font font;
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

static void as_bg_snapshot_destroy(struct as_bg_snapshot *snap)
{
	if (snap == NULL)
		return;
	if (snap->map != NULL && snap->size > 0)
		munmap(snap->map, snap->size);
	free(snap->path);
	*snap = (struct as_bg_snapshot){ 0 };
}

static char *as_expand_tilde_dup(const char *path)
{
	if (path == NULL)
		return NULL;

	if (path[0] != '~')
		return strdup(path);

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		return strdup(path);

	if (path[1] == '\0')
		return strdup(home);
	if (path[1] != '/')
		return strdup(path);

	size_t home_len = strlen(home);
	size_t rest_len = strlen(path + 1);
	char *out = malloc(home_len + rest_len + 1);
	if (out == NULL)
		return NULL;

	memcpy(out, home, home_len);
	memcpy(out + home_len, path + 1, rest_len + 1);
	return out;
}

static char *as_sanitize_filename_segment(const char *s)
{
	if (s == NULL || s[0] == '\0')
		return strdup("wayland");

	size_t len = strlen(s);
	if (len > 200)
		len = 200;

	char *out = malloc(len + 1);
	if (out == NULL)
		return NULL;

	for (size_t i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)s[i];
		if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
			out[i] = (char)ch;
		else
			out[i] = '_';
	}
	out[len] = '\0';
	return out;
}

static char *as_bg_snapshot_path(void)
{
	const char *env = getenv("ASWLBG_SNAPSHOT");
	if (env != NULL) {
		if (env[0] == '\0' || strcmp(env, "0") == 0 || strcasecmp(env, "off") == 0 || strcasecmp(env, "false") == 0)
			return NULL;
		return as_expand_tilde_dup(env);
	}

	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime == NULL || runtime[0] == '\0')
		runtime = "/tmp";

	char *safe = as_sanitize_filename_segment(getenv("WAYLAND_DISPLAY"));
	if (safe == NULL)
		return NULL;

	char *out = NULL;
	if (asprintf(&out, "%s/afterstep.aswlbg.%s.argb", runtime, safe) < 0)
		out = NULL;
	free(safe);
	return out;
}

static bool as_bg_snapshot_load(struct as_bg_snapshot *snap, const char *path)
{
	if (snap == NULL || path == NULL || path[0] == '\0')
		return false;

	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	struct stat st;
	if (fstat(fd, &st) != 0) {
		close(fd);
		return false;
	}
	if (st.st_size < (off_t)sizeof(struct aswl_bg_snapshot_header)) {
		close(fd);
		return false;
	}

	size_t size = (size_t)st.st_size;
	void *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED)
		return false;

	const struct aswl_bg_snapshot_header *hdr = (const struct aswl_bg_snapshot_header *)map;
	static const char want_magic[8] = { 'A', 'S', 'W', 'L', 'B', 'G', '1', '\0' };
	if (memcmp(hdr->magic, want_magic, sizeof(want_magic)) != 0) {
		munmap(map, size);
		return false;
	}

	if (hdr->width == 0 || hdr->height == 0 || hdr->width > 16384 || hdr->height > 16384) {
		munmap(map, size);
		return false;
	}
	if (hdr->stride < hdr->width * 4u) {
		munmap(map, size);
		return false;
	}

	size_t needed = sizeof(*hdr);
	if (hdr->height > 0 && (size_t)hdr->stride > (SIZE_MAX - needed) / (size_t)hdr->height) {
		munmap(map, size);
		return false;
	}
	needed += (size_t)hdr->stride * (size_t)hdr->height;
	if (needed > size) {
		munmap(map, size);
		return false;
	}

	char *saved_path = strdup(path);
	if (saved_path == NULL) {
		munmap(map, size);
		return false;
	}

	*snap = (struct as_bg_snapshot){
		.map = map,
		.size = size,
		.argb = (uint32_t *)((uint8_t *)map + sizeof(*hdr)),
		.width = (int)hdr->width,
		.height = (int)hdr->height,
		.stride = (int)hdr->stride,
		.path = saved_path,
	};
	return true;
}

static bool as_state_ensure_bg_snapshot(struct as_state *state)
{
	if (state == NULL)
		return false;

	char *path = as_bg_snapshot_path();
	if (path == NULL)
		return false;

	bool ok = false;
	if (state->bg_snapshot.map != NULL && state->bg_snapshot.path != NULL && strcmp(state->bg_snapshot.path, path) == 0) {
		ok = true;
	} else {
		as_bg_snapshot_destroy(&state->bg_snapshot);
		ok = as_bg_snapshot_load(&state->bg_snapshot, path);
	}

	free(path);
	return ok;
}

static inline uint8_t as_tint_u8(uint8_t v, uint8_t tint)
{
	unsigned ratio = (unsigned)tint << 1;
	unsigned out = ((unsigned)v * ratio + 128u) >> 8;
	if (out > 255u)
		out = 255u;
	return (uint8_t)out;
}

static uint32_t as_apply_backpixmap_tint(uint32_t bg, uint32_t tint)
{
	uint8_t ba = (uint8_t)((bg >> 24) & 0xFFu);
	uint8_t br = (uint8_t)((bg >> 16) & 0xFFu);
	uint8_t bgc = (uint8_t)((bg >> 8) & 0xFFu);
	uint8_t bb = (uint8_t)(bg & 0xFFu);

	uint8_t ta = (uint8_t)((tint >> 24) & 0xFFu);
	uint8_t tr = (uint8_t)((tint >> 16) & 0xFFu);
	uint8_t tg = (uint8_t)((tint >> 8) & 0xFFu);
	uint8_t tb = (uint8_t)(tint & 0xFFu);

	uint8_t oa = as_tint_u8(ba, ta);
	uint8_t orr = as_tint_u8(br, tr);
	uint8_t og = as_tint_u8(bgc, tg);
	uint8_t ob = as_tint_u8(bb, tb);

	return ((uint32_t)oa << 24) | ((uint32_t)orr << 16) | ((uint32_t)og << 8) | (uint32_t)ob;
}

static void as_buffer_fill_rect(struct as_buffer *buf, int x, int y, int w, int h, uint32_t argb);

static void as_buffer_paint_vertical_gradient(struct as_buffer *buf, uint32_t top_argb, uint32_t bottom_argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	if (buf->height <= 0 || buf->width <= 0)
		return;

	for (int y = 0; y < buf->height; y++) {
		uint8_t t = 0;
		if (buf->height > 1) {
			t = (uint8_t)((uint32_t)y * 255u / (uint32_t)(buf->height - 1));
		}
		uint32_t c = as_premul_argb(aswl_color_blend(top_argb, bottom_argb, t));
		uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)y * (size_t)buf->stride);
		for (int x = 0; x < buf->width; x++)
			row[x] = c;
	}
}

#if HAVE_AFTERIMAGE
struct as_gradient_cache_entry {
	const struct aswl_gradient *grad;
	int width;
	int height;
	uint32_t *argb;
	uint64_t last_use;
};

enum { AS_GRADIENT_CACHE_MAX = 16 };

static struct as_gradient_cache_entry gradient_cache[AS_GRADIENT_CACHE_MAX];
static uint64_t gradient_cache_tick = 0;

static void as_gradient_cache_destroy(void)
{
	for (size_t i = 0; i < sizeof(gradient_cache) / sizeof(gradient_cache[0]); i++) {
		free(gradient_cache[i].argb);
		gradient_cache[i] = (struct as_gradient_cache_entry){ 0 };
	}
	gradient_cache_tick = 0;
}

static int as_afterimage_gradient_type(int type)
{
	/* Normalize the legacy aliases AfterStep documents. */
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

	switch (type) {
	case 6:
		return GRADIENT_TopLeft2BottomRight;
	case 7:
		return GRADIENT_BottomLeft2TopRight;
	case 8:
		return GRADIENT_Top2Bottom;
	case 9:
		return GRADIENT_Left2Right;
	default:
		return -1;
	}
}

static uint32_t *as_afterimage_make_gradient_argb(const struct aswl_gradient *grad, int width, int height)
{
	if (!aswl_gradient_is_valid(grad))
		return NULL;
	if (width <= 0 || height <= 0)
		return NULL;

	int ai_type = as_afterimage_gradient_type(grad->type);
	if (ai_type < 0)
		return NULL;

	if (grad->count < 2 || grad->count > 1024)
		return NULL;

	ASGradient ai_grad = { 0 };
	ai_grad.type = ai_type;
	ai_grad.npoints = (int)grad->count;

	ai_grad.color = calloc(grad->count, sizeof(ARGB32));
	ai_grad.offset = calloc(grad->count, sizeof(double));
	if (ai_grad.color == NULL || ai_grad.offset == NULL) {
		free(ai_grad.color);
		free(ai_grad.offset);
		return NULL;
	}

	for (size_t i = 0; i < grad->count; i++) {
		ai_grad.color[i] = (ARGB32)grad->colors[i];
		ai_grad.offset[i] = grad->offsets[i];
	}

	ASImage *im =
		make_gradient(NULL, &ai_grad, width, height, SCL_DO_ALL, ASA_ASImage, 0, ASIMAGE_QUALITY_DEFAULT);

	free(ai_grad.color);
	free(ai_grad.offset);

	if (im == NULL)
		return NULL;

	uint8_t fill_r = ARGB32_RED8(im->back_color);
	uint8_t fill_g = ARGB32_GREEN8(im->back_color);
	uint8_t fill_b = ARGB32_BLUE8(im->back_color);
	uint8_t fill_a = ARGB32_ALPHA8(im->back_color);

	uint32_t *argb = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
	CARD32 *red = calloc((size_t)width, sizeof(CARD32));
	CARD32 *green = calloc((size_t)width, sizeof(CARD32));
	CARD32 *blue = calloc((size_t)width, sizeof(CARD32));
	CARD32 *alpha = calloc((size_t)width, sizeof(CARD32));

	if (argb == NULL || red == NULL || green == NULL || blue == NULL || alpha == NULL) {
		free(argb);
		free(red);
		free(green);
		free(blue);
		free(alpha);
		destroy_asimage(&im);
		return NULL;
	}

	for (int y = 0; y < height; y++) {
		int n_r = asimage_decode_line(im, IC_RED, red, (unsigned)y, 0, (unsigned)width);
		int n_g = asimage_decode_line(im, IC_GREEN, green, (unsigned)y, 0, (unsigned)width);
		int n_b = asimage_decode_line(im, IC_BLUE, blue, (unsigned)y, 0, (unsigned)width);
		int n_a = asimage_decode_line(im, IC_ALPHA, alpha, (unsigned)y, 0, (unsigned)width);

		for (int x = 0; x < width; x++) {
			uint32_t r = (x < n_r) ? (red[x] & 0xFFu) : (uint32_t)fill_r;
			uint32_t g = (x < n_g) ? (green[x] & 0xFFu) : (uint32_t)fill_g;
			uint32_t b = (x < n_b) ? (blue[x] & 0xFFu) : (uint32_t)fill_b;
			uint32_t a = (x < n_a) ? (alpha[x] & 0xFFu) : (uint32_t)fill_a;

			argb[(size_t)y * (size_t)width + (size_t)x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	free(red);
	free(green);
	free(blue);
	free(alpha);
	destroy_asimage(&im);
	return argb;
}

static const uint32_t *as_gradient_cache_get(const struct aswl_gradient *grad, int width, int height)
{
	if (!aswl_gradient_is_valid(grad) || width <= 0 || height <= 0)
		return NULL;

	gradient_cache_tick++;
	uint64_t now = gradient_cache_tick;

	struct as_gradient_cache_entry *oldest = NULL;
	struct as_gradient_cache_entry *slot = NULL;

	for (size_t i = 0; i < sizeof(gradient_cache) / sizeof(gradient_cache[0]); i++) {
		struct as_gradient_cache_entry *ent = &gradient_cache[i];
		if (ent->argb != NULL && ent->grad == grad && ent->width == width && ent->height == height) {
			ent->last_use = now;
			return ent->argb;
		}

		if (ent->argb == NULL && slot == NULL)
			slot = ent;
		if (ent->argb != NULL && (oldest == NULL || ent->last_use < oldest->last_use))
			oldest = ent;
	}

	if (slot == NULL)
		slot = oldest;
	if (slot == NULL)
		return NULL;

	free(slot->argb);
	*slot = (struct as_gradient_cache_entry){ 0 };

	uint32_t *argb = as_afterimage_make_gradient_argb(grad, width, height);
	if (argb == NULL)
		return NULL;

	slot->grad = grad;
	slot->width = width;
	slot->height = height;
	slot->argb = argb;
	slot->last_use = now;
	return slot->argb;
}
#endif

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

#if HAVE_AFTERIMAGE
	const uint32_t *src = as_gradient_cache_get(grad, w, h);
	if (src == NULL) {
		as_buffer_fill_rect(buf, x, y, w, h, base_argb);
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
		const uint32_t *src_row = src + (size_t)(yy - y) * (size_t)w + (size_t)(x1 - x);
		for (int xx = x1; xx < x2; xx++) {
			uint32_t c = src_row[xx - x1];
			if (nudge != 0)
				c = aswl_color_nudge(c, nudge);
			row[xx] = as_premul_argb(c);
		}
	}
#else
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
			uint32_t c = grad->colors[0];
			if (nudge != 0)
				c = aswl_color_nudge(c, nudge);
			row[xx] = as_premul_argb(c);
		}
	}
#endif
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

static bool as_buffer_fill_backpixmap_tint(struct as_buffer *buf,
                                          const struct as_bg_snapshot *snap,
                                          int src_x,
                                          int src_y,
                                          uint32_t tint)
{
	if (buf == NULL || buf->data == NULL)
		return false;
	if (snap == NULL || snap->argb == NULL)
		return false;
	if (snap->width <= 0 || snap->height <= 0 || snap->stride <= 0)
		return false;

	for (int y = 0; y < buf->height; y++) {
		int sy = src_y + y;
		uint32_t *dst_row = (uint32_t *)((uint8_t *)buf->data + (size_t)y * (size_t)buf->stride);
		if (sy < 0 || sy >= snap->height) {
			for (int x = 0; x < buf->width; x++)
				dst_row[x] = 0;
			continue;
		}

		const uint32_t *src_row = (const uint32_t *)((const uint8_t *)snap->argb + (size_t)sy * (size_t)snap->stride);
		for (int x = 0; x < buf->width; x++) {
			int sx = src_x + x;
			if (sx < 0 || sx >= snap->width) {
				dst_row[x] = 0;
				continue;
			}
			uint32_t bg = src_row[sx];
			uint32_t out = as_apply_backpixmap_tint(bg, tint);
			dst_row[x] = as_premul_argb(out);
		}
	}

	return true;
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

static uint32_t as_state_default_anchor_flags(const struct as_state *state)
{
	if (state == NULL)
		return ASWL_ANCHOR_TOP | ASWL_ANCHOR_LEFT | ASWL_ANCHOR_RIGHT;

	switch (state->edge) {
	case ASWL_PANEL_EDGE_BOTTOM:
		if (state->dock_mode)
			return ASWL_ANCHOR_BOTTOM | ASWL_ANCHOR_LEFT;
		return ASWL_ANCHOR_BOTTOM | ASWL_ANCHOR_LEFT | ASWL_ANCHOR_RIGHT;
	case ASWL_PANEL_EDGE_LEFT:
		return ASWL_ANCHOR_LEFT | ASWL_ANCHOR_TOP | ASWL_ANCHOR_BOTTOM;
	case ASWL_PANEL_EDGE_RIGHT:
		return ASWL_ANCHOR_RIGHT | ASWL_ANCHOR_TOP | ASWL_ANCHOR_BOTTOM;
	case ASWL_PANEL_EDGE_TOP:
	default:
		if (state->dock_mode)
			return ASWL_ANCHOR_TOP | ASWL_ANCHOR_LEFT;
		return ASWL_ANCHOR_TOP | ASWL_ANCHOR_LEFT | ASWL_ANCHOR_RIGHT;
	}
}

static uint32_t as_state_anchor_flags(const struct as_state *state)
{
	if (state == NULL)
		return ASWL_ANCHOR_TOP | ASWL_ANCHOR_LEFT | ASWL_ANCHOR_RIGHT;
	if (state->anchor_override_set)
		return state->anchor_override;
	return as_state_default_anchor_flags(state);
}

static bool as_anchor_spans_horizontal(uint32_t anchor)
{
	return (anchor & ASWL_ANCHOR_LEFT) != 0 && (anchor & ASWL_ANCHOR_RIGHT) != 0;
}

static bool as_anchor_spans_vertical(uint32_t anchor)
{
	return (anchor & ASWL_ANCHOR_TOP) != 0 && (anchor & ASWL_ANCHOR_BOTTOM) != 0;
}

static bool as_state_compute_surface_origin(const struct as_state *state, int *x_out, int *y_out)
{
	if (x_out != NULL)
		*x_out = 0;
	if (y_out != NULL)
		*y_out = 0;
	if (state == NULL)
		return false;

	int ow = state->output_width;
	int oh = state->output_height;
	if (ow <= 0 || oh <= 0)
		return false;

	int w = state->width;
	int h = state->height;
	if (w <= 0 || h <= 0)
		return false;

	uint32_t anchor = as_state_anchor_flags(state);

	int x = 0;
	if (as_anchor_spans_horizontal(anchor) || (anchor & ASWL_ANCHOR_LEFT) != 0) {
		x = state->margins.left;
	} else if ((anchor & ASWL_ANCHOR_RIGHT) != 0) {
		x = ow - state->margins.right - w;
	} else {
		x = (ow - w) / 2;
	}

	int y = 0;
	if (as_anchor_spans_vertical(anchor) || (anchor & ASWL_ANCHOR_TOP) != 0) {
		y = state->margins.top;
	} else if ((anchor & ASWL_ANCHOR_BOTTOM) != 0) {
		y = oh - state->margins.bottom - h;
	} else {
		y = (oh - h) / 2;
	}

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	if (x_out != NULL)
		*x_out = x;
	if (y_out != NULL)
		*y_out = y;
	return true;
}

static int as_clamp_margin(int v)
{
	if (v < 0)
		return 0;
	if (v > 8192)
		return 8192;
	return v;
}

static enum as_panel_edge as_panel_edge_parse(const char *s, enum as_panel_edge fallback)
{
	if (s == NULL)
		return fallback;

	while (*s != '\0' && isspace((unsigned char)*s))
		s++;

	char token[16];
	size_t n = 0;
	while (*s != '\0' && !isspace((unsigned char)*s) && n + 1 < sizeof(token)) {
		token[n++] = (char)tolower((unsigned char)*s);
		s++;
	}
	token[n] = '\0';

	if (strcmp(token, "top") == 0)
		return ASWL_PANEL_EDGE_TOP;
	if (strcmp(token, "bottom") == 0)
		return ASWL_PANEL_EDGE_BOTTOM;
	if (strcmp(token, "left") == 0)
		return ASWL_PANEL_EDGE_LEFT;
	if (strcmp(token, "right") == 0)
		return ASWL_PANEL_EDGE_RIGHT;

	return fallback;
}

static uint32_t as_anchor_parse(const char *s, uint32_t fallback)
{
	if (s == NULL)
		return fallback;

	while (*s != '\0' && isspace((unsigned char)*s))
		s++;

	if (*s == '\0')
		return fallback;

	uint32_t out = 0;
	bool any = false;

	while (*s != '\0') {
		while (*s != '\0' && (isspace((unsigned char)*s) || *s == ',' || *s == ';'))
			s++;
		if (*s == '\0')
			break;

		char token[16];
		size_t n = 0;
		while (*s != '\0' && !isspace((unsigned char)*s) && *s != ',' && *s != ';' && n + 1 < sizeof(token)) {
			token[n++] = (char)tolower((unsigned char)*s);
			s++;
		}
		token[n] = '\0';
		if (token[0] == '\0')
			continue;

		if (strcmp(token, "none") == 0) {
			out = 0;
			any = true;
			continue;
		}
		if (strcmp(token, "top") == 0) {
			out |= ASWL_ANCHOR_TOP;
			any = true;
			continue;
		}
		if (strcmp(token, "bottom") == 0) {
			out |= ASWL_ANCHOR_BOTTOM;
			any = true;
			continue;
		}
		if (strcmp(token, "left") == 0) {
			out |= ASWL_ANCHOR_LEFT;
			any = true;
			continue;
		}
		if (strcmp(token, "right") == 0) {
			out |= ASWL_ANCHOR_RIGHT;
			any = true;
			continue;
		}
	}

	if (!any)
		return fallback;
	return out;
}

static int as_state_exclusive_zone(const struct as_state *state)
{
	if (state == NULL)
		return 0;
	if (state->exclusive_zone_override_set)
		return state->exclusive_zone_override;
	return as_panel_edge_is_vertical(state->edge) ? state->width : state->height;
}

static int as_state_calc_dock_main_axis_size(const struct as_state *state)
{
	if (state == NULL)
		return 0;
	if (!state->dock_mode)
		return 0;
	if (state->button_count == 0)
		return 0;

	/* Keep in sync with as_state_get_layout() for dock mode. */
	const int pad = 0;
	const int spacing = 0;
	const int dock_tile = 64;
	const int max_gutter = 8;

	int cross_raw = 0;
	if (as_panel_edge_is_vertical(state->edge)) {
		cross_raw = state->width - 2 * pad;
	} else {
		cross_raw = state->height - 2 * pad;
	}

	if (cross_raw < 0)
		cross_raw = 0;

	int cross = cross_raw;
	int gutter = cross_raw - dock_tile;
	if (gutter > 0 && gutter <= max_gutter)
		cross = dock_tile;

	return 2 * pad + (int)state->button_count * cross + (int)(state->button_count - 1) * spacing;
}

static bool as_parse_long_token(const char *s, const char **end_out, long *value_out)
{
	if (end_out != NULL)
		*end_out = s;
	if (value_out != NULL)
		*value_out = 0;

	if (s == NULL || s[0] == '\0')
		return false;

	char *end = NULL;
	long v = strtol(s, &end, 10);
	if (end == s)
		return false;

	if (end_out != NULL)
		*end_out = end;
	if (value_out != NULL)
		*value_out = v;
	return true;
}

static void as_margins_apply_css_shorthand(struct as_margins *m, int v1, int v2, int v3, int v4, int count)
{
	if (m == NULL || count <= 0)
		return;

	if (count == 1) {
		m->top = v1;
		m->right = v1;
		m->bottom = v1;
		m->left = v1;
		return;
	}

	if (count == 2) {
		m->top = v1;
		m->bottom = v1;
		m->left = v2;
		m->right = v2;
		return;
	}

	if (count == 3) {
		m->top = v1;
		m->left = v2;
		m->right = v2;
		m->bottom = v3;
		return;
	}

	m->top = v1;
	m->right = v2;
	m->bottom = v3;
	m->left = v4;
}

static void as_margins_parse_and_apply(struct as_margins *m, const char *spec)
{
	if (m == NULL || spec == NULL)
		return;

	const char *s = spec;
	while (*s != '\0') {
		while (*s != '\0' && (isspace((unsigned char)*s) || *s == ','))
			s++;
		if (*s == '\0')
			break;

		const char *token_start = s;
		while (*s != '\0' && !isspace((unsigned char)*s) && *s != ',')
			s++;
		size_t tok_len = (size_t)(s - token_start);
		if (tok_len == 0)
			continue;

		char tok[64];
		if (tok_len >= sizeof(tok))
			tok_len = sizeof(tok) - 1;
		memcpy(tok, token_start, tok_len);
		tok[tok_len] = '\0';

		/* key=value */
		char *eq = strchr(tok, '=');
		if (eq != NULL) {
			*eq = '\0';
			const char *key = tok;
			const char *val_s = eq + 1;
			long v = 0;
			if (as_parse_long_token(val_s, NULL, &v)) {
				int mv = as_clamp_margin((int)v);
				if (strcasecmp(key, "top") == 0)
					m->top = mv;
				else if (strcasecmp(key, "right") == 0)
					m->right = mv;
				else if (strcasecmp(key, "bottom") == 0)
					m->bottom = mv;
				else if (strcasecmp(key, "left") == 0)
					m->left = mv;
				else if (strcasecmp(key, "all") == 0)
					as_margins_apply_css_shorthand(m, mv, mv, mv, mv, 1);
			}
			continue;
		}

		/* key value */
		if (strcasecmp(tok, "top") == 0 || strcasecmp(tok, "right") == 0 || strcasecmp(tok, "bottom") == 0 ||
		    strcasecmp(tok, "left") == 0 || strcasecmp(tok, "all") == 0) {
			while (*s != '\0' && (isspace((unsigned char)*s) || *s == ','))
				s++;
			long v = 0;
			const char *next = NULL;
			if (as_parse_long_token(s, &next, &v)) {
				int mv = as_clamp_margin((int)v);
				if (strcasecmp(tok, "top") == 0)
					m->top = mv;
				else if (strcasecmp(tok, "right") == 0)
					m->right = mv;
				else if (strcasecmp(tok, "bottom") == 0)
					m->bottom = mv;
				else if (strcasecmp(tok, "left") == 0)
					m->left = mv;
				else
					as_margins_apply_css_shorthand(m, mv, mv, mv, mv, 1);
				s = next;
			}
			continue;
		}

		/* Positional integers; parse up to 4 starting from this token. */
		long v[4] = { 0, 0, 0, 0 };
		int n = 0;

		const char *scan = token_start;
		while (*scan != '\0' && n < 4) {
			while (*scan != '\0' && (isspace((unsigned char)*scan) || *scan == ','))
				scan++;
			if (*scan == '\0')
				break;

			const char *next = NULL;
			long tmp = 0;
			if (!as_parse_long_token(scan, &next, &tmp))
				break;
			v[n++] = (long)as_clamp_margin((int)tmp);
			scan = next;
		}

		if (n > 0) {
			as_margins_apply_css_shorthand(m, (int)v[0], (int)v[1], (int)v[2], (int)v[3], n);
			s = scan;
		}
	}
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

static void as_button_try_load_icon(struct as_button *button)
{
	if (button == NULL)
		return;

	as_button_destroy_icon(button);

	if (button->icon_path == NULL || button->icon_path[0] == '\0')
		return;

	uint32_t *pixels = NULL;
	int w = 0;
	int h = 0;
	if (aswl_icon_load_argb(button->icon_path, &pixels, &w, &h)) {
		button->icon_argb = pixels;
		button->icon_w = w;
		button->icon_h = h;
	} else {
		free(pixels);
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

static void as_window_destroy(struct as_window *win)
{
	if (win == NULL)
		return;
	free(win->title);
	free(win->app_id);
	*win = (struct as_window){ 0 };
}

static void as_state_clear_windows(struct as_state *state)
{
	if (state == NULL)
		return;
	for (size_t i = 0; i < state->window_count; i++)
		as_window_destroy(&state->windows[i]);
	state->window_count = 0;
}

static void as_state_destroy_windows(struct as_state *state)
{
	if (state == NULL)
		return;
	as_state_clear_windows(state);
	free(state->windows);
	state->windows = NULL;
	state->window_cap = 0;
}

static struct as_window *as_state_find_window(struct as_state *state, uint32_t id)
{
	if (state == NULL || id == 0)
		return NULL;
	for (size_t i = 0; i < state->window_count; i++) {
		if (state->windows[i].id == id)
			return &state->windows[i];
	}
	return NULL;
}

static bool as_state_upsert_window(struct as_state *state,
                                  uint32_t id,
                                  uint32_t workspace,
                                  uint32_t flags,
                                  const char *title,
                                  const char *app_id)
{
	if (state == NULL || id == 0)
		return false;

	struct as_window *win = as_state_find_window(state, id);
	if (win == NULL) {
		if (state->window_count == state->window_cap) {
			size_t next = state->window_cap == 0 ? 16 : state->window_cap * 2;
			struct as_window *tmp = realloc(state->windows, next * sizeof(*tmp));
			if (tmp == NULL)
				return false;
			state->windows = tmp;
			state->window_cap = next;
		}

		win = &state->windows[state->window_count++];
		*win = (struct as_window){ 0 };
		win->id = id;
	}

	win->workspace = workspace;
	win->flags = flags;

	char *new_title = strdup(title != NULL ? title : "");
	char *new_app_id = strdup(app_id != NULL ? app_id : "");
	if (new_title == NULL || new_app_id == NULL) {
		free(new_title);
		free(new_app_id);
		return false;
	}

	free(win->title);
	free(win->app_id);
	win->title = new_title;
	win->app_id = new_app_id;
	return true;
}

static bool as_state_upsert_window_geometry(struct as_state *state,
                                           uint32_t id,
                                           int x,
                                           int y,
                                           int w,
                                           int h)
{
	if (state == NULL || id == 0)
		return false;

	struct as_window *win = as_state_find_window(state, id);
	if (win == NULL) {
		if (state->window_count == state->window_cap) {
			size_t next = state->window_cap == 0 ? 16 : state->window_cap * 2;
			struct as_window *tmp = realloc(state->windows, next * sizeof(*tmp));
			if (tmp == NULL)
				return false;
			state->windows = tmp;
			state->window_cap = next;
		}

		win = &state->windows[state->window_count++];
		*win = (struct as_window){ 0 };
		win->id = id;

		win->title = strdup("");
		win->app_id = strdup("");
		if (win->title == NULL || win->app_id == NULL) {
			free(win->title);
			free(win->app_id);
			win->title = NULL;
			win->app_id = NULL;
			state->window_count--;
			return false;
		}
	}

	win->x = x;
	win->y = y;
	win->w = w;
	win->h = h;
	return true;
}

static bool as_state_remove_window(struct as_state *state, uint32_t id)
{
	if (state == NULL || id == 0)
		return false;

	for (size_t i = 0; i < state->window_count; i++) {
		if (state->windows[i].id != id)
			continue;
		as_window_destroy(&state->windows[i]);
		if (i + 1 < state->window_count)
			memmove(&state->windows[i], &state->windows[i + 1], (state->window_count - i - 1) * sizeof(state->windows[0]));
		state->window_count--;
		return true;
	}
	return false;
}

static bool append_button(struct as_button **buttons,
                          size_t *count,
                          size_t *cap,
                          const char *label,
                          const char *command,
                          const char *icon_path)
{
	if (buttons == NULL || count == NULL || cap == NULL)
		return false;
	if (label == NULL || label[0] == '\0')
		return false;
	if (command == NULL || command[0] == '\0')
		return false;

	if (*count == *cap) {
		size_t next = *cap == 0 ? 8 : (*cap) * 2;
		struct as_button *tmp = realloc(*buttons, next * sizeof(**buttons));
		if (tmp == NULL)
			return false;
		*buttons = tmp;
		*cap = next;
	}

	(*buttons)[*count] = (struct as_button){ 0 };
	(*buttons)[*count].label = strdup(label);
	(*buttons)[*count].command = strdup(command);
	if (icon_path != NULL)
		(*buttons)[*count].icon_path = strdup(icon_path);

	if ((*buttons)[*count].label == NULL || (*buttons)[*count].command == NULL ||
	    (icon_path != NULL && (*buttons)[*count].icon_path == NULL)) {
		as_button_destroy(&(*buttons)[*count]);
		return false;
	}

	as_button_try_load_icon(&(*buttons)[*count]);
	(*count)++;
	return true;
}

static bool handle_button_directive(struct as_state *state,
                                   bool *dock_mode_io,
                                   bool *pager_mode_io,
                                   struct as_button **buttons,
                                   size_t *count,
                                   size_t *cap,
                                   const char *line,
                                   bool *handled_out,
                                   bool *workspaces_out)
{
	if (handled_out != NULL)
		*handled_out = false;
	if (workspaces_out != NULL)
		*workspaces_out = false;

	if (state == NULL || buttons == NULL || count == NULL || cap == NULL || line == NULL)
		return true;

	const char *s = line;
	if (s[0] != '@')
		return true;
	s++;

	const char *arg = NULL;
	if (strncmp(s, "workspaces", 10) == 0 && (s[10] == '\0' || isspace((unsigned char)s[10]) || s[10] == ':' || s[10] == '=')) {
		arg = s + 10;
	} else if (strncmp(s, "pager", 5) == 0 && (s[5] == '\0' || isspace((unsigned char)s[5]) || s[5] == ':' || s[5] == '=')) {
		arg = s + 5;
	} else if (strncmp(s, "mode", 4) == 0 && (s[4] == '\0' || isspace((unsigned char)s[4]) || s[4] == ':' || s[4] == '=')) {
		arg = s + 4;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;

		char token[16];
		size_t n = 0;
		while (*arg != '\0' && !isspace((unsigned char)*arg) && n + 1 < sizeof(token)) {
			token[n++] = (char)tolower((unsigned char)*arg);
			arg++;
		}
		token[n] = '\0';

		if (strcmp(token, "dock") == 0) {
			if (dock_mode_io != NULL)
				*dock_mode_io = true;
			if (pager_mode_io != NULL)
				*pager_mode_io = false;
		} else if (strcmp(token, "panel") == 0) {
			if (dock_mode_io != NULL)
				*dock_mode_io = false;
			if (pager_mode_io != NULL)
				*pager_mode_io = false;
		} else if (strcmp(token, "pager") == 0) {
			if (dock_mode_io != NULL)
				*dock_mode_io = false;
			if (pager_mode_io != NULL)
				*pager_mode_io = true;
		}

		return true;
	} else if (strncmp(s, "margin", 6) == 0 && (s[6] == '\0' || isspace((unsigned char)s[6]) || s[6] == ':' || s[6] == '=')) {
		arg = s + 6;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		as_margins_parse_and_apply(&state->margins, arg);
		return true;
	} else if (strncmp(s, "edge", 4) == 0 && (s[4] == '\0' || isspace((unsigned char)s[4]) || s[4] == ':' || s[4] == '=')) {
		arg = s + 4;
		if (handled_out != NULL)
			*handled_out = true;

		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;

		if (state != NULL && arg[0] != '\0')
			state->edge = as_panel_edge_parse(arg, state->edge);

		return true;
	} else if (strncmp(s, "anchor", 6) == 0 && (s[6] == '\0' || isspace((unsigned char)s[6]) || s[6] == ':' || s[6] == '=')) {
		arg = s + 6;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		state->anchor_override = as_anchor_parse(arg, state->anchor_override);
		state->anchor_override_set = true;
		return true;
	} else if (strncmp(s, "width", 5) == 0 && (s[5] == '\0' || isspace((unsigned char)s[5]) || s[5] == ':' || s[5] == '=')) {
		arg = s + 5;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		long v = 0;
		if (as_parse_long_token(arg, NULL, &v) && v >= 64 && v <= 8192) {
			state->width = (int)v;
			state->width_override_set = true;
		}
		return true;
	} else if (strncmp(s, "height", 6) == 0 && (s[6] == '\0' || isspace((unsigned char)s[6]) || s[6] == ':' || s[6] == '=')) {
		arg = s + 6;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		long v = 0;
		if (as_parse_long_token(arg, NULL, &v) && v >= 16 && v <= 8192) {
			state->height = (int)v;
			state->height_override_set = true;
		}
		return true;
	} else if (strncmp(s, "item_height", 11) == 0 &&
	           (s[11] == '\0' || isspace((unsigned char)s[11]) || s[11] == ':' || s[11] == '=')) {
		arg = s + 11;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		long v = 0;
		if (as_parse_long_token(arg, NULL, &v) && v >= 16 && v <= 512) {
			state->item_height = (int)v;
			state->item_height_override_set = true;
		}
		return true;
	} else if (strncmp(s, "pager_columns", 13) == 0 &&
	           (s[13] == '\0' || isspace((unsigned char)s[13]) || s[13] == ':' || s[13] == '=')) {
		arg = s + 13;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		long v = 0;
		if (as_parse_long_token(arg, NULL, &v) && v >= 1 && v <= 16)
			state->pager_columns = (int)v;
		return true;
	} else if (strncmp(s, "pager_rows", 10) == 0 &&
	           (s[10] == '\0' || isspace((unsigned char)s[10]) || s[10] == ':' || s[10] == '=')) {
		arg = s + 10;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		long v = 0;
		if (as_parse_long_token(arg, NULL, &v) && v >= 1 && v <= 16)
			state->pager_rows = (int)v;
		return true;
	} else if (strncmp(s, "dock", 4) == 0 && (s[4] == '\0' || isspace((unsigned char)s[4]) || s[4] == ':' || s[4] == '=')) {
		if (dock_mode_io != NULL)
			*dock_mode_io = true;
		if (pager_mode_io != NULL)
			*pager_mode_io = false;
		if (handled_out != NULL)
			*handled_out = true;
		return true;
	} else if (strncmp(s, "nodock", 6) == 0 && (s[6] == '\0' || isspace((unsigned char)s[6]) || s[6] == ':' || s[6] == '=')) {
		if (dock_mode_io != NULL)
			*dock_mode_io = false;
		if (handled_out != NULL)
			*handled_out = true;
		return true;
	} else {
		return true;
	}

	if (handled_out != NULL)
		*handled_out = true;
	if (workspaces_out != NULL)
		*workspaces_out = true;

	while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
		arg++;

	uint32_t n = state->workspace_count > 0 ? state->workspace_count : 9;
	if (*arg != '\0') {
		char *end = NULL;
		unsigned long tmp = strtoul(arg, &end, 10);
		while (end != NULL && isspace((unsigned char)*end))
			end++;
		if (end != arg && end != NULL && *end == '\0' && tmp >= 1 && tmp <= 1000)
			n = (uint32_t)tmp;
	}

	for (uint32_t i = 1; i <= n; i++) {
		char label[16];
		char command[64];
		(void)snprintf(label, sizeof(label), "%u", i);
		(void)snprintf(command, sizeof(command), "@workspace %u", i);
		if (!append_button(buttons, count, cap, label, command, NULL))
			return false;
	}

	return true;
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
	bool has_workspaces_directive = false;
	bool dock_mode = state->dock_mode;
	bool pager_mode = state->pager_mode;

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

		if (*s == '@') {
			bool handled = false;
			bool workspaces = false;
			if (!handle_button_directive(state, &dock_mode, &pager_mode, &buttons, &count, &cap, s, &handled, &workspaces))
				goto fail;
			if (handled) {
				if (workspaces)
					has_workspaces_directive = true;
				continue;
			}
		}

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

		if (!append_button(&buttons, &count, &cap, label, command, icon_path))
			goto fail;
	}

	free(line);
	fclose(fp);

	as_state_free_buttons(state);
	state->buttons = buttons;
	state->button_count = count;
	state->buttons_owned = true;
	state->buttons_has_workspaces_directive = has_workspaces_directive;
	state->dock_mode = dock_mode;
	state->pager_mode = pager_mode;
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
	state->buttons_has_workspaces_directive = false;
	free(state->buttons_config_path);
	state->buttons_config_path = NULL;
	state->dock_mode = false;
	state->pager_mode = false;
	state->edge = ASWL_PANEL_EDGE_TOP;
	state->margins = (struct as_margins){ 0 };
	state->anchor_override_set = false;
	state->anchor_override = 0;
	state->width_override_set = false;
	state->height_override_set = false;
	state->item_height_override_set = false;
	state->pager_columns = 2;
	state->pager_rows = 1;

	const char *mode = getenv("ASWLPANEL_MODE");
	if (mode != NULL && mode[0] != '\0') {
		if (strcasecmp(mode, "dock") == 0)
			state->dock_mode = true;
		else if (strcasecmp(mode, "panel") == 0)
			state->dock_mode = false;
		else if (strcasecmp(mode, "pager") == 0)
			state->pager_mode = true;
	}

	const char *dock = getenv("ASWLPANEL_DOCK");
	if (dock != NULL && dock[0] != '\0') {
		if (dock[0] == '1' || strcasecmp(dock, "true") == 0 || strcasecmp(dock, "yes") == 0)
			state->dock_mode = true;
	}
	if (state->dock_mode)
		state->pager_mode = false;

	const char *edge = getenv("ASWLPANEL_EDGE");
	if (edge != NULL && edge[0] != '\0')
		state->edge = as_panel_edge_parse(edge, state->edge);

	bool loaded = false;

	const char *path = getenv("ASWLPANEL_CONFIG");
	if (path != NULL && load_buttons_from_file(state, path)) {
		state->buttons_config_path = strdup(path);
		loaded = true;
	}

	if (!loaded) {
		const char *home = getenv("HOME");
		if (home != NULL && home[0] != '\0') {
			char *xdg_path = NULL;
			if (asprintf(&xdg_path, "%s/.config/afterstep/aswlpanel.conf", home) >= 0) {
				bool ok = load_buttons_from_file(state, xdg_path);
				if (ok)
					state->buttons_config_path = strdup(xdg_path);
				free(xdg_path);
				loaded = ok;
			}
		}
	}

	if (!loaded) {
		static struct as_button defaults[] = {
			{ .label = "Terminal", .command = "foot" },
			{ .label = "Browser", .command = "firefox" },
		};

		as_state_free_buttons(state);
		state->buttons = defaults;
		state->button_count = sizeof(defaults) / sizeof(defaults[0]);
		state->buttons_owned = false;
	}

	const char *margin_all = getenv("ASWLPANEL_MARGIN");
	if (margin_all != NULL && margin_all[0] != '\0')
		as_margins_parse_and_apply(&state->margins, margin_all);

	const char *margin_top = getenv("ASWLPANEL_MARGIN_TOP");
	const char *margin_right = getenv("ASWLPANEL_MARGIN_RIGHT");
	const char *margin_bottom = getenv("ASWLPANEL_MARGIN_BOTTOM");
	const char *margin_left = getenv("ASWLPANEL_MARGIN_LEFT");
	long mv = 0;
	if (margin_top != NULL && margin_top[0] != '\0' && as_parse_long_token(margin_top, NULL, &mv))
		state->margins.top = as_clamp_margin((int)mv);
	if (margin_right != NULL && margin_right[0] != '\0' && as_parse_long_token(margin_right, NULL, &mv))
		state->margins.right = as_clamp_margin((int)mv);
	if (margin_bottom != NULL && margin_bottom[0] != '\0' && as_parse_long_token(margin_bottom, NULL, &mv))
		state->margins.bottom = as_clamp_margin((int)mv);
	if (margin_left != NULL && margin_left[0] != '\0' && as_parse_long_token(margin_left, NULL, &mv))
		state->margins.left = as_clamp_margin((int)mv);
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
	int cross;
	int row;
	int dock_gutter;
	int icon_pad;
	int icon_size;
	int text_gap;
	int text_scale;
	int icon_scale;
	int max_main;
	int text_h;
	bool vertical;
};

static bool as_state_get_layout(struct as_state *state, struct as_layout *layout)
{
	if (state == NULL || layout == NULL)
		return false;

	layout->vertical = as_panel_edge_is_vertical(state->edge);

	if (state->dock_mode) {
		layout->pad = 0;
		layout->spacing = 0;
	} else if (state->pager_mode && layout->vertical) {
		/* Pager should match X11's tight desk borders (no outer padding). */
		layout->pad = 0;
		layout->spacing = 0;
	} else {
		layout->pad = 6;
		layout->spacing = 6;
	}

	int cross_raw = layout->vertical ? (state->width - 2 * layout->pad) : (state->height - 2 * layout->pad);
	if (cross_raw <= 0)
		return false;

	layout->cross = cross_raw;
	layout->dock_gutter = 0;
	if (state->dock_mode) {
		const int dock_tile = 64;
		const int max_gutter = 8;
		int gutter = cross_raw - dock_tile;
		if (gutter > 0 && gutter <= max_gutter) {
			layout->cross = dock_tile;
			layout->dock_gutter = gutter;
		}
	}

	layout->row = layout->cross;
	if (layout->vertical && !state->dock_mode) {
		int row = state->item_height > 0 ? state->item_height : 48;
		layout->row = clamp_int(row, 24, 256);
	}

	/* AfterStep Wharf tiles generally render ~48px icons inside ~64px buttons. */
	layout->icon_pad = state->dock_mode ? 8 : 4;
	int icon_dim = layout->cross;
	if (layout->vertical && !state->dock_mode)
		icon_dim = layout->row;
	layout->icon_size = icon_dim - 2 * layout->icon_pad;
	int max_icon = layout->cross - 2 * layout->icon_pad;
	if (layout->icon_size > max_icon)
		layout->icon_size = max_icon;
	if (layout->icon_size < 0)
		layout->icon_size = 0;

	layout->text_gap = state->dock_mode ? 0 : 8;
	int text_dim = layout->cross;
	if (layout->vertical && !state->dock_mode)
		text_dim = layout->row;
	if (state->font.use_freetype && state->font.base_px > 0)
		layout->text_scale = 1;
	else
		layout->text_scale = clamp_int((text_dim - 24) / 7, 1, 3);
	layout->icon_scale = clamp_int((layout->icon_size - 2) / 7, 1, 6);
	layout->max_main = state->dock_mode ? layout->cross : 260;
	if (!state->dock_mode && !layout->vertical && state->button_count == 0 && state->window_list_focused_only) {
		int max = state->width - 2 * layout->pad;
		if (max > layout->cross)
			layout->max_main = max;
	}

	(void)aswl_font_set_scale(&state->font, layout->text_scale);
	layout->text_h = aswl_font_height(&state->font);
	return true;
}

static bool as_command_parse_workspace_target(const char *command, uint32_t *workspace_out)
{
	if (workspace_out != NULL)
		*workspace_out = 0;

	if (command == NULL || command[0] == '\0')
		return false;
	if (command[0] != '@')
		return false;

	const char *action = command + 1;
	const char *ws_arg = NULL;
	if (strncmp(action, "workspace", 9) == 0) {
		ws_arg = action + 9;
	} else if (strncmp(action, "ws", 2) == 0) {
		ws_arg = action + 2;
	} else {
		return false;
	}

	while (*ws_arg == ':' || *ws_arg == '=' || isspace((unsigned char)*ws_arg))
		ws_arg++;

	if (*ws_arg == '\0')
		return false;

	char *end = NULL;
	unsigned long ws = strtoul(ws_arg, &end, 10);
	while (end != NULL && isspace((unsigned char)*end))
		end++;

	if (end == ws_arg || end == NULL || *end != '\0' || ws < 1 || ws > 1000)
		return false;

	if (workspace_out != NULL)
		*workspace_out = (uint32_t)ws;
	return true;
}

static int as_state_button_main_size(struct as_state *state, const struct as_layout *layout, size_t idx)
{
	if (state == NULL || layout == NULL)
		return 0;
	if (idx >= state->button_count)
		return 0;

	if (state->dock_mode)
		return layout->cross;

	if (layout->vertical) {
		return layout->row;
	}

	int w = layout->cross;

	(void)aswl_font_set_scale(&state->font, layout->text_scale);
	int label_w = aswl_font_text_width(&state->font, state->buttons[idx].label);
	if (label_w > 0)
		w = layout->icon_pad + layout->icon_size + layout->text_gap + label_w + layout->icon_pad;
	else
		w = layout->icon_pad + layout->icon_size + layout->icon_pad;

	if (w < layout->cross)
		w = layout->cross;
	if (w > layout->max_main)
		w = layout->max_main;

	return w;
}

static bool as_window_visible(const struct as_state *state, const struct as_window *win)
{
	if (state == NULL || win == NULL)
		return false;
	if ((win->flags & ASWL_WINDOW_FLAG_MAPPED) == 0)
		return false;
	if (state->window_list_focused_only && (win->flags & ASWL_WINDOW_FLAG_FOCUSED) == 0)
		return false;
	return win->workspace == state->current_workspace;
}

static const char *as_window_label(const struct as_window *win)
{
	if (win == NULL)
		return "Window";
	if (win->title != NULL && win->title[0] != '\0')
		return win->title;
	if (win->app_id != NULL && win->app_id[0] != '\0')
		return win->app_id;
	return "Window";
}

static int as_state_window_main_size(struct as_state *state, const struct as_layout *layout, const struct as_window *win)
{
	if (state == NULL || layout == NULL || win == NULL)
		return 0;

	if (layout->vertical)
		return layout->text_h + 2 * layout->icon_pad;

	if (state->window_list_focused_only && state->button_count == 0) {
		/*
		 * The classic AfterStep WinList strip is a single, full-width frame with
		 * the focused window title right-aligned. Match that look by forcing the
		 * entry to span the available main axis.
		 */
		return layout->max_main;
	}

	const char *label = as_window_label(win);
	(void)aswl_font_set_scale(&state->font, layout->text_scale);
	int label_w = aswl_font_text_width(&state->font, label);
	int w = layout->icon_pad + label_w + layout->icon_pad;

	if (w < layout->cross)
		w = layout->cross;
	if (w > layout->max_main)
		w = layout->max_main;

	return w;
}

static struct as_window *as_state_visible_window_nth(struct as_state *state, size_t n)
{
	if (state == NULL)
		return NULL;
	size_t idx = 0;
	for (size_t i = 0; i < state->window_count; i++) {
		if (!as_window_visible(state, &state->windows[i]))
			continue;
		if (idx++ == n)
			return &state->windows[i];
	}
	return NULL;
}

static int as_state_hit_test(struct as_state *state, int x, int y)
{
	if (state == NULL)
		return -1;

	struct as_layout layout;
	if (!as_state_get_layout(state, &layout))
		return -1;

	int rx = layout.pad;
	int ry = layout.pad;

	if (state->pager_mode && layout.vertical && !state->dock_mode) {
		size_t ws_count = 0;
		size_t nav_count = 0;
		for (size_t i = 0; i < state->button_count; i++) {
			if (as_command_parse_workspace_target(state->buttons[i].command, NULL))
				ws_count++;
			else
				nav_count++;
		}

		int nav_h = clamp_int(state->item_height > 0 ? state->item_height : 48, 24, 128);
		int nav_total = (int)nav_count * nav_h + (nav_count > 0 ? (int)(nav_count - 1) * layout.spacing : 0);
		int nav_y0 = state->height - layout.pad - nav_total;

		int workspace_area_h = nav_y0 - layout.pad;
		if (ws_count > 0 && nav_count > 0)
			workspace_area_h -= layout.spacing;

		int tile_h = 0;
		if (ws_count > 0)
			tile_h = (workspace_area_h - (int)(ws_count - 1) * layout.spacing) / (int)ws_count;
		if (tile_h < 24)
			tile_h = 24;

		for (size_t i = 0; i < state->button_count; i++) {
			bool is_ws = as_command_parse_workspace_target(state->buttons[i].command, NULL);
			int bx = layout.pad;
			int by = layout.pad;
			int bw = layout.cross;
			int bh = nav_h;

			if (is_ws && tile_h > 0) {
				size_t pos = 0;
				for (size_t j = 0; j < i; j++) {
					if (as_command_parse_workspace_target(state->buttons[j].command, NULL))
						pos++;
				}
				by = layout.pad + (int)pos * (tile_h + layout.spacing);
				bh = tile_h;
			} else if (!is_ws) {
				size_t pos = 0;
				for (size_t j = 0; j < i; j++) {
					if (!as_command_parse_workspace_target(state->buttons[j].command, NULL))
						pos++;
				}
				by = nav_y0 + (int)pos * (nav_h + layout.spacing);
				bh = nav_h;
			} else {
				bh = nav_h;
			}

			if (x >= bx && x < bx + bw && y >= by && y < by + bh)
				return (int)i;
		}

		return -1;
	}

	for (size_t i = 0; i < state->button_count; i++) {
		int main = as_state_button_main_size(state, &layout, i);
		int rw = layout.vertical ? layout.cross : main;
		int rh = layout.vertical ? main : layout.cross;

		if (x >= rx && x < rx + rw && y >= ry && y < ry + rh)
			return (int)i;

		if (layout.vertical)
			ry += rh + layout.spacing;
		else
			rx += rw + layout.spacing;
	}

	if (state->dock_mode)
		return -1;

	int wx = layout.vertical ? layout.pad : rx;
	int wy = layout.vertical ? (ry + layout.spacing) : layout.pad;
	int ww = 0;
	int wh = 0;
	size_t vis_idx = 0;
	for (;;) {
		struct as_window *win = as_state_visible_window_nth(state, vis_idx);
		if (win == NULL)
			break;

		int main = as_state_window_main_size(state, &layout, win);
		if (layout.vertical) {
			ww = layout.cross;
			wh = main;
			if (wy + wh > state->height - layout.pad)
				break;
		} else {
			ww = main;
			wh = layout.cross;
			if (wx + ww > state->width - layout.pad)
				break;
		}

		if (x >= wx && x < wx + ww && y >= wy && y < wy + wh)
			return (int)state->button_count + (int)vis_idx;

		if (layout.vertical)
			wy += wh + layout.spacing;
		else
			wx += ww + layout.spacing;
		vis_idx++;
	}

	return -1;
}

static void as_state_draw(struct as_state *state, struct as_buffer *buf)
{
	bool pager_panel = (state->pager_mode && as_panel_edge_is_vertical(state->edge) && !state->dock_mode);
	bool winlist_strip = (!state->dock_mode && state->button_count == 0 && state->window_list_focused_only);
	bool winlist_has_window = winlist_strip && (as_state_visible_window_nth(state, 0) != NULL);
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
			if (as_state_compute_surface_origin(state, &ox, &oy) && as_state_ensure_bg_snapshot(state) &&
			    state->bg_snapshot.width == state->output_width && state->bg_snapshot.height == state->output_height) {
				bg_backpix_filled = as_buffer_fill_backpixmap_tint(buf, &state->bg_snapshot, ox, oy, bg_backpix_tint);
			}
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
				ix = rx + (rw - is) / 2 - 2;
				iy = ry + (rh - is) / 2 - 2;
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

		const struct aswl_gradient *grad = winlist_frame_style ? &state->theme.frame_inactive_gradient : &state->theme.panel_button_gradient;
		if (is_focused)
			grad = winlist_frame_style ? &state->theme.frame_active_gradient : &state->theme.panel_ws_active_gradient;

		as_buffer_fill_style_rect(buf, wx, wy, rw, rh, grad, base_bg, nudge);
		uint32_t bevel_bg = nudge != 0 ? aswl_color_nudge(base_bg, nudge) : base_bg;
		as_buffer_draw_bevel_rect(buf, wx, wy, rw, rh, bevel_bg, idx == state->pressed_index);

		int tx = wx + layout.icon_pad;
		int tw = rw - 2 * layout.icon_pad;
		const char *label = as_window_label(win);
		if (state->window_list_focused_only && !layout.vertical && state->button_count == 0) {
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

	zwlr_layer_surface_v1_set_exclusive_zone(surface, as_state_exclusive_zone(state));
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

static void as_state_launch_command(struct as_state *state, const char *command)
{
	if (command == NULL || command[0] == '\0')
		return;

	if (state != NULL && state->control != NULL) {
		if (command[0] == '@') {
			const char *action = command + 1;
			if (strcmp(action, "quit") == 0 || strcmp(action, "exit") == 0) {
				fprintf(stderr, "aswlpanel: compositor quit\n");
				afterstep_control_v1_quit(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "close") == 0 || strcmp(action, "close_focused") == 0) {
				fprintf(stderr, "aswlpanel: compositor close_focused\n");
				afterstep_control_v1_close_focused(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "focus_next") == 0 || strcmp(action, "next") == 0) {
				fprintf(stderr, "aswlpanel: compositor focus_next\n");
				afterstep_control_v1_focus_next(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "focus_prev") == 0 || strcmp(action, "prev") == 0) {
				fprintf(stderr, "aswlpanel: compositor focus_prev\n");
				afterstep_control_v1_focus_prev(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if ((strcmp(action, "fullscreen") == 0 || strcmp(action, "toggle_fullscreen") == 0) &&
			    state->control_version >= 5) {
				fprintf(stderr, "aswlpanel: compositor toggle_fullscreen\n");
				afterstep_control_v1_toggle_fullscreen(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if ((strcmp(action, "maximize") == 0 || strcmp(action, "maximized") == 0 ||
			     strcmp(action, "toggle_maximized") == 0 || strcmp(action, "toggle_maximize") == 0) &&
			    state->control_version >= 5) {
				fprintf(stderr, "aswlpanel: compositor toggle_maximized\n");
				afterstep_control_v1_toggle_maximized(state->control);
				(void)wl_display_flush(state->display);
				return;
			}

			if ((strcmp(action, "workspace_next") == 0 || strcmp(action, "ws_next") == 0 || strcmp(action, "ws+") == 0) &&
			    state->control_version >= 2) {
				fprintf(stderr, "aswlpanel: compositor workspace_next\n");
				afterstep_control_v1_workspace_next(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if ((strcmp(action, "workspace_prev") == 0 || strcmp(action, "ws_prev") == 0 || strcmp(action, "ws-") == 0) &&
			    state->control_version >= 2) {
				fprintf(stderr, "aswlpanel: compositor workspace_prev\n");
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
						fprintf(stderr, "aswlpanel: compositor set_workspace=%lu\n", ws);
						afterstep_control_v1_set_workspace(state->control, (uint32_t)ws);
						(void)wl_display_flush(state->display);
						return;
					}
				}
			}
		}

		fprintf(stderr, "aswlpanel: compositor exec %s\n", command);
		afterstep_control_v1_exec(state->control, command);
		(void)wl_display_flush(state->display);
		return;
	}

	spawn_command(command);
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

	if (button != BTN_LEFT && button != BTN_RIGHT && button != BTN_MIDDLE)
		return;

	if (state_w == WL_POINTER_BUTTON_STATE_PRESSED) {
		state->pressed_index = state->hover_index;
		state->pressed_button = button;
		schedule_redraw(state);
		return;
	}

	if (state_w != WL_POINTER_BUTTON_STATE_RELEASED)
		return;

	int clicked = state->pressed_index;
	uint32_t clicked_button = state->pressed_button;
	state->pressed_index = -1;
	state->pressed_button = 0;
	schedule_redraw(state);

	if (clicked < 0 || clicked != state->hover_index)
		return;

	if ((size_t)clicked < state->button_count) {
		if (clicked_button != BTN_LEFT)
			return;
		fprintf(stderr, "aswlpanel: launch %s: %s\n",
		        state->buttons[clicked].label,
		        state->buttons[clicked].command);
		as_state_launch_command(state, state->buttons[clicked].command);
		return;
	}

	size_t win_idx = (size_t)clicked - state->button_count;
	struct as_window *win = as_state_visible_window_nth(state, win_idx);
	if (win == NULL || win->id == 0)
		return;

	if (state->control != NULL && state->control_version >= 4) {
		if (clicked_button == BTN_LEFT) {
			fprintf(stderr, "aswlpanel: focus_window %u\n", win->id);
			afterstep_control_v1_focus_window(state->control, win->id);
		} else if (clicked_button == BTN_MIDDLE) {
			fprintf(stderr, "aswlpanel: close_window %u\n", win->id);
			afterstep_control_v1_close_window(state->control, win->id);
		} else if (clicked_button == BTN_RIGHT) {
			uint32_t count = state->workspace_count > 0 ? state->workspace_count : 1;
			uint32_t next = state->current_workspace + 1;
			if (next > count)
				next = 1;
			fprintf(stderr, "aswlpanel: move_window_to_workspace %u -> %u\n", win->id, next);
			afterstep_control_v1_move_window_to_workspace(state->control, win->id, next);
		}
		(void)wl_display_flush(state->display);
	}
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

static void control_workspace_state(void *data,
                                   struct afterstep_control_v1 *control,
                                   uint32_t current,
                                   uint32_t count)
{
	(void)control;
	struct as_state *state = data;
	if (state == NULL)
		return;

	if (count < 1)
		count = 1;

	bool count_changed = state->workspace_count != count;
	state->workspace_count = count;
	state->current_workspace = current;

	if (count_changed && state->buttons_has_workspaces_directive && state->buttons_config_path != NULL) {
		fprintf(stderr, "aswlpanel: workspace_count=%u, reloading %s\n", count, state->buttons_config_path);
		(void)load_buttons_from_file(state, state->buttons_config_path);
	}

	schedule_redraw(state);
}

static void control_window_list_begin(void *data, struct afterstep_control_v1 *control)
{
	(void)control;
	struct as_state *state = data;
	if (state == NULL)
		return;

	state->window_list_in_progress = true;
	as_state_clear_windows(state);
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

	(void)as_state_upsert_window(state, id, workspace, flags, title, app_id);
	if (!state->window_list_in_progress)
		schedule_redraw(state);
}

static void control_window_list_end(void *data, struct afterstep_control_v1 *control)
{
	(void)control;
	struct as_state *state = data;
	if (state == NULL)
		return;

	state->window_list_in_progress = false;
	schedule_redraw(state);
}

static void control_output_state(void *data,
                                 struct afterstep_control_v1 *control,
                                 uint32_t width,
                                 uint32_t height)
{
	(void)control;
	struct as_state *state = data;
	if (state == NULL)
		return;

	state->output_width = (int)width;
	state->output_height = (int)height;
	schedule_redraw(state);
}

static void control_window_geometry(void *data,
                                    struct afterstep_control_v1 *control,
                                    uint32_t id,
                                    int32_t x,
                                    int32_t y,
                                    int32_t width,
                                    int32_t height)
{
	(void)control;
	struct as_state *state = data;
	if (state == NULL)
		return;

	(void)as_state_upsert_window_geometry(state, id, (int)x, (int)y, (int)width, (int)height);
	if (!state->window_list_in_progress)
		schedule_redraw(state);
}

static void control_window_closed(void *data,
                                  struct afterstep_control_v1 *control,
                                  uint32_t id)
{
	(void)control;
	struct as_state *state = data;
	if (state == NULL)
		return;

	if (as_state_remove_window(state, id))
		schedule_redraw(state);
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
		uint32_t bind_version = version < 6 ? version : 6;
		if (bind_version < 1)
			bind_version = 1;
		state->control_version = bind_version;
		state->control = wl_registry_bind(registry, name, &afterstep_control_v1_interface, bind_version);
		if (state->control != NULL && bind_version >= 3)
			afterstep_control_v1_add_listener(state->control, &control_listener, state);
		if (state->control != NULL && bind_version >= 4)
			afterstep_control_v1_list_windows(state->control);
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
	uint32_t anchor_flags = as_state_anchor_flags(state);

	uint32_t anchor = 0;
	if ((anchor_flags & ASWL_ANCHOR_TOP) != 0)
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	if ((anchor_flags & ASWL_ANCHOR_BOTTOM) != 0)
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	if ((anchor_flags & ASWL_ANCHOR_LEFT) != 0)
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
	if ((anchor_flags & ASWL_ANCHOR_RIGHT) != 0)
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;

	uint32_t w = as_anchor_spans_horizontal(anchor_flags) ? 0u : (uint32_t)state->width;
	uint32_t h = as_anchor_spans_vertical(anchor_flags) ? 0u : (uint32_t)state->height;

	zwlr_layer_surface_v1_set_size(state->layer_surface, w, h);
	zwlr_layer_surface_v1_set_anchor(state->layer_surface, anchor);
	zwlr_layer_surface_v1_set_margin(state->layer_surface,
	                                 (uint32_t)state->margins.top,
	                                 (uint32_t)state->margins.right,
	                                 (uint32_t)state->margins.bottom,
	                                 (uint32_t)state->margins.left);
	zwlr_layer_surface_v1_set_exclusive_zone(state->layer_surface, as_state_exclusive_zone(state));

	wl_surface_commit(state->surface);
	return true;
}
#endif

static void cleanup(struct as_state *state)
{
	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);

#if HAVE_AFTERIMAGE
	as_gradient_cache_destroy();
#endif

	as_state_destroy_buffers(state);
	as_state_free_buttons(state);
	as_state_destroy_windows(state);
	as_bg_snapshot_destroy(&state->bg_snapshot);
	free(state->buttons_config_path);
	state->buttons_config_path = NULL;
	aswl_font_destroy(&state->font);
	aswl_theme_destroy(&state->theme);

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

	if (state->control != NULL)
		afterstep_control_v1_destroy(state->control);

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
		.item_height = 0,
		.pager_columns = 2,
		.pager_rows = 1,
		.running = true,
		.hover_index = -1,
		.pressed_index = -1,
		.current_workspace = 1,
		.workspace_count = 9,
		.edge = ASWL_PANEL_EDGE_TOP,
	};

	aswl_theme_init_default(&state.theme);
	(void)aswl_theme_load(&state.theme);

	aswl_font_init(&state.font);
	const char *font_spec = getenv("ASWLPANEL_FONT");
	if (font_spec == NULL || font_spec[0] == '\0')
		font_spec = getenv("ASWL_FONT");
	if ((font_spec == NULL || font_spec[0] == '\0') && state.theme.panel_font != NULL && state.theme.panel_font[0] != '\0')
		font_spec = state.theme.panel_font;
	if (!aswl_font_load(&state.font, font_spec) && font_spec != NULL && font_spec[0] != '\0')
		fprintf(stderr, "aswlpanel: failed to load font '%s', using builtin 5x7\n", font_spec);

	as_state_load_buttons(&state);

	const char *winlist_mode = getenv("ASWLPANEL_WINDOW_LIST");
	if (winlist_mode != NULL && winlist_mode[0] != '\0') {
		if (strcasecmp(winlist_mode, "focused") == 0 || strcasecmp(winlist_mode, "focused-only") == 0 ||
		    strcasecmp(winlist_mode, "focus") == 0) {
			state.window_list_focused_only = true;
		} else if (winlist_mode[0] == '1' || strcasecmp(winlist_mode, "true") == 0 || strcasecmp(winlist_mode, "yes") == 0) {
			state.window_list_focused_only = true;
		}
	}

	const char *excl_env = getenv("ASWLPANEL_EXCLUSIVE_ZONE");
	if (excl_env != NULL && excl_env[0] != '\0') {
		char *end = NULL;
		long v = strtol(excl_env, &end, 10);
		if (end != excl_env && end != NULL && *end == '\0' && v >= 0 && v <= 8192) {
			state.exclusive_zone_override_set = true;
			state.exclusive_zone_override = (int)v;
		}
	}

	const char *height_env = getenv("ASWLPANEL_HEIGHT");
	if (height_env != NULL && height_env[0] != '\0') {
		char *end = NULL;
		long h = strtol(height_env, &end, 10);
		if (end != height_env && end != NULL && *end == '\0' && h >= 16 && h <= 512) {
			state.height = (int)h;
			state.height_override_set = true;
		}
	} else if (!state.height_override_set && state.dock_mode && !as_panel_edge_is_vertical(state.edge)) {
		state.height = 48;
	}

	if (as_panel_edge_is_vertical(state.edge) && !state.dock_mode && !state.item_height_override_set)
		state.item_height = 48;

	const char *item_height_env = getenv("ASWLPANEL_ITEM_HEIGHT");
	if (item_height_env != NULL && item_height_env[0] != '\0') {
		char *end = NULL;
		long h = strtol(item_height_env, &end, 10);
		if (end != item_height_env && end != NULL && *end == '\0' && h >= 16 && h <= 512) {
			state.item_height = (int)h;
			state.item_height_override_set = true;
		}
	}

	const char *width_env = getenv("ASWLPANEL_WIDTH");
	bool width_set = state.width_override_set;
	if (width_env != NULL && width_env[0] != '\0') {
		char *end = NULL;
		long w = strtol(width_env, &end, 10);
		if (end != width_env && end != NULL && *end == '\0' && w >= 64 && w <= 8192)
			state.width = (int)w;
		width_set = true;
	}

	const char *pager_cols_env = getenv("ASWLPANEL_PAGER_COLUMNS");
	if (pager_cols_env != NULL && pager_cols_env[0] != '\0') {
		char *end = NULL;
		long v = strtol(pager_cols_env, &end, 10);
		if (end != pager_cols_env && end != NULL && *end == '\0' && v >= 1 && v <= 16)
			state.pager_columns = (int)v;
	}

	const char *pager_rows_env = getenv("ASWLPANEL_PAGER_ROWS");
	if (pager_rows_env != NULL && pager_rows_env[0] != '\0') {
		char *end = NULL;
		long v = strtol(pager_rows_env, &end, 10);
		if (end != pager_rows_env && end != NULL && *end == '\0' && v >= 1 && v <= 16)
			state.pager_rows = (int)v;
	}

	if (!width_set && state.dock_mode && !as_panel_edge_is_vertical(state.edge)) {
		int dock_w = as_state_calc_dock_main_axis_size(&state);
		if (dock_w >= 64 && dock_w <= 8192)
			state.width = dock_w;
	}

	if (!width_set && as_panel_edge_is_vertical(state.edge)) {
		if (state.dock_mode)
			state.width = 64;
		else if (state.pager_mode)
			state.width = 192;
		else
			state.width = 96;
	}

	uint32_t anchor_flags = as_state_anchor_flags(&state);
	if (as_panel_edge_is_vertical(state.edge) && state.dock_mode && !as_anchor_spans_vertical(anchor_flags) &&
	    !state.height_override_set) {
		int dock_h = as_state_calc_dock_main_axis_size(&state);
		if (dock_h >= 64 && dock_h <= 8192) {
			state.height = dock_h;
			state.height_override_set = true;
		}
	}

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
