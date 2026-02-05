#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "wlr-output-management-unstable-v1-client-protocol.h"

struct aswlrandr_state;

struct aswlrandr_mode {
	struct wl_list link; /* aswlrandr_head.modes */
	struct aswlrandr_state *state;
	struct zwlr_output_mode_v1 *mode;
	int32_t width;
	int32_t height;
	int32_t refresh_mhz;
	bool have_size;
	bool have_refresh;
	bool preferred;
};

struct aswlrandr_head {
	struct wl_list link; /* aswlrandr_state.heads */
	struct aswlrandr_state *state;
	struct zwlr_output_head_v1 *head;
	char *name;
	char *description;
	char *make;
	char *model;
	char *serial_number;

	bool have_enabled;
	bool enabled;

	bool have_pos;
	int32_t x;
	int32_t y;

	bool have_transform;
	enum wl_output_transform transform;

	bool have_scale;
	double scale;

	struct zwlr_output_mode_v1 *current_mode;
	struct wl_list modes;
};

struct aswlrandr_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct zwlr_output_manager_v1 *manager;
	uint32_t manager_version;
	bool got_manager;
	bool got_done;
	uint32_t serial;
	struct wl_list heads;
};

static void head_destroy(struct aswlrandr_head *head);
static void mode_destroy(struct aswlrandr_mode *mode);

static void usage(const char *prog)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s list\n", prog);
	fprintf(stderr, "  %s move OUTPUT X Y\n", prog);
	fprintf(stderr, "  %s enable OUTPUT\n", prog);
	fprintf(stderr, "  %s disable OUTPUT\n", prog);
	fprintf(stderr, "\n");
	fprintf(stderr, "Notes:\n");
	fprintf(stderr, "  - Connects to $WAYLAND_DISPLAY.\n");
	fprintf(stderr, "  - Uses wlr-output-management-unstable-v1 (zwlr_output_manager_v1).\n");
}

static bool parse_i32(const char *s, int32_t min, int32_t max, int32_t *out)
{
	if (out != NULL)
		*out = 0;
	if (s == NULL || s[0] == '\0')
		return false;

	char *end = NULL;
	errno = 0;
	long v = strtol(s, &end, 10);
	if (errno != 0 || end == s || end == NULL || *end != '\0')
		return false;
	if (v < min || v > max)
		return false;
	if (out != NULL)
		*out = (int32_t)v;
	return true;
}

static const char *transform_to_string(enum wl_output_transform t)
{
	switch (t) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		return "normal";
	case WL_OUTPUT_TRANSFORM_90:
		return "90";
	case WL_OUTPUT_TRANSFORM_180:
		return "180";
	case WL_OUTPUT_TRANSFORM_270:
		return "270";
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		return "flipped";
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		return "flipped-90";
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		return "flipped-180";
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		return "flipped-270";
	}
	return "normal";
}

static struct aswlrandr_head *head_find_by_name(struct aswlrandr_state *state, const char *name)
{
	if (state == NULL || name == NULL)
		return NULL;
	struct aswlrandr_head *h;
	wl_list_for_each(h, &state->heads, link) {
		if (h->name != NULL && strcmp(h->name, name) == 0)
			return h;
	}
	return NULL;
}

static struct aswlrandr_mode *head_find_mode(struct aswlrandr_head *head, struct zwlr_output_mode_v1 *mode_obj)
{
	if (head == NULL || mode_obj == NULL)
		return NULL;
	struct aswlrandr_mode *m;
	wl_list_for_each(m, &head->modes, link) {
		if (m->mode == mode_obj)
			return m;
	}
	return NULL;
}

static struct aswlrandr_mode *head_preferred_mode(struct aswlrandr_head *head)
{
	if (head == NULL)
		return NULL;
	struct aswlrandr_mode *m;
	struct aswlrandr_mode *fallback = NULL;
	wl_list_for_each(m, &head->modes, link) {
		if (fallback == NULL)
			fallback = m;
		if (m->preferred)
			return m;
	}
	return fallback;
}

static void handle_mode_size(void *data, struct zwlr_output_mode_v1 *mode, int32_t width, int32_t height)
{
	(void)mode;
	struct aswlrandr_mode *m = data;
	m->width = width;
	m->height = height;
	m->have_size = true;
}

static void handle_mode_refresh(void *data, struct zwlr_output_mode_v1 *mode, int32_t refresh)
{
	(void)mode;
	struct aswlrandr_mode *m = data;
	m->refresh_mhz = refresh;
	m->have_refresh = true;
}

static void handle_mode_preferred(void *data, struct zwlr_output_mode_v1 *mode)
{
	(void)mode;
	struct aswlrandr_mode *m = data;
	m->preferred = true;
}

static void handle_mode_finished(void *data, struct zwlr_output_mode_v1 *mode)
{
	(void)mode;
	struct aswlrandr_mode *m = data;
	mode_destroy(m);
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
	.size = handle_mode_size,
	.refresh = handle_mode_refresh,
	.preferred = handle_mode_preferred,
	.finished = handle_mode_finished,
};

static void handle_head_name(void *data, struct zwlr_output_head_v1 *head, const char *name)
{
	(void)head;
	struct aswlrandr_head *h = data;
	free(h->name);
	h->name = name != NULL ? strdup(name) : NULL;
}

static void handle_head_description(void *data, struct zwlr_output_head_v1 *head, const char *desc)
{
	(void)head;
	struct aswlrandr_head *h = data;
	free(h->description);
	h->description = desc != NULL ? strdup(desc) : NULL;
}

static void handle_head_physical_size(void *data, struct zwlr_output_head_v1 *head, int32_t w, int32_t h)
{
	(void)data;
	(void)head;
	(void)w;
	(void)h;
}

static void handle_head_mode(void *data, struct zwlr_output_head_v1 *head, struct zwlr_output_mode_v1 *mode)
{
	(void)head;
	struct aswlrandr_head *h = data;

	struct aswlrandr_mode *m = calloc(1, sizeof(*m));
	if (m == NULL) {
		zwlr_output_mode_v1_destroy(mode);
		return;
	}
	m->state = h->state;
	m->mode = mode;
	wl_list_insert(h->modes.prev, &m->link);
	zwlr_output_mode_v1_add_listener(mode, &mode_listener, m);
}

static void handle_head_enabled(void *data, struct zwlr_output_head_v1 *head, int32_t enabled)
{
	(void)head;
	struct aswlrandr_head *h = data;
	h->have_enabled = true;
	h->enabled = enabled != 0;
}

static void handle_head_current_mode(void *data, struct zwlr_output_head_v1 *head, struct zwlr_output_mode_v1 *mode)
{
	(void)head;
	struct aswlrandr_head *h = data;
	h->current_mode = mode;
}

static void handle_head_position(void *data, struct zwlr_output_head_v1 *head, int32_t x, int32_t y)
{
	(void)head;
	struct aswlrandr_head *h = data;
	h->have_pos = true;
	h->x = x;
	h->y = y;
}

static void handle_head_transform(void *data, struct zwlr_output_head_v1 *head, int32_t transform)
{
	(void)head;
	struct aswlrandr_head *h = data;
	h->have_transform = true;
	h->transform = (enum wl_output_transform)transform;
}

static void handle_head_scale(void *data, struct zwlr_output_head_v1 *head, wl_fixed_t scale)
{
	(void)head;
	struct aswlrandr_head *h = data;
	h->have_scale = true;
	h->scale = wl_fixed_to_double(scale);
}

static void handle_head_finished(void *data, struct zwlr_output_head_v1 *head)
{
	(void)head;
	struct aswlrandr_head *h = data;
	head_destroy(h);
}

static void handle_head_make(void *data, struct zwlr_output_head_v1 *head, const char *make)
{
	(void)head;
	struct aswlrandr_head *h = data;
	free(h->make);
	h->make = make != NULL ? strdup(make) : NULL;
}

static void handle_head_model(void *data, struct zwlr_output_head_v1 *head, const char *model)
{
	(void)head;
	struct aswlrandr_head *h = data;
	free(h->model);
	h->model = model != NULL ? strdup(model) : NULL;
}

static void handle_head_serial_number(void *data, struct zwlr_output_head_v1 *head, const char *serial)
{
	(void)head;
	struct aswlrandr_head *h = data;
	free(h->serial_number);
	h->serial_number = serial != NULL ? strdup(serial) : NULL;
}

static void handle_head_adaptive_sync(void *data, struct zwlr_output_head_v1 *head, uint32_t state)
{
	(void)data;
	(void)head;
	(void)state;
}

static const struct zwlr_output_head_v1_listener head_listener = {
	.name = handle_head_name,
	.description = handle_head_description,
	.physical_size = handle_head_physical_size,
	.mode = handle_head_mode,
	.enabled = handle_head_enabled,
	.current_mode = handle_head_current_mode,
	.position = handle_head_position,
	.transform = handle_head_transform,
	.scale = handle_head_scale,
	.finished = handle_head_finished,
	.make = handle_head_make,
	.model = handle_head_model,
	.serial_number = handle_head_serial_number,
	.adaptive_sync = handle_head_adaptive_sync,
};

static void handle_manager_head(void *data, struct zwlr_output_manager_v1 *mgr, struct zwlr_output_head_v1 *head)
{
	(void)mgr;
	struct aswlrandr_state *state = data;

	struct aswlrandr_head *h = calloc(1, sizeof(*h));
	if (h == NULL) {
		zwlr_output_head_v1_destroy(head);
		return;
	}
	h->state = state;
	h->head = head;
	wl_list_init(&h->modes);
	wl_list_insert(state->heads.prev, &h->link);

	zwlr_output_head_v1_add_listener(head, &head_listener, h);
}

static void handle_manager_done(void *data, struct zwlr_output_manager_v1 *mgr, uint32_t serial)
{
	(void)mgr;
	struct aswlrandr_state *state = data;
	state->serial = serial;
	state->got_done = true;
}

static void handle_manager_finished(void *data, struct zwlr_output_manager_v1 *mgr)
{
	(void)mgr;
	struct aswlrandr_state *state = data;
	state->manager = NULL;
}

static const struct zwlr_output_manager_v1_listener manager_listener = {
	.head = handle_manager_head,
	.done = handle_manager_done,
	.finished = handle_manager_finished,
};

static void mode_destroy(struct aswlrandr_mode *mode)
{
	if (mode == NULL)
		return;
	wl_list_remove(&mode->link);
	if (mode->mode != NULL)
		zwlr_output_mode_v1_destroy(mode->mode);
	free(mode);
}

static void head_destroy(struct aswlrandr_head *head)
{
	if (head == NULL)
		return;
	wl_list_remove(&head->link);
	struct aswlrandr_mode *m;
	struct aswlrandr_mode *tmp;
	wl_list_for_each_safe(m, tmp, &head->modes, link)
		mode_destroy(m);
	if (head->head != NULL)
		zwlr_output_head_v1_destroy(head->head);
	free(head->name);
	free(head->description);
	free(head->make);
	free(head->model);
	free(head->serial_number);
	free(head);
}

static void state_destroy(struct aswlrandr_state *state)
{
	if (state == NULL)
		return;
	struct aswlrandr_head *h;
	struct aswlrandr_head *tmp;
	wl_list_for_each_safe(h, tmp, &state->heads, link)
		head_destroy(h);
	if (state->manager != NULL)
		zwlr_output_manager_v1_destroy(state->manager);
	if (state->registry != NULL)
		wl_registry_destroy(state->registry);
	if (state->display != NULL)
		wl_display_disconnect(state->display);
}

static void handle_registry_global(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name,
                                   const char *interface,
                                   uint32_t version)
{
	struct aswlrandr_state *state = data;
	(void)registry;

	if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
		uint32_t bind_version = version;
		if (bind_version > 4)
			bind_version = 4;
		state->manager_version = bind_version;
		state->manager = wl_registry_bind(state->registry, name, &zwlr_output_manager_v1_interface, bind_version);
		state->got_manager = state->manager != NULL;
		if (state->manager != NULL)
			zwlr_output_manager_v1_add_listener(state->manager, &manager_listener, state);
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

struct config_result {
	bool done;
	bool ok;
	bool cancelled;
};

static void handle_config_succeeded(void *data, struct zwlr_output_configuration_v1 *cfg)
{
	(void)cfg;
	struct config_result *r = data;
	r->done = true;
	r->ok = true;
}

static void handle_config_failed(void *data, struct zwlr_output_configuration_v1 *cfg)
{
	(void)cfg;
	struct config_result *r = data;
	r->done = true;
	r->ok = false;
}

static void handle_config_cancelled(void *data, struct zwlr_output_configuration_v1 *cfg)
{
	(void)cfg;
	struct config_result *r = data;
	r->done = true;
	r->ok = false;
	r->cancelled = true;
}

static const struct zwlr_output_configuration_v1_listener config_listener = {
	.succeeded = handle_config_succeeded,
	.failed = handle_config_failed,
	.cancelled = handle_config_cancelled,
};

static bool ensure_initial_state(struct aswlrandr_state *state)
{
	if (state == NULL || state->display == NULL)
		return false;

	for (int i = 0; i < 8 && !state->got_done; i++) {
		if (wl_display_roundtrip(state->display) < 0)
			return false;
	}
	return state->got_done;
}

static void print_heads(struct aswlrandr_state *state)
{
	struct aswlrandr_head *h;
	wl_list_for_each(h, &state->heads, link) {
		const char *name = h->name != NULL ? h->name : "(unknown)";
		const char *desc = h->description != NULL ? h->description : "";
		bool enabled = h->have_enabled ? h->enabled : false;

		const char *mode_s = "";
		char mode_buf[64];
		struct aswlrandr_mode *m = head_find_mode(h, h->current_mode);
		if (m != NULL && m->have_size) {
			if (m->have_refresh)
				(void)snprintf(mode_buf, sizeof(mode_buf), "%dx%d@%d", m->width, m->height, m->refresh_mhz);
			else
				(void)snprintf(mode_buf, sizeof(mode_buf), "%dx%d", m->width, m->height);
			mode_s = mode_buf;
		}

		fprintf(stdout,
		        "%s\tenabled=%d\tpos=%d,%d\tscale=%.3f\ttransform=%s\tmode=%s\t%s\n",
		        name,
		        enabled ? 1 : 0,
		        h->have_pos ? h->x : 0,
		        h->have_pos ? h->y : 0,
		        h->have_scale ? h->scale : 1.0,
		        h->have_transform ? transform_to_string(h->transform) : "normal",
		        mode_s,
		        desc);
	}
}

static bool apply_reconfigure(struct aswlrandr_state *state,
                              const char *target_name,
                              bool want_enable,
                              bool want_disable,
                              bool want_move,
                              int32_t move_x,
                              int32_t move_y)
{
	if (state == NULL || state->manager == NULL || !state->got_done)
		return false;

	struct config_result result = { 0 };
	struct zwlr_output_configuration_v1 *cfg = zwlr_output_manager_v1_create_configuration(state->manager, state->serial);
	if (cfg == NULL)
		return false;
	zwlr_output_configuration_v1_add_listener(cfg, &config_listener, &result);

	struct aswlrandr_head *h;
	wl_list_for_each(h, &state->heads, link) {
		bool enabled = h->have_enabled ? h->enabled : false;
		if (target_name != NULL && h->name != NULL && strcmp(h->name, target_name) == 0) {
			if (want_enable)
				enabled = true;
			if (want_disable)
				enabled = false;
		}

		if (!enabled) {
			zwlr_output_configuration_v1_disable_head(cfg, h->head);
			continue;
		}

		struct zwlr_output_configuration_head_v1 *ch = zwlr_output_configuration_v1_enable_head(cfg, h->head);
		if (ch == NULL)
			continue;

		struct zwlr_output_mode_v1 *mode = h->current_mode;
		if (mode == NULL) {
			struct aswlrandr_mode *pref = head_preferred_mode(h);
			if (pref != NULL)
				mode = pref->mode;
		}
		if (mode != NULL)
			zwlr_output_configuration_head_v1_set_mode(ch, mode);

		int32_t x = h->have_pos ? h->x : 0;
		int32_t y = h->have_pos ? h->y : 0;
		if (want_move && target_name != NULL && h->name != NULL && strcmp(h->name, target_name) == 0) {
			x = move_x;
			y = move_y;
		}
		zwlr_output_configuration_head_v1_set_position(ch, x, y);

		enum wl_output_transform transform = h->have_transform ? h->transform : WL_OUTPUT_TRANSFORM_NORMAL;
		zwlr_output_configuration_head_v1_set_transform(ch, (int32_t)transform);

		double scale = h->have_scale ? h->scale : 1.0;
		zwlr_output_configuration_head_v1_set_scale(ch, wl_fixed_from_double(scale));
	}

	zwlr_output_configuration_v1_apply(cfg);

	while (!result.done) {
		if (wl_display_dispatch(state->display) < 0)
			break;
	}

	zwlr_output_configuration_v1_destroy(cfg);

	if (!result.done)
		return false;
	if (result.cancelled) {
		fprintf(stderr, "aswlrandr: configuration cancelled (outdated serial)\n");
		return false;
	}
	return result.ok;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage(argv[0]);
		return 2;
	}

	struct aswlrandr_state state = { 0 };
	wl_list_init(&state.heads);

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "aswlrandr: wl_display_connect failed\n");
		return 1;
	}

	state.registry = wl_display_get_registry(state.display);
	if (state.registry == NULL) {
		fprintf(stderr, "aswlrandr: wl_display_get_registry failed\n");
		state_destroy(&state);
		return 1;
	}

	wl_registry_add_listener(state.registry, &registry_listener, &state);

	if (wl_display_roundtrip(state.display) < 0 || !state.got_manager) {
		fprintf(stderr, "aswlrandr: zwlr_output_manager_v1 not available\n");
		state_destroy(&state);
		return 1;
	}

	if (!ensure_initial_state(&state)) {
		fprintf(stderr, "aswlrandr: failed to fetch output state\n");
		state_destroy(&state);
		return 1;
	}

	const char *cmd = argv[1];
	if (strcmp(cmd, "list") == 0) {
		print_heads(&state);
		state_destroy(&state);
		return 0;
	}

	if (strcmp(cmd, "move") == 0) {
		if (argc != 5) {
			usage(argv[0]);
			state_destroy(&state);
			return 2;
		}
		const char *name = argv[2];
		int32_t x = 0;
		int32_t y = 0;
		if (!parse_i32(argv[3], -100000, 100000, &x) || !parse_i32(argv[4], -100000, 100000, &y)) {
			fprintf(stderr, "aswlrandr: invalid x/y\n");
			state_destroy(&state);
			return 2;
		}
		if (head_find_by_name(&state, name) == NULL) {
			fprintf(stderr, "aswlrandr: unknown output: %s\n", name);
			state_destroy(&state);
			return 2;
		}
		bool ok = apply_reconfigure(&state, name, false, false, true, x, y);
		state_destroy(&state);
		return ok ? 0 : 1;
	}

	if (strcmp(cmd, "enable") == 0 || strcmp(cmd, "disable") == 0) {
		if (argc != 3) {
			usage(argv[0]);
			state_destroy(&state);
			return 2;
		}
		const char *name = argv[2];
		if (head_find_by_name(&state, name) == NULL) {
			fprintf(stderr, "aswlrandr: unknown output: %s\n", name);
			state_destroy(&state);
			return 2;
		}
		bool ok = apply_reconfigure(&state, name, strcmp(cmd, "enable") == 0, strcmp(cmd, "disable") == 0, false, 0, 0);
		state_destroy(&state);
		return ok ? 0 : 1;
	}

	fprintf(stderr, "aswlrandr: unknown command: %s\n", cmd);
	usage(argv[0]);
	state_destroy(&state);
	return 2;
}
