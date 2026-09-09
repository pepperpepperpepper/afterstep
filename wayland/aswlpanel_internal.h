#ifndef ASWLPANEL_INTERNAL_H
#define ASWLPANEL_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#include "afterstep-control-v1-client-protocol.h"
#if HAVE_WLR_LAYER_SHELL
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#endif

#include "aswltheme.h"
#include "aswlfont.h"

/* Avoid pulling in linux headers just for BTN_LEFT. */
#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif
#ifndef BTN_RIGHT
#define BTN_RIGHT 0x111
#endif
#ifndef BTN_MIDDLE
#define BTN_MIDDLE 0x112
#endif

enum as_panel_edge {
	ASWL_PANEL_EDGE_TOP = 0,
	ASWL_PANEL_EDGE_BOTTOM,
	ASWL_PANEL_EDGE_LEFT,
	ASWL_PANEL_EDGE_RIGHT,
};

enum {
	ASWL_WINDOW_FLAG_MAPPED = 1u << 0,
	ASWL_WINDOW_FLAG_FOCUSED = 1u << 1,
	ASWL_WINDOW_FLAG_XWAYLAND = 1u << 2,
	ASWL_WINDOW_FLAG_MINIMIZED = 1u << 3,
};

enum {
	ASWL_ANCHOR_TOP = 1u << 0,
	ASWL_ANCHOR_BOTTOM = 1u << 1,
	ASWL_ANCHOR_LEFT = 1u << 2,
	ASWL_ANCHOR_RIGHT = 1u << 3,
};

struct as_margins {
	int top;
	int right;
	int bottom;
	int left;
};

struct as_button {
	char *label;
	char *command;
	char *icon_path;
	uint32_t *icon_argb;
	int icon_w;
	int icon_h;
};

struct as_window {
	uint32_t id;
	uint32_t workspace;
	uint32_t flags;
	int x;
	int y;
	int w;
	int h;
	char *title;
	char *app_id;
};

struct aswl_bg_snapshot_header {
	char magic[8]; /* "ASWLBG1\0" */
	uint32_t width;
	uint32_t height;
	uint32_t stride; /* bytes per row */
	uint32_t format; /* reserved; currently 0 = ARGB8888 */
};

struct as_bg_snapshot {
	void *map;
	size_t size;
	uint32_t *argb; /* points into map, immediately after header */
	int width;
	int height;
	int stride;
	char *path;
};

struct as_buffer {
	struct wl_buffer *wl_buffer;
	void *data;
	int width;
	int height;
	int stride;
	size_t size;
	bool busy;
	struct as_state *state;
};

struct as_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;

	struct xdg_wm_base *xdg_wm_base;
	struct afterstep_control_v1 *control;
	uint32_t control_version;
	uint32_t current_workspace;
	uint32_t workspace_count;
	int output_width;
	int output_height;

#if HAVE_WLR_LAYER_SHELL
	struct zwlr_layer_shell_v1 *layer_shell;
#endif

	struct wl_surface *surface;
	struct wl_callback *frame_cb;

	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

#if HAVE_WLR_LAYER_SHELL
	struct zwlr_layer_surface_v1 *layer_surface;
#endif

	struct as_buffer *buffers[2];

	int width;
	int height;
	int item_height;
	bool width_override_set;
	bool height_override_set;
	bool item_height_override_set;
	bool exclusive_zone_override_set;
	int exclusive_zone_override;
	bool configured;
	bool running;

	bool needs_redraw;
	int pointer_x;
	int pointer_y;
	bool pointer_in_surface;
	int hover_index;
	int pressed_index;
	uint32_t pressed_button;

	struct as_button *buttons;
	size_t button_count;
	bool buttons_owned;
	char *buttons_config_path;
	bool buttons_has_workspaces_directive;
	bool dock_mode;
	bool pager_mode;
	bool window_list_focused_only;
	bool window_list_topmost_only;
	enum as_panel_edge edge;
	struct as_margins margins;
	bool anchor_override_set;
	uint32_t anchor_override;
	int pager_columns;
	int pager_rows;

	struct as_window *windows;
	size_t window_count;
	size_t window_cap;
	bool window_list_in_progress;

	struct as_bg_snapshot bg_snapshot;
	struct aswl_theme theme;
	struct aswl_font font;
};

struct as_layout {
	int pad;
	int spacing;
	int cross;
	int row;
	int dock_gutter;
	int icon_pad;
	int icon_size;
	int text_gap;
	int text_scale;
	int icon_scale;
	int max_main;
	int text_h;
	bool vertical;
};

void as_state_destroy_buffers(struct as_state *state);
bool as_state_ensure_buffers(struct as_state *state);
struct as_buffer *as_state_acquire_buffer(struct as_state *state);

uint32_t as_premul_argb(uint32_t argb);
void as_bg_snapshot_destroy(struct as_bg_snapshot *snap);
bool as_state_ensure_bg_snapshot(struct as_state *state);

void as_buffer_paint_vertical_gradient(struct as_buffer *buf, uint32_t top_argb, uint32_t bottom_argb);
void as_buffer_fill_style_rect(struct as_buffer *buf,
                               int x,
                               int y,
                               int w,
                               int h,
                               const struct aswl_gradient *grad,
                               uint32_t base_argb,
                               uint8_t nudge);
void as_buffer_fill_rect(struct as_buffer *buf, int x, int y, int w, int h, uint32_t argb);
bool as_buffer_fill_backpixmap_tint(struct as_buffer *buf,
                                   const struct as_bg_snapshot *snap,
                                   int src_x,
                                   int src_y,
                                   uint32_t tint);
void as_buffer_draw_bevel_rect(struct as_buffer *buf, int x, int y, int w, int h, uint32_t base_argb, bool sunken);
void as_buffer_draw_image_bilinear(struct as_buffer *buf,
                                   int dx,
                                   int dy,
                                   int dw,
                                   int dh,
                                   const uint32_t *src_argb,
                                   int sw,
                                   int sh);

#if HAVE_AFTERIMAGE
void as_gradient_cache_destroy(void);
#endif

uint32_t as_state_anchor_flags(const struct as_state *state);
bool as_state_compute_surface_origin_for_output(const struct as_state *state, int output_w, int output_h, int *x_out, int *y_out);
bool as_state_compute_surface_origin(const struct as_state *state, int *x_out, int *y_out);
int as_state_exclusive_zone(const struct as_state *state);
int as_state_calc_dock_main_axis_size(const struct as_state *state);

void as_state_free_buttons(struct as_state *state);
bool as_state_load_buttons_from_file(struct as_state *state, const char *path);
void as_state_load_buttons(struct as_state *state);

void as_state_clear_windows(struct as_state *state);
void as_state_destroy_windows(struct as_state *state);
bool as_state_upsert_window(struct as_state *state,
                           uint32_t id,
                           uint32_t workspace,
                           uint32_t flags,
                           const char *title,
                           const char *app_id);
bool as_state_upsert_window_geometry(struct as_state *state, uint32_t id, int x, int y, int w, int h);
bool as_state_remove_window(struct as_state *state, uint32_t id);

bool as_state_get_layout(struct as_state *state, struct as_layout *layout);
bool as_command_parse_workspace_target(const char *command, uint32_t *workspace_out);
int as_state_button_main_size(struct as_state *state, const struct as_layout *layout, size_t idx);
const char *as_window_label(const struct as_window *win);
int as_state_window_main_size(struct as_state *state, const struct as_layout *layout, const struct as_window *win);
struct as_window *as_state_visible_window_nth(struct as_state *state, size_t n);
int as_state_hit_test(struct as_state *state, int x, int y);

void as_state_launch_command(struct as_state *state, const char *command);

void schedule_redraw(struct as_state *state);
void draw_and_commit(struct as_state *state);

bool aswlpanel_wl_init(struct as_state *state);
void aswlpanel_cleanup(struct as_state *state);

#endif
