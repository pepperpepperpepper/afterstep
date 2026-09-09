#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <wlr/xwayland.h>

#include "aswlcomp_internal.h"

const char *view_title(struct aswl_view *view)
{
	if (view == NULL)
		return "";

	if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL && view->xdg_surface->toplevel->title != NULL)
		return view->xdg_surface->toplevel->title;

	if (view->xwayland_surface != NULL && view->xwayland_surface->title != NULL)
		return view->xwayland_surface->title;

	return "";
}

const char *view_app_id(struct aswl_view *view)
{
	if (view == NULL)
		return "";

	if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL && view->xdg_surface->toplevel->app_id != NULL)
		return view->xdg_surface->toplevel->app_id;

	if (view->xwayland_surface != NULL && view->xwayland_surface->class != NULL)
		return view->xwayland_surface->class;

	return "";
}

bool view_is_suite_popup(struct aswl_view *view)
{
	if (view == NULL)
		return false;
	return strcmp(view_app_id(view), "afterstep.aswlmenu") == 0;
}

bool view_is_asmodule(struct aswl_view *view)
{
	if (view == NULL)
		return false;
	return strcmp(view_app_id(view), "ASModule") == 0;
}

static bool view_is_nofocus_asmodule(struct aswl_view *view)
{
	if (!view_is_asmodule(view))
		return false;

	/*
	 * X11 AfterStep's default style database treats most ASModule windows as
	 * "NoFocus", but WinTabs/TermTabs is a notable exception: it is interactive
	 * and should be focusable (and is styled with a normal titlebar).
	 *
	 * Under Xwayland the WM_CLASS "class" is exposed as app_id (ASModule). Use
	 * the instance to preserve focus semantics for WinTabs/TermTabs.
	 */
	if (view->xwayland_surface != NULL) {
		const char *instance = view->xwayland_surface->instance;
		if (instance != NULL && (strcmp(instance, "WinTabs") == 0 || strcmp(instance, "TermTabs") == 0))
			return false;
	}

	return true;
}

struct aswl_view *view_from_wlr_surface(struct wlr_surface *surface)
{
	struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(surface);
	while (xdg_surface != NULL && xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		struct wlr_surface *parent = xdg_surface->popup->parent;
		xdg_surface = parent != NULL ? wlr_xdg_surface_try_from_wlr_surface(parent) : NULL;
	}
	if (xdg_surface != NULL && xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return xdg_surface->data;

	struct wlr_xwayland_surface *xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface);
	if (xsurface != NULL)
		return xsurface->data;

	return NULL;
}

void focus_view(struct aswl_view *view, struct wlr_surface *surface)
{
	if (view == NULL || view->server == NULL)
		return;

	struct aswl_server *server = view->server;
	if (server->session_locked)
		return;

	/* Focusing an iconified view RESTORES it: the window-list row and the
	 * foreign-toplevel activate both land here, and a hidden window cannot
	 * take focus — every focus implies un-minimize. */
	if (view->minimized)
		view_set_minimized(view, false);

	struct aswl_view *old_focus = server->focused_view;
	bool nofocus_module = view_is_nofocus_asmodule(view);
	bool steal_focus = !view_is_suite_popup(view) && !nofocus_module;

	if (view->scene_tree != NULL)
		wlr_scene_node_raise_to_top(&view->scene_tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(server->views.prev, &view->link);

	if (steal_focus && old_focus != view) {
		if (old_focus != NULL) {
			if (old_focus->xdg_surface != NULL && old_focus->xdg_surface->toplevel != NULL) {
				(void)wlr_xdg_toplevel_set_activated(old_focus->xdg_surface->toplevel, false);
			} else if (old_focus->xwayland_surface != NULL) {
				wlr_xwayland_surface_activate(old_focus->xwayland_surface, false);
			}
		}
		server->focused_view = view;
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
			(void)wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, true);
		} else if (view->xwayland_surface != NULL) {
			wlr_xwayland_surface_activate(view->xwayland_surface, true);
		}

		view_update_toplevel_protocols(old_focus);
		view_update_toplevel_protocols(view);
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard != NULL && !nofocus_module) {
		if (surface == NULL) {
			if (view->xdg_surface != NULL)
				surface = view->xdg_surface->surface;
			else if (view->xwayland_surface != NULL)
				surface = view->xwayland_surface->surface;
		}
		if (surface != NULL) {
			wlr_seat_keyboard_notify_enter(server->seat,
			                              surface,
			                              keyboard->keycodes,
			                              keyboard->num_keycodes,
			                              &keyboard->modifiers);
			aswl_ime_set_focus(server, surface);
		}

		if (view->xwayland_surface != NULL)
			wlr_xwayland_surface_offer_focus(view->xwayland_surface);
	}

	if (steal_focus && old_focus != view) {
		if (old_focus != NULL)
			broadcast_window_state(server, old_focus);
		broadcast_window_state(server, view);
	}

	if (steal_focus && old_focus != view) {
		view_update_decorations(old_focus);
		view_update_decorations(view);
	}
}

void close_focused_view(struct aswl_server *server)
{
	if (server == NULL || server->focused_view == NULL)
		return;

	struct aswl_view *view = server->focused_view;
	if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
		wlr_xdg_toplevel_send_close(view->xdg_surface->toplevel);
	} else if (view->xwayland_surface != NULL) {
		wlr_xwayland_surface_close(view->xwayland_surface);
	}
}

static bool view_visible(struct aswl_server *server, struct aswl_view *view)
{
	if (server == NULL || view == NULL)
		return false;
	if (!view->mapped || view->scene_tree == NULL)
		return false;
	if (view->minimized)
		return false;
	if (view->is_dock)
		return false;
	if (view_is_suite_popup(view))
		return false;
	if (view_is_asmodule(view))
		return false;
	return view->workspace == server->current_workspace;
}

void focus_next_view(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view_visible(server, view))
			continue;
		if (view == server->focused_view)
			continue;
		focus_view(view, NULL);
		return;
	}
}

void focus_prev_view(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_view *view;
	wl_list_for_each_reverse(view, &server->views, link) {
		if (!view_visible(server, view))
			continue;
		if (view == server->focused_view)
			continue;
		focus_view(view, NULL);
		return;
	}
}

void focus_topmost_view(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_view *view;
	wl_list_for_each_reverse(view, &server->views, link) {
		if (!view_visible(server, view))
			continue;
		focus_view(view, NULL);
		return;
	}
}

uint32_t normalize_workspace(struct aswl_server *server, uint32_t workspace)
{
	if (workspace < 1)
		workspace = 1;

	if (server != NULL && server->workspace_count > 0 && workspace > server->workspace_count) {
		workspace = ((workspace - 1) % server->workspace_count) + 1;
	}

	return workspace;
}

void set_workspace(struct aswl_server *server, uint32_t workspace)
{
	if (server == NULL)
		return;

	workspace = normalize_workspace(server, workspace);
	if (server->current_workspace == workspace)
		return;

	fprintf(stderr, "aswlcomp: workspace: %u -> %u\n", server->current_workspace, workspace);
	server->current_workspace = workspace;

	if (server->grabbed_view != NULL && !server->grabbed_view->is_dock && server->grabbed_view->workspace != workspace)
		end_interactive(server);

	if (server->focused_view != NULL && !server->focused_view->is_dock && server->focused_view->workspace != workspace) {
		struct aswl_view *old = server->focused_view;
		if (old->xdg_surface != NULL && old->xdg_surface->toplevel != NULL) {
			(void)wlr_xdg_toplevel_set_activated(old->xdg_surface->toplevel, false);
		} else if (old->xwayland_surface != NULL) {
			wlr_xwayland_surface_activate(old->xwayland_surface, false);
		}
		server->focused_view = NULL;
		view_update_toplevel_protocols(old);
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		broadcast_window_state(server, old);
	}

	struct aswl_view *view;
	struct aswl_view *tmp;
	wl_list_for_each_safe(view, tmp, &server->views, link) {
		if (view->scene_tree == NULL)
			continue;
		bool enabled = view->mapped && !view->minimized &&
		               (view->is_dock || view->workspace == server->current_workspace);
		wlr_scene_node_set_enabled(&view->scene_tree->node, enabled);
		if (enabled && !view->is_dock)
			place_view(view);
	}

	arrange_dock_views(server);
	focus_topmost_view(server);
	broadcast_workspace_state(server);
	aswl_state_save(server);
}

void workspace_next(struct aswl_server *server)
{
	if (server == NULL)
		return;

	uint32_t count = server->workspace_count > 0 ? server->workspace_count : 1;
	uint32_t next = server->current_workspace + 1;
	if (next > count)
		next = 1;
	set_workspace(server, next);
}

void workspace_prev(struct aswl_server *server)
{
	if (server == NULL)
		return;

	uint32_t count = server->workspace_count > 0 ? server->workspace_count : 1;
	uint32_t prev = server->current_workspace > 1 ? server->current_workspace - 1 : count;
	set_workspace(server, prev);
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

bool view_is_fullscreen(struct aswl_view *view)
{
	if (view == NULL)
		return false;
	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
		return view->xdg_surface->toplevel->current.fullscreen;
	if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		return view->xwayland_surface->fullscreen;
	return false;
}

bool view_is_maximized(struct aswl_view *view)
{
	if (view == NULL)
		return false;
	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
		return view->xdg_surface->toplevel->current.maximized;
	if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		return view->xwayland_surface->maximized_horz || view->xwayland_surface->maximized_vert;
	return false;
}

static void view_save_geometry(struct aswl_view *view)
{
	if (view == NULL || view->saved_geometry)
		return;
	if (view->scene_tree == NULL)
		return;

	int lx = 0;
	int ly = 0;
	(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);

	int w = 0;
	int h = 0;
	view_get_frame_size(view, &w, &h);

	view->saved_geometry = true;
	view->saved_x = lx;
	view->saved_y = ly;
	view->saved_w = w;
	view->saved_h = h;
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

static struct aswl_output *find_aswl_output(struct aswl_server *server, struct wlr_output *output)
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

static struct wlr_output *view_get_output(struct aswl_server *server, struct aswl_view *view)
{
	if (server == NULL || server->output_layout == NULL)
		return NULL;

	int lx = 0;
	int ly = 0;
	if (view != NULL && view->scene_tree != NULL)
		(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);

	struct wlr_output *output = wlr_output_layout_output_at(server->output_layout, lx, ly);
	if (output == NULL && server->cursor != NULL)
		output = wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
	if (output == NULL)
		output = wlr_output_layout_get_center_output(server->output_layout);
	return output;
}

static bool view_target_boxes(struct aswl_server *server, struct aswl_view *view, struct wlr_box *full_out, struct wlr_box *usable_out)
{
	if (server == NULL || server->output_layout == NULL)
		return false;

	struct wlr_output *output = view_get_output(server, view);
	if (output == NULL)
		return false;

	struct wlr_box full = { 0 };
	wlr_output_layout_get_box(server->output_layout, output, &full);
	struct wlr_box usable = full;

	struct aswl_output *out = find_aswl_output(server, output);
	if (out != NULL && out->usable_box.width > 0 && out->usable_box.height > 0)
		usable = out->usable_box;

	if (full_out != NULL)
		*full_out = full;
	if (usable_out != NULL)
		*usable_out = usable;
	return full.width > 0 && full.height > 0;
}

static void view_apply_geometry(struct aswl_view *view, int x, int y, int w, int h)
{
	if (view == NULL || view->scene_tree == NULL)
		return;

	wlr_scene_node_set_position(&view->scene_tree->node, x, y);
	view->placed = true;

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

	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
		if (w > 0 && h > 0)
			(void)wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, cw, ch);
		return;
	}
	if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
		uint16_t uw = clamp_u16(cw, view->xwayland_surface->width);
		uint16_t uh = clamp_u16(ch, view->xwayland_surface->height);
		wlr_xwayland_surface_configure(view->xwayland_surface,
		                               clamp_i16(x + ox),
		                               clamp_i16(y + oy),
		                               uw,
		                               uh);
		return;
	}
}

static void view_restore_geometry(struct aswl_view *view)
{
	if (view == NULL || !view->saved_geometry)
		return;

	view_apply_geometry(view, view->saved_x, view->saved_y, view->saved_w, view->saved_h);
	view->saved_geometry = false;
}

void view_set_fullscreen(struct aswl_view *view, bool fullscreen)
{
	if (view == NULL || view->server == NULL)
		return;
	if (view->is_dock)
		return;

	struct aswl_server *server = view->server;
	struct wlr_box full = { 0 };
	struct wlr_box usable = { 0 };
	(void)view_target_boxes(server, view, &full, &usable);

	if (fullscreen) {
		view_save_geometry(view);
		if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_fullscreen(view->xdg_surface->toplevel, true);
		else if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
			wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, true);

		view_apply_geometry(view, full.x, full.y, full.width, full.height);
		view_update_toplevel_protocols(view);
		return;
	}

	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
		(void)wlr_xdg_toplevel_set_fullscreen(view->xdg_surface->toplevel, false);
	else if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, false);

	view_restore_geometry(view);
	view_update_toplevel_protocols(view);
}

void view_set_maximized(struct aswl_view *view, bool maximized)
{
	if (view == NULL || view->server == NULL)
		return;
	if (view->is_dock)
		return;

	struct aswl_server *server = view->server;
	struct wlr_box full = { 0 };
	struct wlr_box usable = { 0 };
	(void)view_target_boxes(server, view, &full, &usable);

	if (maximized) {
		view_save_geometry(view);
		if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_maximized(view->xdg_surface->toplevel, true);
		else if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
			wlr_xwayland_surface_set_maximized(view->xwayland_surface, true, true);

		view_apply_geometry(view, usable.x, usable.y, usable.width, usable.height);
		view_update_toplevel_protocols(view);
		return;
	}

	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
		(void)wlr_xdg_toplevel_set_maximized(view->xdg_surface->toplevel, false);
	else if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		wlr_xwayland_surface_set_maximized(view->xwayland_surface, false, false);

	view_restore_geometry(view);
	view_update_toplevel_protocols(view);
}

void view_set_minimized(struct aswl_view *view, bool minimized)
{
	if (view == NULL || view->server == NULL)
		return;
	if (view->is_dock)
		return;

	struct aswl_server *server = view->server;

	/* view->minimized is the ONE authoritative field (every enabled term,
	 * view_visible and the window-list flags read it). Do NOT OR in the
	 * Xwayland surface's own flag here: wlr pre-flips xsurface->minimized
	 * on the _NET_WM_STATE path BEFORE emitting request_minimize, so a
	 * pair-read early-returns and the EWMH minimize (pager, wmctrl) never
	 * hides the view. Guarding on our own flag makes a pre-flipped request
	 * just run the verb (the X write below is idempotent). */
	if (view->minimized == minimized)
		return;

	view->minimized = minimized;
	if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		wlr_xwayland_surface_set_minimized(view->xwayland_surface, minimized);

	fprintf(stderr, "aswlcomp: minimize view %u -> %d\n", view->id, minimized ? 1 : 0);

	/* The standard enabled expression, plus the minimized term: the same term
	 * rides every recompute site (map, set_workspace, move_window_to_workspace)
	 * so nothing resurrects a hidden view by switching desks. */
	if (view->scene_tree != NULL) {
		bool enabled = view->mapped && !view->minimized &&
		               (view->is_dock || view->workspace == server->current_workspace);
		wlr_scene_node_set_enabled(&view->scene_tree->node, enabled);
	}

	if (minimized) {
		if (server->grabbed_view == view)
			end_interactive(server);
		if (server->focused_view == view) {
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
	}

	view_update_toplevel_protocols(view);
	broadcast_window_state(server, view);
	aswl_schedule_flush(server);
}

void place_view(struct aswl_view *view)
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
	view_get_frame_size(view, &width, &height);

	int border = 0;
	int title_h = 0;
	view_get_deco_metrics(view, &border, &title_h);

	int content_w = 0;
	int content_h = 0;
	view_get_current_size(view, &content_w, &content_h);

	if (view->xwayland_surface != NULL && !view->xwayland_surface->override_redirect && (content_w <= 1 || content_h <= 1)) {
		/*
		 * Some Xwayland clients can map before a meaningful size is known,
		 * leaving width/height at 0. Avoid forcing a huge default size (which
		 * can stick for clients like xterm); pick a smaller, terminal-like
		 * fallback that still keeps the window visible.
		 */
		if (content_w <= 1)
			content_w = 640;
		if (content_h <= 1)
			content_h = 400;
		width = content_w + 2 * border;
		height = content_h + title_h + border;
	}

	bool suite_popup = view_is_suite_popup(view);

	/* Avoid placing regular windows under top layer-shell bars (e.g. Wharf-ish docks). */
	if (!suite_popup)
		apply_layer_struts_top(server, output, &usable);

	/*
	 * Root-menu style: suite popups (aswlmenu) should appear near the pointer,
	 * not cascade like normal application windows.
	 */
	int x = usable.x + 40 + server->cascade_offset;
	int y = usable.y + 40 + server->cascade_offset;
	if (suite_popup && server->cursor != NULL) {
		x = (int)server->cursor->x;
		y = (int)server->cursor->y;
	}
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
	if (view->xwayland_surface != NULL && width > 0 && height > 0) {
		int ox = border;
		int oy = title_h;
		int cw = width - 2 * border;
		int ch = height - title_h - border;
		if (cw < 1)
			cw = 1;
		if (ch < 1)
			ch = 1;
		wlr_xwayland_surface_configure(view->xwayland_surface,
		                               x + ox,
		                               y + oy,
		                               (uint16_t)cw,
		                               (uint16_t)ch);
		view_update_decorations(view);
	}
	view->placed = true;

	if (!suite_popup) {
		server->cascade_offset += 32;
		if (server->cascade_offset >= 256)
			server->cascade_offset = 0;
	}
}
