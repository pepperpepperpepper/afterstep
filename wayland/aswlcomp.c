#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(HAVE_WLROOTS) && HAVE_WLROOTS

#include <wayland-server-core.h>

#include <linux/input-event-codes.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include <wlr/util/edges.h>

#include <xkbcommon/xkbcommon.h>

enum aswl_cursor_mode {
	ASWL_CURSOR_PASSTHROUGH = 0,
	ASWL_CURSOR_MOVE,
	ASWL_CURSOR_RESIZE,
};

struct aswl_server;

struct aswl_view {
	struct wl_list link; /* aswl_server.views */
	struct aswl_server *server;
	struct wlr_xdg_surface *xdg_surface;
	struct wlr_scene_tree *scene_tree;
	bool mapped;
	bool placed;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;

	struct wl_listener request_move;
	struct wl_listener request_resize;
};

struct aswl_keyboard {
	struct wl_list link; /* aswl_server.keyboards */
	struct aswl_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener key;
	struct wl_listener modifiers;
	struct wl_listener destroy;
};

struct aswl_layer_surface {
	struct wl_list link; /* aswl_server.layer_surfaces */
	struct aswl_server *server;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene;

	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
};

struct aswl_output {
	struct wl_list link; /* aswl_server.outputs */
	struct aswl_server *server;
	struct wlr_output *wlr_output;
	struct wlr_box full_box;
	struct wlr_box usable_box;

	struct wl_listener destroy;
};

struct aswl_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;

	struct wlr_output_layout *output_layout;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wl_list outputs;
	struct wl_list views;
	struct aswl_view *focused_view;
	int cascade_offset;

	struct wlr_scene_tree *layer_trees[4];
	struct wlr_scene_tree *xdg_tree;
	struct wl_list layer_surfaces;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_seat *seat;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;

	struct xkb_context *xkb_context;
	struct wl_list keyboards;

	struct wl_listener new_output;
	struct wl_listener new_input;
	struct wl_listener new_xdg_surface;
	struct wl_listener new_layer_surface;

	struct wl_listener request_cursor;

	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	enum aswl_cursor_mode cursor_mode;
	struct aswl_view *grabbed_view;
	double grab_lx;
	double grab_ly;
	int grab_view_lx;
	int grab_view_ly;
	int grab_view_width;
	int grab_view_height;
	uint32_t grab_edges;
	uint32_t grab_button;
};

static void arrange_layers(struct aswl_server *server);
static void focus_topmost_view(struct aswl_server *server);
static void place_view(struct aswl_view *view);
static void begin_interactive(struct aswl_view *view, enum aswl_cursor_mode mode, uint32_t edges, uint32_t button);
static void end_interactive(struct aswl_server *server);

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--socket NAME] [--spawn CMD]\n", prog);
	fprintf(stderr, "  --socket NAME  Use a fixed WAYLAND_DISPLAY socket name\n");
	fprintf(stderr, "  --spawn CMD    Spawn a client command after startup\n");
	fprintf(stderr, "                (CMD runs via /bin/sh -c with WAYLAND_DISPLAY set)\n");
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

static struct aswl_view *view_from_wlr_surface(struct wlr_surface *surface)
{
	struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(surface);
	while (xdg_surface != NULL && xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		struct wlr_surface *parent = xdg_surface->popup->parent;
		xdg_surface = parent != NULL ? wlr_xdg_surface_try_from_wlr_surface(parent) : NULL;
	}
	if (xdg_surface == NULL || xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return NULL;
	return xdg_surface->data;
}

static void focus_view(struct aswl_view *view, struct wlr_surface *surface)
{
	if (view == NULL || view->server == NULL)
		return;

	struct aswl_server *server = view->server;

	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(server->views.prev, &view->link);

	if (server->focused_view != view) {
		if (server->focused_view != NULL && server->focused_view->xdg_surface != NULL &&
		    server->focused_view->xdg_surface->toplevel != NULL) {
			(void)wlr_xdg_toplevel_set_activated(server->focused_view->xdg_surface->toplevel, false);
		}
		server->focused_view = view;
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, true);
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard == NULL)
		return;

	if (surface == NULL)
		surface = view->xdg_surface->surface;
	if (surface == NULL)
		return;

	wlr_seat_keyboard_notify_enter(server->seat,
	                              surface,
	                              keyboard->keycodes,
	                              keyboard->num_keycodes,
	                              &keyboard->modifiers);
}

static void focus_topmost_view(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_view *view;
	wl_list_for_each_reverse(view, &server->views, link) {
		if (!view->mapped)
			continue;
		focus_view(view, NULL);
		return;
	}
}

static struct aswl_output *output_from_wlr_output(struct aswl_server *server, struct wlr_output *output)
{
	if (server == NULL || output == NULL)
		return NULL;

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out->wlr_output == output)
			return out;
	}
	return NULL;
}

static void view_get_current_size(struct aswl_view *view, int *width, int *height)
{
	int w = 0;
	int h = 0;

	if (view != NULL && view->xdg_surface != NULL && view->xdg_surface->surface != NULL) {
		w = view->xdg_surface->surface->current.width;
		h = view->xdg_surface->surface->current.height;
	}
	if (view != NULL && view->xdg_surface != NULL) {
		if (w <= 0)
			w = view->xdg_surface->geometry.width;
		if (h <= 0)
			h = view->xdg_surface->geometry.height;
	}

	if (width != NULL)
		*width = w;
	if (height != NULL)
		*height = h;
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

static void place_view(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL)
		return;
	struct aswl_server *server = view->server;

	if (view->placed || server->output_layout == NULL)
		return;

	struct wlr_output *output = NULL;
	if (server->cursor != NULL)
		output = wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
	if (output == NULL)
		output = wlr_output_layout_get_center_output(server->output_layout);

	struct wlr_box full = { 0 };
	if (output != NULL)
		wlr_output_layout_get_box(server->output_layout, output, &full);
	struct wlr_box usable = full;

	struct aswl_output *out = output_from_wlr_output(server, output);
	if (out != NULL && out->usable_box.width > 0 && out->usable_box.height > 0)
		usable = out->usable_box;

	int width = 0;
	int height = 0;
	view_get_current_size(view, &width, &height);

	int x = usable.x + 40 + server->cascade_offset;
	int y = usable.y + 40 + server->cascade_offset;
	if (width > 0 && height > 0 && usable.width > 0 && usable.height > 0) {
		int max_x = usable.x + usable.width - width;
		int max_y = usable.y + usable.height - height;
		if (x > max_x)
			x = usable.x + 40;
		if (y > max_y)
			y = usable.y + 40;
		if (x < usable.x)
			x = usable.x;
		if (y < usable.y)
			y = usable.y;
		if (x > max_x)
			x = max_x;
		if (y > max_y)
			y = max_y;
	}

	wlr_scene_node_set_position(&view->scene_tree->node, x, y);
	view->placed = true;

	server->cascade_offset += 32;
	if (server->cascade_offset >= 256)
		server->cascade_offset = 0;
}

static void begin_interactive(struct aswl_view *view, enum aswl_cursor_mode mode, uint32_t edges, uint32_t button)
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

	view_get_current_size(view, &server->grab_view_width, &server->grab_view_height);
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

static void end_interactive(struct aswl_server *server)
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

static void handle_view_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, map);
	view->mapped = true;
	wlr_scene_node_set_enabled(&view->scene_tree->node, true);
	place_view(view);
	focus_view(view, NULL);
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, unmap);
	struct aswl_server *server = view->server;

	view->mapped = false;
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	if (server != NULL && server->grabbed_view == view) {
		end_interactive(server);
	}

	if (server != NULL && server->focused_view == view) {
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, false);
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		focus_topmost_view(server);
	}
}

static void handle_view_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, destroy);
	struct aswl_server *server = view->server;

	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->link);

	if (server != NULL && server->grabbed_view == view) {
		end_interactive(server);
	}

	if (server != NULL && server->focused_view == view) {
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		focus_topmost_view(server);
	}

	free(view);
}

static void handle_request_move(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, request_move);
	struct aswl_server *server = view->server;
	struct wlr_xdg_toplevel_move_event *event = data;

	if (server == NULL || server->cursor == NULL)
		return;

	if (event != NULL && !wlr_seat_validate_pointer_grab_serial(server->seat, view->xdg_surface->surface, event->serial))
		return;

	begin_interactive(view, ASWL_CURSOR_MOVE, 0, 0);
}

static void handle_request_resize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, request_resize);
	struct aswl_server *server = view->server;
	struct wlr_xdg_toplevel_resize_event *event = data;

	if (server == NULL || server->cursor == NULL)
		return;

	if (event == NULL)
		return;
	if (!wlr_seat_validate_pointer_grab_serial(server->seat, view->xdg_surface->surface, event->serial))
		return;

	begin_interactive(view, ASWL_CURSOR_RESIZE, event->edges, 0);
}

static void handle_new_xdg_surface(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;

	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return;

	struct aswl_view *view = calloc(1, sizeof(*view));
	if (view == NULL)
		return;

	view->server = server;
	view->xdg_surface = xdg_surface;
	view->scene_tree = wlr_scene_xdg_surface_create(server->xdg_tree, xdg_surface);
	if (view->scene_tree == NULL) {
		free(view);
		return;
	}
	xdg_surface->data = view;
	wl_list_insert(server->views.prev, &view->link);

	/* Start hidden until the client maps. */
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	view->map.notify = handle_view_map;
	wl_signal_add(&xdg_surface->surface->events.map, &view->map);

	view->unmap.notify = handle_view_unmap;
	wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);

	view->destroy.notify = handle_view_destroy;
	wl_signal_add(&xdg_surface->events.destroy, &view->destroy);

	view->request_move.notify = handle_request_move;
	wl_signal_add(&xdg_surface->toplevel->events.request_move, &view->request_move);

	view->request_resize.notify = handle_request_resize;
	wl_signal_add(&xdg_surface->toplevel->events.request_resize, &view->request_resize);

	/* Initial placement (very naive for now). */
	wlr_scene_node_set_position(&view->scene_tree->node, 80, 120);
}

static void handle_keyboard_key(struct wl_listener *listener, void *data)
{
	struct aswl_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct aswl_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		uint32_t mods = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
		uint32_t keycode = event->keycode + 8;

		const xkb_keysym_t *syms = NULL;
		int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);
		if (nsyms == 1 && syms[0] == XKB_KEY_Escape && (mods & WLR_MODIFIER_ALT) != 0) {
			fprintf(stderr, "aswlcomp: Alt+Escape: exit\n");
			wl_display_terminate(server->display);
			return;
		}
	}

	wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
}

static void handle_keyboard_modifiers(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	struct aswl_server *server = keyboard->server;

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
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

static void handle_new_input(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_POINTER:
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_TABLET:
		wlr_cursor_attach_input_device(server->cursor, device);
		wlr_seat_set_capabilities(server->seat, server->seat->capabilities | WL_SEAT_CAPABILITY_POINTER);
		break;
	case WLR_INPUT_DEVICE_KEYBOARD:
	{
		struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

		if (server->xkb_context == NULL)
			server->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

		if (server->xkb_context != NULL) {
			struct xkb_keymap *keymap = xkb_keymap_new_from_names(server->xkb_context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
			if (keymap != NULL) {
				(void)wlr_keyboard_set_keymap(wlr_keyboard, keymap);
				xkb_keymap_unref(keymap);
			}
		}

		wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

		struct aswl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
		if (keyboard == NULL)
			return;

		keyboard->server = server;
		keyboard->wlr_keyboard = wlr_keyboard;

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

static void handle_request_cursor(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;

	if (server->cursor_mode != ASWL_CURSOR_PASSTHROUGH)
		return;

	if (server->seat->pointer_state.focused_client != event->seat_client)
		return;

	wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
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
		return;
	}

	if (server->cursor_mode == ASWL_CURSOR_RESIZE) {
		struct aswl_view *view = server->grabbed_view;
		if (view == NULL || view->xdg_surface == NULL || view->xdg_surface->toplevel == NULL)
			return;

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

		int min_w = view->xdg_surface->toplevel->current.min_width;
		int min_h = view->xdg_surface->toplevel->current.min_height;
		if (min_w <= 0)
			min_w = 1;
		if (min_h <= 0)
			min_h = 1;

		if (nw < min_w) {
			nw = min_w;
			if ((server->grab_edges & WLR_EDGE_LEFT) != 0)
				nx = right_edge - nw;
		}
		if (nh < min_h) {
			nh = min_h;
			if ((server->grab_edges & WLR_EDGE_TOP) != 0)
				ny = bottom_edge - nh;
		}

		if ((server->grab_edges & WLR_EDGE_LEFT) != 0 || (server->grab_edges & WLR_EDGE_TOP) != 0)
			wlr_scene_node_set_position(&view->scene_tree->node, nx, ny);
		(void)wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, nw, nh);
		return;
	}

	double sx = 0;
	double sy = 0;
	struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	if (surface == NULL) {
		wlr_seat_pointer_notify_clear_focus(server->seat);
		return;
	}

	wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;

	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_button(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	uint32_t serial = wlr_seat_pointer_notify_button(server->seat,
	                                                event->time_msec,
	                                                event->button,
	                                                event->state);

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED && server->cursor_mode != ASWL_CURSOR_PASSTHROUGH) {
		if (server->grab_button == 0 || server->grab_button == event->button)
			end_interactive(server);
		return;
	}

	if (event->state != WL_POINTER_BUTTON_STATE_PRESSED || serial == 0)
		return;

	double sx = 0;
	double sy = 0;
	struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	struct aswl_view *view = surface != NULL ? view_from_wlr_surface(surface) : NULL;
	if (view != NULL) {
		focus_view(view, surface);
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	uint32_t mods = keyboard != NULL ? wlr_keyboard_get_modifiers(keyboard) : 0;
	if ((mods & WLR_MODIFIER_ALT) == 0 || view == NULL)
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
		view_get_current_size(view, &width, &height);

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

static void handle_cursor_axis(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	wlr_seat_pointer_notify_axis(server->seat,
	                            event->time_msec,
	                            event->orientation,
	                            event->delta,
	                            event->delta_discrete,
	                            event->source,
	                            event->relative_direction);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

static struct wlr_scene_tree *layer_tree_for(struct aswl_server *server, enum zwlr_layer_shell_v1_layer layer)
{
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		return server->layer_trees[layer];
	default:
		return server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP];
	}
}

static void handle_layer_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, destroy);

	wl_list_remove(&ls->destroy.link);
	wl_list_remove(&ls->map.link);
	wl_list_remove(&ls->unmap.link);
	wl_list_remove(&ls->commit.link);
	wl_list_remove(&ls->link);
	free(ls);
}

static void handle_layer_surface_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, map);
	arrange_layers(ls->server);
}

static void handle_layer_surface_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, unmap);
	arrange_layers(ls->server);
}

static void handle_layer_surface_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, commit);
	arrange_layers(ls->server);
}

static void handle_new_layer_surface(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *layer_surface = data;

	struct aswl_layer_surface *ls = calloc(1, sizeof(*ls));
	if (ls == NULL)
		return;

	ls->server = server;
	ls->layer_surface = layer_surface;
	ls->scene = wlr_scene_layer_surface_v1_create(layer_tree_for(server, layer_surface->pending.layer), layer_surface);
	if (ls->scene == NULL) {
		free(ls);
		return;
	}

	wl_list_insert(&server->layer_surfaces, &ls->link);

	ls->destroy.notify = handle_layer_surface_destroy;
	wl_signal_add(&layer_surface->events.destroy, &ls->destroy);

	ls->map.notify = handle_layer_surface_map;
	wl_signal_add(&layer_surface->surface->events.map, &ls->map);

	ls->unmap.notify = handle_layer_surface_unmap;
	wl_signal_add(&layer_surface->surface->events.unmap, &ls->unmap);

	ls->commit.notify = handle_layer_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit, &ls->commit);

	arrange_layers(server);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_output *output = wl_container_of(listener, output, destroy);
	struct aswl_server *server = output->server;

	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);

	arrange_layers(server);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *output = data;

	struct aswl_output *as_out = calloc(1, sizeof(*as_out));
	if (as_out == NULL)
		return;
	as_out->server = server;
	as_out->wlr_output = output;
	wl_list_insert(&server->outputs, &as_out->link);

	as_out->destroy.notify = handle_output_destroy;
	wl_signal_add(&output->events.destroy, &as_out->destroy);

	if (!wlr_output_init_render(output, server->allocator, server->renderer)) {
		fprintf(stderr, "aswlcomp: wlr_output_init_render failed\n");
		return;
	}

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
	if (mode != NULL)
		wlr_output_state_set_mode(&state, mode);

	if (!wlr_output_commit_state(output, &state)) {
		fprintf(stderr, "aswlcomp: failed to commit output\n");
		wlr_output_state_finish(&state);
		return;
	}
	wlr_output_state_finish(&state);

	wlr_output_create_global(output, server->display);

	wlr_output_layout_add_auto(server->output_layout, output);
	wlr_scene_output_create(server->scene, output);

	if (server->cursor_mgr != NULL)
		(void)wlr_xcursor_manager_load(server->cursor_mgr, output->scale);
	if (server->cursor != NULL && server->cursor_mgr != NULL)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");

	arrange_layers(server);
}

static void arrange_layers_for_output(struct aswl_output *out)
{
	if (out == NULL || out->server == NULL || out->wlr_output == NULL)
		return;

	struct aswl_server *server = out->server;
	struct wlr_output *output = out->wlr_output;

	struct wlr_box full = { 0 };
	wlr_output_layout_get_box(server->output_layout, output, &full);
	struct wlr_box usable = full;

	struct wlr_output *default_output = wlr_output_layout_get_center_output(server->output_layout);
	if (default_output == NULL)
		default_output = output;

	const enum zwlr_layer_shell_v1_layer order[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
		ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
	};

	for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		enum zwlr_layer_shell_v1_layer layer = order[i];

		struct aswl_layer_surface *ls;
		wl_list_for_each(ls, &server->layer_surfaces, link) {
			struct wlr_layer_surface_v1 *surf = ls->layer_surface;
			if (surf == NULL)
				continue;

			struct wlr_output *target = surf->output;
			if (target == NULL)
				target = default_output;
			if (target != output)
				continue;

			if (surf->current.layer != layer)
				continue;

			wlr_scene_layer_surface_v1_configure(ls->scene, &full, &usable);
		}
	}

	out->full_box = full;
	out->usable_box = usable;
}

static void arrange_layers(struct aswl_server *server)
{
	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link)
		arrange_layers_for_output(out);
}

int main(int argc, char **argv)
{
	const char *socket_name = NULL;
	const char *spawn_cmd = NULL;

	for (int i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
			usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--socket") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 2;
			}
			socket_name = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--spawn") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 2;
			}
			spawn_cmd = argv[++i];
			continue;
		}

		fprintf(stderr, "aswlcomp: unknown argument: %s\n", argv[i]);
		usage(argv[0]);
		return 2;
	}

	wlr_log_init(WLR_INFO, NULL);

	struct aswl_server server = { 0 };

	server.display = wl_display_create();
	if (server.display == NULL) {
		fprintf(stderr, "aswlcomp: wl_display_create failed\n");
		return 1;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(server.display);
	server.backend = wlr_backend_autocreate(loop, NULL);
	if (server.backend == NULL) {
		fprintf(stderr, "aswlcomp: wlr_backend_autocreate failed\n");
		return 1;
	}

	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		fprintf(stderr, "aswlcomp: wlr_renderer_autocreate failed\n");
		return 1;
	}
	wlr_renderer_init_wl_display(server.renderer, server.display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (server.allocator == NULL) {
		fprintf(stderr, "aswlcomp: wlr_allocator_autocreate failed\n");
		return 1;
	}

	(void)wlr_compositor_create(server.display, 6, server.renderer);
	(void)wlr_subcompositor_create(server.display);
	(void)wlr_data_device_manager_create(server.display);

	server.output_layout = wlr_output_layout_create(server.display);
	if (server.output_layout == NULL) {
		fprintf(stderr, "aswlcomp: wlr_output_layout_create failed\n");
		return 1;
	}

	server.scene = wlr_scene_create();
	if (server.scene == NULL) {
		fprintf(stderr, "aswlcomp: wlr_scene_create failed\n");
		return 1;
	}
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	server.seat = wlr_seat_create(server.display, "seat0");
	if (server.seat == NULL) {
		fprintf(stderr, "aswlcomp: wlr_seat_create failed\n");
		return 1;
	}

	server.cursor = wlr_cursor_create();
	if (server.cursor == NULL) {
		fprintf(stderr, "aswlcomp: wlr_cursor_create failed\n");
		return 1;
	}
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	server.cursor_motion.notify = handle_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);

	server.cursor_motion_absolute.notify = handle_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);

	server.cursor_button.notify = handle_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);

	server.cursor_axis.notify = handle_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);

	server.cursor_frame.notify = handle_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	server.request_cursor.notify = handle_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);

	server.layer_shell = wlr_layer_shell_v1_create(server.display, 4);
	if (server.layer_shell == NULL) {
		fprintf(stderr, "aswlcomp: wlr_layer_shell_v1_create failed\n");
		return 1;
	}

	server.xdg_shell = wlr_xdg_shell_create(server.display, 6);
	if (server.xdg_shell == NULL) {
		fprintf(stderr, "aswlcomp: wlr_xdg_shell_create failed\n");
		return 1;
	}

	wl_list_init(&server.outputs);
	wl_list_init(&server.views);
	wl_list_init(&server.keyboards);
	wl_list_init(&server.layer_surfaces);

	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] = wlr_scene_tree_create(&server.scene->tree);
	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] = wlr_scene_tree_create(&server.scene->tree);
	server.xdg_tree = wlr_scene_tree_create(&server.scene->tree);
	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP] = wlr_scene_tree_create(&server.scene->tree);
	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] = wlr_scene_tree_create(&server.scene->tree);
	if (server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] == NULL ||
	    server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] == NULL ||
	    server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP] == NULL ||
	    server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] == NULL ||
	    server.xdg_tree == NULL) {
		fprintf(stderr, "aswlcomp: failed to create scene roots\n");
		return 1;
	}

	server.new_output.notify = handle_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.new_input.notify = handle_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);

	server.new_xdg_surface.notify = handle_new_xdg_surface;
	wl_signal_add(&server.xdg_shell->events.new_surface, &server.new_xdg_surface);

	server.new_layer_surface.notify = handle_new_layer_surface;
	wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

	const char *socket = NULL;
	if (socket_name != NULL) {
		if (wl_display_add_socket(server.display, socket_name) != 0) {
			fprintf(stderr, "aswlcomp: wl_display_add_socket(%s) failed: %s\n",
			        socket_name,
			        strerror(errno));
			return 1;
		}
		socket = socket_name;
	} else {
		socket = wl_display_add_socket_auto(server.display);
	}
	if (socket == NULL) {
		fprintf(stderr, "aswlcomp: wl_display_add_socket_auto failed\n");
		return 1;
	}

	if (!wlr_backend_start(server.backend)) {
		fprintf(stderr, "aswlcomp: wlr_backend_start failed\n");
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, 1);
	fprintf(stderr, "aswlcomp: running on WAYLAND_DISPLAY=%s\n", socket);

	if (spawn_cmd != NULL) {
		fprintf(stderr, "aswlcomp: spawn: %s\n", spawn_cmd);
		spawn_command(spawn_cmd);
	}

	wl_display_run(server.display);

	wl_display_destroy(server.display);
	return 0;
}

#else

int main(void)
{
	fprintf(stderr,
	        "aswlcomp: wlroots support not enabled.\n"
	        "Install wlroots development packages and rebuild (make -C wayland aswlcomp).\n");
	return 1;
}

#endif
