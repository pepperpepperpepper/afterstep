#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>

#include "aswlcomp_internal.h"

static struct wl_client *aswl_client_from_surface(struct wlr_surface *surface)
{
	if (surface == NULL || surface->resource == NULL)
		return NULL;
	return wl_resource_get_client(surface->resource);
}

static struct wl_client *aswl_client_from_text_input(struct wlr_text_input_v3 *text_input)
{
	if (text_input == NULL || text_input->resource == NULL)
		return NULL;
	return wl_resource_get_client(text_input->resource);
}

static struct wlr_text_input_v3 *aswl_ime_find_enabled_text_input_for_focused_client(struct aswl_server *server)
{
	if (server == NULL)
		return NULL;

	struct wl_client *focused_client = aswl_client_from_surface(server->ime_focused_surface);
	if (focused_client == NULL)
		return NULL;

	struct aswl_text_input *ti;
	wl_list_for_each(ti, &server->text_inputs, link) {
		if (ti == NULL || ti->text_input == NULL)
			continue;
		if (!ti->enabled)
			continue;
		if (aswl_client_from_text_input(ti->text_input) != focused_client)
			continue;
		return ti->text_input;
	}

	return NULL;
}

static void aswl_ime_update_popups(struct aswl_server *server)
{
	if (server == NULL)
		return;

	bool want_popups = !server->session_locked &&
	                   server->ime_active_text_input != NULL &&
	                   server->ime_focused_surface != NULL &&
	                   server->focused_view != NULL &&
	                   server->ime_popup_tree != NULL &&
	                   server->ime_cursor_rect_valid;

	if (!want_popups) {
		struct aswl_input_popup *popup;
		wl_list_for_each(popup, &server->input_popups, link) {
			if (popup != NULL && popup->scene_tree != NULL)
				wlr_scene_node_set_enabled(&popup->scene_tree->node, false);
		}
		return;
	}

	struct aswl_view *view = server->focused_view;
	if (view == NULL || view->scene_tree == NULL) {
		struct aswl_input_popup *popup;
		wl_list_for_each(popup, &server->input_popups, link) {
			if (popup != NULL && popup->scene_tree != NULL)
				wlr_scene_node_set_enabled(&popup->scene_tree->node, false);
		}
		return;
	}

	int vx = 0;
	int vy = 0;
	(void)wlr_scene_node_coords(&view->scene_tree->node, &vx, &vy);

	int ox = 0;
	int oy = 0;
	view_get_content_offset(view, &ox, &oy);

	int base_x = vx + ox;
	int base_y = vy + oy;

	int caret_x = base_x + server->ime_cursor_rect.x;
	int caret_y = base_y + server->ime_cursor_rect.y + server->ime_cursor_rect.height;
	int caret_top_y = base_y + server->ime_cursor_rect.y;

	struct wlr_output *output = NULL;
	if (server->output_layout != NULL)
		output = wlr_output_layout_output_at(server->output_layout, caret_x, caret_y);

	struct wlr_box obox = { 0 };
	if (server->output_layout != NULL)
		wlr_output_layout_get_box(server->output_layout, output, &obox);

	struct aswl_input_popup *popup;
	wl_list_for_each(popup, &server->input_popups, link) {
		if (popup == NULL || popup->popup_surface == NULL || popup->popup_surface->surface == NULL)
			continue;
		if (popup->scene_tree == NULL)
			continue;

		wlr_scene_node_set_enabled(&popup->scene_tree->node, true);
		wlr_scene_node_raise_to_top(&popup->scene_tree->node);

		struct wlr_surface *surface = popup->popup_surface->surface;
		int pw = surface->current.width;
		int ph = surface->current.height;

		int px = caret_x;
		int py = caret_y;

		/* If it doesn't fit below the caret, try above. */
		if (ph > 0 && obox.height > 0) {
			int bottom = obox.y + obox.height;
			if (py + ph > bottom)
				py = caret_top_y - ph;
		}

		int max_x = obox.x;
		if (obox.width > 0 && pw > 0) {
			max_x = obox.x + obox.width - pw;
			if (max_x < obox.x)
				max_x = obox.x;
		}

		int max_y = obox.y;
		if (obox.height > 0 && ph > 0) {
			max_y = obox.y + obox.height - ph;
			if (max_y < obox.y)
				max_y = obox.y;
		}

		px = clamp_int(px, obox.x, max_x);
		py = clamp_int(py, obox.y, max_y);

		wlr_scene_node_set_position(&popup->scene_tree->node, px, py);
		wlr_input_popup_surface_v2_send_text_input_rectangle(popup->popup_surface, &server->ime_cursor_rect);
	}
}

static void aswl_ime_send_text_input_state(struct aswl_server *server, struct wlr_text_input_v3 *text_input)
{
	if (server == NULL || text_input == NULL)
		return;
	if (server->session_locked)
		return;

	struct wlr_input_method_v2 *input_method = server->ime_input_method;
	if (input_method == NULL)
		return;

	const struct wlr_text_input_v3_state *state = &text_input->current;
	uint32_t features = text_input->active_features;

	if ((features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) != 0) {
		const char *text = state->surrounding.text != NULL ? state->surrounding.text : "";
		wlr_input_method_v2_send_surrounding_text(input_method, text, state->surrounding.cursor, state->surrounding.anchor);
	}

	if ((features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) != 0) {
		wlr_input_method_v2_send_content_type(input_method, state->content_type.hint, state->content_type.purpose);
	}

	if ((features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE) != 0) {
		server->ime_cursor_rect = state->cursor_rectangle;
		server->ime_cursor_rect_valid = true;
	} else {
		server->ime_cursor_rect_valid = false;
	}

	wlr_input_method_v2_send_text_change_cause(input_method, state->text_change_cause);
	wlr_input_method_v2_send_done(input_method);
	aswl_ime_update_popups(server);
}

static void aswl_ime_update(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct wlr_text_input_v3 *new_active = NULL;
	if (!server->session_locked)
		new_active = aswl_ime_find_enabled_text_input_for_focused_client(server);

	struct wlr_text_input_v3 *old_active = server->ime_active_text_input;
	if (new_active != old_active) {
		if (old_active != NULL) {
			wlr_text_input_v3_send_preedit_string(old_active, "", -1, -1);
			wlr_text_input_v3_send_done(old_active);
		}
		server->ime_active_text_input = new_active;
	}

	struct wlr_input_method_v2 *input_method = server->ime_input_method;
	if (input_method != NULL) {
		bool want_active = (new_active != NULL);
		if (want_active && !input_method->active) {
			wlr_input_method_v2_send_activate(input_method);
		} else if (!want_active && input_method->active) {
			wlr_input_method_v2_send_deactivate(input_method);
		}

		if (want_active)
			aswl_ime_send_text_input_state(server, new_active);
	}

	aswl_ime_update_popups(server);
}

void aswl_ime_set_focus(struct aswl_server *server, struct wlr_surface *surface)
{
	if (server == NULL)
		return;

	if (server->session_locked)
		surface = NULL;

	server->ime_focused_surface = surface;

	struct wl_client *focused_client = aswl_client_from_surface(surface);

	struct aswl_text_input *ti;
	wl_list_for_each(ti, &server->text_inputs, link) {
		if (ti == NULL || ti->text_input == NULL)
			continue;

		struct wl_client *ti_client = aswl_client_from_text_input(ti->text_input);
		if (focused_client != NULL && ti_client == focused_client) {
			if (ti->text_input->focused_surface != surface)
				wlr_text_input_v3_send_enter(ti->text_input, surface);
		} else {
			if (ti->text_input->focused_surface != NULL)
				wlr_text_input_v3_send_leave(ti->text_input);
		}
	}

	aswl_ime_update(server);
}

void aswl_ime_maybe_set_keyboard_grab(struct aswl_server *server, struct wlr_keyboard *keyboard)
{
	if (server == NULL || keyboard == NULL)
		return;
	if (server->session_locked)
		return;

	struct wlr_input_method_v2 *input_method = server->ime_input_method;
	if (input_method == NULL || input_method->keyboard_grab == NULL)
		return;
	if (input_method->keyboard_grab->keyboard == keyboard)
		return;

	wlr_input_method_keyboard_grab_v2_set_keyboard(input_method->keyboard_grab, keyboard);
}

void aswl_ime_notify_key(struct aswl_server *server, struct wlr_keyboard_key_event *event)
{
	if (server == NULL || event == NULL)
		return;
	if (server->session_locked)
		return;
	if (server->ime_active_text_input == NULL)
		return;

	struct wlr_input_method_v2 *input_method = server->ime_input_method;
	if (input_method == NULL || input_method->keyboard_grab == NULL)
		return;

	wlr_input_method_keyboard_grab_v2_send_key(input_method->keyboard_grab, event->time_msec, event->keycode, event->state);
}

void aswl_ime_notify_modifiers(struct aswl_server *server, struct wlr_keyboard *keyboard)
{
	if (server == NULL || keyboard == NULL)
		return;
	if (server->session_locked)
		return;
	if (server->ime_active_text_input == NULL)
		return;

	struct wlr_input_method_v2 *input_method = server->ime_input_method;
	if (input_method == NULL || input_method->keyboard_grab == NULL)
		return;

	wlr_input_method_keyboard_grab_v2_send_modifiers(input_method->keyboard_grab, &keyboard->modifiers);
}

static void handle_text_input_enable(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_text_input *ti = wl_container_of(listener, ti, enable);
	if (ti == NULL || ti->server == NULL)
		return;

	ti->enabled = true;
	aswl_ime_update(ti->server);
}

static void handle_text_input_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_text_input *ti = wl_container_of(listener, ti, commit);
	if (ti == NULL || ti->server == NULL)
		return;

	aswl_ime_update(ti->server);
	if (ti->server->ime_active_text_input == ti->text_input)
		aswl_ime_send_text_input_state(ti->server, ti->text_input);
}

static void handle_text_input_disable(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_text_input *ti = wl_container_of(listener, ti, disable);
	if (ti == NULL || ti->server == NULL)
		return;

	ti->enabled = false;
	aswl_ime_update(ti->server);
}

static void handle_text_input_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_text_input *ti = wl_container_of(listener, ti, destroy);
	if (ti == NULL)
		return;

	struct aswl_server *server = ti->server;

	wl_list_remove(&ti->enable.link);
	wl_list_remove(&ti->commit.link);
	wl_list_remove(&ti->disable.link);
	wl_list_remove(&ti->destroy.link);
	wl_list_remove(&ti->link);

	if (server != NULL && server->ime_active_text_input == ti->text_input)
		server->ime_active_text_input = NULL;

	free(ti);
	aswl_ime_update(server);
}

static void handle_new_text_input(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_text_input);
	struct wlr_text_input_v3 *text_input = data;
	if (server == NULL || text_input == NULL)
		return;

	struct aswl_text_input *ti = calloc(1, sizeof(*ti));
	if (ti == NULL)
		return;

	ti->server = server;
	ti->text_input = text_input;

	wl_list_init(&ti->enable.link);
	wl_list_init(&ti->commit.link);
	wl_list_init(&ti->disable.link);
	wl_list_init(&ti->destroy.link);

	ti->enable.notify = handle_text_input_enable;
	wl_signal_add(&text_input->events.enable, &ti->enable);

	ti->commit.notify = handle_text_input_commit;
	wl_signal_add(&text_input->events.commit, &ti->commit);

	ti->disable.notify = handle_text_input_disable;
	wl_signal_add(&text_input->events.disable, &ti->disable);

	ti->destroy.notify = handle_text_input_destroy;
	wl_signal_add(&text_input->events.destroy, &ti->destroy);

	wl_list_insert(&server->text_inputs, &ti->link);

	struct wl_client *focused_client = aswl_client_from_surface(server->ime_focused_surface);
	if (focused_client != NULL && aswl_client_from_text_input(text_input) == focused_client)
		wlr_text_input_v3_send_enter(text_input, server->ime_focused_surface);

	aswl_ime_update(server);
}

static void handle_input_popup_surface_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_input_popup *popup = wl_container_of(listener, popup, surface_commit);
	if (popup == NULL || popup->server == NULL)
		return;

	aswl_ime_update_popups(popup->server);
}

static void handle_input_popup_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_input_popup *popup = wl_container_of(listener, popup, destroy);
	if (popup == NULL)
		return;

	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->surface_commit.link);
	wl_list_remove(&popup->link);

	if (popup->scene_tree != NULL)
		wlr_scene_node_destroy(&popup->scene_tree->node);

	free(popup);
}

static void handle_input_method_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_input_method *im = wl_container_of(listener, im, commit);
	if (im == NULL || im->server == NULL || im->input_method == NULL)
		return;

	struct aswl_server *server = im->server;
	if (server->session_locked)
		return;

	struct wlr_text_input_v3 *text_input = server->ime_active_text_input;
	if (text_input == NULL)
		return;

	const char *preedit = im->input_method->current.preedit.text != NULL ? im->input_method->current.preedit.text : "";
	int32_t cursor_begin = im->input_method->current.preedit.text != NULL ? im->input_method->current.preedit.cursor_begin : -1;
	int32_t cursor_end = im->input_method->current.preedit.text != NULL ? im->input_method->current.preedit.cursor_end : -1;
	wlr_text_input_v3_send_preedit_string(text_input, preedit, cursor_begin, cursor_end);

	if (im->input_method->current.commit_text != NULL && im->input_method->current.commit_text[0] != '\0')
		wlr_text_input_v3_send_commit_string(text_input, im->input_method->current.commit_text);

	if (im->input_method->current.delete.before_length > 0 || im->input_method->current.delete.after_length > 0)
		wlr_text_input_v3_send_delete_surrounding_text(text_input,
		                                               im->input_method->current.delete.before_length,
		                                               im->input_method->current.delete.after_length);

	wlr_text_input_v3_send_done(text_input);
}

static void handle_input_method_new_popup_surface(struct wl_listener *listener, void *data)
{
	struct aswl_input_method *im = wl_container_of(listener, im, new_popup_surface);
	struct wlr_input_popup_surface_v2 *popup_surface = data;
	if (im == NULL || im->server == NULL || popup_surface == NULL)
		return;

	struct aswl_server *server = im->server;

	struct aswl_input_popup *popup = calloc(1, sizeof(*popup));
	if (popup == NULL)
		return;

	popup->server = server;
	popup->popup_surface = popup_surface;
	popup_surface->data = popup;

	wl_list_init(&popup->destroy.link);
	wl_list_init(&popup->surface_commit.link);

	popup->destroy.notify = handle_input_popup_destroy;
	wl_signal_add(&popup_surface->events.destroy, &popup->destroy);

	if (popup_surface->surface != NULL) {
		popup->surface_commit.notify = handle_input_popup_surface_commit;
		wl_signal_add(&popup_surface->surface->events.commit, &popup->surface_commit);
	}

	wl_list_insert(&server->input_popups, &popup->link);

	if (server->ime_popup_tree != NULL && popup_surface->surface != NULL) {
		popup->scene_tree = wlr_scene_subsurface_tree_create(server->ime_popup_tree, popup_surface->surface);
		if (popup->scene_tree != NULL)
			wlr_scene_node_raise_to_top(&popup->scene_tree->node);
	}

	aswl_ime_update_popups(server);
}

static void handle_input_method_grab_keyboard(struct wl_listener *listener, void *data)
{
	struct aswl_input_method *im = wl_container_of(listener, im, grab_keyboard);
	struct wlr_input_method_keyboard_grab_v2 *grab = data;
	if (im == NULL || im->server == NULL || im->server->seat == NULL || grab == NULL)
		return;

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(im->server->seat);
	if (keyboard != NULL)
		wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard);
}

static void handle_input_method_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_input_method *im = wl_container_of(listener, im, destroy);
	if (im == NULL)
		return;

	struct aswl_server *server = im->server;

	wl_list_remove(&im->commit.link);
	wl_list_remove(&im->new_popup_surface.link);
	wl_list_remove(&im->grab_keyboard.link);
	wl_list_remove(&im->destroy.link);
	wl_list_remove(&im->link);

	if (server != NULL && server->ime_input_method == im->input_method) {
		server->ime_input_method = NULL;
		struct aswl_input_method *it;
		wl_list_for_each(it, &server->input_methods, link) {
			if (it != NULL && it->input_method != NULL)
				server->ime_input_method = it->input_method;
		}
	}

	free(im);
	aswl_ime_update(server);
}

static void handle_new_input_method(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_input_method);
	struct wlr_input_method_v2 *input_method = data;
	if (server == NULL || input_method == NULL)
		return;

	struct aswl_input_method *im = calloc(1, sizeof(*im));
	if (im == NULL)
		return;

	im->server = server;
	im->input_method = input_method;

	im->commit.notify = handle_input_method_commit;
	wl_signal_add(&input_method->events.commit, &im->commit);

	im->new_popup_surface.notify = handle_input_method_new_popup_surface;
	wl_signal_add(&input_method->events.new_popup_surface, &im->new_popup_surface);

	im->grab_keyboard.notify = handle_input_method_grab_keyboard;
	wl_signal_add(&input_method->events.grab_keyboard, &im->grab_keyboard);

	im->destroy.notify = handle_input_method_destroy;
	wl_signal_add(&input_method->events.destroy, &im->destroy);

	wl_list_insert(&server->input_methods, &im->link);

	/* Prefer the newest input-method client for the seat. */
	server->ime_input_method = input_method;
	aswl_ime_update(server);
}

void aswl_ime_init(struct aswl_server *server)
{
	if (server == NULL || server->display == NULL)
		return;

	server->text_input_manager = wlr_text_input_manager_v3_create(server->display);
	if (server->text_input_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_text_input_manager_v3_create failed\n");
	} else {
		server->new_text_input.notify = handle_new_text_input;
		wl_signal_add(&server->text_input_manager->events.text_input, &server->new_text_input);
	}

	server->input_method_manager = wlr_input_method_manager_v2_create(server->display);
	if (server->input_method_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_input_method_manager_v2_create failed\n");
	} else {
		server->new_input_method.notify = handle_new_input_method;
		wl_signal_add(&server->input_method_manager->events.input_method, &server->new_input_method);
	}
}
