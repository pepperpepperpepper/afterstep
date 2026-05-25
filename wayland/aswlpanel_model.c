#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlpanel_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int clamp_int(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static bool as_panel_edge_is_vertical(enum as_panel_edge edge)
{
	return edge == ASWL_PANEL_EDGE_LEFT || edge == ASWL_PANEL_EDGE_RIGHT;
}

static uint32_t as_state_default_anchor_flags(const struct as_state *state)
{
	if (state == NULL)
		return ASWL_ANCHOR_TOP | ASWL_ANCHOR_LEFT | ASWL_ANCHOR_RIGHT;

	switch (state->edge) {
	case ASWL_PANEL_EDGE_BOTTOM:
		if (state->dock_mode)
			return ASWL_ANCHOR_BOTTOM | ASWL_ANCHOR_LEFT;
		return ASWL_ANCHOR_BOTTOM | ASWL_ANCHOR_LEFT | ASWL_ANCHOR_RIGHT;
	case ASWL_PANEL_EDGE_LEFT:
		return ASWL_ANCHOR_LEFT | ASWL_ANCHOR_TOP | ASWL_ANCHOR_BOTTOM;
	case ASWL_PANEL_EDGE_RIGHT:
		return ASWL_ANCHOR_RIGHT | ASWL_ANCHOR_TOP | ASWL_ANCHOR_BOTTOM;
	case ASWL_PANEL_EDGE_TOP:
	default:
		if (state->dock_mode)
			return ASWL_ANCHOR_TOP | ASWL_ANCHOR_LEFT;
		return ASWL_ANCHOR_TOP | ASWL_ANCHOR_LEFT | ASWL_ANCHOR_RIGHT;
	}
}

uint32_t as_state_anchor_flags(const struct as_state *state)
{
	if (state == NULL)
		return ASWL_ANCHOR_TOP | ASWL_ANCHOR_LEFT | ASWL_ANCHOR_RIGHT;
	if (state->anchor_override_set)
		return state->anchor_override;
	return as_state_default_anchor_flags(state);
}

static bool as_anchor_spans_horizontal(uint32_t anchor)
{
	return (anchor & ASWL_ANCHOR_LEFT) != 0 && (anchor & ASWL_ANCHOR_RIGHT) != 0;
}

static bool as_anchor_spans_vertical(uint32_t anchor)
{
	return (anchor & ASWL_ANCHOR_TOP) != 0 && (anchor & ASWL_ANCHOR_BOTTOM) != 0;
}

bool as_state_compute_surface_origin_for_output(const struct as_state *state, int output_w, int output_h, int *x_out, int *y_out)
{
	if (x_out != NULL)
		*x_out = 0;
	if (y_out != NULL)
		*y_out = 0;
	if (state == NULL)
		return false;

	int ow = output_w;
	int oh = output_h;
	if (ow <= 0 || oh <= 0)
		return false;

	int w = state->width;
	int h = state->height;
	if (w <= 0 || h <= 0)
		return false;

	uint32_t anchor = as_state_anchor_flags(state);

	int x = 0;
	if (as_anchor_spans_horizontal(anchor) || (anchor & ASWL_ANCHOR_LEFT) != 0) {
		x = state->margins.left;
	} else if ((anchor & ASWL_ANCHOR_RIGHT) != 0) {
		x = ow - state->margins.right - w;
	} else {
		x = (ow - w) / 2;
	}

	int y = 0;
	if (as_anchor_spans_vertical(anchor) || (anchor & ASWL_ANCHOR_TOP) != 0) {
		y = state->margins.top;
	} else if ((anchor & ASWL_ANCHOR_BOTTOM) != 0) {
		y = oh - state->margins.bottom - h;
	} else {
		y = (oh - h) / 2;
	}

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	if (x_out != NULL)
		*x_out = x;
	if (y_out != NULL)
		*y_out = y;
	return true;
}

bool as_state_compute_surface_origin(const struct as_state *state, int *x_out, int *y_out)
{
	if (state == NULL) {
		if (x_out != NULL)
			*x_out = 0;
		if (y_out != NULL)
			*y_out = 0;
		return false;
	}

	return as_state_compute_surface_origin_for_output(state, state->output_width, state->output_height, x_out, y_out);
}

int as_state_exclusive_zone(const struct as_state *state)
{
	if (state == NULL)
		return 0;
	if (state->exclusive_zone_override_set)
		return state->exclusive_zone_override;
	return as_panel_edge_is_vertical(state->edge) ? state->width : state->height;
}

int as_state_calc_dock_main_axis_size(const struct as_state *state)
{
	if (state == NULL)
		return 0;
	if (!state->dock_mode)
		return 0;
	if (state->button_count == 0)
		return 0;

	/* Keep in sync with as_state_get_layout() for dock mode. */
	const int pad = 0;
	const int spacing = 0;
	const int dock_tile = 64;
	const int max_gutter = 8;

	int cross_raw = 0;
	if (as_panel_edge_is_vertical(state->edge)) {
		cross_raw = state->width - 2 * pad;
	} else {
		cross_raw = state->height - 2 * pad;
	}

	if (cross_raw < 0)
		cross_raw = 0;

	int cross = cross_raw;
	int gutter = cross_raw - dock_tile;
	if (gutter > 0 && gutter <= max_gutter)
		cross = dock_tile;

	return 2 * pad + (int)state->button_count * cross + (int)(state->button_count - 1) * spacing;
}

static void as_window_destroy(struct as_window *win)
{
	if (win == NULL)
		return;
	free(win->title);
	free(win->app_id);
	*win = (struct as_window){ 0 };
}

void as_state_clear_windows(struct as_state *state)
{
	if (state == NULL)
		return;
	for (size_t i = 0; i < state->window_count; i++)
		as_window_destroy(&state->windows[i]);
	state->window_count = 0;
}

void as_state_destroy_windows(struct as_state *state)
{
	if (state == NULL)
		return;
	as_state_clear_windows(state);
	free(state->windows);
	state->windows = NULL;
	state->window_cap = 0;
}

static struct as_window *as_state_find_window(struct as_state *state, uint32_t id)
{
	if (state == NULL || id == 0)
		return NULL;
	for (size_t i = 0; i < state->window_count; i++) {
		if (state->windows[i].id == id)
			return &state->windows[i];
	}
	return NULL;
}

bool as_state_upsert_window(struct as_state *state,
                           uint32_t id,
                           uint32_t workspace,
                           uint32_t flags,
                           const char *title,
                           const char *app_id)
{
	if (state == NULL || id == 0)
		return false;

	struct as_window *win = as_state_find_window(state, id);
	if (win == NULL) {
		if (state->window_count == state->window_cap) {
			size_t next = state->window_cap == 0 ? 16 : state->window_cap * 2;
			struct as_window *tmp = realloc(state->windows, next * sizeof(*tmp));
			if (tmp == NULL)
				return false;
			state->windows = tmp;
			state->window_cap = next;
		}

		win = &state->windows[state->window_count++];
		*win = (struct as_window){ 0 };
		win->id = id;
	}

	win->workspace = workspace;
	win->flags = flags;

	char *new_title = strdup(title != NULL ? title : "");
	char *new_app_id = strdup(app_id != NULL ? app_id : "");
	if (new_title == NULL || new_app_id == NULL) {
		free(new_title);
		free(new_app_id);
		return false;
	}

	free(win->title);
	free(win->app_id);
	win->title = new_title;
	win->app_id = new_app_id;
	return true;
}

bool as_state_upsert_window_geometry(struct as_state *state, uint32_t id, int x, int y, int w, int h)
{
	if (state == NULL || id == 0)
		return false;

	struct as_window *win = as_state_find_window(state, id);
	if (win == NULL) {
		if (state->window_count == state->window_cap) {
			size_t next = state->window_cap == 0 ? 16 : state->window_cap * 2;
			struct as_window *tmp = realloc(state->windows, next * sizeof(*tmp));
			if (tmp == NULL)
				return false;
			state->windows = tmp;
			state->window_cap = next;
		}

		win = &state->windows[state->window_count++];
		*win = (struct as_window){ 0 };
		win->id = id;

		win->title = strdup("");
		win->app_id = strdup("");
		if (win->title == NULL || win->app_id == NULL) {
			free(win->title);
			free(win->app_id);
			win->title = NULL;
			win->app_id = NULL;
			state->window_count--;
			return false;
		}
	}

	win->x = x;
	win->y = y;
	win->w = w;
	win->h = h;
	return true;
}

bool as_state_remove_window(struct as_state *state, uint32_t id)
{
	if (state == NULL || id == 0)
		return false;

	for (size_t i = 0; i < state->window_count; i++) {
		if (state->windows[i].id != id)
			continue;
		as_window_destroy(&state->windows[i]);
		if (i + 1 < state->window_count)
			memmove(&state->windows[i], &state->windows[i + 1], (state->window_count - i - 1) * sizeof(state->windows[0]));
		state->window_count--;
		return true;
	}
	return false;
}

bool as_state_get_layout(struct as_state *state, struct as_layout *layout)
{
	if (state == NULL || layout == NULL)
		return false;

	layout->vertical = as_panel_edge_is_vertical(state->edge);

	if (state->dock_mode) {
		layout->pad = 0;
		layout->spacing = 0;
	} else if (state->pager_mode && layout->vertical) {
		/* Pager should match X11's tight desk borders (no outer padding). */
		layout->pad = 0;
		layout->spacing = 0;
	} else {
		layout->pad = 6;
		layout->spacing = 6;
	}

	int cross_raw = layout->vertical ? (state->width - 2 * layout->pad) : (state->height - 2 * layout->pad);
	if (cross_raw <= 0)
		return false;

	layout->cross = cross_raw;
	layout->dock_gutter = 0;
	if (state->dock_mode) {
		const int dock_tile = 64;
		const int max_gutter = 8;
		int gutter = cross_raw - dock_tile;
		if (gutter > 0 && gutter <= max_gutter) {
			layout->cross = dock_tile;
			layout->dock_gutter = gutter;
		}
	}

	layout->row = layout->cross;
	if (layout->vertical && !state->dock_mode) {
		int row = state->item_height > 0 ? state->item_height : 48;
		layout->row = clamp_int(row, 24, 256);
	}

	/* Dock mode should match Wharf semantics: center icons at native size within 64px tiles (no forced 48px downscale). */
	layout->icon_pad = state->dock_mode ? 0 : 4;
	int icon_dim = layout->cross;
	if (layout->vertical && !state->dock_mode)
		icon_dim = layout->row;
	layout->icon_size = icon_dim - 2 * layout->icon_pad;
	int max_icon = layout->cross - 2 * layout->icon_pad;
	if (layout->icon_size > max_icon)
		layout->icon_size = max_icon;
	if (layout->icon_size < 0)
		layout->icon_size = 0;

	layout->text_gap = state->dock_mode ? 0 : 8;
	int text_dim = layout->cross;
	if (layout->vertical && !state->dock_mode)
		text_dim = layout->row;
	if (state->font.use_freetype && state->font.base_px > 0)
		layout->text_scale = 1;
	else
		layout->text_scale = clamp_int((text_dim - 24) / 7, 1, 3);
	layout->icon_scale = clamp_int((layout->icon_size - 2) / 7, 1, 6);
	layout->max_main = state->dock_mode ? layout->cross : 260;
	if (!state->dock_mode && !layout->vertical && state->button_count == 0 &&
	    (state->window_list_focused_only || state->window_list_topmost_only)) {
		int max = state->width - 2 * layout->pad;
		if (max > layout->cross)
			layout->max_main = max;
	}

	(void)aswl_font_set_scale(&state->font, layout->text_scale);
	layout->text_h = aswl_font_height(&state->font);
	return true;
}

bool as_command_parse_workspace_target(const char *command, uint32_t *workspace_out)
{
	if (workspace_out != NULL)
		*workspace_out = 0;

	if (command == NULL || command[0] == '\0')
		return false;
	if (command[0] != '@')
		return false;

	const char *action = command + 1;
	const char *ws_arg = NULL;
	if (strncmp(action, "workspace", 9) == 0) {
		ws_arg = action + 9;
	} else if (strncmp(action, "ws", 2) == 0) {
		ws_arg = action + 2;
	} else {
		return false;
	}

	while (*ws_arg == ':' || *ws_arg == '=' || isspace((unsigned char)*ws_arg))
		ws_arg++;

	if (*ws_arg == '\0')
		return false;

	char *end = NULL;
	unsigned long ws = strtoul(ws_arg, &end, 10);
	while (end != NULL && isspace((unsigned char)*end))
		end++;

	if (end == ws_arg || end == NULL || *end != '\0' || ws < 1 || ws > 1000)
		return false;

	if (workspace_out != NULL)
		*workspace_out = (uint32_t)ws;
	return true;
}

int as_state_button_main_size(struct as_state *state, const struct as_layout *layout, size_t idx)
{
	if (state == NULL || layout == NULL)
		return 0;
	if (idx >= state->button_count)
		return 0;

	if (state->dock_mode)
		return layout->cross;

	if (layout->vertical) {
		return layout->row;
	}

	int w = layout->cross;

	(void)aswl_font_set_scale(&state->font, layout->text_scale);
	int label_w = aswl_font_text_width(&state->font, state->buttons[idx].label);
	if (label_w > 0)
		w = layout->icon_pad + layout->icon_size + layout->text_gap + label_w + layout->icon_pad;
	else
		w = layout->icon_pad + layout->icon_size + layout->icon_pad;

	if (w < layout->cross)
		w = layout->cross;
	if (w > layout->max_main)
		w = layout->max_main;

	return w;
}

static bool as_window_visible(const struct as_state *state, const struct as_window *win)
{
	if (state == NULL || win == NULL)
		return false;
	if ((win->flags & ASWL_WINDOW_FLAG_MAPPED) == 0)
		return false;
	/*
	 * Match AfterStep's default style database: module windows (WM_CLASS=ASModule)
	 * and internal suite popups (afterstep.aswl*) are skipped by WinList/window
	 * lists.
	 */
	if (win->app_id != NULL) {
		if (strcmp(win->app_id, "ASModule") == 0)
			return false;
		if (strncmp(win->app_id, "afterstep.aswl", 13) == 0)
			return false;
	}
	if (state->window_list_focused_only && (win->flags & ASWL_WINDOW_FLAG_FOCUSED) == 0)
		return false;
	return win->workspace == state->current_workspace;
}

const char *as_window_label(const struct as_window *win)
{
	if (win == NULL)
		return "Window";
	if (win->title != NULL && win->title[0] != '\0')
		return win->title;
	if (win->app_id != NULL && win->app_id[0] != '\0')
		return win->app_id;
	return "Window";
}

int as_state_window_main_size(struct as_state *state, const struct as_layout *layout, const struct as_window *win)
{
	if (state == NULL || layout == NULL || win == NULL)
		return 0;

	if (layout->vertical)
		return layout->text_h + 2 * layout->icon_pad;

	if ((state->window_list_focused_only || state->window_list_topmost_only) && state->button_count == 0) {
		/*
		 * The classic AfterStep WinList strip is a single, full-width frame with
		 * a single window title right-aligned. Match that look by forcing the
		 * entry to span the available main axis.
		 */
		return layout->max_main;
	}

	const char *label = as_window_label(win);
	(void)aswl_font_set_scale(&state->font, layout->text_scale);
	int label_w = aswl_font_text_width(&state->font, label);
	int w = layout->icon_pad + label_w + layout->icon_pad;

	if (w < layout->cross)
		w = layout->cross;
	if (w > layout->max_main)
		w = layout->max_main;

	return w;
}

struct as_window *as_state_visible_window_nth(struct as_state *state, size_t n)
{
	if (state == NULL)
		return NULL;

	if (state->window_list_topmost_only && state->button_count == 0) {
		if (n != 0)
			return NULL;
		for (size_t i = state->window_count; i-- > 0;) {
			if (as_window_visible(state, &state->windows[i]))
				return &state->windows[i];
		}
		return NULL;
	}

	size_t idx = 0;
	for (size_t i = 0; i < state->window_count; i++) {
		if (!as_window_visible(state, &state->windows[i]))
			continue;
		if (idx++ == n)
			return &state->windows[i];
	}
	return NULL;
}

int as_state_hit_test(struct as_state *state, int x, int y)
{
	if (state == NULL)
		return -1;

	struct as_layout layout;
	if (!as_state_get_layout(state, &layout))
		return -1;

	int rx = layout.pad;
	int ry = layout.pad;

	if (state->pager_mode && layout.vertical && !state->dock_mode) {
		size_t ws_count = 0;
		size_t nav_count = 0;
		for (size_t i = 0; i < state->button_count; i++) {
			if (as_command_parse_workspace_target(state->buttons[i].command, NULL))
				ws_count++;
			else
				nav_count++;
		}

		int nav_h = clamp_int(state->item_height > 0 ? state->item_height : 48, 24, 128);
		int nav_total = (int)nav_count * nav_h + (nav_count > 0 ? (int)(nav_count - 1) * layout.spacing : 0);
		int nav_y0 = state->height - layout.pad - nav_total;

		int workspace_area_h = nav_y0 - layout.pad;
		if (ws_count > 0 && nav_count > 0)
			workspace_area_h -= layout.spacing;

		int tile_h = 0;
		if (ws_count > 0)
			tile_h = (workspace_area_h - (int)(ws_count - 1) * layout.spacing) / (int)ws_count;
		if (tile_h < 24)
			tile_h = 24;

		for (size_t i = 0; i < state->button_count; i++) {
			bool is_ws = as_command_parse_workspace_target(state->buttons[i].command, NULL);
			int bx = layout.pad;
			int by = layout.pad;
			int bw = layout.cross;
			int bh = nav_h;

			if (is_ws && tile_h > 0) {
				size_t pos = 0;
				for (size_t j = 0; j < i; j++) {
					if (as_command_parse_workspace_target(state->buttons[j].command, NULL))
						pos++;
				}
				by = layout.pad + (int)pos * (tile_h + layout.spacing);
				bh = tile_h;
			} else if (!is_ws) {
				size_t pos = 0;
				for (size_t j = 0; j < i; j++) {
					if (!as_command_parse_workspace_target(state->buttons[j].command, NULL))
						pos++;
				}
				by = nav_y0 + (int)pos * (nav_h + layout.spacing);
				bh = nav_h;
			} else {
				bh = nav_h;
			}

			if (x >= bx && x < bx + bw && y >= by && y < by + bh)
				return (int)i;
		}

		return -1;
	}

	for (size_t i = 0; i < state->button_count; i++) {
		int main = as_state_button_main_size(state, &layout, i);
		int rw = layout.vertical ? layout.cross : main;
		int rh = layout.vertical ? main : layout.cross;

		if (x >= rx && x < rx + rw && y >= ry && y < ry + rh)
			return (int)i;

		if (layout.vertical)
			ry += rh + layout.spacing;
		else
			rx += rw + layout.spacing;
	}

	if (state->dock_mode)
		return -1;

	int wx = layout.vertical ? layout.pad : rx;
	int wy = layout.vertical ? (ry + layout.spacing) : layout.pad;
	int ww = 0;
	int wh = 0;
	size_t vis_idx = 0;
	for (;;) {
		struct as_window *win = as_state_visible_window_nth(state, vis_idx);
		if (win == NULL)
			break;

		int main = as_state_window_main_size(state, &layout, win);
		if (layout.vertical) {
			ww = layout.cross;
			wh = main;
			if (wy + wh > state->height - layout.pad)
				break;
		} else {
			ww = main;
			wh = layout.cross;
			if (wx + ww > state->width - layout.pad)
				break;
		}

		if (x >= wx && x < wx + ww && y >= wy && y < wy + wh)
			return (int)state->button_count + (int)vis_idx;

		if (layout.vertical)
			wy += wh + layout.spacing;
		else
			wx += ww + layout.spacing;
		vis_idx++;
	}

	return -1;
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

void as_state_launch_command(struct as_state *state, const char *command)
{
	if (command == NULL || command[0] == '\0')
		return;

	if (state != NULL && state->control != NULL) {
		if (command[0] == '@') {
			const char *action = command + 1;
			if (strcmp(action, "quit") == 0 || strcmp(action, "exit") == 0) {
				fprintf(stderr, "aswlpanel: compositor quit\n");
				afterstep_control_v1_quit(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "close") == 0 || strcmp(action, "close_focused") == 0) {
				fprintf(stderr, "aswlpanel: compositor close_focused\n");
				afterstep_control_v1_close_focused(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "focus_next") == 0 || strcmp(action, "next") == 0) {
				fprintf(stderr, "aswlpanel: compositor focus_next\n");
				afterstep_control_v1_focus_next(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "focus_prev") == 0 || strcmp(action, "prev") == 0) {
				fprintf(stderr, "aswlpanel: compositor focus_prev\n");
				afterstep_control_v1_focus_prev(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if ((strcmp(action, "fullscreen") == 0 || strcmp(action, "toggle_fullscreen") == 0) &&
			    state->control_version >= 5) {
				fprintf(stderr, "aswlpanel: compositor toggle_fullscreen\n");
				afterstep_control_v1_toggle_fullscreen(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if ((strcmp(action, "maximize") == 0 || strcmp(action, "maximized") == 0 ||
			     strcmp(action, "toggle_maximized") == 0 || strcmp(action, "toggle_maximize") == 0) &&
			    state->control_version >= 5) {
				fprintf(stderr, "aswlpanel: compositor toggle_maximized\n");
				afterstep_control_v1_toggle_maximized(state->control);
				(void)wl_display_flush(state->display);
				return;
			}

			if ((strcmp(action, "workspace_next") == 0 || strcmp(action, "ws_next") == 0 || strcmp(action, "ws+") == 0) &&
			    state->control_version >= 2) {
				fprintf(stderr, "aswlpanel: compositor workspace_next\n");
				afterstep_control_v1_workspace_next(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if ((strcmp(action, "workspace_prev") == 0 || strcmp(action, "ws_prev") == 0 || strcmp(action, "ws-") == 0) &&
			    state->control_version >= 2) {
				fprintf(stderr, "aswlpanel: compositor workspace_prev\n");
				afterstep_control_v1_workspace_prev(state->control);
				(void)wl_display_flush(state->display);
				return;
			}

			const char *ws_arg = NULL;
			if (strncmp(action, "workspace", 9) == 0) {
				ws_arg = action + 9;
			} else if (strncmp(action, "ws", 2) == 0) {
				ws_arg = action + 2;
			}
			if (ws_arg != NULL) {
				while (*ws_arg == ':' || *ws_arg == '=' || isspace((unsigned char)*ws_arg))
					ws_arg++;

				if (*ws_arg != '\0' && state->control_version >= 2) {
					char *end = NULL;
					unsigned long ws = strtoul(ws_arg, &end, 10);
					while (end != NULL && isspace((unsigned char)*end))
						end++;

					if (end != ws_arg && end != NULL && *end == '\0' && ws >= 1 && ws <= 1000) {
						fprintf(stderr, "aswlpanel: compositor set_workspace=%lu\n", ws);
						afterstep_control_v1_set_workspace(state->control, (uint32_t)ws);
						(void)wl_display_flush(state->display);
						return;
					}
				}
			}
		}

		fprintf(stderr, "aswlpanel: compositor exec %s\n", command);
		afterstep_control_v1_exec(state->control, command);
		(void)wl_display_flush(state->display);
		return;
	}

	spawn_command(command);
}
