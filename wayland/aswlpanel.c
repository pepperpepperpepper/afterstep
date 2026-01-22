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

static void as_state_free_buttons(struct as_state *state)
{
	if (!state->buttons_owned)
		return;

	for (size_t i = 0; i < state->button_count; i++) {
		free(state->buttons[i].label);
		free(state->buttons[i].command);
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

		while (*label == ' ' || *label == '\t')
			label++;
		while (*command == ' ' || *command == '\t')
			command++;

		for (char *end = label + strlen(label); end > label && (end[-1] == ' ' || end[-1] == '\t'); end--)
			end[-1] = '\0';
		for (char *end = command + strlen(command); end > command && (end[-1] == ' ' || end[-1] == '\t'); end--)
			end[-1] = '\0';

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

		buttons[count].label = strdup(label);
		buttons[count].command = strdup(command);
		if (buttons[count].label == NULL || buttons[count].command == NULL) {
			free(buttons[count].label);
			free(buttons[count].command);
			goto fail;
		}
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
		free(buttons[i].label);
		free(buttons[i].command);
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

static int as_state_hit_test(struct as_state *state, int x, int y)
{
	if (state->button_count == 0)
		return -1;

	int pad = 6;
	int spacing = 6;
	int btn = state->height - 2 * pad;
	if (btn <= 0)
		return -1;

	int rx = pad;
	int ry = pad;

	for (size_t i = 0; i < state->button_count; i++) {
		int rw = btn;
		int rh = btn;

		if (x >= rx && x < rx + rw && y >= ry && y < ry + rh)
			return (int)i;

		rx += rw + spacing;
	}

	return -1;
}

static void as_state_draw(struct as_state *state, struct as_buffer *buf)
{
	as_buffer_paint_solid(buf, 0xFF202020u);

	int pad = 6;
	int spacing = 6;
	int btn = state->height - 2 * pad;
	if (btn <= 0)
		return;

	int rx = pad;
	int ry = pad;

	for (size_t i = 0; i < state->button_count; i++) {
		int idx = (int)i;
		uint32_t color = 0xFF3A3A3Au;
		if (idx == state->pressed_index)
			color = 0xFF6A6A6Au;
		else if (idx == state->hover_index)
			color = 0xFF505050u;

		as_buffer_fill_rect(buf, rx, ry, btn, btn, color);

		/* Cheap border. */
		as_buffer_fill_rect(buf, rx, ry, btn, 1, 0xFF101010u);
		as_buffer_fill_rect(buf, rx, ry + btn - 1, btn, 1, 0xFF101010u);
		as_buffer_fill_rect(buf, rx, ry, 1, btn, 0xFF101010u);
		as_buffer_fill_rect(buf, rx + btn - 1, ry, 1, btn, 0xFF101010u);

		rx += btn + spacing;
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
