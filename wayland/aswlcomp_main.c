#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "aswlcomp_internal.h"

#if defined(HAVE_WLROOTS) && HAVE_WLROOTS

#include <wlr/util/log.h>

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--socket NAME] [--autostart PATH] [--spawn CMD]... [--state PATH]\n", prog);
	fprintf(stderr, "  --socket NAME  Use a fixed WAYLAND_DISPLAY socket name\n");
	fprintf(stderr, "  --autostart PATH\n");
	fprintf(stderr, "                Spawn commands from a file.\n");
	fprintf(stderr, "                Lines are: 'exec CMD' / 'CMD' / 'bind MODS+KEY exec CMD' / 'bind MODS+KEY ACTION'\n");
	fprintf(stderr, "                           'set KEY VALUE' (xkb_layout/xkb_variant/xkb_options/xkb_model/xkb_rules,\n");
	fprintf(stderr, "                                           repeat_rate/repeat_delay, pointer_accel(-1..1), tap_to_click)\n");
	fprintf(stderr, "                ACTION is: quit|close_focused|focus_next|focus_prev|workspace N|workspace_next|workspace_prev\n");
	fprintf(stderr, "                           toggle_fullscreen|toggle_maximized|lock\n");
	fprintf(stderr, "                Default: $XDG_CONFIG_HOME/afterstep/aswlcomp.autostart\n");
	fprintf(stderr, "                         or ~/.config/afterstep/aswlcomp.autostart\n");
	fprintf(stderr, "  --spawn CMD    Spawn a client command after startup (may be repeated)\n");
	fprintf(stderr, "                (CMD runs via /bin/sh -c with WAYLAND_DISPLAY set)\n");
	fprintf(stderr, "  --state PATH   Persist basic session state (default: $XDG_STATE_HOME/afterstep/aswlcomp.state)\n");
}

static char *xstrdup_printf(const char *fmt, ...)
{
	if (fmt == NULL)
		return NULL;

	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0)
		return NULL;

	char *buf = malloc((size_t)n + 1);
	if (buf == NULL)
		return NULL;

	va_start(ap, fmt);
	(void)vsnprintf(buf, (size_t)n + 1, fmt, ap);
	va_end(ap);
	return buf;
}

static char *lstrip(char *s)
{
	if (s == NULL)
		return NULL;
	while (*s != '\0' && isspace((unsigned char)*s))
		s++;
	return s;
}

static void rstrip_inplace(char *s)
{
	if (s == NULL)
		return;
	size_t len = strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1]))
		s[--len] = '\0';
}

static void aswl_dock_config_free_rules(struct aswl_dock_config *dock)
{
	if (dock == NULL)
		return;
	for (size_t i = 0; i < dock->rule_count; i++)
		free(dock->rules[i]);
	free(dock->rules);
	dock->rules = NULL;
	dock->rule_count = 0;
}

static enum aswl_dock_anchor aswl_dock_anchor_parse(const char *s, enum aswl_dock_anchor fallback)
{
	if (s == NULL)
		return fallback;
	while (*s != '\0' && isspace((unsigned char)*s))
		s++;

	char token[32];
	size_t n = 0;
	while (*s != '\0' && !isspace((unsigned char)*s) && n + 1 < sizeof(token)) {
		char c = (char)tolower((unsigned char)*s);
		if (c == '_')
			c = '-';
		token[n++] = c;
		s++;
	}
	token[n] = '\0';

	if (strcmp(token, "bottom-left") == 0 || strcmp(token, "bl") == 0)
		return ASWL_DOCK_ANCHOR_BOTTOM_LEFT;
	if (strcmp(token, "bottom-right") == 0 || strcmp(token, "br") == 0)
		return ASWL_DOCK_ANCHOR_BOTTOM_RIGHT;
	if (strcmp(token, "top-left") == 0 || strcmp(token, "tl") == 0)
		return ASWL_DOCK_ANCHOR_TOP_LEFT;
	if (strcmp(token, "top-right") == 0 || strcmp(token, "tr") == 0)
		return ASWL_DOCK_ANCHOR_TOP_RIGHT;

	return fallback;
}

static enum aswl_dock_flow aswl_dock_flow_parse(const char *s, enum aswl_dock_flow fallback)
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

	if (strcmp(token, "row") == 0 || strcmp(token, "rows") == 0 || strcmp(token, "h") == 0 ||
	    strcmp(token, "horizontal") == 0)
		return ASWL_DOCK_FLOW_ROW;
	if (strcmp(token, "col") == 0 || strcmp(token, "cols") == 0 || strcmp(token, "column") == 0 ||
	    strcmp(token, "columns") == 0 || strcmp(token, "v") == 0 || strcmp(token, "vertical") == 0)
		return ASWL_DOCK_FLOW_COLUMN;

	return fallback;
}

static enum aswl_dock_order_mode aswl_dock_order_parse(const char *s, enum aswl_dock_order_mode fallback)
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

	if (strcmp(token, "create") == 0 || strcmp(token, "created") == 0)
		return ASWL_DOCK_ORDER_CREATE;
	if (strcmp(token, "title") == 0)
		return ASWL_DOCK_ORDER_TITLE;
	if (strcmp(token, "class") == 0 || strcmp(token, "app") == 0 || strcmp(token, "appid") == 0 ||
	    strcmp(token, "app_id") == 0)
		return ASWL_DOCK_ORDER_CLASS;
	if (strcmp(token, "config") == 0 || strcmp(token, "rules") == 0)
		return ASWL_DOCK_ORDER_CONFIG;

	return fallback;
}

static char *aswl_dock_rules_path_default(void)
{
	const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	if (xdg_config_home != NULL && xdg_config_home[0] != '\0')
		return xstrdup_printf("%s/afterstep/aswlcomp.dockapps", xdg_config_home);

	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0')
		return xstrdup_printf("%s/.config/afterstep/aswlcomp.dockapps", home);

	return NULL;
}

static char *aswl_dock_rules_path_resolve(void)
{
	const char *override = getenv("ASWLCOMP_DOCKAPPS");
	if (override != NULL && override[0] != '\0')
		return strdup(override);

	override = getenv("ASWLCOMP_DOCK_RULES");
	if (override != NULL && override[0] != '\0')
		return strdup(override);

	return aswl_dock_rules_path_default();
}

static bool aswl_dock_config_load_rules(struct aswl_dock_config *dock, const char *path)
{
	if (dock == NULL || path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	char **rules = NULL;
	size_t count = 0;
	size_t cap = 0;

	char *line = NULL;
	size_t line_cap = 0;
	while (getline(&line, &line_cap, fp) != -1) {
		rstrip_inplace(line);
		char *s = lstrip(line);
		if (s == NULL || s[0] == '\0' || s[0] == '#' || s[0] == ';')
			continue;

		if (count == cap) {
			size_t new_cap = cap > 0 ? cap * 2 : 16;
			char **new_rules = realloc(rules, new_cap * sizeof(*new_rules));
			if (new_rules == NULL)
				break;
			rules = new_rules;
			cap = new_cap;
		}

		rules[count] = strdup(s);
		if (rules[count] == NULL)
			break;
		count++;
	}

	free(line);
	fclose(fp);

	if (count == 0) {
		for (size_t i = 0; i < count; i++)
			free(rules[i]);
		free(rules);
		return false;
	}

	aswl_dock_config_free_rules(dock);
	dock->rules = rules;
	dock->rule_count = count;
	return true;
}

static void aswl_dock_config_init(struct aswl_dock_config *dock)
{
	if (dock == NULL)
		return;
	memset(dock, 0, sizeof(*dock));
	dock->anchor = ASWL_DOCK_ANCHOR_BOTTOM_LEFT;
	dock->flow = ASWL_DOCK_FLOW_ROW;
	dock->order = ASWL_DOCK_ORDER_CREATE;
	dock->pad = 12;
	dock->spacing = 8;
	dock->max_dim = 512;

	const char *anchor = getenv("ASWLCOMP_DOCK_ANCHOR");
	if (anchor != NULL && anchor[0] != '\0')
		dock->anchor = aswl_dock_anchor_parse(anchor, dock->anchor);

	const char *flow = getenv("ASWLCOMP_DOCK_FLOW");
	if (flow != NULL && flow[0] != '\0')
		dock->flow = aswl_dock_flow_parse(flow, dock->flow);

	const char *order = getenv("ASWLCOMP_DOCK_ORDER");
	if (order != NULL && order[0] != '\0')
		dock->order = aswl_dock_order_parse(order, dock->order);

	const char *pad = getenv("ASWLCOMP_DOCK_PAD");
	if (pad != NULL && pad[0] != '\0') {
		char *end = NULL;
		long v = strtol(pad, &end, 10);
		if (end != pad && *end == '\0')
			dock->pad = clamp_int((int)v, 0, 512);
	}

	const char *spacing = getenv("ASWLCOMP_DOCK_SPACING");
	if (spacing != NULL && spacing[0] != '\0') {
		char *end = NULL;
		long v = strtol(spacing, &end, 10);
		if (end != spacing && *end == '\0')
			dock->spacing = clamp_int((int)v, 0, 256);
	}

	const char *max_dim = getenv("ASWLCOMP_DOCK_MAX_DIM");
	if (max_dim != NULL && max_dim[0] != '\0') {
		char *end = NULL;
		long v = strtol(max_dim, &end, 10);
		if (end != max_dim && *end == '\0')
			dock->max_dim = (uint16_t)clamp_int((int)v, 16, 4096);
	}

	dock->rules_path = aswl_dock_rules_path_resolve();
	if (dock->rules_path != NULL && access(dock->rules_path, R_OK) == 0)
		(void)aswl_dock_config_load_rules(dock, dock->rules_path);
}

int main(int argc, char **argv)
{
	const char *socket_name = NULL;
	const char *autostart_path = NULL;
	const char **spawn_cmds = NULL;
	size_t spawn_count = 0;
	const char *state_path_override = NULL;

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
		if (strcmp(argv[i], "--autostart") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 2;
			}
			autostart_path = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--state") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 2;
			}
			state_path_override = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--spawn") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 2;
			}
			const char **new_spawn = realloc(spawn_cmds, (spawn_count + 1) * sizeof(*new_spawn));
			if (new_spawn == NULL) {
				fprintf(stderr, "aswlcomp: realloc failed\n");
				return 1;
			}
			spawn_cmds = new_spawn;
			spawn_cmds[spawn_count++] = argv[++i];
			continue;
		}

		fprintf(stderr, "aswlcomp: unknown argument: %s\n", argv[i]);
		usage(argv[0]);
		return 2;
	}

	wlr_log_init(WLR_INFO, NULL);

	struct aswl_server server = { 0 };
	server.current_workspace = 1;
	server.workspace_count = 9;
	server.next_view_id = 1;
	server.repeat_rate = 25;
	server.repeat_delay = 600;
	wl_list_init(&server.outputs_persist);
	wl_list_init(&server.idle_inhibitors);

	aswl_theme_init_default(&server.theme);
	(void)aswl_theme_load(&server.theme);
	aswl_font_init(&server.deco_font);
	aswl_font_init(&server.deco_font_inactive);
	const char *deco_font = server.theme.frame_font != NULL ? server.theme.frame_font : server.theme.panel_font;
	(void)aswl_font_load(&server.deco_font, deco_font);
	const char *deco_inactive_font = server.theme.frame_inactive_font != NULL ? server.theme.frame_inactive_font : deco_font;
	(void)aswl_font_load(&server.deco_font_inactive, deco_inactive_font);
	aswl_dock_config_init(&server.dock);

	bool workspace_count_from_env = false;
	const char *ws_env = getenv("ASWLCOMP_WORKSPACES");
	if (ws_env != NULL && ws_env[0] != '\0') {
		char *end = NULL;
		unsigned long n = strtoul(ws_env, &end, 10);
		if (end != ws_env && *end == '\0' && n > 0 && n <= 1000) {
			server.workspace_count = (uint32_t)n;
			workspace_count_from_env = true;
		}
	}

	server.state_path = aswl_state_path_resolve(state_path_override);
	aswl_state_load(&server, !workspace_count_from_env);

	if (aswl_server_init(&server, socket_name) != 0)
		return 1;

	if (autostart_path != NULL) {
		fprintf(stderr, "aswlcomp: autostart file: %s\n", autostart_path);
		load_config_file(&server, autostart_path, true);
	} else {
		char default_path[4096];
		if (default_autostart_path(default_path, sizeof(default_path)))
			load_config_file(&server, default_path, false);
	}
	for (size_t i = 0; i < spawn_count; i++) {
		fprintf(stderr, "aswlcomp: spawn: %s\n", spawn_cmds[i]);
		spawn_command(spawn_cmds[i]);
	}

	free(spawn_cmds);
	wl_display_run(server.display);

	aswl_server_finish(&server);
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
