#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <xcb/xcb.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <wlr/xwayland.h>

#include "afterstep-control-v1-protocol.h"
#include "aswlcomp_internal.h"

#define ASWL_ACTIVATION_MAX_AGE_MSEC 10000u

static bool aswl_backend_list_contains_token(const char *list, const char *token)
{
	if (list == NULL || token == NULL)
		return false;

	const size_t token_len = strlen(token);
	const char *p = list;
	for (;;) {
		while (*p == ',' || *p == ' ' || *p == '\t')
			p++;
		if (*p == '\0')
			return false;

		const char *start = p;
		while (*p != '\0' && *p != ',' && *p != ' ' && *p != '\t')
			p++;
		const size_t len = (size_t)(p - start);
		if (len == token_len && strncmp(start, token, token_len) == 0)
			return true;
	}
}

static bool aswl_should_preintern_x11_atoms(void)
{
	const char *display = getenv("DISPLAY");
	if (display == NULL || display[0] == '\0')
		return false;

	const char *backends = getenv("WLR_BACKENDS");
	if (backends != NULL && backends[0] != '\0')
		return aswl_backend_list_contains_token(backends, "x11");

	/* No explicit backend list: follow wlroots' usual preference for Wayland. */
	const char *wayland_display = getenv("WAYLAND_DISPLAY");
	if (wayland_display != NULL && wayland_display[0] != '\0')
		return false;

	return true;
}

static void aswl_x11_preintern_atoms(struct aswl_server *server)
{
	if (!aswl_should_preintern_x11_atoms())
		return;

	if (server == NULL || server->x11_keepalive != NULL)
		return;

	server->x11_keepalive = xcb_connect(NULL, NULL);
	if (server->x11_keepalive == NULL || xcb_connection_has_error(server->x11_keepalive)) {
		if (server->x11_keepalive != NULL)
			xcb_disconnect(server->x11_keepalive);
		server->x11_keepalive = NULL;
		return;
	}

	/*
	 * wlroots' X11 backend currently uses xcb_intern_atom(..., only_if_exists=true)
	 * for a few atoms, which come back as NONE on a fresh Xvfb. If those NONE atoms
	 * are later used in xcb_change_property(), wlroots logs spurious "BadAtom"
	 * errors. Pre-intern them here so nested/headless runs stay clean.
	 */
	static const char *const atom_names[] = {
		"WM_PROTOCOLS",
		"WM_DELETE_WINDOW",
		"_NET_WM_NAME",
		"UTF8_STRING",
		"_VARIABLE_REFRESH",
	};

	for (size_t i = 0; i < sizeof(atom_names) / sizeof(atom_names[0]); i++) {
		const char *name = atom_names[i];
		xcb_intern_atom_cookie_t cookie = xcb_intern_atom(server->x11_keepalive, false, strlen(name), name);
		xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(server->x11_keepalive, cookie, NULL);
		free(reply);
	}
}

static uint64_t aswl_now_msec(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static bool aswl_user_event_is_recent(struct aswl_server *server, uint64_t now_msec)
{
	if (server == NULL)
		return false;
	if (server->last_user_serial == 0 || server->last_user_time_msec == 0)
		return false;
	if (now_msec < server->last_user_time_msec)
		return false;
	return (now_msec - server->last_user_time_msec) <= ASWL_ACTIVATION_MAX_AGE_MSEC;
}

static bool aswl_surface_is_focused_for_activation(struct aswl_server *server, struct wlr_surface *surface)
{
	if (server == NULL || surface == NULL || server->seat == NULL)
		return false;
	if (server->seat->pointer_state.focused_surface == surface)
		return true;
	if (server->seat->keyboard_state.focused_surface == surface)
		return true;

	struct aswl_view *v = view_from_wlr_surface(surface);
	if (v != NULL && v == server->focused_view)
		return true;

	return false;
}

static void handle_xdg_activation_new_token(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, xdg_activation_new_token);
	struct wlr_xdg_activation_token_v1 *token = data;
	if (server == NULL || token == NULL || server->seat == NULL)
		return;

	/* Keep compositor-generated tokens as-is. */
	if (token->data != NULL)
		return;

	bool ok = true;
	if (token->seat == NULL || token->seat != server->seat || token->serial == 0)
		ok = false;
	if (token->serial != server->last_user_serial)
		ok = false;
	if (token->surface == NULL || !aswl_surface_is_focused_for_activation(server, token->surface))
		ok = false;
	if (!aswl_user_event_is_recent(server, aswl_now_msec()))
		ok = false;

	token->data = ok ? (void *)1 : NULL;

	if (getenv("ASWLCOMP_DEBUG_ACTIVATION") != NULL) {
		fprintf(stderr,
		        "aswlcomp: xdg-activation: new_token serial=%u focused=%d recent=%d -> %s\n",
		        token->serial,
		        token->surface != NULL && aswl_surface_is_focused_for_activation(server, token->surface),
		        aswl_user_event_is_recent(server, aswl_now_msec()),
		        ok ? "ok" : "reject");
	}
}

static void handle_xdg_activation_request_activate(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, xdg_activation_request_activate);
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	if (server == NULL || event == NULL)
		return;

	if (server->session_locked)
		return;

	if (event->token == NULL || event->surface == NULL)
		return;
	if (event->token->data == NULL)
		return;

	struct aswl_view *view = view_from_wlr_surface(event->surface);
	if (view == NULL)
		return;

	if (view->scene_tree == NULL)
		return;

	if (!view->is_dock && view->workspace != server->current_workspace)
		set_workspace(server, view->workspace);

	if (getenv("ASWLCOMP_DEBUG_ACTIVATION") != NULL) {
		fprintf(stderr,
		        "aswlcomp: xdg-activation: activate app_id=%s title=%s\n",
		        view_app_id(view),
		        view_title(view));
	}

	focus_view(view, NULL);
	aswl_schedule_flush(server);
}

static struct wlr_xdg_activation_token_v1 *aswl_activation_token_for_spawn(struct aswl_server *server)
{
	if (server == NULL || server->xdg_activation == NULL || server->seat == NULL)
		return NULL;

	uint64_t now_msec = aswl_now_msec();
	if (!aswl_user_event_is_recent(server, now_msec))
		return NULL;

	struct wlr_surface *source = server->seat->pointer_state.focused_surface;
	if (source == NULL)
		source = server->seat->keyboard_state.focused_surface;

	struct wlr_xdg_activation_token_v1 *token = wlr_xdg_activation_token_v1_create(server->xdg_activation);
	if (token == NULL)
		return NULL;

	token->seat = server->seat;
	token->serial = server->last_user_serial;
	token->surface = source;
	token->data = (void *)1;

	return token;
}

void spawn_command(const char *command)
{
	if (command == NULL || command[0] == '\0')
		return;

	pid_t pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		unsetenv("XDG_ACTIVATION_TOKEN");
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}
}

void spawn_command_with_activation(struct aswl_server *server, const char *command)
{
	if (command == NULL || command[0] == '\0')
		return;

	pid_t pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		unsetenv("XDG_ACTIVATION_TOKEN");

		struct wlr_xdg_activation_token_v1 *token = aswl_activation_token_for_spawn(server);
		const char *token_name = token != NULL ? wlr_xdg_activation_token_v1_get_name(token) : NULL;
		if (token_name != NULL && token_name[0] != '\0')
			setenv("XDG_ACTIVATION_TOKEN", token_name, 1);

		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}
}

static int aswl_flush_timer_cb(void *data)
{
	struct aswl_server *server = data;
	if (server == NULL)
		return 0;

	if (getenv("ASWLCOMP_DEBUG_FLUSH") != NULL)
		fprintf(stderr, "aswlcomp: flush_timer_cb\n");

	if (server->flush_timer != NULL) {
		wl_event_source_remove(server->flush_timer);
		server->flush_timer = NULL;
	}
	if (server->display != NULL) {
		/*
		 * wl_display_flush_clients() can fail to make progress if one client is
		 * wedged, starving other clients (e.g., control protocol callers).
		 * Flush each client individually so one slow client can't block others.
		 */
		struct wl_list *clients = wl_display_get_client_list(server->display);
		if (clients != NULL) {
			for (struct wl_list *l = clients->next; l != clients; l = l->next) {
				struct wl_client *client = wl_client_from_link(l);
				if (client != NULL)
					wl_client_flush(client);
			}
		}
	}

	return 0;
}

void aswl_schedule_flush(struct aswl_server *server)
{
	if (server == NULL || server->display == NULL)
		return;
	if (server->flush_timer != NULL)
		return;

	struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
	if (loop == NULL)
		return;

	server->flush_timer = wl_event_loop_add_timer(loop, aswl_flush_timer_cb, server);
	if (server->flush_timer == NULL)
		return;

	if (getenv("ASWLCOMP_DEBUG_FLUSH") != NULL)
		fprintf(stderr, "aswlcomp: schedule_flush\n");

	/* 0ms delays can be treated as "disarm" by some libwayland builds; use 1ms. */
	(void)wl_event_source_timer_update(server->flush_timer, 1);
}

void arrange_layers(struct aswl_server *server)
{
	if (server == NULL || server->output_layout == NULL)
		return;

	aswl_layer_arrange(server);

	if (server->session_locked)
		arrange_lock_surfaces(server);

	arrange_dock_views(server);
}

int aswl_server_init(struct aswl_server *server, const char *socket_name)
{
	if (server == NULL)
		return 1;

	server->display = wl_display_create();
	if (server->display == NULL) {
		fprintf(stderr, "aswlcomp: wl_display_create failed\n");
		return 1;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(server->display);

	const char *lock_cmd_env = getenv("ASWLCOMP_LOCK_CMD");
	if (lock_cmd_env != NULL && lock_cmd_env[0] != '\0')
		server->lock_command = strdup(lock_cmd_env);
	else
		server->lock_command = strdup("aswllock");

	const char *idle_lock_env = getenv("ASWLCOMP_IDLE_LOCK_SECONDS");
	if (idle_lock_env != NULL && idle_lock_env[0] != '\0') {
		char *end = NULL;
		long v = strtol(idle_lock_env, &end, 10);
		if (end != idle_lock_env && end != NULL && *end == '\0' && v >= 0 && v <= 86400)
			server->idle_lock_seconds = (int)v;
		else
			fprintf(stderr, "aswlcomp: bad ASWLCOMP_IDLE_LOCK_SECONDS=%s\n", idle_lock_env);
	}
	if (server->idle_lock_seconds > 0 && loop != NULL) {
		server->idle_lock_timer = wl_event_loop_add_timer(loop, aswl_idle_lock_timer_cb, server);
		if (server->idle_lock_timer != NULL)
			(void)wl_event_source_timer_update(server->idle_lock_timer, server->idle_lock_seconds * 1000);
	}

	aswl_x11_preintern_atoms(server);

	server->backend = wlr_backend_autocreate(loop, NULL);
	if (server->backend == NULL) {
		fprintf(stderr, "aswlcomp: wlr_backend_autocreate failed\n");
		return 1;
	}

	server->renderer = wlr_renderer_autocreate(server->backend);
	if (server->renderer == NULL) {
		fprintf(stderr, "aswlcomp: wlr_renderer_autocreate failed\n");
		return 1;
	}
	wlr_renderer_init_wl_display(server->renderer, server->display);

	server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
	if (server->allocator == NULL) {
		fprintf(stderr, "aswlcomp: wlr_allocator_autocreate failed\n");
		return 1;
	}

	server->compositor = wlr_compositor_create(server->display, 6, server->renderer);
	if (server->compositor == NULL) {
		fprintf(stderr, "aswlcomp: wlr_compositor_create failed\n");
		return 1;
	}
	(void)wlr_subcompositor_create(server->display);
	(void)wlr_data_device_manager_create(server->display);
	server->primary_selection_manager = wlr_primary_selection_v1_device_manager_create(server->display);
	if (server->primary_selection_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_primary_selection_v1_device_manager_create failed\n");
	}

	/*
	 * wlroots logs an [ERROR] if we attempt to create linux-dmabuf with a
	 * renderer that can't provide a DRM FD (e.g. X11 backend + pixman). Gate the
	 * protocol on drm-fd availability to keep headless/nested runs clean.
	 */
	int renderer_drm_fd = wlr_renderer_get_drm_fd(server->renderer);
	if (renderer_drm_fd >= 0) {
		server->linux_dmabuf = wlr_linux_dmabuf_v1_create_with_renderer(server->display, 5, server->renderer);
		if (server->linux_dmabuf == NULL) {
			fprintf(stderr, "aswlcomp: wlr_linux_dmabuf_v1_create_with_renderer failed\n");
		}
		server->export_dmabuf_manager = wlr_export_dmabuf_manager_v1_create(server->display);
		if (server->export_dmabuf_manager == NULL) {
			fprintf(stderr, "aswlcomp: wlr_export_dmabuf_manager_v1_create failed\n");
		}
	} else {
		server->linux_dmabuf = NULL;
		server->export_dmabuf_manager = NULL;
	}

	server->screencopy_manager = wlr_screencopy_manager_v1_create(server->display);
	if (server->screencopy_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_screencopy_manager_v1_create failed\n");
	}

	server->ext_foreign_toplevel_list = wlr_ext_foreign_toplevel_list_v1_create(server->display, 1);
	if (server->ext_foreign_toplevel_list == NULL) {
		fprintf(stderr, "aswlcomp: wlr_ext_foreign_toplevel_list_v1_create failed\n");
	}

	server->foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(server->display);
	if (server->foreign_toplevel_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_foreign_toplevel_manager_v1_create failed\n");
	}

	server->session_lock_manager = wlr_session_lock_manager_v1_create(server->display);
	if (server->session_lock_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_session_lock_manager_v1_create failed\n");
	} else {
		server->new_session_lock.notify = handle_new_session_lock;
		wl_signal_add(&server->session_lock_manager->events.new_lock, &server->new_session_lock);
	}

	server->idle_notifier = wlr_idle_notifier_v1_create(server->display);
	if (server->idle_notifier == NULL) {
		fprintf(stderr, "aswlcomp: wlr_idle_notifier_v1_create failed\n");
	}

	server->idle_inhibit_manager = wlr_idle_inhibit_v1_create(server->display);
	if (server->idle_inhibit_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_idle_inhibit_v1_create failed\n");
	} else {
		server->new_idle_inhibitor.notify = handle_new_idle_inhibitor;
		wl_signal_add(&server->idle_inhibit_manager->events.new_inhibitor, &server->new_idle_inhibitor);
	}

	aswl_idle_inhibit_refresh(server);

	server->control_global = wl_global_create(server->display, &afterstep_control_v1_interface, 6, server, aswl_control_bind);
	if (server->control_global == NULL) {
		fprintf(stderr, "aswlcomp: wl_global_create(afterstep_control_v1) failed\n");
		return 1;
	}

	server->output_layout = wlr_output_layout_create(server->display);
	if (server->output_layout == NULL) {
		fprintf(stderr, "aswlcomp: wlr_output_layout_create failed\n");
		return 1;
	}

	server->xdg_output_manager = wlr_xdg_output_manager_v1_create(server->display, server->output_layout);
	if (server->xdg_output_manager == NULL)
		fprintf(stderr, "aswlcomp: wlr_xdg_output_manager_v1_create failed\n");

	server->output_manager = wlr_output_manager_v1_create(server->display);
	if (server->output_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_output_manager_v1_create failed\n");
	} else {
		server->output_manager_apply.notify = handle_output_manager_apply;
		wl_signal_add(&server->output_manager->events.apply, &server->output_manager_apply);

		server->output_manager_test.notify = handle_output_manager_test;
		wl_signal_add(&server->output_manager->events.test, &server->output_manager_test);
	}

	server->scene = wlr_scene_create();
	if (server->scene == NULL) {
		fprintf(stderr, "aswlcomp: wlr_scene_create failed\n");
		return 1;
	}
	server->scene_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);

	server->seat = wlr_seat_create(server->display, "seat0");
	if (server->seat == NULL) {
		fprintf(stderr, "aswlcomp: wlr_seat_create failed\n");
		return 1;
	}

	aswl_ime_init(server);

	server->virtual_keyboard_manager = wlr_virtual_keyboard_manager_v1_create(server->display);
	if (server->virtual_keyboard_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_virtual_keyboard_manager_v1_create failed\n");
	} else {
		server->new_virtual_keyboard.notify = handle_new_virtual_keyboard;
		wl_signal_add(&server->virtual_keyboard_manager->events.new_virtual_keyboard, &server->new_virtual_keyboard);
	}

	server->relative_pointer_manager = wlr_relative_pointer_manager_v1_create(server->display);
	if (server->relative_pointer_manager == NULL)
		fprintf(stderr, "aswlcomp: wlr_relative_pointer_manager_v1_create failed\n");

	server->pointer_constraints = wlr_pointer_constraints_v1_create(server->display);
	if (server->pointer_constraints == NULL) {
		fprintf(stderr, "aswlcomp: wlr_pointer_constraints_v1_create failed\n");
	} else {
		server->new_pointer_constraint.notify = handle_new_pointer_constraint;
		wl_signal_add(&server->pointer_constraints->events.new_constraint, &server->new_pointer_constraint);
	}

	server->xdg_activation = wlr_xdg_activation_v1_create(server->display);
	if (server->xdg_activation == NULL) {
		fprintf(stderr, "aswlcomp: wlr_xdg_activation_v1_create failed\n");
	} else {
		server->xdg_activation->token_timeout_msec = 30000;

		server->xdg_activation_new_token.notify = handle_xdg_activation_new_token;
		wl_signal_add(&server->xdg_activation->events.new_token, &server->xdg_activation_new_token);

		server->xdg_activation_request_activate.notify = handle_xdg_activation_request_activate;
		wl_signal_add(&server->xdg_activation->events.request_activate, &server->xdg_activation_request_activate);
	}

	server->xwayland = wlr_xwayland_create(server->display, server->compositor, true);
	if (server->xwayland != NULL) {
		wlr_xwayland_set_seat(server->xwayland, server->seat);
		if (server->xwayland->display_name != NULL) {
			setenv("DISPLAY", server->xwayland->display_name, 1);
			fprintf(stderr, "aswlcomp: Xwayland DISPLAY=%s\n", server->xwayland->display_name);
		}
	} else {
		fprintf(stderr, "aswlcomp: Xwayland disabled/unavailable\n");
	}

	server->cursor = wlr_cursor_create();
	if (server->cursor == NULL) {
		fprintf(stderr, "aswlcomp: wlr_cursor_create failed\n");
		return 1;
	}
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

	server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	server->cursor_motion.notify = handle_cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);

	server->cursor_motion_absolute.notify = handle_cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);

	server->cursor_button.notify = handle_cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);

	server->cursor_axis.notify = handle_cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);

	server->cursor_frame.notify = handle_cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

	server->request_cursor.notify = handle_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);

	server->request_set_selection.notify = handle_request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);

	server->request_set_primary_selection.notify = handle_request_set_primary_selection;
	wl_signal_add(&server->seat->events.request_set_primary_selection, &server->request_set_primary_selection);

	server->layer_shell = wlr_layer_shell_v1_create(server->display, 4);
	if (server->layer_shell == NULL) {
		fprintf(stderr, "aswlcomp: wlr_layer_shell_v1_create failed\n");
		return 1;
	}

	server->xdg_shell = wlr_xdg_shell_create(server->display, 6);
	if (server->xdg_shell == NULL) {
		fprintf(stderr, "aswlcomp: wlr_xdg_shell_create failed\n");
		return 1;
	}

	server->xdg_deco_mgr = wlr_xdg_decoration_manager_v1_create(server->display);
	if (server->xdg_deco_mgr == NULL) {
		fprintf(stderr, "aswlcomp: wlr_xdg_decoration_manager_v1_create failed (no xdg-decoration)\n");
	} else {
		server->new_xdg_decoration.notify = handle_new_xdg_decoration;
		wl_signal_add(&server->xdg_deco_mgr->events.new_toplevel_decoration, &server->new_xdg_decoration);
	}

	wl_list_init(&server->outputs);
	wl_list_init(&server->views);
	wl_list_init(&server->keyboards);
	wl_list_init(&server->pointer_devices);
	wl_list_init(&server->layer_surfaces);
	wl_list_init(&server->lock_surfaces);
	wl_list_init(&server->bindings);
	wl_list_init(&server->text_inputs);
	wl_list_init(&server->input_methods);
	wl_list_init(&server->input_popups);
	wl_list_init(&server->control_clients);

	server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] = wlr_scene_tree_create(&server->scene->tree);
	server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] = wlr_scene_tree_create(&server->scene->tree);
	server->xdg_tree = wlr_scene_tree_create(&server->scene->tree);
	server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP] = wlr_scene_tree_create(&server->scene->tree);
	server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] = wlr_scene_tree_create(&server->scene->tree);
	server->lock_tree = wlr_scene_tree_create(&server->scene->tree);
	if (server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] != NULL)
		server->ime_popup_tree = wlr_scene_tree_create(server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);
	if (server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] == NULL ||
	    server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] == NULL ||
	    server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP] == NULL ||
	    server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] == NULL ||
	    server->xdg_tree == NULL ||
	    server->lock_tree == NULL) {
		fprintf(stderr, "aswlcomp: failed to create scene roots\n");
		return 1;
	}
	if (server->ime_popup_tree == NULL)
		fprintf(stderr, "aswlcomp: failed to create IME popup tree\n");
	wlr_scene_node_set_enabled(&server->lock_tree->node, false);

	server->new_output.notify = handle_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);

	server->new_input.notify = handle_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);

	server->new_xdg_toplevel.notify = handle_new_xdg_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);

	if (server->xwayland != NULL) {
		server->new_xwayland_surface.notify = handle_new_xwayland_surface;
		wl_signal_add(&server->xwayland->events.new_surface, &server->new_xwayland_surface);
	}

	server->new_layer_surface.notify = handle_new_layer_surface;
	wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);

	const char *socket = NULL;
	if (socket_name != NULL) {
		if (wl_display_add_socket(server->display, socket_name) != 0) {
			fprintf(stderr, "aswlcomp: wl_display_add_socket(%s) failed: %s\n",
			        socket_name,
			        strerror(errno));
			return 1;
		}
		socket = socket_name;
	} else {
		socket = wl_display_add_socket_auto(server->display);
	}
	if (socket == NULL) {
		fprintf(stderr, "aswlcomp: wl_display_add_socket_auto failed\n");
		return 1;
	}

	if (!wlr_backend_start(server->backend)) {
		fprintf(stderr, "aswlcomp: wlr_backend_start failed\n");
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, 1);
	fprintf(stderr, "aswlcomp: running on WAYLAND_DISPLAY=%s\n", socket);
	return 0;
}

void aswl_server_finish(struct aswl_server *server)
{
	if (server == NULL)
		return;

	/* Ensure protocol-side handles are destroyed before wlroots globals go away. */
	struct aswl_view *view;
	struct aswl_view *view_tmp;
	wl_list_for_each_safe(view, view_tmp, &server->views, link) {
		view_toplevel_protocols_destroy(view);
	}

	if (server->idle_lock_timer != NULL) {
		wl_event_source_remove(server->idle_lock_timer);
		server->idle_lock_timer = NULL;
	}
	if (server->flush_timer != NULL) {
		wl_event_source_remove(server->flush_timer);
		server->flush_timer = NULL;
	}

	if (server->session_lock != NULL)
		session_lock_detach(server);
	lock_surfaces_destroy(server);
	if (server->session_lock_manager != NULL)
		wl_list_remove(&server->new_session_lock.link);
	if (server->idle_inhibit_manager != NULL)
		wl_list_remove(&server->new_idle_inhibitor.link);
	if (server->text_input_manager != NULL)
		wl_list_remove(&server->new_text_input.link);
	if (server->input_method_manager != NULL)
		wl_list_remove(&server->new_input_method.link);
	if (server->virtual_keyboard_manager != NULL)
		wl_list_remove(&server->new_virtual_keyboard.link);
	if (server->pointer_constraints != NULL)
		wl_list_remove(&server->new_pointer_constraint.link);
	if (server->xdg_activation != NULL) {
		wl_list_remove(&server->xdg_activation_new_token.link);
		wl_list_remove(&server->xdg_activation_request_activate.link);
	}
	idle_inhibitors_destroy(server);

	/* Detach listeners before wlroots globals are torn down. */
	if (server->cursor != NULL) {
		wl_list_remove(&server->cursor_motion.link);
		wl_list_remove(&server->cursor_motion_absolute.link);
		wl_list_remove(&server->cursor_button.link);
		wl_list_remove(&server->cursor_axis.link);
		wl_list_remove(&server->cursor_frame.link);
	}
	if (server->seat != NULL) {
		wl_list_remove(&server->request_cursor.link);
		wl_list_remove(&server->request_set_selection.link);
		wl_list_remove(&server->request_set_primary_selection.link);
	}
	wl_list_remove(&server->new_layer_surface.link);
	wl_list_remove(&server->new_xdg_toplevel.link);
	if (server->xdg_deco_mgr != NULL) {
		wl_list_remove(&server->new_xdg_decoration.link);
	}
	wl_list_remove(&server->new_input.link);
	wl_list_remove(&server->new_output.link);
	if (server->output_manager != NULL) {
		wl_list_remove(&server->output_manager_apply.link);
		wl_list_remove(&server->output_manager_test.link);
	}

	if (server->xwayland != NULL) {
		wl_list_remove(&server->new_xwayland_surface.link);
		wlr_xwayland_destroy(server->xwayland);
		server->xwayland = NULL;
	}

	/*
	 * During wl_display_destroy(), outputs and layer surfaces may emit destroy
	 * signals. Guard arrange_layers()/arrange_dock_views() against touching
	 * wlroots objects that may already be torn down.
	 */
	server->output_layout = NULL;

	aswl_outputs_persist_destroy(server);
	aswl_deco_assets_destroy(&server->deco);
	aswl_font_destroy(&server->deco_font_inactive);
	aswl_font_destroy(&server->deco_font);
	if (server->dock.rules != NULL) {
		for (size_t i = 0; i < server->dock.rule_count; i++)
			free(server->dock.rules[i]);
		free(server->dock.rules);
		server->dock.rules = NULL;
		server->dock.rule_count = 0;
	}
	free(server->dock.rules_path);
	server->dock.rules_path = NULL;
	aswl_theme_destroy(&server->theme);
	free(server->xkb_rules);
	free(server->xkb_model);
	free(server->xkb_layout);
	free(server->xkb_variant);
	free(server->xkb_options);
	free(server->lock_command);
	free(server->state_path);
	if (server->x11_keepalive != NULL) {
		xcb_disconnect(server->x11_keepalive);
		server->x11_keepalive = NULL;
	}
	wl_display_destroy(server->display);
}
