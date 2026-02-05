#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--require IFACE]... [IFACE]...\n", prog);
	fprintf(stderr, "Checks that the compositor advertises required Wayland globals.\n");
}

struct as_probe_state {
	const char **required;
	bool *found;
	size_t required_count;
};

static void handle_registry_global(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name,
                                   const char *interface,
                                   uint32_t version)
{
	(void)registry;
	(void)name;
	(void)version;
	struct as_probe_state *st = data;
	if (st == NULL || interface == NULL)
		return;

	for (size_t i = 0; i < st->required_count; i++) {
		if (st->required[i] != NULL && strcmp(st->required[i], interface) == 0)
			st->found[i] = true;
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

int main(int argc, char **argv)
{
	const char *default_required[] = {
		"zwp_text_input_manager_v3",
		"zwp_input_method_manager_v2",
		"zwp_virtual_keyboard_manager_v1",
	};

	const char **required = NULL;
	size_t required_count = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage(argv[0]);
			return 0;
		} else if (strcmp(argv[i], "--require") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "%s: --require needs an argument\n", argv[0]);
				return 2;
			}
			i++;
			const char **new_required = realloc(required, (required_count + 1) * sizeof(*new_required));
			if (new_required == NULL) {
				fprintf(stderr, "%s: realloc failed\n", argv[0]);
				return 1;
			}
			required = new_required;
			required[required_count++] = argv[i];
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "%s: unknown argument: %s\n", argv[0], argv[i]);
			usage(argv[0]);
			return 2;
		} else {
			const char **new_required = realloc(required, (required_count + 1) * sizeof(*new_required));
			if (new_required == NULL) {
				fprintf(stderr, "%s: realloc failed\n", argv[0]);
				return 1;
			}
			required = new_required;
			required[required_count++] = argv[i];
		}
	}

	if (required_count == 0) {
		required = (const char **)default_required;
		required_count = sizeof(default_required) / sizeof(default_required[0]);
	}

	bool *found = calloc(required_count, sizeof(*found));
	if (found == NULL) {
		fprintf(stderr, "%s: calloc failed\n", argv[0]);
		if (required != default_required)
			free(required);
		return 1;
	}

	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "%s: wl_display_connect failed\n", argv[0]);
		if (required != default_required)
			free(required);
		free(found);
		return 1;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	if (registry == NULL) {
		fprintf(stderr, "%s: wl_display_get_registry failed\n", argv[0]);
		wl_display_disconnect(display);
		if (required != default_required)
			free(required);
		free(found);
		return 1;
	}

	struct as_probe_state st = {
		.required = required,
		.found = found,
		.required_count = required_count,
	};

	wl_registry_add_listener(registry, &registry_listener, &st);

	(void)wl_display_roundtrip(display);
	(void)wl_display_roundtrip(display);

	bool ok = true;
	for (size_t i = 0; i < required_count; i++) {
		if (!found[i]) {
			ok = false;
			fprintf(stderr, "%s: missing global: %s\n", argv[0], required[i]);
		}
	}

	wl_registry_destroy(registry);
	wl_display_disconnect(display);

	if (required != default_required)
		free(required);
	free(found);

	return ok ? 0 : 1;
}

