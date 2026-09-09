#define _POSIX_C_SOURCE 200809L

#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <wlr/xwayland.h>

#include "aswlcomp_internal.h"

static void handle_foreign_request_maximize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, foreign_request_maximize);
	struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
	if (view == NULL || view->server == NULL || view->foreign_toplevel == NULL || event == NULL)
		return;
	if (event->toplevel != view->foreign_toplevel)
		return;
	if (view->is_dock)
		return;

	view_set_maximized(view, event->maximized);
	aswl_schedule_flush(view->server);
}

static void handle_foreign_request_minimize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, foreign_request_minimize);
	struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
	if (view == NULL || view->server == NULL || view->foreign_toplevel == NULL || event == NULL)
		return;
	if (event->toplevel != view->foreign_toplevel)
		return;
	if (view->is_dock)
		return;

	/* The shared minimize verb (xdg and Xwayland alike): hides/shows the
	 * node, hands focus off, and exports the state. The old body only poked
	 * the X surface and left xdg views — and the screen — untouched. */
	view_set_minimized(view, event->minimized);

	view_update_toplevel_protocols(view);
	aswl_schedule_flush(view->server);
}

static void handle_foreign_request_activate(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, foreign_request_activate);
	struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;
	if (view == NULL || view->server == NULL || view->foreign_toplevel == NULL || event == NULL)
		return;
	if (event->toplevel != view->foreign_toplevel)
		return;
	if (view->is_dock)
		return;
	if (event->seat != NULL && event->seat != view->server->seat)
		return;

	focus_view(view, NULL);
	aswl_schedule_flush(view->server);
}

static void handle_foreign_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, foreign_request_fullscreen);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
	if (view == NULL || view->server == NULL || view->foreign_toplevel == NULL || event == NULL)
		return;
	if (event->toplevel != view->foreign_toplevel)
		return;
	if (view->is_dock)
		return;

	view_set_fullscreen(view, event->fullscreen);
	aswl_schedule_flush(view->server);
}

static void handle_foreign_request_close(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, foreign_request_close);
	if (view == NULL || view->server == NULL || view->foreign_toplevel == NULL)
		return;
	if (view->is_dock)
		return;

	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
		wlr_xdg_toplevel_send_close(view->xdg_surface->toplevel);
	else if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		wlr_xwayland_surface_close(view->xwayland_surface);

	aswl_schedule_flush(view->server);
}

void view_update_toplevel_protocols(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL || view->is_dock)
		return;

	const char *title = view_title(view);
	const char *app_id = view_app_id(view);

	if (view->ext_foreign_toplevel != NULL) {
		struct wlr_ext_foreign_toplevel_handle_v1_state state = {
			.title = title,
			.app_id = app_id,
		};
		wlr_ext_foreign_toplevel_handle_v1_update_state(view->ext_foreign_toplevel, &state);
	}

	if (view->foreign_toplevel != NULL) {
		wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel, title);
		wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel, app_id);
		wlr_foreign_toplevel_handle_v1_set_activated(view->foreign_toplevel, view->server->focused_view == view);
		wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, view_is_fullscreen(view));
		wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, view_is_maximized(view));
		/* The compositor-owned flag (view->minimized) is authoritative; the
		 * Xwayland surface's own field rides the same verb and agrees. */
		bool minimized = view->minimized ||
		                 (view->xwayland_surface != NULL && view->xwayland_surface->minimized);
		wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, minimized);
	}
}

void view_toplevel_protocols_create(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL)
		return;
	if (!view->mapped || view->is_dock)
		return;

	struct aswl_server *server = view->server;

	if (view->ext_foreign_toplevel == NULL && server->ext_foreign_toplevel_list != NULL) {
		struct wlr_ext_foreign_toplevel_handle_v1_state state = {
			.title = view_title(view),
			.app_id = view_app_id(view),
		};
		view->ext_foreign_toplevel = wlr_ext_foreign_toplevel_handle_v1_create(server->ext_foreign_toplevel_list, &state);
		if (view->ext_foreign_toplevel != NULL)
			view->ext_foreign_toplevel->data = view;
	}

	if (view->foreign_toplevel == NULL && server->foreign_toplevel_manager != NULL) {
		view->foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_manager);
		if (view->foreign_toplevel != NULL) {
			view->foreign_toplevel->data = view;

			view->foreign_request_maximize.notify = handle_foreign_request_maximize;
			wl_signal_add(&view->foreign_toplevel->events.request_maximize, &view->foreign_request_maximize);

			view->foreign_request_minimize.notify = handle_foreign_request_minimize;
			wl_signal_add(&view->foreign_toplevel->events.request_minimize, &view->foreign_request_minimize);

			view->foreign_request_activate.notify = handle_foreign_request_activate;
			wl_signal_add(&view->foreign_toplevel->events.request_activate, &view->foreign_request_activate);

			view->foreign_request_fullscreen.notify = handle_foreign_request_fullscreen;
			wl_signal_add(&view->foreign_toplevel->events.request_fullscreen, &view->foreign_request_fullscreen);

			view->foreign_request_close.notify = handle_foreign_request_close;
			wl_signal_add(&view->foreign_toplevel->events.request_close, &view->foreign_request_close);

			view->foreign_toplevel_listeners_added = true;

			if (server->output_layout != NULL && view->scene_tree != NULL) {
				int lx = 0;
				int ly = 0;
				(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);
				struct wlr_output *output = wlr_output_layout_output_at(server->output_layout, lx + 1, ly + 1);
				if (output != NULL)
					wlr_foreign_toplevel_handle_v1_output_enter(view->foreign_toplevel, output);
			}
		}
	}

	view_update_toplevel_protocols(view);
}

void view_toplevel_protocols_destroy(struct aswl_view *view)
{
	if (view == NULL)
		return;

	if (view->ext_foreign_toplevel != NULL) {
		wlr_ext_foreign_toplevel_handle_v1_destroy(view->ext_foreign_toplevel);
		view->ext_foreign_toplevel = NULL;
	}

	if (view->foreign_toplevel != NULL) {
		if (view->foreign_toplevel_listeners_added) {
			wl_list_remove(&view->foreign_request_maximize.link);
			wl_list_remove(&view->foreign_request_minimize.link);
			wl_list_remove(&view->foreign_request_activate.link);
			wl_list_remove(&view->foreign_request_fullscreen.link);
			wl_list_remove(&view->foreign_request_close.link);
			view->foreign_toplevel_listeners_added = false;
		}

		wlr_foreign_toplevel_handle_v1_destroy(view->foreign_toplevel);
		view->foreign_toplevel = NULL;
	}
}

