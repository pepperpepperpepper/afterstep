#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>

#include "ext-session-lock-v1-client-protocol.h"

#include "aswlfont.h"
#include "aswltheme.h"

struct as_state;

struct as_buffer {
	struct wl_buffer *wl_buffer;
	void *data;
	int width;
	int height;
	int stride;
	size_t size;
	bool busy;
	bool destroy_on_release;
};

struct as_output {
	struct wl_list link; /* as_state.outputs */
	struct as_state *state;
	uint32_t registry_name;

	struct wl_output *wl_output;
	int32_t scale;

	struct wl_surface *surface;
	struct ext_session_lock_surface_v1 *lock_surface;

	bool configured;
	uint32_t configure_serial;
	uint32_t width;
	uint32_t height;

	struct as_buffer *buffer;
};

struct as_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;

	struct ext_session_lock_manager_v1 *lock_manager;
	struct ext_session_lock_v1 *lock;

	struct wl_callback *sync_cb;
	bool locked;
	bool unlocking;
	bool running;
	int auto_unlock_seconds;
	int64_t auto_unlock_deadline_ms;

	struct wl_list outputs;

	struct aswl_theme theme;
	struct aswl_font font;
};

static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int create_tmpfile(size_t size)
{
	int fd = -1;

#ifdef __linux__
	fd = memfd_create("aswllock", MFD_CLOEXEC);
	if (fd >= 0) {
		if (ftruncate(fd, (off_t)size) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}
#endif

	char template[] = "/tmp/aswllock-XXXXXX";
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
	if (buf == NULL)
		return;

	buf->busy = false;
	if (!buf->destroy_on_release)
		return;

	if (buf->wl_buffer != NULL)
		wl_buffer_destroy(buf->wl_buffer);
	if (buf->data != NULL && buf->size > 0)
		munmap(buf->data, buf->size);
	free(buf);
}

static const struct wl_buffer_listener wl_buf_listener = {
	.release = buffer_release,
};

static void as_buffer_dispose(struct as_buffer *buf)
{
	if (buf == NULL)
		return;
	if (buf->busy) {
		buf->destroy_on_release = true;
		return;
	}

	if (buf->wl_buffer != NULL)
		wl_buffer_destroy(buf->wl_buffer);
	if (buf->data != NULL && buf->size > 0)
		munmap(buf->data, buf->size);
	free(buf);
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
		as_buffer_dispose(buf);
		return NULL;
	}

	buf->data = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf->data == MAP_FAILED) {
		close(fd);
		as_buffer_dispose(buf);
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
		as_buffer_dispose(buf);
		return NULL;
	}

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

static double aswl_gradient_t(int type, int x, int y, int w, int h)
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

static uint32_t aswl_gradient_sample(const struct aswl_gradient *grad, double t)
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

static void as_buffer_fill_gradient(struct as_buffer *buf, const struct aswl_gradient *grad, uint32_t fallback_argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	if (!aswl_gradient_is_valid(grad)) {
		as_buffer_fill_rect(buf, 0, 0, buf->width, buf->height, fallback_argb);
		return;
	}

	for (int y = 0; y < buf->height; y++) {
		uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)y * (size_t)buf->stride);
		for (int x = 0; x < buf->width; x++) {
			double t = aswl_gradient_t(grad->type, x, y, buf->width, buf->height);
			uint32_t c = aswl_gradient_sample(grad, t) | 0xFF000000u;
			row[x] = as_premul_argb(c);
		}
	}
}

static void draw_lock_ui(struct as_state *state, struct as_output *out)
{
	if (state == NULL || out == NULL || out->buffer == NULL)
		return;

	struct as_buffer *buf = out->buffer;

	uint32_t desk_bg = state->theme.desk_bg | 0xFF000000u;
	as_buffer_fill_gradient(buf, &state->theme.desk_gradient, desk_bg);

	int scale = out->scale > 0 ? out->scale : 1;
	int border = 2 * scale;
	int pad = 12 * scale;
	int header_h = 32 * scale;

	int panel_w = 540 * scale;
	int panel_h = 140 * scale;
	if (panel_w > buf->width - 2 * pad)
		panel_w = buf->width - 2 * pad;
	if (panel_w < 200 * scale)
		panel_w = buf->width;
	if (panel_h > buf->height - 2 * pad)
		panel_h = buf->height - 2 * pad;
	if (panel_h < 80 * scale)
		panel_h = buf->height;

	int panel_x = (buf->width - panel_w) / 2;
	int panel_y = (buf->height - panel_h) / 2;

	uint32_t border_c = state->theme.menu_border | 0xFF000000u;
	uint32_t bg_c = state->theme.menu_bg | 0xFF000000u;
	uint32_t header_bg = state->theme.menu_header_bg | 0xFF000000u;
	uint32_t header_fg = state->theme.menu_header_fg | 0xFF000000u;
	uint32_t body_fg = state->theme.menu_item_fg | 0xFF000000u;

	/* Border */
	as_buffer_fill_rect(buf, panel_x, panel_y, panel_w, border, border_c);
	as_buffer_fill_rect(buf, panel_x, panel_y + panel_h - border, panel_w, border, border_c);
	as_buffer_fill_rect(buf, panel_x, panel_y, border, panel_h, border_c);
	as_buffer_fill_rect(buf, panel_x + panel_w - border, panel_y, border, panel_h, border_c);

	/* Body and header */
	as_buffer_fill_rect(buf, panel_x + border, panel_y + border, panel_w - 2 * border, panel_h - 2 * border, bg_c);
	as_buffer_fill_rect(buf, panel_x + border, panel_y + border, panel_w - 2 * border, header_h, header_bg);

	int text_x = panel_x + border + pad;
	int y_title = panel_y + border + (header_h - aswl_font_height(&state->font)) / 2;
	int body_y = panel_y + border + header_h + pad;

	aswl_font_draw_text(&state->font,
	                   (uint32_t *)buf->data,
	                   buf->width,
	                   buf->height,
	                   buf->stride / 4,
	                   text_x,
	                   y_title,
	                   "AfterStep Locked",
	                   panel_w - 2 * (border + pad),
	                   header_fg);

	const char *hint = "Press Enter to unlock (demo)";
	aswl_font_draw_text(&state->font,
	                   (uint32_t *)buf->data,
	                   buf->width,
	                   buf->height,
	                   buf->stride / 4,
	                   text_x,
	                   body_y,
	                   hint,
	                   panel_w - 2 * (border + pad),
	                   body_fg);
}

static void output_commit(struct as_output *out)
{
	if (out == NULL || out->state == NULL || out->surface == NULL || out->buffer == NULL)
		return;

	int scale = out->scale > 0 ? out->scale : 1;
	wl_surface_set_buffer_scale(out->surface, scale);

	wl_surface_attach(out->surface, out->buffer->wl_buffer, 0, 0);
	wl_surface_damage_buffer(out->surface, 0, 0, out->buffer->width, out->buffer->height);
	wl_surface_commit(out->surface);
	out->buffer->busy = true;
}

static void output_redraw(struct as_output *out)
{
	if (out == NULL || out->state == NULL)
		return;
	if (!out->configured || out->width == 0 || out->height == 0)
		return;

	int scale = out->scale > 0 ? out->scale : 1;
	int bw = (int)out->width * scale;
	int bh = (int)out->height * scale;
	if (bw <= 0)
		bw = 1;
	if (bh <= 0)
		bh = 1;

	if (out->buffer == NULL || out->buffer->width != bw || out->buffer->height != bh || out->buffer->busy) {
		struct as_buffer *next = as_buffer_create(out->state, bw, bh);
		if (next == NULL) {
			fprintf(stderr, "aswllock: failed to allocate shm buffer %dx%d\n", bw, bh);
			return;
		}
		as_buffer_dispose(out->buffer);
		out->buffer = next;
	}

	draw_lock_ui(out->state, out);
	output_commit(out);
}

static void sync_done(void *data, struct wl_callback *cb, uint32_t serial)
{
	(void)serial;
	struct as_state *state = data;
	if (cb != NULL)
		wl_callback_destroy(cb);
	if (state == NULL)
		return;
	state->sync_cb = NULL;
	state->running = false;
}

static const struct wl_callback_listener sync_listener = {
	.done = sync_done,
};

static void request_unlock(struct as_state *state)
{
	if (state == NULL || state->unlocking)
		return;
	if (state->lock == NULL)
		return;
	if (!state->locked) {
		state->running = false;
		return;
	}

	state->unlocking = true;
	ext_session_lock_v1_unlock_and_destroy(state->lock);
	state->lock = NULL;

	wl_display_flush(state->display);
	state->sync_cb = wl_display_sync(state->display);
	wl_callback_add_listener(state->sync_cb, &sync_listener, state);
}

static void lock_locked(void *data, struct ext_session_lock_v1 *lock)
{
	(void)lock;
	struct as_state *state = data;
	if (state == NULL)
		return;
	state->locked = true;
	if (state->auto_unlock_seconds > 0 && state->auto_unlock_deadline_ms == 0) {
		state->auto_unlock_deadline_ms = now_ms() + (int64_t)state->auto_unlock_seconds * 1000;
	}
	fprintf(stderr, "aswllock: locked\n");
}

static void lock_finished(void *data, struct ext_session_lock_v1 *lock)
{
	struct as_state *state = data;
	if (state == NULL)
		return;
	fprintf(stderr, "aswllock: finished\n");

	if (!state->locked) {
		ext_session_lock_v1_destroy(lock);
		state->lock = NULL;
		state->running = false;
		return;
	}

	request_unlock(state);
}

static const struct ext_session_lock_v1_listener lock_listener = {
	.locked = lock_locked,
	.finished = lock_finished,
};

static void lock_surface_configure(void *data,
                                  struct ext_session_lock_surface_v1 *lock_surface,
                                  uint32_t serial,
                                  uint32_t width,
                                  uint32_t height)
{
	struct as_output *out = data;
	if (out == NULL)
		return;

	out->configured = true;
	out->configure_serial = serial;
	out->width = width;
	out->height = height;

	ext_session_lock_surface_v1_ack_configure(lock_surface, serial);
	output_redraw(out);
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
	.configure = lock_surface_configure,
};

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
	if (state == NULL)
		return;

	if (state_w != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	if (!state->locked || state->unlocking)
		return;

	if (key == KEY_ENTER || key == KEY_ESC)
		request_unlock(state);
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
	if (state == NULL)
		return;

	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && state->keyboard == NULL) {
		state->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(state->keyboard, &keyboard_listener, state);
	} else if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0 && state->keyboard != NULL) {
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

static void output_geometry(void *data,
                            struct wl_output *wl_output,
                            int32_t x,
                            int32_t y,
                            int32_t physical_width,
                            int32_t physical_height,
                            int32_t subpixel,
                            const char *make,
                            const char *model,
                            int32_t transform)
{
	(void)data;
	(void)wl_output;
	(void)x;
	(void)y;
	(void)physical_width;
	(void)physical_height;
	(void)subpixel;
	(void)make;
	(void)model;
	(void)transform;
}

static void output_mode(void *data,
                        struct wl_output *wl_output,
                        uint32_t flags,
                        int32_t width,
                        int32_t height,
                        int32_t refresh)
{
	(void)data;
	(void)wl_output;
	(void)flags;
	(void)width;
	(void)height;
	(void)refresh;
}

static void output_done(void *data, struct wl_output *wl_output)
{
	(void)data;
	(void)wl_output;
}

static void output_scale(void *data, struct wl_output *wl_output, int32_t factor)
{
	(void)wl_output;
	struct as_output *out = data;
	if (out == NULL)
		return;
	if (factor <= 0)
		factor = 1;
	out->scale = factor;
}

static const struct wl_output_listener output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
};

static void output_destroy(struct as_output *out)
{
	if (out == NULL)
		return;
	wl_list_remove(&out->link);

	if (out->lock_surface != NULL)
		ext_session_lock_surface_v1_destroy(out->lock_surface);
	if (out->surface != NULL)
		wl_surface_destroy(out->surface);
	if (out->wl_output != NULL)
		wl_output_destroy(out->wl_output);

	as_buffer_dispose(out->buffer);
	free(out);
}

static struct as_output *output_find(struct as_state *state, uint32_t name)
{
	if (state == NULL)
		return NULL;

	struct as_output *out;
	wl_list_for_each(out, &state->outputs, link) {
		if (out != NULL && out->registry_name == name)
			return out;
	}
	return NULL;
}

static void output_ensure_lock_surface(struct as_state *state, struct as_output *out)
{
	if (state == NULL || out == NULL)
		return;
	if (state->lock == NULL || state->compositor == NULL)
		return;
	if (out->lock_surface != NULL)
		return;

	out->surface = wl_compositor_create_surface(state->compositor);
	if (out->surface == NULL)
		return;

	out->lock_surface = ext_session_lock_v1_get_lock_surface(state->lock, out->surface, out->wl_output);
	ext_session_lock_surface_v1_add_listener(out->lock_surface, &lock_surface_listener, out);
}

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
	struct as_state *state = data;
	if (state == NULL)
		return;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 4 ? version : 4);
		return;
	}
	if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		return;
	}
	if (strcmp(interface, wl_seat_interface.name) == 0) {
		state->seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 8 ? version : 8);
		wl_seat_add_listener(state->seat, &seat_listener, state);
		return;
	}
	if (strcmp(interface, wl_output_interface.name) == 0) {
		struct as_output *out = calloc(1, sizeof(*out));
		if (out == NULL)
			return;
		out->state = state;
		out->registry_name = name;
		out->scale = 1;
		out->wl_output = wl_registry_bind(registry, name, &wl_output_interface, version < 3 ? version : 3);
		wl_output_add_listener(out->wl_output, &output_listener, out);
		wl_list_insert(&state->outputs, &out->link);
		output_ensure_lock_surface(state, out);
		return;
	}
	if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
		state->lock_manager = wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1);
		return;
	}
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)registry;
	struct as_state *state = data;
	if (state == NULL)
		return;

	struct as_output *out = output_find(state, name);
	if (out != NULL)
		output_destroy(out);
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--auto-unlock SECONDS]\n", prog);
	fprintf(stderr, "  --auto-unlock SECONDS  Demo/testing: unlock automatically after SECONDS (default: off)\n");
}

int main(int argc, char **argv)
{
	long auto_unlock_seconds = 0;

	for (int i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
			usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--auto-unlock") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 2;
			}
			char *end = NULL;
			auto_unlock_seconds = strtol(argv[++i], &end, 10);
			if (end == argv[i] || (end != NULL && *end != '\0') || auto_unlock_seconds < 0 || auto_unlock_seconds > 86400) {
				fprintf(stderr, "aswllock: bad --auto-unlock value: %s\n", argv[i]);
				return 2;
			}
			continue;
		}
		fprintf(stderr, "aswllock: unknown argument: %s\n", argv[i]);
		usage(argv[0]);
		return 2;
	}

	struct as_state state = { 0 };
	wl_list_init(&state.outputs);

	aswl_theme_init_default(&state.theme);
	(void)aswl_theme_load(&state.theme);
	aswl_font_init(&state.font);
	const char *font_spec = state.theme.menu_font != NULL ? state.theme.menu_font : state.theme.frame_font;
	(void)aswl_font_load(&state.font, font_spec);

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "aswllock: wl_display_connect failed\n");
		return 1;
	}

	state.registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(state.registry, &registry_listener, &state);

	/* Get initial globals (including outputs). */
	if (wl_display_roundtrip(state.display) < 0) {
		fprintf(stderr, "aswllock: wl_display_roundtrip failed\n");
		return 1;
	}
	if (wl_display_roundtrip(state.display) < 0) {
		fprintf(stderr, "aswllock: wl_display_roundtrip(2) failed\n");
		return 1;
	}

	if (state.compositor == NULL || state.shm == NULL || state.lock_manager == NULL) {
		fprintf(stderr, "aswllock: missing globals (compositor=%p shm=%p lock_manager=%p)\n",
		        (void *)state.compositor,
		        (void *)state.shm,
		        (void *)state.lock_manager);
		return 1;
	}

	state.lock = ext_session_lock_manager_v1_lock(state.lock_manager);
	ext_session_lock_v1_add_listener(state.lock, &lock_listener, &state);

	/* Ensure lock surfaces get created for any already-seen outputs. */
	struct as_output *out;
	wl_list_for_each(out, &state.outputs, link)
		output_ensure_lock_surface(&state, out);

	/* Pull in initial configure events. */
	(void)wl_display_roundtrip(state.display);

	if (auto_unlock_seconds > 0)
		state.auto_unlock_seconds = (int)auto_unlock_seconds;

	state.running = true;
	while (state.running) {
		int timeout_ms = -1;
		if (state.locked && !state.unlocking && state.auto_unlock_deadline_ms > 0) {
			int64_t remain = state.auto_unlock_deadline_ms - now_ms();
			if (remain <= 0) {
				request_unlock(&state);
				timeout_ms = 0;
			} else if (remain > INT32_MAX) {
				timeout_ms = INT32_MAX;
			} else {
				timeout_ms = (int)remain;
			}
		}

		int fd = wl_display_get_fd(state.display);
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		wl_display_flush(state.display);
		int rc = poll(&pfd, 1, timeout_ms);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (rc == 0)
			continue;

		if ((pfd.revents & POLLIN) != 0) {
			if (wl_display_dispatch(state.display) < 0)
				break;
		} else {
			(void)wl_display_dispatch_pending(state.display);
		}
	}

	/* Cleanup. */
	if (state.sync_cb != NULL)
		wl_callback_destroy(state.sync_cb);
	if (state.lock != NULL) {
		if (state.locked)
			ext_session_lock_v1_unlock_and_destroy(state.lock);
		else
			ext_session_lock_v1_destroy(state.lock);
	}
	if (state.keyboard != NULL)
		wl_keyboard_destroy(state.keyboard);
	if (state.seat != NULL)
		wl_seat_destroy(state.seat);
	if (state.lock_manager != NULL)
		ext_session_lock_manager_v1_destroy(state.lock_manager);
	if (state.compositor != NULL)
		wl_compositor_destroy(state.compositor);
	if (state.shm != NULL)
		wl_shm_destroy(state.shm);

	struct as_output *out_tmp;
	wl_list_for_each_safe(out, out_tmp, &state.outputs, link)
		output_destroy(out);

	aswl_font_destroy(&state.font);
	aswl_theme_destroy(&state.theme);

	if (state.display != NULL)
		wl_display_disconnect(state.display);

	return 0;
}
