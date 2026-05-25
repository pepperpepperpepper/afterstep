#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_keyboard.h>

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

static bool parse_modifiers(char *mods_str, uint32_t *mods_out)
{
	if (mods_out == NULL)
		return false;
	*mods_out = 0;

	if (mods_str == NULL || mods_str[0] == '\0')
		return true;

	char *saveptr = NULL;
	for (char *tok = strtok_r(mods_str, "+", &saveptr); tok != NULL; tok = strtok_r(NULL, "+", &saveptr)) {
		tok = lstrip(tok);
		rstrip_inplace(tok);
		if (tok[0] == '\0')
			continue;

		if (str_ieq(tok, "alt") || str_ieq(tok, "mod1")) {
			*mods_out |= WLR_MODIFIER_ALT;
			continue;
		}
		if (str_ieq(tok, "ctrl") || str_ieq(tok, "control")) {
			*mods_out |= WLR_MODIFIER_CTRL;
			continue;
		}
		if (str_ieq(tok, "shift")) {
			*mods_out |= WLR_MODIFIER_SHIFT;
			continue;
		}
		if (str_ieq(tok, "logo") || str_ieq(tok, "super") || str_ieq(tok, "mod4")) {
			*mods_out |= WLR_MODIFIER_LOGO;
			continue;
		}

		fprintf(stderr, "aswlcomp: bind: unknown modifier: %s\n", tok);
		return false;
	}

	return true;
}

static void aswl_set_opt_string(char **dst, const char *value)
{
	if (dst == NULL)
		return;

	if (value == NULL || value[0] == '\0' || str_ieq(value, "default") || str_ieq(value, "none") || str_ieq(value, "null")) {
		free(*dst);
		*dst = NULL;
		return;
	}

	char *dup = strdup(value);
	if (dup == NULL)
		return;

	free(*dst);
	*dst = dup;
}

void aswl_apply_keyboard_device_config(struct aswl_server *server, struct aswl_keyboard *keyboard)
{
	if (server == NULL || keyboard == NULL || keyboard->wlr_keyboard == NULL)
		return;

	if (!keyboard->has_keymap) {
		if (server->xkb_context == NULL)
			server->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

		if (server->xkb_context != NULL) {
			struct xkb_rule_names names = { 0 };
			names.rules = server->xkb_rules;
			names.model = server->xkb_model;
			names.layout = server->xkb_layout;
			names.variant = server->xkb_variant;
			names.options = server->xkb_options;

			const struct xkb_rule_names *names_ptr = NULL;
			if (names.rules != NULL || names.model != NULL || names.layout != NULL || names.variant != NULL || names.options != NULL)
				names_ptr = &names;

			struct xkb_keymap *keymap = xkb_keymap_new_from_names(server->xkb_context, names_ptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
			if (keymap != NULL) {
				(void)wlr_keyboard_set_keymap(keyboard->wlr_keyboard, keymap);
				xkb_keymap_unref(keymap);
			} else if (names_ptr != NULL) {
				fprintf(stderr, "aswlcomp: xkb: failed to compile keymap (layout=%s)\n",
				        names.layout != NULL ? names.layout : "");
			}
		}
	}

	int rate = server->repeat_rate;
	int delay = server->repeat_delay;
	if (rate < 0)
		rate = 0;
	if (delay < 0)
		delay = 0;
	wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard, rate, delay);
}

static void aswl_apply_keyboard_config(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_keyboard *keyboard;
	wl_list_for_each(keyboard, &server->keyboards, link) {
		aswl_apply_keyboard_device_config(server, keyboard);
	}
}

static bool aswl_parse_bool(const char *s, bool *out)
{
	if (out == NULL)
		return false;
	if (s == NULL)
		return false;

	if (strcmp(s, "1") == 0 || str_ieq(s, "true") || str_ieq(s, "yes") || str_ieq(s, "on") ||
	    str_ieq(s, "enable") || str_ieq(s, "enabled")) {
		*out = true;
		return true;
	}
	if (strcmp(s, "0") == 0 || str_ieq(s, "false") || str_ieq(s, "no") || str_ieq(s, "off") ||
	    str_ieq(s, "disable") || str_ieq(s, "disabled")) {
		*out = false;
		return true;
	}

	return false;
}

void aswl_apply_pointer_device_config(struct aswl_server *server, struct wlr_input_device *device)
{
	if (server == NULL || device == NULL)
		return;

	if (!server->have_pointer_accel && !server->have_tap_to_click)
		return;

	if (!wlr_input_device_is_libinput(device))
		return;

	struct libinput_device *lid = wlr_libinput_get_device_handle(device);
	if (lid == NULL)
		return;

	if (server->have_pointer_accel) {
		double speed = server->pointer_accel;
		if (speed < -1.0)
			speed = -1.0;
		if (speed > 1.0)
			speed = 1.0;
		(void)libinput_device_config_accel_set_speed(lid, speed);
	}

	if (server->have_tap_to_click) {
		enum libinput_config_tap_state state =
			server->tap_to_click ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED;
		(void)libinput_device_config_tap_set_enabled(lid, state);
	}
}

static void aswl_apply_pointer_config(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_pointer_device *pd;
	wl_list_for_each(pd, &server->pointer_devices, link) {
		if (pd == NULL)
			continue;
		aswl_apply_pointer_device_config(server, pd->device);
	}
}

static void add_binding_exec(struct aswl_server *server, uint32_t mods, xkb_keysym_t keysym, const char *command)
{
	if (server == NULL || command == NULL || command[0] == '\0' || keysym == XKB_KEY_NoSymbol)
		return;

	struct aswl_binding *b = calloc(1, sizeof(*b));
	if (b == NULL)
		return;

	b->mods = mods;
	b->keysym = keysym;
	b->action = ASWL_BINDING_EXEC;
	b->command = strdup(command);
	if (b->command == NULL) {
		free(b);
		return;
	}

	wl_list_insert(server->bindings.prev, &b->link);
}

static void add_binding_action(struct aswl_server *server, uint32_t mods, xkb_keysym_t keysym, int action)
{
	if (server == NULL || keysym == XKB_KEY_NoSymbol)
		return;

	struct aswl_binding *b = calloc(1, sizeof(*b));
	if (b == NULL)
		return;

	b->mods = mods;
	b->keysym = keysym;
	b->action = action;
	wl_list_insert(server->bindings.prev, &b->link);
}

static void add_binding_workspace_set(struct aswl_server *server, uint32_t mods, xkb_keysym_t keysym, uint32_t workspace)
{
	if (server == NULL || keysym == XKB_KEY_NoSymbol || workspace < 1)
		return;

	struct aswl_binding *b = calloc(1, sizeof(*b));
	if (b == NULL)
		return;

	b->mods = mods;
	b->keysym = keysym;
	b->action = ASWL_BINDING_WORKSPACE_SET;
	b->workspace = workspace;
	wl_list_insert(server->bindings.prev, &b->link);
}

const char *binding_action_name(int action)
{
	switch (action) {
	case ASWL_BINDING_QUIT:
		return "quit";
	case ASWL_BINDING_CLOSE_FOCUSED:
		return "close_focused";
	case ASWL_BINDING_FOCUS_NEXT:
		return "focus_next";
	case ASWL_BINDING_FOCUS_PREV:
		return "focus_prev";
	case ASWL_BINDING_WORKSPACE_SET:
		return "workspace";
	case ASWL_BINDING_WORKSPACE_NEXT:
		return "workspace_next";
	case ASWL_BINDING_WORKSPACE_PREV:
		return "workspace_prev";
	case ASWL_BINDING_TOGGLE_FULLSCREEN:
		return "toggle_fullscreen";
	case ASWL_BINDING_TOGGLE_MAXIMIZED:
		return "toggle_maximized";
	case ASWL_BINDING_LOCK:
		return "lock";
	default:
		return "exec";
	}
}

bool default_autostart_path(char *out, size_t out_size)
{
	if (out == NULL || out_size == 0)
		return false;

	const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");

	int n = -1;
	if (xdg_config_home != NULL && xdg_config_home[0] != '\0') {
		n = snprintf(out, out_size, "%s/afterstep/aswlcomp.autostart", xdg_config_home);
	} else if (home != NULL && home[0] != '\0') {
		n = snprintf(out, out_size, "%s/.config/afterstep/aswlcomp.autostart", home);
	}

	if (n < 0 || (size_t)n >= out_size)
		return false;
	return true;
}

void load_config_file(struct aswl_server *server, const char *path, bool log_missing)
{
	if (path == NULL || path[0] == '\0')
		return;

	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (log_missing)
			fprintf(stderr, "aswlcomp: autostart: %s: %s\n", path, strerror(errno));
		return;
	}

	char *line = NULL;
	size_t cap = 0;
	while (getline(&line, &cap, fp) >= 0) {
		rstrip_inplace(line);
		char *cmd = lstrip(line);
		if (cmd == NULL || cmd[0] == '\0' || cmd[0] == '#')
			continue;

		if (strncmp(cmd, "set", 3) == 0 && isspace((unsigned char)cmd[3])) {
			char *rest = lstrip(cmd + 3);
			if (rest == NULL || rest[0] == '\0') {
				fprintf(stderr, "aswlcomp: config: set: missing key\n");
				continue;
			}

			char *key = rest;
			while (*rest != '\0' && !isspace((unsigned char)*rest))
				rest++;
			if (*rest == '\0') {
				fprintf(stderr, "aswlcomp: config: set: missing value for %s\n", key);
				continue;
			}
			*rest++ = '\0';
			char *val = lstrip(rest);
			rstrip_inplace(val);

			if (val == NULL || val[0] == '\0') {
				fprintf(stderr, "aswlcomp: config: set: missing value for %s\n", key);
				continue;
			}

			if (str_ieq(key, "xkb_layout") || str_ieq(key, "keyboard_layout")) {
				aswl_set_opt_string(&server->xkb_layout, val);
				fprintf(stderr, "aswlcomp: config: xkb_layout=%s\n", server->xkb_layout != NULL ? server->xkb_layout : "(default)");
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "xkb_variant") || str_ieq(key, "keyboard_variant")) {
				aswl_set_opt_string(&server->xkb_variant, val);
				fprintf(stderr, "aswlcomp: config: xkb_variant=%s\n", server->xkb_variant != NULL ? server->xkb_variant : "(default)");
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "xkb_options") || str_ieq(key, "keyboard_options")) {
				aswl_set_opt_string(&server->xkb_options, val);
				fprintf(stderr, "aswlcomp: config: xkb_options=%s\n", server->xkb_options != NULL ? server->xkb_options : "(default)");
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "xkb_model") || str_ieq(key, "keyboard_model")) {
				aswl_set_opt_string(&server->xkb_model, val);
				fprintf(stderr, "aswlcomp: config: xkb_model=%s\n", server->xkb_model != NULL ? server->xkb_model : "(default)");
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "xkb_rules") || str_ieq(key, "keyboard_rules")) {
				aswl_set_opt_string(&server->xkb_rules, val);
				fprintf(stderr, "aswlcomp: config: xkb_rules=%s\n", server->xkb_rules != NULL ? server->xkb_rules : "(default)");
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "repeat_rate") || str_ieq(key, "keyboard_repeat_rate")) {
				char *end = NULL;
				long v = strtol(val, &end, 10);
				if (end == val || (end != NULL && *end != '\0') || v < 0 || v > 2000) {
					fprintf(stderr, "aswlcomp: config: repeat_rate: bad value: %s\n", val);
					continue;
				}
				server->repeat_rate = (int)v;
				fprintf(stderr, "aswlcomp: config: repeat_rate=%d\n", server->repeat_rate);
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "repeat_delay") || str_ieq(key, "keyboard_repeat_delay")) {
				char *end = NULL;
				long v = strtol(val, &end, 10);
				if (end == val || (end != NULL && *end != '\0') || v < 0 || v > 60000) {
					fprintf(stderr, "aswlcomp: config: repeat_delay: bad value: %s\n", val);
					continue;
				}
				server->repeat_delay = (int)v;
				fprintf(stderr, "aswlcomp: config: repeat_delay=%d\n", server->repeat_delay);
				aswl_apply_keyboard_config(server);
				continue;
			}

			if (str_ieq(key, "pointer_accel") || str_ieq(key, "pointer_acceleration")) {
				if (str_ieq(val, "default") || str_ieq(val, "none") || str_ieq(val, "null")) {
					server->have_pointer_accel = false;
					fprintf(stderr, "aswlcomp: config: pointer_accel=(default)\n");
					aswl_apply_pointer_config(server);
					continue;
				}

				char *end = NULL;
				double v = strtod(val, &end);
				if (end == val || end == NULL || *end != '\0' || v < -1.0 || v > 1.0) {
					fprintf(stderr, "aswlcomp: config: pointer_accel: bad value (use -1..1): %s\n", val);
					continue;
				}

				server->have_pointer_accel = true;
				server->pointer_accel = v;
				fprintf(stderr, "aswlcomp: config: pointer_accel=%.3f\n", server->pointer_accel);
				aswl_apply_pointer_config(server);
				continue;
			}

			if (str_ieq(key, "tap_to_click") || str_ieq(key, "tap")) {
				if (str_ieq(val, "default") || str_ieq(val, "none") || str_ieq(val, "null")) {
					server->have_tap_to_click = false;
					fprintf(stderr, "aswlcomp: config: tap_to_click=(default)\n");
					aswl_apply_pointer_config(server);
					continue;
				}

				bool enabled = false;
				if (!aswl_parse_bool(val, &enabled)) {
					fprintf(stderr, "aswlcomp: config: tap_to_click: bad value: %s\n", val);
					continue;
				}

				server->have_tap_to_click = true;
				server->tap_to_click = enabled;
				fprintf(stderr, "aswlcomp: config: tap_to_click=%d\n", server->tap_to_click ? 1 : 0);
				aswl_apply_pointer_config(server);
				continue;
			}

			fprintf(stderr, "aswlcomp: config: unknown key: %s\n", key);
			continue;
		}

		if (strncmp(cmd, "bind", 4) == 0 && isspace((unsigned char)cmd[4])) {
			char *rest = lstrip(cmd + 4);
			if (rest == NULL || rest[0] == '\0') {
				fprintf(stderr, "aswlcomp: bind: missing binding\n");
				continue;
			}

			char *combo = rest;
			while (*rest != '\0' && !isspace((unsigned char)*rest))
				rest++;
			if (*rest == '\0') {
				fprintf(stderr, "aswlcomp: bind: missing command\n");
				continue;
			}
			*rest++ = '\0';
			char *bind_cmd = lstrip(rest);
			rstrip_inplace(bind_cmd);
			if (bind_cmd[0] == '\0') {
				fprintf(stderr, "aswlcomp: bind: missing command\n");
				continue;
			}

			char *key_str = combo;
			char *mods_str = NULL;
			char *plus = strrchr(combo, '+');
			if (plus != NULL) {
				*plus = '\0';
				mods_str = combo;
				key_str = plus + 1;
			}

			key_str = lstrip(key_str);
			rstrip_inplace(key_str);
			if (key_str[0] == '\0') {
				fprintf(stderr, "aswlcomp: bind: missing key\n");
				continue;
			}

			xkb_keysym_t keysym = xkb_keysym_from_name(key_str, XKB_KEYSYM_CASE_INSENSITIVE);
			if (keysym == XKB_KEY_NoSymbol) {
				fprintf(stderr, "aswlcomp: bind: unknown keysym: %s\n", key_str);
				continue;
			}

			uint32_t mods = 0;
			if (mods_str != NULL && mods_str[0] != '\0') {
				if (!parse_modifiers(mods_str, &mods))
					continue;
			}

				if (strncmp(bind_cmd, "exec", 4) == 0 && isspace((unsigned char)bind_cmd[4])) {
					bind_cmd = lstrip(bind_cmd + 4);
					if (bind_cmd[0] == '\0') {
						fprintf(stderr, "aswlcomp: bind: missing command\n");
						continue;
					}
					fprintf(stderr, "aswlcomp: bind: mods=0x%x key=%s exec=%s\n", mods, key_str, bind_cmd);
					add_binding_exec(server, mods, keysym, bind_cmd);
					continue;
				}

				if ((strncmp(bind_cmd, "workspace", 9) == 0 && isspace((unsigned char)bind_cmd[9])) ||
				    (strncmp(bind_cmd, "ws", 2) == 0 && isspace((unsigned char)bind_cmd[2]))) {
					char *num = bind_cmd;
					if (strncmp(bind_cmd, "workspace", 9) == 0)
						num = lstrip(bind_cmd + 9);
					else
						num = lstrip(bind_cmd + 2);

					if (num == NULL || num[0] == '\0') {
						fprintf(stderr, "aswlcomp: bind: workspace: missing number\n");
						continue;
					}

					char *end = NULL;
					unsigned long ws = strtoul(num, &end, 10);
					if (end == num || (end != NULL && *end != '\0') || ws < 1 || ws > 1000) {
						fprintf(stderr, "aswlcomp: bind: workspace: bad number: %s\n", num);
						continue;
					}

					fprintf(stderr, "aswlcomp: bind: mods=0x%x key=%s workspace=%lu\n", mods, key_str, ws);
					add_binding_workspace_set(server, mods, keysym, (uint32_t)ws);
					continue;
				}

				int action = ASWL_BINDING_EXEC;
				if (str_ieq(bind_cmd, "quit") || str_ieq(bind_cmd, "exit")) {
					action = ASWL_BINDING_QUIT;
				} else if (str_ieq(bind_cmd, "close") || str_ieq(bind_cmd, "close_focused")) {
					action = ASWL_BINDING_CLOSE_FOCUSED;
				} else if (str_ieq(bind_cmd, "focus_next") || str_ieq(bind_cmd, "next")) {
					action = ASWL_BINDING_FOCUS_NEXT;
				} else if (str_ieq(bind_cmd, "focus_prev") || str_ieq(bind_cmd, "prev")) {
					action = ASWL_BINDING_FOCUS_PREV;
					} else if (str_ieq(bind_cmd, "workspace_next") || str_ieq(bind_cmd, "ws_next") || str_ieq(bind_cmd, "ws+")) {
						action = ASWL_BINDING_WORKSPACE_NEXT;
					} else if (str_ieq(bind_cmd, "workspace_prev") || str_ieq(bind_cmd, "ws_prev") || str_ieq(bind_cmd, "ws-")) {
						action = ASWL_BINDING_WORKSPACE_PREV;
					} else if (str_ieq(bind_cmd, "fullscreen") || str_ieq(bind_cmd, "toggle_fullscreen")) {
						action = ASWL_BINDING_TOGGLE_FULLSCREEN;
					} else if (str_ieq(bind_cmd, "maximized") || str_ieq(bind_cmd, "maximize") ||
					           str_ieq(bind_cmd, "toggle_maximized") || str_ieq(bind_cmd, "toggle_maximize")) {
						action = ASWL_BINDING_TOGGLE_MAXIMIZED;
					} else if (str_ieq(bind_cmd, "lock") || str_ieq(bind_cmd, "lock_now") || str_ieq(bind_cmd, "session_lock")) {
						action = ASWL_BINDING_LOCK;
					}

				if (action == ASWL_BINDING_EXEC) {
					fprintf(stderr, "aswlcomp: bind: missing exec prefix: %s\n", bind_cmd);
					continue;
			}

			fprintf(stderr, "aswlcomp: bind: mods=0x%x key=%s action=%s\n", mods, key_str, binding_action_name(action));
			add_binding_action(server, mods, keysym, action);
			continue;
		}

		if (strncmp(cmd, "exec", 4) == 0 && isspace((unsigned char)cmd[4]))
			cmd = lstrip(cmd + 4);

		if (cmd[0] == '\0')
			continue;

		fprintf(stderr, "aswlcomp: autostart: %s\n", cmd);
		spawn_command(cmd);
	}

	free(line);
	fclose(fp);
}

static bool aswl_idle_inhibit_is_active(struct aswl_server *server)
{
	if (server == NULL)
		return false;

	struct aswl_idle_inhibitor *inhib;
	wl_list_for_each(inhib, &server->idle_inhibitors, link) {
		if (inhib == NULL || inhib->wlr_inhibitor == NULL || inhib->wlr_inhibitor->surface == NULL)
			continue;
		if (inhib->wlr_inhibitor->surface->mapped)
			return true;
	}

	return false;
}

int aswl_idle_lock_timer_cb(void *data)
{
	struct aswl_server *server = data;
	if (server == NULL)
		return 0;
	if (server->idle_lock_seconds <= 0)
		return 0;
	if (server->session_locked || server->session_lock != NULL)
		return 0;

	if (aswl_idle_inhibit_is_active(server)) {
		if (getenv("ASWLCOMP_DEBUG_IDLE") != NULL)
			fprintf(stderr, "aswlcomp: idle-lock inhibited\n");
		if (server->idle_lock_timer != NULL) {
			(void)wl_event_source_timer_update(server->idle_lock_timer, server->idle_lock_seconds * 1000);
		}
		return 0;
	}

	fprintf(stderr, "aswlcomp: idle-lock\n");
	spawn_lock(server);
	return 0;
}

void aswl_idle_lock_note_activity(struct aswl_server *server)
{
	if (server == NULL)
		return;
	if (server->idle_lock_timer == NULL)
		return;
	if (server->idle_lock_seconds <= 0)
		return;
	if (server->session_locked || server->session_lock != NULL)
		return;

	int ms = server->idle_lock_seconds * 1000;
	if (ms <= 0)
		return;

	(void)wl_event_source_timer_update(server->idle_lock_timer, ms);
}

void aswl_idle_inhibit_refresh(struct aswl_server *server)
{
	if (server == NULL)
		return;

	bool inhibited = aswl_idle_inhibit_is_active(server);
	if (inhibited == server->idle_inhibited)
		return;

	server->idle_inhibited = inhibited;

	if (getenv("ASWLCOMP_DEBUG_IDLE") != NULL)
		fprintf(stderr, "aswlcomp: idle-inhibited=%s\n", inhibited ? "true" : "false");

	if (server->idle_notifier != NULL)
		wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, inhibited);

	/* When inhibition is cleared, restart the idle-lock timer from "now". */
	if (!inhibited)
		aswl_idle_lock_note_activity(server);
}

static void handle_idle_inhibitor_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_idle_inhibitor *inhib = wl_container_of(listener, inhib, destroy);
	if (inhib == NULL)
		return;

	struct aswl_server *server = inhib->server;

	wl_list_remove(&inhib->destroy.link);
	wl_list_remove(&inhib->surface_map.link);
	wl_list_remove(&inhib->surface_unmap.link);
	wl_list_remove(&inhib->link);
	free(inhib);

	aswl_idle_inhibit_refresh(server);
}

static void handle_idle_inhibitor_surface_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_idle_inhibitor *inhib = wl_container_of(listener, inhib, surface_map);
	if (inhib == NULL)
		return;

	aswl_idle_inhibit_refresh(inhib->server);
}

static void handle_idle_inhibitor_surface_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_idle_inhibitor *inhib = wl_container_of(listener, inhib, surface_unmap);
	if (inhib == NULL)
		return;

	aswl_idle_inhibit_refresh(inhib->server);
}

void handle_new_idle_inhibitor(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_idle_inhibitor);
	struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;
	if (server == NULL || wlr_inhibitor == NULL)
		return;

	struct aswl_idle_inhibitor *inhib = calloc(1, sizeof(*inhib));
	if (inhib == NULL)
		return;

	inhib->server = server;
	inhib->wlr_inhibitor = wlr_inhibitor;

	wl_list_init(&inhib->destroy.link);
	wl_list_init(&inhib->surface_map.link);
	wl_list_init(&inhib->surface_unmap.link);

	inhib->destroy.notify = handle_idle_inhibitor_destroy;
	wl_signal_add(&wlr_inhibitor->events.destroy, &inhib->destroy);

	struct wlr_surface *surface = wlr_inhibitor->surface;
	if (surface != NULL) {
		inhib->surface_map.notify = handle_idle_inhibitor_surface_map;
		wl_signal_add(&surface->events.map, &inhib->surface_map);

		inhib->surface_unmap.notify = handle_idle_inhibitor_surface_unmap;
		wl_signal_add(&surface->events.unmap, &inhib->surface_unmap);
	}

	wl_list_insert(&server->idle_inhibitors, &inhib->link);
	aswl_idle_inhibit_refresh(server);
}

void idle_inhibitors_destroy(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_idle_inhibitor *inhib;
	struct aswl_idle_inhibitor *tmp;
	wl_list_for_each_safe(inhib, tmp, &server->idle_inhibitors, link) {
		wl_list_remove(&inhib->destroy.link);
		wl_list_remove(&inhib->surface_map.link);
		wl_list_remove(&inhib->surface_unmap.link);
		wl_list_remove(&inhib->link);
		free(inhib);
	}

	server->idle_inhibited = false;
	if (server->idle_notifier != NULL)
		wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, false);
}
