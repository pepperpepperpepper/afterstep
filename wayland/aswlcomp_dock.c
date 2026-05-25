#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>

#include <wlr/xwayland.h>

#include "aswlcomp_internal.h"

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

static int str_icmp(const char *a, const char *b)
{
	if (a == NULL)
		a = "";
	if (b == NULL)
		b = "";
	while (*a != '\0' && *b != '\0') {
		int da = tolower((unsigned char)*a);
		int db = tolower((unsigned char)*b);
		if (da != db)
			return da - db;
		a++;
		b++;
	}
	return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static bool str_contains_case_insensitive(const char *haystack, const char *needle)
{
	if (haystack == NULL || needle == NULL)
		return false;
	if (needle[0] == '\0')
		return false;

	for (const char *h = haystack; *h != '\0'; h++) {
		if (tolower((unsigned char)*h) != tolower((unsigned char)needle[0]))
			continue;

		const char *hh = h;
		const char *nn = needle;
		while (*hh != '\0' && *nn != '\0') {
			if (tolower((unsigned char)*hh) != tolower((unsigned char)*nn))
				break;
			hh++;
			nn++;
		}
		if (*nn == '\0')
			return true;
	}

	return false;
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

static bool view_is_small_dock_candidate(struct aswl_view *view)
{
	if (view == NULL || view->xwayland_surface == NULL)
		return false;

	uint16_t max_dim = 512;
	if (view->server != NULL && view->server->dock.max_dim > 0)
		max_dim = view->server->dock.max_dim;

	/*
	 * Small dockapps should be tiny; treat 0 sizes as "unknown" and allow them
	 * (Xwayland may not have a size until after the first configure).
	 */
	uint16_t w = view->xwayland_surface->width;
	uint16_t h = view->xwayland_surface->height;
	if (w > 0 && w > max_dim)
		return false;
	if (h > 0 && h > max_dim)
		return false;

	return true;
}

static bool dock_rule_matches_view(const char *rule, struct aswl_view *view)
{
	if (rule == NULL || view == NULL)
		return false;

	while (*rule != '\0' && isspace((unsigned char)*rule))
		rule++;
	if (rule[0] == '\0')
		return false;

	const char *title = view_title(view);
	const char *app = view_app_id(view);

	if (strncmp(rule, "class=", 6) == 0 || strncmp(rule, "app_id=", 7) == 0 || strncmp(rule, "appid=", 6) == 0) {
		const char *val = strchr(rule, '=');
		if (val == NULL)
			return false;
		val++;
		while (*val != '\0' && isspace((unsigned char)*val))
			val++;
		return val[0] != '\0' && str_ieq(val, app);
	}

	if (strncmp(rule, "title=", 6) == 0) {
		const char *val = rule + 6;
		while (*val != '\0' && isspace((unsigned char)*val))
			val++;
		return val[0] != '\0' && str_contains_case_insensitive(title, val);
	}

	return str_contains_case_insensitive(app, rule) || str_contains_case_insensitive(title, rule);
}

static size_t dock_rule_rank(const struct aswl_dock_config *dock, struct aswl_view *view)
{
	if (dock == NULL || dock->rule_count == 0 || view == NULL)
		return SIZE_MAX;

	for (size_t i = 0; i < dock->rule_count; i++) {
		if (dock_rule_matches_view(dock->rules[i], view))
			return i;
	}
	return SIZE_MAX;
}

static bool view_is_xwayland_dockapp(struct aswl_view *view)
{
	if (view == NULL || view->xwayland_surface == NULL)
		return false;
	if (!view_is_small_dock_candidate(view))
		return false;

	if (wlr_xwayland_surface_has_window_type(view->xwayland_surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DOCK))
		return true;

	/*
	 * Historic "dockapp" convention: many dockapps set WM_CLASS to "DockApp"
	 * without also tagging themselves as an EWMH DOCK window.
	 */
	const char *klass = view_app_id(view);
	if (klass != NULL && klass[0] != '\0' && str_ieq(klass, "DockApp"))
		return true;

	/* Config-driven allowlist: treat matching small Xwayland windows as dockapps. */
	if (view->server != NULL && view->server->dock.rule_count > 0) {
		if (dock_rule_rank(&view->server->dock, view) != SIZE_MAX)
			return true;
	}

	return false;
}

void view_maybe_mark_dock(struct aswl_view *view)
{
	if (view == NULL || view->is_dock)
		return;
	if (view->type != ASWL_VIEW_XWAYLAND)
		return;
	if (!view_is_xwayland_dockapp(view))
		return;

	view->is_dock = true;

	if (view->xwayland_surface != NULL) {
		wlr_xwayland_surface_set_sticky(view->xwayland_surface, true);
		wlr_xwayland_surface_set_skip_taskbar(view->xwayland_surface, true);
		wlr_xwayland_surface_set_skip_pager(view->xwayland_surface, true);
	}

	fprintf(stderr, "aswlcomp: dockapp: class=%s title=%s\n", view_app_id(view), view_title(view));
}

struct aswl_dock_item {
	struct aswl_view *view;
	size_t create_index;
	size_t rule_rank;
	const char *title;
	const char *app;
};

static int aswl_dock_item_cmp(const struct aswl_dock_item *a,
                              const struct aswl_dock_item *b,
                              enum aswl_dock_order_mode order)
{
	if (a == NULL || b == NULL)
		return 0;

	if (a->rule_rank != b->rule_rank)
		return a->rule_rank < b->rule_rank ? -1 : 1;

	switch (order) {
	case ASWL_DOCK_ORDER_TITLE: {
		int c = str_icmp(a->title, b->title);
		if (c != 0)
			return c;
		break;
	}
	case ASWL_DOCK_ORDER_CLASS: {
		int c = str_icmp(a->app, b->app);
		if (c != 0)
			return c;
		c = str_icmp(a->title, b->title);
		if (c != 0)
			return c;
		break;
	}
	case ASWL_DOCK_ORDER_CONFIG:
	case ASWL_DOCK_ORDER_CREATE:
	default:
		break;
	}

	if (a->create_index != b->create_index)
		return a->create_index < b->create_index ? -1 : 1;
	return 0;
}

void arrange_dock_views(struct aswl_server *server)
{
	if (server == NULL || server->output_layout == NULL)
		return;

	struct wlr_output *output = wlr_output_layout_get_center_output(server->output_layout);
	if (output == NULL)
		return;

	struct wlr_box full = { 0 };
	wlr_output_layout_get_box(server->output_layout, output, &full);

	struct wlr_box usable = full;
	struct aswl_output *out = output_from_wlr_output(server, output);
	if (out != NULL && out->usable_box.width > 0 && out->usable_box.height > 0)
		usable = out->usable_box;

	const int pad = server->dock.pad;
	const int spacing = server->dock.spacing;
	const enum aswl_dock_flow flow = server->dock.flow;
	const enum aswl_dock_order_mode order = server->dock.order;

	bool anchor_right = (server->dock.anchor == ASWL_DOCK_ANCHOR_BOTTOM_RIGHT ||
	                     server->dock.anchor == ASWL_DOCK_ANCHOR_TOP_RIGHT);
	bool anchor_bottom = (server->dock.anchor == ASWL_DOCK_ANCHOR_BOTTOM_LEFT ||
	                      server->dock.anchor == ASWL_DOCK_ANCHOR_BOTTOM_RIGHT);

	size_t dock_count = 0;
	struct aswl_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->is_dock && view->mapped && view->scene_tree != NULL)
			dock_count++;
	}
	if (dock_count == 0)
		return;

	struct aswl_dock_item *items = calloc(dock_count, sizeof(*items));
	if (items == NULL)
		return;

	size_t idx = 0;
	size_t create_index = 0;
	wl_list_for_each(view, &server->views, link) {
		if (view->is_dock && view->mapped && view->scene_tree != NULL) {
			items[idx].view = view;
			items[idx].create_index = create_index;
			items[idx].rule_rank = dock_rule_rank(&server->dock, view);
			items[idx].title = view_title(view);
			items[idx].app = view_app_id(view);
			idx++;
		}
		create_index++;
	}

	for (size_t i = 1; i < dock_count; i++) {
		struct aswl_dock_item key = items[i];
		size_t j = i;
		while (j > 0 && aswl_dock_item_cmp(&key, &items[j - 1], order) < 0) {
			items[j] = items[j - 1];
			j--;
		}
		items[j] = key;
	}

	const int min_x = usable.x + pad;
	const int max_x = usable.x + usable.width - pad;
	const int min_y = usable.y + pad;
	const int max_y = usable.y + usable.height - pad;

	const int x_step_dir = anchor_right ? -1 : 1;
	const int y_step_dir = anchor_bottom ? -1 : 1;

	int start_x = anchor_right ? max_x : min_x;
	int start_y = anchor_bottom ? max_y : min_y;
	int x_cursor = start_x;
	int y_cursor = start_y;
	int line_extent = 0;

	for (size_t i = 0; i < dock_count; i++) {
		view = items[i].view;
		if (view == NULL || view->scene_tree == NULL)
			continue;

		int w = 0;
		int h = 0;
		view_get_frame_size(view, &w, &h);
		if (w <= 0)
			w = 64;
		if (h <= 0)
			h = 64;

		if (flow == ASWL_DOCK_FLOW_ROW) {
			int x = anchor_right ? x_cursor - w : x_cursor;
			int y = anchor_bottom ? y_cursor - h : y_cursor;

			if (anchor_right) {
				if (x < min_x && x_cursor != start_x) {
					x_cursor = start_x;
					y_cursor += y_step_dir * (line_extent + spacing);
					line_extent = 0;
					x = anchor_right ? x_cursor - w : x_cursor;
					y = anchor_bottom ? y_cursor - h : y_cursor;
				}
			} else {
				if (x + w > max_x && x_cursor != start_x) {
					x_cursor = start_x;
					y_cursor += y_step_dir * (line_extent + spacing);
					line_extent = 0;
					x = anchor_right ? x_cursor - w : x_cursor;
					y = anchor_bottom ? y_cursor - h : y_cursor;
				}
			}

			if (anchor_right) {
				if (x < min_x)
					x = min_x;
			} else {
				if (x + w > max_x)
					x = max_x - w;
				if (x < min_x)
					x = min_x;
			}

			if (anchor_bottom) {
				if (y < min_y)
					y = min_y;
			} else {
				if (y + h > max_y)
					y = max_y - h;
				if (y < min_y)
					y = min_y;
			}

			wlr_scene_node_set_position(&view->scene_tree->node, x, y);
			wlr_scene_node_raise_to_top(&view->scene_tree->node);

			if (view->xwayland_surface != NULL) {
				uint16_t ww = view->xwayland_surface->width;
				uint16_t hh = view->xwayland_surface->height;
				if (ww == 0)
					ww = (uint16_t)w;
				if (hh == 0)
					hh = (uint16_t)h;
				int ox = 0;
				int oy = 0;
				view_get_content_offset(view, &ox, &oy);
				wlr_xwayland_surface_configure(view->xwayland_surface, x + ox, y + oy, ww, hh);
			}

			view->placed = true;

			x_cursor += x_step_dir * (w + spacing);
			if (h > line_extent)
				line_extent = h;
			continue;
		}

		/* Column flow. */
		int x = anchor_right ? x_cursor - w : x_cursor;
		int y = anchor_bottom ? y_cursor - h : y_cursor;

		if (anchor_bottom) {
			if (y < min_y && y_cursor != start_y) {
				y_cursor = start_y;
				x_cursor += x_step_dir * (line_extent + spacing);
				line_extent = 0;
				x = anchor_right ? x_cursor - w : x_cursor;
				y = anchor_bottom ? y_cursor - h : y_cursor;
			}
		} else {
			if (y + h > max_y && y_cursor != start_y) {
				y_cursor = start_y;
				x_cursor += x_step_dir * (line_extent + spacing);
				line_extent = 0;
				x = anchor_right ? x_cursor - w : x_cursor;
				y = anchor_bottom ? y_cursor - h : y_cursor;
			}
		}

		if (anchor_right) {
			if (x < min_x)
				x = min_x;
		} else {
			if (x + w > max_x)
				x = max_x - w;
			if (x < min_x)
				x = min_x;
		}

		if (anchor_bottom) {
			if (y < min_y)
				y = min_y;
		} else {
			if (y + h > max_y)
				y = max_y - h;
			if (y < min_y)
				y = min_y;
		}

		wlr_scene_node_set_position(&view->scene_tree->node, x, y);
		wlr_scene_node_raise_to_top(&view->scene_tree->node);

		if (view->xwayland_surface != NULL) {
			uint16_t ww = view->xwayland_surface->width;
			uint16_t hh = view->xwayland_surface->height;
			if (ww == 0)
				ww = (uint16_t)w;
			if (hh == 0)
				hh = (uint16_t)h;
			int ox = 0;
			int oy = 0;
			view_get_content_offset(view, &ox, &oy);
			wlr_xwayland_surface_configure(view->xwayland_surface, x + ox, y + oy, ww, hh);
		}

		view->placed = true;

		y_cursor += y_step_dir * (h + spacing);
		if (w > line_extent)
			line_extent = w;
	}

	free(items);
}

