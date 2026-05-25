#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <wlr/xwayland.h>

#include "afterstep-control-v1-protocol.h"
#include "aswlcomp_internal.h"

void broadcast_workspace_state(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_control_client *cc;
	struct aswl_control_client *tmp;
	wl_list_for_each_safe(cc, tmp, &server->control_clients, link) {
		if (cc->resource == NULL)
			continue;
		if (wl_resource_get_version(cc->resource) < 3)
			continue;
		afterstep_control_v1_send_workspace_state(cc->resource, server->current_workspace, server->workspace_count);
	}
}

static struct aswl_output *server_primary_output(struct aswl_server *server)
{
	if (server == NULL)
		return NULL;
	if (wl_list_empty(&server->outputs))
		return NULL;

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out == NULL || out->wlr_output == NULL)
			continue;
		if (!out->wlr_output->enabled)
			continue;
		if (server->output_layout != NULL) {
			struct wlr_box box = { 0 };
			wlr_output_layout_get_box(server->output_layout, out->wlr_output, &box);
			if (box.width <= 0 || box.height <= 0)
				continue;
		}
		return out;
	}

	return wl_container_of(server->outputs.next, out, link);
}

static void send_output_state(struct aswl_server *server, struct wl_resource *resource)
{
	if (server == NULL || resource == NULL)
		return;
	if (wl_resource_get_version(resource) < 6)
		return;

	struct aswl_output *out = server_primary_output(server);
	uint32_t width = 0;
	uint32_t height = 0;
	if (out != NULL) {
		if (out->full_box.width > 0)
			width = (uint32_t)out->full_box.width;
		if (out->full_box.height > 0)
			height = (uint32_t)out->full_box.height;
		if ((width == 0 || height == 0) && out->wlr_output != NULL) {
			if (out->wlr_output->width > 0)
				width = (uint32_t)out->wlr_output->width;
			if (out->wlr_output->height > 0)
				height = (uint32_t)out->wlr_output->height;
		}
	}

	afterstep_control_v1_send_output_state(resource, width, height);
}

void broadcast_output_state(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_control_client *cc;
	struct aswl_control_client *tmp;
	wl_list_for_each_safe(cc, tmp, &server->control_clients, link) {
		if (cc->resource == NULL)
			continue;
		send_output_state(server, cc->resource);
	}
}

static uint32_t view_window_flags(struct aswl_server *server, struct aswl_view *view)
{
	uint32_t flags = 0;
	if (view == NULL)
		return flags;

	if (view->mapped)
		flags |= ASWL_WINDOW_FLAG_MAPPED;
	if (server != NULL && server->focused_view == view)
		flags |= ASWL_WINDOW_FLAG_FOCUSED;
	if (view->type == ASWL_VIEW_XWAYLAND)
		flags |= ASWL_WINDOW_FLAG_XWAYLAND;

	return flags;
}

static bool view_should_skip_window_list(struct aswl_view *view)
{
	if (view == NULL)
		return true;
	if (view->is_dock)
		return true;

	/* Skip Xwayland internal/placeholder surfaces that carry no identifying info. */
	if (view->type == ASWL_VIEW_XWAYLAND) {
		const char *app_id = view_app_id(view);
		const char *title = view_title(view);
		if ((app_id == NULL || app_id[0] == '\0') && (title == NULL || title[0] == '\0'))
			return true;
	}

	return false;
}

static void send_window_state(struct aswl_server *server, struct wl_resource *resource, struct aswl_view *view)
{
	if (server == NULL || resource == NULL || view == NULL)
		return;
	if (wl_resource_get_version(resource) < 4)
		return;
	if (view->id == 0)
		return;
	if (view_should_skip_window_list(view))
		return;

	uint32_t flags = view_window_flags(server, view);
	afterstep_control_v1_send_window(resource, view->id, view->workspace, flags, view_title(view), view_app_id(view));
}

static void send_window_geometry(struct aswl_server *server, struct wl_resource *resource, struct aswl_view *view)
{
	if (server == NULL || resource == NULL || view == NULL)
		return;
	if (wl_resource_get_version(resource) < 6)
		return;
	if (view->id == 0)
		return;
	if (view_should_skip_window_list(view))
		return;
	if (view->scene_tree == NULL)
		return;

	int lx = 0;
	int ly = 0;
	(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);

	int w = 0;
	int h = 0;
	view_get_frame_size(view, &w, &h);
	if (w < 0)
		w = 0;
	if (h < 0)
		h = 0;

	afterstep_control_v1_send_window_geometry(resource, view->id, lx, ly, w, h);
}

static void send_window_list_snapshot(struct aswl_server *server, struct wl_resource *resource)
{
	if (server == NULL || resource == NULL)
		return;
	if (wl_resource_get_version(resource) < 4)
		return;

	afterstep_control_v1_send_window_list_begin(resource);
	struct aswl_view *view;
	struct aswl_view *tmp;
	wl_list_for_each_safe(view, tmp, &server->views, link) {
		send_window_state(server, resource, view);
		send_window_geometry(server, resource, view);
	}
	afterstep_control_v1_send_window_list_end(resource);
}

void broadcast_window_state(struct aswl_server *server, struct aswl_view *view)
{
	if (server == NULL || view == NULL)
		return;

	struct aswl_control_client *cc;
	struct aswl_control_client *tmp;
	wl_list_for_each_safe(cc, tmp, &server->control_clients, link) {
		if (cc->resource == NULL)
			continue;
		send_window_state(server, cc->resource, view);
		send_window_geometry(server, cc->resource, view);
	}
}

void broadcast_window_closed(struct aswl_server *server, uint32_t id)
{
	if (server == NULL || id == 0)
		return;

	struct aswl_control_client *cc;
	struct aswl_control_client *tmp;
	wl_list_for_each_safe(cc, tmp, &server->control_clients, link) {
		if (cc->resource == NULL)
			continue;
		if (wl_resource_get_version(cc->resource) < 4)
			continue;
		afterstep_control_v1_send_window_closed(cc->resource, id);
	}
}

static struct aswl_view *find_view_by_id(struct aswl_server *server, uint32_t id)
{
	if (server == NULL || id == 0)
		return NULL;

	struct aswl_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->id == id)
			return view;
	}
	return NULL;
}

static void aswl_control_resource_destroy(struct wl_resource *resource)
{
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	if (cc == NULL)
		return;
	wl_list_remove(&cc->link);
	free(cc);
}

static void aswl_control_destroy(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static void aswl_control_exec(struct wl_client *client, struct wl_resource *resource, const char *command)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;

	if (command == NULL || command[0] == '\0')
		return;

	if (strlen(command) > 4096) {
		fprintf(stderr, "aswlcomp: control exec: command too long\n");
		return;
	}

	fprintf(stderr, "aswlcomp: control exec: %s\n", command);
	spawn_command_with_activation(server, command);

	aswl_schedule_flush(server);
}

static void aswl_control_quit(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control quit\n");
	wl_display_terminate(server->display);
}

static void aswl_control_close_focused(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control close_focused\n");
	close_focused_view(server);
	aswl_schedule_flush(server);
}

static void aswl_control_focus_next(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control focus_next\n");
	focus_next_view(server);
	aswl_schedule_flush(server);
}

static void aswl_control_focus_prev(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control focus_prev\n");
	focus_prev_view(server);
	aswl_schedule_flush(server);
}

static void aswl_control_set_workspace(struct wl_client *client, struct wl_resource *resource, uint32_t workspace)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control set_workspace=%u\n", workspace);
	set_workspace(server, workspace);
	aswl_schedule_flush(server);
}

static void aswl_control_workspace_next(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control workspace_next\n");
	workspace_next(server);
	aswl_schedule_flush(server);
}

static void aswl_control_workspace_prev(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control workspace_prev\n");
	workspace_prev(server);
	aswl_schedule_flush(server);
}

static void aswl_control_list_windows(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;
	if (wl_resource_get_version(resource) < 4)
		return;

	fprintf(stderr, "aswlcomp: control list_windows\n");
	send_window_list_snapshot(server, resource);
	aswl_schedule_flush(server);
}

static void aswl_control_focus_window(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	struct aswl_view *view = find_view_by_id(server, id);
	if (view == NULL)
		return;

	fprintf(stderr, "aswlcomp: control focus_window=%u\n", id);
	if (view->workspace != server->current_workspace)
		set_workspace(server, view->workspace);
	focus_view(view, NULL);
	aswl_schedule_flush(server);
}

static void aswl_control_close_window(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	struct aswl_view *view = find_view_by_id(server, id);
	if (view == NULL)
		return;

	fprintf(stderr, "aswlcomp: control close_window=%u\n", id);
	if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
		wlr_xdg_toplevel_send_close(view->xdg_surface->toplevel);
	} else if (view->xwayland_surface != NULL) {
		wlr_xwayland_surface_close(view->xwayland_surface);
	}
	aswl_schedule_flush(server);
}

static void aswl_control_move_window_to_workspace(struct wl_client *client,
                                                  struct wl_resource *resource,
                                                  uint32_t id,
                                                  uint32_t workspace)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	struct aswl_view *view = find_view_by_id(server, id);
	if (view == NULL)
		return;
	if (view->is_dock)
		return;

	workspace = normalize_workspace(server, workspace);
	if (view->workspace == workspace)
		return;

	fprintf(stderr, "aswlcomp: control move_window_to_workspace id=%u ws=%u\n", id, workspace);
	view->workspace = workspace;

	if (view->scene_tree != NULL) {
		bool enabled = view->mapped && view->workspace == server->current_workspace;
		wlr_scene_node_set_enabled(&view->scene_tree->node, enabled);
		if (enabled)
			place_view(view);
	}

	if (server->grabbed_view == view && view->workspace != server->current_workspace)
		end_interactive(server);

	if (server->focused_view == view && view->workspace != server->current_workspace) {
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
			(void)wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, false);
		} else if (view->xwayland_surface != NULL) {
			wlr_xwayland_surface_activate(view->xwayland_surface, false);
		}
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		focus_topmost_view(server);
	}

	broadcast_window_state(server, view);
	aswl_schedule_flush(server);
}

static void aswl_control_toggle_fullscreen(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	struct aswl_view *view = server->focused_view;
	if (view == NULL || !view->mapped || view->scene_tree == NULL)
		return;
	if (view->is_dock)
		return;

	fprintf(stderr, "aswlcomp: control toggle_fullscreen\n");
	if (server->grabbed_view == view)
		end_interactive(server);
	view_set_fullscreen(view, !view_is_fullscreen(view));

	aswl_schedule_flush(server);
}

static void aswl_control_toggle_maximized(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	struct aswl_view *view = server->focused_view;
	if (view == NULL || !view->mapped || view->scene_tree == NULL)
		return;
	if (view->is_dock)
		return;

	fprintf(stderr, "aswlcomp: control toggle_maximized\n");
	if (server->grabbed_view == view)
		end_interactive(server);
	view_set_maximized(view, !view_is_maximized(view));

	aswl_schedule_flush(server);
}

static const struct afterstep_control_v1_interface aswl_control_impl = {
	.destroy = aswl_control_destroy,
	.exec = aswl_control_exec,
	.quit = aswl_control_quit,
	.close_focused = aswl_control_close_focused,
	.focus_next = aswl_control_focus_next,
	.focus_prev = aswl_control_focus_prev,
	.set_workspace = aswl_control_set_workspace,
	.workspace_next = aswl_control_workspace_next,
	.workspace_prev = aswl_control_workspace_prev,
	.list_windows = aswl_control_list_windows,
	.focus_window = aswl_control_focus_window,
	.close_window = aswl_control_close_window,
	.move_window_to_workspace = aswl_control_move_window_to_workspace,
	.toggle_fullscreen = aswl_control_toggle_fullscreen,
	.toggle_maximized = aswl_control_toggle_maximized,
};

void aswl_control_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct aswl_server *server = data;
	uint32_t v = version > 6 ? 6 : version;
	struct wl_resource *res = wl_resource_create(client, &afterstep_control_v1_interface, v, id);
	if (res == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	struct aswl_control_client *cc = calloc(1, sizeof(*cc));
	if (cc == NULL) {
		wl_client_post_no_memory(client);
		wl_resource_destroy(res);
		return;
	}

	cc->server = server;
	cc->resource = res;
	wl_list_insert(server->control_clients.prev, &cc->link);

	wl_resource_set_implementation(res, &aswl_control_impl, cc, aswl_control_resource_destroy);
	if (v >= 3)
		afterstep_control_v1_send_workspace_state(res, server->current_workspace, server->workspace_count);
	send_output_state(server, res);
	if (v >= 4)
		send_window_list_snapshot(server, res);
}

void spawn_lock(struct aswl_server *server)
{
	if (server == NULL)
		return;
	if (server->session_locked || server->session_lock != NULL)
		return;

	const char *cmd = server->lock_command != NULL ? server->lock_command : "aswllock";
	if (cmd == NULL || cmd[0] == '\0')
		return;

	fprintf(stderr, "aswlcomp: lock: %s\n", cmd);
	spawn_command(cmd);
}

static void focus_lock_surface(struct aswl_server *server, struct wlr_surface *surface)
{
	if (server == NULL || surface == NULL || server->seat == NULL)
		return;

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	struct wlr_keyboard_modifiers mods = { 0 };
	if (keyboard != NULL) {
		mods = keyboard->modifiers;
		wlr_seat_set_keyboard(server->seat, keyboard);
	}

	wlr_seat_keyboard_notify_enter(server->seat,
	                              surface,
	                              keyboard != NULL ? keyboard->keycodes : NULL,
	                              keyboard != NULL ? keyboard->num_keycodes : 0,
	                              &mods);
	aswl_ime_set_focus(server, NULL);
}

static void focus_any_lock_surface(struct aswl_server *server)
{
	if (server == NULL || server->seat == NULL)
		return;

	struct aswl_lock_surface *ls;
	wl_list_for_each(ls, &server->lock_surfaces, link) {
		if (ls == NULL || ls->lock_surface == NULL || ls->lock_surface->surface == NULL)
			continue;
		if (!ls->lock_surface->surface->mapped)
			continue;
		focus_lock_surface(server, ls->lock_surface->surface);
		return;
	}

	wlr_seat_keyboard_notify_clear_focus(server->seat);
	aswl_ime_set_focus(server, NULL);
}

void lock_surfaces_destroy(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_lock_surface *ls;
	struct aswl_lock_surface *tmp;
	wl_list_for_each_safe(ls, tmp, &server->lock_surfaces, link) {
		wl_list_remove(&ls->link);

		wl_list_remove(&ls->destroy.link);

		if (ls->surface_destroy_listener_added) {
			wl_list_remove(&ls->surface_destroy.link);
			ls->surface_destroy_listener_added = false;
		}
		if (ls->surface_listeners_added) {
			wl_list_remove(&ls->map.link);
			wl_list_remove(&ls->unmap.link);
			ls->surface_listeners_added = false;
		}

		if (ls->scene_tree != NULL)
			wlr_scene_node_destroy(&ls->scene_tree->node);

		free(ls);
	}
}

static void session_lock_apply_scene(struct aswl_server *server)
{
	if (server == NULL)
		return;

	bool locked = server->session_locked;
	if (server->lock_tree != NULL)
		wlr_scene_node_set_enabled(&server->lock_tree->node, locked);

	for (size_t i = 0; i < 4; i++) {
		if (server->layer_trees[i] != NULL)
			wlr_scene_node_set_enabled(&server->layer_trees[i]->node, !locked);
	}
	if (server->xdg_tree != NULL)
		wlr_scene_node_set_enabled(&server->xdg_tree->node, !locked);

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out == NULL || out->wlr_output == NULL)
			continue;
		if (out->lock_rect != NULL)
			wlr_scene_node_set_enabled(&out->lock_rect->node, locked);
		wlr_output_schedule_frame(out->wlr_output);
	}
}

static void session_lock_enter(struct aswl_server *server)
{
	if (server == NULL)
		return;

	server->session_locked = true;
	server->session_lock_sent_locked = false;

	if (server->grabbed_view != NULL)
		end_interactive(server);

	if (server->seat != NULL) {
		wlr_seat_pointer_notify_clear_focus(server->seat);
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		aswl_pointer_constraints_update(server, NULL);
	}

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out != NULL)
			out->lock_frame_presented = false;
	}

	if (server->lock_tree != NULL)
		wlr_scene_node_raise_to_top(&server->lock_tree->node);
	session_lock_apply_scene(server);
}

static void session_lock_exit(struct aswl_server *server)
{
	if (server == NULL)
		return;

	server->session_locked = false;
	server->session_lock_sent_locked = false;

	lock_surfaces_destroy(server);
	session_lock_apply_scene(server);

	focus_topmost_view(server);
	aswl_idle_lock_note_activity(server);
}

static void handle_lock_surface_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_lock_surface *ls = wl_container_of(listener, ls, surface_destroy);
	if (ls == NULL)
		return;

	wl_list_remove(&ls->surface_destroy.link);
	ls->surface_destroy_listener_added = false;

	if (ls->surface_listeners_added) {
		wl_list_remove(&ls->map.link);
		wl_list_remove(&ls->unmap.link);
		ls->surface_listeners_added = false;
	}
}

static void handle_lock_surface_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_lock_surface *ls = wl_container_of(listener, ls, map);
	if (ls == NULL || ls->server == NULL || ls->lock_surface == NULL)
		return;

	if (!ls->server->session_locked)
		return;

	if (ls->lock_surface->surface != NULL)
		focus_lock_surface(ls->server, ls->lock_surface->surface);
}

static void handle_lock_surface_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_lock_surface *ls = wl_container_of(listener, ls, unmap);
	if (ls == NULL || ls->server == NULL)
		return;

	if (!ls->server->session_locked)
		return;

	focus_any_lock_surface(ls->server);
}

static void handle_lock_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_lock_surface *ls = wl_container_of(listener, ls, destroy);
	if (ls == NULL)
		return;

	wl_list_remove(&ls->destroy.link);

	if (ls->surface_destroy_listener_added) {
		wl_list_remove(&ls->surface_destroy.link);
		ls->surface_destroy_listener_added = false;
	}
	if (ls->surface_listeners_added) {
		wl_list_remove(&ls->map.link);
		wl_list_remove(&ls->unmap.link);
		ls->surface_listeners_added = false;
	}

	if (ls->scene_tree != NULL)
		wlr_scene_node_destroy(&ls->scene_tree->node);

	wl_list_remove(&ls->link);
	free(ls);
}

void arrange_lock_surfaces(struct aswl_server *server)
{
	if (server == NULL || server->output_layout == NULL)
		return;

	struct aswl_lock_surface *ls;
	wl_list_for_each(ls, &server->lock_surfaces, link) {
		if (ls == NULL || ls->lock_surface == NULL || ls->lock_surface->output == NULL || ls->scene_tree == NULL)
			continue;

		struct wlr_box box = { 0 };
		wlr_output_layout_get_box(server->output_layout, ls->lock_surface->output, &box);

		wlr_scene_node_set_position(&ls->scene_tree->node, box.x, box.y);

		uint32_t w = box.width > 0 ? (uint32_t)box.width : 1;
		uint32_t h = box.height > 0 ? (uint32_t)box.height : 1;
		if (ls->configured_width != w || ls->configured_height != h) {
			ls->configured_width = w;
			ls->configured_height = h;
			(void)wlr_session_lock_surface_v1_configure(ls->lock_surface, w, h);
		}
	}
}

static void handle_session_lock_new_surface(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, session_lock_new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	if (server == NULL || lock_surface == NULL || lock_surface->surface == NULL)
		return;

	struct aswl_lock_surface *ls = calloc(1, sizeof(*ls));
	if (ls == NULL)
		return;
	ls->server = server;
	ls->lock_surface = lock_surface;
	wl_list_insert(&server->lock_surfaces, &ls->link);

	ls->scene_tree = wlr_scene_subsurface_tree_create(server->lock_tree, lock_surface->surface);
	if (ls->scene_tree == NULL) {
		wl_list_remove(&ls->link);
		free(ls);
		return;
	}

	ls->destroy.notify = handle_lock_surface_destroy;
	wl_signal_add(&lock_surface->events.destroy, &ls->destroy);

	ls->map.notify = handle_lock_surface_map;
	wl_signal_add(&lock_surface->surface->events.map, &ls->map);

	ls->unmap.notify = handle_lock_surface_unmap;
	wl_signal_add(&lock_surface->surface->events.unmap, &ls->unmap);
	ls->surface_listeners_added = true;

	ls->surface_destroy.notify = handle_lock_surface_surface_destroy;
	wl_signal_add(&lock_surface->surface->events.destroy, &ls->surface_destroy);
	ls->surface_destroy_listener_added = true;

	arrange_lock_surfaces(server);
}

void session_lock_detach(struct aswl_server *server)
{
	if (server == NULL)
		return;
	if (server->session_lock == NULL)
		return;

	wl_list_remove(&server->session_lock_new_surface.link);
	wl_list_remove(&server->session_lock_unlock.link);
	wl_list_remove(&server->session_lock_destroy.link);

	server->session_lock = NULL;
	server->session_lock_sent_locked = false;
}

static void handle_session_lock_unlock(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_server *server = wl_container_of(listener, server, session_lock_unlock);
	if (server == NULL)
		return;

	session_lock_detach(server);
	session_lock_exit(server);
}

static void handle_session_lock_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_server *server = wl_container_of(listener, server, session_lock_destroy);
	if (server == NULL)
		return;

	/*
	 * Client died or otherwise destroyed the lock without unlocking.
	 * Per protocol, do not unlock in response: remain locked and allow recovery
	 * via a new lock client.
	 */
	session_lock_detach(server);
	lock_surfaces_destroy(server);
	session_lock_apply_scene(server);
}

void handle_new_session_lock(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_session_lock);
	struct wlr_session_lock_v1 *lock = data;
	if (server == NULL || lock == NULL)
		return;

	if (server->session_lock != NULL) {
		wlr_session_lock_v1_destroy(lock);
		return;
	}

	server->session_lock = lock;
	server->session_lock_sent_locked = false;

	server->session_lock_new_surface.notify = handle_session_lock_new_surface;
	wl_signal_add(&lock->events.new_surface, &server->session_lock_new_surface);

	server->session_lock_unlock.notify = handle_session_lock_unlock;
	wl_signal_add(&lock->events.unlock, &server->session_lock_unlock);

	server->session_lock_destroy.notify = handle_session_lock_destroy;
	wl_signal_add(&lock->events.destroy, &server->session_lock_destroy);

	session_lock_enter(server);
}
