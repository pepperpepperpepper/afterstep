#ifndef ASWL_COMP_INTERNAL_H
#define ASWL_COMP_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(HAVE_WLROOTS) && HAVE_WLROOTS

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <xkbcommon/xkbcommon.h>

#include <wlr/util/box.h>

#include "aswlfont.h"
#include "aswltheme.h"

typedef struct xcb_connection_t xcb_connection_t;

struct wlr_allocator;
struct wlr_backend;
struct wlr_buffer;
struct wlr_compositor;
struct wlr_cursor;
struct wlr_export_dmabuf_manager_v1;
struct wlr_ext_foreign_toplevel_handle_v1;
struct wlr_ext_foreign_toplevel_list_v1;
struct wlr_foreign_toplevel_handle_v1;
struct wlr_foreign_toplevel_manager_v1;
struct wlr_idle_inhibit_manager_v1;
struct wlr_idle_inhibitor_v1;
struct wlr_idle_notifier_v1;
struct wlr_input_device;
struct wlr_input_method_keyboard_grab_v2;
struct wlr_input_method_manager_v2;
struct wlr_input_method_v2;
struct wlr_input_popup_surface_v2;
struct wlr_keyboard;
struct wlr_keyboard_key_event;
struct wlr_layer_shell_v1;
struct wlr_layer_surface_v1;
struct wlr_linux_dmabuf_v1;
struct wlr_output;
struct wlr_output_state;
struct wlr_output_layout;
struct wlr_output_manager_v1;
struct wlr_pointer_constraint_v1;
struct wlr_pointer_constraints_v1;
struct wlr_primary_selection_v1_device_manager;
struct wlr_relative_pointer_manager_v1;
struct wlr_renderer;
struct wlr_screencopy_manager_v1;
struct wlr_scene;
struct wlr_scene_buffer;
struct wlr_scene_layer_surface_v1;
struct wlr_scene_output;
struct wlr_scene_output_layout;
struct wlr_scene_rect;
struct wlr_scene_tree;
struct wlr_seat;
struct wlr_session_lock_manager_v1;
struct wlr_session_lock_surface_v1;
struct wlr_session_lock_v1;
struct wlr_text_input_manager_v3;
struct wlr_text_input_v3;
struct wlr_virtual_keyboard_manager_v1;
struct wlr_virtual_keyboard_v1;
struct wlr_xcursor_manager;
struct wlr_xdg_activation_token_v1;
struct wlr_xdg_activation_v1;
struct wlr_xdg_decoration_manager_v1;
struct wlr_xdg_surface;
struct wlr_xdg_toplevel_decoration_v1;
struct wlr_xdg_toplevel;
struct wlr_xdg_shell;
struct wlr_xwayland;
struct wlr_xwayland_surface;

enum aswl_cursor_mode {
	ASWL_CURSOR_PASSTHROUGH = 0,
	ASWL_CURSOR_MOVE,
	ASWL_CURSOR_RESIZE,
};

struct aswl_server;

enum aswl_dock_anchor {
	ASWL_DOCK_ANCHOR_BOTTOM_LEFT = 0,
	ASWL_DOCK_ANCHOR_BOTTOM_RIGHT,
	ASWL_DOCK_ANCHOR_TOP_LEFT,
	ASWL_DOCK_ANCHOR_TOP_RIGHT,
};

enum aswl_dock_flow {
	ASWL_DOCK_FLOW_ROW = 0,
	ASWL_DOCK_FLOW_COLUMN,
};

enum aswl_dock_order_mode {
	ASWL_DOCK_ORDER_CREATE = 0,
	ASWL_DOCK_ORDER_TITLE,
	ASWL_DOCK_ORDER_CLASS,
	ASWL_DOCK_ORDER_CONFIG,
};

struct aswl_dock_config {
	enum aswl_dock_anchor anchor;
	enum aswl_dock_flow flow;
	enum aswl_dock_order_mode order;
	int pad;
	int spacing;
	uint16_t max_dim;
	char *rules_path;
	char **rules;
	size_t rule_count;
};

enum {
	ASWL_WINDOW_FLAG_MAPPED = 1u << 0,
	ASWL_WINDOW_FLAG_FOCUSED = 1u << 1,
	ASWL_WINDOW_FLAG_XWAYLAND = 1u << 2,
};

struct aswl_view {
	struct wl_list link; /* aswl_server.views */
	struct aswl_server *server;
	uint32_t id;
	enum {
		ASWL_VIEW_XDG = 0,
		ASWL_VIEW_XWAYLAND,
	} type;
	struct wlr_xdg_surface *xdg_surface;
	struct wlr_xwayland_surface *xwayland_surface;
	struct wlr_ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel;
	struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel;
	bool foreign_toplevel_listeners_added;
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *content_tree;
	struct wlr_scene_tree *surface_tree;
	struct wlr_scene_rect *deco_left;
	struct wlr_scene_rect *deco_right;
	struct wlr_scene_rect *deco_bottom;
	struct wlr_scene_buffer *deco_titlebar;
	struct wlr_buffer *deco_titlebar_buf;
	int deco_border;
	int deco_title_height;
	int deco_close_x;
	int deco_close_y;
	int deco_close_w;
	int deco_close_h;
	bool mapped;
	bool is_dock;
	bool placed;
	bool saved_geometry;
	int saved_x;
	int saved_y;
	int saved_w;
	int saved_h;
	uint32_t workspace;
	bool surface_listeners_added;
	bool commit_listener_added;
	bool scene_destroy_listener_added;
	bool surface_destroy_listener_added;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener surface_destroy;
	struct wl_listener destroy;
	struct wl_listener scene_destroy;

	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_fullscreen;
	struct wl_listener request_maximize;
	struct wl_listener request_minimize;

	struct wl_listener foreign_request_maximize;
	struct wl_listener foreign_request_minimize;
	struct wl_listener foreign_request_activate;
	struct wl_listener foreign_request_fullscreen;
	struct wl_listener foreign_request_close;

	struct wl_listener set_title;
	struct wl_listener set_app_id;
	struct wl_listener set_class;

	struct wl_listener xwayland_associate;
	struct wl_listener xwayland_dissociate;
	struct wl_listener xwayland_map_request;
	struct wl_listener xwayland_request_configure;
};

struct aswl_keyboard {
	struct wl_list link; /* aswl_server.keyboards */
	struct aswl_server *server;
	struct wlr_keyboard *wlr_keyboard;
	bool has_keymap;

	struct wl_listener key;
	struct wl_listener modifiers;
	struct wl_listener destroy;
};

struct aswl_pointer_device {
	struct wl_list link; /* aswl_server.pointer_devices */
	struct aswl_server *server;
	struct wlr_input_device *device;

	struct wl_listener destroy;
};

struct aswl_layer_surface {
	struct wl_list link; /* aswl_server.layer_surfaces */
	struct aswl_server *server;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene;
	bool surface_listeners_added;
	bool surface_destroy_listener_added;

	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener surface_destroy;
};

struct aswl_lock_surface {
	struct wl_list link; /* aswl_server.lock_surfaces */
	struct aswl_server *server;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_scene_tree *scene_tree;
	uint32_t configured_width;
	uint32_t configured_height;
	bool surface_listeners_added;
	bool surface_destroy_listener_added;

	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener surface_destroy;
};

struct aswl_idle_inhibitor {
	struct wl_list link; /* aswl_server.idle_inhibitors */
	struct aswl_server *server;
	struct wlr_idle_inhibitor_v1 *wlr_inhibitor;

	struct wl_listener destroy;
	struct wl_listener surface_map;
	struct wl_listener surface_unmap;
};

struct aswl_text_input {
	struct wl_list link; /* aswl_server.text_inputs */
	struct aswl_server *server;
	struct wlr_text_input_v3 *text_input;
	bool enabled;

	struct wl_listener enable;
	struct wl_listener commit;
	struct wl_listener disable;
	struct wl_listener destroy;
};

struct aswl_input_method {
	struct wl_list link; /* aswl_server.input_methods */
	struct aswl_server *server;
	struct wlr_input_method_v2 *input_method;

	struct wl_listener commit;
	struct wl_listener new_popup_surface;
	struct wl_listener grab_keyboard;
	struct wl_listener destroy;
};

struct aswl_input_popup {
	struct wl_list link; /* aswl_server.input_popups */
	struct aswl_server *server;
	struct wlr_input_popup_surface_v2 *popup_surface;
	struct wlr_scene_tree *scene_tree;

	struct wl_listener destroy;
	struct wl_listener surface_commit;
};

struct aswl_pointer_constraint {
	struct aswl_server *server;
	struct wlr_pointer_constraint_v1 *constraint;

	struct wl_listener destroy;
	struct wl_listener set_region;
};

struct aswl_xdg_deco {
	struct aswl_server *server;
	struct wlr_xdg_toplevel_decoration_v1 *deco;
	struct wl_listener request_mode;
	struct wl_listener destroy;
};

struct aswl_binding {
	struct wl_list link; /* aswl_server.bindings */
	uint32_t mods;
	xkb_keysym_t keysym;
	enum {
		ASWL_BINDING_EXEC = 0,
		ASWL_BINDING_QUIT,
		ASWL_BINDING_CLOSE_FOCUSED,
		ASWL_BINDING_FOCUS_NEXT,
		ASWL_BINDING_FOCUS_PREV,
		ASWL_BINDING_WORKSPACE_SET,
		ASWL_BINDING_WORKSPACE_NEXT,
		ASWL_BINDING_WORKSPACE_PREV,
		ASWL_BINDING_TOGGLE_FULLSCREEN,
		ASWL_BINDING_TOGGLE_MAXIMIZED,
		ASWL_BINDING_LOCK,
	} action;
	uint32_t workspace;
	char *command;
};

struct aswl_output {
	struct wl_list link; /* aswl_server.outputs */
	struct aswl_server *server;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_box full_box;
	struct wlr_box usable_box;
	struct wlr_scene_rect *lock_rect;
	bool lock_frame_presented;

	struct wl_listener destroy;
	struct wl_listener frame;
};

struct aswl_output_persist {
	struct wl_list link; /* aswl_server.outputs_persist */
	char *name;
	bool have_enabled;
	bool enabled;
	bool have_pos;
	int x;
	int y;
	bool have_scale;
	float scale;
	bool have_transform;
	enum wl_output_transform transform;
	bool have_mode;
	bool mode_preferred;
	int mode_width;
	int mode_height;
	int mode_refresh_mhz;
};

struct aswl_control_client {
	struct wl_list link; /* aswl_server.control_clients */
	struct aswl_server *server;
	struct wl_resource *resource;
};

struct aswl_deco_icon {
	uint32_t *argb;
	int w;
	int h;
};

struct aswl_deco_assets {
	bool loaded;
	struct aswl_deco_icon btn_switch;
	struct aswl_deco_icon btn_menu;
	struct aswl_deco_icon btn_pin;
	struct aswl_deco_icon btn_iconize;
	struct aswl_deco_icon btn_shade;
	struct aswl_deco_icon btn_kill;
};

struct aswl_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	xcb_connection_t *x11_keepalive;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_compositor *compositor;
	struct wlr_screencopy_manager_v1 *screencopy_manager;
	struct wlr_export_dmabuf_manager_v1 *export_dmabuf_manager;
	struct wlr_linux_dmabuf_v1 *linux_dmabuf;
	struct wlr_ext_foreign_toplevel_list_v1 *ext_foreign_toplevel_list;
	struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;
	struct wlr_session_lock_manager_v1 *session_lock_manager;
	struct wlr_session_lock_v1 *session_lock;
	bool session_locked;
	bool session_lock_sent_locked;
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit_manager;
	struct wlr_idle_notifier_v1 *idle_notifier;
	struct wl_list idle_inhibitors;
	bool idle_inhibited;
	struct wl_event_source *flush_timer;
	struct wl_event_source *idle_lock_timer;
	int idle_lock_seconds;
	char *lock_command;
	char *state_path;

	struct aswl_theme theme;
	struct aswl_font deco_font;
	struct aswl_font deco_font_inactive;
	struct aswl_deco_assets deco;
	struct aswl_dock_config dock;

	struct wlr_output_layout *output_layout;
	struct wlr_output_manager_v1 *output_manager;
	struct wlr_xdg_output_manager_v1 *xdg_output_manager;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wl_list outputs;
	struct wl_list outputs_persist;
	struct wl_list views;
	struct aswl_view *focused_view;
	int cascade_offset;
	uint32_t current_workspace;
	uint32_t workspace_count;
	uint32_t next_view_id;

	struct wl_list bindings;

	struct wlr_scene_tree *layer_trees[4];
	struct wlr_scene_tree *xdg_tree;
	struct wlr_scene_tree *lock_tree;
	struct wl_list layer_surfaces;
	struct wl_list lock_surfaces;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_xdg_decoration_manager_v1 *xdg_deco_mgr;
	struct wlr_xwayland *xwayland;
	struct wlr_seat *seat;
	struct wlr_text_input_manager_v3 *text_input_manager;
	struct wlr_input_method_manager_v2 *input_method_manager;
	struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_manager;

	struct wl_list text_inputs;
	struct wl_list input_methods;
	struct wl_list input_popups;
	struct wlr_text_input_v3 *ime_active_text_input;
	struct wlr_input_method_v2 *ime_input_method;
	struct wlr_surface *ime_focused_surface;
	struct wlr_box ime_cursor_rect;
	bool ime_cursor_rect_valid;
	struct wlr_scene_tree *ime_popup_tree;
	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wlr_primary_selection_v1_device_manager *primary_selection_manager;
	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
	struct wlr_pointer_constraint_v1 *active_pointer_constraint;
	double pointer_constraint_lx;
	double pointer_constraint_ly;
	bool pointer_constraint_locked;

	struct wlr_xdg_activation_v1 *xdg_activation;
	struct wl_listener xdg_activation_request_activate;
	struct wl_listener xdg_activation_new_token;
	uint32_t last_user_serial;
	uint64_t last_user_time_msec;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;

	struct xkb_context *xkb_context;
	struct wl_list keyboards;
	struct wl_list pointer_devices;

	char *xkb_rules;
	char *xkb_model;
	char *xkb_layout;
	char *xkb_variant;
	char *xkb_options;
	int repeat_rate;
	int repeat_delay;

	bool have_pointer_accel;
	double pointer_accel;
	bool have_tap_to_click;
	bool tap_to_click;

	struct wl_listener new_output;
	struct wl_listener new_input;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_decoration;
	struct wl_listener new_xwayland_surface;
	struct wl_listener new_layer_surface;
	struct wl_listener new_idle_inhibitor;
	struct wl_listener new_session_lock;
	struct wl_listener session_lock_new_surface;
	struct wl_listener session_lock_unlock;
	struct wl_listener session_lock_destroy;
	struct wl_listener new_text_input;
	struct wl_listener new_input_method;
	struct wl_listener new_virtual_keyboard;
	struct wl_listener new_pointer_constraint;

	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;

	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;

	enum aswl_cursor_mode cursor_mode;
	struct aswl_view *grabbed_view;
	double grab_lx;
	double grab_ly;
	int grab_view_lx;
	int grab_view_ly;
	int grab_view_width;
	int grab_view_height;
	uint32_t grab_edges;
	uint32_t grab_button;

	struct wl_global *control_global;
	struct wl_list control_clients;
};

static inline int clamp_int(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

void aswl_argb_to_premul_f(uint32_t argb, float out[static 4]);

const char *view_title(struct aswl_view *view);
const char *view_app_id(struct aswl_view *view);
bool view_is_suite_popup(struct aswl_view *view);
bool view_is_asmodule(struct aswl_view *view);

struct aswl_view *view_from_wlr_surface(struct wlr_surface *surface);

void view_update_toplevel_protocols(struct aswl_view *view);
void view_toplevel_protocols_create(struct aswl_view *view);
void view_toplevel_protocols_destroy(struct aswl_view *view);

void focus_view(struct aswl_view *view, struct wlr_surface *surface);
void focus_topmost_view(struct aswl_server *server);
void close_focused_view(struct aswl_server *server);
void focus_next_view(struct aswl_server *server);
void focus_prev_view(struct aswl_server *server);

uint32_t normalize_workspace(struct aswl_server *server, uint32_t workspace);
void set_workspace(struct aswl_server *server, uint32_t workspace);
void workspace_next(struct aswl_server *server);
void workspace_prev(struct aswl_server *server);

bool view_is_fullscreen(struct aswl_view *view);
bool view_is_maximized(struct aswl_view *view);
void view_set_fullscreen(struct aswl_view *view, bool fullscreen);
void view_set_maximized(struct aswl_view *view, bool maximized);

void view_maybe_mark_dock(struct aswl_view *view);
void arrange_dock_views(struct aswl_server *server);

void place_view(struct aswl_view *view);
void begin_interactive(struct aswl_view *view, enum aswl_cursor_mode mode, uint32_t edges, uint32_t button);
void end_interactive(struct aswl_server *server);

void spawn_command(const char *command);
void spawn_command_with_activation(struct aswl_server *server, const char *command);
void spawn_lock(struct aswl_server *server);

void aswl_apply_keyboard_device_config(struct aswl_server *server, struct aswl_keyboard *keyboard);
void aswl_apply_pointer_device_config(struct aswl_server *server, struct wlr_input_device *device);
const char *binding_action_name(int action);

bool default_autostart_path(char *out, size_t out_size);
void load_config_file(struct aswl_server *server, const char *path, bool log_missing);

int aswl_server_init(struct aswl_server *server, const char *socket_name);
void aswl_server_finish(struct aswl_server *server);

int aswl_idle_lock_timer_cb(void *data);
void aswl_idle_lock_note_activity(struct aswl_server *server);
void aswl_idle_inhibit_refresh(struct aswl_server *server);
void handle_new_idle_inhibitor(struct wl_listener *listener, void *data);
void idle_inhibitors_destroy(struct aswl_server *server);

void aswl_pointer_constraints_update(struct aswl_server *server, struct wlr_surface *surface);

void aswl_schedule_flush(struct aswl_server *server);

void view_get_current_size(struct aswl_view *view, int *width, int *height);
void view_get_deco_metrics(struct aswl_view *view, int *border_out, int *title_h_out);
void view_get_content_offset(struct aswl_view *view, int *ox, int *oy);
void view_get_frame_size(struct aswl_view *view, int *width, int *height);
bool view_create_frame_scene(struct aswl_view *view);
void view_update_decorations(struct aswl_view *view);

char *aswl_state_path_resolve(const char *override);
void aswl_state_load(struct aswl_server *server, bool allow_workspace_count_override);
void aswl_state_save(struct aswl_server *server);
void aswl_outputs_persist_destroy(struct aswl_server *server);
bool aswl_output_persist_apply(struct aswl_server *server,
                               struct wlr_output *output,
                               struct wlr_output_state *state,
                               int *out_x,
                               int *out_y,
                               bool *out_have_pos);

void broadcast_output_state(struct aswl_server *server);
void broadcast_workspace_state(struct aswl_server *server);
void broadcast_window_state(struct aswl_server *server, struct aswl_view *view);
void broadcast_window_closed(struct aswl_server *server, uint32_t id);

void aswl_control_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id);

void arrange_lock_surfaces(struct aswl_server *server);
void session_lock_detach(struct aswl_server *server);
void lock_surfaces_destroy(struct aswl_server *server);
void handle_new_session_lock(struct wl_listener *listener, void *data);

void handle_output_manager_apply(struct wl_listener *listener, void *data);
void handle_output_manager_test(struct wl_listener *listener, void *data);
void handle_new_output(struct wl_listener *listener, void *data);

void handle_new_input(struct wl_listener *listener, void *data);
void handle_new_virtual_keyboard(struct wl_listener *listener, void *data);
void handle_new_pointer_constraint(struct wl_listener *listener, void *data);
void handle_request_cursor(struct wl_listener *listener, void *data);
void handle_request_set_selection(struct wl_listener *listener, void *data);
void handle_request_set_primary_selection(struct wl_listener *listener, void *data);
void handle_cursor_motion(struct wl_listener *listener, void *data);
void handle_cursor_motion_absolute(struct wl_listener *listener, void *data);
void handle_cursor_button(struct wl_listener *listener, void *data);
void handle_cursor_axis(struct wl_listener *listener, void *data);
void handle_cursor_frame(struct wl_listener *listener, void *data);

void arrange_layers(struct aswl_server *server);
void aswl_layer_arrange(struct aswl_server *server);
void apply_layer_struts_top(struct aswl_server *server, struct wlr_output *output, struct wlr_box *usable);
void handle_new_layer_surface(struct wl_listener *listener, void *data);

void handle_new_xdg_toplevel(struct wl_listener *listener, void *data);
void handle_new_xwayland_surface(struct wl_listener *listener, void *data);

void aswl_deco_assets_destroy(struct aswl_deco_assets *deco);
void handle_new_xdg_decoration(struct wl_listener *listener, void *data);

void aswl_ime_init(struct aswl_server *server);
void aswl_ime_set_focus(struct aswl_server *server, struct wlr_surface *surface);
void aswl_ime_notify_key(struct aswl_server *server, struct wlr_keyboard_key_event *event);
void aswl_ime_notify_modifiers(struct aswl_server *server, struct wlr_keyboard *keyboard);
void aswl_ime_maybe_set_keyboard_grab(struct aswl_server *server, struct wlr_keyboard *keyboard);

#endif /* defined(HAVE_WLROOTS) && HAVE_WLROOTS */

#endif /* ASWL_COMP_INTERNAL_H */
