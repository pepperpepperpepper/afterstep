#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(HAVE_WLROOTS) && HAVE_WLROOTS

#include <wayland-server-core.h>

#include <linux/input-event-codes.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include <wlr/xwayland.h>

#include <wlr/util/edges.h>

#include <xkbcommon/xkbcommon.h>

#include "afterstep-control-v1-protocol.h"

enum aswl_cursor_mode {
	ASWL_CURSOR_PASSTHROUGH = 0,
	ASWL_CURSOR_MOVE,
	ASWL_CURSOR_RESIZE,
};

struct aswl_server;

enum {
	ASWL_WINDOW_FLAG_MAPPED = 1u << 0,
	ASWL_WINDOW_FLAG_FOCUSED = 1u << 1,
	ASWL_WINDOW_FLAG_XWAYLAND = 1u << 2,
};

struct aswl_view {
	struct wl_list link; /* aswl_server.views */
	struct aswl_server *server;
	uint32_t id;
	enum {
		ASWL_VIEW_XDG = 0,
		ASWL_VIEW_XWAYLAND,
	} type;
	struct wlr_xdg_surface *xdg_surface;
	struct wlr_xwayland_surface *xwayland_surface;
	struct wlr_scene_tree *scene_tree;
	bool mapped;
	bool is_dock;
	bool placed;
	uint32_t workspace;
	bool surface_listeners_added;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;

	struct wl_listener request_move;
	struct wl_listener request_resize;

	struct wl_listener set_title;
	struct wl_listener set_app_id;
	struct wl_listener set_class;

	struct wl_listener xwayland_associate;
	struct wl_listener xwayland_dissociate;
	struct wl_listener xwayland_request_configure;
};

struct aswl_keyboard {
	struct wl_list link; /* aswl_server.keyboards */
	struct aswl_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener key;
	struct wl_listener modifiers;
	struct wl_listener destroy;
};

struct aswl_layer_surface {
	struct wl_list link; /* aswl_server.layer_surfaces */
	struct aswl_server *server;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene;

	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
};

struct aswl_binding {
	struct wl_list link; /* aswl_server.bindings */
	uint32_t mods;
	xkb_keysym_t keysym;
	enum {
		ASWL_BINDING_EXEC = 0,
		ASWL_BINDING_QUIT,
		ASWL_BINDING_CLOSE_FOCUSED,
		ASWL_BINDING_FOCUS_NEXT,
		ASWL_BINDING_FOCUS_PREV,
		ASWL_BINDING_WORKSPACE_SET,
		ASWL_BINDING_WORKSPACE_NEXT,
		ASWL_BINDING_WORKSPACE_PREV,
	} action;
	uint32_t workspace;
	char *command;
};

struct aswl_output {
	struct wl_list link; /* aswl_server.outputs */
	struct aswl_server *server;
	struct wlr_output *wlr_output;
	struct wlr_box full_box;
	struct wlr_box usable_box;

	struct wl_listener destroy;
};

struct aswl_control_client {
	struct wl_list link; /* aswl_server.control_clients */
	struct aswl_server *server;
	struct wl_resource *resource;
};

struct aswl_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_compositor *compositor;
	char *state_path;

	struct wlr_output_layout *output_layout;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wl_list outputs;
	struct wl_list views;
	struct aswl_view *focused_view;
	int cascade_offset;
	uint32_t current_workspace;
	uint32_t workspace_count;
	uint32_t next_view_id;

	struct wl_list bindings;

	struct wlr_scene_tree *layer_trees[4];
	struct wlr_scene_tree *xdg_tree;
	struct wl_list layer_surfaces;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_xwayland *xwayland;
	struct wlr_seat *seat;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;

	struct xkb_context *xkb_context;
	struct wl_list keyboards;

	struct wl_listener new_output;
	struct wl_listener new_input;
	struct wl_listener new_xdg_surface;
	struct wl_listener new_xwayland_surface;
	struct wl_listener new_layer_surface;

	struct wl_listener request_cursor;

	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	enum aswl_cursor_mode cursor_mode;
	struct aswl_view *grabbed_view;
	double grab_lx;
	double grab_ly;
	int grab_view_lx;
	int grab_view_ly;
	int grab_view_width;
	int grab_view_height;
	uint32_t grab_edges;
	uint32_t grab_button;

	struct wl_global *control_global;
	struct wl_list control_clients;
};

static void arrange_layers(struct aswl_server *server);
static void focus_topmost_view(struct aswl_server *server);
static void focus_view(struct aswl_view *view, struct wlr_surface *surface);
static void place_view(struct aswl_view *view);
static void arrange_dock_views(struct aswl_server *server);
static void begin_interactive(struct aswl_view *view, enum aswl_cursor_mode mode, uint32_t edges, uint32_t button);
static void end_interactive(struct aswl_server *server);
static void close_focused_view(struct aswl_server *server);
static void focus_next_view(struct aswl_server *server);
static void focus_prev_view(struct aswl_server *server);
static bool str_ieq(const char *a, const char *b);
static char *lstrip(char *s);
static void rstrip_inplace(char *s);
static uint32_t normalize_workspace(struct aswl_server *server, uint32_t workspace);
static void set_workspace(struct aswl_server *server, uint32_t workspace);
static void workspace_next(struct aswl_server *server);
static void workspace_prev(struct aswl_server *server);
static void broadcast_workspace_state(struct aswl_server *server);
static void broadcast_window_state(struct aswl_server *server, struct aswl_view *view);
static void broadcast_window_closed(struct aswl_server *server, uint32_t id);
static void aswl_state_save(struct aswl_server *server);

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--socket NAME] [--autostart PATH] [--spawn CMD]... [--state PATH]\n", prog);
	fprintf(stderr, "  --socket NAME  Use a fixed WAYLAND_DISPLAY socket name\n");
	fprintf(stderr, "  --autostart PATH\n");
	fprintf(stderr, "                Spawn commands from a file.\n");
	fprintf(stderr, "                Lines are either: 'exec CMD' / 'CMD' / 'bind MODS+KEY exec CMD'\n");
	fprintf(stderr, "                Default: $XDG_CONFIG_HOME/afterstep/aswlcomp.autostart\n");
	fprintf(stderr, "                         or ~/.config/afterstep/aswlcomp.autostart\n");
	fprintf(stderr, "  --spawn CMD    Spawn a client command after startup (may be repeated)\n");
	fprintf(stderr, "                (CMD runs via /bin/sh -c with WAYLAND_DISPLAY set)\n");
	fprintf(stderr, "  --state PATH   Persist basic session state (default: $XDG_STATE_HOME/afterstep/aswlcomp.state)\n");
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

static int aswl_mkdir_p(const char *dir, mode_t mode)
{
	if (dir == NULL || dir[0] == '\0')
		return -1;

	char *path = strdup(dir);
	if (path == NULL)
		return -1;

	for (char *p = path + 1; *p != '\0'; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(path, mode) < 0 && errno != EEXIST) {
			free(path);
			return -1;
		}
		*p = '/';
	}

	if (mkdir(path, mode) < 0 && errno != EEXIST) {
		free(path);
		return -1;
	}

	free(path);
	return 0;
}

static int aswl_ensure_parent_dir(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return -1;

	char *dup = strdup(path);
	if (dup == NULL)
		return -1;

	char *slash = strrchr(dup, '/');
	if (slash == NULL) {
		free(dup);
		return 0;
	}
	if (slash == dup) {
		free(dup);
		return 0;
	}
	*slash = '\0';
	int rc = aswl_mkdir_p(dup, 0700);
	free(dup);
	return rc;
}

static bool parse_u32_strict(const char *s, uint32_t min, uint32_t max, uint32_t *out)
{
	if (out == NULL)
		return false;
	*out = 0;

	if (s == NULL)
		return false;

	char *end = NULL;
	errno = 0;
	unsigned long n = strtoul(s, &end, 10);
	if (errno != 0)
		return false;
	if (end == s || end == NULL || *end != '\0')
		return false;
	if (n < min || n > max)
		return false;

	*out = (uint32_t)n;
	return true;
}

static char *aswl_state_path_default(void)
{
	const char *state_home = getenv("XDG_STATE_HOME");
	if (state_home != NULL && state_home[0] != '\0')
		return xstrdup_printf("%s/afterstep/aswlcomp.state", state_home);

	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0')
		return xstrdup_printf("%s/.local/state/afterstep/aswlcomp.state", home);

	return NULL;
}

static char *aswl_state_path_resolve(const char *override)
{
	if (override != NULL && override[0] != '\0')
		return strdup(override);

	const char *env = getenv("ASWLCOMP_STATE");
	if (env != NULL && env[0] != '\0')
		return strdup(env);

	return aswl_state_path_default();
}

static void aswl_state_load(struct aswl_server *server, bool allow_workspace_count_override)
{
	if (server == NULL || server->state_path == NULL)
		return;

	FILE *fp = fopen(server->state_path, "r");
	if (fp == NULL)
		return;

	char *line = NULL;
	size_t cap = 0;
	uint32_t loaded_ws = 0;
	uint32_t loaded_count = 0;
	bool have_ws = false;
	bool have_count = false;

	while (getline(&line, &cap, fp) != -1) {
		rstrip_inplace(line);
		char *s = lstrip(line);
		if (s[0] == '\0' || s[0] == '#' || s[0] == ';')
			continue;

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';

		char *key = lstrip(s);
		rstrip_inplace(key);

		char *val = lstrip(eq + 1);
		rstrip_inplace(val);

		if (str_ieq(key, "current_workspace")) {
			uint32_t ws = 0;
			if (parse_u32_strict(val, 1, 1000, &ws)) {
				loaded_ws = ws;
				have_ws = true;
			}
			continue;
		}

		if (str_ieq(key, "workspace_count")) {
			uint32_t count = 0;
			if (parse_u32_strict(val, 1, 1000, &count)) {
				loaded_count = count;
				have_count = true;
			}
			continue;
		}
	}

	free(line);
	fclose(fp);

	if (have_count && allow_workspace_count_override)
		server->workspace_count = loaded_count;

	if (have_ws)
		server->current_workspace = loaded_ws;
	server->current_workspace = normalize_workspace(server, server->current_workspace);
}

static void aswl_state_save(struct aswl_server *server)
{
	if (server == NULL || server->state_path == NULL)
		return;

	if (aswl_ensure_parent_dir(server->state_path) != 0) {
		fprintf(stderr, "aswlcomp: state: ensure dir failed: %s\n", server->state_path);
		return;
	}

	char *tmp = xstrdup_printf("%s.XXXXXX", server->state_path);
	if (tmp == NULL)
		return;

	int fd = mkstemp(tmp);
	if (fd < 0) {
		fprintf(stderr, "aswlcomp: state: mkstemp failed: %s: %s\n", tmp, strerror(errno));
		free(tmp);
		return;
	}

	FILE *fp = fdopen(fd, "w");
	if (fp == NULL) {
		(void)close(fd);
		(void)unlink(tmp);
		free(tmp);
		return;
	}

	fprintf(fp, "# aswlcomp state v1\n");
	fprintf(fp, "workspace_count=%u\n", server->workspace_count);
	fprintf(fp, "current_workspace=%u\n", server->current_workspace);
	(void)fflush(fp);

	bool ok = !ferror(fp);
	if (ok)
		ok = fsync(fd) == 0;

	(void)fclose(fp);

	if (!ok) {
		(void)unlink(tmp);
		free(tmp);
		return;
	}

	if (rename(tmp, server->state_path) < 0) {
		fprintf(stderr, "aswlcomp: state: rename failed: %s -> %s: %s\n", tmp, server->state_path, strerror(errno));
		(void)unlink(tmp);
	}

	free(tmp);
}

static void broadcast_workspace_state(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_control_client *cc;
	wl_list_for_each(cc, &server->control_clients, link) {
		if (cc->resource == NULL)
			continue;
		if (wl_resource_get_version(cc->resource) < 3)
			continue;
		afterstep_control_v1_send_workspace_state(cc->resource, server->current_workspace, server->workspace_count);
	}
}

static const char *view_title(struct aswl_view *view)
{
	if (view == NULL)
		return "";

	if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL && view->xdg_surface->toplevel->title != NULL)
		return view->xdg_surface->toplevel->title;

	if (view->xwayland_surface != NULL && view->xwayland_surface->title != NULL)
		return view->xwayland_surface->title;

	return "";
}

static const char *view_app_id(struct aswl_view *view)
{
	if (view == NULL)
		return "";

	if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL && view->xdg_surface->toplevel->app_id != NULL)
		return view->xdg_surface->toplevel->app_id;

	if (view->xwayland_surface != NULL && view->xwayland_surface->class != NULL)
		return view->xwayland_surface->class;

	return "";
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

static void send_window_state(struct aswl_server *server, struct wl_resource *resource, struct aswl_view *view)
{
	if (server == NULL || resource == NULL || view == NULL)
		return;
	if (wl_resource_get_version(resource) < 4)
		return;
	if (view->id == 0)
		return;
	if (view->is_dock)
		return;

	uint32_t flags = view_window_flags(server, view);
	afterstep_control_v1_send_window(resource, view->id, view->workspace, flags, view_title(view), view_app_id(view));
}

static void send_window_list_snapshot(struct aswl_server *server, struct wl_resource *resource)
{
	if (server == NULL || resource == NULL)
		return;
	if (wl_resource_get_version(resource) < 4)
		return;

	afterstep_control_v1_send_window_list_begin(resource);
	struct aswl_view *view;
	wl_list_for_each(view, &server->views, link)
		send_window_state(server, resource, view);
	afterstep_control_v1_send_window_list_end(resource);
}

static void broadcast_window_state(struct aswl_server *server, struct aswl_view *view)
{
	if (server == NULL || view == NULL)
		return;

	struct aswl_control_client *cc;
	wl_list_for_each(cc, &server->control_clients, link) {
		if (cc->resource == NULL)
			continue;
		send_window_state(server, cc->resource, view);
	}
}

static void broadcast_window_closed(struct aswl_server *server, uint32_t id)
{
	if (server == NULL || id == 0)
		return;

	struct aswl_control_client *cc;
	wl_list_for_each(cc, &server->control_clients, link) {
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
	spawn_command(command);

	if (server != NULL)
		wl_display_flush_clients(server->display);
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
	wl_display_flush_clients(server->display);
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
	wl_display_flush_clients(server->display);
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
	wl_display_flush_clients(server->display);
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
	wl_display_flush_clients(server->display);
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
	wl_display_flush_clients(server->display);
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
	wl_display_flush_clients(server->display);
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
	wl_display_flush_clients(server->display);
}

static void aswl_control_focus_window(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	struct aswl_view *view = find_view_by_id(server, id);
	if (view == NULL || !view->mapped || view->scene_tree == NULL)
		return;

	fprintf(stderr, "aswlcomp: control focus_window=%u\n", id);
	if (view->workspace != server->current_workspace)
		set_workspace(server, view->workspace);
	focus_view(view, NULL);
	wl_display_flush_clients(server->display);
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
	wl_display_flush_clients(server->display);
}

static void aswl_control_move_window_to_workspace(struct wl_client *client, struct wl_resource *resource, uint32_t id, uint32_t workspace)
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
		focus_topmost_view(server);
	}

	broadcast_window_state(server, view);
	wl_display_flush_clients(server->display);
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
};

static void aswl_control_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct aswl_server *server = data;
	uint32_t v = version > 4 ? 4 : version;
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
	if (v >= 4)
		send_window_list_snapshot(server, res);
}

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

static const char *binding_action_name(int action)
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
	default:
		return "exec";
	}
}

static bool default_autostart_path(char *out, size_t out_size)
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

static void load_config_file(struct aswl_server *server, const char *path, bool log_missing)
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

static struct wlr_surface *surface_at(struct aswl_server *server, double lx, double ly, double *sx, double *sy)
{
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER)
		return NULL;

	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	if (scene_buffer == NULL)
		return NULL;

	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (scene_surface == NULL)
		return NULL;

	return scene_surface->surface;
}

static struct aswl_view *view_from_wlr_surface(struct wlr_surface *surface)
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

static void focus_view(struct aswl_view *view, struct wlr_surface *surface)
{
	if (view == NULL || view->server == NULL)
		return;

	struct aswl_server *server = view->server;
	struct aswl_view *old_focus = server->focused_view;

	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(server->views.prev, &view->link);

	if (old_focus != view) {
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
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard != NULL) {
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
		}

		if (view->xwayland_surface != NULL)
			wlr_xwayland_surface_offer_focus(view->xwayland_surface);
	}

	if (old_focus != view) {
		if (old_focus != NULL)
			broadcast_window_state(server, old_focus);
		broadcast_window_state(server, view);
	}
}

static void close_focused_view(struct aswl_server *server)
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
	if (view->is_dock)
		return false;
	return view->workspace == server->current_workspace;
}

static void focus_next_view(struct aswl_server *server)
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

static void focus_prev_view(struct aswl_server *server)
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

static void focus_topmost_view(struct aswl_server *server)
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

static uint32_t normalize_workspace(struct aswl_server *server, uint32_t workspace)
{
	if (workspace < 1)
		workspace = 1;

	if (server != NULL && server->workspace_count > 0 && workspace > server->workspace_count) {
		workspace = ((workspace - 1) % server->workspace_count) + 1;
	}

	return workspace;
}

static void set_workspace(struct aswl_server *server, uint32_t workspace)
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
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		broadcast_window_state(server, old);
	}

	struct aswl_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->scene_tree == NULL)
			continue;
		bool enabled = view->mapped && (view->is_dock || view->workspace == server->current_workspace);
		wlr_scene_node_set_enabled(&view->scene_tree->node, enabled);
		if (enabled && !view->is_dock)
			place_view(view);
	}

	arrange_dock_views(server);
	focus_topmost_view(server);
	broadcast_workspace_state(server);
	aswl_state_save(server);
}

static void workspace_next(struct aswl_server *server)
{
	if (server == NULL)
		return;

	uint32_t count = server->workspace_count > 0 ? server->workspace_count : 1;
	uint32_t next = server->current_workspace + 1;
	if (next > count)
		next = 1;
	set_workspace(server, next);
}

static void workspace_prev(struct aswl_server *server)
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

static bool view_is_xwayland_dockapp(struct aswl_view *view)
{
	if (view == NULL || view->xwayland_surface == NULL)
		return false;
	if (!wlr_xwayland_surface_has_window_type(view->xwayland_surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DOCK))
		return false;

	/*
	 * _NET_WM_WINDOW_TYPE_DOCK is also used by panels. We only want "dockapps"
	 * here (small, Wharf-like widgets), not full-width bars.
	 */
	uint16_t w = view->xwayland_surface->width;
	uint16_t h = view->xwayland_surface->height;
	if (w > 0 && w > 512)
		return false;
	if (h > 0 && h > 512)
		return false;

	return true;
}

static void view_maybe_mark_dock(struct aswl_view *view)
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

static void view_get_current_size(struct aswl_view *view, int *width, int *height)
{
	int w = 0;
	int h = 0;

	if (view != NULL && view->xwayland_surface != NULL) {
		w = view->xwayland_surface->width;
		h = view->xwayland_surface->height;
	}

	if (view != NULL && view->xdg_surface != NULL && view->xdg_surface->surface != NULL) {
		w = view->xdg_surface->surface->current.width;
		h = view->xdg_surface->surface->current.height;
	}
	if (view != NULL && view->xdg_surface != NULL) {
		if (w <= 0)
			w = view->xdg_surface->geometry.width;
		if (h <= 0)
			h = view->xdg_surface->geometry.height;
	}

	if (width != NULL)
		*width = w;
	if (height != NULL)
		*height = h;
}

static const char *xcursor_for_resize_edges(uint32_t edges)
{
	if ((edges & (WLR_EDGE_TOP | WLR_EDGE_LEFT)) == (WLR_EDGE_TOP | WLR_EDGE_LEFT))
		return "top_left_corner";
	if ((edges & (WLR_EDGE_TOP | WLR_EDGE_RIGHT)) == (WLR_EDGE_TOP | WLR_EDGE_RIGHT))
		return "top_right_corner";
	if ((edges & (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT)) == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT))
		return "bottom_left_corner";
	if ((edges & (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT)) == (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT))
		return "bottom_right_corner";
	if ((edges & WLR_EDGE_TOP) != 0)
		return "top_side";
	if ((edges & WLR_EDGE_BOTTOM) != 0)
		return "bottom_side";
	if ((edges & WLR_EDGE_LEFT) != 0)
		return "left_side";
	if ((edges & WLR_EDGE_RIGHT) != 0)
		return "right_side";
	return "left_ptr";
}

static void arrange_dock_views(struct aswl_server *server)
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

	const int pad = 12;
	const int spacing = 8;

	int x = usable.x + pad;
	int y_bottom = usable.y + usable.height - pad;
	int row_h = 0;

	struct aswl_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->is_dock || !view->mapped || view->scene_tree == NULL)
			continue;

		int w = 0;
		int h = 0;
		view_get_current_size(view, &w, &h);
		if (w <= 0)
			w = 64;
		if (h <= 0)
			h = 64;

		int max_x = usable.x + usable.width - pad;
		if (x + w > max_x && x > usable.x + pad) {
			x = usable.x + pad;
			y_bottom -= row_h + spacing;
			row_h = 0;
		}

		int y = y_bottom - h;
		if (y < usable.y + pad)
			y = usable.y + pad;

		wlr_scene_node_set_position(&view->scene_tree->node, x, y);
		wlr_scene_node_raise_to_top(&view->scene_tree->node);

		if (view->xwayland_surface != NULL) {
			uint16_t ww = view->xwayland_surface->width;
			uint16_t hh = view->xwayland_surface->height;
			if (ww == 0)
				ww = (uint16_t)w;
			if (hh == 0)
				hh = (uint16_t)h;
			wlr_xwayland_surface_configure(view->xwayland_surface, x, y, ww, hh);
		}

		view->placed = true;

		x += w + spacing;
		if (h > row_h)
			row_h = h;
	}
}

static void place_view(struct aswl_view *view)
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
	view_get_current_size(view, &width, &height);

	int x = usable.x + 40 + server->cascade_offset;
	int y = usable.y + 40 + server->cascade_offset;
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
		wlr_xwayland_surface_configure(view->xwayland_surface, x, y, (uint16_t)width, (uint16_t)height);
	}
	view->placed = true;

	server->cascade_offset += 32;
	if (server->cascade_offset >= 256)
		server->cascade_offset = 0;
}

static void begin_interactive(struct aswl_view *view, enum aswl_cursor_mode mode, uint32_t edges, uint32_t button)
{
	if (view == NULL || view->server == NULL)
		return;
	struct aswl_server *server = view->server;

	if (server->cursor == NULL || server->cursor_mgr == NULL)
		return;

	focus_view(view, NULL);

	server->cursor_mode = mode;
	server->grabbed_view = view;
	server->grab_lx = server->cursor->x;
	server->grab_ly = server->cursor->y;
	server->grab_edges = edges;
	server->grab_button = button;

	int lx = 0;
	int ly = 0;
	(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);
	server->grab_view_lx = lx;
	server->grab_view_ly = ly;

	view_get_current_size(view, &server->grab_view_width, &server->grab_view_height);
	if (server->grab_view_width <= 0)
		server->grab_view_width = 1;
	if (server->grab_view_height <= 0)
		server->grab_view_height = 1;

	switch (mode) {
	case ASWL_CURSOR_MOVE:
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "grabbing");
		break;
	case ASWL_CURSOR_RESIZE:
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_resizing(view->xdg_surface->toplevel, true);
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, xcursor_for_resize_edges(edges));
		break;
	case ASWL_CURSOR_PASSTHROUGH:
	default:
		break;
	}
}

static void end_interactive(struct aswl_server *server)
{
	if (server == NULL)
		return;

	if (server->cursor_mode == ASWL_CURSOR_RESIZE) {
		struct aswl_view *view = server->grabbed_view;
		if (view != NULL && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_resizing(view->xdg_surface->toplevel, false);
	}

	server->cursor_mode = ASWL_CURSOR_PASSTHROUGH;
	server->grabbed_view = NULL;
	server->grab_button = 0;
	if (server->cursor != NULL && server->cursor_mgr != NULL)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
}

static void handle_view_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, map);
	struct aswl_server *server = view->server;

	view->mapped = true;
	view_maybe_mark_dock(view);

	bool enabled = server != NULL && (view->is_dock || view->workspace == server->current_workspace);
	wlr_scene_node_set_enabled(&view->scene_tree->node, enabled);

	if (enabled) {
		if (view->is_dock) {
			arrange_dock_views(server);
		} else {
			place_view(view);
			focus_view(view, NULL);
		}
	} else if (server != NULL) {
		broadcast_window_state(server, view);
	}
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, unmap);
	struct aswl_server *server = view->server;

	view->mapped = false;
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	if (server != NULL && server->grabbed_view == view) {
		end_interactive(server);
	}

	if (server != NULL && server->focused_view == view) {
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, false);
		else if (view->xwayland_surface != NULL)
			wlr_xwayland_surface_activate(view->xwayland_surface, false);
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		focus_topmost_view(server);
	}

	if (server != NULL)
		broadcast_window_state(server, view);

	if (server != NULL && view->is_dock)
		arrange_dock_views(server);
}

static void handle_view_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, destroy);
	struct aswl_server *server = view->server;
	uint32_t id = view->id;

	if (view->surface_listeners_added) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		view->surface_listeners_added = false;
	}
	if (server != NULL)
		broadcast_window_closed(server, id);

	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	if (view->type == ASWL_VIEW_XDG) {
		wl_list_remove(&view->set_title.link);
		wl_list_remove(&view->set_app_id.link);
	} else if (view->type == ASWL_VIEW_XWAYLAND) {
		wl_list_remove(&view->set_title.link);
		wl_list_remove(&view->set_class.link);
	}
	if (view->type == ASWL_VIEW_XWAYLAND) {
		wl_list_remove(&view->xwayland_associate.link);
		wl_list_remove(&view->xwayland_dissociate.link);
		wl_list_remove(&view->xwayland_request_configure.link);
	}
	wl_list_remove(&view->link);

	if (server != NULL && server->grabbed_view == view) {
		end_interactive(server);
	}

	if (server != NULL && server->focused_view == view) {
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		focus_topmost_view(server);
	}

	if (server != NULL && view->is_dock)
		arrange_dock_views(server);

	if (view->type == ASWL_VIEW_XWAYLAND && view->scene_tree != NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
	}

	free(view);
}

static void handle_view_set_title(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, set_title);
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
}

static void handle_view_set_app_id(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, set_app_id);
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
}

static void handle_view_set_class(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, set_class);
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
}

static void handle_request_move(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, request_move);
	struct aswl_server *server = view->server;
	struct wlr_xdg_toplevel_move_event *event = data;

	if (server == NULL || server->cursor == NULL)
		return;

	if (event != NULL && !wlr_seat_validate_pointer_grab_serial(server->seat, view->xdg_surface->surface, event->serial))
		return;

	begin_interactive(view, ASWL_CURSOR_MOVE, 0, 0);
}

static void handle_request_resize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, request_resize);
	struct aswl_server *server = view->server;
	struct wlr_xdg_toplevel_resize_event *event = data;

	if (server == NULL || server->cursor == NULL)
		return;

	if (event == NULL)
		return;
	if (!wlr_seat_validate_pointer_grab_serial(server->seat, view->xdg_surface->surface, event->serial))
		return;

	begin_interactive(view, ASWL_CURSOR_RESIZE, event->edges, 0);
}

static void xwayland_attach_surface(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL || view->xwayland_surface == NULL)
		return;
	struct aswl_server *server = view->server;
	struct wlr_xwayland_surface *xsurface = view->xwayland_surface;

	if (xsurface->surface == NULL)
		return;

	if (view->scene_tree != NULL)
		return;

	view->scene_tree = wlr_scene_subsurface_tree_create(server->xdg_tree, xsurface->surface);
	if (view->scene_tree == NULL)
		return;

	wlr_scene_node_set_position(&view->scene_tree->node, xsurface->x, xsurface->y);

	/* Start hidden until the client maps. */
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	view->map.notify = handle_view_map;
	wl_signal_add(&xsurface->surface->events.map, &view->map);

	view->unmap.notify = handle_view_unmap;
	wl_signal_add(&xsurface->surface->events.unmap, &view->unmap);
	view->surface_listeners_added = true;
}

static void xwayland_detach_surface(struct aswl_view *view)
{
	if (view == NULL)
		return;

	if (view->surface_listeners_added) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		view->surface_listeners_added = false;
	}

	if (view->scene_tree != NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
	}

	view->mapped = false;
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
}

static void handle_xwayland_associate(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, xwayland_associate);
	xwayland_attach_surface(view);
}

static void handle_xwayland_dissociate(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, xwayland_dissociate);
	struct aswl_server *server = view->server;

	if (server != NULL && server->grabbed_view == view)
		end_interactive(server);

	if (server != NULL && server->focused_view == view) {
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		focus_topmost_view(server);
	}

	xwayland_detach_surface(view);
}

static void handle_xwayland_request_configure(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, xwayland_request_configure);
	struct wlr_xwayland_surface_configure_event *event = data;

	if (view == NULL || view->xwayland_surface == NULL || view->scene_tree == NULL || event == NULL)
		return;

	if (view->is_dock) {
		arrange_dock_views(view->server);
		return;
	}

	wlr_scene_node_set_position(&view->scene_tree->node, event->x, event->y);
	wlr_xwayland_surface_configure(view->xwayland_surface, event->x, event->y, event->width, event->height);
}

static void handle_xwayland_request_move(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_move);
	if (view->is_dock)
		return;
	if (view->server == NULL || view->server->cursor == NULL)
		return;
	begin_interactive(view, ASWL_CURSOR_MOVE, 0, 0);
}

static void handle_xwayland_request_resize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xwayland_resize_event *event = data;
	if (view->is_dock)
		return;
	if (view->server == NULL || view->server->cursor == NULL || event == NULL)
		return;
	begin_interactive(view, ASWL_CURSOR_RESIZE, event->edges, 0);
}

static void handle_new_xwayland_surface(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *xsurface = data;

	if (xsurface == NULL)
		return;

	struct aswl_view *view = calloc(1, sizeof(*view));
	if (view == NULL)
		return;

	view->server = server;
	view->type = ASWL_VIEW_XWAYLAND;
	view->xwayland_surface = xsurface;
	view->workspace = server->current_workspace;
	view->id = server->next_view_id++;
	if (server->next_view_id == 0)
		server->next_view_id = 1;
	xsurface->data = view;

	wl_list_insert(server->views.prev, &view->link);

	view->destroy.notify = handle_view_destroy;
	wl_signal_add(&xsurface->events.destroy, &view->destroy);

	view->set_title.notify = handle_view_set_title;
	wl_signal_add(&xsurface->events.set_title, &view->set_title);

	view->set_class.notify = handle_view_set_class;
	wl_signal_add(&xsurface->events.set_class, &view->set_class);

	view->xwayland_associate.notify = handle_xwayland_associate;
	wl_signal_add(&xsurface->events.associate, &view->xwayland_associate);

	view->xwayland_dissociate.notify = handle_xwayland_dissociate;
	wl_signal_add(&xsurface->events.dissociate, &view->xwayland_dissociate);

	view->xwayland_request_configure.notify = handle_xwayland_request_configure;
	wl_signal_add(&xsurface->events.request_configure, &view->xwayland_request_configure);

	view->request_move.notify = handle_xwayland_request_move;
	wl_signal_add(&xsurface->events.request_move, &view->request_move);

	view->request_resize.notify = handle_xwayland_request_resize;
	wl_signal_add(&xsurface->events.request_resize, &view->request_resize);

	/* Xwayland may already have an associated wlr_surface. */
	xwayland_attach_surface(view);
}

static void handle_new_xdg_surface(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;

	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return;

	struct aswl_view *view = calloc(1, sizeof(*view));
	if (view == NULL)
		return;

	view->server = server;
	view->type = ASWL_VIEW_XDG;
	view->xdg_surface = xdg_surface;
	view->workspace = server->current_workspace;
	view->id = server->next_view_id++;
	if (server->next_view_id == 0)
		server->next_view_id = 1;
	view->scene_tree = wlr_scene_xdg_surface_create(server->xdg_tree, xdg_surface);
	if (view->scene_tree == NULL) {
		free(view);
		return;
	}
	xdg_surface->data = view;
	wl_list_insert(server->views.prev, &view->link);

	/* Start hidden until the client maps. */
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	view->map.notify = handle_view_map;
	wl_signal_add(&xdg_surface->surface->events.map, &view->map);

	view->unmap.notify = handle_view_unmap;
	wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
	view->surface_listeners_added = true;

	view->destroy.notify = handle_view_destroy;
	wl_signal_add(&xdg_surface->events.destroy, &view->destroy);

	view->set_title.notify = handle_view_set_title;
	wl_signal_add(&xdg_surface->toplevel->events.set_title, &view->set_title);

	view->set_app_id.notify = handle_view_set_app_id;
	wl_signal_add(&xdg_surface->toplevel->events.set_app_id, &view->set_app_id);

	view->request_move.notify = handle_request_move;
	wl_signal_add(&xdg_surface->toplevel->events.request_move, &view->request_move);

	view->request_resize.notify = handle_request_resize;
	wl_signal_add(&xdg_surface->toplevel->events.request_resize, &view->request_resize);

	/* Initial placement (very naive for now). */
	wlr_scene_node_set_position(&view->scene_tree->node, 80, 120);
}

static void handle_keyboard_key(struct wl_listener *listener, void *data)
{
	struct aswl_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct aswl_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		uint32_t mods = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
		uint32_t mods_masked = mods & (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT | WLR_MODIFIER_LOGO);
		uint32_t keycode = event->keycode + 8;

		const xkb_keysym_t *syms = NULL;
		int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);
		if (nsyms == 1) {
			xkb_keysym_t sym = syms[0];

			if (sym == XKB_KEY_Escape && (mods_masked & WLR_MODIFIER_ALT) != 0) {
				fprintf(stderr, "aswlcomp: Alt+Escape: exit\n");
				wl_display_terminate(server->display);
				return;
			}

			struct aswl_binding *b;
			wl_list_for_each(b, &server->bindings, link) {
				if (b->mods == mods_masked && b->keysym == sym) {
					fprintf(stderr, "aswlcomp: bind action=%s\n", binding_action_name(b->action));
					switch (b->action) {
					case ASWL_BINDING_QUIT:
						wl_display_terminate(server->display);
						break;
					case ASWL_BINDING_CLOSE_FOCUSED:
						close_focused_view(server);
						break;
					case ASWL_BINDING_FOCUS_NEXT:
						focus_next_view(server);
						break;
					case ASWL_BINDING_FOCUS_PREV:
						focus_prev_view(server);
						break;
					case ASWL_BINDING_WORKSPACE_SET:
						set_workspace(server, b->workspace);
						break;
					case ASWL_BINDING_WORKSPACE_NEXT:
						workspace_next(server);
						break;
					case ASWL_BINDING_WORKSPACE_PREV:
						workspace_prev(server);
						break;
					case ASWL_BINDING_EXEC:
					default:
						if (b->command != NULL && b->command[0] != '\0') {
							fprintf(stderr, "aswlcomp: exec: %s\n", b->command);
							spawn_command(b->command);
						}
						break;
					}
					return;
				}
			}
		}
	}

	wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
}

static void handle_keyboard_modifiers(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	struct aswl_server *server = keyboard->server;

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(server->seat, &keyboard->wlr_keyboard->modifiers);
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);

	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void handle_new_input(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_POINTER:
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_TABLET:
		wlr_cursor_attach_input_device(server->cursor, device);
		wlr_seat_set_capabilities(server->seat, server->seat->capabilities | WL_SEAT_CAPABILITY_POINTER);
		break;
	case WLR_INPUT_DEVICE_KEYBOARD:
	{
		struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

		if (server->xkb_context == NULL)
			server->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

		if (server->xkb_context != NULL) {
			struct xkb_keymap *keymap = xkb_keymap_new_from_names(server->xkb_context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
			if (keymap != NULL) {
				(void)wlr_keyboard_set_keymap(wlr_keyboard, keymap);
				xkb_keymap_unref(keymap);
			}
		}

		wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

		struct aswl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
		if (keyboard == NULL)
			return;

		keyboard->server = server;
		keyboard->wlr_keyboard = wlr_keyboard;

		keyboard->key.notify = handle_keyboard_key;
		wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

		keyboard->modifiers.notify = handle_keyboard_modifiers;
		wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

		keyboard->destroy.notify = handle_keyboard_destroy;
		wl_signal_add(&device->events.destroy, &keyboard->destroy);

		wl_list_insert(&server->keyboards, &keyboard->link);

		wlr_seat_set_keyboard(server->seat, wlr_keyboard);
		wlr_seat_set_capabilities(server->seat, server->seat->capabilities | WL_SEAT_CAPABILITY_KEYBOARD);
		break;
	}
	default:
		break;
	}
}

static void handle_request_cursor(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;

	if (server->cursor_mode != ASWL_CURSOR_PASSTHROUGH)
		return;

	if (server->seat->pointer_state.focused_client != event->seat_client)
		return;

	wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

static void process_cursor_motion(struct aswl_server *server, uint32_t time_msec)
{
	if (server->cursor_mode == ASWL_CURSOR_MOVE) {
		struct aswl_view *view = server->grabbed_view;
		if (view == NULL)
			return;

		double dx = server->cursor->x - server->grab_lx;
		double dy = server->cursor->y - server->grab_ly;

		int nx = server->grab_view_lx + (int)dx;
		int ny = server->grab_view_ly + (int)dy;
		wlr_scene_node_set_position(&view->scene_tree->node, nx, ny);
		if (view->xwayland_surface != NULL) {
			int w = server->grab_view_width;
			int h = server->grab_view_height;
			if (w <= 0)
				w = view->xwayland_surface->width;
			if (h <= 0)
				h = view->xwayland_surface->height;
			if (w > 0 && h > 0)
				wlr_xwayland_surface_configure(view->xwayland_surface, nx, ny, (uint16_t)w, (uint16_t)h);
		}
		return;
	}

	if (server->cursor_mode == ASWL_CURSOR_RESIZE) {
		struct aswl_view *view = server->grabbed_view;
		if (view == NULL)
			return;

		double dx = server->cursor->x - server->grab_lx;
		double dy = server->cursor->y - server->grab_ly;

		int dx_i = (int)dx;
		int dy_i = (int)dy;

		int right_edge = server->grab_view_lx + server->grab_view_width;
		int bottom_edge = server->grab_view_ly + server->grab_view_height;

		int nx = server->grab_view_lx;
		int ny = server->grab_view_ly;
		int nw = server->grab_view_width;
		int nh = server->grab_view_height;

		if ((server->grab_edges & WLR_EDGE_LEFT) != 0) {
			nx = server->grab_view_lx + dx_i;
			nw = server->grab_view_width - dx_i;
		} else if ((server->grab_edges & WLR_EDGE_RIGHT) != 0) {
			nw = server->grab_view_width + dx_i;
		}

		if ((server->grab_edges & WLR_EDGE_TOP) != 0) {
			ny = server->grab_view_ly + dy_i;
			nh = server->grab_view_height - dy_i;
		} else if ((server->grab_edges & WLR_EDGE_BOTTOM) != 0) {
			nh = server->grab_view_height + dy_i;
		}

		int min_w = 1;
		int min_h = 1;
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
			min_w = view->xdg_surface->toplevel->current.min_width;
			min_h = view->xdg_surface->toplevel->current.min_height;
			if (min_w <= 0)
				min_w = 1;
			if (min_h <= 0)
				min_h = 1;
		}

		if (nw < min_w) {
			nw = min_w;
			if ((server->grab_edges & WLR_EDGE_LEFT) != 0)
				nx = right_edge - nw;
		}
		if (nh < min_h) {
			nh = min_h;
			if ((server->grab_edges & WLR_EDGE_TOP) != 0)
				ny = bottom_edge - nh;
		}

		if ((server->grab_edges & WLR_EDGE_LEFT) != 0 || (server->grab_edges & WLR_EDGE_TOP) != 0)
			wlr_scene_node_set_position(&view->scene_tree->node, nx, ny);
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
			(void)wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, nw, nh);
		} else if (view->xwayland_surface != NULL) {
			wlr_xwayland_surface_configure(view->xwayland_surface, nx, ny, (uint16_t)nw, (uint16_t)nh);
		}
		return;
	}

	double sx = 0;
	double sy = 0;
	struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	if (surface == NULL) {
		wlr_seat_pointer_notify_clear_focus(server->seat);
		return;
	}

	wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;

	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_button(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	uint32_t serial = wlr_seat_pointer_notify_button(server->seat,
	                                                event->time_msec,
	                                                event->button,
	                                                event->state);

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED && server->cursor_mode != ASWL_CURSOR_PASSTHROUGH) {
		if (server->grab_button == 0 || server->grab_button == event->button)
			end_interactive(server);
		return;
	}

	if (event->state != WL_POINTER_BUTTON_STATE_PRESSED || serial == 0)
		return;

	double sx = 0;
	double sy = 0;
	struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	struct aswl_view *view = surface != NULL ? view_from_wlr_surface(surface) : NULL;
	if (view != NULL) {
		if (view->scene_tree != NULL)
			wlr_scene_node_raise_to_top(&view->scene_tree->node);
		if (!view->is_dock)
			focus_view(view, surface);
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	uint32_t mods = keyboard != NULL ? wlr_keyboard_get_modifiers(keyboard) : 0;
	if ((mods & WLR_MODIFIER_ALT) == 0 || view == NULL || view->is_dock)
		return;

	if (event->button == BTN_LEFT) {
		begin_interactive(view, ASWL_CURSOR_MOVE, 0, event->button);
		return;
	}

	if (event->button == BTN_RIGHT) {
		int vx = 0;
		int vy = 0;
		(void)wlr_scene_node_coords(&view->scene_tree->node, &vx, &vy);

		int width = 0;
		int height = 0;
		view_get_current_size(view, &width, &height);

		uint32_t edges = 0;
		if (width > 0) {
			int local_x = (int)server->cursor->x - vx;
			edges |= local_x < width / 2 ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
		} else {
			edges |= WLR_EDGE_RIGHT;
		}

		if (height > 0) {
			int local_y = (int)server->cursor->y - vy;
			edges |= local_y < height / 2 ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
		} else {
			edges |= WLR_EDGE_BOTTOM;
		}

		begin_interactive(view, ASWL_CURSOR_RESIZE, edges, event->button);
		return;
	}
}

static void handle_cursor_axis(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	wlr_seat_pointer_notify_axis(server->seat,
	                            event->time_msec,
	                            event->orientation,
	                            event->delta,
	                            event->delta_discrete,
	                            event->source,
	                            event->relative_direction);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

static struct wlr_scene_tree *layer_tree_for(struct aswl_server *server, enum zwlr_layer_shell_v1_layer layer)
{
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		return server->layer_trees[layer];
	default:
		return server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP];
	}
}

static void handle_layer_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, destroy);

	wl_list_remove(&ls->destroy.link);
	wl_list_remove(&ls->map.link);
	wl_list_remove(&ls->unmap.link);
	wl_list_remove(&ls->commit.link);
	wl_list_remove(&ls->link);
	free(ls);
}

static void handle_layer_surface_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, map);
	arrange_layers(ls->server);
}

static void handle_layer_surface_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, unmap);
	arrange_layers(ls->server);
}

static void handle_layer_surface_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, commit);
	arrange_layers(ls->server);
}

static void handle_new_layer_surface(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *layer_surface = data;

	struct aswl_layer_surface *ls = calloc(1, sizeof(*ls));
	if (ls == NULL)
		return;

	ls->server = server;
	ls->layer_surface = layer_surface;
	ls->scene = wlr_scene_layer_surface_v1_create(layer_tree_for(server, layer_surface->pending.layer), layer_surface);
	if (ls->scene == NULL) {
		free(ls);
		return;
	}

	wl_list_insert(&server->layer_surfaces, &ls->link);

	ls->destroy.notify = handle_layer_surface_destroy;
	wl_signal_add(&layer_surface->events.destroy, &ls->destroy);

	ls->map.notify = handle_layer_surface_map;
	wl_signal_add(&layer_surface->surface->events.map, &ls->map);

	ls->unmap.notify = handle_layer_surface_unmap;
	wl_signal_add(&layer_surface->surface->events.unmap, &ls->unmap);

	ls->commit.notify = handle_layer_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit, &ls->commit);

	arrange_layers(server);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_output *output = wl_container_of(listener, output, destroy);
	struct aswl_server *server = output->server;

	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);

	arrange_layers(server);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *output = data;

	struct aswl_output *as_out = calloc(1, sizeof(*as_out));
	if (as_out == NULL)
		return;
	as_out->server = server;
	as_out->wlr_output = output;
	wl_list_insert(&server->outputs, &as_out->link);

	as_out->destroy.notify = handle_output_destroy;
	wl_signal_add(&output->events.destroy, &as_out->destroy);

	if (!wlr_output_init_render(output, server->allocator, server->renderer)) {
		fprintf(stderr, "aswlcomp: wlr_output_init_render failed\n");
		return;
	}

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
	if (mode != NULL)
		wlr_output_state_set_mode(&state, mode);

	if (!wlr_output_commit_state(output, &state)) {
		fprintf(stderr, "aswlcomp: failed to commit output\n");
		wlr_output_state_finish(&state);
		return;
	}
	wlr_output_state_finish(&state);

	wlr_output_create_global(output, server->display);

	wlr_output_layout_add_auto(server->output_layout, output);
	wlr_scene_output_create(server->scene, output);

	if (server->cursor_mgr != NULL)
		(void)wlr_xcursor_manager_load(server->cursor_mgr, output->scale);
	if (server->cursor != NULL && server->cursor_mgr != NULL)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");

	arrange_layers(server);
}

static void arrange_layers_for_output(struct aswl_output *out)
{
	if (out == NULL || out->server == NULL || out->wlr_output == NULL)
		return;

	struct aswl_server *server = out->server;
	struct wlr_output *output = out->wlr_output;

	struct wlr_box full = { 0 };
	wlr_output_layout_get_box(server->output_layout, output, &full);
	struct wlr_box usable = full;

	struct wlr_output *default_output = wlr_output_layout_get_center_output(server->output_layout);
	if (default_output == NULL)
		default_output = output;

	const enum zwlr_layer_shell_v1_layer order[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
		ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
	};

	for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		enum zwlr_layer_shell_v1_layer layer = order[i];

		struct aswl_layer_surface *ls;
		wl_list_for_each(ls, &server->layer_surfaces, link) {
			struct wlr_layer_surface_v1 *surf = ls->layer_surface;
			if (surf == NULL)
				continue;

			struct wlr_output *target = surf->output;
			if (target == NULL)
				target = default_output;
			if (target != output)
				continue;

			if (surf->current.layer != layer)
				continue;

			wlr_scene_layer_surface_v1_configure(ls->scene, &full, &usable);
		}
	}

	out->full_box = full;
	out->usable_box = usable;
}

static void arrange_layers(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link)
		arrange_layers_for_output(out);

	arrange_dock_views(server);
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

	server.display = wl_display_create();
	if (server.display == NULL) {
		fprintf(stderr, "aswlcomp: wl_display_create failed\n");
		return 1;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(server.display);
	server.backend = wlr_backend_autocreate(loop, NULL);
	if (server.backend == NULL) {
		fprintf(stderr, "aswlcomp: wlr_backend_autocreate failed\n");
		return 1;
	}

	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		fprintf(stderr, "aswlcomp: wlr_renderer_autocreate failed\n");
		return 1;
	}
	wlr_renderer_init_wl_display(server.renderer, server.display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (server.allocator == NULL) {
		fprintf(stderr, "aswlcomp: wlr_allocator_autocreate failed\n");
		return 1;
	}

	server.compositor = wlr_compositor_create(server.display, 6, server.renderer);
	if (server.compositor == NULL) {
		fprintf(stderr, "aswlcomp: wlr_compositor_create failed\n");
		return 1;
	}
	(void)wlr_subcompositor_create(server.display);
	(void)wlr_data_device_manager_create(server.display);

	server.control_global = wl_global_create(server.display, &afterstep_control_v1_interface, 4, &server, aswl_control_bind);
	if (server.control_global == NULL) {
		fprintf(stderr, "aswlcomp: wl_global_create(afterstep_control_v1) failed\n");
		return 1;
	}

	server.output_layout = wlr_output_layout_create(server.display);
	if (server.output_layout == NULL) {
		fprintf(stderr, "aswlcomp: wlr_output_layout_create failed\n");
		return 1;
	}

	server.scene = wlr_scene_create();
	if (server.scene == NULL) {
		fprintf(stderr, "aswlcomp: wlr_scene_create failed\n");
		return 1;
	}
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	server.seat = wlr_seat_create(server.display, "seat0");
	if (server.seat == NULL) {
		fprintf(stderr, "aswlcomp: wlr_seat_create failed\n");
		return 1;
	}

	server.xwayland = wlr_xwayland_create(server.display, server.compositor, true);
	if (server.xwayland != NULL) {
		wlr_xwayland_set_seat(server.xwayland, server.seat);
		if (server.xwayland->display_name != NULL) {
			setenv("DISPLAY", server.xwayland->display_name, 1);
			fprintf(stderr, "aswlcomp: Xwayland DISPLAY=%s\n", server.xwayland->display_name);
		}
	} else {
		fprintf(stderr, "aswlcomp: Xwayland disabled/unavailable\n");
	}

	server.cursor = wlr_cursor_create();
	if (server.cursor == NULL) {
		fprintf(stderr, "aswlcomp: wlr_cursor_create failed\n");
		return 1;
	}
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	server.cursor_motion.notify = handle_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);

	server.cursor_motion_absolute.notify = handle_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);

	server.cursor_button.notify = handle_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);

	server.cursor_axis.notify = handle_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);

	server.cursor_frame.notify = handle_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	server.request_cursor.notify = handle_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);

	server.layer_shell = wlr_layer_shell_v1_create(server.display, 4);
	if (server.layer_shell == NULL) {
		fprintf(stderr, "aswlcomp: wlr_layer_shell_v1_create failed\n");
		return 1;
	}

	server.xdg_shell = wlr_xdg_shell_create(server.display, 6);
	if (server.xdg_shell == NULL) {
		fprintf(stderr, "aswlcomp: wlr_xdg_shell_create failed\n");
		return 1;
	}

	wl_list_init(&server.outputs);
	wl_list_init(&server.views);
	wl_list_init(&server.keyboards);
	wl_list_init(&server.layer_surfaces);
	wl_list_init(&server.bindings);
	wl_list_init(&server.control_clients);

	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] = wlr_scene_tree_create(&server.scene->tree);
	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] = wlr_scene_tree_create(&server.scene->tree);
	server.xdg_tree = wlr_scene_tree_create(&server.scene->tree);
	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP] = wlr_scene_tree_create(&server.scene->tree);
	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] = wlr_scene_tree_create(&server.scene->tree);
	if (server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] == NULL ||
	    server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] == NULL ||
	    server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP] == NULL ||
	    server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] == NULL ||
	    server.xdg_tree == NULL) {
		fprintf(stderr, "aswlcomp: failed to create scene roots\n");
		return 1;
	}

	server.new_output.notify = handle_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.new_input.notify = handle_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);

	server.new_xdg_surface.notify = handle_new_xdg_surface;
	wl_signal_add(&server.xdg_shell->events.new_surface, &server.new_xdg_surface);

	if (server.xwayland != NULL) {
		server.new_xwayland_surface.notify = handle_new_xwayland_surface;
		wl_signal_add(&server.xwayland->events.new_surface, &server.new_xwayland_surface);
	}

	server.new_layer_surface.notify = handle_new_layer_surface;
	wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

	const char *socket = NULL;
	if (socket_name != NULL) {
		if (wl_display_add_socket(server.display, socket_name) != 0) {
			fprintf(stderr, "aswlcomp: wl_display_add_socket(%s) failed: %s\n",
			        socket_name,
			        strerror(errno));
			return 1;
		}
		socket = socket_name;
	} else {
		socket = wl_display_add_socket_auto(server.display);
	}
	if (socket == NULL) {
		fprintf(stderr, "aswlcomp: wl_display_add_socket_auto failed\n");
		return 1;
	}

	if (!wlr_backend_start(server.backend)) {
		fprintf(stderr, "aswlcomp: wlr_backend_start failed\n");
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, 1);
	fprintf(stderr, "aswlcomp: running on WAYLAND_DISPLAY=%s\n", socket);

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

	if (server.xwayland != NULL)
		wlr_xwayland_destroy(server.xwayland);
	wl_display_destroy(server.display);
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
