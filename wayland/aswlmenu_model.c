#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlmenu_internal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int clamp_int(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static int env_int(const char *name, int def, int lo, int hi)
{
	const char *s = name != NULL ? getenv(name) : NULL;
	if (s != NULL && s[0] != '\0') {
		char *end = NULL;
		long v = strtol(s, &end, 10);
		if (end != s && *end == '\0')
			def = (int)v;
	}
	return clamp_int(def, lo, hi);
}

bool menu_command_is_submenu(const char *command)
{
	if (command == NULL)
		return false;
	if (command[0] != '@')
		return false;

	const char *action = command + 1;
	while (*action != '\0' && isspace((unsigned char)*action))
		action++;

	if (strncmp(action, "submenu", 6) != 0)
		return false;

	char c = action[6];
	return c == '\0' || isspace((unsigned char)c) || c == ':' || c == '=';
}

void as_state_filter_clear_silent(struct as_state *state)
{
	if (state == NULL)
		return;
	if (state->filter != NULL)
		state->filter[0] = '\0';
	state->filter_len = 0;
}

void as_state_menu_stack_clear(struct as_state *state)
{
	if (state == NULL)
		return;

	for (size_t i = 0; i < state->menu_stack_len; i++) {
		free(state->menu_stack[i].section);
		free(state->menu_stack[i].title);
		free(state->menu_stack[i].filter);
	}
	free(state->menu_stack);
	state->menu_stack = NULL;
	state->menu_stack_len = 0;
	state->menu_stack_cap = 0;
}

bool as_state_menu_stack_push(struct as_state *state)
{
	if (state == NULL)
		return false;

	if (state->menu_stack_len == state->menu_stack_cap) {
		size_t next = state->menu_stack_cap == 0 ? 8 : state->menu_stack_cap * 2;
		struct as_menu_stack_entry *tmp = realloc(state->menu_stack, next * sizeof(*tmp));
		if (tmp == NULL)
			return false;
		state->menu_stack = tmp;
		state->menu_stack_cap = next;
	}

	struct as_menu_stack_entry ent = { 0 };
	ent.section = state->menu_section;
	state->menu_section = NULL;
	ent.title = state->title;
	state->title = NULL;
	ent.selected_index = state->selected_index;
	ent.scroll = state->scroll;
	if (state->filter != NULL && state->filter_len > 0)
		ent.filter = strdup(state->filter);

	state->menu_stack[state->menu_stack_len++] = ent;
	return true;
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

static void as_state_launch_command(struct as_state *state, const char *command)
{
	if (command == NULL || command[0] == '\0')
		return;

	if (menu_command_is_submenu(command))
		return;

	if (state != NULL && state->control != NULL) {
		if (command[0] == '@') {
			const char *action = command + 1;
			if (strcmp(action, "quit") == 0 || strcmp(action, "exit") == 0) {
				fprintf(stderr, "aswlmenu: compositor quit\n");
				afterstep_control_v1_quit(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "close") == 0 || strcmp(action, "close_focused") == 0) {
				fprintf(stderr, "aswlmenu: compositor close_focused\n");
				afterstep_control_v1_close_focused(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "focus_next") == 0 || strcmp(action, "next") == 0) {
				fprintf(stderr, "aswlmenu: compositor focus_next\n");
				afterstep_control_v1_focus_next(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "focus_prev") == 0 || strcmp(action, "prev") == 0) {
				fprintf(stderr, "aswlmenu: compositor focus_prev\n");
				afterstep_control_v1_focus_prev(state->control);
				(void)wl_display_flush(state->display);
				return;
			}

			const char *id_arg = NULL;
			if (strncmp(action, "focus_window", 11) == 0) {
				id_arg = action + 11;
			} else if (strncmp(action, "focus", 5) == 0) {
				/* Avoid matching focus_next/focus_prev. */
				if (action[5] != '_')
					id_arg = action + 5;
			}
			if (id_arg != NULL) {
				while (*id_arg == ':' || *id_arg == '=' || isspace((unsigned char)*id_arg))
					id_arg++;

				if (state->control_version < 4) {
					fprintf(stderr, "aswlmenu: focus_window requires control protocol v4\n");
					return;
				}

				char *end = NULL;
				unsigned long id = strtoul(id_arg, &end, 10);
				while (end != NULL && isspace((unsigned char)*end))
					end++;

				if (end != id_arg && end != NULL && *end == '\0' && id >= 1 && id <= UINT32_MAX) {
					fprintf(stderr, "aswlmenu: compositor focus_window=%lu\n", id);
					afterstep_control_v1_focus_window(state->control, (uint32_t)id);
					(void)wl_display_flush(state->display);
					return;
				}

				fprintf(stderr, "aswlmenu: invalid focus_window id: %s\n", id_arg);
				return;
			}
			if ((strcmp(action, "fullscreen") == 0 || strcmp(action, "toggle_fullscreen") == 0) &&
			    state->control_version >= 5) {
				fprintf(stderr, "aswlmenu: compositor toggle_fullscreen\n");
				afterstep_control_v1_toggle_fullscreen(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if ((strcmp(action, "maximize") == 0 || strcmp(action, "maximized") == 0 ||
			     strcmp(action, "toggle_maximized") == 0 || strcmp(action, "toggle_maximize") == 0) &&
			    state->control_version >= 5) {
				fprintf(stderr, "aswlmenu: compositor toggle_maximized\n");
				afterstep_control_v1_toggle_maximized(state->control);
				(void)wl_display_flush(state->display);
				return;
			}

			if ((strcmp(action, "workspace_next") == 0 || strcmp(action, "ws_next") == 0 || strcmp(action, "ws+") == 0) &&
			    state->control_version >= 2) {
				fprintf(stderr, "aswlmenu: compositor workspace_next\n");
				afterstep_control_v1_workspace_next(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if ((strcmp(action, "workspace_prev") == 0 || strcmp(action, "ws_prev") == 0 || strcmp(action, "ws-") == 0) &&
			    state->control_version >= 2) {
				fprintf(stderr, "aswlmenu: compositor workspace_prev\n");
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
						fprintf(stderr, "aswlmenu: compositor set_workspace=%lu\n", ws);
						afterstep_control_v1_set_workspace(state->control, (uint32_t)ws);
						(void)wl_display_flush(state->display);
						return;
					}
				}
			}
		}

		fprintf(stderr, "aswlmenu: compositor exec %s\n", command);
		afterstep_control_v1_exec(state->control, command);
		(void)wl_display_flush(state->display);
		return;
	}

	spawn_command(command);
}

bool as_state_go_back(struct as_state *state)
{
	if (state == NULL)
		return false;
	if (state->menu_stack_len == 0)
		return false;

	struct as_menu_stack_entry ent = state->menu_stack[--state->menu_stack_len];

	free(state->menu_section);
	state->menu_section = ent.section;
	ent.section = NULL;

	free(state->title);
	state->title = ent.title;
	ent.title = NULL;

	int restore_selected = ent.selected_index;
	int restore_scroll = ent.scroll;
	char *restore_filter = ent.filter;
	ent.filter = NULL;

	bool ok = as_state_reload_menu_from_config(state);
	if (ok) {
		if (restore_filter != NULL)
			(void)as_state_filter_set(state, restore_filter);

		if (state->filtered_count > 0) {
			if (restore_selected < 0)
				restore_selected = 0;
			if ((size_t)restore_selected >= state->filtered_count)
				restore_selected = (int)(state->filtered_count - 1);

			if (restore_scroll < 0)
				restore_scroll = 0;
			if ((size_t)restore_scroll > state->filtered_count)
				restore_scroll = 0;

			state->selected_index = restore_selected;
			state->scroll = restore_scroll;
			as_state_ensure_selection_visible(state);
		}
	}

	free(restore_filter);
	free(ent.section);
	free(ent.title);
	free(ent.filter);
	return ok;
}

static void as_state_open_submenu(struct as_state *state, size_t entry_idx)
{
	if (state == NULL)
		return;
	if (entry_idx >= state->entry_count)
		return;
	if (state->menu_config_path == NULL || state->menu_config_path[0] == '\0')
		return;

	const struct as_menu_entry *e = &state->entries[entry_idx];
	char *target = menu_command_submenu_target(e->command, e->label);
	if (target == NULL)
		return;

	if (!menu_config_section_exists(state->menu_config_path, target)) {
		fprintf(stderr, "aswlmenu: submenu section not found: %s\n", target);
		free(target);
		return;
	}

	char *submenu_title = NULL;
	if (e->label != NULL && e->label[0] != '\0')
		submenu_title = strdup(e->label);
	if (submenu_title == NULL)
		submenu_title = strdup(target);
	if (submenu_title == NULL) {
		free(target);
		return;
	}

	if (!as_state_menu_stack_push(state)) {
		free(submenu_title);
		free(target);
		return;
	}

	free(state->menu_section);
	state->menu_section = target;
	target = NULL;

	free(state->title);
	state->title = submenu_title;
	submenu_title = NULL;

	state->include_desktop_entries = false;
	if (!as_state_reload_menu_from_config(state)) {
		fprintf(stderr, "aswlmenu: failed to load submenu '%s'\n",
		        state->menu_section != NULL ? state->menu_section : "(null)");
		(void)as_state_go_back(state);
	}
}

void as_state_activate_entry(struct as_state *state, size_t entry_idx)
{
	if (state == NULL)
		return;
	if (entry_idx >= state->entry_count)
		return;

	const char *cmd = state->entries[entry_idx].command;
	if (menu_command_is_submenu(cmd)) {
		const char *label = state->entries[entry_idx].label != NULL ? state->entries[entry_idx].label : "(null)";
		fprintf(stderr, "aswlmenu: open submenu %s\n", label);
		as_state_open_submenu(state, entry_idx);
		return;
	}

	const char *label = state->entries[entry_idx].label != NULL ? state->entries[entry_idx].label : "(null)";
	const char *command = state->entries[entry_idx].command != NULL ? state->entries[entry_idx].command : "(null)";
	fprintf(stderr, "aswlmenu: launch %s: %s\n",
	        label, command);
	as_state_launch_command(state, cmd);
	if (!(state->window_list_mode && state->pinned_open))
		state->running = false;
}

static bool str_case_contains(const char *haystack, const char *needle)
{
	if (haystack == NULL || needle == NULL)
		return false;
	if (needle[0] == '\0')
		return true;

	for (size_t i = 0; haystack[i] != '\0'; i++) {
		size_t j = 0;
		while (needle[j] != '\0' && haystack[i + j] != '\0') {
			char a = (char)tolower((unsigned char)haystack[i + j]);
			char b = (char)tolower((unsigned char)needle[j]);
			if (a != b)
				break;
			j++;
		}
		if (needle[j] == '\0')
			return true;
	}
	return false;
}

static void as_menu_entry_destroy_icon(struct as_menu_entry *e)
{
	if (e == NULL)
		return;
	free(e->icon_argb);
	e->icon_argb = NULL;
	e->icon_w = 0;
	e->icon_h = 0;
	e->icon_tried = false;
}

void as_state_free_entries(struct as_state *state)
{
	if (state == NULL)
		return;
	for (size_t i = 0; i < state->entry_count; i++) {
		free(state->entries[i].label);
		free(state->entries[i].icon_spec);
		free(state->entries[i].command);
		as_menu_entry_destroy_icon(&state->entries[i]);
	}
	free(state->entries);
	state->entries = NULL;
	state->entry_count = 0;
	state->entry_cap = 0;
	state->pinned_count = 0;
}

bool as_state_append_entry(struct as_state *state, const char *label, const char *command, const char *icon_spec, bool pinned)
{
	if (state == NULL || label == NULL || label[0] == '\0' || command == NULL || command[0] == '\0')
		return false;

	if (state->entry_count == state->entry_cap) {
		size_t next = state->entry_cap == 0 ? 64 : state->entry_cap * 2;
		struct as_menu_entry *tmp = realloc(state->entries, next * sizeof(*tmp));
		if (tmp == NULL)
			return false;
		state->entries = tmp;
		state->entry_cap = next;
	}

	state->entries[state->entry_count] = (struct as_menu_entry){ 0 };
	state->entries[state->entry_count].label = strdup(label);
	state->entries[state->entry_count].command = strdup(command);
	if (icon_spec != NULL && icon_spec[0] != '\0')
		state->entries[state->entry_count].icon_spec = strdup(icon_spec);
	state->entries[state->entry_count].pinned = pinned;
	if (state->entries[state->entry_count].label == NULL ||
	    state->entries[state->entry_count].command == NULL ||
	    (icon_spec != NULL && icon_spec[0] != '\0' && state->entries[state->entry_count].icon_spec == NULL)) {
		free(state->entries[state->entry_count].label);
		free(state->entries[state->entry_count].icon_spec);
		free(state->entries[state->entry_count].command);
		state->entries[state->entry_count] = (struct as_menu_entry){ 0 };
		return false;
	}

	state->entry_count++;
	if (pinned)
		state->pinned_count++;
	return true;
}

void as_state_free_filtered(struct as_state *state)
{
	if (state == NULL)
		return;
	free(state->filtered);
	state->filtered = NULL;
	state->filtered_count = 0;
	state->filtered_cap = 0;
}

static bool as_state_ensure_filtered(struct as_state *state, size_t cap)
{
	if (state == NULL)
		return false;
	if (cap <= state->filtered_cap)
		return true;

	size_t next = state->filtered_cap == 0 ? 128 : state->filtered_cap;
	while (next < cap)
		next *= 2;

	size_t *tmp = realloc(state->filtered, next * sizeof(*tmp));
	if (tmp == NULL)
		return false;
	state->filtered = tmp;
	state->filtered_cap = next;
	return true;
}

void as_state_rebuild_filtered(struct as_state *state)
{
	if (state == NULL)
		return;

	state->filtered_count = 0;
	if (!as_state_ensure_filtered(state, state->entry_count))
		return;

	const char *needle = state->filter != NULL ? state->filter : "";
	for (size_t i = 0; i < state->entry_count; i++) {
		const struct as_menu_entry *e = &state->entries[i];
		if (needle[0] == '\0' || str_case_contains(e->label, needle) || str_case_contains(e->command, needle)) {
			state->filtered[state->filtered_count++] = i;
		}
	}

	if (state->filtered_count == 0) {
		state->selected_index = -1;
		state->hover_index = -1;
		state->pressed_index = -1;
		state->scroll = 0;
	} else {
		if (state->selected_index < 0)
			state->selected_index = 0;
		if ((size_t)state->selected_index >= state->filtered_count)
			state->selected_index = (int)(state->filtered_count - 1);
		if (state->scroll < 0)
			state->scroll = 0;
		if ((size_t)state->scroll > state->filtered_count)
			state->scroll = 0;
	}

	schedule_redraw(state);
}

bool as_state_filter_set(struct as_state *state, const char *text)
{
	if (state == NULL)
		return false;
	if (text == NULL)
		text = "";

	size_t n = strlen(text);
	if (n + 1 > state->filter_cap) {
		size_t next = state->filter_cap == 0 ? 64 : state->filter_cap;
		while (next < n + 1)
			next *= 2;
		char *tmp = realloc(state->filter, next);
		if (tmp == NULL)
			return false;
		state->filter = tmp;
		state->filter_cap = next;
	}

	memcpy(state->filter, text, n + 1);
	state->filter_len = n;
	as_state_rebuild_filtered(state);
	return true;
}

bool as_state_filter_append_utf8(struct as_state *state, const char *utf8)
{
	if (state == NULL || utf8 == NULL || utf8[0] == '\0')
		return false;

	size_t add = strlen(utf8);
	if (state->filter_len + add + 1 > 256)
		return false;

	if (state->filter_len + add + 1 > state->filter_cap) {
		size_t next = state->filter_cap == 0 ? 64 : state->filter_cap;
		while (next < state->filter_len + add + 1)
			next *= 2;
		char *tmp = realloc(state->filter, next);
		if (tmp == NULL)
			return false;
		state->filter = tmp;
		state->filter_cap = next;
	}

	memcpy(state->filter + state->filter_len, utf8, add);
	state->filter_len += add;
	state->filter[state->filter_len] = '\0';
	as_state_rebuild_filtered(state);
	return true;
}

void as_state_filter_backspace(struct as_state *state)
{
	if (state == NULL || state->filter == NULL || state->filter_len == 0)
		return;

	/* Remove last UTF-8 byte sequence (best-effort). */
	size_t i = state->filter_len;
	while (i > 0 && ((unsigned char)state->filter[i - 1] & 0xC0u) == 0x80u)
		i--;
	if (i == 0)
		i = state->filter_len - 1;
	state->filter[i] = '\0';
	state->filter_len = i;
	as_state_rebuild_filtered(state);
}

bool as_state_get_layout(struct as_state *state, struct as_menu_layout *layout)
{
	if (state == NULL || layout == NULL)
		return false;

	layout->pad = state->window_list_mode ? 1 : 10;
	layout->text_scale = 2;
	if ((state->font.use_freetype && state->font.base_px > 0) ||
	    (state->header_font.use_freetype && state->header_font.base_px > 0) ||
	    (state->hilite_font.use_freetype && state->hilite_font.base_px > 0))
		layout->text_scale = 1;
	layout->help_scale = 1;

	(void)aswl_font_set_scale(&state->font, layout->text_scale);
	(void)aswl_font_set_scale(&state->header_font, layout->text_scale);
	(void)aswl_font_set_scale(&state->hilite_font, layout->text_scale);

	int item_text_h = aswl_font_height(&state->font);
	int header_text_h = aswl_font_height(&state->header_font);
	if (header_text_h <= 0)
		header_text_h = item_text_h;

	int row_text_h = item_text_h;
	if (!state->window_list_mode) {
		int hilite_text_h = aswl_font_height(&state->hilite_font);
		if (hilite_text_h > row_text_h)
			row_text_h = hilite_text_h;
	}

	/* window-list mode: +1px over the bare text so the raised title bevel
	 * (drawn in aswlmenu_render) has room and the bar height matches X11. */
	int header_vpad = state->window_list_mode ? 2 : 8;
	int row_vpad = state->window_list_mode ? 1 : 6;
	layout->header_h = header_text_h + 2 * header_vpad;
	layout->row_h = row_text_h + 2 * row_vpad;
	int min_row_extra = state->window_list_mode ? 2 : 4;
	if (layout->row_h < row_text_h + min_row_extra)
		layout->row_h = row_text_h + min_row_extra;

	layout->icon_size = layout->row_h - 8;
	if (layout->icon_size < 0)
		layout->icon_size = 0;
	if (layout->icon_size > 64)
		layout->icon_size = 64;
	layout->icon_col_w = layout->icon_size > 0 ? layout->icon_size + 10 : 0;

	/* The WinList-ish "Windows on Desktop N" popup is typically text-only. */
	if (state->window_list_mode) {
		layout->icon_size = 0;
		layout->icon_col_w = 0;
	}

	return true;
}

void as_state_update_close_button_metrics(struct as_state *state, const struct as_menu_layout *layout)
{
	if (state == NULL || layout == NULL)
		return;

	state->close_x = 0;
	state->close_y = 0;
	state->close_w = 0;
	state->close_h = 0;

	/*
	 * Menus draw a 1px border at the edge; keep the button inside that.
	 * Use the same general sizing rules as the compositor decorations.
	 */
	int border = 1;
	int header_h = layout->header_h;
	int icon_box = clamp_int(header_h - 8, 10, 16);
	int max_hit = header_h - 2 * border;
	int hit_box = clamp_int(icon_box, 0, max_hit);
	if (hit_box <= 0)
		return;

	int btn_outer_pad = 2;
	int x = state->width - border - btn_outer_pad - hit_box;
	int y = (header_h - hit_box) / 2;
	if (x < border)
		x = border;
	if (y < border)
		y = border;

	/* If the menu is extremely narrow, just skip the button. */
	if (x + hit_box > state->width - border)
		return;

	state->close_x = x;
	state->close_y = y;
	state->close_w = hit_box;
	state->close_h = hit_box;
}

static bool as_state_point_in_close_button(const struct as_state *state, int x, int y)
{
	if (state == NULL || state->close_w <= 0 || state->close_h <= 0)
		return false;
	return x >= state->close_x && x < state->close_x + state->close_w && y >= state->close_y &&
	       y < state->close_y + state->close_h;
}

void as_state_update_iconize_button_metrics(struct as_state *state, const struct as_menu_layout *layout)
{
	if (state == NULL || layout == NULL)
		return;

	state->iconize_x = 0;
	state->iconize_y = 0;
	state->iconize_w = 0;
	state->iconize_h = 0;

	/*
	 * The classic AfterStep "WindowList" popup has an iconize button in its
	 * titlebar in some looks, but the X11 reference screenshots for this repo
	 * only show pin + close. Keep iconize opt-in to preserve parity.
	 */
	if (!state->window_list_mode)
		return;
	if (env_int("ASWLMENU_WINLIST_ICONIZE", 0, 0, 1) != 1)
		return;
	if (state->close_w <= 0 || state->close_h <= 0)
		return;

	int border = 1;
	int header_h = layout->header_h;
	int icon_box = clamp_int(header_h - 8, 10, 16);
	int max_hit = header_h - 2 * border;
	int hit_box = clamp_int(icon_box, 0, max_hit);
	if (hit_box <= 0)
		return;

	int btn_spacing = 2;
	int x = state->close_x - btn_spacing - hit_box;
	int y = (header_h - hit_box) / 2;
	if (x < border)
		return;
	if (y < border)
		y = border;
	if (y + hit_box > header_h - border)
		y = header_h - border - hit_box;
	if (y < border)
		y = border;

	/* Avoid overlapping the pin button when the menu is very narrow. */
	if (state->pin_w > 0 && x < state->pin_x + state->pin_w + layout->pad)
		return;

	state->iconize_x = x;
	state->iconize_y = y;
	state->iconize_w = hit_box;
	state->iconize_h = hit_box;
}

static bool as_state_point_in_iconize_button(const struct as_state *state, int x, int y)
{
	if (state == NULL || state->iconize_w <= 0 || state->iconize_h <= 0)
		return false;
	return x >= state->iconize_x && x < state->iconize_x + state->iconize_w && y >= state->iconize_y &&
	       y < state->iconize_y + state->iconize_h;
}

void as_state_update_pin_button_metrics(struct as_state *state, const struct as_menu_layout *layout)
{
	if (state == NULL || layout == NULL)
		return;

	state->pin_x = 0;
	state->pin_y = 0;
	state->pin_w = 0;
	state->pin_h = 0;

	/* Only the WinList-ish `--windows` mode shows a pin button in the header. */
	if (!state->window_list_mode)
		return;

	/*
	 * Keep the sizing/alignment consistent with the close button so the header
	 * matches classic AfterStep WinList.
	 */
	int border = 1;
	int header_h = layout->header_h;
	int icon_box = clamp_int(header_h - 8, 10, 16);
	int max_hit = header_h - 2 * border;
	int hit_box = clamp_int(icon_box, 0, max_hit);
	if (hit_box <= 0)
		return;

	int btn_outer_pad = 2;
	int x = border + btn_outer_pad;
	int y = (header_h - hit_box) / 2;
	if (x < border)
		x = border;
	if (y < border)
		y = border;

	/* If the menu is extremely narrow, just skip the button. */
	if (x + hit_box > state->width - border)
		return;
	/* Avoid overlapping the close button when the menu is very narrow. */
	if (state->close_w > 0 && x + hit_box > state->close_x - layout->pad)
		return;

	state->pin_x = x;
	state->pin_y = y;
	state->pin_w = hit_box;
	state->pin_h = hit_box;
}

static bool as_state_point_in_pin_button(const struct as_state *state, int x, int y)
{
	if (state == NULL || state->pin_w <= 0 || state->pin_h <= 0)
		return false;
	return x >= state->pin_x && x < state->pin_x + state->pin_w && y >= state->pin_y &&
	       y < state->pin_y + state->pin_h;
}

void as_state_autosize(struct as_state *state)
{
	if (state == NULL)
		return;

	struct as_menu_layout layout;
	if (!as_state_get_layout(state, &layout))
		return;

	(void)aswl_font_set_scale(&state->font, layout.text_scale);
	(void)aswl_font_set_scale(&state->header_font, layout.text_scale);

	/* Size to fit the menu content, similar to AfterStep's classic root menu. */
	int default_rows = state->window_list_mode ? 8 : 12;
	size_t max_rows = (size_t)env_int("ASWLMENU_ROWS", default_rows, 1, 64);
	size_t rows = state->filtered_count;
	if (rows < 1)
		rows = 1;
	if (rows > max_rows)
		rows = max_rows;

	int min_h = layout.header_h + layout.pad * 2 + layout.row_h;
	int desired_h = layout.header_h + layout.pad * 2 + (int)rows * layout.row_h;
	desired_h = clamp_int(desired_h, min_h, 4096);

	int max_label_w = 0;
	bool any_submenu = false;
	size_t sample = state->filtered_count;
	if (sample > 512)
		sample = 512;
	for (size_t i = 0; i < sample; i++) {
		size_t idx = state->filtered[i];
		if (idx >= state->entry_count)
			continue;
		const struct as_menu_entry *e = &state->entries[idx];
		const char *label = e->label;
		if (label == NULL)
			continue;
		int w = aswl_font_text_width(&state->font, label);
		if (w > max_label_w)
			max_label_w = w;
		if (!any_submenu && menu_command_is_submenu(e->command))
			any_submenu = true;
	}

	const char *header = state->title != NULL ? state->title : "AfterStep";
	int header_style = state->theme.menu_title_text_style;
	if (state->window_list_mode)
		header_style = state->theme.menu_hititle_text_style;
	int header_w = aswl_font_text_width_styled(&state->header_font, header, header_style);

	int arrow_w = aswl_font_text_width(&state->font, ">");
	int list_w = 8 + layout.icon_col_w + max_label_w + 8;
	if (any_submenu && arrow_w > 0)
		list_w += arrow_w + 8;
	int desired_w = layout.pad * 2 + header_w;
	if (state->window_list_mode) {
		/*
		 * In WinList mode, the header is left-aligned and flanked by pin/close
		 * buttons. Auto-sizing must include those hit-boxes; otherwise the
		 * "Windows on Desktop N" title gets ellipsized even when the menu is
		 * otherwise wide enough for the row labels.
		 */
		int border = 1;
		int header_h = layout.header_h;
		int icon_box = clamp_int(header_h - 8, 10, 16);
		int max_hit = header_h - 2 * border;
		int hit_box = clamp_int(icon_box, 0, max_hit);
		int btn_outer_pad = 2;
		int btn_spacing = 2;
		int extra = 2 * border + 2 * btn_outer_pad + 2 * hit_box + 2 * layout.pad;
		/* Optional iconize button adds another hit-box next to close. */
		if (env_int("ASWLMENU_WINLIST_ICONIZE", 0, 0, 1) == 1)
			extra += hit_box + btn_spacing;
		int header_required_w = header_w + extra;
		if (header_required_w > desired_w)
			desired_w = header_required_w;
	}
	int desired_list_w = layout.pad * 2 + list_w;
	if (desired_list_w > desired_w)
		desired_w = desired_list_w;
	int max_w = state->window_list_mode ? 280 : 640;
	desired_w = clamp_int(desired_w, 240, max_w);

	state->width = env_int("ASWLMENU_WIDTH", desired_w, 120, 4096);
	int min_env_h = state->window_list_mode ? min_h : 120;
	state->height = env_int("ASWLMENU_HEIGHT", desired_h, min_env_h, 4096);
}

size_t as_state_visible_rows(struct as_state *state, const struct as_menu_layout *layout)
{
	if (state == NULL || layout == NULL)
		return 0;

	int usable_h = state->height - layout->header_h - layout->pad * 2;
	if (usable_h <= 0 || layout->row_h <= 0)
		return 0;
	return (size_t)(usable_h / layout->row_h);
}

void as_state_ensure_selection_visible(struct as_state *state)
{
	if (state == NULL)
		return;
	if (state->filtered_count == 0 || state->selected_index < 0)
		return;

	struct as_menu_layout layout;
	if (!as_state_get_layout(state, &layout))
		return;

	size_t rows = as_state_visible_rows(state, &layout);
	if (rows == 0)
		return;

	int sel = state->selected_index;
	if (sel < state->scroll)
		state->scroll = sel;
	else if ((size_t)sel >= (size_t)state->scroll + rows)
		state->scroll = sel - (int)rows + 1;

	if (state->scroll < 0)
		state->scroll = 0;
	if ((size_t)state->scroll > state->filtered_count)
		state->scroll = 0;
}

void as_state_select_delta(struct as_state *state, int delta)
{
	if (state == NULL)
		return;
	if (state->filtered_count == 0)
		return;

	int sel = state->selected_index;
	if (sel < 0)
		sel = 0;
	sel += delta;
	if (sel < 0)
		sel = 0;
	if ((size_t)sel >= state->filtered_count)
		sel = (int)(state->filtered_count - 1);

	state->selected_index = sel;
	as_state_ensure_selection_visible(state);
	schedule_redraw(state);
}

int as_state_hit_test_layout(struct as_state *state, const struct as_menu_layout *layout, int x, int y)
{
	if (state == NULL || layout == NULL)
		return -1;

	if (y < layout->header_h)
		return -1;

	int list_y = y - layout->header_h - layout->pad;
	if (list_y < 0)
		return -1;

	int row = list_y / layout->row_h;
	if (row < 0)
		return -1;

	size_t visible = as_state_visible_rows(state, layout);
	if ((size_t)row >= visible)
		return -1;

	int idx = state->scroll + row;
	if (idx < 0 || (size_t)idx >= state->filtered_count)
		return -1;

	int row_y = layout->header_h + layout->pad + row * layout->row_h;
	if (y < row_y || y >= row_y + layout->row_h)
		return -1;

	(void)x;
	return idx;
}

void as_state_update_hover(struct as_state *state)
{
	if (state == NULL)
		return;

	int old_idx = state->hover_index;
	bool old_close = state->hover_close;
	bool old_iconize = state->hover_iconize;
	bool old_pin = state->hover_pin;

	if (!state->pointer_in_surface) {
		state->hover_index = -1;
		state->hover_close = false;
		state->hover_iconize = false;
		state->hover_pin = false;
	} else {
		struct as_menu_layout layout;
		if (!as_state_get_layout(state, &layout)) {
			state->hover_index = -1;
			state->hover_close = false;
			state->hover_iconize = false;
			state->hover_pin = false;
		} else {
			as_state_update_close_button_metrics(state, &layout);
			as_state_update_pin_button_metrics(state, &layout);
			as_state_update_iconize_button_metrics(state, &layout);
			state->hover_close = as_state_point_in_close_button(state, state->pointer_x, state->pointer_y);
			state->hover_iconize = as_state_point_in_iconize_button(state, state->pointer_x, state->pointer_y);
			state->hover_pin = as_state_point_in_pin_button(state, state->pointer_x, state->pointer_y);
			state->hover_index = as_state_hit_test_layout(state, &layout, state->pointer_x, state->pointer_y);
		}
	}

	if (state->hover_index != old_idx || state->hover_close != old_close || state->hover_iconize != old_iconize ||
	    state->hover_pin != old_pin)
		schedule_redraw(state);
}
