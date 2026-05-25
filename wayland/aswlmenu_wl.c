#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlmenu_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

static void as_state_destroy_buffers(struct as_state *state)
{
	for (size_t i = 0; i < sizeof(state->buffers) / sizeof(state->buffers[0]); i++) {
		as_buffer_destroy(state->buffers[i]);
		state->buffers[i] = NULL;
	}
}

bool as_state_ensure_buffers(struct as_state *state)
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

struct as_buffer *as_state_acquire_buffer(struct as_state *state)
{
	for (size_t i = 0; i < sizeof(state->buffers) / sizeof(state->buffers[0]); i++) {
		struct as_buffer *buf = state->buffers[i];
		if (buf != NULL && !buf->busy)
			return buf;
	}
	return NULL;
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

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
	struct as_state *state = data;

	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0) {
		if (state->pointer == NULL) {
			state->pointer = wl_seat_get_pointer(seat);
			if (state->pointer != NULL)
				aswlmenu_input_attach_pointer(state->pointer, state);
		}
	} else if (state->pointer != NULL) {
		wl_pointer_destroy(state->pointer);
		state->pointer = NULL;
	}

	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0) {
		if (state->keyboard == NULL) {
			state->keyboard = wl_seat_get_keyboard(seat);
			if (state->keyboard != NULL)
				aswlmenu_input_attach_keyboard(state->keyboard, state);
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

void aswlmenu_update_window_list_title(struct as_state *state)
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
	aswlmenu_update_window_list_title(state);
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
	if (app_id != NULL && strncmp(app_id, "afterstep.aswl", 13) == 0)
		return;
	if (app_id != NULL && strcmp(app_id, "ASModule") == 0)
		return;

	/* Skip helper surfaces with no useful label. */
	if ((title == NULL || title[0] == '\0') && (app_id == NULL || app_id[0] == '\0'))
		return;

	const char *label = NULL;
	if (title != NULL && title[0] != '\0')
		label = title;
	else if (app_id != NULL && app_id[0] != '\0')
		label = app_id;

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

bool aswlmenu_wl_connect(struct as_state *state)
{
	if (state == NULL)
		return false;

	state->display = wl_display_connect(NULL);
	if (state->display == NULL) {
		fprintf(stderr, "aswlmenu: wl_display_connect failed: %s\n", strerror(errno));
		return false;
	}

	state->registry = wl_display_get_registry(state->display);
	if (state->registry == NULL) {
		fprintf(stderr, "aswlmenu: wl_display_get_registry failed\n");
		return false;
	}

	wl_registry_add_listener(state->registry, &registry_listener, state);
	wl_display_roundtrip(state->display);

	if (state->compositor == NULL || state->shm == NULL) {
		fprintf(stderr, "aswlmenu: missing required globals (compositor=%p shm=%p)\n",
		        (void *)state->compositor,
		        (void *)state->shm);
		return false;
	}

	return true;
}

bool aswlmenu_wl_setup_surface(struct as_state *state)
{
	if (state == NULL)
		return false;

	state->surface = wl_compositor_create_surface(state->compositor);
	if (state->surface == NULL) {
		fprintf(stderr, "aswlmenu: wl_compositor_create_surface failed\n");
		return false;
	}

	if (!setup_xdg(state)) {
		fprintf(stderr, "aswlmenu: failed to set up xdg-shell surface\n");
		return false;
	}

	/* Ensure listeners see initial seat/keymap events. */
	wl_display_roundtrip(state->display);
	return true;
}

void aswlmenu_cleanup(struct as_state *state)
{
	if (state == NULL)
		return;

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
	free(state->close_icon_argb);
	state->close_icon_argb = NULL;
	state->close_icon_w = 0;
	state->close_icon_h = 0;
	state->close_icon_tried = false;
	free(state->close_icon_pressed_argb);
	state->close_icon_pressed_argb = NULL;
	state->close_icon_pressed_w = 0;
	state->close_icon_pressed_h = 0;
	state->close_icon_pressed_tried = false;
	free(state->iconize_icon_argb);
	state->iconize_icon_argb = NULL;
	state->iconize_icon_w = 0;
	state->iconize_icon_h = 0;
	state->iconize_icon_tried = false;
	free(state->iconize_icon_pressed_argb);
	state->iconize_icon_pressed_argb = NULL;
	state->iconize_icon_pressed_w = 0;
	state->iconize_icon_pressed_h = 0;
	state->iconize_icon_pressed_tried = false;
	free(state->pin_icon_argb);
	state->pin_icon_argb = NULL;
	state->pin_icon_w = 0;
	state->pin_icon_h = 0;
	state->pin_icon_tried = false;
	free(state->pin_icon_pressed_argb);
	state->pin_icon_pressed_argb = NULL;
	state->pin_icon_pressed_w = 0;
	state->pin_icon_pressed_h = 0;
	state->pin_icon_pressed_tried = false;
	aswl_font_destroy(&state->hilite_font);
	aswl_font_destroy(&state->header_font);
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
