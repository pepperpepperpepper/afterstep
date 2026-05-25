#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include <wlr/xwayland.h>

#include "aswlcomp_internal.h"

static uint64_t aswl_now_msec(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void aswl_idle_note_activity(struct aswl_server *server)
{
	if (server == NULL)
		return;

	if (server->idle_notifier != NULL && server->seat != NULL)
		wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);

	aswl_idle_lock_note_activity(server);
}

static void handle_pointer_device_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_pointer_device *pd = wl_container_of(listener, pd, destroy);
	if (pd == NULL)
		return;

	wl_list_remove(&pd->destroy.link);
	wl_list_remove(&pd->link);
	free(pd);
}

static void handle_pointer_constraint_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_pointer_constraint *pc = wl_container_of(listener, pc, destroy);
	if (pc == NULL)
		return;

	struct aswl_server *server = pc->server;
	struct wlr_pointer_constraint_v1 *constraint = pc->constraint;

	wl_list_remove(&pc->destroy.link);
	wl_list_remove(&pc->set_region.link);
	free(pc);

	if (server != NULL && server->active_pointer_constraint == constraint) {
		server->active_pointer_constraint = NULL;
		server->pointer_constraint_locked = false;
	}
}

static void handle_pointer_constraint_set_region(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_pointer_constraint *pc = wl_container_of(listener, pc, set_region);
	if (pc == NULL || pc->server == NULL)
		return;

	/* Region changes are applied on the next pointer motion. */
}

void aswl_pointer_constraints_update(struct aswl_server *server, struct wlr_surface *surface)
{
	if (server == NULL || server->pointer_constraints == NULL || server->seat == NULL)
		return;

	struct wlr_pointer_constraint_v1 *constraint = NULL;
	if (surface != NULL)
		constraint = wlr_pointer_constraints_v1_constraint_for_surface(server->pointer_constraints, surface, server->seat);

	if (constraint == server->active_pointer_constraint)
		return;

	struct wlr_pointer_constraint_v1 *old = server->active_pointer_constraint;
	server->active_pointer_constraint = NULL;
	server->pointer_constraint_locked = false;
	if (old != NULL)
		wlr_pointer_constraint_v1_send_deactivated(old);

	if (constraint != NULL) {
		server->active_pointer_constraint = constraint;
		if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED && server->cursor != NULL) {
			server->pointer_constraint_locked = true;
			server->pointer_constraint_lx = server->cursor->x;
			server->pointer_constraint_ly = server->cursor->y;
		}
		wlr_pointer_constraint_v1_send_activated(constraint);
	}
}

void handle_new_pointer_constraint(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_pointer_constraint);
	struct wlr_pointer_constraint_v1 *constraint = data;
	if (server == NULL || constraint == NULL)
		return;

	struct aswl_pointer_constraint *pc = calloc(1, sizeof(*pc));
	if (pc == NULL)
		return;

	pc->server = server;
	pc->constraint = constraint;
	constraint->data = pc;

	wl_list_init(&pc->destroy.link);
	wl_list_init(&pc->set_region.link);

	pc->destroy.notify = handle_pointer_constraint_destroy;
	wl_signal_add(&constraint->events.destroy, &pc->destroy);

	pc->set_region.notify = handle_pointer_constraint_set_region;
	wl_signal_add(&constraint->events.set_region, &pc->set_region);

	struct wlr_surface *focused = server->seat != NULL ? server->seat->pointer_state.focused_surface : NULL;
	aswl_pointer_constraints_update(server, focused);
}

static const char *xcursor_for_resize_edges(uint32_t edges)
{
	if ((edges & (WLR_EDGE_TOP | WLR_EDGE_LEFT)) == (WLR_EDGE_TOP | WLR_EDGE_LEFT))
		return "top_left_corner";
	if ((edges & (WLR_EDGE_TOP | WLR_EDGE_RIGHT)) == (WLR_EDGE_TOP | WLR_EDGE_RIGHT))
		return "top_right_corner";
	if ((edges & (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT)) == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT))
		return "bottom_left_corner";
	if ((edges & (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT)) == (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT))
		return "bottom_right_corner";
	if ((edges & WLR_EDGE_TOP) != 0)
		return "top_side";
	if ((edges & WLR_EDGE_BOTTOM) != 0)
		return "bottom_side";
	if ((edges & WLR_EDGE_LEFT) != 0)
		return "left_side";
	if ((edges & WLR_EDGE_RIGHT) != 0)
		return "right_side";
	return "left_ptr";
}

void begin_interactive(struct aswl_view *view, enum aswl_cursor_mode mode, uint32_t edges, uint32_t button)
{
	if (view == NULL || view->server == NULL)
		return;
	struct aswl_server *server = view->server;

	if (server->cursor == NULL || server->cursor_mgr == NULL)
		return;

	focus_view(view, NULL);

	server->cursor_mode = mode;
	server->grabbed_view = view;
	server->grab_lx = server->cursor->x;
	server->grab_ly = server->cursor->y;
	server->grab_edges = edges;
	server->grab_button = button;

	int lx = 0;
	int ly = 0;
	(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);
	server->grab_view_lx = lx;
	server->grab_view_ly = ly;

	view_get_frame_size(view, &server->grab_view_width, &server->grab_view_height);
	if (server->grab_view_width <= 0)
		server->grab_view_width = 1;
	if (server->grab_view_height <= 0)
		server->grab_view_height = 1;

	switch (mode) {
	case ASWL_CURSOR_MOVE:
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "grabbing");
		break;
	case ASWL_CURSOR_RESIZE:
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_resizing(view->xdg_surface->toplevel, true);
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, xcursor_for_resize_edges(edges));
		break;
	case ASWL_CURSOR_PASSTHROUGH:
	default:
		break;
	}
}

void end_interactive(struct aswl_server *server)
{
	if (server == NULL)
		return;

	if (server->cursor_mode == ASWL_CURSOR_RESIZE) {
		struct aswl_view *view = server->grabbed_view;
		if (view != NULL && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_resizing(view->xdg_surface->toplevel, false);
	}

	server->cursor_mode = ASWL_CURSOR_PASSTHROUGH;
	server->grabbed_view = NULL;
	server->grab_button = 0;
	if (server->cursor != NULL && server->cursor_mgr != NULL)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
}

static void handle_keyboard_key(struct wl_listener *listener, void *data)
{
	struct aswl_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct aswl_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	aswl_ime_maybe_set_keyboard_grab(server, keyboard->wlr_keyboard);
	aswl_idle_note_activity(server);

	if (server->session_locked) {
		wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			server->last_user_serial = wl_display_get_serial(server->display);
			server->last_user_time_msec = aswl_now_msec();
		}
		return;
	}

	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		uint32_t mods = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
		uint32_t mods_masked = mods & (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT | WLR_MODIFIER_LOGO);
		uint32_t keycode = event->keycode + 8;

		const xkb_keysym_t *syms = NULL;
		int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);
		if (nsyms == 1) {
			xkb_keysym_t sym = syms[0];

			if (sym == XKB_KEY_Escape && (mods_masked & WLR_MODIFIER_ALT) != 0) {
				fprintf(stderr, "aswlcomp: Alt+Escape: exit\n");
				wl_display_terminate(server->display);
				return;
			}

			struct aswl_binding *b;
			wl_list_for_each(b, &server->bindings, link) {
				if (b->mods == mods_masked && b->keysym == sym) {
					fprintf(stderr, "aswlcomp: bind action=%s\n", binding_action_name(b->action));
					server->last_user_serial = wl_display_next_serial(server->display);
					server->last_user_time_msec = aswl_now_msec();
					switch (b->action) {
					case ASWL_BINDING_QUIT:
						wl_display_terminate(server->display);
						break;
					case ASWL_BINDING_CLOSE_FOCUSED:
						close_focused_view(server);
						break;
					case ASWL_BINDING_FOCUS_NEXT:
						focus_next_view(server);
						break;
					case ASWL_BINDING_FOCUS_PREV:
						focus_prev_view(server);
						break;
					case ASWL_BINDING_WORKSPACE_SET:
						set_workspace(server, b->workspace);
						break;
					case ASWL_BINDING_WORKSPACE_NEXT:
						workspace_next(server);
						break;
						case ASWL_BINDING_WORKSPACE_PREV:
							workspace_prev(server);
							break;
						case ASWL_BINDING_TOGGLE_FULLSCREEN:
							if (server->grabbed_view != NULL)
								end_interactive(server);
							if (server->focused_view != NULL)
								view_set_fullscreen(server->focused_view, !view_is_fullscreen(server->focused_view));
							break;
						case ASWL_BINDING_TOGGLE_MAXIMIZED:
								if (server->grabbed_view != NULL)
									end_interactive(server);
								if (server->focused_view != NULL)
									view_set_maximized(server->focused_view, !view_is_maximized(server->focused_view));
								break;
						case ASWL_BINDING_LOCK:
							spawn_lock(server);
							break;
						case ASWL_BINDING_EXEC:
						default:
								if (b->command != NULL && b->command[0] != '\0') {
									fprintf(stderr, "aswlcomp: exec: %s\n", b->command);
									spawn_command_with_activation(server, b->command);
						}
						break;
					}
					return;
				}
			}
		}
	}

	aswl_ime_notify_key(server, event);
	wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		server->last_user_serial = wl_display_get_serial(server->display);
		server->last_user_time_msec = aswl_now_msec();
	}
}

static void handle_keyboard_modifiers(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	struct aswl_server *server = keyboard->server;

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	aswl_ime_maybe_set_keyboard_grab(server, keyboard->wlr_keyboard);
	aswl_idle_note_activity(server);
	aswl_ime_notify_modifiers(server, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(server->seat, &keyboard->wlr_keyboard->modifiers);
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);

	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

void handle_new_virtual_keyboard(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *vk = data;
	if (server == NULL || server->seat == NULL || vk == NULL)
		return;

	if (getenv("ASWLCOMP_DISABLE_KEYBOARD") != NULL) {
		fprintf(stderr, "aswlcomp: ignoring virtual keyboard (ASWLCOMP_DISABLE_KEYBOARD)\n");
		return;
	}

	struct wlr_keyboard *wlr_keyboard = &vk->keyboard;

	struct aswl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (keyboard == NULL)
		return;

	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;
	keyboard->has_keymap = vk->has_keymap;

	aswl_apply_keyboard_device_config(server, keyboard);

	keyboard->key.notify = handle_keyboard_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

	keyboard->modifiers.notify = handle_keyboard_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

	keyboard->destroy.notify = handle_keyboard_destroy;
	wl_signal_add(&wlr_keyboard->base.events.destroy, &keyboard->destroy);

	wl_list_insert(&server->keyboards, &keyboard->link);

	wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	wlr_seat_set_capabilities(server->seat, server->seat->capabilities | WL_SEAT_CAPABILITY_KEYBOARD);
	aswl_ime_maybe_set_keyboard_grab(server, wlr_keyboard);
}

void handle_new_input(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_POINTER:
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_TABLET:
		wlr_cursor_attach_input_device(server->cursor, device);
		wlr_seat_set_capabilities(server->seat, server->seat->capabilities | WL_SEAT_CAPABILITY_POINTER);

		struct aswl_pointer_device *pd = calloc(1, sizeof(*pd));
		if (pd != NULL) {
			pd->server = server;
			pd->device = device;
			wl_list_insert(&server->pointer_devices, &pd->link);

			pd->destroy.notify = handle_pointer_device_destroy;
			wl_signal_add(&device->events.destroy, &pd->destroy);
		}

		aswl_apply_pointer_device_config(server, device);
		break;
	case WLR_INPUT_DEVICE_KEYBOARD:
	{
		if (getenv("ASWLCOMP_DISABLE_KEYBOARD") != NULL) {
			fprintf(stderr, "aswlcomp: ignoring keyboard device (ASWLCOMP_DISABLE_KEYBOARD)\n");
			break;
		}

		struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

		struct aswl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
		if (keyboard == NULL)
			return;

		keyboard->server = server;
		keyboard->wlr_keyboard = wlr_keyboard;
		keyboard->has_keymap = false;

		aswl_apply_keyboard_device_config(server, keyboard);

		keyboard->key.notify = handle_keyboard_key;
		wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

		keyboard->modifiers.notify = handle_keyboard_modifiers;
		wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

		keyboard->destroy.notify = handle_keyboard_destroy;
		wl_signal_add(&device->events.destroy, &keyboard->destroy);

		wl_list_insert(&server->keyboards, &keyboard->link);

		wlr_seat_set_keyboard(server->seat, wlr_keyboard);
		wlr_seat_set_capabilities(server->seat, server->seat->capabilities | WL_SEAT_CAPABILITY_KEYBOARD);
		break;
	}
	default:
		break;
	}
}

void handle_request_cursor(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;

	if (server->cursor_mode != ASWL_CURSOR_PASSTHROUGH)
		return;

	if (server->seat->pointer_state.focused_client != event->seat_client)
		return;

	wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

void handle_request_set_selection(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	if (server == NULL || server->seat == NULL || event == NULL)
		return;

	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void handle_request_set_primary_selection(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	if (server == NULL || server->seat == NULL || event == NULL)
		return;

	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

static struct wlr_surface *surface_at(struct aswl_server *server, double lx, double ly, double *sx, double *sy)
{
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER)
		return NULL;

	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	if (scene_buffer == NULL)
		return NULL;

	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (scene_surface == NULL)
		return NULL;

	return scene_surface->surface;
}

static void process_cursor_motion(struct aswl_server *server, uint32_t time_msec)
{
	if (server->cursor_mode == ASWL_CURSOR_MOVE) {
		struct aswl_view *view = server->grabbed_view;
		if (view == NULL)
			return;

		double dx = server->cursor->x - server->grab_lx;
		double dy = server->cursor->y - server->grab_ly;

		int nx = server->grab_view_lx + (int)dx;
		int ny = server->grab_view_ly + (int)dy;
		wlr_scene_node_set_position(&view->scene_tree->node, nx, ny);
		if (view->xwayland_surface != NULL) {
			int w = server->grab_view_width;
			int h = server->grab_view_height;
			if (w <= 0)
				w = view->xwayland_surface->width;
			if (h <= 0)
				h = view->xwayland_surface->height;
			if (w > 0 && h > 0) {
				int border = 0;
				int title_h = 0;
				view_get_deco_metrics(view, &border, &title_h);
				int ox = border;
				int oy = title_h;
				int cw = w - 2 * border;
				int ch = h - title_h - border;
				if (cw < 1)
					cw = 1;
				if (ch < 1)
					ch = 1;
				wlr_xwayland_surface_configure(view->xwayland_surface,
				                               nx + ox,
				                               ny + oy,
				                               (uint16_t)cw,
				                               (uint16_t)ch);
			}
		}
		return;
	}

	if (server->cursor_mode == ASWL_CURSOR_RESIZE) {
		struct aswl_view *view = server->grabbed_view;
		if (view == NULL)
			return;

		int border = 0;
		int title_h = 0;
		view_get_deco_metrics(view, &border, &title_h);

		double dx = server->cursor->x - server->grab_lx;
		double dy = server->cursor->y - server->grab_ly;

		int dx_i = (int)dx;
		int dy_i = (int)dy;

		int right_edge = server->grab_view_lx + server->grab_view_width;
		int bottom_edge = server->grab_view_ly + server->grab_view_height;

		int nx = server->grab_view_lx;
		int ny = server->grab_view_ly;
		int nw = server->grab_view_width;
		int nh = server->grab_view_height;

		if ((server->grab_edges & WLR_EDGE_LEFT) != 0) {
			nx = server->grab_view_lx + dx_i;
			nw = server->grab_view_width - dx_i;
		} else if ((server->grab_edges & WLR_EDGE_RIGHT) != 0) {
			nw = server->grab_view_width + dx_i;
		}

		if ((server->grab_edges & WLR_EDGE_TOP) != 0) {
			ny = server->grab_view_ly + dy_i;
			nh = server->grab_view_height - dy_i;
		} else if ((server->grab_edges & WLR_EDGE_BOTTOM) != 0) {
			nh = server->grab_view_height + dy_i;
		}

		int min_w = 1;
		int min_h = 1;
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
			min_w = view->xdg_surface->toplevel->current.min_width;
			min_h = view->xdg_surface->toplevel->current.min_height;
			if (min_w <= 0)
				min_w = 1;
			if (min_h <= 0)
				min_h = 1;
		}

		int min_fw = min_w + 2 * border;
		int min_fh = min_h + title_h + border;

		if (nw < min_fw) {
			nw = min_fw;
			if ((server->grab_edges & WLR_EDGE_LEFT) != 0)
				nx = right_edge - nw;
		}
		if (nh < min_fh) {
			nh = min_fh;
			if ((server->grab_edges & WLR_EDGE_TOP) != 0)
				ny = bottom_edge - nh;
		}

		int cw = nw - 2 * border;
		int ch = nh - title_h - border;
		if (cw < 1)
			cw = 1;
		if (ch < 1)
			ch = 1;

		if ((server->grab_edges & WLR_EDGE_LEFT) != 0 || (server->grab_edges & WLR_EDGE_TOP) != 0)
			wlr_scene_node_set_position(&view->scene_tree->node, nx, ny);
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
			(void)wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, cw, ch);
		} else if (view->xwayland_surface != NULL) {
			wlr_xwayland_surface_configure(view->xwayland_surface,
			                               nx + border,
			                               ny + title_h,
			                               (uint16_t)cw,
			                               (uint16_t)ch);
		}
		return;
	}

	double sx = 0;
	double sy = 0;
	struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	if (surface == NULL) {
		wlr_seat_pointer_notify_clear_focus(server->seat);
		aswl_pointer_constraints_update(server, NULL);
		return;
	}

	wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
	aswl_pointer_constraints_update(server, surface);
	wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
}

static void aswl_confine_point_to_region(const pixman_region32_t *region,
                                        int width,
                                        int height,
                                        double *sx,
                                        double *sy)
{
	if (sx == NULL || sy == NULL)
		return;

	int px = (int)*sx;
	int py = (int)*sy;

	if (width > 0)
		px = clamp_int(px, 0, width - 1);
	if (height > 0)
		py = clamp_int(py, 0, height - 1);

	/* An empty region means the whole surface. */
	if (region == NULL || !pixman_region32_not_empty((pixman_region32_t *)region)) {
		*sx = (double)px;
		*sy = (double)py;
		return;
	}

	if (pixman_region32_contains_point((pixman_region32_t *)region, px, py, NULL)) {
		*sx = (double)px;
		*sy = (double)py;
		return;
	}

	int nrects = 0;
	pixman_box32_t *rects = pixman_region32_rectangles((pixman_region32_t *)region, &nrects);
	if (rects == NULL || nrects <= 0) {
		*sx = (double)px;
		*sy = (double)py;
		return;
	}

	int best_x = px;
	int best_y = py;
	int64_t best_d2 = INT64_MAX;

	for (int i = 0; i < nrects; i++) {
		int x1 = rects[i].x1;
		int y1 = rects[i].y1;
		int x2 = rects[i].x2 - 1;
		int y2 = rects[i].y2 - 1;
		if (x2 < x1 || y2 < y1)
			continue;

		int cx = clamp_int(px, x1, x2);
		int cy = clamp_int(py, y1, y2);
		int64_t dx = (int64_t)px - (int64_t)cx;
		int64_t dy = (int64_t)py - (int64_t)cy;
		int64_t d2 = dx * dx + dy * dy;
		if (d2 < best_d2) {
			best_d2 = d2;
			best_x = cx;
			best_y = cy;
		}
	}

	*sx = (double)best_x;
	*sy = (double)best_y;
}

static void aswl_relative_pointer_send_motion(struct aswl_server *server,
                                             uint32_t time_msec,
                                             double dx,
                                             double dy,
                                             double dx_unaccel,
                                             double dy_unaccel)
{
	if (server == NULL || server->relative_pointer_manager == NULL || server->seat == NULL)
		return;

	uint64_t time_usec = (uint64_t)time_msec * 1000u;
	wlr_relative_pointer_manager_v1_send_relative_motion(server->relative_pointer_manager,
	                                                     server->seat,
	                                                     time_usec,
	                                                     dx,
	                                                     dy,
	                                                     dx_unaccel,
	                                                     dy_unaccel);
}

static void aswl_cursor_apply_pointer_constraints_delta(struct aswl_server *server,
                                                       struct wlr_input_device *device,
                                                       double dx,
                                                       double dy)
{
	if (server == NULL || server->cursor == NULL || device == NULL)
		return;

	struct wlr_pointer_constraint_v1 *constraint = server->active_pointer_constraint;
	if (constraint == NULL || constraint->surface == NULL ||
	    server->session_locked || server->cursor_mode != ASWL_CURSOR_PASSTHROUGH) {
		wlr_cursor_move(server->cursor, device, dx, dy);
		return;
	}

	if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED && server->pointer_constraint_locked) {
		wlr_cursor_warp_closest(server->cursor, device, server->pointer_constraint_lx, server->pointer_constraint_ly);
		server->pointer_constraint_lx = server->cursor->x;
		server->pointer_constraint_ly = server->cursor->y;
		return;
	}

	if (constraint->type != WLR_POINTER_CONSTRAINT_V1_CONFINED) {
		wlr_cursor_move(server->cursor, device, dx, dy);
		return;
	}

	double lx = server->cursor->x;
	double ly = server->cursor->y;

	double sx = 0.0;
	double sy = 0.0;
	struct wlr_surface *surface = surface_at(server, lx, ly, &sx, &sy);
	if (surface == NULL || surface != constraint->surface) {
		wlr_cursor_move(server->cursor, device, dx, dy);
		return;
	}

	double new_sx = sx + dx;
	double new_sy = sy + dy;
	aswl_confine_point_to_region(&constraint->region, surface->current.width, surface->current.height, &new_sx, &new_sy);

	double origin_x = lx - sx;
	double origin_y = ly - sy;

	wlr_cursor_warp_closest(server->cursor, device, origin_x + new_sx, origin_y + new_sy);
}

void handle_cursor_motion(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	aswl_idle_note_activity(server);
	aswl_relative_pointer_send_motion(server,
	                                 event->time_msec,
	                                 event->delta_x,
	                                 event->delta_y,
	                                 event->unaccel_dx,
	                                 event->unaccel_dy);
	aswl_cursor_apply_pointer_constraints_delta(server, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void handle_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;

	aswl_idle_note_activity(server);
	double old_lx = server->cursor->x;
	double old_ly = server->cursor->y;

	double target_lx = old_lx;
	double target_ly = old_ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->pointer->base, event->x, event->y, &target_lx, &target_ly);

	double dx = target_lx - old_lx;
	double dy = target_ly - old_ly;
	aswl_relative_pointer_send_motion(server, event->time_msec, dx, dy, dx, dy);
	aswl_cursor_apply_pointer_constraints_delta(server, &event->pointer->base, dx, dy);
	process_cursor_motion(server, event->time_msec);
}

void handle_cursor_button(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	aswl_idle_note_activity(server);
	uint32_t serial = wlr_seat_pointer_notify_button(server->seat,
	                                                event->time_msec,
	                                                event->button,
	                                                event->state);
	if (serial != 0 && event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		server->last_user_serial = serial;
		server->last_user_time_msec = aswl_now_msec();
	}

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED && server->cursor_mode != ASWL_CURSOR_PASSTHROUGH) {
		if (server->grab_button == 0 || server->grab_button == event->button)
			end_interactive(server);
		return;
	}

	if (event->state != WL_POINTER_BUTTON_STATE_PRESSED)
		return;

	double sx = 0.0;
	double sy = 0.0;
	struct wlr_surface *surface = NULL;
	struct aswl_view *view = NULL;

	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, server->cursor->x, server->cursor->y, &sx, &sy);
	if (node != NULL && node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
		struct wlr_scene_surface *scene_surface = scene_buffer != NULL ? wlr_scene_surface_try_from_buffer(scene_buffer) : NULL;
		if (scene_surface != NULL) {
			surface = scene_surface->surface;
			view = view_from_wlr_surface(surface);
		}
	}

	for (struct wlr_scene_node *n = node; view == NULL && n != NULL; n = n->parent != NULL ? &n->parent->node : NULL) {
		if (n->data != NULL)
			view = n->data;
	}

	if (view != NULL) {
		if (view->scene_tree != NULL)
			wlr_scene_node_raise_to_top(&view->scene_tree->node);
		if (!view->is_dock)
			focus_view(view, surface);
	}

	if (view != NULL && view->deco_titlebar != NULL && node == &view->deco_titlebar->node &&
	    event->button == BTN_LEFT && !view->is_dock) {
		int lx = (int)sx;
		int ly = (int)sy;
		if (lx >= view->deco_close_x && lx < view->deco_close_x + view->deco_close_w &&
		    ly >= view->deco_close_y && ly < view->deco_close_y + view->deco_close_h) {
			if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
				wlr_xdg_toplevel_send_close(view->xdg_surface->toplevel);
			} else if (view->xwayland_surface != NULL) {
				wlr_xwayland_surface_close(view->xwayland_surface);
			}
			return;
		}
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	uint32_t mods = keyboard != NULL ? wlr_keyboard_get_modifiers(keyboard) : 0;
	if ((mods & WLR_MODIFIER_ALT) == 0 || view == NULL || view->is_dock)
		return;

	if (event->button == BTN_LEFT) {
		begin_interactive(view, ASWL_CURSOR_MOVE, 0, event->button);
		return;
	}

	if (event->button == BTN_RIGHT) {
		int vx = 0;
		int vy = 0;
		(void)wlr_scene_node_coords(&view->scene_tree->node, &vx, &vy);

		int width = 0;
		int height = 0;
		view_get_frame_size(view, &width, &height);

		uint32_t edges = 0;
		if (width > 0) {
			int local_x = (int)server->cursor->x - vx;
			edges |= local_x < width / 2 ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
		} else {
			edges |= WLR_EDGE_RIGHT;
		}

		if (height > 0) {
			int local_y = (int)server->cursor->y - vy;
			edges |= local_y < height / 2 ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
		} else {
			edges |= WLR_EDGE_BOTTOM;
		}

		begin_interactive(view, ASWL_CURSOR_RESIZE, edges, event->button);
		return;
	}
}

void handle_cursor_axis(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	aswl_idle_note_activity(server);
	wlr_seat_pointer_notify_axis(server->seat,
	                            event->time_msec,
	                            event->orientation,
	                            event->delta,
	                            event->delta_discrete,
	                            event->source,
	                            event->relative_direction);
}

void handle_cursor_frame(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

