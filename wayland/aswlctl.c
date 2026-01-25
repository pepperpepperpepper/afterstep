#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "afterstep-control-v1-client-protocol.h"

enum {
	ASWL_WINDOW_FLAG_MAPPED = 1u << 0,
	ASWL_WINDOW_FLAG_FOCUSED = 1u << 1,
	ASWL_WINDOW_FLAG_XWAYLAND = 1u << 2,
};

struct aswl_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct afterstep_control_v1 *control;
	uint32_t control_version;
	bool got_control;

	bool listing_windows;
	bool list_done;
};

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s COMMAND [ARGS...]\n", prog);
	fprintf(stderr, "\n");
	fprintf(stderr, "Commands:\n");
	fprintf(stderr, "  exec CMD...                       Execute a command (runs in compositor via /bin/sh -c)\n");
	fprintf(stderr, "  quit                              Exit the compositor\n");
	fprintf(stderr, "  close_focused                     Close the focused window\n");
	fprintf(stderr, "  focus_next | focus_prev           Change focus\n");
	fprintf(stderr, "  workspace N                       Switch to workspace N (1-based)\n");
	fprintf(stderr, "  workspace_next | workspace_prev   Switch workspaces\n");
	fprintf(stderr, "  list_windows                      Print window list snapshot\n");
	fprintf(stderr, "  focus_window ID                   Focus a window by ID\n");
	fprintf(stderr, "  close_window ID                   Close a window by ID\n");
	fprintf(stderr, "  move_window_to_workspace ID N      Move window to workspace N\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Notes:\n");
	fprintf(stderr, "  - This connects to $WAYLAND_DISPLAY.\n");
	fprintf(stderr, "  - For exec: if you pass multiple args, they are shell-escaped and joined.\n");
	fprintf(stderr, "           If you pass a single arg, it is sent verbatim (so you can use shell syntax).\n");
}

static bool parse_u32(const char *s, uint32_t min, uint32_t max, uint32_t *out)
{
	if (out != NULL)
		*out = 0;
	if (s == NULL || s[0] == '\0')
		return false;

	char *end = NULL;
	errno = 0;
	unsigned long n = strtoul(s, &end, 10);
	if (errno != 0 || end == s || end == NULL || *end != '\0')
		return false;
	if (n < min || n > max)
		return false;
	if (out != NULL)
		*out = (uint32_t)n;
	return true;
}

static char *shell_escape_arg(const char *arg)
{
	if (arg == NULL)
		return NULL;

	size_t len = 2; /* quotes */
	for (const char *p = arg; *p != '\0'; p++) {
		if (*p == '\'')
			len += 4; /* '\'' */
		else
			len += 1;
	}

	char *out = malloc(len + 1);
	if (out == NULL)
		return NULL;

	char *w = out;
	*w++ = '\'';
	for (const char *p = arg; *p != '\0'; p++) {
		if (*p == '\'') {
			*w++ = '\'';
			*w++ = '\\';
			*w++ = '\'';
			*w++ = '\'';
			continue;
		}
		*w++ = *p;
	}
	*w++ = '\'';
	*w = '\0';
	return out;
}

static char *join_exec_args_shell_escaped(int argc, char **argv, int start_index)
{
	if (argc <= start_index)
		return NULL;

	if (argc - start_index == 1)
		return strdup(argv[start_index]);

	char **parts = calloc((size_t)(argc - start_index), sizeof(*parts));
	if (parts == NULL)
		return NULL;

	size_t total = 0;
	for (int i = start_index; i < argc; i++) {
		parts[i - start_index] = shell_escape_arg(argv[i]);
		if (parts[i - start_index] == NULL)
			goto fail;
		total += strlen(parts[i - start_index]) + 1;
	}

	char *buf = malloc(total + 1);
	if (buf == NULL)
		goto fail;

	buf[0] = '\0';
	for (int i = 0; i < argc - start_index; i++) {
		if (i != 0)
			strcat(buf, " ");
		strcat(buf, parts[i]);
	}

	for (int i = 0; i < argc - start_index; i++)
		free(parts[i]);
	free(parts);

	return buf;

fail:
	for (int i = 0; i < argc - start_index; i++)
		free(parts[i]);
	free(parts);
	return NULL;
}

static void handle_control_workspace_state(void *data,
                                           struct afterstep_control_v1 *control,
                                           uint32_t current,
                                           uint32_t count)
{
	(void)control;
	struct aswl_state *st = data;
	if (st == NULL)
		return;

	/* Currently only used for future expansion; silence -Wunused-parameter. */
	(void)current;
	(void)count;
}

static void handle_control_window_list_begin(void *data, struct afterstep_control_v1 *control)
{
	(void)control;
	struct aswl_state *st = data;
	if (st == NULL)
		return;

	st->listing_windows = true;
}

static void handle_control_window(void *data,
                                  struct afterstep_control_v1 *control,
                                  uint32_t id,
                                  uint32_t workspace,
                                  uint32_t flags,
                                  const char *title,
                                  const char *app_id)
{
	(void)control;
	struct aswl_state *st = data;
	if (st == NULL)
		return;
	if (!st->listing_windows)
		return;

	const char *mapped = (flags & ASWL_WINDOW_FLAG_MAPPED) ? "mapped" : "-";
	const char *focused = (flags & ASWL_WINDOW_FLAG_FOCUSED) ? "focused" : "-";
	const char *xwayland = (flags & ASWL_WINDOW_FLAG_XWAYLAND) ? "xwayland" : "-";
	printf("%u\tws=%u\tflags=%u\t%s,%s,%s\tapp_id=%s\ttitle=%s\n",
	       id,
	       workspace,
	       flags,
	       mapped,
	       focused,
	       xwayland,
	       app_id != NULL ? app_id : "",
	       title != NULL ? title : "");
}

static void handle_control_window_list_end(void *data, struct afterstep_control_v1 *control)
{
	(void)control;
	struct aswl_state *st = data;
	if (st == NULL)
		return;
	st->list_done = true;
}

static void handle_control_window_closed(void *data, struct afterstep_control_v1 *control, uint32_t id)
{
	(void)control;
	struct aswl_state *st = data;
	if (st == NULL)
		return;
	if (!st->listing_windows)
		return;
	printf("%u\tclosed\n", id);
}

static const struct afterstep_control_v1_listener control_listener = {
	.workspace_state = handle_control_workspace_state,
	.window_list_begin = handle_control_window_list_begin,
	.window = handle_control_window,
	.window_list_end = handle_control_window_list_end,
	.window_closed = handle_control_window_closed,
};

static void handle_registry_global(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name,
                                   const char *interface,
                                   uint32_t version)
{
	struct aswl_state *st = data;
	if (st == NULL || registry == NULL || interface == NULL)
		return;

	if (strcmp(interface, afterstep_control_v1_interface.name) == 0) {
		uint32_t bind_version = version;
		if (bind_version > 4)
			bind_version = 4;
		st->control_version = bind_version;
		st->control = wl_registry_bind(registry, name, &afterstep_control_v1_interface, bind_version);
		st->got_control = st->control != NULL;
	}
}

static void handle_registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_registry_global,
	.global_remove = handle_registry_global_remove,
};

static bool connect_control(struct aswl_state *st)
{
	if (st == NULL)
		return false;

	st->display = wl_display_connect(NULL);
	if (st->display == NULL) {
		fprintf(stderr, "aswlctl: wl_display_connect failed (WAYLAND_DISPLAY?)\n");
		return false;
	}

	st->registry = wl_display_get_registry(st->display);
	if (st->registry == NULL) {
		fprintf(stderr, "aswlctl: wl_display_get_registry failed\n");
		return false;
	}

	wl_registry_add_listener(st->registry, &registry_listener, st);
	if (wl_display_roundtrip(st->display) < 0) {
		fprintf(stderr, "aswlctl: roundtrip failed while binding globals\n");
		return false;
	}

	if (st->control == NULL) {
		fprintf(stderr, "aswlctl: compositor does not advertise afterstep_control_v1\n");
		return false;
	}

	afterstep_control_v1_add_listener(st->control, &control_listener, st);
	return true;
}

static void disconnect_control(struct aswl_state *st)
{
	if (st == NULL)
		return;

	if (st->control != NULL) {
		afterstep_control_v1_destroy(st->control);
		st->control = NULL;
	}
	if (st->registry != NULL) {
		wl_registry_destroy(st->registry);
		st->registry = NULL;
	}
	if (st->display != NULL) {
		wl_display_disconnect(st->display);
		st->display = NULL;
	}
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage(argv[0]);
		return 2;
	}

	const char *cmd = argv[1];

	struct aswl_state st = { 0 };
	if (!connect_control(&st)) {
		disconnect_control(&st);
		return 1;
	}

	if (strcmp(cmd, "exec") == 0) {
		if (argc < 3) {
			fprintf(stderr, "aswlctl: exec: missing command\n");
			disconnect_control(&st);
			return 2;
		}
		char *joined = join_exec_args_shell_escaped(argc, argv, 2);
		if (joined == NULL) {
			fprintf(stderr, "aswlctl: exec: out of memory\n");
			disconnect_control(&st);
			return 1;
		}
		afterstep_control_v1_exec(st.control, joined);
		free(joined);
		(void)wl_display_flush(st.display);
		(void)wl_display_roundtrip(st.display);
		disconnect_control(&st);
		return 0;
	}

	if (strcmp(cmd, "quit") == 0) {
		afterstep_control_v1_quit(st.control);
		(void)wl_display_flush(st.display);
		disconnect_control(&st);
		return 0;
	}

	if (strcmp(cmd, "close_focused") == 0) {
		afterstep_control_v1_close_focused(st.control);
		(void)wl_display_flush(st.display);
		disconnect_control(&st);
		return 0;
	}

	if (strcmp(cmd, "focus_next") == 0) {
		afterstep_control_v1_focus_next(st.control);
		(void)wl_display_flush(st.display);
		disconnect_control(&st);
		return 0;
	}

	if (strcmp(cmd, "focus_prev") == 0) {
		afterstep_control_v1_focus_prev(st.control);
		(void)wl_display_flush(st.display);
		disconnect_control(&st);
		return 0;
	}

	if (strcmp(cmd, "workspace") == 0 || strcmp(cmd, "set_workspace") == 0) {
		if (argc < 3) {
			fprintf(stderr, "aswlctl: workspace: missing number\n");
			disconnect_control(&st);
			return 2;
		}
		uint32_t ws = 0;
		if (!parse_u32(argv[2], 1, 1000, &ws)) {
			fprintf(stderr, "aswlctl: workspace: invalid number: %s\n", argv[2]);
			disconnect_control(&st);
			return 2;
		}
		afterstep_control_v1_set_workspace(st.control, ws);
		(void)wl_display_flush(st.display);
		disconnect_control(&st);
		return 0;
	}

	if (strcmp(cmd, "workspace_next") == 0 || strcmp(cmd, "ws_next") == 0) {
		afterstep_control_v1_workspace_next(st.control);
		(void)wl_display_flush(st.display);
		disconnect_control(&st);
		return 0;
	}

	if (strcmp(cmd, "workspace_prev") == 0 || strcmp(cmd, "ws_prev") == 0) {
		afterstep_control_v1_workspace_prev(st.control);
		(void)wl_display_flush(st.display);
		disconnect_control(&st);
		return 0;
	}

	if (strcmp(cmd, "list_windows") == 0) {
		if (st.control_version < 4) {
			fprintf(stderr, "aswlctl: list_windows requires control protocol v4\n");
			disconnect_control(&st);
			return 1;
		}
		st.listing_windows = false;
		st.list_done = false;
		afterstep_control_v1_list_windows(st.control);
		(void)wl_display_flush(st.display);

		while (!st.list_done) {
			if (wl_display_dispatch(st.display) < 0) {
				fprintf(stderr, "aswlctl: dispatch failed\n");
				disconnect_control(&st);
				return 1;
			}
		}

		disconnect_control(&st);
		return 0;
	}

	if (strcmp(cmd, "focus_window") == 0) {
		if (st.control_version < 4) {
			fprintf(stderr, "aswlctl: focus_window requires control protocol v4\n");
			disconnect_control(&st);
			return 1;
		}
		if (argc < 3) {
			fprintf(stderr, "aswlctl: focus_window: missing id\n");
			disconnect_control(&st);
			return 2;
		}
		uint32_t id = 0;
		if (!parse_u32(argv[2], 1, UINT32_MAX, &id)) {
			fprintf(stderr, "aswlctl: focus_window: invalid id: %s\n", argv[2]);
			disconnect_control(&st);
			return 2;
		}
		afterstep_control_v1_focus_window(st.control, id);
		(void)wl_display_flush(st.display);
		disconnect_control(&st);
		return 0;
	}

	if (strcmp(cmd, "close_window") == 0) {
		if (st.control_version < 4) {
			fprintf(stderr, "aswlctl: close_window requires control protocol v4\n");
			disconnect_control(&st);
			return 1;
		}
		if (argc < 3) {
			fprintf(stderr, "aswlctl: close_window: missing id\n");
			disconnect_control(&st);
			return 2;
		}
		uint32_t id = 0;
		if (!parse_u32(argv[2], 1, UINT32_MAX, &id)) {
			fprintf(stderr, "aswlctl: close_window: invalid id: %s\n", argv[2]);
			disconnect_control(&st);
			return 2;
		}
		afterstep_control_v1_close_window(st.control, id);
		(void)wl_display_flush(st.display);
		disconnect_control(&st);
		return 0;
	}

	if (strcmp(cmd, "move_window_to_workspace") == 0 || strcmp(cmd, "move_window") == 0) {
		if (st.control_version < 4) {
			fprintf(stderr, "aswlctl: move_window_to_workspace requires control protocol v4\n");
			disconnect_control(&st);
			return 1;
		}
		if (argc < 4) {
			fprintf(stderr, "aswlctl: move_window_to_workspace: missing args\n");
			disconnect_control(&st);
			return 2;
		}
		uint32_t id = 0;
		uint32_t ws = 0;
		if (!parse_u32(argv[2], 1, UINT32_MAX, &id)) {
			fprintf(stderr, "aswlctl: move_window_to_workspace: invalid id: %s\n", argv[2]);
			disconnect_control(&st);
			return 2;
		}
		if (!parse_u32(argv[3], 1, 1000, &ws)) {
			fprintf(stderr, "aswlctl: move_window_to_workspace: invalid workspace: %s\n", argv[3]);
			disconnect_control(&st);
			return 2;
		}
		afterstep_control_v1_move_window_to_workspace(st.control, id, ws);
		(void)wl_display_flush(st.display);
		disconnect_control(&st);
		return 0;
	}

	fprintf(stderr, "aswlctl: unknown command: %s\n", cmd);
	usage(argv[0]);
	disconnect_control(&st);
	return 2;
}
