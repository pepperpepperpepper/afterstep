#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlmenu_internal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

bool menu_config_section_exists(const char *path, const char *section)
{
	if (path == NULL || path[0] == '\0' || section == NULL || section[0] == '\0')
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	char *line = NULL;
	size_t cap = 0;
	ssize_t len;
	bool found = false;

	while ((len = getline(&line, &cap, fp)) != -1) {
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		char *s = lstrip(line);
		if (*s == '\0' || *s == '#')
			continue;
		if (*s != '@')
			continue;
		s++;

		char *arg = strchr(s, ' ');
		if (arg != NULL) {
			*arg = '\0';
			arg = lstrip(arg + 1);
			rstrip(arg);
		}

		if (strcmp(s, "menu") != 0)
			continue;
		if (arg == NULL || arg[0] == '\0')
			continue;
		if (strcmp(arg, section) == 0) {
			found = true;
			break;
		}
	}

	free(line);
	fclose(fp);
	return found;
}

char *menu_command_submenu_target(const char *command, const char *fallback_label)
{
	if (!menu_command_is_submenu(command))
		return NULL;

	const char *action = command + 1;
	while (*action != '\0' && isspace((unsigned char)*action))
		action++;
	if (strncmp(action, "submenu", 6) != 0)
		return NULL;

	const char *arg = action + 6;
	while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
		arg++;

	const char *picked = arg;
	if (picked[0] == '\0')
		picked = fallback_label != NULL ? fallback_label : "";

	while (*picked != '\0' && isspace((unsigned char)*picked))
		picked++;

	char *out = strdup(picked);
	if (out == NULL)
		return NULL;
	rstrip(out);
	if (out[0] == '\0') {
		free(out);
		return NULL;
	}
	return out;
}

static int cmp_entry_label_ci(const void *a, const void *b)
{
	const struct as_menu_entry *ea = a;
	const struct as_menu_entry *eb = b;
	if (ea->label == NULL && eb->label == NULL)
		return 0;
	if (ea->label == NULL)
		return -1;
	if (eb->label == NULL)
		return 1;
	return strcasecmp(ea->label, eb->label);
}

static bool load_menu_from_file_section(struct as_state *state, const char *path, const char *section)
{
	if (state == NULL || path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len;
	bool any = false;
	bool in_section = section == NULL;
	bool saw_section = section == NULL;

	while ((line_len = getline(&line, &line_cap, fp)) != -1) {
		while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
			line[--line_len] = '\0';

		char *s = lstrip(line);
		if (*s == '\0' || *s == '#')
			continue;

		if (*s == '@') {
			s++;
			char *arg = strchr(s, ' ');
			if (arg != NULL) {
				*arg = '\0';
				arg = lstrip(arg + 1);
				rstrip(arg);
			}

			if (strcmp(s, "menu") == 0) {
				in_section = false;
				if (section != NULL && arg != NULL && arg[0] != '\0' && strcmp(arg, section) == 0) {
					in_section = true;
					saw_section = true;
				}
				continue;
			}
			if (strcmp(s, "endmenu") == 0 || strcmp(s, "end_menu") == 0) {
				in_section = section == NULL;
				continue;
			}

			if (!in_section)
				continue;

			if (strcmp(s, "desktop_entries") == 0 || strcmp(s, "desktop") == 0) {
				state->include_desktop_entries = true;
				any = true;
				continue;
			}
			if (strcmp(s, "no_desktop_entries") == 0 || strcmp(s, "no_desktop") == 0) {
				state->include_desktop_entries = false;
				any = true;
				continue;
			}
			if (strcmp(s, "show_help") == 0 || strcmp(s, "help") == 0) {
				state->show_help = true;
				any = true;
				continue;
			}
			if (strcmp(s, "no_help") == 0 || strcmp(s, "hide_help") == 0) {
				state->show_help = false;
				any = true;
				continue;
			}
			if (strcmp(s, "title") == 0) {
				if (arg != NULL && arg[0] != '\0') {
					free(state->title);
					state->title = strdup(arg);
					any = true;
				}
				continue;
			}
		}

		if (!in_section)
			continue;

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';

		char *label = s;
		char *command = eq + 1;
		char *icon_spec = NULL;
		rstrip(label);
		label = lstrip(label);
		command = lstrip(command);
		rstrip(command);

		char *bar = strchr(label, '|');
		if (bar != NULL) {
			*bar = '\0';
			icon_spec = lstrip(bar + 1);
			rstrip(icon_spec);
			rstrip(label);
			if (icon_spec[0] == '\0')
				icon_spec = NULL;
		}

		if (label[0] == '\0' || command[0] == '\0')
			continue;

		any |= as_state_append_entry(state, label, command, icon_spec, true);
	}

	free(line);
	fclose(fp);
	if (section != NULL)
		return saw_section || any;
	return any;
}

static bool load_menu_from_file(struct as_state *state, const char *path)
{
	return load_menu_from_file_section(state, path, NULL);
}

void as_state_load_menu(struct as_state *state)
{
	state->include_desktop_entries = true;
	free(state->menu_config_path);
	state->menu_config_path = NULL;

	const char *path = getenv("ASWLMENU_CONFIG");
	if (path != NULL && load_menu_from_file(state, path)) {
		state->menu_config_path = strdup(path);
		return;
	}

	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg != NULL && xdg[0] != '\0') {
		char *xdg_path = NULL;
		if (asprintf(&xdg_path, "%s/afterstep/aswlmenu.conf", xdg) >= 0) {
			bool ok = load_menu_from_file(state, xdg_path);
			if (ok)
				state->menu_config_path = strdup(xdg_path);
			free(xdg_path);
			if (ok)
				return;
		}
	}

	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		char *home_path = NULL;
		if (asprintf(&home_path, "%s/.config/afterstep/aswlmenu.conf", home) >= 0) {
			bool ok = load_menu_from_file(state, home_path);
			if (ok)
				state->menu_config_path = strdup(home_path);
			free(home_path);
			if (ok)
				return;
		}
	}

	/* Defaults if no config exists. */
	(void)as_state_append_entry(state, "Terminal", "${TERMINAL:-foot}", NULL, true);
	(void)as_state_append_entry(state, "Close focused", "@close", NULL, true);
	(void)as_state_append_entry(state, "Quit compositor", "@quit", NULL, true);
}

void as_state_finalize_menu(struct as_state *state)
{
	if (state == NULL)
		return;

	if (state->include_desktop_entries)
		as_state_add_desktop_entries(state);

	if (state->entry_count > state->pinned_count) {
		qsort(state->entries + state->pinned_count,
		      state->entry_count - state->pinned_count,
		      sizeof(state->entries[0]),
		      cmp_entry_label_ci);
	}

	as_state_rebuild_filtered(state);
}

bool as_state_reload_menu_from_config(struct as_state *state)
{
	if (state == NULL)
		return false;
	if (state->menu_config_path == NULL || state->menu_config_path[0] == '\0')
		return false;

	as_state_free_entries(state);
	as_state_free_filtered(state);
	state->selected_index = 0;
	state->hover_index = -1;
	state->pressed_index = -1;
	state->scroll = 0;
	as_state_filter_clear_silent(state);

	state->include_desktop_entries = state->menu_section == NULL;
	if (!load_menu_from_file_section(state, state->menu_config_path, state->menu_section))
		return false;

	as_state_finalize_menu(state);
	as_state_autosize(state);
	return true;
}

