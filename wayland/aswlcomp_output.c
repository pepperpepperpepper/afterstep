#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <wlr/backend.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include "aswlcomp_internal.h"

static void handle_output_frame(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_output *out = wl_container_of(listener, out, frame);
	if (out == NULL || out->wlr_output == NULL || out->scene_output == NULL)
		return;

	if (!wlr_scene_output_needs_frame(out->scene_output))
		return;

	if (!wlr_scene_output_commit(out->scene_output, NULL))
		return;

	struct aswl_server *server = out->server;
	if (server != NULL && server->session_locked && server->session_lock != NULL && !server->session_lock_sent_locked) {
		out->lock_frame_presented = true;

		bool all = true;
		bool any = false;
		struct aswl_output *o;
		wl_list_for_each(o, &server->outputs, link) {
			if (o == NULL || o->wlr_output == NULL)
				continue;
			if (!o->wlr_output->enabled)
				continue;
			any = true;
			if (!o->lock_frame_presented) {
				all = false;
				break;
			}
		}

		if (!any || all) {
			wlr_session_lock_v1_send_locked(server->session_lock);
			server->session_lock_sent_locked = true;
		}
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(out->scene_output, &now);
}

static void output_manager_update_current_config(struct aswl_server *server)
{
	if (server == NULL || server->output_manager == NULL || server->output_layout == NULL)
		return;

	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
	if (config == NULL)
		return;

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out == NULL || out->wlr_output == NULL)
			continue;

		struct wlr_output_configuration_head_v1 *head = wlr_output_configuration_head_v1_create(config, out->wlr_output);
		if (head == NULL)
			continue;

		struct wlr_output_layout_output *lo = wlr_output_layout_get(server->output_layout, out->wlr_output);
		if (lo != NULL) {
			head->state.x = lo->x;
			head->state.y = lo->y;
		}
	}

	wlr_output_manager_v1_set_configuration(server->output_manager, config);
}

static bool output_manager_apply_or_test(struct aswl_server *server, struct wlr_output_configuration_v1 *config, bool test_only)
{
	if (server == NULL || config == NULL)
		return false;

	size_t states_len = 0;
	struct wlr_backend_output_state *states = wlr_output_configuration_v1_build_state(config, &states_len);
	if (states == NULL)
		return false;

	bool ok = true;
	if (server->backend != NULL)
		ok = wlr_backend_test(server->backend, states, states_len);

	if (ok && !test_only && server->backend != NULL)
		ok = wlr_backend_commit(server->backend, states, states_len);

	free(states);

	if (!ok)
		return false;

	if (test_only)
		return true;

	if (server->output_layout != NULL) {
		struct wlr_output_configuration_head_v1 *head;
		wl_list_for_each(head, &config->heads, link) {
			if (head == NULL || head->state.output == NULL)
				continue;

			if (head->state.enabled) {
				wlr_output_layout_add(server->output_layout, head->state.output, head->state.x, head->state.y);
			} else {
				wlr_output_layout_remove(server->output_layout, head->state.output);
			}
		}
	}

	if (server->cursor_mgr != NULL) {
		struct aswl_output *out;
		wl_list_for_each(out, &server->outputs, link) {
			if (out != NULL && out->wlr_output != NULL)
				(void)wlr_xcursor_manager_load(server->cursor_mgr, out->wlr_output->scale);
		}
	}
	if (server->cursor != NULL && server->cursor_mgr != NULL)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");

	arrange_layers(server);
	broadcast_output_state(server);
	output_manager_update_current_config(server);

	aswl_state_save(server);
	return true;
}

void handle_output_manager_apply(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, output_manager_apply);
	struct wlr_output_configuration_v1 *config = data;

	bool ok = output_manager_apply_or_test(server, config, false);
	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);

	wlr_output_configuration_v1_destroy(config);
}

void handle_output_manager_test(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, output_manager_test);
	struct wlr_output_configuration_v1 *config = data;

	bool ok = output_manager_apply_or_test(server, config, true);
	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);

	wlr_output_configuration_v1_destroy(config);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_output *output = wl_container_of(listener, output, destroy);
	struct aswl_server *server = output->server;

	if (server != NULL && server->output_layout != NULL && output->wlr_output != NULL)
		wlr_output_layout_remove(server->output_layout, output->wlr_output);

	if (output->lock_rect != NULL)
		wlr_scene_node_destroy(&output->lock_rect->node);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);

	arrange_layers(server);
	broadcast_output_state(server);
	output_manager_update_current_config(server);

	if (server != NULL && server->output_layout != NULL)
		aswl_state_save(server);
}

void handle_new_output(struct wl_listener *listener, void *data)
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

	int persist_x = 0;
	int persist_y = 0;
	bool persist_have_pos = false;

	int want_w = 0;
	int want_h = 0;
	const char *ow_env = getenv("ASWLCOMP_OUTPUT_WIDTH");
	const char *oh_env = getenv("ASWLCOMP_OUTPUT_HEIGHT");
	if (ow_env != NULL && ow_env[0] != '\0' && oh_env != NULL && oh_env[0] != '\0') {
		char *end_w = NULL;
		char *end_h = NULL;
		long w = strtol(ow_env, &end_w, 10);
		long h = strtol(oh_env, &end_h, 10);
		if (end_w != ow_env && end_w != NULL && *end_w == '\0' && w > 0 && w <= 16384)
			want_w = (int)w;
		if (end_h != oh_env && end_h != NULL && *end_h == '\0' && h > 0 && h <= 16384)
			want_h = (int)h;
	}

	bool applied_persist = false;
	if (want_w > 0 && want_h > 0) {
		wlr_output_state_set_custom_mode(&state, want_w, want_h, 0);
	} else {
		applied_persist = aswl_output_persist_apply(server, output, &state, &persist_x, &persist_y, &persist_have_pos);
		if (!applied_persist) {
			struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
			if (mode != NULL)
				wlr_output_state_set_mode(&state, mode);
		}
	}

	if (!wlr_output_commit_state(output, &state)) {
		fprintf(stderr, "aswlcomp: failed to commit output (persisted config?)\n");
		wlr_output_state_finish(&state);

		struct wlr_output_state fallback;
		wlr_output_state_init(&fallback);
		wlr_output_state_set_enabled(&fallback, true);
		struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
		if (mode != NULL)
			wlr_output_state_set_mode(&fallback, mode);
		if (!wlr_output_commit_state(output, &fallback)) {
			fprintf(stderr, "aswlcomp: failed to commit output fallback\n");
			wlr_output_state_finish(&fallback);
			return;
		}
		wlr_output_state_finish(&fallback);
	}
	wlr_output_state_finish(&state);

	wlr_output_create_global(output, server->display);

	if (output->enabled) {
		if (persist_have_pos && applied_persist) {
			wlr_output_layout_add(server->output_layout, output, persist_x, persist_y);
		} else {
			wlr_output_layout_add_auto(server->output_layout, output);
		}
	}
	as_out->scene_output = wlr_scene_output_create(server->scene, output);

	float lock_color[4];
	aswl_argb_to_premul_f(server->theme.desk_bg | 0xFF000000u, lock_color);
	as_out->lock_rect = wlr_scene_rect_create(server->lock_tree, 1, 1, lock_color);
	if (as_out->lock_rect != NULL)
		wlr_scene_node_set_enabled(&as_out->lock_rect->node, server->session_locked);
	as_out->lock_frame_presented = false;

	as_out->frame.notify = handle_output_frame;
	wl_signal_add(&output->events.frame, &as_out->frame);

	if (server->cursor_mgr != NULL)
		(void)wlr_xcursor_manager_load(server->cursor_mgr, output->scale);
	if (server->cursor != NULL && server->cursor_mgr != NULL)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");

	wlr_output_schedule_frame(output);

	arrange_layers(server);
	broadcast_output_state(server);
	output_manager_update_current_config(server);

	aswl_state_save(server);
}

