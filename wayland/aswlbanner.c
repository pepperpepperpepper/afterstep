#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <X11/Xlib.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "afterimage.h"

struct as_image {
	uint32_t *argb;
	int width;
	int height;
};

static void as_image_destroy(struct as_image *img)
{
	if (img == NULL)
		return;
	free(img->argb);
	*img = (struct as_image){ 0 };
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

struct as_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zwlr_layer_shell_v1 *layer_shell;

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;

	struct wl_callback *frame_cb;

	struct as_buffer *buffer;

	int width;
	int height;
	bool configured;
	bool running;

	struct as_image banner;

	Display *x11_display;
	ASVisual *asv;
};

static volatile sig_atomic_t g_stop = 0;

static void on_alarm(int signum)
{
	(void)signum;
	g_stop = 1;
}

static bool aswl_is_file_readable(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return false;

	struct stat st;
	if (stat(path, &st) != 0)
		return false;
	if (!S_ISREG(st.st_mode))
		return false;
	return access(path, R_OK) == 0;
}

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

static const char *aswl_default_banner_xml(void)
{
	static const char *candidates[] = {
		"~/.afterstep/banner",
		"/usr/share/afterstep/banner",
		"_install/share/afterstep/banner",
		"afterstep/banner",
	};

	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		char *expanded = aswl_expand_tilde(candidates[i]);
		if (expanded == NULL)
			continue;
		bool ok = aswl_is_file_readable(expanded);
		free(expanded);
		if (ok)
			return candidates[i];
	}

	return NULL;
}

static char *aswl_guess_share_root(const char *path)
{
	if (path == NULL)
		return NULL;

	const char *p = strstr(path, "/share/afterstep/");
	if (p != NULL) {
		size_t len = (size_t)(p - path) + strlen("/share/afterstep");
		char *out = malloc(len + 1);
		if (out == NULL)
			return NULL;
		memcpy(out, path, len);
		out[len] = '\0';
		return out;
	}

	if (strncmp(path, "/usr/share/afterstep/", strlen("/usr/share/afterstep/")) == 0)
		return strdup("/usr/share/afterstep");

	if (strncmp(path, "afterstep/", strlen("afterstep/")) == 0)
		return strdup("afterstep");

	return NULL;
}

static char *aswl_resolve_tiles_asset(const char *share_root, const char *src)
{
	if (src == NULL || src[0] == '\0')
		return NULL;
	if (aswl_is_file_readable(src))
		return strdup(src);

	if (strncmp(src, "tiles/", 6) != 0)
		return strdup(src);

	const char *leaf = src + 6;
	char *p = NULL;

	if (share_root != NULL && share_root[0] != '\0') {
		/* installed: desktop/tiles/AfterStepBeauty */
		if (asprintf(&p, "%s/desktop/tiles/%s", share_root, leaf) >= 0) {
			if (aswl_is_file_readable(p))
				return p;
			free(p);
		}

		/* repo-style: desktop/tiles/png/AfterStepBeauty */
		if (asprintf(&p, "%s/desktop/tiles/png/%s", share_root, leaf) >= 0) {
			if (aswl_is_file_readable(p))
				return p;
			free(p);
		}
		if (asprintf(&p, "%s/desktop/tiles/jpg/%s", share_root, leaf) >= 0) {
			if (aswl_is_file_readable(p))
				return p;
			free(p);
		}
	}

	if (asprintf(&p, "afterstep/desktop/tiles/png/%s", leaf) >= 0) {
		if (aswl_is_file_readable(p))
			return p;
		free(p);
	}

	if (asprintf(&p, "_install/share/afterstep/desktop/tiles/%s", leaf) >= 0) {
		if (aswl_is_file_readable(p))
			return p;
		free(p);
	}

	if (asprintf(&p, "/usr/share/afterstep/desktop/tiles/%s", leaf) >= 0) {
		if (aswl_is_file_readable(p))
			return p;
		free(p);
	}

	return strdup(src);
}

static void aswl_afterimage_init(struct as_state *state)
{
	if (state == NULL)
		return;
	if (state->asv != NULL)
		return;

	Display *dpy = XOpenDisplay(NULL);
	state->x11_display = dpy;
	if (dpy != NULL) {
		int screen = DefaultScreen(dpy);
		int depth = DefaultDepth(dpy, screen);
		state->asv = create_asvisual(dpy, screen, depth, NULL);
	} else {
		/* Match `ascompose`: build an offscreen 32bpp visual when no X display is available. */
		state->asv = create_asvisual(NULL, 0, 32, NULL);
	}
}

static bool aswl_render_banner_afterimage(struct as_state *state, struct as_image *img, int width, int height)
{
	if (state == NULL || img == NULL)
		return false;
	if (width <= 0 || height <= 0)
		return false;

	const char *banner_env = getenv("ASWLBANNER_BANNER");
	const char *banner_path_raw =
		(banner_env != NULL && banner_env[0] != '\0') ? banner_env : aswl_default_banner_xml();
	if (banner_path_raw == NULL)
		return false;

	char *banner_path = aswl_expand_tilde(banner_path_raw);
	if (banner_path == NULL)
		return false;

	char *share_root = aswl_guess_share_root(banner_path);
	if (share_root == NULL && aswl_is_file_readable("afterstep/banner"))
		share_root = strdup("afterstep");

	char *beauty = aswl_resolve_tiles_asset(share_root, "tiles/AfterStepBeauty");
	char *logo = aswl_resolve_tiles_asset(share_root, "tiles/AfterStep");

	aswl_afterimage_init(state);
	if (state->asv == NULL)
		goto fail;

	char *fonts_dir = NULL;
	if (share_root != NULL && share_root[0] != '\0') {
		if (asprintf(&fonts_dir, "%s/desktop/fonts", share_root) < 0)
			fonts_dir = NULL;
	}

	struct ASFontManager *fontman = NULL;
	if (fonts_dir != NULL && fonts_dir[0] != '\0')
		fontman = create_generic_fontman(state->x11_display, fonts_dir);

	/*
	 * Wayland equivalent of the X11 Banner module. The X11 version runs
	 * `ascompose -I` on `afterstep/banner`, which is a short fade animation.
	 * For screenshots/parity, render a single stable frame that matches the
	 * X11 reference capture.
	 */
	const char *tint = getenv("ASWLBANNER_TINT");
	if (tint == NULL || tint[0] == '\0')
		tint = "#0e7f7f7f";

	const char *beauty_src = beauty != NULL ? beauty : "tiles/AfterStepBeauty";
	const char *logo_src = logo != NULL ? logo : "tiles/AfterStep";

	char *processed = NULL;
	if (asprintf(&processed,
	             "<tile tint=%s>"
	             "<composite>"
	             "<solid width=%d height=%d color=#00000000/>"
	             "<tile y=30 tint=#7fbf9f7f><img src=\"%s\"/></tile>"
	             "<tile y=0 x=25 tint=#7f7F7F7F><img y=40 src=\"%s\"/></tile>"
	             "<text y=250 x=165 fgcolor=#222222 font=DefaultBoldOblique.ttf size=15>"
	             "v.2 the freedom has arrived"
	             "</text>"
	             "</composite>"
	             "</tile>",
	             tint,
	             width,
	             height,
	             beauty_src,
	             logo_src) < 0) {
		processed = NULL;
		goto fail;
	}

	asxml_var_init();

	ASImage *im = compose_asimage_xml_at_size(state->asv,
	                                          NULL,
	                                          fontman,
	                                          processed,
	                                          ASFLAGS_EVERYTHING,
	                                          0,
	                                          None,
	                                          ".",
	                                          width,
	                                          height);
	if (fontman != NULL)
		destroy_font_manager(fontman, False);

	if (im == NULL)
		goto fail;

	uint32_t *argb = calloc((size_t)im->width * (size_t)im->height, sizeof(*argb));
	if (argb == NULL) {
		safe_asimage_destroy(im);
		goto fail;
	}

	ASImageDecoder *dec = start_image_decoding(state->asv, im, SCL_DO_ALL, 0, 0, im->width, im->height, NULL);
	if (dec == NULL) {
		free(argb);
		safe_asimage_destroy(im);
		goto fail;
	}

	for (unsigned int y = 0; y < im->height; y++) {
		dec->decode_image_scanline(dec);
		for (unsigned int x = 0; x < im->width; x++) {
			uint32_t a = dec->buffer.alpha[x] & 0xFFu;
			uint32_t r = dec->buffer.red[x] & 0xFFu;
			uint32_t g = dec->buffer.green[x] & 0xFFu;
			uint32_t b = dec->buffer.blue[x] & 0xFFu;
			/*
			 * WL_SHM_FORMAT_ARGB8888 requires premultiplied alpha.
			 * libAfterImage provides channels separately; ensure they are
			 * premultiplied before handing them to the compositor.
			 */
			if (a == 0) {
				r = 0;
				g = 0;
				b = 0;
			} else if (a < 255) {
				r = (r * a + 127u) / 255u;
				g = (g * a + 127u) / 255u;
				b = (b * a + 127u) / 255u;
			}
			argb[(size_t)y * (size_t)im->width + (size_t)x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	stop_image_decoding(&dec);

	unsigned int im_width = im->width;
	unsigned int im_height = im->height;
	safe_asimage_destroy(im);

	as_image_destroy(img);
	img->argb = argb;
	img->width = (int)im_width;
	img->height = (int)im_height;

	free(fonts_dir);
	free(processed);
	free(logo);
	free(beauty);
	free(share_root);
	free(banner_path);
	return true;

fail:
	free(fonts_dir);
	free(processed);
	free(logo);
	free(beauty);
	free(share_root);
	free(banner_path);
	return false;
}

static int create_tmpfile(size_t size)
{
	int fd = -1;

#ifdef __linux__
	fd = memfd_create("aswlbanner", MFD_CLOEXEC);
	if (fd >= 0) {
		if (ftruncate(fd, (off_t)size) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}
#endif

	char template[] = "/tmp/aswlbanner-XXXXXX";
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
	                                           WL_SHM_FORMAT_ARGB8888);
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

	if (state->banner.argb == NULL || state->banner.width != buf->width || state->banner.height != buf->height) {
		if (!aswl_render_banner_afterimage(state, &state->banner, buf->width, buf->height))
			as_image_destroy(&state->banner);
	}

	if (state->banner.argb != NULL) {
		uint32_t *dst = buf->data;
		int dst_stride_px = buf->stride / 4;
		for (int y = 0; y < buf->height; y++) {
			memcpy(dst + (size_t)y * (size_t)dst_stride_px,
			       state->banner.argb + (size_t)y * (size_t)buf->width,
			       (size_t)buf->width * 4);
		}
	} else {
		memset(buf->data, 0, buf->size);
	}

	/* 1px black border (matches the X11 Banner window edge). */
	uint32_t *px = buf->data;
	int stride_px = buf->stride / 4;
	if (buf->width >= 2 && buf->height >= 2) {
		/* X11 reference only shows a left + bottom edge (no top/right border). */
		for (int x = 0; x < buf->width; x++)
			px[(buf->height - 1) * stride_px + x] = 0xFF000000u;
		for (int y = 0; y < buf->height; y++)
			px[y * stride_px] = 0xFF000000u;
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
	if (state != NULL && state->frame_cb == cb)
		state->frame_cb = NULL;
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
	if (state != NULL)
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
		uint32_t bind_version = version > 4 ? 4 : version;
		state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, bind_version);
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

	as_image_destroy(&state->banner);

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

static int clamp_int(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static int env_int(const char *name, int def, int lo, int hi)
{
	const char *s = name != NULL ? getenv(name) : NULL;
	if (s != NULL && s[0] != '\0') {
		char *end = NULL;
		long v = strtol(s, &end, 10);
		if (end != s && end != NULL && *end == '\0')
			def = (int)v;
	}
	return clamp_int(def, lo, hi);
}

int main(void)
{
	struct as_state state = {
		.width = env_int("ASWLBANNER_WIDTH", 400, 64, 4096),
		.height = env_int("ASWLBANNER_HEIGHT", 270, 64, 4096),
		.running = true,
	};

	int timeout = env_int("ASWLBANNER_TIMEOUT", 200, 0, 86400);
	if (timeout > 0) {
		struct sigaction sa = { 0 };
		sa.sa_handler = on_alarm;
		sigemptyset(&sa.sa_mask);
		(void)sigaction(SIGALRM, &sa, NULL);
		alarm((unsigned)timeout);
	}

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "aswlbanner: wl_display_connect failed: %s\n", strerror(errno));
		return 1;
	}

	state.registry = wl_display_get_registry(state.display);
	if (state.registry == NULL) {
		fprintf(stderr, "aswlbanner: wl_display_get_registry failed\n");
		cleanup(&state);
		return 1;
	}

	wl_registry_add_listener(state.registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);

	if (state.compositor == NULL || state.shm == NULL || state.layer_shell == NULL) {
		fprintf(stderr, "aswlbanner: missing globals (compositor=%p shm=%p layer_shell=%p)\n",
		        (void *)state.compositor,
		        (void *)state.shm,
		        (void *)state.layer_shell);
		cleanup(&state);
		return 1;
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	if (state.surface == NULL) {
		fprintf(stderr, "aswlbanner: wl_compositor_create_surface failed\n");
		cleanup(&state);
		return 1;
	}

	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell,
	                                                            state.surface,
	                                                            NULL,
	                                                            ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	                                                            "afterstep-aswlbanner");
	if (state.layer_surface == NULL) {
		fprintf(stderr, "aswlbanner: zwlr_layer_shell_v1_get_layer_surface failed\n");
		cleanup(&state);
		return 1;
	}

	zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surface_listener, &state);
	zwlr_layer_surface_v1_set_anchor(state.layer_surface,
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);
	zwlr_layer_surface_v1_set_size(state.layer_surface, (uint32_t)state.width, (uint32_t)state.height);
	zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, 0);

	wl_surface_commit(state.surface);
	wl_display_roundtrip(state.display);

	while (state.running && !g_stop) {
		if (wl_display_dispatch(state.display) < 0) {
			if (errno == EINTR && g_stop)
				break;
			break;
		}
	}

	cleanup(&state);
	return 0;
}
