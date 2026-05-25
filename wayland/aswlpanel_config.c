#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlpanel_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

#include "aswlicon.h"

static int as_clamp_margin(int v)
{
	if (v < 0)
		return 0;
	if (v > 8192)
		return 8192;
	return v;
}

static enum as_panel_edge as_panel_edge_parse(const char *s, enum as_panel_edge fallback)
{
	if (s == NULL)
		return fallback;

	while (*s != '\0' && isspace((unsigned char)*s))
		s++;

	char token[16];
	size_t n = 0;
	while (*s != '\0' && !isspace((unsigned char)*s) && n + 1 < sizeof(token)) {
		token[n++] = (char)tolower((unsigned char)*s);
		s++;
	}
	token[n] = '\0';

	if (strcmp(token, "top") == 0)
		return ASWL_PANEL_EDGE_TOP;
	if (strcmp(token, "bottom") == 0)
		return ASWL_PANEL_EDGE_BOTTOM;
	if (strcmp(token, "left") == 0)
		return ASWL_PANEL_EDGE_LEFT;
	if (strcmp(token, "right") == 0)
		return ASWL_PANEL_EDGE_RIGHT;

	return fallback;
}

static uint32_t as_anchor_parse(const char *s, uint32_t fallback)
{
	if (s == NULL)
		return fallback;

	while (*s != '\0' && isspace((unsigned char)*s))
		s++;

	if (*s == '\0')
		return fallback;

	uint32_t out = 0;
	bool any = false;

	while (*s != '\0') {
		while (*s != '\0' && (isspace((unsigned char)*s) || *s == ',' || *s == ';'))
			s++;
		if (*s == '\0')
			break;

		char token[16];
		size_t n = 0;
		while (*s != '\0' && !isspace((unsigned char)*s) && *s != ',' && *s != ';' && n + 1 < sizeof(token)) {
			token[n++] = (char)tolower((unsigned char)*s);
			s++;
		}
		token[n] = '\0';
		if (token[0] == '\0')
			continue;

		if (strcmp(token, "none") == 0) {
			out = 0;
			any = true;
			continue;
		}
		if (strcmp(token, "top") == 0) {
			out |= ASWL_ANCHOR_TOP;
			any = true;
			continue;
		}
		if (strcmp(token, "bottom") == 0) {
			out |= ASWL_ANCHOR_BOTTOM;
			any = true;
			continue;
		}
		if (strcmp(token, "left") == 0) {
			out |= ASWL_ANCHOR_LEFT;
			any = true;
			continue;
		}
		if (strcmp(token, "right") == 0) {
			out |= ASWL_ANCHOR_RIGHT;
			any = true;
			continue;
		}
	}

	if (!any)
		return fallback;
	return out;
}

static bool as_parse_long_token(const char *s, const char **end_out, long *value_out)
{
	if (end_out != NULL)
		*end_out = s;
	if (value_out != NULL)
		*value_out = 0;

	if (s == NULL || s[0] == '\0')
		return false;

	char *end = NULL;
	long v = strtol(s, &end, 10);
	if (end == s)
		return false;

	if (end_out != NULL)
		*end_out = end;
	if (value_out != NULL)
		*value_out = v;
	return true;
}

static void as_margins_apply_css_shorthand(struct as_margins *m, int v1, int v2, int v3, int v4, int count)
{
	if (m == NULL || count <= 0)
		return;

	if (count == 1) {
		m->top = v1;
		m->right = v1;
		m->bottom = v1;
		m->left = v1;
		return;
	}

	if (count == 2) {
		m->top = v1;
		m->bottom = v1;
		m->left = v2;
		m->right = v2;
		return;
	}

	if (count == 3) {
		m->top = v1;
		m->left = v2;
		m->right = v2;
		m->bottom = v3;
		return;
	}

	m->top = v1;
	m->right = v2;
	m->bottom = v3;
	m->left = v4;
}

static void as_margins_parse_and_apply(struct as_margins *m, const char *spec)
{
	if (m == NULL || spec == NULL)
		return;

	const char *s = spec;
	while (*s != '\0') {
		while (*s != '\0' && (isspace((unsigned char)*s) || *s == ','))
			s++;
		if (*s == '\0')
			break;

		const char *token_start = s;
		while (*s != '\0' && !isspace((unsigned char)*s) && *s != ',')
			s++;
		size_t tok_len = (size_t)(s - token_start);
		if (tok_len == 0)
			continue;

		char tok[64];
		if (tok_len >= sizeof(tok))
			tok_len = sizeof(tok) - 1;
		memcpy(tok, token_start, tok_len);
		tok[tok_len] = '\0';

		/* key=value */
		char *eq = strchr(tok, '=');
		if (eq != NULL) {
			*eq = '\0';
			const char *key = tok;
			const char *val_s = eq + 1;
			long v = 0;
			if (as_parse_long_token(val_s, NULL, &v)) {
				int mv = as_clamp_margin((int)v);
				if (strcasecmp(key, "top") == 0)
					m->top = mv;
				else if (strcasecmp(key, "right") == 0)
					m->right = mv;
				else if (strcasecmp(key, "bottom") == 0)
					m->bottom = mv;
				else if (strcasecmp(key, "left") == 0)
					m->left = mv;
				else if (strcasecmp(key, "all") == 0)
					as_margins_apply_css_shorthand(m, mv, mv, mv, mv, 1);
			}
			continue;
		}

		/* key value */
		if (strcasecmp(tok, "top") == 0 || strcasecmp(tok, "right") == 0 || strcasecmp(tok, "bottom") == 0 ||
		    strcasecmp(tok, "left") == 0 || strcasecmp(tok, "all") == 0) {
			while (*s != '\0' && (isspace((unsigned char)*s) || *s == ','))
				s++;
			long v = 0;
			const char *next = NULL;
			if (as_parse_long_token(s, &next, &v)) {
				int mv = as_clamp_margin((int)v);
				if (strcasecmp(tok, "top") == 0)
					m->top = mv;
				else if (strcasecmp(tok, "right") == 0)
					m->right = mv;
				else if (strcasecmp(tok, "bottom") == 0)
					m->bottom = mv;
				else if (strcasecmp(tok, "left") == 0)
					m->left = mv;
				else
					as_margins_apply_css_shorthand(m, mv, mv, mv, mv, 1);
				s = next;
			}
			continue;
		}

		/* Positional integers; parse up to 4 starting from this token. */
		long v[4] = { 0, 0, 0, 0 };
		int n = 0;

		const char *scan = token_start;
		while (*scan != '\0' && n < 4) {
			while (*scan != '\0' && (isspace((unsigned char)*scan) || *scan == ','))
				scan++;
			if (*scan == '\0')
				break;

			const char *next = NULL;
			long tmp = 0;
			if (!as_parse_long_token(scan, &next, &tmp))
				break;
			v[n++] = (long)as_clamp_margin((int)tmp);
			scan = next;
		}

		if (n > 0) {
			as_margins_apply_css_shorthand(m, (int)v[0], (int)v[1], (int)v[2], (int)v[3], n);
			s = scan;
		}
	}
}

static void as_button_destroy_icon(struct as_button *button)
{
	if (button == NULL)
		return;
	free(button->icon_argb);
	button->icon_argb = NULL;
	button->icon_w = 0;
	button->icon_h = 0;
}

static void as_button_destroy(struct as_button *button)
{
	if (button == NULL)
		return;
	free(button->label);
	free(button->command);
	free(button->icon_path);
	as_button_destroy_icon(button);
	memset(button, 0, sizeof(*button));
}

static void as_button_try_load_icon(struct as_button *button)
{
	if (button == NULL)
		return;

	as_button_destroy_icon(button);

	if (button->icon_path == NULL || button->icon_path[0] == '\0')
		return;

	uint32_t *pixels = NULL;
	int w = 0;
	int h = 0;
	if (aswl_icon_load_argb(button->icon_path, &pixels, &w, &h)) {
		button->icon_argb = pixels;
		button->icon_w = w;
		button->icon_h = h;
	} else {
		free(pixels);
	}
}

void as_state_free_buttons(struct as_state *state)
{
	if (!state->buttons_owned)
		return;

	for (size_t i = 0; i < state->button_count; i++) {
		as_button_destroy(&state->buttons[i]);
	}
	free(state->buttons);
	state->buttons = NULL;
	state->button_count = 0;
	state->buttons_owned = false;
}

static bool append_button(struct as_button **buttons,
                          size_t *count,
                          size_t *cap,
                          const char *label,
                          const char *command,
                          const char *icon_path)
{
	if (buttons == NULL || count == NULL || cap == NULL)
		return false;
	if (label == NULL || label[0] == '\0')
		return false;
	if (command == NULL || command[0] == '\0')
		return false;

	if (*count == *cap) {
		size_t next = *cap == 0 ? 8 : (*cap) * 2;
		struct as_button *tmp = realloc(*buttons, next * sizeof(**buttons));
		if (tmp == NULL)
			return false;
		*buttons = tmp;
		*cap = next;
	}

	(*buttons)[*count] = (struct as_button){ 0 };
	(*buttons)[*count].label = strdup(label);
	(*buttons)[*count].command = strdup(command);
	if (icon_path != NULL)
		(*buttons)[*count].icon_path = strdup(icon_path);

	if ((*buttons)[*count].label == NULL || (*buttons)[*count].command == NULL ||
	    (icon_path != NULL && (*buttons)[*count].icon_path == NULL)) {
		as_button_destroy(&(*buttons)[*count]);
		return false;
	}

	as_button_try_load_icon(&(*buttons)[*count]);
	(*count)++;
	return true;
}

static bool handle_button_directive(struct as_state *state,
                                   bool *dock_mode_io,
                                   bool *pager_mode_io,
                                   struct as_button **buttons,
                                   size_t *count,
                                   size_t *cap,
                                   const char *line,
                                   bool *handled_out,
                                   bool *workspaces_out)
{
	if (handled_out != NULL)
		*handled_out = false;
	if (workspaces_out != NULL)
		*workspaces_out = false;

	if (state == NULL || buttons == NULL || count == NULL || cap == NULL || line == NULL)
		return true;

	const char *s = line;
	if (s[0] != '@')
		return true;
	s++;

	const char *arg = NULL;
	if (strncmp(s, "workspaces", 10) == 0 && (s[10] == '\0' || isspace((unsigned char)s[10]) || s[10] == ':' || s[10] == '=')) {
		arg = s + 10;
	} else if (strncmp(s, "pager", 5) == 0 && (s[5] == '\0' || isspace((unsigned char)s[5]) || s[5] == ':' || s[5] == '=')) {
		arg = s + 5;
	} else if (strncmp(s, "mode", 4) == 0 && (s[4] == '\0' || isspace((unsigned char)s[4]) || s[4] == ':' || s[4] == '=')) {
		arg = s + 4;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;

		char token[16];
		size_t n = 0;
		while (*arg != '\0' && !isspace((unsigned char)*arg) && n + 1 < sizeof(token)) {
			token[n++] = (char)tolower((unsigned char)*arg);
			arg++;
		}
		token[n] = '\0';

		if (strcmp(token, "dock") == 0) {
			if (dock_mode_io != NULL)
				*dock_mode_io = true;
			if (pager_mode_io != NULL)
				*pager_mode_io = false;
		} else if (strcmp(token, "panel") == 0) {
			if (dock_mode_io != NULL)
				*dock_mode_io = false;
			if (pager_mode_io != NULL)
				*pager_mode_io = false;
		} else if (strcmp(token, "pager") == 0) {
			if (dock_mode_io != NULL)
				*dock_mode_io = false;
			if (pager_mode_io != NULL)
				*pager_mode_io = true;
		}

		return true;
	} else if (strncmp(s, "margin", 6) == 0 && (s[6] == '\0' || isspace((unsigned char)s[6]) || s[6] == ':' || s[6] == '=')) {
		arg = s + 6;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		as_margins_parse_and_apply(&state->margins, arg);
		return true;
	} else if (strncmp(s, "edge", 4) == 0 && (s[4] == '\0' || isspace((unsigned char)s[4]) || s[4] == ':' || s[4] == '=')) {
		arg = s + 4;
		if (handled_out != NULL)
			*handled_out = true;

		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;

		if (state != NULL && arg[0] != '\0')
			state->edge = as_panel_edge_parse(arg, state->edge);

		return true;
	} else if (strncmp(s, "anchor", 6) == 0 && (s[6] == '\0' || isspace((unsigned char)s[6]) || s[6] == ':' || s[6] == '=')) {
		arg = s + 6;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		state->anchor_override = as_anchor_parse(arg, state->anchor_override);
		state->anchor_override_set = true;
		return true;
	} else if (strncmp(s, "width", 5) == 0 && (s[5] == '\0' || isspace((unsigned char)s[5]) || s[5] == ':' || s[5] == '=')) {
		arg = s + 5;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		long v = 0;
		if (as_parse_long_token(arg, NULL, &v) && v >= 64 && v <= 8192) {
			state->width = (int)v;
			state->width_override_set = true;
		}
		return true;
	} else if (strncmp(s, "height", 6) == 0 && (s[6] == '\0' || isspace((unsigned char)s[6]) || s[6] == ':' || s[6] == '=')) {
		arg = s + 6;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		long v = 0;
		if (as_parse_long_token(arg, NULL, &v) && v >= 16 && v <= 8192) {
			state->height = (int)v;
			state->height_override_set = true;
		}
		return true;
	} else if (strncmp(s, "item_height", 11) == 0 &&
	           (s[11] == '\0' || isspace((unsigned char)s[11]) || s[11] == ':' || s[11] == '=')) {
		arg = s + 11;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		long v = 0;
		if (as_parse_long_token(arg, NULL, &v) && v >= 16 && v <= 512) {
			state->item_height = (int)v;
			state->item_height_override_set = true;
		}
		return true;
	} else if (strncmp(s, "pager_columns", 13) == 0 &&
	           (s[13] == '\0' || isspace((unsigned char)s[13]) || s[13] == ':' || s[13] == '=')) {
		arg = s + 13;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		long v = 0;
		if (as_parse_long_token(arg, NULL, &v) && v >= 1 && v <= 16)
			state->pager_columns = (int)v;
		return true;
	} else if (strncmp(s, "pager_rows", 10) == 0 &&
	           (s[10] == '\0' || isspace((unsigned char)s[10]) || s[10] == ':' || s[10] == '=')) {
		arg = s + 10;
		if (handled_out != NULL)
			*handled_out = true;
		while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
			arg++;
		long v = 0;
		if (as_parse_long_token(arg, NULL, &v) && v >= 1 && v <= 16)
			state->pager_rows = (int)v;
		return true;
	} else if (strncmp(s, "dock", 4) == 0 && (s[4] == '\0' || isspace((unsigned char)s[4]) || s[4] == ':' || s[4] == '=')) {
		if (dock_mode_io != NULL)
			*dock_mode_io = true;
		if (pager_mode_io != NULL)
			*pager_mode_io = false;
		if (handled_out != NULL)
			*handled_out = true;
		return true;
	} else if (strncmp(s, "nodock", 6) == 0 && (s[6] == '\0' || isspace((unsigned char)s[6]) || s[6] == ':' || s[6] == '=')) {
		if (dock_mode_io != NULL)
			*dock_mode_io = false;
		if (handled_out != NULL)
			*handled_out = true;
		return true;
	} else {
		return true;
	}

	if (handled_out != NULL)
		*handled_out = true;
	if (workspaces_out != NULL)
		*workspaces_out = true;

	while (*arg == ':' || *arg == '=' || isspace((unsigned char)*arg))
		arg++;

	uint32_t n = state->workspace_count > 0 ? state->workspace_count : 9;
	if (*arg != '\0') {
		char *end = NULL;
		unsigned long tmp = strtoul(arg, &end, 10);
		while (end != NULL && isspace((unsigned char)*end))
			end++;
		if (end != arg && end != NULL && *end == '\0' && tmp >= 1 && tmp <= 1000)
			n = (uint32_t)tmp;
	}

	for (uint32_t i = 1; i <= n; i++) {
		char label[16];
		char command[64];
		(void)snprintf(label, sizeof(label), "%u", i);
		(void)snprintf(command, sizeof(command), "@workspace %u", i);
		if (!append_button(buttons, count, cap, label, command, NULL))
			return false;
	}

	return true;
}

static bool load_buttons_from_file(struct as_state *state, const char *path)
{
	if (path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	struct as_button *buttons = NULL;
	size_t count = 0;
	size_t cap = 0;
	bool has_workspaces_directive = false;
	bool dock_mode = state->dock_mode;
	bool pager_mode = state->pager_mode;

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len;

	while ((line_len = getline(&line, &line_cap, fp)) != -1) {
		while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
			line[--line_len] = '\0';

		char *s = line;
		while (*s == ' ' || *s == '\t')
			s++;

		if (*s == '\0' || *s == '#')
			continue;

		if (*s == '@') {
			bool handled = false;
			bool workspaces = false;
			if (!handle_button_directive(state, &dock_mode, &pager_mode, &buttons, &count, &cap, s, &handled, &workspaces))
				goto fail;
			if (handled) {
				if (workspaces)
					has_workspaces_directive = true;
				continue;
			}
		}

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';

		char *label = s;
		char *command = eq + 1;
		char *icon_path = NULL;

		while (*label == ' ' || *label == '\t')
			label++;
		while (*command == ' ' || *command == '\t')
			command++;

		for (char *end = label + strlen(label); end > label && (end[-1] == ' ' || end[-1] == '\t'); end--)
			end[-1] = '\0';
		for (char *end = command + strlen(command); end > command && (end[-1] == ' ' || end[-1] == '\t'); end--)
			end[-1] = '\0';

		char *bar = strchr(label, '|');
		if (bar != NULL) {
			*bar = '\0';
			icon_path = bar + 1;
			while (*icon_path == ' ' || *icon_path == '\t')
				icon_path++;
			for (char *end = icon_path + strlen(icon_path);
			     end > icon_path && (end[-1] == ' ' || end[-1] == '\t');
			     end--)
				end[-1] = '\0';
			for (char *end = label + strlen(label);
			     end > label && (end[-1] == ' ' || end[-1] == '\t');
			     end--)
				end[-1] = '\0';
			if (icon_path[0] == '\0')
				icon_path = NULL;
		}

		if (label[0] == '\0' || command[0] == '\0')
			continue;

		if (!append_button(&buttons, &count, &cap, label, command, icon_path))
			goto fail;
	}

	free(line);
	fclose(fp);

	as_state_free_buttons(state);
	state->buttons = buttons;
	state->button_count = count;
	state->buttons_owned = true;
	state->buttons_has_workspaces_directive = has_workspaces_directive;
	state->dock_mode = dock_mode;
	state->pager_mode = pager_mode;
	return true;

fail:
	free(line);
	fclose(fp);
	for (size_t i = 0; i < count; i++) {
		as_button_destroy(&buttons[i]);
	}
	free(buttons);
	return false;
}

bool as_state_load_buttons_from_file(struct as_state *state, const char *path)
{
	return load_buttons_from_file(state, path);
}

void as_state_load_buttons(struct as_state *state)
{
	state->buttons_has_workspaces_directive = false;
	free(state->buttons_config_path);
	state->buttons_config_path = NULL;
	state->dock_mode = false;
	state->pager_mode = false;
	state->edge = ASWL_PANEL_EDGE_TOP;
	state->margins = (struct as_margins){ 0 };
	state->anchor_override_set = false;
	state->anchor_override = 0;
	state->width_override_set = false;
	state->height_override_set = false;
	state->item_height_override_set = false;
	state->pager_columns = 2;
	state->pager_rows = 1;

	const char *mode = getenv("ASWLPANEL_MODE");
	if (mode != NULL && mode[0] != '\0') {
		if (strcasecmp(mode, "dock") == 0)
			state->dock_mode = true;
		else if (strcasecmp(mode, "panel") == 0)
			state->dock_mode = false;
		else if (strcasecmp(mode, "pager") == 0)
			state->pager_mode = true;
	}

	const char *dock = getenv("ASWLPANEL_DOCK");
	if (dock != NULL && dock[0] != '\0') {
		if (dock[0] == '1' || strcasecmp(dock, "true") == 0 || strcasecmp(dock, "yes") == 0)
			state->dock_mode = true;
	}
	if (state->dock_mode)
		state->pager_mode = false;

	const char *edge = getenv("ASWLPANEL_EDGE");
	if (edge != NULL && edge[0] != '\0')
		state->edge = as_panel_edge_parse(edge, state->edge);

	bool loaded = false;

	const char *path = getenv("ASWLPANEL_CONFIG");
	if (path != NULL && load_buttons_from_file(state, path)) {
		state->buttons_config_path = strdup(path);
		loaded = true;
	}

	if (!loaded) {
		const char *home = getenv("HOME");
		if (home != NULL && home[0] != '\0') {
			char *xdg_path = NULL;
			if (asprintf(&xdg_path, "%s/.config/afterstep/aswlpanel.conf", home) >= 0) {
				bool ok = load_buttons_from_file(state, xdg_path);
				if (ok)
					state->buttons_config_path = strdup(xdg_path);
				free(xdg_path);
				loaded = ok;
			}
		}
	}

	if (!loaded) {
		static struct as_button defaults[] = {
			{ .label = "Terminal", .command = "foot" },
			{ .label = "Browser", .command = "firefox" },
		};

		as_state_free_buttons(state);
		state->buttons = defaults;
		state->button_count = sizeof(defaults) / sizeof(defaults[0]);
		state->buttons_owned = false;
	}

	const char *margin_all = getenv("ASWLPANEL_MARGIN");
	if (margin_all != NULL && margin_all[0] != '\0')
		as_margins_parse_and_apply(&state->margins, margin_all);

	const char *margin_top = getenv("ASWLPANEL_MARGIN_TOP");
	const char *margin_right = getenv("ASWLPANEL_MARGIN_RIGHT");
	const char *margin_bottom = getenv("ASWLPANEL_MARGIN_BOTTOM");
	const char *margin_left = getenv("ASWLPANEL_MARGIN_LEFT");
	long mv = 0;
	if (margin_top != NULL && margin_top[0] != '\0' && as_parse_long_token(margin_top, NULL, &mv))
		state->margins.top = as_clamp_margin((int)mv);
	if (margin_right != NULL && margin_right[0] != '\0' && as_parse_long_token(margin_right, NULL, &mv))
		state->margins.right = as_clamp_margin((int)mv);
	if (margin_bottom != NULL && margin_bottom[0] != '\0' && as_parse_long_token(margin_bottom, NULL, &mv))
		state->margins.bottom = as_clamp_margin((int)mv);
	if (margin_left != NULL && margin_left[0] != '\0' && as_parse_long_token(margin_left, NULL, &mv))
		state->margins.left = as_clamp_margin((int)mv);
}

