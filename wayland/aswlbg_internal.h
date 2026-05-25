#pragma once

#include <stdbool.h>
#include <stdint.h>

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_shm;
struct zwlr_layer_shell_v1;

struct wl_surface;
struct zwlr_layer_surface_v1;
struct wl_callback;

struct as_buffer;

struct as_image {
	uint32_t *argb;
	int width;
	int height;
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

	struct as_image background;

	struct _XDisplay *x11_display;
	struct ASVisual *asv;
};

bool aswlbg_render_background_afterimage(struct as_state *state, struct as_image *img, int width, int height);
void aswlbg_image_destroy(struct as_image *img);

int aswlbg_run(struct as_state *state);

