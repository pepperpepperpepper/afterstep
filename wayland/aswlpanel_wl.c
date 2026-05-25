#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlpanel_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface)
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
		fprintf(stderr, "aswlpanel: launch %s: %s\n", state->buttons[clicked].label, state->buttons[clicked].command);
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

static void control_workspace_state(void *data, struct afterstep_control_v1 *control, uint32_t current, uint32_t count)
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
		(void)as_state_load_buttons_from_file(state, state->buttons_config_path);
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

static void control_output_state(void *data, struct afterstep_control_v1 *control, uint32_t width, uint32_t height)
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

static void control_window_closed(void *data, struct afterstep_control_v1 *control, uint32_t id)
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

	uint32_t w = ((anchor_flags & ASWL_ANCHOR_LEFT) != 0 && (anchor_flags & ASWL_ANCHOR_RIGHT) != 0) ? 0u :
	                                                                                                   (uint32_t)state->width;
	uint32_t h = ((anchor_flags & ASWL_ANCHOR_TOP) != 0 && (anchor_flags & ASWL_ANCHOR_BOTTOM) != 0) ? 0u :
	                                                                                                     (uint32_t)state->height;

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

bool aswlpanel_wl_init(struct as_state *state)
{
	if (state == NULL)
		return false;

	state->display = wl_display_connect(NULL);
	if (state->display == NULL) {
		fprintf(stderr, "aswlpanel: wl_display_connect failed: %s\n", strerror(errno));
		return false;
	}

	state->registry = wl_display_get_registry(state->display);
	if (state->registry == NULL) {
		fprintf(stderr, "aswlpanel: wl_display_get_registry failed\n");
		return false;
	}

	wl_registry_add_listener(state->registry, &registry_listener, state);
	wl_display_roundtrip(state->display);

	if (state->compositor == NULL || state->shm == NULL) {
		fprintf(stderr,
		        "aswlpanel: missing required globals (compositor=%p shm=%p)\n",
		        (void *)state->compositor,
		        (void *)state->shm);
		return false;
	}

	state->surface = wl_compositor_create_surface(state->compositor);
	if (state->surface == NULL) {
		fprintf(stderr, "aswlpanel: wl_compositor_create_surface failed\n");
		return false;
	}

	bool ok = false;

#if HAVE_WLR_LAYER_SHELL
	ok = setup_layer_shell(state);
	if (!ok)
		ok = setup_xdg(state);
#else
	ok = setup_xdg(state);
#endif

	if (!ok) {
		fprintf(stderr, "aswlpanel: failed to set up a surface role (layer-shell/xdg-shell)\n");
		return false;
	}

	return true;
}

void aswlpanel_cleanup(struct as_state *state)
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

