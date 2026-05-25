#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <xcb/xcb.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include <wlr/xwayland.h>

#include "aswlcomp_internal.h"

static void xwayland_detach_surface(struct aswl_view *view);

static bool str_ieq(const char *a, const char *b)
{
	if (a == NULL || b == NULL)
		return false;
	while (*a != '\0' && *b != '\0') {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return false;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static uint16_t clamp_u16(int v, uint16_t fallback)
{
	if (v <= 0)
		return fallback;
	if (v > (int)UINT16_MAX)
		return UINT16_MAX;
	return (uint16_t)v;
}

static int16_t clamp_i16(int v)
{
	if (v < (int)INT16_MIN)
		return INT16_MIN;
	if (v > (int)INT16_MAX)
		return INT16_MAX;
	return (int16_t)v;
}

static void xwayland_try_apply_wm_normal_hints(struct aswl_server *server,
                                              struct wlr_xwayland_surface *xsurface,
                                              int *x,
                                              int *y,
                                              int *w,
                                              int *h)
{
	if (server == NULL || xsurface == NULL)
		return;
	if (server->xwayland == NULL)
		return;

	xcb_connection_t *c = wlr_xwayland_get_xwm_connection(server->xwayland);
	if (c == NULL)
		return;

	xcb_generic_error_t *err = NULL;
	xcb_size_hints_t sh = { 0 };
	if (xcb_icccm_get_wm_normal_hints_reply(c, xcb_icccm_get_wm_normal_hints(c, xsurface->window_id), &sh, &err) == 0) {
		free(err);
		return;
	}
	free(err);

	if (x != NULL && y != NULL && *x == 0 && *y == 0 &&
	    ((sh.flags & XCB_ICCCM_SIZE_HINT_US_POSITION) != 0 || (sh.flags & XCB_ICCCM_SIZE_HINT_P_POSITION) != 0)) {
		*x = sh.x;
		*y = sh.y;
	}

	if (w != NULL && h != NULL && (*w <= 1 || *h <= 1) &&
	    ((sh.flags & XCB_ICCCM_SIZE_HINT_US_SIZE) != 0 || (sh.flags & XCB_ICCCM_SIZE_HINT_P_SIZE) != 0)) {
		if (sh.width > 1 && sh.width <= 4096)
			*w = sh.width;
		if (sh.height > 1 && sh.height <= 4096)
			*h = sh.height;
	}

	if (w != NULL && h != NULL && (*w <= 1 || *h <= 1) && (sh.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) != 0) {
		if (sh.base_width > 1 && sh.base_width <= 4096)
			*w = sh.base_width;
		if (sh.base_height > 1 && sh.base_height <= 4096)
			*h = sh.base_height;
	}

	if (w != NULL && h != NULL && (*w <= 1 || *h <= 1) && (sh.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) != 0) {
		if (sh.min_width > 1 && sh.min_width <= 4096)
			*w = sh.min_width;
		if (sh.min_height > 1 && sh.min_height <= 4096)
			*h = sh.min_height;
	}

	if (getenv("ASWLCOMP_DEBUG_XWAYLAND_HINTS") != NULL) {
		fprintf(stderr,
		        "aswlcomp: xwayland WM_NORMAL_HINTS: title=%s class=%s flags=0x%x pos=%d,%d size=%d,%d min=%d,%d base=%d,%d\n",
		        xsurface->title != NULL ? xsurface->title : "",
		        xsurface->class != NULL ? xsurface->class : "",
		        sh.flags,
		        (int)sh.x,
		        (int)sh.y,
		        (int)sh.width,
		        (int)sh.height,
		        (int)sh.min_width,
		        (int)sh.min_height,
		        (int)sh.base_width,
		        (int)sh.base_height);
	}
}

static void xwayland_force_window_bg(struct aswl_server *server,
                                     struct wlr_xwayland_surface *xsurface,
                                     int width,
                                     int height,
                                     uint16_t r16,
                                     uint16_t g16,
                                     uint16_t b16)
{
	if (server == NULL || server->xwayland == NULL || xsurface == NULL)
		return;

	xcb_connection_t *c = wlr_xwayland_get_xwm_connection(server->xwayland);
	if (c == NULL)
		return;

	/*
	 * Some classic X11 demo clients (notably xeyes) can appear with an
	 * unexpected black background under Xwayland in our nested harness.
	 *
	 * Force a known background pixel and clear so the first rendered frame is
	 * visually consistent with the X11 baseline screenshots.
	 */
	uint32_t pixel = 0x00ffffffu;
	const xcb_setup_t *setup = xcb_get_setup(c);
	xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
	xcb_screen_t *screen = it.data;
	if (screen != NULL) {
		xcb_alloc_color_cookie_t cc = xcb_alloc_color(c, screen->default_colormap, r16, g16, b16);
		xcb_alloc_color_reply_t *cr = xcb_alloc_color_reply(c, cc, NULL);
		if (cr != NULL) {
			pixel = cr->pixel;
			free(cr);
		}
	}

	xcb_rectangle_t rect = { 0, 0, clamp_u16(width, 1), clamp_u16(height, 1) };

	/*
	 * xeyes uses a child window for its drawing widget; filling only the
	 * toplevel may have no visible effect. Paint the toplevel and any direct
	 * children so the rendered buffer has a stable background.
	 */
	xcb_window_t targets[32];
	size_t target_count = 0;
	targets[target_count++] = xsurface->window_id;

	xcb_query_tree_cookie_t tc = xcb_query_tree(c, xsurface->window_id);
	xcb_query_tree_reply_t *tr = xcb_query_tree_reply(c, tc, NULL);
	if (tr != NULL) {
		int n = xcb_query_tree_children_length(tr);
		xcb_window_t *children = xcb_query_tree_children(tr);
		for (int i = 0; i < n && target_count < (sizeof(targets) / sizeof(targets[0])); i++)
			targets[target_count++] = children[i];
		free(tr);
	}

	for (size_t i = 0; i < target_count; i++) {
		xcb_window_t win = targets[i];

		uint32_t values[] = { pixel };
		(void)xcb_change_window_attributes(c, win, XCB_CW_BACK_PIXEL, values);
		(void)xcb_clear_area(c, 0, win, 0, 0, 0, 0);

		if (width > 0 && height > 0) {
			xcb_gcontext_t gc = xcb_generate_id(c);
			uint32_t gc_values[] = { pixel };
			(void)xcb_create_gc(c, gc, win, XCB_GC_FOREGROUND, gc_values);
			(void)xcb_poly_fill_rectangle(c, win, gc, 1, &rect);
			(void)xcb_free_gc(c, gc);
		}
	}

	xcb_flush(c);
}

static void xwayland_maybe_force_xeyes_bg(struct aswl_server *server,
                                          struct wlr_xwayland_surface *xsurface,
                                          int width,
                                          int height)
{
	if (server == NULL || xsurface == NULL)
		return;

	/* WM_CLASS (class) is exposed as xsurface->class; for xeyes it is "XEyes". */
	if (xsurface->class != NULL && strcmp(xsurface->class, "XEyes") == 0) {
		if (getenv("ASWLCOMP_DEBUG_XEYES_BG") != NULL) {
			fprintf(stderr, "aswlcomp: forcing xeyes background (w=%d h=%d)\n", width, height);
		}
		xwayland_force_window_bg(server, xsurface, width, height, 0xffff, 0xffff, 0xffff);
	}
}

static void handle_view_scene_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, scene_destroy);
	if (view == NULL)
		return;

	wl_list_remove(&view->scene_destroy.link);
	view->scene_destroy_listener_added = false;

	if (view->deco_titlebar_buf != NULL) {
		wlr_buffer_drop(view->deco_titlebar_buf);
		view->deco_titlebar_buf = NULL;
	}

	view->scene_tree = NULL;
	view->content_tree = NULL;
	view->surface_tree = NULL;
	view->deco_left = NULL;
	view->deco_right = NULL;
	view->deco_bottom = NULL;
	view->deco_titlebar = NULL;
	view->mapped = false;
}

static void handle_view_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, surface_destroy);
	if (view == NULL)
		return;

	wl_list_remove(&view->surface_destroy.link);
	view->surface_destroy_listener_added = false;

	/*
	 * If a wlr_surface is being destroyed before our higher-level destroy
	 * signals fire, avoid touching the surface's wl_signal lists later.
	 */
	if (view->type == ASWL_VIEW_XWAYLAND) {
		xwayland_detach_surface(view);
		return;
	}

	view->mapped = false;
	if (view->scene_tree != NULL)
		wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	if (view->surface_listeners_added) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		view->surface_listeners_added = false;
	}
	if (view->commit_listener_added) {
		wl_list_remove(&view->commit.link);
		view->commit_listener_added = false;
	}

	struct aswl_server *server = view->server;
	if (server != NULL && server->grabbed_view == view)
		end_interactive(server);

	if (server != NULL && server->focused_view == view) {
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		focus_topmost_view(server);
	}

	if (server != NULL)
		broadcast_window_state(server, view);

	/* Suite popups (root-menu style) should not become the "active" view. If the
	 * menu goes away, restore keyboard focus to the active view if any. */
	if (server != NULL && view_is_suite_popup(view)) {
		if (server->focused_view != NULL)
			focus_view(server->focused_view, NULL);
		else {
			wlr_seat_keyboard_notify_clear_focus(server->seat);
			aswl_ime_set_focus(server, NULL);
		}
	}
}

static void handle_view_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, commit);
	if (view == NULL || view->server == NULL || view->xdg_surface == NULL || view->xdg_surface->toplevel == NULL)
		return;

	/*
	 * Wayland clients typically wait for the compositor to send an initial
	 * xdg_toplevel configure (after an initial, buffer-less commit) before
	 * they commit a buffer and become mapped. Without this, native clients
	 * remain present but unmapped.
	 */
	if (!view->xdg_surface->initialized)
		return;

	if (!view->xdg_surface->configured) {
		(void)wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, 0, 0);
		aswl_schedule_flush(view->server);
	}

	view_update_decorations(view);
}

static void handle_view_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, map);
	struct aswl_server *server = view->server;

	view->mapped = true;
	if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
		/* Paint after map so any widget child windows exist (e.g. xeyes). */
		xwayland_maybe_force_xeyes_bg(server,
		                              view->xwayland_surface,
		                              (int)view->xwayland_surface->width,
		                              (int)view->xwayland_surface->height);
	}
	view_maybe_mark_dock(view);
	view_update_decorations(view);
	view_toplevel_protocols_create(view);

	bool enabled = server != NULL && (view->is_dock || view->workspace == server->current_workspace);
	wlr_scene_node_set_enabled(&view->scene_tree->node, enabled);

	if (enabled) {
		if (view->is_dock) {
			arrange_dock_views(server);
		} else {
			place_view(view);
			/*
			 * AfterStep does not aggressively focus-steal. For visual parity
			 * (and to avoid focus flicker), do not focus newly-mapped views
			 * by default. Only in-suite popups (e.g. aswlmenu) take keyboard
			 * focus automatically.
			 *
			 * Still raise newly-mapped views so they remain visible.
			 */
			bool focus_on_map = false;
			const char *focus_env = getenv("ASWLCOMP_FOCUS_ON_MAP");
			if (focus_env != NULL && focus_env[0] != '\0' && strcmp(focus_env, "0") != 0)
				focus_on_map = true;

			bool should_focus = view_is_suite_popup(view);
			if (focus_on_map) {
				should_focus = !view_is_asmodule(view) &&
				               (server == NULL || server->focused_view == NULL || view_is_suite_popup(view));
			}
			if (should_focus) {
				focus_view(view, NULL);
			} else {
				if (view->scene_tree != NULL) {
					wlr_scene_node_raise_to_top(&view->scene_tree->node);
					wl_list_remove(&view->link);
					wl_list_insert(server->views.prev, &view->link);
				}
				broadcast_window_state(server, view);
			}
		}
	} else if (server != NULL) {
		broadcast_window_state(server, view);
	}
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, unmap);
	struct aswl_server *server = view->server;

	view->mapped = false;
	view_toplevel_protocols_destroy(view);
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	if (server != NULL && server->grabbed_view == view) {
		end_interactive(server);
	}

	if (server != NULL && server->focused_view == view) {
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, false);
		else if (view->xwayland_surface != NULL)
			wlr_xwayland_surface_activate(view->xwayland_surface, false);
		server->focused_view = NULL;
		view_update_decorations(view);
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		focus_topmost_view(server);
	}

	if (server != NULL)
		broadcast_window_state(server, view);

	/* If a suite popup held keyboard focus, restore focus to the active view. */
	if (server != NULL && view_is_suite_popup(view)) {
		if (server->focused_view != NULL)
			focus_view(server->focused_view, NULL);
		else {
			wlr_seat_keyboard_notify_clear_focus(server->seat);
			aswl_ime_set_focus(server, NULL);
		}
	}

	if (server != NULL && view->is_dock)
		arrange_dock_views(server);
}

static void handle_view_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, destroy);
	struct aswl_server *server = view->server;
	uint32_t id = view->id;
	bool was_focused = (server != NULL && server->focused_view == view);

	view_toplevel_protocols_destroy(view);

	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL)
		view->xdg_surface->data = NULL;
	if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		view->xwayland_surface->data = NULL;

	/* Ensure we won't re-focus a view while it's being torn down. */
	view->mapped = false;
	if (view->scene_tree != NULL)
		wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	if (view->surface_listeners_added) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		view->surface_listeners_added = false;
	}
	if (view->commit_listener_added) {
		wl_list_remove(&view->commit.link);
		view->commit_listener_added = false;
	}
	if (view->surface_destroy_listener_added) {
		wl_list_remove(&view->surface_destroy.link);
		view->surface_destroy_listener_added = false;
	}
	if (server != NULL)
		broadcast_window_closed(server, id);

	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_minimize.link);
	if (view->type == ASWL_VIEW_XDG) {
		wl_list_remove(&view->set_title.link);
		wl_list_remove(&view->set_app_id.link);
	} else if (view->type == ASWL_VIEW_XWAYLAND) {
		wl_list_remove(&view->set_title.link);
		wl_list_remove(&view->set_class.link);
	}
	if (view->type == ASWL_VIEW_XWAYLAND) {
		wl_list_remove(&view->xwayland_associate.link);
		wl_list_remove(&view->xwayland_dissociate.link);
		wl_list_remove(&view->xwayland_map_request.link);
		wl_list_remove(&view->xwayland_request_configure.link);
	}
	wl_list_remove(&view->link);

	if (server != NULL && server->grabbed_view == view) {
		end_interactive(server);
	}

	if (server != NULL && was_focused) {
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		focus_topmost_view(server);
	}

	if (server != NULL && view->is_dock)
		arrange_dock_views(server);

	if (view->deco_titlebar_buf != NULL) {
		wlr_buffer_drop(view->deco_titlebar_buf);
		view->deco_titlebar_buf = NULL;
	}

	if (view->scene_tree != NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
	}

	free(view);
}

static void handle_view_set_title(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, set_title);
	view_update_decorations(view);
	view_update_toplevel_protocols(view);
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
	if (view->server != NULL && view->is_dock && view->mapped)
		arrange_dock_views(view->server);
}

static void handle_view_set_app_id(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, set_app_id);
	view_update_toplevel_protocols(view);
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
	if (view->server != NULL && view->is_dock && view->mapped)
		arrange_dock_views(view->server);
}

static void handle_view_set_class(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, set_class);
	if (view != NULL && view->server != NULL && view->mapped &&
	    view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
		xwayland_maybe_force_xeyes_bg(view->server,
		                              view->xwayland_surface,
		                              (int)view->xwayland_surface->width,
		                              (int)view->xwayland_surface->height);
	}
	view_update_toplevel_protocols(view);
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
	if (view->server != NULL && view->is_dock && view->mapped)
		arrange_dock_views(view->server);
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

static void handle_request_fullscreen(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_fullscreen);
	if (view == NULL || view->xdg_surface == NULL || view->xdg_surface->toplevel == NULL)
		return;

	bool requested = view->xdg_surface->toplevel->requested.fullscreen;
	fprintf(stderr, "aswlcomp: xdg request_fullscreen=%d\n", requested ? 1 : 0);
	view_set_fullscreen(view, requested);

	aswl_schedule_flush(view->server);
}

static void handle_request_maximize(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_maximize);
	if (view == NULL || view->xdg_surface == NULL || view->xdg_surface->toplevel == NULL)
		return;

	bool requested = view->xdg_surface->toplevel->requested.maximized;
	fprintf(stderr, "aswlcomp: xdg request_maximize=%d\n", requested ? 1 : 0);
	view_set_maximized(view, requested);

	aswl_schedule_flush(view->server);
}

static void handle_request_minimize(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_minimize);
	if (view == NULL || view->xdg_surface == NULL || view->xdg_surface->toplevel == NULL)
		return;

	/*
	 * xdg-shell doesn't have a minimized state in configure, but we still need
	 * to send a configure event to acknowledge set_minimized requests.
	 */
	fprintf(stderr, "aswlcomp: xdg request_minimize\n");
	(void)wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, view->xdg_surface->toplevel->current.activated);

	aswl_schedule_flush(view->server);
}

static void xwayland_attach_surface(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL || view->xwayland_surface == NULL)
		return;
	struct wlr_xwayland_surface *xsurface = view->xwayland_surface;

	if (xsurface->surface == NULL)
		return;

	if (view->scene_tree != NULL)
		return;

	if (!view_create_frame_scene(view))
		return;

	view->surface_tree = wlr_scene_subsurface_tree_create(view->content_tree, xsurface->surface);
	if (view->surface_tree == NULL)
		return;

	view->scene_destroy.notify = handle_view_scene_destroy;
	wl_signal_add(&view->scene_tree->node.events.destroy, &view->scene_destroy);
	view->scene_destroy_listener_added = true;

	view_update_decorations(view);

	int ox = 0;
	int oy = 0;
	view_get_content_offset(view, &ox, &oy);
	wlr_scene_node_set_position(&view->scene_tree->node, xsurface->x - ox, xsurface->y - oy);

	/* Start hidden until the client maps. */
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	view->map.notify = handle_view_map;
	wl_signal_add(&xsurface->surface->events.map, &view->map);

	view->unmap.notify = handle_view_unmap;
	wl_signal_add(&xsurface->surface->events.unmap, &view->unmap);
	view->surface_listeners_added = true;

	/* wlr_surface may be destroyed before xwayland_surface destroy; guard listener cleanup. */
	view->surface_destroy.notify = handle_view_surface_destroy;
	wl_signal_add(&xsurface->surface->events.destroy, &view->surface_destroy);
	view->surface_destroy_listener_added = true;

	/*
	 * Some Xwayland clients can fully map (including committing a buffer) before
	 * we manage to attach our surface listeners. In that case we'd miss the
	 * wlr_surface map event and the view would remain "unmapped" forever.
	 */
	if (!view->mapped && xsurface->surface->mapped)
		handle_view_map(&view->map, NULL);
}

static void xwayland_detach_surface(struct aswl_view *view)
{
	if (view == NULL)
		return;

	if (view->surface_destroy_listener_added) {
		wl_list_remove(&view->surface_destroy.link);
		view->surface_destroy_listener_added = false;
	}

	if (view->surface_listeners_added) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		view->surface_listeners_added = false;
	}

	if (view->scene_tree != NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
	}

	view->mapped = false;
	view_toplevel_protocols_destroy(view);
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
}

static void handle_xwayland_associate(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, xwayland_associate);
	xwayland_attach_surface(view);
}

static void handle_xwayland_dissociate(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, xwayland_dissociate);
	struct aswl_server *server = view->server;

	if (server != NULL && server->grabbed_view == view)
		end_interactive(server);

	if (server != NULL && server->focused_view == view) {
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		focus_topmost_view(server);
	}

	xwayland_detach_surface(view);
}

static void handle_xwayland_request_configure(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, xwayland_request_configure);
	struct wlr_xwayland_surface_configure_event *event = data;

	if (view == NULL || view->xwayland_surface == NULL || event == NULL)
		return;
	const bool have_scene = (view->scene_tree != NULL);

	if (view->is_dock) {
		if (view->server != NULL)
			arrange_dock_views(view->server);
		return;
	}

	view_update_decorations(view);

	int ox = 0;
	int oy = 0;
	view_get_content_offset(view, &ox, &oy);

	/*
	 * X11 ConfigureWindow requests may omit some fields; wlroots exposes a mask
	 * so we can distinguish "not requested" from zero values. In particular,
	 * some clients send move-only configures and wlroots may leave width/height
	 * as 0 unless explicitly requested, so blindly applying 0×0 would collapse
	 * the view to just its titlebar.
	 */
	int lx = 0;
	int ly = 0;
	if (have_scene)
		(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);

	int cur_x = have_scene ? (lx + ox) : (int)view->xwayland_surface->x;
	int cur_y = have_scene ? (ly + oy) : (int)view->xwayland_surface->y;

	int x = cur_x;
	int y = cur_y;
		/*
		 * X11 clients (especially legacy AfterStep modules like WinTabs) may map
		 * before issuing their first real ConfigureWindow request with x/y. If we
		 * eagerly "place" them during map, we'd ignore those x/y requests forever
		 * and the module windows would land in the wrong spot.
		 *
		 * Accept x/y from ConfigureWindow requests unless we're currently in an
		 * interactive move/resize for this view.
		 */
		if (view->server == NULL || view->server->grabbed_view != view) {
			if ((event->mask & XCB_CONFIG_WINDOW_X) != 0)
				x = event->x;
			if ((event->mask & XCB_CONFIG_WINDOW_Y) != 0)
				y = event->y;
		}

	int w = (int)view->xwayland_surface->width;
	int h = (int)view->xwayland_surface->height;
	if ((event->mask & XCB_CONFIG_WINDOW_WIDTH) != 0)
		w = (int)event->width;
	if ((event->mask & XCB_CONFIG_WINDOW_HEIGHT) != 0)
		h = (int)event->height;
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;

	/*
	 * Some Xwayland clients (notably xterm) may map before a meaningful size
	 * propagates, leaving width/height at 0 and causing us to shrink them to
	 * 1×1. Avoid making newly created windows invisible; they can still request
	 * a different size later.
	 */
	if (!view->xwayland_surface->override_redirect && (w <= 1 || h <= 1)) {
		xwayland_try_apply_wm_normal_hints(view->server, view->xwayland_surface, NULL, NULL, &w, &h);
		if (w <= 1)
			w = 640;
		if (h <= 1)
			h = 400;
	}

	if ((x != cur_x || y != cur_y) && have_scene) {
		wlr_scene_node_set_position(&view->scene_tree->node, x - ox, y - oy);
		view->placed = true;
	}

	wlr_xwayland_surface_configure(view->xwayland_surface,
	                               clamp_i16(x),
	                               clamp_i16(y),
	                               (uint16_t)w,
	                               (uint16_t)h);
	view_update_decorations(view);
}

static void handle_xwayland_map_request(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, xwayland_map_request);
	struct wlr_xwayland_surface *xsurface = view != NULL ? view->xwayland_surface : NULL;

	if (view == NULL || xsurface == NULL)
		return;

	int x = (int)xsurface->x;
	int y = (int)xsurface->y;
	int w = (int)xsurface->width;
	int h = (int)xsurface->height;

	/*
	 * Some X11 clients (notably AfterStep modules like WinTabs) create their
	 * toplevel as 1×1 and rely on the window manager to configure an initial
	 * size before they render/commit. Without this, the wl_surface never maps.
	 */
	if (w <= 1 || h <= 1)
		xwayland_try_apply_wm_normal_hints(view->server, xsurface, &x, &y, &w, &h);

	if ((w <= 1 || h <= 1) && xsurface->size_hints != NULL) {
		if (getenv("ASWLCOMP_DEBUG_XWAYLAND_HINTS") != NULL) {
			const xcb_size_hints_t *sh = xsurface->size_hints;
			fprintf(stderr,
			        "aswlcomp: xwayland map_request hints: title=%s class=%s flags=0x%x "
			        "pos=%d,%d size=%d,%d min=%d,%d base=%d,%d\n",
			        xsurface->title != NULL ? xsurface->title : "",
			        xsurface->class != NULL ? xsurface->class : "",
			        sh->flags,
			        (int)sh->x,
			        (int)sh->y,
			        (int)sh->width,
			        (int)sh->height,
			        (int)sh->min_width,
			        (int)sh->min_height,
			        (int)sh->base_width,
			        (int)sh->base_height);
		}
		int hw = xsurface->size_hints->width;
		int hh = xsurface->size_hints->height;
		if (hw > 1 && hw <= 4096)
			w = hw;
		if (hh > 1 && hh <= 4096)
			h = hh;
	}
	if ((w <= 1 || h <= 1) && getenv("ASWLCOMP_DEBUG_XWAYLAND_HINTS") != NULL) {
		fprintf(stderr,
		        "aswlcomp: xwayland map_request no usable size yet: title=%s class=%s "
		        "x=%d y=%d w=%d h=%d\n",
		        xsurface->title != NULL ? xsurface->title : "",
		        xsurface->class != NULL ? xsurface->class : "",
		        x,
		        y,
		        w,
		        h);
	}

	if (w <= 1 || h <= 1) {
		int fallback_w = 640;
		int fallback_h = 400;
		if (xsurface->class != NULL && str_ieq(xsurface->class, "ASModule")) {
			fallback_w = 320;
			fallback_h = 80;
		}
		if (w <= 1)
			w = fallback_w;
		if (h <= 1)
			h = fallback_h;
	}

	wlr_xwayland_surface_configure(xsurface,
	                               clamp_i16(x),
	                               clamp_i16(y),
	                               clamp_u16(w, 320),
	                               clamp_u16(h, 80));

	if (view->scene_tree != NULL) {
		int ox = 0;
		int oy = 0;
		view_get_content_offset(view, &ox, &oy);
		wlr_scene_node_set_position(&view->scene_tree->node, x - ox, y - oy);
	}

	if (!view->placed && (x != 0 || y != 0))
		view->placed = true;
}

static void handle_xwayland_request_move(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_move);
	if (view->is_dock)
		return;
	if (view->server == NULL || view->server->cursor == NULL)
		return;
	begin_interactive(view, ASWL_CURSOR_MOVE, 0, 0);
}

static void handle_xwayland_request_resize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xwayland_resize_event *event = data;
	if (view->is_dock)
		return;
	if (view->server == NULL || view->server->cursor == NULL || event == NULL)
		return;
	begin_interactive(view, ASWL_CURSOR_RESIZE, event->edges, 0);
}

static void handle_xwayland_request_fullscreen(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_fullscreen);
	if (view == NULL || view->xwayland_surface == NULL)
		return;
	if (view->is_dock)
		return;

	bool requested = view->xwayland_surface->fullscreen;
	fprintf(stderr, "aswlcomp: xwayland request_fullscreen=%d\n", requested ? 1 : 0);
	view_set_fullscreen(view, requested);

	aswl_schedule_flush(view->server);
}

static void handle_xwayland_request_maximize(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_maximize);
	if (view == NULL || view->xwayland_surface == NULL)
		return;
	if (view->is_dock)
		return;

	bool requested = view->xwayland_surface->maximized_horz || view->xwayland_surface->maximized_vert;
	fprintf(stderr, "aswlcomp: xwayland request_maximize=%d\n", requested ? 1 : 0);
	view_set_maximized(view, requested);

	aswl_schedule_flush(view->server);
}

static void handle_xwayland_request_minimize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, request_minimize);
	struct wlr_xwayland_minimize_event *event = data;
	if (view == NULL || view->xwayland_surface == NULL || event == NULL)
		return;
	if (event->surface != view->xwayland_surface)
		return;
	if (view->server == NULL)
		return;

	fprintf(stderr, "aswlcomp: xwayland request_minimize=%d\n", event->minimize ? 1 : 0);
	wlr_xwayland_surface_set_minimized(view->xwayland_surface, event->minimize);

	if (view->scene_tree != NULL) {
		bool enabled = view->mapped && !event->minimize &&
		               (view->is_dock || view->workspace == view->server->current_workspace);
		wlr_scene_node_set_enabled(&view->scene_tree->node, enabled);
	}

	if (event->minimize && view->server->focused_view == view) {
		view->server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(view->server->seat);
		aswl_ime_set_focus(view->server, NULL);
		focus_topmost_view(view->server);
	}

	view_update_toplevel_protocols(view);
	broadcast_window_state(view->server, view);
	aswl_schedule_flush(view->server);
}

void handle_new_xwayland_surface(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *xsurface = data;

	if (xsurface == NULL)
		return;

	struct aswl_view *view = calloc(1, sizeof(*view));
	if (view == NULL)
		return;

	view->server = server;
	view->type = ASWL_VIEW_XWAYLAND;
	view->xwayland_surface = xsurface;
	view->workspace = server->current_workspace;
	view->id = server->next_view_id++;
	if (server->next_view_id == 0)
		server->next_view_id = 1;
	xsurface->data = view;

	wl_list_insert(server->views.prev, &view->link);

	view->destroy.notify = handle_view_destroy;
	wl_signal_add(&xsurface->events.destroy, &view->destroy);

	view->set_title.notify = handle_view_set_title;
	wl_signal_add(&xsurface->events.set_title, &view->set_title);

	view->set_class.notify = handle_view_set_class;
	wl_signal_add(&xsurface->events.set_class, &view->set_class);

	view->xwayland_associate.notify = handle_xwayland_associate;
	wl_signal_add(&xsurface->events.associate, &view->xwayland_associate);

	view->xwayland_dissociate.notify = handle_xwayland_dissociate;
	wl_signal_add(&xsurface->events.dissociate, &view->xwayland_dissociate);

	view->xwayland_map_request.notify = handle_xwayland_map_request;
	wl_signal_add(&xsurface->events.map_request, &view->xwayland_map_request);

	view->xwayland_request_configure.notify = handle_xwayland_request_configure;
	wl_signal_add(&xsurface->events.request_configure, &view->xwayland_request_configure);

	view->request_move.notify = handle_xwayland_request_move;
	wl_signal_add(&xsurface->events.request_move, &view->request_move);

	view->request_resize.notify = handle_xwayland_request_resize;
	wl_signal_add(&xsurface->events.request_resize, &view->request_resize);

	view->request_fullscreen.notify = handle_xwayland_request_fullscreen;
	wl_signal_add(&xsurface->events.request_fullscreen, &view->request_fullscreen);

	view->request_maximize.notify = handle_xwayland_request_maximize;
	wl_signal_add(&xsurface->events.request_maximize, &view->request_maximize);

	view->request_minimize.notify = handle_xwayland_request_minimize;
	wl_signal_add(&xsurface->events.request_minimize, &view->request_minimize);

	/* Xwayland may already have an associated wlr_surface. */
	xwayland_attach_surface(view);
}

void handle_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *toplevel = data;
	struct wlr_xdg_surface *xdg_surface = toplevel != NULL ? toplevel->base : NULL;

	if (xdg_surface == NULL || xdg_surface->toplevel == NULL)
		return;

	struct aswl_view *view = calloc(1, sizeof(*view));
	if (view == NULL)
		return;

	view->server = server;
	view->type = ASWL_VIEW_XDG;
	view->xdg_surface = xdg_surface;
	view->workspace = server->current_workspace;
	view->id = server->next_view_id++;
	if (server->next_view_id == 0)
		server->next_view_id = 1;
	if (!view_create_frame_scene(view)) {
		free(view);
		return;
	}

	view->surface_tree = wlr_scene_xdg_surface_create(view->content_tree, xdg_surface);
	if (view->surface_tree == NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		free(view);
		return;
	}

	view->scene_destroy.notify = handle_view_scene_destroy;
	wl_signal_add(&view->scene_tree->node.events.destroy, &view->scene_destroy);
	view->scene_destroy_listener_added = true;

	xdg_surface->data = view;
	wl_list_insert(server->views.prev, &view->link);

	/* Start hidden until the client maps. */
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	view->map.notify = handle_view_map;
	wl_signal_add(&xdg_surface->surface->events.map, &view->map);

	view->unmap.notify = handle_view_unmap;
	wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
	view->surface_listeners_added = true;

	view->commit.notify = handle_view_commit;
	wl_signal_add(&xdg_surface->surface->events.commit, &view->commit);
	view->commit_listener_added = true;

	view->surface_destroy.notify = handle_view_surface_destroy;
	wl_signal_add(&xdg_surface->surface->events.destroy, &view->surface_destroy);
	view->surface_destroy_listener_added = true;

	view->destroy.notify = handle_view_destroy;
	/* Must listen to toplevel destroy to remove toplevel event listeners before wlroots frees it. */
	wl_signal_add(&xdg_surface->toplevel->events.destroy, &view->destroy);

	view->set_title.notify = handle_view_set_title;
	wl_signal_add(&xdg_surface->toplevel->events.set_title, &view->set_title);

	view->set_app_id.notify = handle_view_set_app_id;
	wl_signal_add(&xdg_surface->toplevel->events.set_app_id, &view->set_app_id);

	view->request_move.notify = handle_request_move;
	wl_signal_add(&xdg_surface->toplevel->events.request_move, &view->request_move);

	view->request_resize.notify = handle_request_resize;
	wl_signal_add(&xdg_surface->toplevel->events.request_resize, &view->request_resize);

	view->request_fullscreen.notify = handle_request_fullscreen;
	wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen, &view->request_fullscreen);

	view->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&xdg_surface->toplevel->events.request_maximize, &view->request_maximize);

	view->request_minimize.notify = handle_request_minimize;
	wl_signal_add(&xdg_surface->toplevel->events.request_minimize, &view->request_minimize);

	/* Initial placement (very naive for now). */
	wlr_scene_node_set_position(&view->scene_tree->node, 80, 120);
	view_update_decorations(view);
}
