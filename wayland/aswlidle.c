#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <wayland-client.h>

#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct as_buffer {
	struct wl_buffer *wl_buffer;
	void *data;
	int width;
	int height;
	int stride;
	size_t size;
	bool busy;
};

struct as_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zwlr_layer_shell_v1 *layer_shell;

	struct zwp_idle_inhibit_manager_v1 *idle_inhibit_manager;

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct zwp_idle_inhibitor_v1 *idle_inhibitor;
	struct as_buffer buffer;
	bool have_configure;
	bool buffer_committed;

	int hold_ms;
};

static void usage(const char *argv0)
{
	fprintf(stderr,
	        "Usage: %s [--hold-ms MS]\n"
	        "\n"
	        "Creates a tiny 1x1 layer-shell surface and an idle inhibitor for it, then sleeps\n"
	        "for the requested amount of time.\n",
	        argv0);
}

static int create_tmpfile(size_t size)
{
	int fd = -1;

#ifdef __linux__
	fd = memfd_create("aswlidle", MFD_CLOEXEC);
	if (fd >= 0) {
		if (ftruncate(fd, (off_t)size) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}
#endif

	char template[] = "/tmp/aswlidle-XXXXXX";
	fd = mkstemp(template);
	if (fd < 0)
		return -1;

	unlink(template);
	if (ftruncate(fd, (off_t)size) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	(void)wl_buffer;
	struct as_buffer *buf = data;
	if (buf != NULL)
		buf->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_release,
};

static bool buffer_init(struct as_buffer *buf, struct wl_shm *shm, int width, int height)
{
	if (buf == NULL || shm == NULL || width <= 0 || height <= 0)
		return false;

	memset(buf, 0, sizeof(*buf));
	buf->width = width;
	buf->height = height;
	buf->stride = width * 4;
	buf->size = (size_t)buf->stride * (size_t)height;

	int fd = create_tmpfile(buf->size);
	if (fd < 0)
		return false;

	buf->data = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf->data == MAP_FAILED) {
		close(fd);
		buf->data = NULL;
		return false;
	}

	memset(buf->data, 0, buf->size); /* fully transparent */

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int)buf->size);
	close(fd);
	if (pool == NULL)
		return false;

	buf->wl_buffer = wl_shm_pool_create_buffer(pool,
	                                           0,
	                                           width,
	                                           height,
	                                           buf->stride,
	                                           WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);

	if (buf->wl_buffer == NULL)
		return false;

	wl_buffer_add_listener(buf->wl_buffer, &buffer_listener, buf);
	buf->busy = true;
	return true;
}

static void buffer_destroy(struct as_buffer *buf)
{
	if (buf == NULL)
		return;

	if (buf->wl_buffer != NULL)
		wl_buffer_destroy(buf->wl_buffer);
	buf->wl_buffer = NULL;

	if (buf->data != NULL && buf->data != MAP_FAILED)
		munmap(buf->data, buf->size);
	buf->data = NULL;
	buf->size = 0;
}

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
	struct as_state *state = data;
	if (state == NULL)
		return;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		uint32_t v = version < 4 ? version : 4;
		state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, v);
		return;
	}
	if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		return;
	}
	if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		state->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
		return;
	}
	if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0) {
		state->idle_inhibit_manager = wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, 1);
		return;
	}
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *layer_surface,
                                    uint32_t serial,
                                    uint32_t width,
                                    uint32_t height)
{
	(void)width;
	(void)height;

	struct as_state *state = data;
	if (state == NULL)
		return;

	state->have_configure = true;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

	if (!state->buffer_committed) {
		wl_surface_attach(state->surface, state->buffer.wl_buffer, 0, 0);
		wl_surface_damage(state->surface, 0, 0, state->buffer.width, state->buffer.height);
		state->buffer_committed = true;
	}
	wl_surface_commit(state->surface);

	if (state->idle_inhibitor == NULL && state->idle_inhibit_manager != NULL) {
		state->idle_inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(state->idle_inhibit_manager, state->surface);
	}
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
	struct as_state *state = data;
	if (state != NULL && state->layer_surface == layer_surface)
		state->layer_surface = NULL;
	zwlr_layer_surface_v1_destroy(layer_surface);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

int main(int argc, char **argv)
{
	struct as_state state = { 0 };
	state.hold_ms = 2000;

	for (int i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
			usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--hold-ms") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 2;
			}
			char *end = NULL;
			long v = strtol(argv[++i], &end, 10);
			if (end == argv[i] || *end != '\0' || v < 0 || v > 60 * 60 * 1000L) {
				fprintf(stderr, "aswlidle: bad --hold-ms %s\n", argv[i]);
				return 2;
			}
			state.hold_ms = (int)v;
			continue;
		}
		fprintf(stderr, "aswlidle: unknown argument: %s\n", argv[i]);
		usage(argv[0]);
		return 2;
	}

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "aswlidle: wl_display_connect failed\n");
		return 1;
	}

	state.registry = wl_display_get_registry(state.display);
	if (state.registry == NULL) {
		fprintf(stderr, "aswlidle: wl_display_get_registry failed\n");
		wl_display_disconnect(state.display);
		return 1;
	}
	wl_registry_add_listener(state.registry, &registry_listener, &state);
	(void)wl_display_roundtrip(state.display);

	if (state.compositor == NULL || state.shm == NULL) {
		fprintf(stderr, "aswlidle: missing compositor/shm globals\n");
		wl_display_disconnect(state.display);
		return 1;
	}
	if (state.idle_inhibit_manager == NULL) {
		fprintf(stderr, "aswlidle: missing zwp_idle_inhibit_manager_v1 (idle-inhibit-v1)\n");
		wl_display_disconnect(state.display);
		return 1;
	}
	if (state.layer_shell == NULL) {
		fprintf(stderr, "aswlidle: missing zwlr_layer_shell_v1 (layer-shell)\n");
		wl_display_disconnect(state.display);
		return 1;
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	if (state.surface == NULL) {
		fprintf(stderr, "aswlidle: wl_compositor_create_surface failed\n");
		wl_display_disconnect(state.display);
		return 1;
	}

	if (!buffer_init(&state.buffer, state.shm, 1, 1)) {
		fprintf(stderr, "aswlidle: buffer_init failed\n");
		wl_display_disconnect(state.display);
		return 1;
	}

	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell,
	                                                            state.surface,
	                                                            NULL,
	                                                            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
	                                                            "afterstep.aswlidle");
	if (state.layer_surface == NULL) {
		fprintf(stderr, "aswlidle: get_layer_surface failed\n");
		wl_display_disconnect(state.display);
		return 1;
	}
	zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surface_listener, &state);

	zwlr_layer_surface_v1_set_size(state.layer_surface, 1, 1);
	zwlr_layer_surface_v1_set_anchor(state.layer_surface,
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, 0);
	zwlr_layer_surface_v1_set_keyboard_interactivity(state.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

	/* Initial commit so the compositor sends configure. */
	wl_surface_commit(state.surface);
	(void)wl_display_roundtrip(state.display);
	if (!state.have_configure || state.idle_inhibitor == NULL) {
		fprintf(stderr, "aswlidle: did not get configure or create inhibitor\n");
		wl_display_disconnect(state.display);
		return 1;
	}
	(void)wl_display_roundtrip(state.display);

	if (state.hold_ms > 0) {
		struct timespec ts;
		ts.tv_sec = state.hold_ms / 1000;
		ts.tv_nsec = (long)(state.hold_ms % 1000) * 1000000L;
		(void)nanosleep(&ts, NULL);
	}

	if (state.idle_inhibitor != NULL)
		zwp_idle_inhibitor_v1_destroy(state.idle_inhibitor);
	if (state.layer_surface != NULL)
		zwlr_layer_surface_v1_destroy(state.layer_surface);
	if (state.surface != NULL)
		wl_surface_destroy(state.surface);
	buffer_destroy(&state.buffer);

	if (state.idle_inhibit_manager != NULL)
		zwp_idle_inhibit_manager_v1_destroy(state.idle_inhibit_manager);
	if (state.layer_shell != NULL)
		zwlr_layer_shell_v1_destroy(state.layer_shell);
	if (state.shm != NULL)
		wl_shm_destroy(state.shm);
	if (state.compositor != NULL)
		wl_compositor_destroy(state.compositor);
	if (state.registry != NULL)
		wl_registry_destroy(state.registry);

	wl_display_disconnect(state.display);
	return 0;
}
