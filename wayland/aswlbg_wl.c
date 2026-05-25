#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlbg_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <X11/Xlib.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "afterimage.h"

struct aswl_bg_snapshot_header {
	char magic[8]; /* "ASWLBG1\0" */
	uint32_t width;
	uint32_t height;
	uint32_t stride; /* bytes per row */
	uint32_t format; /* reserved; currently 0 = ARGB8888 */
};

static char *aswl_expand_tilde(const char *path)
{
	if (path == NULL)
		return NULL;

	if (path[0] != '~')
		return strdup(path);

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		return strdup(path);

	if (path[1] == '\0')
		return strdup(home);
	if (path[1] != '/')
		return strdup(path);

	size_t home_len = strlen(home);
	size_t rest_len = strlen(path + 1);
	char *out = malloc(home_len + rest_len + 1);
	if (out == NULL)
		return NULL;

	memcpy(out, home, home_len);
	memcpy(out + home_len, path + 1, rest_len + 1);
	return out;
}

static bool aswl_write_full(int fd, const void *data, size_t len)
{
	const uint8_t *p = data;
	size_t left = len;
	while (left > 0) {
		ssize_t n = write(fd, p, left);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (n == 0)
			return false;
		p += (size_t)n;
		left -= (size_t)n;
	}
	return true;
}

static char *aswl_sanitize_filename_segment(const char *s)
{
	if (s == NULL || s[0] == '\0')
		return strdup("wayland");

	size_t len = strlen(s);
	if (len > 200)
		len = 200;

	char *out = malloc(len + 1);
	if (out == NULL)
		return NULL;

	for (size_t i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)s[i];
		if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
			out[i] = (char)ch;
		else
			out[i] = '_';
	}
	out[len] = '\0';
	return out;
}

static char *aswl_bg_snapshot_path(void)
{
	const char *env = getenv("ASWLBG_SNAPSHOT");
	if (env != NULL) {
		if (env[0] == '\0' || strcmp(env, "0") == 0 || strcasecmp(env, "off") == 0 || strcasecmp(env, "false") == 0)
			return NULL;
		return aswl_expand_tilde(env);
	}

	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime == NULL || runtime[0] == '\0')
		runtime = "/tmp";

	char *safe = aswl_sanitize_filename_segment(getenv("WAYLAND_DISPLAY"));
	if (safe == NULL)
		return NULL;

	char *out = NULL;
	if (asprintf(&out, "%s/afterstep.aswlbg.%s.argb", runtime, safe) < 0)
		out = NULL;
	free(safe);
	return out;
}

static void aswl_bg_snapshot_write(const struct as_state *state)
{
	if (state == NULL)
		return;
	if (state->background.argb == NULL || state->background.width <= 0 || state->background.height <= 0)
		return;

	char *path = aswl_bg_snapshot_path();
	if (path == NULL)
		return;

	char *tmp = NULL;
	if (asprintf(&tmp, "%s.tmp.%ld", path, (long)getpid()) < 0)
		tmp = NULL;

	int fd = -1;
	if (tmp != NULL)
		fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) {
		free(tmp);
		free(path);
		return;
	}

	struct aswl_bg_snapshot_header hdr = {
		.magic = { 'A', 'S', 'W', 'L', 'B', 'G', '1', '\0' },
		.width = (uint32_t)state->background.width,
		.height = (uint32_t)state->background.height,
		.stride = (uint32_t)state->background.width * 4u,
		.format = 0,
	};

	size_t pixels_bytes = (size_t)state->background.width * (size_t)state->background.height * 4u;
	bool ok = aswl_write_full(fd, &hdr, sizeof(hdr)) && aswl_write_full(fd, state->background.argb, pixels_bytes);
	(void)fsync(fd);
	close(fd);

	if (ok)
		(void)rename(tmp, path);
	else
		(void)unlink(tmp);

	free(tmp);
	free(path);
}

struct as_buffer {
	struct wl_buffer *wl_buffer;
	void *data;
	int width;
	int height;
	int stride;
	size_t size;
	bool busy;
};

static int create_tmpfile(size_t size)
{
	int fd = -1;

#ifdef __linux__
	fd = memfd_create("aswlbg", MFD_CLOEXEC);
	if (fd >= 0) {
		if (ftruncate(fd, (off_t)size) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}
#endif

	char template[] = "/tmp/aswlbg-XXXXXX";
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
	buf->busy = false;
}

static void as_buffer_destroy(struct as_buffer *buf)
{
	if (buf == NULL)
		return;
	if (buf->wl_buffer != NULL)
		wl_buffer_destroy(buf->wl_buffer);
	if (buf->data != NULL && buf->size > 0)
		munmap(buf->data, buf->size);
	free(buf);
}

static struct as_buffer *as_buffer_create(struct as_state *state, int width, int height)
{
	if (state == NULL || state->shm == NULL)
		return NULL;
	if (width <= 0 || height <= 0)
		return NULL;

	struct as_buffer *buf = calloc(1, sizeof(*buf));
	if (buf == NULL)
		return NULL;

	buf->width = width;
	buf->height = height;
	buf->stride = width * 4;
	buf->size = (size_t)buf->stride * (size_t)height;

	int fd = create_tmpfile(buf->size);
	if (fd < 0) {
		as_buffer_destroy(buf);
		return NULL;
	}

	buf->data = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf->data == MAP_FAILED) {
		close(fd);
		as_buffer_destroy(buf);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, (int)buf->size);
	buf->wl_buffer = wl_shm_pool_create_buffer(pool,
	                                           0,
	                                           width,
	                                           height,
	                                           buf->stride,
	                                           WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	if (buf->wl_buffer == NULL) {
		as_buffer_destroy(buf);
		return NULL;
	}

	static const struct wl_buffer_listener wl_buf_listener = {
		.release = buffer_release,
	};
	wl_buffer_add_listener(buf->wl_buffer, &wl_buf_listener, buf);
	return buf;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms);

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

static void draw_and_commit(struct as_state *state)
{
	if (state == NULL || state->surface == NULL)
		return;
	if (!state->configured)
		return;
	if (state->width <= 0 || state->height <= 0)
		return;

	if (state->buffer == NULL || state->buffer->width != state->width || state->buffer->height != state->height) {
		as_buffer_destroy(state->buffer);
		state->buffer = as_buffer_create(state, state->width, state->height);
	}

	struct as_buffer *buf = state->buffer;
	if (buf == NULL || buf->data == NULL) {
		state->running = false;
		return;
	}
	if (buf->busy)
		return;

	/* Render AfterStep's default background XML (fallback to simple wallpaper if needed). */
	if (state->background.argb == NULL || state->background.width != buf->width || state->background.height != buf->height) {
		if (!aswlbg_render_background_afterimage(state, &state->background, buf->width, buf->height)) {
			aswlbg_image_destroy(&state->background);
		}
	}

	if (state->background.argb != NULL) {
		aswl_bg_snapshot_write(state);
		uint32_t *dst = buf->data;
		int dst_stride_px = buf->stride / 4;
		for (int y = 0; y < buf->height; y++) {
			memcpy(dst + (size_t)y * (size_t)dst_stride_px,
			       state->background.argb + (size_t)y * (size_t)buf->width,
			       (size_t)buf->width * 4);
		}
	} else {
		memset(buf->data, 0, buf->size);
	}

	buf->busy = true;
	wl_surface_attach(state->surface, buf->wl_buffer, 0, 0);
	wl_surface_damage(state->surface, 0, 0, buf->width, buf->height);
	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);
	state->frame_cb = wl_surface_frame(state->surface);
	wl_callback_add_listener(state->frame_cb, &frame_listener, state);
	wl_surface_commit(state->surface);
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms)
{
	(void)time_ms;
	struct as_state *state = data;

	if (cb != NULL)
		wl_callback_destroy(cb);
	if (state->frame_cb == cb)
		state->frame_cb = NULL;

	/* Redraw only when needed (configure triggers draw). */
}

static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial,
                                    uint32_t width,
                                    uint32_t height)
{
	struct as_state *state = data;

	zwlr_layer_surface_v1_ack_configure(surface, serial);
	state->configured = true;

	if ((int)width > 0)
		state->width = (int)width;
	if ((int)height > 0)
		state->height = (int)height;

	draw_and_commit(state);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
	(void)surface;
	struct as_state *state = data;
	state->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
	struct as_state *state = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version > 4 ? 4 : version);
		return;
	}
	if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		return;
	}
	if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		uint32_t bind_version = version > 4 ? 4 : version;
		state->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, bind_version);
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

static void cleanup(struct as_state *state)
{
	if (state == NULL)
		return;

	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);

	as_buffer_destroy(state->buffer);
	state->buffer = NULL;

	aswlbg_image_destroy(&state->background);

	if (state->asv != NULL) {
		destroy_asvisual(state->asv, false);
		state->asv = NULL;
	}
	if (state->x11_display != NULL) {
		XCloseDisplay(state->x11_display);
		state->x11_display = NULL;
	}

	if (state->layer_surface != NULL)
		zwlr_layer_surface_v1_destroy(state->layer_surface);
	if (state->surface != NULL)
		wl_surface_destroy(state->surface);

	if (state->layer_shell != NULL)
		zwlr_layer_shell_v1_destroy(state->layer_shell);
	if (state->shm != NULL)
		wl_shm_destroy(state->shm);
	if (state->compositor != NULL)
		wl_compositor_destroy(state->compositor);
	if (state->registry != NULL)
		wl_registry_destroy(state->registry);
	if (state->display != NULL)
		wl_display_disconnect(state->display);
}

int aswlbg_run(struct as_state *state)
{
	if (state == NULL)
		return 1;

	state->display = wl_display_connect(NULL);
	if (state->display == NULL) {
		fprintf(stderr, "aswlbg: wl_display_connect failed: %s\n", strerror(errno));
		return 1;
	}

	state->registry = wl_display_get_registry(state->display);
	if (state->registry == NULL) {
		fprintf(stderr, "aswlbg: wl_display_get_registry failed\n");
		cleanup(state);
		return 1;
	}

	wl_registry_add_listener(state->registry, &registry_listener, state);
	wl_display_roundtrip(state->display);

	if (state->compositor == NULL || state->shm == NULL || state->layer_shell == NULL) {
		fprintf(stderr, "aswlbg: missing globals (compositor=%p shm=%p layer_shell=%p)\n",
		        (void *)state->compositor,
		        (void *)state->shm,
		        (void *)state->layer_shell);
		cleanup(state);
		return 1;
	}

	state->surface = wl_compositor_create_surface(state->compositor);
	if (state->surface == NULL) {
		fprintf(stderr, "aswlbg: wl_compositor_create_surface failed\n");
		cleanup(state);
		return 1;
	}

	state->layer_surface = zwlr_layer_shell_v1_get_layer_surface(state->layer_shell,
	                                                            state->surface,
	                                                            NULL,
	                                                            ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
	                                                            "afterstep-aswlbg");
	if (state->layer_surface == NULL) {
		fprintf(stderr, "aswlbg: zwlr_layer_shell_v1_get_layer_surface failed\n");
		cleanup(state);
		return 1;
	}

	zwlr_layer_surface_v1_add_listener(state->layer_surface, &layer_surface_listener, state);

	zwlr_layer_surface_v1_set_anchor(state->layer_surface,
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(state->layer_surface, 0, 0);
	zwlr_layer_surface_v1_set_exclusive_zone(state->layer_surface, 0);

	wl_surface_commit(state->surface);
	wl_display_roundtrip(state->display);

	while (state->running) {
		if (wl_display_dispatch(state->display) < 0)
			break;
	}

	cleanup(state);
	return 0;
}

