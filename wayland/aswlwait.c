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

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "aswlfont.h"
#include "aswltheme.h"

struct as_buffer {
	struct wl_buffer *wl_buffer;
	void *data;
	int width;
	int height;
	int stride;
	size_t size;
	bool busy;
};

struct as_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zwlr_layer_shell_v1 *layer_shell;

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;

	struct wl_callback *frame_cb;

	struct as_buffer *buffer;

	int width;
	int height;
	bool configured;
	bool running;

	int margin_left;
	int margin_top;

	struct aswl_theme theme;
	struct aswl_font font;
	char *text;
};

static void as_buffer_destroy(struct as_buffer *buf);

static void buffer_release(void *data, struct wl_buffer *buffer)
{
	(void)buffer;
	struct as_buffer *buf = data;
	if (buf != NULL)
		buf->busy = false;
}

static int create_tmpfile(size_t size)
{
	int fd = -1;

#ifdef __linux__
	fd = memfd_create("aswlwait", MFD_CLOEXEC);
	if (fd >= 0) {
		if (ftruncate(fd, (off_t)size) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}
#endif

	char template[] = "/tmp/aswlwait-XXXXXX";
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

static struct as_buffer *as_buffer_create(struct as_state *state, int width, int height)
{
	if (state == NULL || state->shm == NULL)
		return NULL;
	if (width <= 0 || height <= 0)
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

	static const struct wl_buffer_listener wl_buf_listener = {
		.release = buffer_release,
	};
	wl_buffer_add_listener(buf->wl_buffer, &wl_buf_listener, buf);
	return buf;
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
                                      uint32_t base_argb)
{
	if (buf == NULL || buf->data == NULL)
		return;
	if (w <= 0 || h <= 0)
		return;

	if (!aswl_gradient_is_valid(grad)) {
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
		for (int xx = x1; xx < x2; xx++) {
			double t = as_gradient_t(grad->type, xx - x, yy - y, w, h);
			uint32_t c = as_gradient_sample(grad, t);
			row[xx] = as_premul_argb(c);
		}
	}
}

static long aswl_parse_long(const char *s, long def)
{
	if (s == NULL || s[0] == '\0')
		return def;
	errno = 0;
	char *end = NULL;
	long v = strtol(s, &end, 10);
	if (errno != 0 || end == s || (end != NULL && *end != '\0'))
		return def;
	return v;
}

static void aswl_state_draw(struct as_state *state, struct as_buffer *buf)
{
	if (state == NULL || buf == NULL || buf->data == NULL)
		return;

	as_buffer_fill_style_rect(buf, 0, 0, buf->width, buf->height, &state->theme.frame_active_gradient, state->theme.frame_active_bg);

	uint32_t *pixels = buf->data;
	int stride_px = buf->stride / 4;

	int tw = aswl_font_text_width(&state->font, state->text);
	int th = aswl_font_height(&state->font);

	int tx = 0;
	if (tw > 0 && tw < buf->width)
		tx = (buf->width - tw) / 2;
	int ty = 0;
	if (th > 0 && th < buf->height)
		ty = (buf->height - th) / 2;

	/* Subtle shadow for legibility against gradients. */
	uint32_t shadow = aswl_color_shadow(state->theme.frame_active_bg);
	aswl_font_draw_text(&state->font,
	                    pixels,
	                    buf->width,
	                    buf->height,
	                    stride_px,
	                    tx + 1,
	                    ty + 1,
	                    state->text,
	                    buf->width,
	                    shadow);

	aswl_font_draw_text(&state->font,
	                    pixels,
	                    buf->width,
	                    buf->height,
	                    stride_px,
	                    tx,
	                    ty,
	                    state->text,
	                    buf->width,
	                    state->theme.frame_active_fg);
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms)
{
	(void)time_ms;
	struct as_state *state = data;

	if (cb != NULL)
		wl_callback_destroy(cb);
	if (state != NULL && state->frame_cb == cb)
		state->frame_cb = NULL;
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

static void draw_and_commit(struct as_state *state)
{
	if (state == NULL || state->surface == NULL)
		return;
	if (!state->configured)
		return;
	if (state->width <= 0 || state->height <= 0)
		return;

	if (state->buffer == NULL || state->buffer->width != state->width || state->buffer->height != state->height) {
		as_buffer_destroy(state->buffer);
		state->buffer = as_buffer_create(state, state->width, state->height);
	}

	struct as_buffer *buf = state->buffer;
	if (buf == NULL || buf->data == NULL) {
		state->running = false;
		return;
	}
	if (buf->busy)
		return;

	aswl_state_draw(state, buf);

	buf->busy = true;
	wl_surface_attach(state->surface, buf->wl_buffer, 0, 0);
	wl_surface_damage(state->surface, 0, 0, buf->width, buf->height);
	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);
	state->frame_cb = wl_surface_frame(state->surface);
	wl_callback_add_listener(state->frame_cb, &frame_listener, state);
	wl_surface_commit(state->surface);
}

static void layer_surface_configure(void *data,
                                   struct zwlr_layer_surface_v1 *surface,
                                   uint32_t serial,
                                   uint32_t width,
                                   uint32_t height)
{
	struct as_state *state = data;
	if (state == NULL)
		return;

	zwlr_layer_surface_v1_ack_configure(surface, serial);

	if (width > 0)
		state->width = (int)width;
	if (height > 0)
		state->height = (int)height;

	state->configured = true;
	draw_and_commit(state);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
	(void)surface;
	struct as_state *state = data;
	if (state != NULL)
		state->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
	struct as_state *state = data;
	(void)version;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		state->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
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

static void cleanup(struct as_state *state)
{
	if (state == NULL)
		return;

	as_buffer_destroy(state->buffer);
	state->buffer = NULL;

	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);
	if (state->layer_surface != NULL)
		zwlr_layer_surface_v1_destroy(state->layer_surface);
	if (state->surface != NULL)
		wl_surface_destroy(state->surface);

	if (state->layer_shell != NULL)
		zwlr_layer_shell_v1_destroy(state->layer_shell);
	if (state->shm != NULL)
		wl_shm_destroy(state->shm);
	if (state->compositor != NULL)
		wl_compositor_destroy(state->compositor);
	if (state->registry != NULL)
		wl_registry_destroy(state->registry);
	if (state->display != NULL)
		wl_display_disconnect(state->display);

	aswl_font_destroy(&state->font);
	aswl_theme_destroy(&state->theme);
	free(state->text);
}

int main(void)
{
	struct as_state state = { 0 };
	state.running = true;

	aswl_theme_init_default(&state.theme);
	(void)aswl_theme_load(&state.theme);

	aswl_font_init(&state.font);
	const char *font_spec = getenv("ASWLWAIT_FONT");
	if (font_spec == NULL || font_spec[0] == '\0')
		font_spec = state.theme.frame_font;
	if (font_spec == NULL || font_spec[0] == '\0')
		font_spec = state.theme.menu_font;
	(void)aswl_font_load(&state.font, font_spec);

	const char *scale_env = getenv("ASWLWAIT_SCALE");
	if (scale_env != NULL && scale_env[0] != '\0') {
		long v = aswl_parse_long(scale_env, state.font.scale);
		if (v >= 1 && v <= 12)
			(void)aswl_font_set_scale(&state.font, (int)v);
	}

	const char *text = getenv("ASWLWAIT_TEXT");
	if (text == NULL || text[0] == '\0')
		text = "Waiting for window matching \"WinList\" ... Press button to cancel.";
	state.text = strdup(text);
	if (state.text == NULL) {
		fprintf(stderr, "aswlwait: out of memory\n");
		return 1;
	}

	state.margin_left = (int)aswl_parse_long(getenv("ASWLWAIT_X"), 10);
	state.margin_top = (int)aswl_parse_long(getenv("ASWLWAIT_Y"), 10);

	int want_w = (int)aswl_parse_long(getenv("ASWLWAIT_W"), 0);
	int want_h = (int)aswl_parse_long(getenv("ASWLWAIT_H"), 0);
	if (want_w <= 0) {
		int pad_x = 12;
		int border = 2;
		int tw = aswl_font_text_width(&state.font, state.text);
		want_w = tw + (pad_x + border) * 2;
		if (want_w < 200)
			want_w = 200;
	}
	if (want_h <= 0) {
		int pad_y = 6;
		int border = 2;
		int th = aswl_font_height(&state.font);
		want_h = th + (pad_y + border) * 2;
		if (want_h < 24)
			want_h = 24;
	}

	state.width = want_w;
	state.height = want_h;

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "aswlwait: wl_display_connect failed: %s\n", strerror(errno));
		cleanup(&state);
		return 1;
	}

	state.registry = wl_display_get_registry(state.display);
	if (state.registry == NULL) {
		fprintf(stderr, "aswlwait: wl_display_get_registry failed\n");
		cleanup(&state);
		return 1;
	}

	wl_registry_add_listener(state.registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);

	if (state.compositor == NULL || state.shm == NULL || state.layer_shell == NULL) {
		fprintf(stderr, "aswlwait: missing globals (compositor=%p shm=%p layer_shell=%p)\n",
		        (void *)state.compositor,
		        (void *)state.shm,
		        (void *)state.layer_shell);
		cleanup(&state);
		return 1;
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	if (state.surface == NULL) {
		fprintf(stderr, "aswlwait: wl_compositor_create_surface failed\n");
		cleanup(&state);
		return 1;
	}

	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell,
	                                                            state.surface,
	                                                            NULL,
	                                                            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
	                                                            "afterstep-aswlwait");
	if (state.layer_surface == NULL) {
		fprintf(stderr, "aswlwait: zwlr_layer_shell_v1_get_layer_surface failed\n");
		cleanup(&state);
		return 1;
	}

	zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surface_listener, &state);

	zwlr_layer_surface_v1_set_anchor(state.layer_surface,
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_margin(state.layer_surface,
	                                 state.margin_top,
	                                 0,
	                                 0,
	                                 state.margin_left);
	zwlr_layer_surface_v1_set_size(state.layer_surface, (uint32_t)state.width, (uint32_t)state.height);
	zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, 0);
	zwlr_layer_surface_v1_set_keyboard_interactivity(state.layer_surface, 0);

	wl_surface_commit(state.surface);
	wl_display_roundtrip(state.display);

	while (state.running) {
		if (wl_display_dispatch(state.display) < 0)
			break;
	}

	cleanup(&state);
	return 0;
}
