#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(HAVE_WLROOTS) && HAVE_WLROOTS

#include <wayland-server-core.h>

#include <linux/input-event-codes.h>

#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
	#include <wlr/types/wlr_buffer.h>
	#include <wlr/types/wlr_xdg_shell.h>
	#include <wlr/types/wlr_xdg_decoration_v1.h>
	#include <wlr/util/log.h>

#include <wlr/xwayland.h>

#include <wlr/interfaces/wlr_buffer.h>

#include <wlr/util/edges.h>

#include <xkbcommon/xkbcommon.h>

#include "afterstep-control-v1-protocol.h"
#include "aswlfont.h"
#include "aswlicon.h"
#include "aswltheme.h"

#include <drm_fourcc.h>

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

struct aswl_pixbuf_buffer {
	struct wlr_buffer base;
	uint32_t *argb;
	size_t stride;
	uint32_t format;
};

static void arrange_layers(struct aswl_server *server);
static void focus_topmost_view(struct aswl_server *server);
static void focus_view(struct aswl_view *view, struct wlr_surface *surface);
static struct aswl_view *view_from_wlr_surface(struct wlr_surface *surface);
static void place_view(struct aswl_view *view);
static void arrange_dock_views(struct aswl_server *server);
static void view_update_decorations(struct aswl_view *view);
static void view_get_frame_size(struct aswl_view *view, int *width, int *height);
static void view_get_content_offset(struct aswl_view *view, int *ox, int *oy);
static void begin_interactive(struct aswl_view *view, enum aswl_cursor_mode mode, uint32_t edges, uint32_t button);
static void end_interactive(struct aswl_server *server);
static void close_focused_view(struct aswl_server *server);
static void focus_next_view(struct aswl_server *server);
static void focus_prev_view(struct aswl_server *server);
static bool view_is_fullscreen(struct aswl_view *view);
static bool view_is_maximized(struct aswl_view *view);
static void view_set_fullscreen(struct aswl_view *view, bool fullscreen);
static void view_set_maximized(struct aswl_view *view, bool maximized);
static void view_update_toplevel_protocols(struct aswl_view *view);
static void view_toplevel_protocols_create(struct aswl_view *view);
static void view_toplevel_protocols_destroy(struct aswl_view *view);
static int clamp_int(int v, int lo, int hi);
static bool str_ieq(const char *a, const char *b);
static bool str_contains_case_insensitive(const char *haystack, const char *needle);
static char *lstrip(char *s);
static void rstrip_inplace(char *s);
static uint32_t normalize_workspace(struct aswl_server *server, uint32_t workspace);
static void set_workspace(struct aswl_server *server, uint32_t workspace);
static void workspace_next(struct aswl_server *server);
static void workspace_prev(struct aswl_server *server);
static void broadcast_workspace_state(struct aswl_server *server);
static void broadcast_output_state(struct aswl_server *server);
static void broadcast_window_state(struct aswl_server *server, struct aswl_view *view);
static void broadcast_window_closed(struct aswl_server *server, uint32_t id);
static void aswl_state_save(struct aswl_server *server);
	static struct aswl_output_persist *aswl_output_persist_find(struct aswl_server *server, const char *name);
	static struct aswl_output_persist *aswl_output_persist_get(struct aswl_server *server, const char *name);
	static void aswl_outputs_persist_destroy(struct aswl_server *server);
	static void aswl_outputs_persist_sync_from_live(struct aswl_server *server);
	static bool aswl_output_persist_apply(struct aswl_server *server,
	                                     struct wlr_output *output,
	                                     struct wlr_output_state *state,
	                                     int *out_x,
	                                     int *out_y,
	                                     bool *out_have_pos);
	static void handle_view_surface_destroy(struct wl_listener *listener, void *data);
	static void xwayland_detach_surface(struct aswl_view *view);
	static void aswl_dock_config_init(struct aswl_dock_config *dock);
	static void aswl_dock_config_destroy(struct aswl_dock_config *dock);
	static void handle_new_xdg_decoration(struct wl_listener *listener, void *data);
	static void handle_keyboard_key(struct wl_listener *listener, void *data);
	static void handle_keyboard_modifiers(struct wl_listener *listener, void *data);
	static void handle_keyboard_destroy(struct wl_listener *listener, void *data);
	static void aswl_apply_keyboard_device_config(struct aswl_server *server, struct aswl_keyboard *keyboard);
	static void aswl_apply_keyboard_config(struct aswl_server *server);
	static bool aswl_parse_bool(const char *s, bool *out);
	static void aswl_apply_pointer_device_config(struct aswl_server *server, struct wlr_input_device *device);
	static void aswl_apply_pointer_config(struct aswl_server *server);
	static void handle_pointer_device_destroy(struct wl_listener *listener, void *data);
	static void aswl_set_opt_string(char **dst, const char *value);
	static void aswl_ime_set_focus(struct aswl_server *server, struct wlr_surface *surface);
	static void aswl_ime_update(struct aswl_server *server);
	static void aswl_ime_notify_key(struct aswl_server *server, struct wlr_keyboard_key_event *event);
	static void aswl_ime_notify_modifiers(struct aswl_server *server, struct wlr_keyboard *keyboard);
	static void aswl_ime_maybe_set_keyboard_grab(struct aswl_server *server, struct wlr_keyboard *keyboard);
static void handle_new_text_input(struct wl_listener *listener, void *data);
	static void handle_new_input_method(struct wl_listener *listener, void *data);
	static void handle_new_virtual_keyboard(struct wl_listener *listener, void *data);
	static void aswl_pointer_constraints_update(struct aswl_server *server, struct wlr_surface *surface);
	static void handle_new_pointer_constraint(struct wl_listener *listener, void *data);
	static void handle_xdg_activation_request_activate(struct wl_listener *listener, void *data);
	static void handle_xdg_activation_new_token(struct wl_listener *listener, void *data);
	static uint64_t aswl_now_msec(void);

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--socket NAME] [--autostart PATH] [--spawn CMD]... [--state PATH]\n", prog);
	fprintf(stderr, "  --socket NAME  Use a fixed WAYLAND_DISPLAY socket name\n");
	fprintf(stderr, "  --autostart PATH\n");
	fprintf(stderr, "                Spawn commands from a file.\n");
	fprintf(stderr, "                Lines are: 'exec CMD' / 'CMD' / 'bind MODS+KEY exec CMD' / 'bind MODS+KEY ACTION'\n");
	fprintf(stderr, "                           'set KEY VALUE' (xkb_layout/xkb_variant/xkb_options/xkb_model/xkb_rules,\n");
	fprintf(stderr, "                                           repeat_rate/repeat_delay, pointer_accel(-1..1), tap_to_click)\n");
	fprintf(stderr, "                ACTION is: quit|close_focused|focus_next|focus_prev|workspace N|workspace_next|workspace_prev\n");
	fprintf(stderr, "                           toggle_fullscreen|toggle_maximized|lock\n");
	fprintf(stderr, "                Default: $XDG_CONFIG_HOME/afterstep/aswlcomp.autostart\n");
	fprintf(stderr, "                         or ~/.config/afterstep/aswlcomp.autostart\n");
	fprintf(stderr, "  --spawn CMD    Spawn a client command after startup (may be repeated)\n");
	fprintf(stderr, "                (CMD runs via /bin/sh -c with WAYLAND_DISPLAY set)\n");
	fprintf(stderr, "  --state PATH   Persist basic session state (default: $XDG_STATE_HOME/afterstep/aswlcomp.state)\n");
}

static void aswl_argb_to_premul_f(uint32_t argb, float out[static 4])
{
	float a = ((argb >> 24) & 0xFFu) / 255.0f;
	float r = ((argb >> 16) & 0xFFu) / 255.0f;
	float g = ((argb >> 8) & 0xFFu) / 255.0f;
	float b = (argb & 0xFFu) / 255.0f;

	out[0] = r * a;
	out[1] = g * a;
	out[2] = b * a;
	out[3] = a;
}

static void aswl_pixbuf_buffer_destroy(struct wlr_buffer *wlr_buffer)
{
	struct aswl_pixbuf_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	wlr_buffer_finish(&buf->base);
	free(buf->argb);
	free(buf);
}

static bool aswl_pixbuf_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
                                                     uint32_t flags,
                                                     void **data,
                                                     uint32_t *format,
                                                     size_t *stride)
{
	(void)flags;
	struct aswl_pixbuf_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	if (data != NULL)
		*data = buf->argb;
	if (format != NULL)
		*format = buf->format;
	if (stride != NULL)
		*stride = buf->stride;
	return true;
}

static void aswl_pixbuf_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer)
{
	(void)wlr_buffer;
}

static const struct wlr_buffer_impl aswl_pixbuf_buffer_impl = {
	.destroy = aswl_pixbuf_buffer_destroy,
	.begin_data_ptr_access = aswl_pixbuf_buffer_begin_data_ptr_access,
	.end_data_ptr_access = aswl_pixbuf_buffer_end_data_ptr_access,
};

static struct wlr_buffer *aswl_pixbuf_buffer_create(uint32_t *argb, int width, int height)
{
	if (argb == NULL || width <= 0 || height <= 0)
		return NULL;

	struct aswl_pixbuf_buffer *buf = calloc(1, sizeof(*buf));
	if (buf == NULL) {
		free(argb);
		return NULL;
	}

	buf->argb = argb;
	buf->stride = (size_t)width * 4u;
	buf->format = DRM_FORMAT_ARGB8888;

	wlr_buffer_init(&buf->base, &aswl_pixbuf_buffer_impl, width, height);
	return &buf->base;
}

static uint32_t aswl_premul_argb(uint32_t argb)
{
	uint32_t a = (argb >> 24) & 0xFFu;
	if (a == 0)
		return 0;
	if (a == 255u)
		return argb;

	uint32_t r = (argb >> 16) & 0xFFu;
	uint32_t g = (argb >> 8) & 0xFFu;
	uint32_t b = argb & 0xFFu;

	r = (r * a + 127u) / 255u;
	g = (g * a + 127u) / 255u;
	b = (b * a + 127u) / 255u;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static uint32_t aswl_unpremul_argb(uint32_t argb)
{
	uint32_t a = (argb >> 24) & 0xFFu;
	if (a == 0)
		return 0;
	if (a == 255u)
		return argb;

	uint32_t r = (argb >> 16) & 0xFFu;
	uint32_t g = (argb >> 8) & 0xFFu;
	uint32_t b = argb & 0xFFu;

	r = (r * 255u + a / 2u) / a;
	g = (g * 255u + a / 2u) / a;
	b = (b * 255u + a / 2u) / a;

	if (r > 255u)
		r = 255u;
	if (g > 255u)
		g = 255u;
	if (b > 255u)
		b = 255u;

	return (a << 24) | (r << 16) | (g << 8) | b;
}

static void aswl_blend_pixel_argb(uint32_t *dst, uint32_t src_argb)
{
	uint32_t sa = (src_argb >> 24) & 0xFFu;
	if (sa == 0)
		return;

	uint32_t src = aswl_premul_argb(src_argb);
	if (sa == 255u) {
		*dst = src;
		return;
	}

	uint32_t dst_argb = *dst;
	uint32_t da = (dst_argb >> 24) & 0xFFu;
	uint32_t inv = 255u - sa;

	uint32_t out_a = sa + (da * inv + 127u) / 255u;

	uint32_t dr = (dst_argb >> 16) & 0xFFu;
	uint32_t dg = (dst_argb >> 8) & 0xFFu;
	uint32_t db = dst_argb & 0xFFu;

	uint32_t sr = (src >> 16) & 0xFFu;
	uint32_t sg = (src >> 8) & 0xFFu;
	uint32_t sb = src & 0xFFu;

	uint32_t out_r = sr + (dr * inv + 127u) / 255u;
	uint32_t out_g = sg + (dg * inv + 127u) / 255u;
	uint32_t out_b = sb + (db * inv + 127u) / 255u;

	*dst = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

static uint32_t aswl_sample_image_bilinear_unpremul(const uint32_t *src_argb,
                                                    int sw,
                                                    int sh,
                                                    double gx,
                                                    double gy)
{
	if (src_argb == NULL || sw <= 0 || sh <= 0)
		return 0;

	if (gx < 0.0)
		gx = 0.0;
	if (gy < 0.0)
		gy = 0.0;

	double max_x = (double)(sw - 1);
	double max_y = (double)(sh - 1);
	if (gx > max_x)
		gx = max_x;
	if (gy > max_y)
		gy = max_y;

	int x0 = (int)gx;
	int y0 = (int)gy;
	int x1 = x0 + 1;
	int y1 = y0 + 1;
	if (x1 >= sw)
		x1 = sw - 1;
	if (y1 >= sh)
		y1 = sh - 1;

	double tx = gx - (double)x0;
	double ty = gy - (double)y0;
	if (tx < 0.0)
		tx = 0.0;
	if (ty < 0.0)
		ty = 0.0;
	if (tx > 1.0)
		tx = 1.0;
	if (ty > 1.0)
		ty = 1.0;

	uint32_t p00 = aswl_premul_argb(src_argb[(size_t)y0 * (size_t)sw + (size_t)x0]);
	uint32_t p10 = aswl_premul_argb(src_argb[(size_t)y0 * (size_t)sw + (size_t)x1]);
	uint32_t p01 = aswl_premul_argb(src_argb[(size_t)y1 * (size_t)sw + (size_t)x0]);
	uint32_t p11 = aswl_premul_argb(src_argb[(size_t)y1 * (size_t)sw + (size_t)x1]);

	double w00 = (1.0 - tx) * (1.0 - ty);
	double w10 = tx * (1.0 - ty);
	double w01 = (1.0 - tx) * ty;
	double w11 = tx * ty;

	double a = (double)((p00 >> 24) & 0xFFu) * w00 +
	           (double)((p10 >> 24) & 0xFFu) * w10 +
	           (double)((p01 >> 24) & 0xFFu) * w01 +
	           (double)((p11 >> 24) & 0xFFu) * w11;
	double r = (double)((p00 >> 16) & 0xFFu) * w00 +
	           (double)((p10 >> 16) & 0xFFu) * w10 +
	           (double)((p01 >> 16) & 0xFFu) * w01 +
	           (double)((p11 >> 16) & 0xFFu) * w11;
	double g = (double)((p00 >> 8) & 0xFFu) * w00 +
	           (double)((p10 >> 8) & 0xFFu) * w10 +
	           (double)((p01 >> 8) & 0xFFu) * w01 +
	           (double)((p11 >> 8) & 0xFFu) * w11;
	double b = (double)(p00 & 0xFFu) * w00 +
	           (double)(p10 & 0xFFu) * w10 +
	           (double)(p01 & 0xFFu) * w01 +
	           (double)(p11 & 0xFFu) * w11;

	uint32_t ia = (uint32_t)(a + 0.5);
	uint32_t ir = (uint32_t)(r + 0.5);
	uint32_t ig = (uint32_t)(g + 0.5);
	uint32_t ib = (uint32_t)(b + 0.5);
	if (ia > 255u)
		ia = 255u;
	if (ir > 255u)
		ir = 255u;
	if (ig > 255u)
		ig = 255u;
	if (ib > 255u)
		ib = 255u;

	uint32_t premul = (ia << 24) | (ir << 16) | (ig << 8) | ib;
	return aswl_unpremul_argb(premul);
}

static void aswl_blend_image_bilinear_argb(uint32_t *dst,
                                           int dst_w,
                                           int dst_h,
                                           int dx,
                                           int dy,
                                           int dw,
                                           int dh,
                                           const uint32_t *src,
                                           int sw,
                                           int sh)
{
	if (dst == NULL || src == NULL)
		return;
	if (dst_w <= 0 || dst_h <= 0 || sw <= 0 || sh <= 0)
		return;
	if (dw <= 0 || dh <= 0)
		return;

	int x0 = 0;
	int y0 = 0;
	int x1 = dw;
	int y1 = dh;
	if (dx < 0)
		x0 = -dx;
	if (dy < 0)
		y0 = -dy;
	if (dx + dw > dst_w)
		x1 = dst_w - dx;
	if (dy + dh > dst_h)
		y1 = dst_h - dy;
	if (x1 <= x0 || y1 <= y0)
		return;

	double sx_scale = 0.0;
	double sy_scale = 0.0;
	if (dw > 1 && sw > 1)
		sx_scale = (double)(sw - 1) / (double)(dw - 1);
	if (dh > 1 && sh > 1)
		sy_scale = (double)(sh - 1) / (double)(dh - 1);

	for (int y = y0; y < y1; y++) {
		double gy = (double)y * sy_scale;
		for (int x = x0; x < x1; x++) {
			double gx = (double)x * sx_scale;
			uint32_t c = aswl_sample_image_bilinear_unpremul(src, sw, sh, gx, gy);
			aswl_blend_pixel_argb(&dst[(size_t)(dy + y) * (size_t)dst_w + (size_t)(dx + x)], c);
		}
	}
}

static double aswl_gradient_t(int type, int x, int y, int w, int h)
{
	if (w <= 1)
		w = 1;
	if (h <= 1)
		h = 1;

	switch (type) {
	case 1:
		type = 6;
		break;
	case 2:
		type = 8;
		break;
	case 4:
		type = 9;
		break;
	default:
		break;
	}

	double fx = (double)x;
	double fy = (double)y;
	double fw = (double)(w - 1);
	double fh = (double)(h - 1);

	switch (type) {
	case 6:
	{
		double denom = fw + fh;
		if (denom <= 0.0)
			return 0.0;
		return (fx + fy) / denom;
	}
	case 7:
	{
		double denom = fw + fh;
		if (denom <= 0.0)
			return 0.0;
		return (fx + (fh - fy)) / denom;
	}
	case 8:
		if (fh <= 0.0)
			return 0.0;
		return fy / fh;
	case 9:
		if (fw <= 0.0)
			return 0.0;
		return fx / fw;
	case 3:
	{
		double mid = fh / 2.0;
		if (mid <= 0.0)
			return 0.0;
		double d = (fy > mid) ? (fy - mid) : (mid - fy);
		double t = 1.0 - (d / mid);
		return t < 0.0 ? 0.0 : t;
	}
	case 5:
	{
		double mid = fw / 2.0;
		if (mid <= 0.0)
			return 0.0;
		double d = (fx > mid) ? (fx - mid) : (mid - fx);
		double t = 1.0 - (d / mid);
		return t < 0.0 ? 0.0 : t;
	}
	default:
		return 0.0;
	}
}

static uint32_t aswl_gradient_sample(const struct aswl_gradient *grad, double t)
{
	if (!aswl_gradient_is_valid(grad))
		return 0;

	if (t <= grad->offsets[0])
		return grad->colors[0];
	if (t >= grad->offsets[grad->count - 1])
		return grad->colors[grad->count - 1];

	for (size_t i = 0; i + 1 < grad->count; i++) {
		double a = grad->offsets[i];
		double b = grad->offsets[i + 1];
		if (t > b)
			continue;

		double span = b - a;
		if (span <= 0.0)
			return grad->colors[i + 1];

		double local = (t - a) / span;
		if (local < 0.0)
			local = 0.0;
		if (local > 1.0)
			local = 1.0;

		uint8_t tt = (uint8_t)(local * 255.0 + 0.5);
		return aswl_color_blend(grad->colors[i], grad->colors[i + 1], tt);
	}

	return grad->colors[grad->count - 1];
}

#define ASWL_ACTIVATION_MAX_AGE_MSEC 10000u

static uint64_t aswl_now_msec(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static bool aswl_user_event_is_recent(struct aswl_server *server, uint64_t now_msec)
{
	if (server == NULL)
		return false;
	if (server->last_user_serial == 0 || server->last_user_time_msec == 0)
		return false;
	if (now_msec < server->last_user_time_msec)
		return false;
	return (now_msec - server->last_user_time_msec) <= ASWL_ACTIVATION_MAX_AGE_MSEC;
}

static bool aswl_surface_is_focused_for_activation(struct aswl_server *server, struct wlr_surface *surface)
{
	if (server == NULL || surface == NULL || server->seat == NULL)
		return false;
	if (server->seat->pointer_state.focused_surface == surface)
		return true;
	if (server->seat->keyboard_state.focused_surface == surface)
		return true;

	struct aswl_view *v = view_from_wlr_surface(surface);
	if (v != NULL && v == server->focused_view)
		return true;

	return false;
}

static struct wlr_xdg_activation_token_v1 *aswl_activation_token_for_spawn(struct aswl_server *server)
{
	if (server == NULL || server->xdg_activation == NULL || server->seat == NULL)
		return NULL;

	uint64_t now_msec = aswl_now_msec();
	if (!aswl_user_event_is_recent(server, now_msec))
		return NULL;

	struct wlr_surface *source = server->seat->pointer_state.focused_surface;
	if (source == NULL)
		source = server->seat->keyboard_state.focused_surface;

	struct wlr_xdg_activation_token_v1 *token = wlr_xdg_activation_token_v1_create(server->xdg_activation);
	if (token == NULL)
		return NULL;

	token->seat = server->seat;
	token->serial = server->last_user_serial;
	token->surface = source;
	token->data = (void *)1;

	return token;
}

static void spawn_command(const char *command)
{
	if (command == NULL || command[0] == '\0')
		return;

	pid_t pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		unsetenv("XDG_ACTIVATION_TOKEN");
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}
}

static void spawn_command_with_activation(struct aswl_server *server, const char *command)
{
	if (command == NULL || command[0] == '\0')
		return;

	pid_t pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		unsetenv("XDG_ACTIVATION_TOKEN");

		struct wlr_xdg_activation_token_v1 *token = aswl_activation_token_for_spawn(server);
		const char *token_name = token != NULL ? wlr_xdg_activation_token_v1_get_name(token) : NULL;
		if (token_name != NULL && token_name[0] != '\0')
			setenv("XDG_ACTIVATION_TOKEN", token_name, 1);

		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}
}

static void spawn_lock(struct aswl_server *server)
{
	if (server == NULL)
		return;
	if (server->session_locked || server->session_lock != NULL)
		return;

	const char *cmd = server->lock_command != NULL ? server->lock_command : "aswllock";
	if (cmd == NULL || cmd[0] == '\0')
		return;

	fprintf(stderr, "aswlcomp: lock: %s\n", cmd);
	spawn_command(cmd);
}

static bool aswl_idle_inhibit_is_active(struct aswl_server *server)
{
	if (server == NULL)
		return false;

	struct aswl_idle_inhibitor *inhib;
	wl_list_for_each(inhib, &server->idle_inhibitors, link) {
		if (inhib == NULL || inhib->wlr_inhibitor == NULL || inhib->wlr_inhibitor->surface == NULL)
			continue;
		if (inhib->wlr_inhibitor->surface->mapped)
			return true;
	}

	return false;
}

static int aswl_idle_lock_timer_cb(void *data)
{
	struct aswl_server *server = data;
	if (server == NULL)
		return 0;
	if (server->idle_lock_seconds <= 0)
		return 0;
	if (server->session_locked || server->session_lock != NULL)
		return 0;

	if (aswl_idle_inhibit_is_active(server)) {
		if (getenv("ASWLCOMP_DEBUG_IDLE") != NULL)
			fprintf(stderr, "aswlcomp: idle-lock inhibited\n");
		if (server->idle_lock_timer != NULL) {
			(void)wl_event_source_timer_update(server->idle_lock_timer, server->idle_lock_seconds * 1000);
		}
		return 0;
	}

	fprintf(stderr, "aswlcomp: idle-lock\n");
	spawn_lock(server);
	return 0;
}

static void aswl_idle_lock_note_activity(struct aswl_server *server)
{
	if (server == NULL)
		return;
	if (server->idle_lock_timer == NULL)
		return;
	if (server->idle_lock_seconds <= 0)
		return;
	if (server->session_locked || server->session_lock != NULL)
		return;

	int ms = server->idle_lock_seconds * 1000;
	if (ms <= 0)
		return;

	(void)wl_event_source_timer_update(server->idle_lock_timer, ms);
}

static void aswl_idle_note_activity(struct aswl_server *server)
{
	if (server == NULL)
		return;

	if (server->idle_notifier != NULL && server->seat != NULL)
		wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);

	aswl_idle_lock_note_activity(server);
}

static void aswl_idle_inhibit_refresh(struct aswl_server *server)
{
	if (server == NULL)
		return;

	bool inhibited = aswl_idle_inhibit_is_active(server);
	if (inhibited == server->idle_inhibited)
		return;

	server->idle_inhibited = inhibited;

	if (getenv("ASWLCOMP_DEBUG_IDLE") != NULL)
		fprintf(stderr, "aswlcomp: idle-inhibited=%s\n", inhibited ? "true" : "false");

	if (server->idle_notifier != NULL)
		wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, inhibited);

	/* When inhibition is cleared, restart the idle-lock timer from "now". */
	if (!inhibited)
		aswl_idle_lock_note_activity(server);
}

static void handle_idle_inhibitor_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_idle_inhibitor *inhib = wl_container_of(listener, inhib, destroy);
	if (inhib == NULL)
		return;

	struct aswl_server *server = inhib->server;

	wl_list_remove(&inhib->destroy.link);
	wl_list_remove(&inhib->surface_map.link);
	wl_list_remove(&inhib->surface_unmap.link);
	wl_list_remove(&inhib->link);
	free(inhib);

	aswl_idle_inhibit_refresh(server);
}

static void handle_idle_inhibitor_surface_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_idle_inhibitor *inhib = wl_container_of(listener, inhib, surface_map);
	if (inhib == NULL)
		return;

	aswl_idle_inhibit_refresh(inhib->server);
}

static void handle_idle_inhibitor_surface_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_idle_inhibitor *inhib = wl_container_of(listener, inhib, surface_unmap);
	if (inhib == NULL)
		return;

	aswl_idle_inhibit_refresh(inhib->server);
}

static void handle_new_idle_inhibitor(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_idle_inhibitor);
	struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;
	if (server == NULL || wlr_inhibitor == NULL)
		return;

	struct aswl_idle_inhibitor *inhib = calloc(1, sizeof(*inhib));
	if (inhib == NULL)
		return;

	inhib->server = server;
	inhib->wlr_inhibitor = wlr_inhibitor;

	wl_list_init(&inhib->destroy.link);
	wl_list_init(&inhib->surface_map.link);
	wl_list_init(&inhib->surface_unmap.link);

	inhib->destroy.notify = handle_idle_inhibitor_destroy;
	wl_signal_add(&wlr_inhibitor->events.destroy, &inhib->destroy);

	struct wlr_surface *surface = wlr_inhibitor->surface;
	if (surface != NULL) {
		inhib->surface_map.notify = handle_idle_inhibitor_surface_map;
		wl_signal_add(&surface->events.map, &inhib->surface_map);

		inhib->surface_unmap.notify = handle_idle_inhibitor_surface_unmap;
		wl_signal_add(&surface->events.unmap, &inhib->surface_unmap);
	}

	wl_list_insert(&server->idle_inhibitors, &inhib->link);
	aswl_idle_inhibit_refresh(server);
}

static void idle_inhibitors_destroy(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_idle_inhibitor *inhib;
	struct aswl_idle_inhibitor *tmp;
	wl_list_for_each_safe(inhib, tmp, &server->idle_inhibitors, link) {
		wl_list_remove(&inhib->destroy.link);
		wl_list_remove(&inhib->surface_map.link);
		wl_list_remove(&inhib->surface_unmap.link);
		wl_list_remove(&inhib->link);
		free(inhib);
	}

	server->idle_inhibited = false;
	if (server->idle_notifier != NULL)
		wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, false);
}

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

static void aswl_ime_set_focus(struct aswl_server *server, struct wlr_surface *surface)
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

static void aswl_ime_maybe_set_keyboard_grab(struct aswl_server *server, struct wlr_keyboard *keyboard)
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

static void aswl_ime_notify_key(struct aswl_server *server, struct wlr_keyboard_key_event *event)
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

static void aswl_ime_notify_modifiers(struct aswl_server *server, struct wlr_keyboard *keyboard)
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

static void handle_new_virtual_keyboard(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *vk = data;
	if (server == NULL || server->seat == NULL || vk == NULL)
		return;

	if (getenv("ASWLCOMP_DISABLE_KEYBOARD") != NULL) {
		fprintf(stderr, "aswlcomp: ignoring virtual keyboard (ASWLCOMP_DISABLE_KEYBOARD)\n");
		return;
	}

	struct wlr_keyboard *wlr_keyboard = &vk->keyboard;

	struct aswl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (keyboard == NULL)
		return;

	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;
	keyboard->has_keymap = vk->has_keymap;

	aswl_apply_keyboard_device_config(server, keyboard);

	keyboard->key.notify = handle_keyboard_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

	keyboard->modifiers.notify = handle_keyboard_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

	keyboard->destroy.notify = handle_keyboard_destroy;
	wl_signal_add(&wlr_keyboard->base.events.destroy, &keyboard->destroy);

	wl_list_insert(&server->keyboards, &keyboard->link);

	wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	wlr_seat_set_capabilities(server->seat, server->seat->capabilities | WL_SEAT_CAPABILITY_KEYBOARD);
	aswl_ime_maybe_set_keyboard_grab(server, wlr_keyboard);
}

static void handle_pointer_constraint_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_pointer_constraint *pc = wl_container_of(listener, pc, destroy);
	if (pc == NULL)
		return;

	struct aswl_server *server = pc->server;
	struct wlr_pointer_constraint_v1 *constraint = pc->constraint;

	wl_list_remove(&pc->destroy.link);
	wl_list_remove(&pc->set_region.link);
	free(pc);

	if (server != NULL && server->active_pointer_constraint == constraint) {
		server->active_pointer_constraint = NULL;
		server->pointer_constraint_locked = false;
	}
}

static void handle_pointer_constraint_set_region(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_pointer_constraint *pc = wl_container_of(listener, pc, set_region);
	if (pc == NULL || pc->server == NULL)
		return;

	/* Region changes are applied on the next pointer motion. */
}

static void aswl_pointer_constraints_update(struct aswl_server *server, struct wlr_surface *surface)
{
	if (server == NULL || server->pointer_constraints == NULL || server->seat == NULL)
		return;

	struct wlr_pointer_constraint_v1 *constraint = NULL;
	if (surface != NULL)
		constraint = wlr_pointer_constraints_v1_constraint_for_surface(server->pointer_constraints, surface, server->seat);

	if (constraint == server->active_pointer_constraint)
		return;

	struct wlr_pointer_constraint_v1 *old = server->active_pointer_constraint;
	server->active_pointer_constraint = NULL;
	server->pointer_constraint_locked = false;
	if (old != NULL)
		wlr_pointer_constraint_v1_send_deactivated(old);

	if (constraint != NULL) {
		server->active_pointer_constraint = constraint;
		if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED && server->cursor != NULL) {
			server->pointer_constraint_locked = true;
			server->pointer_constraint_lx = server->cursor->x;
			server->pointer_constraint_ly = server->cursor->y;
		}
		wlr_pointer_constraint_v1_send_activated(constraint);
	}
}

static void handle_new_pointer_constraint(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_pointer_constraint);
	struct wlr_pointer_constraint_v1 *constraint = data;
	if (server == NULL || constraint == NULL)
		return;

	struct aswl_pointer_constraint *pc = calloc(1, sizeof(*pc));
	if (pc == NULL)
		return;

	pc->server = server;
	pc->constraint = constraint;
	constraint->data = pc;

	wl_list_init(&pc->destroy.link);
	wl_list_init(&pc->set_region.link);

	pc->destroy.notify = handle_pointer_constraint_destroy;
	wl_signal_add(&constraint->events.destroy, &pc->destroy);

	pc->set_region.notify = handle_pointer_constraint_set_region;
	wl_signal_add(&constraint->events.set_region, &pc->set_region);

	struct wlr_surface *focused = server->seat != NULL ? server->seat->pointer_state.focused_surface : NULL;
	aswl_pointer_constraints_update(server, focused);
}

static int aswl_flush_timer_cb(void *data)
{
	struct aswl_server *server = data;
	if (server == NULL)
		return 0;

	if (getenv("ASWLCOMP_DEBUG_FLUSH") != NULL)
		fprintf(stderr, "aswlcomp: flush_timer_cb\n");

	if (server->flush_timer != NULL) {
		wl_event_source_remove(server->flush_timer);
		server->flush_timer = NULL;
	}
	if (server->display != NULL) {
		/*
		 * wl_display_flush_clients() can fail to make progress if one client is
		 * wedged, starving other clients (e.g., control protocol callers).
		 * Flush each client individually so one slow client can't block others.
		 */
		struct wl_list *clients = wl_display_get_client_list(server->display);
		if (clients != NULL) {
			for (struct wl_list *l = clients->next; l != clients; l = l->next) {
				struct wl_client *client = wl_client_from_link(l);
				if (client != NULL)
					wl_client_flush(client);
			}
		}
	}

	return 0;
}

static void aswl_schedule_flush(struct aswl_server *server)
{
	if (server == NULL || server->display == NULL)
		return;
	if (server->flush_timer != NULL)
		return;

	struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
	if (loop == NULL)
		return;

	server->flush_timer = wl_event_loop_add_timer(loop, aswl_flush_timer_cb, server);
	if (server->flush_timer == NULL)
		return;

	if (getenv("ASWLCOMP_DEBUG_FLUSH") != NULL)
		fprintf(stderr, "aswlcomp: schedule_flush\n");

	/* 0ms delays can be treated as "disarm" by some libwayland builds; use 1ms. */
	(void)wl_event_source_timer_update(server->flush_timer, 1);
}

static char *xstrdup_printf(const char *fmt, ...)
{
	if (fmt == NULL)
		return NULL;

	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0)
		return NULL;

	char *buf = malloc((size_t)n + 1);
	if (buf == NULL)
		return NULL;

	va_start(ap, fmt);
	(void)vsnprintf(buf, (size_t)n + 1, fmt, ap);
	va_end(ap);
	return buf;
}

static int aswl_mkdir_p(const char *dir, mode_t mode)
{
	if (dir == NULL || dir[0] == '\0')
		return -1;

	char *path = strdup(dir);
	if (path == NULL)
		return -1;

	for (char *p = path + 1; *p != '\0'; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(path, mode) < 0 && errno != EEXIST) {
			free(path);
			return -1;
		}
		*p = '/';
	}

	if (mkdir(path, mode) < 0 && errno != EEXIST) {
		free(path);
		return -1;
	}

	free(path);
	return 0;
}

static int aswl_ensure_parent_dir(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return -1;

	char *dup = strdup(path);
	if (dup == NULL)
		return -1;

	char *slash = strrchr(dup, '/');
	if (slash == NULL) {
		free(dup);
		return 0;
	}
	if (slash == dup) {
		free(dup);
		return 0;
	}
	*slash = '\0';
	int rc = aswl_mkdir_p(dup, 0700);
	free(dup);
	return rc;
}

static bool parse_u32_strict(const char *s, uint32_t min, uint32_t max, uint32_t *out)
{
	if (out == NULL)
		return false;
	*out = 0;

	if (s == NULL)
		return false;

	char *end = NULL;
	errno = 0;
	unsigned long n = strtoul(s, &end, 10);
	if (errno != 0)
		return false;
	if (end == s || end == NULL || *end != '\0')
		return false;
	if (n < min || n > max)
		return false;

	*out = (uint32_t)n;
	return true;
}

static bool parse_i32_strict(const char *s, int min, int max, int *out)
{
	if (out != NULL)
		*out = 0;
	if (s == NULL)
		return false;

	char *end = NULL;
	errno = 0;
	long n = strtol(s, &end, 10);
	if (errno != 0)
		return false;
	if (end == s || end == NULL || *end != '\0')
		return false;
	if (n < min || n > max)
		return false;
	if (out != NULL)
		*out = (int)n;
	return true;
}

static bool parse_float_strict(const char *s, float min, float max, float *out)
{
	if (out != NULL)
		*out = 0.0f;
	if (s == NULL)
		return false;

	char *end = NULL;
	errno = 0;
	float n = strtof(s, &end);
	if (errno != 0)
		return false;
	if (end == s || end == NULL || *end != '\0')
		return false;
	if (!(n >= min && n <= max))
		return false;
	if (out != NULL)
		*out = n;
	return true;
}

static bool parse_bool_strict(const char *s, bool *out)
{
	if (out != NULL)
		*out = false;
	if (s == NULL || s[0] == '\0')
		return false;

	if (strcmp(s, "1") == 0 || str_ieq(s, "true") || str_ieq(s, "yes") || str_ieq(s, "on")) {
		if (out != NULL)
			*out = true;
		return true;
	}
	if (strcmp(s, "0") == 0 || str_ieq(s, "false") || str_ieq(s, "no") || str_ieq(s, "off")) {
		if (out != NULL)
			*out = false;
		return true;
	}
	return false;
}

static bool parse_output_transform_strict(const char *s, enum wl_output_transform *out)
{
	if (out != NULL)
		*out = WL_OUTPUT_TRANSFORM_NORMAL;
	if (s == NULL)
		return false;

	while (*s != '\0' && isspace((unsigned char)*s))
		s++;

	char token[32];
	size_t n = 0;
	while (*s != '\0' && !isspace((unsigned char)*s) && n + 1 < sizeof(token)) {
		char c = (char)tolower((unsigned char)*s);
		if (c == '_')
			c = '-';
		token[n++] = c;
		s++;
	}
	token[n] = '\0';

	enum wl_output_transform t = WL_OUTPUT_TRANSFORM_NORMAL;
	bool ok = true;

	if (strcmp(token, "normal") == 0 || strcmp(token, "0") == 0) {
		t = WL_OUTPUT_TRANSFORM_NORMAL;
	} else if (strcmp(token, "90") == 0 || strcmp(token, "rot90") == 0 || strcmp(token, "rotate90") == 0) {
		t = WL_OUTPUT_TRANSFORM_90;
	} else if (strcmp(token, "180") == 0 || strcmp(token, "rot180") == 0 || strcmp(token, "rotate180") == 0) {
		t = WL_OUTPUT_TRANSFORM_180;
	} else if (strcmp(token, "270") == 0 || strcmp(token, "rot270") == 0 || strcmp(token, "rotate270") == 0) {
		t = WL_OUTPUT_TRANSFORM_270;
	} else if (strcmp(token, "flipped") == 0 || strcmp(token, "flip") == 0) {
		t = WL_OUTPUT_TRANSFORM_FLIPPED;
	} else if (strcmp(token, "flipped-90") == 0 || strcmp(token, "flip-90") == 0) {
		t = WL_OUTPUT_TRANSFORM_FLIPPED_90;
	} else if (strcmp(token, "flipped-180") == 0 || strcmp(token, "flip-180") == 0) {
		t = WL_OUTPUT_TRANSFORM_FLIPPED_180;
	} else if (strcmp(token, "flipped-270") == 0 || strcmp(token, "flip-270") == 0) {
		t = WL_OUTPUT_TRANSFORM_FLIPPED_270;
	} else {
		ok = false;
	}

	if (ok && out != NULL)
		*out = t;
	return ok;
}

static const char *output_transform_to_string(enum wl_output_transform t)
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

static bool parse_output_mode_strict(const char *s, bool *out_preferred, int *out_w, int *out_h, int *out_refresh_mhz)
{
	if (out_preferred != NULL)
		*out_preferred = false;
	if (out_w != NULL)
		*out_w = 0;
	if (out_h != NULL)
		*out_h = 0;
	if (out_refresh_mhz != NULL)
		*out_refresh_mhz = 0;
	if (s == NULL)
		return false;

	if (str_ieq(s, "preferred")) {
		if (out_preferred != NULL)
			*out_preferred = true;
		return true;
	}

	const char *x = strchr(s, 'x');
	if (x == NULL)
		x = strchr(s, 'X');
	if (x == NULL)
		return false;

	char wbuf[32];
	size_t wlen = (size_t)(x - s);
	if (wlen == 0 || wlen >= sizeof(wbuf))
		return false;
	memcpy(wbuf, s, wlen);
	wbuf[wlen] = '\0';

	const char *rest = x + 1;
	const char *at = strchr(rest, '@');

	char hbuf[32];
	size_t hlen = at != NULL ? (size_t)(at - rest) : strlen(rest);
	if (hlen == 0 || hlen >= sizeof(hbuf))
		return false;
	memcpy(hbuf, rest, hlen);
	hbuf[hlen] = '\0';

	int w = 0;
	int h = 0;
	if (!parse_i32_strict(wbuf, 1, 16384, &w))
		return false;
	if (!parse_i32_strict(hbuf, 1, 16384, &h))
		return false;

	int refresh_mhz = 0;
	if (at != NULL) {
		int refresh = 0;
		if (!parse_i32_strict(at + 1, 0, 1000000, &refresh))
			return false;
		if (refresh < 1000)
			refresh_mhz = refresh * 1000;
		else
			refresh_mhz = refresh;
	}

	if (out_w != NULL)
		*out_w = w;
	if (out_h != NULL)
		*out_h = h;
	if (out_refresh_mhz != NULL)
		*out_refresh_mhz = refresh_mhz;
	if (out_preferred != NULL)
		*out_preferred = false;
	return true;
}

static char *aswl_state_path_default(void)
{
	const char *state_home = getenv("XDG_STATE_HOME");
	if (state_home != NULL && state_home[0] != '\0')
		return xstrdup_printf("%s/afterstep/aswlcomp.state", state_home);

	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0')
		return xstrdup_printf("%s/.local/state/afterstep/aswlcomp.state", home);

	return NULL;
}

static char *aswl_state_path_resolve(const char *override)
{
	if (override != NULL && override[0] != '\0')
		return strdup(override);

	const char *env = getenv("ASWLCOMP_STATE");
	if (env != NULL && env[0] != '\0')
		return strdup(env);

	return aswl_state_path_default();
}

static void aswl_state_load(struct aswl_server *server, bool allow_workspace_count_override)
{
	if (server == NULL || server->state_path == NULL)
		return;

	FILE *fp = fopen(server->state_path, "r");
	if (fp == NULL)
		return;

	char *line = NULL;
	size_t cap = 0;
	uint32_t loaded_ws = 0;
	uint32_t loaded_count = 0;
	bool have_ws = false;
	bool have_count = false;

	while (getline(&line, &cap, fp) != -1) {
		rstrip_inplace(line);
		char *s = lstrip(line);
		if (s[0] == '\0' || s[0] == '#' || s[0] == ';')
			continue;

		char *scan = s;
		char word[32];
		size_t wn = 0;
		while (*scan != '\0' && !isspace((unsigned char)*scan) && wn + 1 < sizeof(word)) {
			word[wn++] = *scan++;
		}
		word[wn] = '\0';
		if (wn > 0 && str_ieq(word, "output")) {
			while (*scan != '\0' && isspace((unsigned char)*scan))
				scan++;
			char *name = scan;
			if (name[0] == '\0')
				continue;
			while (*scan != '\0' && !isspace((unsigned char)*scan))
				scan++;
			char saved = *scan;
			*scan = '\0';
			struct aswl_output_persist *po = aswl_output_persist_get(server, name);
			*scan = saved;
			if (po == NULL)
				continue;
			while (*scan != '\0' && isspace((unsigned char)*scan))
				scan++;

			char *saveptr = NULL;
			for (char *tok = strtok_r(scan, " \t", &saveptr); tok != NULL; tok = strtok_r(NULL, " \t", &saveptr)) {
				char *eq = strchr(tok, '=');
				if (eq == NULL)
					continue;
				*eq = '\0';
				const char *key = tok;
				const char *val = eq + 1;

				if (str_ieq(key, "enabled")) {
					bool b = false;
					if (parse_bool_strict(val, &b)) {
						po->have_enabled = true;
						po->enabled = b;
					}
					continue;
				}

				if (str_ieq(key, "x")) {
					int v = 0;
					if (parse_i32_strict(val, -100000, 100000, &v)) {
						po->have_pos = true;
						po->x = v;
					}
					continue;
				}

				if (str_ieq(key, "y")) {
					int v = 0;
					if (parse_i32_strict(val, -100000, 100000, &v)) {
						po->have_pos = true;
						po->y = v;
					}
					continue;
				}

				if (str_ieq(key, "scale")) {
					float v = 1.0f;
					if (parse_float_strict(val, 0.1f, 16.0f, &v)) {
						po->have_scale = true;
						po->scale = v;
					}
					continue;
				}

				if (str_ieq(key, "transform")) {
					enum wl_output_transform t = WL_OUTPUT_TRANSFORM_NORMAL;
					if (parse_output_transform_strict(val, &t)) {
						po->have_transform = true;
						po->transform = t;
					}
					continue;
				}

				if (str_ieq(key, "mode")) {
					bool preferred = false;
					int mw = 0;
					int mh = 0;
					int mr = 0;
					if (parse_output_mode_strict(val, &preferred, &mw, &mh, &mr)) {
						po->have_mode = true;
						po->mode_preferred = preferred;
						po->mode_width = mw;
						po->mode_height = mh;
						po->mode_refresh_mhz = mr;
					}
					continue;
				}
			}
			continue;
		}

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';

		char *key = lstrip(s);
		rstrip_inplace(key);

		char *val = lstrip(eq + 1);
		rstrip_inplace(val);

		if (str_ieq(key, "current_workspace")) {
			uint32_t ws = 0;
			if (parse_u32_strict(val, 1, 1000, &ws)) {
				loaded_ws = ws;
				have_ws = true;
			}
			continue;
		}

		if (str_ieq(key, "workspace_count")) {
			uint32_t count = 0;
			if (parse_u32_strict(val, 1, 1000, &count)) {
				loaded_count = count;
				have_count = true;
			}
			continue;
		}
	}

	free(line);
	fclose(fp);

	if (have_count && allow_workspace_count_override)
		server->workspace_count = loaded_count;

	if (have_ws)
		server->current_workspace = loaded_ws;
	server->current_workspace = normalize_workspace(server, server->current_workspace);
}

static void aswl_state_save(struct aswl_server *server)
{
	if (server == NULL || server->state_path == NULL)
		return;

	aswl_outputs_persist_sync_from_live(server);

	if (aswl_ensure_parent_dir(server->state_path) != 0) {
		fprintf(stderr, "aswlcomp: state: ensure dir failed: %s\n", server->state_path);
		return;
	}

	char *tmp = xstrdup_printf("%s.XXXXXX", server->state_path);
	if (tmp == NULL)
		return;

	int fd = mkstemp(tmp);
	if (fd < 0) {
		fprintf(stderr, "aswlcomp: state: mkstemp failed: %s: %s\n", tmp, strerror(errno));
		free(tmp);
		return;
	}

	FILE *fp = fdopen(fd, "w");
	if (fp == NULL) {
		(void)close(fd);
		(void)unlink(tmp);
		free(tmp);
		return;
	}

	fprintf(fp, "# aswlcomp state v2\n");
	fprintf(fp, "workspace_count=%u\n", server->workspace_count);
	fprintf(fp, "current_workspace=%u\n", server->current_workspace);

	struct aswl_output_persist *po;
	wl_list_for_each(po, &server->outputs_persist, link) {
		if (po == NULL || po->name == NULL || po->name[0] == '\0')
			continue;

		const bool enabled = po->have_enabled ? po->enabled : true;
		const float scale = po->have_scale ? po->scale : 1.0f;
		const enum wl_output_transform transform = po->have_transform ? po->transform : WL_OUTPUT_TRANSFORM_NORMAL;

		char mode[64];
		const char *mode_s = "preferred";
		if (po->have_mode) {
			if (po->mode_preferred) {
				mode_s = "preferred";
			} else {
				if (po->mode_refresh_mhz > 0)
					(void)snprintf(mode, sizeof(mode), "%dx%d@%d", po->mode_width, po->mode_height, po->mode_refresh_mhz);
				else
					(void)snprintf(mode, sizeof(mode), "%dx%d", po->mode_width, po->mode_height);
				mode_s = mode;
			}
		}

		fprintf(fp,
		        "output %s enabled=%d x=%d y=%d scale=%.3f transform=%s mode=%s\n",
		        po->name,
		        enabled ? 1 : 0,
		        po->have_pos ? po->x : 0,
		        po->have_pos ? po->y : 0,
		        (double)scale,
		        output_transform_to_string(transform),
		        mode_s);
	}
	(void)fflush(fp);

	bool ok = !ferror(fp);
	if (ok)
		ok = fsync(fd) == 0;

	(void)fclose(fp);

	if (!ok) {
		(void)unlink(tmp);
		free(tmp);
		return;
	}

	if (rename(tmp, server->state_path) < 0) {
		fprintf(stderr, "aswlcomp: state: rename failed: %s -> %s: %s\n", tmp, server->state_path, strerror(errno));
		(void)unlink(tmp);
	}

	free(tmp);
}

static struct aswl_output_persist *aswl_output_persist_find(struct aswl_server *server, const char *name)
{
	if (server == NULL || name == NULL || name[0] == '\0')
		return NULL;

	struct aswl_output_persist *po;
	wl_list_for_each(po, &server->outputs_persist, link) {
		if (po == NULL || po->name == NULL)
			continue;
		if (strcmp(po->name, name) == 0)
			return po;
	}
	return NULL;
}

static struct aswl_output_persist *aswl_output_persist_get(struct aswl_server *server, const char *name)
{
	struct aswl_output_persist *po = aswl_output_persist_find(server, name);
	if (po != NULL)
		return po;
	if (server == NULL || name == NULL || name[0] == '\0')
		return NULL;

	po = calloc(1, sizeof(*po));
	if (po == NULL)
		return NULL;
	po->name = strdup(name);
	if (po->name == NULL) {
		free(po);
		return NULL;
	}
	po->scale = 1.0f;
	po->transform = WL_OUTPUT_TRANSFORM_NORMAL;

	/*
	 * Keep insertion order stable: append at the end so state files remain
	 * readable and deterministic as outputs come and go.
	 */
	wl_list_insert(server->outputs_persist.prev, &po->link);
	return po;
}

static void aswl_outputs_persist_destroy(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_output_persist *po;
	struct aswl_output_persist *tmp;
	wl_list_for_each_safe(po, tmp, &server->outputs_persist, link) {
		wl_list_remove(&po->link);
		free(po->name);
		free(po);
	}
	wl_list_init(&server->outputs_persist);
}

static void aswl_outputs_persist_sync_from_live(struct aswl_server *server)
{
	if (server == NULL)
		return;
	if (server->output_layout == NULL)
		return;

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out == NULL || out->wlr_output == NULL || out->wlr_output->name[0] == '\0')
			continue;

		struct wlr_output *wo = out->wlr_output;
		struct aswl_output_persist *po = aswl_output_persist_get(server, wo->name);
		if (po == NULL)
			continue;

		po->have_enabled = true;
		po->enabled = wo->enabled;

		struct wlr_output_layout_output *lo = wlr_output_layout_get(server->output_layout, wo);
		if (lo != NULL) {
			po->have_pos = true;
			po->x = lo->x;
			po->y = lo->y;
		}

		po->have_scale = true;
		po->scale = wo->scale > 0.0f ? wo->scale : 1.0f;

		po->have_transform = true;
		po->transform = wo->transform;

		struct wlr_output_mode *preferred = wlr_output_preferred_mode(wo);
		if (wo->current_mode != NULL) {
			po->have_mode = true;
			po->mode_width = wo->current_mode->width;
			po->mode_height = wo->current_mode->height;
			po->mode_refresh_mhz = wo->current_mode->refresh;
			po->mode_preferred = (preferred != NULL && wo->current_mode == preferred);
		} else if (wo->width > 0 && wo->height > 0) {
			po->have_mode = true;
			po->mode_width = wo->width;
			po->mode_height = wo->height;
			po->mode_refresh_mhz = (int)wo->refresh;
			po->mode_preferred = (preferred != NULL &&
			                      preferred->width == wo->width &&
			                      preferred->height == wo->height &&
			                      preferred->refresh == wo->refresh);
		}
	}
}

static struct wlr_output_mode *find_output_mode(struct wlr_output *output, int w, int h, int refresh_mhz)
{
	if (output == NULL || w <= 0 || h <= 0)
		return NULL;

	struct wlr_output_mode *best = NULL;
	int best_delta = 0;

	struct wlr_output_mode *mode;
	wl_list_for_each(mode, &output->modes, link) {
		if (mode == NULL)
			continue;
		if (mode->width != w || mode->height != h)
			continue;

		if (refresh_mhz <= 0)
			return mode;

		int delta = mode->refresh - refresh_mhz;
		if (delta < 0)
			delta = -delta;
		if (best == NULL || delta < best_delta) {
			best = mode;
			best_delta = delta;
		}
	}

	return best;
}

static bool aswl_output_persist_apply(struct aswl_server *server,
                                     struct wlr_output *output,
                                     struct wlr_output_state *state,
                                     int *out_x,
                                     int *out_y,
                                     bool *out_have_pos)
{
	if (out_x != NULL)
		*out_x = 0;
	if (out_y != NULL)
		*out_y = 0;
	if (out_have_pos != NULL)
		*out_have_pos = false;

	if (server == NULL || output == NULL || state == NULL)
		return false;

	struct aswl_output_persist *po = aswl_output_persist_find(server, output->name);
	if (po == NULL)
		return false;

	if (po->have_enabled)
		wlr_output_state_set_enabled(state, po->enabled);
	if (po->have_scale)
		wlr_output_state_set_scale(state, po->scale);
	if (po->have_transform)
		wlr_output_state_set_transform(state, po->transform);

	if (po->have_pos) {
		if (out_x != NULL)
			*out_x = po->x;
		if (out_y != NULL)
			*out_y = po->y;
		if (out_have_pos != NULL)
			*out_have_pos = true;
	}

	struct wlr_output_mode *mode = NULL;
	if (!po->have_mode || po->mode_preferred) {
		mode = wlr_output_preferred_mode(output);
	} else {
		mode = find_output_mode(output, po->mode_width, po->mode_height, po->mode_refresh_mhz);
		if (mode == NULL && wl_list_empty(&output->modes)) {
			wlr_output_state_set_custom_mode(state, po->mode_width, po->mode_height, po->mode_refresh_mhz);
			return true;
		}
		if (mode == NULL)
			mode = wlr_output_preferred_mode(output);
	}

	if (mode != NULL)
		wlr_output_state_set_mode(state, mode);
	return true;
}

static void broadcast_workspace_state(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_control_client *cc;
	struct aswl_control_client *tmp;
	wl_list_for_each_safe(cc, tmp, &server->control_clients, link) {
		if (cc->resource == NULL)
			continue;
		if (wl_resource_get_version(cc->resource) < 3)
			continue;
		afterstep_control_v1_send_workspace_state(cc->resource, server->current_workspace, server->workspace_count);
	}
}

static struct aswl_output *server_primary_output(struct aswl_server *server)
{
	if (server == NULL)
		return NULL;
	if (wl_list_empty(&server->outputs))
		return NULL;

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out == NULL || out->wlr_output == NULL)
			continue;
		if (!out->wlr_output->enabled)
			continue;
		if (server->output_layout != NULL) {
			struct wlr_box box = { 0 };
			wlr_output_layout_get_box(server->output_layout, out->wlr_output, &box);
			if (box.width <= 0 || box.height <= 0)
				continue;
		}
		return out;
	}

	return wl_container_of(server->outputs.next, out, link);
}

static void send_output_state(struct aswl_server *server, struct wl_resource *resource)
{
	if (server == NULL || resource == NULL)
		return;
	if (wl_resource_get_version(resource) < 6)
		return;

	struct aswl_output *out = server_primary_output(server);
	uint32_t width = 0;
	uint32_t height = 0;
	if (out != NULL) {
		if (out->full_box.width > 0)
			width = (uint32_t)out->full_box.width;
		if (out->full_box.height > 0)
			height = (uint32_t)out->full_box.height;
		if ((width == 0 || height == 0) && out->wlr_output != NULL) {
			if (out->wlr_output->width > 0)
				width = (uint32_t)out->wlr_output->width;
			if (out->wlr_output->height > 0)
				height = (uint32_t)out->wlr_output->height;
		}
	}

	afterstep_control_v1_send_output_state(resource, width, height);
}

static void broadcast_output_state(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_control_client *cc;
	struct aswl_control_client *tmp;
	wl_list_for_each_safe(cc, tmp, &server->control_clients, link) {
		if (cc->resource == NULL)
			continue;
		send_output_state(server, cc->resource);
	}
}

static const char *view_title(struct aswl_view *view)
{
	if (view == NULL)
		return "";

	if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL && view->xdg_surface->toplevel->title != NULL)
		return view->xdg_surface->toplevel->title;

	if (view->xwayland_surface != NULL && view->xwayland_surface->title != NULL)
		return view->xwayland_surface->title;

	return "";
}

static const char *view_app_id(struct aswl_view *view)
{
	if (view == NULL)
		return "";

	if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL && view->xdg_surface->toplevel->app_id != NULL)
		return view->xdg_surface->toplevel->app_id;

	if (view->xwayland_surface != NULL && view->xwayland_surface->class != NULL)
		return view->xwayland_surface->class;

	return "";
}

static bool view_is_suite_popup(struct aswl_view *view)
{
	if (view == NULL)
		return false;
	return strcmp(view_app_id(view), "afterstep.aswlmenu") == 0;
}

static void handle_foreign_request_maximize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, foreign_request_maximize);
	struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
	if (view == NULL || view->server == NULL || view->foreign_toplevel == NULL || event == NULL)
		return;
	if (event->toplevel != view->foreign_toplevel)
		return;
	if (view->is_dock)
		return;

	view_set_maximized(view, event->maximized);
	aswl_schedule_flush(view->server);
}

static void handle_foreign_request_minimize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, foreign_request_minimize);
	struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
	if (view == NULL || view->server == NULL || view->foreign_toplevel == NULL || event == NULL)
		return;
	if (event->toplevel != view->foreign_toplevel)
		return;
	if (view->is_dock)
		return;

	/* xdg-shell doesn't support compositor-driven minimize; support it for Xwayland only. */
	if (view->xwayland_surface != NULL)
		wlr_xwayland_surface_set_minimized(view->xwayland_surface, event->minimized);

	view_update_toplevel_protocols(view);
	aswl_schedule_flush(view->server);
}

static void handle_foreign_request_activate(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, foreign_request_activate);
	struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;
	if (view == NULL || view->server == NULL || view->foreign_toplevel == NULL || event == NULL)
		return;
	if (event->toplevel != view->foreign_toplevel)
		return;
	if (view->is_dock)
		return;
	if (event->seat != NULL && event->seat != view->server->seat)
		return;

	focus_view(view, NULL);
	aswl_schedule_flush(view->server);
}

static void handle_foreign_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, foreign_request_fullscreen);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
	if (view == NULL || view->server == NULL || view->foreign_toplevel == NULL || event == NULL)
		return;
	if (event->toplevel != view->foreign_toplevel)
		return;
	if (view->is_dock)
		return;

	view_set_fullscreen(view, event->fullscreen);
	aswl_schedule_flush(view->server);
}

static void handle_foreign_request_close(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, foreign_request_close);
	if (view == NULL || view->server == NULL || view->foreign_toplevel == NULL)
		return;
	if (view->is_dock)
		return;

	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
		wlr_xdg_toplevel_send_close(view->xdg_surface->toplevel);
	else if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		wlr_xwayland_surface_close(view->xwayland_surface);

	aswl_schedule_flush(view->server);
}

static void view_update_toplevel_protocols(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL || view->is_dock)
		return;

	const char *title = view_title(view);
	const char *app_id = view_app_id(view);

	if (view->ext_foreign_toplevel != NULL) {
		struct wlr_ext_foreign_toplevel_handle_v1_state state = {
			.title = title,
			.app_id = app_id,
		};
		wlr_ext_foreign_toplevel_handle_v1_update_state(view->ext_foreign_toplevel, &state);
	}

	if (view->foreign_toplevel != NULL) {
		wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel, title);
		wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel, app_id);
		wlr_foreign_toplevel_handle_v1_set_activated(view->foreign_toplevel, view->server->focused_view == view);
		wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, view_is_fullscreen(view));
		wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, view_is_maximized(view));
		bool minimized = view->xwayland_surface != NULL && view->xwayland_surface->minimized;
		wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, minimized);
	}
}

static void view_toplevel_protocols_create(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL)
		return;
	if (!view->mapped || view->is_dock)
		return;

	struct aswl_server *server = view->server;

	if (view->ext_foreign_toplevel == NULL && server->ext_foreign_toplevel_list != NULL) {
		struct wlr_ext_foreign_toplevel_handle_v1_state state = {
			.title = view_title(view),
			.app_id = view_app_id(view),
		};
		view->ext_foreign_toplevel = wlr_ext_foreign_toplevel_handle_v1_create(server->ext_foreign_toplevel_list, &state);
		if (view->ext_foreign_toplevel != NULL)
			view->ext_foreign_toplevel->data = view;
	}

	if (view->foreign_toplevel == NULL && server->foreign_toplevel_manager != NULL) {
		view->foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_manager);
		if (view->foreign_toplevel != NULL) {
			view->foreign_toplevel->data = view;

			view->foreign_request_maximize.notify = handle_foreign_request_maximize;
			wl_signal_add(&view->foreign_toplevel->events.request_maximize, &view->foreign_request_maximize);

			view->foreign_request_minimize.notify = handle_foreign_request_minimize;
			wl_signal_add(&view->foreign_toplevel->events.request_minimize, &view->foreign_request_minimize);

			view->foreign_request_activate.notify = handle_foreign_request_activate;
			wl_signal_add(&view->foreign_toplevel->events.request_activate, &view->foreign_request_activate);

			view->foreign_request_fullscreen.notify = handle_foreign_request_fullscreen;
			wl_signal_add(&view->foreign_toplevel->events.request_fullscreen, &view->foreign_request_fullscreen);

			view->foreign_request_close.notify = handle_foreign_request_close;
			wl_signal_add(&view->foreign_toplevel->events.request_close, &view->foreign_request_close);

			view->foreign_toplevel_listeners_added = true;

			if (server->output_layout != NULL && view->scene_tree != NULL) {
				int lx = 0;
				int ly = 0;
				(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);
				struct wlr_output *output = wlr_output_layout_output_at(server->output_layout, lx + 1, ly + 1);
				if (output != NULL)
					wlr_foreign_toplevel_handle_v1_output_enter(view->foreign_toplevel, output);
			}
		}
	}

	view_update_toplevel_protocols(view);
}

static void view_toplevel_protocols_destroy(struct aswl_view *view)
{
	if (view == NULL)
		return;

	if (view->ext_foreign_toplevel != NULL) {
		wlr_ext_foreign_toplevel_handle_v1_destroy(view->ext_foreign_toplevel);
		view->ext_foreign_toplevel = NULL;
	}

	if (view->foreign_toplevel != NULL) {
		if (view->foreign_toplevel_listeners_added) {
			wl_list_remove(&view->foreign_request_maximize.link);
			wl_list_remove(&view->foreign_request_minimize.link);
			wl_list_remove(&view->foreign_request_activate.link);
			wl_list_remove(&view->foreign_request_fullscreen.link);
			wl_list_remove(&view->foreign_request_close.link);
			view->foreign_toplevel_listeners_added = false;
		}

		wlr_foreign_toplevel_handle_v1_destroy(view->foreign_toplevel);
		view->foreign_toplevel = NULL;
	}
}

static uint32_t view_window_flags(struct aswl_server *server, struct aswl_view *view)
{
	uint32_t flags = 0;
	if (view == NULL)
		return flags;

	if (view->mapped)
		flags |= ASWL_WINDOW_FLAG_MAPPED;
	if (server != NULL && server->focused_view == view)
		flags |= ASWL_WINDOW_FLAG_FOCUSED;
	if (view->type == ASWL_VIEW_XWAYLAND)
		flags |= ASWL_WINDOW_FLAG_XWAYLAND;

	return flags;
}

static void send_window_state(struct aswl_server *server, struct wl_resource *resource, struct aswl_view *view)
{
	if (server == NULL || resource == NULL || view == NULL)
		return;
	if (wl_resource_get_version(resource) < 4)
		return;
	if (view->id == 0)
		return;
	if (view->is_dock)
		return;

	uint32_t flags = view_window_flags(server, view);
	afterstep_control_v1_send_window(resource, view->id, view->workspace, flags, view_title(view), view_app_id(view));
}

static void send_window_geometry(struct aswl_server *server, struct wl_resource *resource, struct aswl_view *view)
{
	if (server == NULL || resource == NULL || view == NULL)
		return;
	if (wl_resource_get_version(resource) < 6)
		return;
	if (view->id == 0)
		return;
	if (view->is_dock)
		return;
	if (view->scene_tree == NULL)
		return;

	int lx = 0;
	int ly = 0;
	(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);

	int w = 0;
	int h = 0;
	view_get_frame_size(view, &w, &h);
	if (w < 0)
		w = 0;
	if (h < 0)
		h = 0;

	afterstep_control_v1_send_window_geometry(resource, view->id, lx, ly, w, h);
}

static void send_window_list_snapshot(struct aswl_server *server, struct wl_resource *resource)
{
	if (server == NULL || resource == NULL)
		return;
	if (wl_resource_get_version(resource) < 4)
		return;

	afterstep_control_v1_send_window_list_begin(resource);
	struct aswl_view *view;
	struct aswl_view *tmp;
	wl_list_for_each_safe(view, tmp, &server->views, link) {
		send_window_state(server, resource, view);
		send_window_geometry(server, resource, view);
	}
	afterstep_control_v1_send_window_list_end(resource);
}

static void broadcast_window_state(struct aswl_server *server, struct aswl_view *view)
{
	if (server == NULL || view == NULL)
		return;

	struct aswl_control_client *cc;
	struct aswl_control_client *tmp;
	wl_list_for_each_safe(cc, tmp, &server->control_clients, link) {
		if (cc->resource == NULL)
			continue;
		send_window_state(server, cc->resource, view);
		send_window_geometry(server, cc->resource, view);
	}
}

static void broadcast_window_closed(struct aswl_server *server, uint32_t id)
{
	if (server == NULL || id == 0)
		return;

	struct aswl_control_client *cc;
	struct aswl_control_client *tmp;
	wl_list_for_each_safe(cc, tmp, &server->control_clients, link) {
		if (cc->resource == NULL)
			continue;
		if (wl_resource_get_version(cc->resource) < 4)
			continue;
		afterstep_control_v1_send_window_closed(cc->resource, id);
	}
}

static struct aswl_view *find_view_by_id(struct aswl_server *server, uint32_t id)
{
	if (server == NULL || id == 0)
		return NULL;

	struct aswl_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->id == id)
			return view;
	}
	return NULL;
}

static void aswl_control_resource_destroy(struct wl_resource *resource)
{
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	if (cc == NULL)
		return;
	wl_list_remove(&cc->link);
	free(cc);
}

static void aswl_control_destroy(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static void aswl_control_exec(struct wl_client *client, struct wl_resource *resource, const char *command)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;

	if (command == NULL || command[0] == '\0')
		return;

	if (strlen(command) > 4096) {
		fprintf(stderr, "aswlcomp: control exec: command too long\n");
		return;
	}

	fprintf(stderr, "aswlcomp: control exec: %s\n", command);
	spawn_command_with_activation(server, command);

	aswl_schedule_flush(server);
}

static void aswl_control_quit(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control quit\n");
	wl_display_terminate(server->display);
}

static void aswl_control_close_focused(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control close_focused\n");
	close_focused_view(server);
	aswl_schedule_flush(server);
}

static void aswl_control_focus_next(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control focus_next\n");
	focus_next_view(server);
	aswl_schedule_flush(server);
}

static void aswl_control_focus_prev(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control focus_prev\n");
	focus_prev_view(server);
	aswl_schedule_flush(server);
}

static void aswl_control_set_workspace(struct wl_client *client, struct wl_resource *resource, uint32_t workspace)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control set_workspace=%u\n", workspace);
	set_workspace(server, workspace);
	aswl_schedule_flush(server);
}

static void aswl_control_workspace_next(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control workspace_next\n");
	workspace_next(server);
	aswl_schedule_flush(server);
}

static void aswl_control_workspace_prev(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	fprintf(stderr, "aswlcomp: control workspace_prev\n");
	workspace_prev(server);
	aswl_schedule_flush(server);
}

static void aswl_control_list_windows(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;
	if (wl_resource_get_version(resource) < 4)
		return;

	fprintf(stderr, "aswlcomp: control list_windows\n");
	send_window_list_snapshot(server, resource);
	aswl_schedule_flush(server);
}

static void aswl_control_focus_window(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	struct aswl_view *view = find_view_by_id(server, id);
	if (view == NULL || !view->mapped || view->scene_tree == NULL)
		return;

	fprintf(stderr, "aswlcomp: control focus_window=%u\n", id);
	if (view->workspace != server->current_workspace)
		set_workspace(server, view->workspace);
	focus_view(view, NULL);
	aswl_schedule_flush(server);
}

static void aswl_control_close_window(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	struct aswl_view *view = find_view_by_id(server, id);
	if (view == NULL)
		return;

	fprintf(stderr, "aswlcomp: control close_window=%u\n", id);
	if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
		wlr_xdg_toplevel_send_close(view->xdg_surface->toplevel);
	} else if (view->xwayland_surface != NULL) {
		wlr_xwayland_surface_close(view->xwayland_surface);
	}
	aswl_schedule_flush(server);
}

static void aswl_control_move_window_to_workspace(struct wl_client *client, struct wl_resource *resource, uint32_t id, uint32_t workspace)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	struct aswl_view *view = find_view_by_id(server, id);
	if (view == NULL)
		return;
	if (view->is_dock)
		return;

	workspace = normalize_workspace(server, workspace);
	if (view->workspace == workspace)
		return;

	fprintf(stderr, "aswlcomp: control move_window_to_workspace id=%u ws=%u\n", id, workspace);
	view->workspace = workspace;

	if (view->scene_tree != NULL) {
		bool enabled = view->mapped && view->workspace == server->current_workspace;
		wlr_scene_node_set_enabled(&view->scene_tree->node, enabled);
		if (enabled)
			place_view(view);
	}

	if (server->grabbed_view == view && view->workspace != server->current_workspace)
		end_interactive(server);

	if (server->focused_view == view && view->workspace != server->current_workspace) {
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
			(void)wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, false);
		} else if (view->xwayland_surface != NULL) {
			wlr_xwayland_surface_activate(view->xwayland_surface, false);
		}
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		focus_topmost_view(server);
	}

	broadcast_window_state(server, view);
	aswl_schedule_flush(server);
}

static void aswl_control_toggle_fullscreen(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	struct aswl_view *view = server->focused_view;
	if (view == NULL || !view->mapped || view->scene_tree == NULL)
		return;
	if (view->is_dock)
		return;

	fprintf(stderr, "aswlcomp: control toggle_fullscreen\n");
	if (server->grabbed_view == view)
		end_interactive(server);
	view_set_fullscreen(view, !view_is_fullscreen(view));

	aswl_schedule_flush(server);
}

static void aswl_control_toggle_maximized(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	struct aswl_control_client *cc = wl_resource_get_user_data(resource);
	struct aswl_server *server = cc != NULL ? cc->server : NULL;
	if (server == NULL)
		return;

	struct aswl_view *view = server->focused_view;
	if (view == NULL || !view->mapped || view->scene_tree == NULL)
		return;
	if (view->is_dock)
		return;

	fprintf(stderr, "aswlcomp: control toggle_maximized\n");
	if (server->grabbed_view == view)
		end_interactive(server);
	view_set_maximized(view, !view_is_maximized(view));

	aswl_schedule_flush(server);
}

static const struct afterstep_control_v1_interface aswl_control_impl = {
	.destroy = aswl_control_destroy,
	.exec = aswl_control_exec,
	.quit = aswl_control_quit,
	.close_focused = aswl_control_close_focused,
	.focus_next = aswl_control_focus_next,
	.focus_prev = aswl_control_focus_prev,
	.set_workspace = aswl_control_set_workspace,
	.workspace_next = aswl_control_workspace_next,
	.workspace_prev = aswl_control_workspace_prev,
	.list_windows = aswl_control_list_windows,
	.focus_window = aswl_control_focus_window,
	.close_window = aswl_control_close_window,
	.move_window_to_workspace = aswl_control_move_window_to_workspace,
	.toggle_fullscreen = aswl_control_toggle_fullscreen,
	.toggle_maximized = aswl_control_toggle_maximized,
};

static void aswl_control_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct aswl_server *server = data;
	uint32_t v = version > 6 ? 6 : version;
	struct wl_resource *res = wl_resource_create(client, &afterstep_control_v1_interface, v, id);
	if (res == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	struct aswl_control_client *cc = calloc(1, sizeof(*cc));
	if (cc == NULL) {
		wl_client_post_no_memory(client);
		wl_resource_destroy(res);
		return;
	}

	cc->server = server;
	cc->resource = res;
	wl_list_insert(server->control_clients.prev, &cc->link);

	wl_resource_set_implementation(res, &aswl_control_impl, cc, aswl_control_resource_destroy);
	if (v >= 3)
		afterstep_control_v1_send_workspace_state(res, server->current_workspace, server->workspace_count);
	send_output_state(server, res);
	if (v >= 4)
		send_window_list_snapshot(server, res);
}

static bool str_ieq(const char *a, const char *b)
{
	if (a == NULL || b == NULL)
		return false;
	while (*a != '\0' && *b != '\0') {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return false;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static int str_icmp(const char *a, const char *b)
{
	if (a == NULL)
		a = "";
	if (b == NULL)
		b = "";
	while (*a != '\0' && *b != '\0') {
		int da = tolower((unsigned char)*a);
		int db = tolower((unsigned char)*b);
		if (da != db)
			return da - db;
		a++;
		b++;
	}
	return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static bool str_contains_case_insensitive(const char *haystack, const char *needle)
{
	if (haystack == NULL || needle == NULL)
		return false;
	if (needle[0] == '\0')
		return false;

	for (const char *h = haystack; *h != '\0'; h++) {
		if (tolower((unsigned char)*h) != tolower((unsigned char)needle[0]))
			continue;

		const char *hh = h;
		const char *nn = needle;
		while (*hh != '\0' && *nn != '\0') {
			if (tolower((unsigned char)*hh) != tolower((unsigned char)*nn))
				break;
			hh++;
			nn++;
		}
		if (*nn == '\0')
			return true;
	}

	return false;
}

static char *lstrip(char *s)
{
	if (s == NULL)
		return NULL;
	while (*s != '\0' && isspace((unsigned char)*s))
		s++;
	return s;
}

static void rstrip_inplace(char *s)
{
	if (s == NULL)
		return;
	size_t len = strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1]))
		s[--len] = '\0';
}

static void aswl_dock_config_free_rules(struct aswl_dock_config *dock)
{
	if (dock == NULL)
		return;
	for (size_t i = 0; i < dock->rule_count; i++)
		free(dock->rules[i]);
	free(dock->rules);
	dock->rules = NULL;
	dock->rule_count = 0;
}

static enum aswl_dock_anchor aswl_dock_anchor_parse(const char *s, enum aswl_dock_anchor fallback)
{
	if (s == NULL)
		return fallback;
	while (*s != '\0' && isspace((unsigned char)*s))
		s++;

	char token[32];
	size_t n = 0;
	while (*s != '\0' && !isspace((unsigned char)*s) && n + 1 < sizeof(token)) {
		char c = (char)tolower((unsigned char)*s);
		if (c == '_')
			c = '-';
		token[n++] = c;
		s++;
	}
	token[n] = '\0';

	if (strcmp(token, "bottom-left") == 0 || strcmp(token, "bl") == 0)
		return ASWL_DOCK_ANCHOR_BOTTOM_LEFT;
	if (strcmp(token, "bottom-right") == 0 || strcmp(token, "br") == 0)
		return ASWL_DOCK_ANCHOR_BOTTOM_RIGHT;
	if (strcmp(token, "top-left") == 0 || strcmp(token, "tl") == 0)
		return ASWL_DOCK_ANCHOR_TOP_LEFT;
	if (strcmp(token, "top-right") == 0 || strcmp(token, "tr") == 0)
		return ASWL_DOCK_ANCHOR_TOP_RIGHT;

	return fallback;
}

static enum aswl_dock_flow aswl_dock_flow_parse(const char *s, enum aswl_dock_flow fallback)
{
	if (s == NULL)
		return fallback;
	while (*s != '\0' && isspace((unsigned char)*s))
		s++;

	char token[16];
	size_t n = 0;
	while (*s != '\0' && !isspace((unsigned char)*s) && n + 1 < sizeof(token)) {
		token[n++] = (char)tolower((unsigned char)*s);
		s++;
	}
	token[n] = '\0';

	if (strcmp(token, "row") == 0 || strcmp(token, "rows") == 0 || strcmp(token, "h") == 0 ||
	    strcmp(token, "horizontal") == 0)
		return ASWL_DOCK_FLOW_ROW;
	if (strcmp(token, "col") == 0 || strcmp(token, "cols") == 0 || strcmp(token, "column") == 0 ||
	    strcmp(token, "columns") == 0 || strcmp(token, "v") == 0 || strcmp(token, "vertical") == 0)
		return ASWL_DOCK_FLOW_COLUMN;

	return fallback;
}

static enum aswl_dock_order_mode aswl_dock_order_parse(const char *s, enum aswl_dock_order_mode fallback)
{
	if (s == NULL)
		return fallback;
	while (*s != '\0' && isspace((unsigned char)*s))
		s++;

	char token[16];
	size_t n = 0;
	while (*s != '\0' && !isspace((unsigned char)*s) && n + 1 < sizeof(token)) {
		token[n++] = (char)tolower((unsigned char)*s);
		s++;
	}
	token[n] = '\0';

	if (strcmp(token, "create") == 0 || strcmp(token, "created") == 0)
		return ASWL_DOCK_ORDER_CREATE;
	if (strcmp(token, "title") == 0)
		return ASWL_DOCK_ORDER_TITLE;
	if (strcmp(token, "class") == 0 || strcmp(token, "app") == 0 || strcmp(token, "appid") == 0 ||
	    strcmp(token, "app_id") == 0)
		return ASWL_DOCK_ORDER_CLASS;
	if (strcmp(token, "config") == 0 || strcmp(token, "rules") == 0)
		return ASWL_DOCK_ORDER_CONFIG;

	return fallback;
}

static char *aswl_dock_rules_path_default(void)
{
	const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	if (xdg_config_home != NULL && xdg_config_home[0] != '\0')
		return xstrdup_printf("%s/afterstep/aswlcomp.dockapps", xdg_config_home);

	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0')
		return xstrdup_printf("%s/.config/afterstep/aswlcomp.dockapps", home);

	return NULL;
}

static char *aswl_dock_rules_path_resolve(void)
{
	const char *override = getenv("ASWLCOMP_DOCKAPPS");
	if (override != NULL && override[0] != '\0')
		return strdup(override);

	override = getenv("ASWLCOMP_DOCK_RULES");
	if (override != NULL && override[0] != '\0')
		return strdup(override);

	return aswl_dock_rules_path_default();
}

static bool aswl_dock_config_load_rules(struct aswl_dock_config *dock, const char *path)
{
	if (dock == NULL || path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	char **rules = NULL;
	size_t count = 0;
	size_t cap = 0;

	char *line = NULL;
	size_t line_cap = 0;
	while (getline(&line, &line_cap, fp) != -1) {
		rstrip_inplace(line);
		char *s = lstrip(line);
		if (s == NULL || s[0] == '\0' || s[0] == '#' || s[0] == ';')
			continue;

		if (count == cap) {
			size_t new_cap = cap > 0 ? cap * 2 : 16;
			char **new_rules = realloc(rules, new_cap * sizeof(*new_rules));
			if (new_rules == NULL)
				break;
			rules = new_rules;
			cap = new_cap;
		}

		rules[count] = strdup(s);
		if (rules[count] == NULL)
			break;
		count++;
	}

	free(line);
	fclose(fp);

	if (count == 0) {
		for (size_t i = 0; i < count; i++)
			free(rules[i]);
		free(rules);
		return false;
	}

	aswl_dock_config_free_rules(dock);
	dock->rules = rules;
	dock->rule_count = count;
	return true;
}

static void aswl_dock_config_init(struct aswl_dock_config *dock)
{
	if (dock == NULL)
		return;
	memset(dock, 0, sizeof(*dock));
	dock->anchor = ASWL_DOCK_ANCHOR_BOTTOM_LEFT;
	dock->flow = ASWL_DOCK_FLOW_ROW;
	dock->order = ASWL_DOCK_ORDER_CREATE;
	dock->pad = 12;
	dock->spacing = 8;
	dock->max_dim = 512;

	const char *anchor = getenv("ASWLCOMP_DOCK_ANCHOR");
	if (anchor != NULL && anchor[0] != '\0')
		dock->anchor = aswl_dock_anchor_parse(anchor, dock->anchor);

	const char *flow = getenv("ASWLCOMP_DOCK_FLOW");
	if (flow != NULL && flow[0] != '\0')
		dock->flow = aswl_dock_flow_parse(flow, dock->flow);

	const char *order = getenv("ASWLCOMP_DOCK_ORDER");
	if (order != NULL && order[0] != '\0')
		dock->order = aswl_dock_order_parse(order, dock->order);

	const char *pad = getenv("ASWLCOMP_DOCK_PAD");
	if (pad != NULL && pad[0] != '\0') {
		char *end = NULL;
		long v = strtol(pad, &end, 10);
		if (end != pad && *end == '\0')
			dock->pad = clamp_int((int)v, 0, 512);
	}

	const char *spacing = getenv("ASWLCOMP_DOCK_SPACING");
	if (spacing != NULL && spacing[0] != '\0') {
		char *end = NULL;
		long v = strtol(spacing, &end, 10);
		if (end != spacing && *end == '\0')
			dock->spacing = clamp_int((int)v, 0, 256);
	}

	const char *max_dim = getenv("ASWLCOMP_DOCK_MAX_DIM");
	if (max_dim != NULL && max_dim[0] != '\0') {
		char *end = NULL;
		long v = strtol(max_dim, &end, 10);
		if (end != max_dim && *end == '\0')
			dock->max_dim = (uint16_t)clamp_int((int)v, 16, 4096);
	}

	dock->rules_path = aswl_dock_rules_path_resolve();
	if (dock->rules_path != NULL && access(dock->rules_path, R_OK) == 0)
		(void)aswl_dock_config_load_rules(dock, dock->rules_path);
}

static void aswl_dock_config_destroy(struct aswl_dock_config *dock)
{
	if (dock == NULL)
		return;
	aswl_dock_config_free_rules(dock);
	free(dock->rules_path);
	dock->rules_path = NULL;
}

static bool parse_modifiers(char *mods_str, uint32_t *mods_out)
{
	if (mods_out == NULL)
		return false;
	*mods_out = 0;

	if (mods_str == NULL || mods_str[0] == '\0')
		return true;

	char *saveptr = NULL;
	for (char *tok = strtok_r(mods_str, "+", &saveptr); tok != NULL; tok = strtok_r(NULL, "+", &saveptr)) {
		tok = lstrip(tok);
		rstrip_inplace(tok);
		if (tok[0] == '\0')
			continue;

		if (str_ieq(tok, "alt") || str_ieq(tok, "mod1")) {
			*mods_out |= WLR_MODIFIER_ALT;
			continue;
		}
		if (str_ieq(tok, "ctrl") || str_ieq(tok, "control")) {
			*mods_out |= WLR_MODIFIER_CTRL;
			continue;
		}
		if (str_ieq(tok, "shift")) {
			*mods_out |= WLR_MODIFIER_SHIFT;
			continue;
		}
		if (str_ieq(tok, "logo") || str_ieq(tok, "super") || str_ieq(tok, "mod4")) {
			*mods_out |= WLR_MODIFIER_LOGO;
			continue;
		}

		fprintf(stderr, "aswlcomp: bind: unknown modifier: %s\n", tok);
		return false;
	}

	return true;
}

static void aswl_set_opt_string(char **dst, const char *value)
{
	if (dst == NULL)
		return;

	if (value == NULL || value[0] == '\0' || str_ieq(value, "default") || str_ieq(value, "none") || str_ieq(value, "null")) {
		free(*dst);
		*dst = NULL;
		return;
	}

	char *dup = strdup(value);
	if (dup == NULL)
		return;

	free(*dst);
	*dst = dup;
}

static void aswl_apply_keyboard_device_config(struct aswl_server *server, struct aswl_keyboard *keyboard)
{
	if (server == NULL || keyboard == NULL || keyboard->wlr_keyboard == NULL)
		return;

	if (!keyboard->has_keymap) {
		if (server->xkb_context == NULL)
			server->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

		if (server->xkb_context != NULL) {
			struct xkb_rule_names names = { 0 };
			names.rules = server->xkb_rules;
			names.model = server->xkb_model;
			names.layout = server->xkb_layout;
			names.variant = server->xkb_variant;
			names.options = server->xkb_options;

			const struct xkb_rule_names *names_ptr = NULL;
			if (names.rules != NULL || names.model != NULL || names.layout != NULL || names.variant != NULL || names.options != NULL)
				names_ptr = &names;

			struct xkb_keymap *keymap = xkb_keymap_new_from_names(server->xkb_context, names_ptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
			if (keymap != NULL) {
				(void)wlr_keyboard_set_keymap(keyboard->wlr_keyboard, keymap);
				xkb_keymap_unref(keymap);
			} else if (names_ptr != NULL) {
				fprintf(stderr, "aswlcomp: xkb: failed to compile keymap (layout=%s)\n",
				        names.layout != NULL ? names.layout : "");
			}
		}
	}

	int rate = server->repeat_rate;
	int delay = server->repeat_delay;
	if (rate < 0)
		rate = 0;
	if (delay < 0)
		delay = 0;
	wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard, rate, delay);
}

static void aswl_apply_keyboard_config(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_keyboard *keyboard;
	wl_list_for_each(keyboard, &server->keyboards, link) {
		aswl_apply_keyboard_device_config(server, keyboard);
	}
}

static bool aswl_parse_bool(const char *s, bool *out)
{
	if (out == NULL)
		return false;
	if (s == NULL)
		return false;

	if (strcmp(s, "1") == 0 || str_ieq(s, "true") || str_ieq(s, "yes") || str_ieq(s, "on") ||
	    str_ieq(s, "enable") || str_ieq(s, "enabled")) {
		*out = true;
		return true;
	}
	if (strcmp(s, "0") == 0 || str_ieq(s, "false") || str_ieq(s, "no") || str_ieq(s, "off") ||
	    str_ieq(s, "disable") || str_ieq(s, "disabled")) {
		*out = false;
		return true;
	}

	return false;
}

static void aswl_apply_pointer_device_config(struct aswl_server *server, struct wlr_input_device *device)
{
	if (server == NULL || device == NULL)
		return;

	if (!server->have_pointer_accel && !server->have_tap_to_click)
		return;

	if (!wlr_input_device_is_libinput(device))
		return;

	struct libinput_device *lid = wlr_libinput_get_device_handle(device);
	if (lid == NULL)
		return;

	if (server->have_pointer_accel) {
		double speed = server->pointer_accel;
		if (speed < -1.0)
			speed = -1.0;
		if (speed > 1.0)
			speed = 1.0;
		(void)libinput_device_config_accel_set_speed(lid, speed);
	}

	if (server->have_tap_to_click) {
		enum libinput_config_tap_state state =
			server->tap_to_click ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED;
		(void)libinput_device_config_tap_set_enabled(lid, state);
	}
}

static void aswl_apply_pointer_config(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_pointer_device *pd;
	wl_list_for_each(pd, &server->pointer_devices, link) {
		if (pd == NULL)
			continue;
		aswl_apply_pointer_device_config(server, pd->device);
	}
}

static void handle_pointer_device_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_pointer_device *pd = wl_container_of(listener, pd, destroy);
	if (pd == NULL)
		return;

	wl_list_remove(&pd->destroy.link);
	wl_list_remove(&pd->link);
	free(pd);
}

static void add_binding_exec(struct aswl_server *server, uint32_t mods, xkb_keysym_t keysym, const char *command)
{
	if (server == NULL || command == NULL || command[0] == '\0' || keysym == XKB_KEY_NoSymbol)
		return;

	struct aswl_binding *b = calloc(1, sizeof(*b));
	if (b == NULL)
		return;

	b->mods = mods;
	b->keysym = keysym;
	b->action = ASWL_BINDING_EXEC;
	b->command = strdup(command);
	if (b->command == NULL) {
		free(b);
		return;
	}

	wl_list_insert(server->bindings.prev, &b->link);
}

static void add_binding_action(struct aswl_server *server, uint32_t mods, xkb_keysym_t keysym, int action)
{
	if (server == NULL || keysym == XKB_KEY_NoSymbol)
		return;

	struct aswl_binding *b = calloc(1, sizeof(*b));
	if (b == NULL)
		return;

	b->mods = mods;
	b->keysym = keysym;
	b->action = action;
	wl_list_insert(server->bindings.prev, &b->link);
}

static void add_binding_workspace_set(struct aswl_server *server, uint32_t mods, xkb_keysym_t keysym, uint32_t workspace)
{
	if (server == NULL || keysym == XKB_KEY_NoSymbol || workspace < 1)
		return;

	struct aswl_binding *b = calloc(1, sizeof(*b));
	if (b == NULL)
		return;

	b->mods = mods;
	b->keysym = keysym;
	b->action = ASWL_BINDING_WORKSPACE_SET;
	b->workspace = workspace;
	wl_list_insert(server->bindings.prev, &b->link);
}

static const char *binding_action_name(int action)
{
	switch (action) {
	case ASWL_BINDING_QUIT:
		return "quit";
	case ASWL_BINDING_CLOSE_FOCUSED:
		return "close_focused";
	case ASWL_BINDING_FOCUS_NEXT:
		return "focus_next";
	case ASWL_BINDING_FOCUS_PREV:
		return "focus_prev";
	case ASWL_BINDING_WORKSPACE_SET:
		return "workspace";
	case ASWL_BINDING_WORKSPACE_NEXT:
		return "workspace_next";
	case ASWL_BINDING_WORKSPACE_PREV:
		return "workspace_prev";
	case ASWL_BINDING_TOGGLE_FULLSCREEN:
		return "toggle_fullscreen";
	case ASWL_BINDING_TOGGLE_MAXIMIZED:
		return "toggle_maximized";
	case ASWL_BINDING_LOCK:
		return "lock";
	default:
		return "exec";
	}
}

static bool default_autostart_path(char *out, size_t out_size)
{
	if (out == NULL || out_size == 0)
		return false;

	const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");

	int n = -1;
	if (xdg_config_home != NULL && xdg_config_home[0] != '\0') {
		n = snprintf(out, out_size, "%s/afterstep/aswlcomp.autostart", xdg_config_home);
	} else if (home != NULL && home[0] != '\0') {
		n = snprintf(out, out_size, "%s/.config/afterstep/aswlcomp.autostart", home);
	}

	if (n < 0 || (size_t)n >= out_size)
		return false;
	return true;
}

static void load_config_file(struct aswl_server *server, const char *path, bool log_missing)
{
	if (path == NULL || path[0] == '\0')
		return;

	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (log_missing)
			fprintf(stderr, "aswlcomp: autostart: %s: %s\n", path, strerror(errno));
		return;
	}

	char *line = NULL;
	size_t cap = 0;
	while (getline(&line, &cap, fp) >= 0) {
		rstrip_inplace(line);
		char *cmd = lstrip(line);
		if (cmd == NULL || cmd[0] == '\0' || cmd[0] == '#')
			continue;

		if (strncmp(cmd, "set", 3) == 0 && isspace((unsigned char)cmd[3])) {
			char *rest = lstrip(cmd + 3);
			if (rest == NULL || rest[0] == '\0') {
				fprintf(stderr, "aswlcomp: config: set: missing key\n");
				continue;
			}

			char *key = rest;
			while (*rest != '\0' && !isspace((unsigned char)*rest))
				rest++;
			if (*rest == '\0') {
				fprintf(stderr, "aswlcomp: config: set: missing value for %s\n", key);
				continue;
			}
			*rest++ = '\0';
			char *val = lstrip(rest);
			rstrip_inplace(val);

			if (val == NULL || val[0] == '\0') {
				fprintf(stderr, "aswlcomp: config: set: missing value for %s\n", key);
				continue;
			}

			if (str_ieq(key, "xkb_layout") || str_ieq(key, "keyboard_layout")) {
				aswl_set_opt_string(&server->xkb_layout, val);
				fprintf(stderr, "aswlcomp: config: xkb_layout=%s\n", server->xkb_layout != NULL ? server->xkb_layout : "(default)");
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "xkb_variant") || str_ieq(key, "keyboard_variant")) {
				aswl_set_opt_string(&server->xkb_variant, val);
				fprintf(stderr, "aswlcomp: config: xkb_variant=%s\n", server->xkb_variant != NULL ? server->xkb_variant : "(default)");
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "xkb_options") || str_ieq(key, "keyboard_options")) {
				aswl_set_opt_string(&server->xkb_options, val);
				fprintf(stderr, "aswlcomp: config: xkb_options=%s\n", server->xkb_options != NULL ? server->xkb_options : "(default)");
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "xkb_model") || str_ieq(key, "keyboard_model")) {
				aswl_set_opt_string(&server->xkb_model, val);
				fprintf(stderr, "aswlcomp: config: xkb_model=%s\n", server->xkb_model != NULL ? server->xkb_model : "(default)");
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "xkb_rules") || str_ieq(key, "keyboard_rules")) {
				aswl_set_opt_string(&server->xkb_rules, val);
				fprintf(stderr, "aswlcomp: config: xkb_rules=%s\n", server->xkb_rules != NULL ? server->xkb_rules : "(default)");
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "repeat_rate") || str_ieq(key, "keyboard_repeat_rate")) {
				char *end = NULL;
				long v = strtol(val, &end, 10);
				if (end == val || (end != NULL && *end != '\0') || v < 0 || v > 2000) {
					fprintf(stderr, "aswlcomp: config: repeat_rate: bad value: %s\n", val);
					continue;
				}
				server->repeat_rate = (int)v;
				fprintf(stderr, "aswlcomp: config: repeat_rate=%d\n", server->repeat_rate);
				aswl_apply_keyboard_config(server);
				continue;
			}
			if (str_ieq(key, "repeat_delay") || str_ieq(key, "keyboard_repeat_delay")) {
				char *end = NULL;
				long v = strtol(val, &end, 10);
				if (end == val || (end != NULL && *end != '\0') || v < 0 || v > 60000) {
					fprintf(stderr, "aswlcomp: config: repeat_delay: bad value: %s\n", val);
					continue;
				}
				server->repeat_delay = (int)v;
				fprintf(stderr, "aswlcomp: config: repeat_delay=%d\n", server->repeat_delay);
				aswl_apply_keyboard_config(server);
				continue;
			}

			if (str_ieq(key, "pointer_accel") || str_ieq(key, "pointer_acceleration")) {
				if (str_ieq(val, "default") || str_ieq(val, "none") || str_ieq(val, "null")) {
					server->have_pointer_accel = false;
					fprintf(stderr, "aswlcomp: config: pointer_accel=(default)\n");
					aswl_apply_pointer_config(server);
					continue;
				}

				char *end = NULL;
				double v = strtod(val, &end);
				if (end == val || end == NULL || *end != '\0' || v < -1.0 || v > 1.0) {
					fprintf(stderr, "aswlcomp: config: pointer_accel: bad value (use -1..1): %s\n", val);
					continue;
				}

				server->have_pointer_accel = true;
				server->pointer_accel = v;
				fprintf(stderr, "aswlcomp: config: pointer_accel=%.3f\n", server->pointer_accel);
				aswl_apply_pointer_config(server);
				continue;
			}

			if (str_ieq(key, "tap_to_click") || str_ieq(key, "tap")) {
				if (str_ieq(val, "default") || str_ieq(val, "none") || str_ieq(val, "null")) {
					server->have_tap_to_click = false;
					fprintf(stderr, "aswlcomp: config: tap_to_click=(default)\n");
					aswl_apply_pointer_config(server);
					continue;
				}

				bool enabled = false;
				if (!aswl_parse_bool(val, &enabled)) {
					fprintf(stderr, "aswlcomp: config: tap_to_click: bad value: %s\n", val);
					continue;
				}

				server->have_tap_to_click = true;
				server->tap_to_click = enabled;
				fprintf(stderr, "aswlcomp: config: tap_to_click=%d\n", server->tap_to_click ? 1 : 0);
				aswl_apply_pointer_config(server);
				continue;
			}

			fprintf(stderr, "aswlcomp: config: unknown key: %s\n", key);
			continue;
		}

		if (strncmp(cmd, "bind", 4) == 0 && isspace((unsigned char)cmd[4])) {
			char *rest = lstrip(cmd + 4);
			if (rest == NULL || rest[0] == '\0') {
				fprintf(stderr, "aswlcomp: bind: missing binding\n");
				continue;
			}

			char *combo = rest;
			while (*rest != '\0' && !isspace((unsigned char)*rest))
				rest++;
			if (*rest == '\0') {
				fprintf(stderr, "aswlcomp: bind: missing command\n");
				continue;
			}
			*rest++ = '\0';
			char *bind_cmd = lstrip(rest);
			rstrip_inplace(bind_cmd);
			if (bind_cmd[0] == '\0') {
				fprintf(stderr, "aswlcomp: bind: missing command\n");
				continue;
			}

			char *key_str = combo;
			char *mods_str = NULL;
			char *plus = strrchr(combo, '+');
			if (plus != NULL) {
				*plus = '\0';
				mods_str = combo;
				key_str = plus + 1;
			}

			key_str = lstrip(key_str);
			rstrip_inplace(key_str);
			if (key_str[0] == '\0') {
				fprintf(stderr, "aswlcomp: bind: missing key\n");
				continue;
			}

			xkb_keysym_t keysym = xkb_keysym_from_name(key_str, XKB_KEYSYM_CASE_INSENSITIVE);
			if (keysym == XKB_KEY_NoSymbol) {
				fprintf(stderr, "aswlcomp: bind: unknown keysym: %s\n", key_str);
				continue;
			}

			uint32_t mods = 0;
			if (mods_str != NULL && mods_str[0] != '\0') {
				if (!parse_modifiers(mods_str, &mods))
					continue;
			}

				if (strncmp(bind_cmd, "exec", 4) == 0 && isspace((unsigned char)bind_cmd[4])) {
					bind_cmd = lstrip(bind_cmd + 4);
					if (bind_cmd[0] == '\0') {
						fprintf(stderr, "aswlcomp: bind: missing command\n");
						continue;
					}
					fprintf(stderr, "aswlcomp: bind: mods=0x%x key=%s exec=%s\n", mods, key_str, bind_cmd);
					add_binding_exec(server, mods, keysym, bind_cmd);
					continue;
				}

				if ((strncmp(bind_cmd, "workspace", 9) == 0 && isspace((unsigned char)bind_cmd[9])) ||
				    (strncmp(bind_cmd, "ws", 2) == 0 && isspace((unsigned char)bind_cmd[2]))) {
					char *num = bind_cmd;
					if (strncmp(bind_cmd, "workspace", 9) == 0)
						num = lstrip(bind_cmd + 9);
					else
						num = lstrip(bind_cmd + 2);

					if (num == NULL || num[0] == '\0') {
						fprintf(stderr, "aswlcomp: bind: workspace: missing number\n");
						continue;
					}

					char *end = NULL;
					unsigned long ws = strtoul(num, &end, 10);
					if (end == num || (end != NULL && *end != '\0') || ws < 1 || ws > 1000) {
						fprintf(stderr, "aswlcomp: bind: workspace: bad number: %s\n", num);
						continue;
					}

					fprintf(stderr, "aswlcomp: bind: mods=0x%x key=%s workspace=%lu\n", mods, key_str, ws);
					add_binding_workspace_set(server, mods, keysym, (uint32_t)ws);
					continue;
				}

				int action = ASWL_BINDING_EXEC;
				if (str_ieq(bind_cmd, "quit") || str_ieq(bind_cmd, "exit")) {
					action = ASWL_BINDING_QUIT;
				} else if (str_ieq(bind_cmd, "close") || str_ieq(bind_cmd, "close_focused")) {
					action = ASWL_BINDING_CLOSE_FOCUSED;
				} else if (str_ieq(bind_cmd, "focus_next") || str_ieq(bind_cmd, "next")) {
					action = ASWL_BINDING_FOCUS_NEXT;
				} else if (str_ieq(bind_cmd, "focus_prev") || str_ieq(bind_cmd, "prev")) {
					action = ASWL_BINDING_FOCUS_PREV;
					} else if (str_ieq(bind_cmd, "workspace_next") || str_ieq(bind_cmd, "ws_next") || str_ieq(bind_cmd, "ws+")) {
						action = ASWL_BINDING_WORKSPACE_NEXT;
					} else if (str_ieq(bind_cmd, "workspace_prev") || str_ieq(bind_cmd, "ws_prev") || str_ieq(bind_cmd, "ws-")) {
						action = ASWL_BINDING_WORKSPACE_PREV;
					} else if (str_ieq(bind_cmd, "fullscreen") || str_ieq(bind_cmd, "toggle_fullscreen")) {
						action = ASWL_BINDING_TOGGLE_FULLSCREEN;
					} else if (str_ieq(bind_cmd, "maximized") || str_ieq(bind_cmd, "maximize") ||
					           str_ieq(bind_cmd, "toggle_maximized") || str_ieq(bind_cmd, "toggle_maximize")) {
						action = ASWL_BINDING_TOGGLE_MAXIMIZED;
					} else if (str_ieq(bind_cmd, "lock") || str_ieq(bind_cmd, "lock_now") || str_ieq(bind_cmd, "session_lock")) {
						action = ASWL_BINDING_LOCK;
					}

				if (action == ASWL_BINDING_EXEC) {
					fprintf(stderr, "aswlcomp: bind: missing exec prefix: %s\n", bind_cmd);
					continue;
			}

			fprintf(stderr, "aswlcomp: bind: mods=0x%x key=%s action=%s\n", mods, key_str, binding_action_name(action));
			add_binding_action(server, mods, keysym, action);
			continue;
		}

		if (strncmp(cmd, "exec", 4) == 0 && isspace((unsigned char)cmd[4]))
			cmd = lstrip(cmd + 4);

		if (cmd[0] == '\0')
			continue;

		fprintf(stderr, "aswlcomp: autostart: %s\n", cmd);
		spawn_command(cmd);
	}

	free(line);
	fclose(fp);
}

static struct wlr_surface *surface_at(struct aswl_server *server, double lx, double ly, double *sx, double *sy)
{
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER)
		return NULL;

	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	if (scene_buffer == NULL)
		return NULL;

	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (scene_surface == NULL)
		return NULL;

	return scene_surface->surface;
}

static struct aswl_view *view_from_wlr_surface(struct wlr_surface *surface)
{
	struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(surface);
	while (xdg_surface != NULL && xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		struct wlr_surface *parent = xdg_surface->popup->parent;
		xdg_surface = parent != NULL ? wlr_xdg_surface_try_from_wlr_surface(parent) : NULL;
	}
	if (xdg_surface != NULL && xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return xdg_surface->data;

	struct wlr_xwayland_surface *xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface);
	if (xsurface != NULL)
		return xsurface->data;

	return NULL;
}

static void focus_view(struct aswl_view *view, struct wlr_surface *surface)
{
	if (view == NULL || view->server == NULL)
		return;

	struct aswl_server *server = view->server;
	if (server->session_locked)
		return;
	struct aswl_view *old_focus = server->focused_view;
	bool steal_focus = !view_is_suite_popup(view);

	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(server->views.prev, &view->link);

	if (steal_focus && old_focus != view) {
		if (old_focus != NULL) {
			if (old_focus->xdg_surface != NULL && old_focus->xdg_surface->toplevel != NULL) {
				(void)wlr_xdg_toplevel_set_activated(old_focus->xdg_surface->toplevel, false);
			} else if (old_focus->xwayland_surface != NULL) {
				wlr_xwayland_surface_activate(old_focus->xwayland_surface, false);
			}
		}
		server->focused_view = view;
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
			(void)wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, true);
		} else if (view->xwayland_surface != NULL) {
			wlr_xwayland_surface_activate(view->xwayland_surface, true);
		}

		view_update_toplevel_protocols(old_focus);
		view_update_toplevel_protocols(view);
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard != NULL) {
		if (surface == NULL) {
			if (view->xdg_surface != NULL)
				surface = view->xdg_surface->surface;
			else if (view->xwayland_surface != NULL)
				surface = view->xwayland_surface->surface;
		}
		if (surface != NULL) {
			wlr_seat_keyboard_notify_enter(server->seat,
			                              surface,
			                              keyboard->keycodes,
			                              keyboard->num_keycodes,
			                              &keyboard->modifiers);
			aswl_ime_set_focus(server, surface);
		}

		if (view->xwayland_surface != NULL)
			wlr_xwayland_surface_offer_focus(view->xwayland_surface);
	}

	if (steal_focus && old_focus != view) {
		if (old_focus != NULL)
			broadcast_window_state(server, old_focus);
		broadcast_window_state(server, view);
	}

	if (steal_focus && old_focus != view) {
		view_update_decorations(old_focus);
		view_update_decorations(view);
	}
}

static void close_focused_view(struct aswl_server *server)
{
	if (server == NULL || server->focused_view == NULL)
		return;

	struct aswl_view *view = server->focused_view;
	if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
		wlr_xdg_toplevel_send_close(view->xdg_surface->toplevel);
	} else if (view->xwayland_surface != NULL) {
		wlr_xwayland_surface_close(view->xwayland_surface);
	}
}

static bool view_visible(struct aswl_server *server, struct aswl_view *view)
{
	if (server == NULL || view == NULL)
		return false;
	if (!view->mapped || view->scene_tree == NULL)
		return false;
	if (view->is_dock)
		return false;
	return view->workspace == server->current_workspace;
}

static void focus_next_view(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view_visible(server, view))
			continue;
		if (view == server->focused_view)
			continue;
		focus_view(view, NULL);
		return;
	}
}

static void focus_prev_view(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_view *view;
	wl_list_for_each_reverse(view, &server->views, link) {
		if (!view_visible(server, view))
			continue;
		if (view == server->focused_view)
			continue;
		focus_view(view, NULL);
		return;
	}
}

static void focus_topmost_view(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_view *view;
	wl_list_for_each_reverse(view, &server->views, link) {
		if (!view_visible(server, view))
			continue;
		focus_view(view, NULL);
		return;
	}
}

static uint32_t normalize_workspace(struct aswl_server *server, uint32_t workspace)
{
	if (workspace < 1)
		workspace = 1;

	if (server != NULL && server->workspace_count > 0 && workspace > server->workspace_count) {
		workspace = ((workspace - 1) % server->workspace_count) + 1;
	}

	return workspace;
}

static void set_workspace(struct aswl_server *server, uint32_t workspace)
{
	if (server == NULL)
		return;

	workspace = normalize_workspace(server, workspace);
	if (server->current_workspace == workspace)
		return;

	fprintf(stderr, "aswlcomp: workspace: %u -> %u\n", server->current_workspace, workspace);
	server->current_workspace = workspace;

	if (server->grabbed_view != NULL && !server->grabbed_view->is_dock && server->grabbed_view->workspace != workspace)
		end_interactive(server);

	if (server->focused_view != NULL && !server->focused_view->is_dock && server->focused_view->workspace != workspace) {
		struct aswl_view *old = server->focused_view;
		if (old->xdg_surface != NULL && old->xdg_surface->toplevel != NULL) {
			(void)wlr_xdg_toplevel_set_activated(old->xdg_surface->toplevel, false);
		} else if (old->xwayland_surface != NULL) {
			wlr_xwayland_surface_activate(old->xwayland_surface, false);
		}
		server->focused_view = NULL;
		view_update_toplevel_protocols(old);
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		broadcast_window_state(server, old);
	}

	struct aswl_view *view;
	struct aswl_view *tmp;
	wl_list_for_each_safe(view, tmp, &server->views, link) {
		if (view->scene_tree == NULL)
			continue;
		bool enabled = view->mapped && (view->is_dock || view->workspace == server->current_workspace);
		wlr_scene_node_set_enabled(&view->scene_tree->node, enabled);
		if (enabled && !view->is_dock)
			place_view(view);
	}

	arrange_dock_views(server);
	focus_topmost_view(server);
	broadcast_workspace_state(server);
	aswl_state_save(server);
}

static void workspace_next(struct aswl_server *server)
{
	if (server == NULL)
		return;

	uint32_t count = server->workspace_count > 0 ? server->workspace_count : 1;
	uint32_t next = server->current_workspace + 1;
	if (next > count)
		next = 1;
	set_workspace(server, next);
}

static void workspace_prev(struct aswl_server *server)
{
	if (server == NULL)
		return;

	uint32_t count = server->workspace_count > 0 ? server->workspace_count : 1;
	uint32_t prev = server->current_workspace > 1 ? server->current_workspace - 1 : count;
	set_workspace(server, prev);
}

static struct aswl_output *output_from_wlr_output(struct aswl_server *server, struct wlr_output *output)
{
	if (server == NULL || output == NULL)
		return NULL;

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out->wlr_output == output)
			return out;
	}
	return NULL;
}

static bool view_is_small_dock_candidate(struct aswl_view *view)
{
	if (view == NULL || view->xwayland_surface == NULL)
		return false;

	uint16_t max_dim = 512;
	if (view->server != NULL && view->server->dock.max_dim > 0)
		max_dim = view->server->dock.max_dim;

	/*
	 * Small dockapps should be tiny; treat 0 sizes as "unknown" and allow them
	 * (Xwayland may not have a size until after the first configure).
	 */
	uint16_t w = view->xwayland_surface->width;
	uint16_t h = view->xwayland_surface->height;
	if (w > 0 && w > max_dim)
		return false;
	if (h > 0 && h > max_dim)
		return false;

	return true;
}

static bool dock_rule_matches_view(const char *rule, struct aswl_view *view)
{
	if (rule == NULL || view == NULL)
		return false;

	while (*rule != '\0' && isspace((unsigned char)*rule))
		rule++;
	if (rule[0] == '\0')
		return false;

	const char *title = view_title(view);
	const char *app = view_app_id(view);

	if (strncmp(rule, "class=", 6) == 0 || strncmp(rule, "app_id=", 7) == 0 || strncmp(rule, "appid=", 6) == 0) {
		const char *val = strchr(rule, '=');
		if (val == NULL)
			return false;
		val++;
		while (*val != '\0' && isspace((unsigned char)*val))
			val++;
		return val[0] != '\0' && str_ieq(val, app);
	}

	if (strncmp(rule, "title=", 6) == 0) {
		const char *val = rule + 6;
		while (*val != '\0' && isspace((unsigned char)*val))
			val++;
		return val[0] != '\0' && str_contains_case_insensitive(title, val);
	}

	return str_contains_case_insensitive(app, rule) || str_contains_case_insensitive(title, rule);
}

static size_t dock_rule_rank(const struct aswl_dock_config *dock, struct aswl_view *view)
{
	if (dock == NULL || dock->rule_count == 0 || view == NULL)
		return SIZE_MAX;

	for (size_t i = 0; i < dock->rule_count; i++) {
		if (dock_rule_matches_view(dock->rules[i], view))
			return i;
	}
	return SIZE_MAX;
}

static bool view_is_xwayland_dockapp(struct aswl_view *view)
{
	if (view == NULL || view->xwayland_surface == NULL)
		return false;
	if (!view_is_small_dock_candidate(view))
		return false;

	if (wlr_xwayland_surface_has_window_type(view->xwayland_surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DOCK))
		return true;

	/*
	 * Historic "dockapp" convention: many dockapps set WM_CLASS to "DockApp"
	 * without also tagging themselves as an EWMH DOCK window.
	 */
	const char *klass = view_app_id(view);
	if (klass != NULL && klass[0] != '\0' && str_ieq(klass, "DockApp"))
		return true;

	/* Config-driven allowlist: treat matching small Xwayland windows as dockapps. */
	if (view->server != NULL && view->server->dock.rule_count > 0) {
		if (dock_rule_rank(&view->server->dock, view) != SIZE_MAX)
			return true;
	}

	return false;
}

static void view_maybe_mark_dock(struct aswl_view *view)
{
	if (view == NULL || view->is_dock)
		return;
	if (view->type != ASWL_VIEW_XWAYLAND)
		return;
	if (!view_is_xwayland_dockapp(view))
		return;

	view->is_dock = true;

	if (view->xwayland_surface != NULL) {
		wlr_xwayland_surface_set_sticky(view->xwayland_surface, true);
		wlr_xwayland_surface_set_skip_taskbar(view->xwayland_surface, true);
		wlr_xwayland_surface_set_skip_pager(view->xwayland_surface, true);
	}

	fprintf(stderr, "aswlcomp: dockapp: class=%s title=%s\n", view_app_id(view), view_title(view));
}

static void view_get_current_size(struct aswl_view *view, int *width, int *height)
{
	int w = 0;
	int h = 0;

	if (view != NULL && view->xwayland_surface != NULL) {
		w = view->xwayland_surface->width;
		h = view->xwayland_surface->height;
	}

	if (view != NULL && view->xdg_surface != NULL) {
		/*
		 * The scene-graph xdg_surface helper positions the node at the top-left
		 * corner of the *window geometry*, so use geometry size as our content
		 * size when possible. This avoids "double framed" looking margins when
		 * the client surface includes extra padding/shadows around the geometry.
		 */
		if (view->xdg_surface->geometry.width > 0)
			w = view->xdg_surface->geometry.width;
		if (view->xdg_surface->geometry.height > 0)
			h = view->xdg_surface->geometry.height;

		if ((w <= 0 || h <= 0) && view->xdg_surface->surface != NULL) {
			w = view->xdg_surface->surface->current.width;
			h = view->xdg_surface->surface->current.height;
		}
	}

	if (width != NULL)
		*width = w;
	if (height != NULL)
		*height = h;
}

static int clamp_int(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static void view_get_deco_metrics(struct aswl_view *view, int *border_out, int *title_h_out)
{
	int border = 2;
	int title_h = 24;

	const char *b_env = getenv("ASWLCOMP_DECOR_BORDER");
	if (b_env != NULL && b_env[0] != '\0') {
		char *end = NULL;
		long v = strtol(b_env, &end, 10);
		if (end != b_env && *end == '\0')
			border = (int)v;
	}

	const char *t_env = getenv("ASWLCOMP_DECOR_TITLE");
	if (t_env != NULL && t_env[0] != '\0') {
		char *end = NULL;
		long v = strtol(t_env, &end, 10);
		if (end != t_env && *end == '\0')
			title_h = (int)v;
	}

	border = clamp_int(border, 0, 64);
	title_h = clamp_int(title_h, 0, 128);

	if (view == NULL || view->is_dock) {
		border = 0;
		title_h = 0;
	}

	/* Suite popups/menus draw their own frame and should not get server-side decorations. */
	const char *app_id = view_app_id(view);
	if (app_id != NULL && strcmp(app_id, "afterstep.aswlmenu") == 0) {
		border = 0;
		title_h = 0;
	}

	if (border_out != NULL)
		*border_out = border;
	if (title_h_out != NULL)
		*title_h_out = title_h;
}

static void view_get_content_offset(struct aswl_view *view, int *ox, int *oy)
{
	int border = 0;
	int title_h = 0;
	view_get_deco_metrics(view, &border, &title_h);

	if (ox != NULL)
		*ox = border;
	if (oy != NULL)
		*oy = title_h;
}

static void view_get_frame_size(struct aswl_view *view, int *width, int *height)
{
	int cw = 0;
	int ch = 0;
	view_get_current_size(view, &cw, &ch);
	if (cw < 1)
		cw = 1;
	if (ch < 1)
		ch = 1;

	int border = 0;
	int title_h = 0;
	view_get_deco_metrics(view, &border, &title_h);

	int fw = cw + 2 * border;
	int fh = ch + title_h + border;

	if (width != NULL)
		*width = fw;
	if (height != NULL)
		*height = fh;
}

static bool view_create_frame_scene(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL)
		return false;
	if (view->scene_tree != NULL)
		return true;

	struct aswl_server *server = view->server;

	view->scene_tree = wlr_scene_tree_create(server->xdg_tree);
	if (view->scene_tree == NULL)
		return false;
	view->scene_tree->node.data = view;

	view->content_tree = wlr_scene_tree_create(view->scene_tree);
	if (view->content_tree == NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
		return false;
	}

	float border_color[4];
	aswl_argb_to_premul_f(server->theme.frame_border, border_color);

	view->deco_left = wlr_scene_rect_create(view->scene_tree, 1, 1, border_color);
	view->deco_right = wlr_scene_rect_create(view->scene_tree, 1, 1, border_color);
	view->deco_bottom = wlr_scene_rect_create(view->scene_tree, 1, 1, border_color);
	view->deco_titlebar = wlr_scene_buffer_create(view->scene_tree, NULL);
	if (view->deco_left == NULL || view->deco_right == NULL || view->deco_bottom == NULL || view->deco_titlebar == NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
		view->content_tree = NULL;
		view->deco_left = NULL;
		view->deco_right = NULL;
		view->deco_bottom = NULL;
		view->deco_titlebar = NULL;
		return false;
	}

	view_update_decorations(view);
	return true;
}

static void aswl_deco_icon_destroy(struct aswl_deco_icon *icon)
{
	if (icon == NULL)
		return;
	free(icon->argb);
	*icon = (struct aswl_deco_icon){ 0 };
}

static bool aswl_deco_icon_try_load(struct aswl_deco_icon *icon, const char *const specs[])
{
	if (icon == NULL)
		return false;
	if (icon->argb != NULL && icon->w > 0 && icon->h > 0)
		return true;
	if (specs == NULL)
		return false;

	for (size_t i = 0; specs[i] != NULL; i++) {
		uint32_t *argb = NULL;
		int w = 0;
		int h = 0;
		if (aswl_icon_load_argb(specs[i], &argb, &w, &h) && argb != NULL && w > 0 && h > 0) {
			free(icon->argb);
			icon->argb = argb;
			icon->w = w;
			icon->h = h;
			return true;
		}
		free(argb);
	}

	return false;
}

static void aswl_deco_assets_ensure(struct aswl_server *server)
{
	if (server == NULL || server->deco.loaded)
		return;

	/* Prefer look.DEFAULT title button assets; fall back to existing icons. */
	static const char *const kill_specs[] = { "default-kill-dark", "dots/abi-close", NULL };
	static const char *const shade_specs[] = { "default-shade-dark", NULL };
	static const char *const iconize_specs[] = { "default-iconize-dark", NULL };
	static const char *const menu_specs[] = { "default-menu-light", NULL };
	static const char *const switch_specs[] = { "default-switchwindow-light", NULL };
	static const char *const pin_specs[] = { "default-pin-light", NULL };

	(void)aswl_deco_icon_try_load(&server->deco.btn_kill, kill_specs);
	(void)aswl_deco_icon_try_load(&server->deco.btn_shade, shade_specs);
	(void)aswl_deco_icon_try_load(&server->deco.btn_iconize, iconize_specs);
	(void)aswl_deco_icon_try_load(&server->deco.btn_menu, menu_specs);
	(void)aswl_deco_icon_try_load(&server->deco.btn_switch, switch_specs);
	(void)aswl_deco_icon_try_load(&server->deco.btn_pin, pin_specs);

	server->deco.loaded = true;
}

static void aswl_deco_assets_destroy(struct aswl_deco_assets *deco)
{
	if (deco == NULL)
		return;
	aswl_deco_icon_destroy(&deco->btn_switch);
	aswl_deco_icon_destroy(&deco->btn_menu);
	aswl_deco_icon_destroy(&deco->btn_pin);
	aswl_deco_icon_destroy(&deco->btn_iconize);
	aswl_deco_icon_destroy(&deco->btn_shade);
	aswl_deco_icon_destroy(&deco->btn_kill);
	deco->loaded = false;
}

static struct wlr_buffer *view_render_titlebar_buffer(struct aswl_view *view, int frame_w, int title_h)
{
	if (view == NULL || view->server == NULL || frame_w <= 0 || title_h <= 0)
		return NULL;

	struct aswl_server *server = view->server;

	bool focused = server->focused_view == view;
	uint32_t bg = focused ? server->theme.frame_active_bg : server->theme.frame_inactive_bg;
	uint32_t fg = focused ? server->theme.frame_active_fg : server->theme.frame_inactive_fg;

	uint32_t *pixels = malloc((size_t)frame_w * (size_t)title_h * sizeof(*pixels));
	if (pixels == NULL)
		return NULL;

	const struct aswl_gradient *grad = focused ? &server->theme.frame_active_gradient : &server->theme.frame_inactive_gradient;
	if (aswl_gradient_is_valid(grad)) {
		for (int y = 0; y < title_h; y++) {
			for (int x = 0; x < frame_w; x++) {
				double t = aswl_gradient_t(grad->type, x, y, frame_w, title_h);
				uint32_t c = aswl_gradient_sample(grad, t);
				pixels[(size_t)y * (size_t)frame_w + (size_t)x] = aswl_premul_argb(c);
			}
		}
	} else {
		/* Fallback: subtle vertical gradient. */
		uint32_t grad_top = aswl_color_lighten(bg, 42);
		uint32_t grad_bot = aswl_color_darken(bg, 52);
		for (int y = 0; y < title_h; y++) {
			uint8_t t = 0;
			if (title_h > 1)
				t = (uint8_t)((uint32_t)y * 255u / (uint32_t)(title_h - 1));
			uint32_t c = aswl_premul_argb(aswl_color_blend(grad_top, grad_bot, t));
			for (int x = 0; x < frame_w; x++)
				pixels[(size_t)y * (size_t)frame_w + (size_t)x] = c;
		}
	}

			/* AfterStep-style bevel (fixed colors derived from the base). */
			uint32_t relief_fore = aswl_color_hilite(bg);
			uint32_t relief_back = aswl_color_shadow(bg);

				uint32_t hi_color = relief_fore;
				uint32_t lo_color = relief_back;
				uint32_t hihi_color = aswl_color_hilite(relief_fore);
				uint32_t lolo_color = relief_back;
				uint32_t hilo_color = aswl_color_average(hi_color, lo_color);

			uint32_t hi_premul = aswl_premul_argb(hi_color);
			uint32_t lo_premul = aswl_premul_argb(lo_color);

			/* Top/bottom edges */
			for (int x = 0; x < frame_w; x++) {
				pixels[(size_t)x] = hi_premul;
				size_t pos = (size_t)(title_h - 1) * (size_t)frame_w + (size_t)x;
				pixels[pos] = lo_premul;
			}
			if (frame_w > 1 && title_h > 2) {
				for (int y = 1; y < title_h - 1; y++) {
					size_t lpos = (size_t)y * (size_t)frame_w;
					size_t rpos = lpos + (size_t)(frame_w - 1);
					pixels[lpos] = hi_premul;
					pixels[rpos] = lo_premul;
				}
			}
			if (frame_w > 1 && title_h > 1) {
				pixels[0] = aswl_premul_argb(hihi_color);
				pixels[(size_t)(frame_w - 1)] = aswl_premul_argb(hilo_color);
				size_t bl = (size_t)(title_h - 1) * (size_t)frame_w;
				pixels[bl] = aswl_premul_argb(hilo_color);
				pixels[bl + (size_t)(frame_w - 1)] = aswl_premul_argb(lolo_color);
			}

			aswl_deco_assets_ensure(server);

		int border = 0;
		view_get_deco_metrics(view, &border, NULL);

		/*
		 * Title buttons (AfterStep look.DEFAULT):
		 *   Left:  switchwindow, menu, pin
		 *   Right: iconize, shade, kill
		 */
		int btn_spacing = 2;
		int btn_outer_pad = 2;
		/*
		 * In classic AfterStep, TitleButtons are small (typically 16x16) and do
		 * not sit inside oversized bevel boxes. Use native asset sizing and keep
		 * a slightly larger hit-box for usability.
		 */
		int icon_box = clamp_int(title_h - 8, 10, 16);
		int hit_box = clamp_int(icon_box + 4, icon_box, title_h);
		int btn_y = (title_h - hit_box) / 2;
		if (btn_y < 0)
			btn_y = 0;
		if (btn_y + hit_box > title_h)
			btn_y = title_h - hit_box;

		int left_x = border + btn_outer_pad;
		int right_x = frame_w - border - btn_outer_pad - hit_box;
		if (left_x < 0)
			left_x = 0;
		if (right_x < 0)
			right_x = 0;

		/* Left cluster. */
		int switch_x = left_x;
		int menu_x = switch_x + hit_box + btn_spacing;
		int pin_x = menu_x + hit_box + btn_spacing;

		struct {
			int x;
			const struct aswl_deco_icon *icon;
			char fallback_glyph;
		} left_btns[] = {
			{ .x = switch_x, .icon = &server->deco.btn_switch, .fallback_glyph = 'S' },
			{ .x = menu_x, .icon = &server->deco.btn_menu, .fallback_glyph = 'M' },
			{ .x = pin_x, .icon = &server->deco.btn_pin, .fallback_glyph = 'P' },
		};

		for (size_t i = 0; i < sizeof(left_btns) / sizeof(left_btns[0]); i++) {
			int bx = left_btns[i].x;
			if (left_btns[i].icon->argb != NULL && left_btns[i].icon->w > 0 && left_btns[i].icon->h > 0) {
				int dw = left_btns[i].icon->w;
				int dh = left_btns[i].icon->h;
				if (icon_box > 0 && dw > 0 && dh > 0) {
					if (dw > icon_box || dh > icon_box) {
						double sx = (double)icon_box / (double)dw;
						double sy = (double)icon_box / (double)dh;
						double s = sx < sy ? sx : sy;
						dw = (int)((double)dw * s + 0.5);
						dh = (int)((double)dh * s + 0.5);
						if (dw < 1)
							dw = 1;
						if (dh < 1)
							dh = 1;
					}

					int px = bx + (hit_box - dw) / 2;
					int py = btn_y + (hit_box - dh) / 2;
					aswl_blend_image_bilinear_argb(pixels,
					                               frame_w,
					                               title_h,
					                               px,
					                               py,
					                               dw,
					                               dh,
					                               left_btns[i].icon->argb,
					                               left_btns[i].icon->w,
					                               left_btns[i].icon->h);
				}
			} else {
				int scale = clamp_int(icon_box / 10, 1, 3);
				int gw = aswl_font5x7_glyph_w(scale);
				int gh = aswl_font5x7_glyph_h(scale);
				int gx = bx + (hit_box - gw) / 2;
				int gy = btn_y + (hit_box - gh) / 2;
				aswl_font5x7_draw_glyph(pixels, frame_w, title_h, frame_w, gx, gy, left_btns[i].fallback_glyph, scale, fg);
			}
		}

		/* Right cluster. */
		int kill_x = right_x;
		int shade_x = kill_x - (hit_box + btn_spacing);
		int iconize_x = shade_x - (hit_box + btn_spacing);

		struct {
			int x;
			const struct aswl_deco_icon *icon;
			char fallback_glyph;
		} right_btns[] = {
			{ .x = iconize_x, .icon = &server->deco.btn_iconize, .fallback_glyph = '_' },
			{ .x = shade_x, .icon = &server->deco.btn_shade, .fallback_glyph = '^' },
			{ .x = kill_x, .icon = &server->deco.btn_kill, .fallback_glyph = 'X' },
		};

		for (size_t i = 0; i < sizeof(right_btns) / sizeof(right_btns[0]); i++) {
			int bx = right_btns[i].x;
			if (right_btns[i].icon->argb != NULL && right_btns[i].icon->w > 0 && right_btns[i].icon->h > 0) {
				int dw = right_btns[i].icon->w;
				int dh = right_btns[i].icon->h;
				if (icon_box > 0 && dw > 0 && dh > 0) {
					if (dw > icon_box || dh > icon_box) {
						double sx = (double)icon_box / (double)dw;
						double sy = (double)icon_box / (double)dh;
						double s = sx < sy ? sx : sy;
						dw = (int)((double)dw * s + 0.5);
						dh = (int)((double)dh * s + 0.5);
						if (dw < 1)
							dw = 1;
						if (dh < 1)
							dh = 1;
					}

					int px = bx + (hit_box - dw) / 2;
					int py = btn_y + (hit_box - dh) / 2;
					aswl_blend_image_bilinear_argb(pixels,
					                               frame_w,
					                               title_h,
					                               px,
					                               py,
					                               dw,
					                               dh,
					                               right_btns[i].icon->argb,
					                               right_btns[i].icon->w,
					                               right_btns[i].icon->h);
				}
			} else {
				int scale = clamp_int(icon_box / 10, 1, 3);
				int gw = aswl_font5x7_glyph_w(scale);
				int gh = aswl_font5x7_glyph_h(scale);
				int gx = bx + (hit_box - gw) / 2;
				int gy = btn_y + (hit_box - gh) / 2;
				aswl_font5x7_draw_glyph(pixels, frame_w, title_h, frame_w, gx, gy, right_btns[i].fallback_glyph, scale, fg);
			}
		}

		/* Hit-test the rightmost (kill) button for close. */
		view->deco_close_x = kill_x;
		view->deco_close_y = btn_y;
		view->deco_close_w = hit_box;
		view->deco_close_h = hit_box;

		/* Title text (centered between button clusters). */
		const char *title = view_title(view);
		struct aswl_font *title_font = focused ? &server->deco_font : &server->deco_font_inactive;
		int scale = clamp_int((title_h - 8) / 7, 1, 12);
		if (title_font->use_freetype && title_font->base_px > 0)
			scale = 1;
		(void)aswl_font_set_scale(title_font, scale);
		int text_h = aswl_font_height(title_font);

		int left_end = pin_x + hit_box;
		int right_start = iconize_x;
		int tx_pad = 6;
		int text_left = left_end + tx_pad;
		int text_right = right_start - tx_pad;
		int tw = text_right - text_left;
		if (tw > 0 && title != NULL && title[0] != '\0') {
			int title_w = aswl_font_text_width(title_font, title);
			int tx = text_left;
			if (title_w > 0 && title_w < tw)
				tx = text_left + (tw - title_w) / 2;
			int ty = (title_h - text_h) / 2;
			aswl_font_draw_text(title_font, pixels, frame_w, title_h, frame_w, tx, ty, title, tw, fg);
		}

		return aswl_pixbuf_buffer_create(pixels, frame_w, title_h);
	}

static void view_update_decorations(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL || view->scene_tree == NULL)
		return;

	struct aswl_server *server = view->server;

	int border = 0;
	int title_h = 0;
	view_get_deco_metrics(view, &border, &title_h);

	view->deco_border = border;
	view->deco_title_height = title_h;

	if (view->content_tree != NULL)
		wlr_scene_node_set_position(&view->content_tree->node, border, title_h);

	int frame_w = 0;
	int frame_h = 0;
	view_get_frame_size(view, &frame_w, &frame_h);

	uint32_t border_base = server->theme.frame_border;
		uint32_t border_hilite = aswl_color_hilite(border_base);
		uint32_t border_shadow = aswl_color_shadow(border_base);
	float border_hilite_color[4];
	float border_shadow_color[4];
	aswl_argb_to_premul_f(border_hilite, border_hilite_color);
	aswl_argb_to_premul_f(border_shadow, border_shadow_color);

	if (view->deco_left != NULL) {
		wlr_scene_rect_set_color(view->deco_left, border_hilite_color);
		wlr_scene_rect_set_size(view->deco_left, border, frame_h - title_h);
		wlr_scene_node_set_position(&view->deco_left->node, 0, title_h);
		wlr_scene_node_set_enabled(&view->deco_left->node, border > 0);
		view->deco_left->node.data = view;
	}
	if (view->deco_right != NULL) {
		wlr_scene_rect_set_color(view->deco_right, border_shadow_color);
		wlr_scene_rect_set_size(view->deco_right, border, frame_h - title_h);
		wlr_scene_node_set_position(&view->deco_right->node, frame_w - border, title_h);
		wlr_scene_node_set_enabled(&view->deco_right->node, border > 0);
		view->deco_right->node.data = view;
	}
	if (view->deco_bottom != NULL) {
		wlr_scene_rect_set_color(view->deco_bottom, border_shadow_color);
		wlr_scene_rect_set_size(view->deco_bottom, frame_w, border);
		wlr_scene_node_set_position(&view->deco_bottom->node, 0, frame_h - border);
		wlr_scene_node_set_enabled(&view->deco_bottom->node, border > 0);
		view->deco_bottom->node.data = view;
	}

	if (view->deco_titlebar != NULL) {
		if (view->deco_titlebar_buf != NULL) {
			wlr_buffer_drop(view->deco_titlebar_buf);
			view->deco_titlebar_buf = NULL;
		}
		if (title_h > 0) {
			view->deco_titlebar_buf = view_render_titlebar_buffer(view, frame_w, title_h);
			wlr_scene_buffer_set_buffer(view->deco_titlebar, view->deco_titlebar_buf);
			wlr_scene_node_set_position(&view->deco_titlebar->node, 0, 0);
			wlr_scene_node_set_enabled(&view->deco_titlebar->node, true);
			view->deco_titlebar->node.data = view;
		} else {
			wlr_scene_buffer_set_buffer(view->deco_titlebar, NULL);
			wlr_scene_node_set_enabled(&view->deco_titlebar->node, false);
		}
	}
}

static bool view_is_fullscreen(struct aswl_view *view)
{
	if (view == NULL)
		return false;
	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
		return view->xdg_surface->toplevel->current.fullscreen;
	if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		return view->xwayland_surface->fullscreen;
	return false;
}

static bool view_is_maximized(struct aswl_view *view)
{
	if (view == NULL)
		return false;
	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
		return view->xdg_surface->toplevel->current.maximized;
	if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		return view->xwayland_surface->maximized_horz || view->xwayland_surface->maximized_vert;
	return false;
}

static void view_save_geometry(struct aswl_view *view)
{
	if (view == NULL || view->saved_geometry)
		return;
	if (view->scene_tree == NULL)
		return;

	int lx = 0;
	int ly = 0;
	(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);

	int w = 0;
	int h = 0;
	view_get_frame_size(view, &w, &h);

	view->saved_geometry = true;
	view->saved_x = lx;
	view->saved_y = ly;
	view->saved_w = w;
	view->saved_h = h;
}

static uint16_t clamp_u16(int v, uint16_t fallback)
{
	if (v <= 0)
		return fallback;
	if (v > (int)UINT16_MAX)
		return UINT16_MAX;
	return (uint16_t)v;
}

static int16_t clamp_i16(int v)
{
	if (v < (int)INT16_MIN)
		return INT16_MIN;
	if (v > (int)INT16_MAX)
		return INT16_MAX;
	return (int16_t)v;
}

static struct aswl_output *find_aswl_output(struct aswl_server *server, struct wlr_output *output)
{
	if (server == NULL || output == NULL)
		return NULL;
	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out->wlr_output == output)
			return out;
	}
	return NULL;
}

static struct wlr_output *view_get_output(struct aswl_server *server, struct aswl_view *view)
{
	if (server == NULL || server->output_layout == NULL)
		return NULL;

	int lx = 0;
	int ly = 0;
	if (view != NULL && view->scene_tree != NULL)
		(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);

	struct wlr_output *output = wlr_output_layout_output_at(server->output_layout, lx, ly);
	if (output == NULL && server->cursor != NULL)
		output = wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
	if (output == NULL)
		output = wlr_output_layout_get_center_output(server->output_layout);
	return output;
}

static bool view_target_boxes(struct aswl_server *server, struct aswl_view *view, struct wlr_box *full_out, struct wlr_box *usable_out)
{
	if (server == NULL || server->output_layout == NULL)
		return false;

	struct wlr_output *output = view_get_output(server, view);
	if (output == NULL)
		return false;

	struct wlr_box full = { 0 };
	wlr_output_layout_get_box(server->output_layout, output, &full);
	struct wlr_box usable = full;

	struct aswl_output *out = find_aswl_output(server, output);
	if (out != NULL && out->usable_box.width > 0 && out->usable_box.height > 0)
		usable = out->usable_box;

	if (full_out != NULL)
		*full_out = full;
	if (usable_out != NULL)
		*usable_out = usable;
	return full.width > 0 && full.height > 0;
}

static void view_apply_geometry(struct aswl_view *view, int x, int y, int w, int h)
{
	if (view == NULL || view->scene_tree == NULL)
		return;

	wlr_scene_node_set_position(&view->scene_tree->node, x, y);
	view->placed = true;

	int border = 0;
	int title_h = 0;
	view_get_deco_metrics(view, &border, &title_h);
	int ox = border;
	int oy = title_h;

	int cw = w - 2 * border;
	int ch = h - title_h - border;
	if (cw < 1)
		cw = 1;
	if (ch < 1)
		ch = 1;

	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
		if (w > 0 && h > 0)
			(void)wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, cw, ch);
		return;
	}
	if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
		uint16_t uw = clamp_u16(cw, view->xwayland_surface->width);
		uint16_t uh = clamp_u16(ch, view->xwayland_surface->height);
		wlr_xwayland_surface_configure(view->xwayland_surface,
		                               clamp_i16(x + ox),
		                               clamp_i16(y + oy),
		                               uw,
		                               uh);
		return;
	}
}

static void view_restore_geometry(struct aswl_view *view)
{
	if (view == NULL || !view->saved_geometry)
		return;

	view_apply_geometry(view, view->saved_x, view->saved_y, view->saved_w, view->saved_h);
	view->saved_geometry = false;
}

static void view_set_fullscreen(struct aswl_view *view, bool fullscreen)
{
	if (view == NULL || view->server == NULL)
		return;
	if (view->is_dock)
		return;

	struct aswl_server *server = view->server;
	struct wlr_box full = { 0 };
	struct wlr_box usable = { 0 };
	(void)view_target_boxes(server, view, &full, &usable);

	if (fullscreen) {
		view_save_geometry(view);
		if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_fullscreen(view->xdg_surface->toplevel, true);
		else if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
			wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, true);

		view_apply_geometry(view, full.x, full.y, full.width, full.height);
		view_update_toplevel_protocols(view);
		return;
	}

	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
		(void)wlr_xdg_toplevel_set_fullscreen(view->xdg_surface->toplevel, false);
	else if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, false);

	view_restore_geometry(view);
	view_update_toplevel_protocols(view);
}

static void view_set_maximized(struct aswl_view *view, bool maximized)
{
	if (view == NULL || view->server == NULL)
		return;
	if (view->is_dock)
		return;

	struct aswl_server *server = view->server;
	struct wlr_box full = { 0 };
	struct wlr_box usable = { 0 };
	(void)view_target_boxes(server, view, &full, &usable);

	if (maximized) {
		view_save_geometry(view);
		if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_maximized(view->xdg_surface->toplevel, true);
		else if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
			wlr_xwayland_surface_set_maximized(view->xwayland_surface, true, true);

		view_apply_geometry(view, usable.x, usable.y, usable.width, usable.height);
		view_update_toplevel_protocols(view);
		return;
	}

	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
		(void)wlr_xdg_toplevel_set_maximized(view->xdg_surface->toplevel, false);
	else if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		wlr_xwayland_surface_set_maximized(view->xwayland_surface, false, false);

	view_restore_geometry(view);
	view_update_toplevel_protocols(view);
}

static const char *xcursor_for_resize_edges(uint32_t edges)
{
	if ((edges & (WLR_EDGE_TOP | WLR_EDGE_LEFT)) == (WLR_EDGE_TOP | WLR_EDGE_LEFT))
		return "top_left_corner";
	if ((edges & (WLR_EDGE_TOP | WLR_EDGE_RIGHT)) == (WLR_EDGE_TOP | WLR_EDGE_RIGHT))
		return "top_right_corner";
	if ((edges & (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT)) == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT))
		return "bottom_left_corner";
	if ((edges & (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT)) == (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT))
		return "bottom_right_corner";
	if ((edges & WLR_EDGE_TOP) != 0)
		return "top_side";
	if ((edges & WLR_EDGE_BOTTOM) != 0)
		return "bottom_side";
	if ((edges & WLR_EDGE_LEFT) != 0)
		return "left_side";
	if ((edges & WLR_EDGE_RIGHT) != 0)
		return "right_side";
	return "left_ptr";
}

struct aswl_dock_item {
	struct aswl_view *view;
	size_t create_index;
	size_t rule_rank;
	const char *title;
	const char *app;
};

static int aswl_dock_item_cmp(const struct aswl_dock_item *a,
                              const struct aswl_dock_item *b,
                              enum aswl_dock_order_mode order)
{
	if (a == NULL || b == NULL)
		return 0;

	if (a->rule_rank != b->rule_rank)
		return a->rule_rank < b->rule_rank ? -1 : 1;

	switch (order) {
	case ASWL_DOCK_ORDER_TITLE: {
		int c = str_icmp(a->title, b->title);
		if (c != 0)
			return c;
		break;
	}
	case ASWL_DOCK_ORDER_CLASS: {
		int c = str_icmp(a->app, b->app);
		if (c != 0)
			return c;
		c = str_icmp(a->title, b->title);
		if (c != 0)
			return c;
		break;
	}
	case ASWL_DOCK_ORDER_CONFIG:
	case ASWL_DOCK_ORDER_CREATE:
	default:
		break;
	}

	if (a->create_index != b->create_index)
		return a->create_index < b->create_index ? -1 : 1;
	return 0;
}

static void arrange_dock_views(struct aswl_server *server)
{
	if (server == NULL || server->output_layout == NULL)
		return;

	struct wlr_output *output = wlr_output_layout_get_center_output(server->output_layout);
	if (output == NULL)
		return;

	struct wlr_box full = { 0 };
	wlr_output_layout_get_box(server->output_layout, output, &full);

	struct wlr_box usable = full;
	struct aswl_output *out = output_from_wlr_output(server, output);
	if (out != NULL && out->usable_box.width > 0 && out->usable_box.height > 0)
		usable = out->usable_box;

	const int pad = server->dock.pad;
	const int spacing = server->dock.spacing;
	const enum aswl_dock_flow flow = server->dock.flow;
	const enum aswl_dock_order_mode order = server->dock.order;

	bool anchor_right = (server->dock.anchor == ASWL_DOCK_ANCHOR_BOTTOM_RIGHT ||
	                     server->dock.anchor == ASWL_DOCK_ANCHOR_TOP_RIGHT);
	bool anchor_bottom = (server->dock.anchor == ASWL_DOCK_ANCHOR_BOTTOM_LEFT ||
	                      server->dock.anchor == ASWL_DOCK_ANCHOR_BOTTOM_RIGHT);

	size_t dock_count = 0;
	struct aswl_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->is_dock && view->mapped && view->scene_tree != NULL)
			dock_count++;
	}
	if (dock_count == 0)
		return;

	struct aswl_dock_item *items = calloc(dock_count, sizeof(*items));
	if (items == NULL)
		return;

	size_t idx = 0;
	size_t create_index = 0;
	wl_list_for_each(view, &server->views, link) {
		if (view->is_dock && view->mapped && view->scene_tree != NULL) {
			items[idx].view = view;
			items[idx].create_index = create_index;
			items[idx].rule_rank = dock_rule_rank(&server->dock, view);
			items[idx].title = view_title(view);
			items[idx].app = view_app_id(view);
			idx++;
		}
		create_index++;
	}

	for (size_t i = 1; i < dock_count; i++) {
		struct aswl_dock_item key = items[i];
		size_t j = i;
		while (j > 0 && aswl_dock_item_cmp(&key, &items[j - 1], order) < 0) {
			items[j] = items[j - 1];
			j--;
		}
		items[j] = key;
	}

	const int min_x = usable.x + pad;
	const int max_x = usable.x + usable.width - pad;
	const int min_y = usable.y + pad;
	const int max_y = usable.y + usable.height - pad;

	const int x_step_dir = anchor_right ? -1 : 1;
	const int y_step_dir = anchor_bottom ? -1 : 1;

	int start_x = anchor_right ? max_x : min_x;
	int start_y = anchor_bottom ? max_y : min_y;
	int x_cursor = start_x;
	int y_cursor = start_y;
	int line_extent = 0;

	for (size_t i = 0; i < dock_count; i++) {
		view = items[i].view;
		if (view == NULL || view->scene_tree == NULL)
			continue;

		int w = 0;
		int h = 0;
		view_get_frame_size(view, &w, &h);
		if (w <= 0)
			w = 64;
		if (h <= 0)
			h = 64;

		if (flow == ASWL_DOCK_FLOW_ROW) {
			int x = anchor_right ? x_cursor - w : x_cursor;
			int y = anchor_bottom ? y_cursor - h : y_cursor;

			if (anchor_right) {
				if (x < min_x && x_cursor != start_x) {
					x_cursor = start_x;
					y_cursor += y_step_dir * (line_extent + spacing);
					line_extent = 0;
					x = anchor_right ? x_cursor - w : x_cursor;
					y = anchor_bottom ? y_cursor - h : y_cursor;
				}
			} else {
				if (x + w > max_x && x_cursor != start_x) {
					x_cursor = start_x;
					y_cursor += y_step_dir * (line_extent + spacing);
					line_extent = 0;
					x = anchor_right ? x_cursor - w : x_cursor;
					y = anchor_bottom ? y_cursor - h : y_cursor;
				}
			}

			if (anchor_right) {
				if (x < min_x)
					x = min_x;
			} else {
				if (x + w > max_x)
					x = max_x - w;
				if (x < min_x)
					x = min_x;
			}

			if (anchor_bottom) {
				if (y < min_y)
					y = min_y;
			} else {
				if (y + h > max_y)
					y = max_y - h;
				if (y < min_y)
					y = min_y;
			}

			wlr_scene_node_set_position(&view->scene_tree->node, x, y);
			wlr_scene_node_raise_to_top(&view->scene_tree->node);

			if (view->xwayland_surface != NULL) {
				uint16_t ww = view->xwayland_surface->width;
				uint16_t hh = view->xwayland_surface->height;
				if (ww == 0)
					ww = (uint16_t)w;
				if (hh == 0)
					hh = (uint16_t)h;
				int ox = 0;
				int oy = 0;
				view_get_content_offset(view, &ox, &oy);
				wlr_xwayland_surface_configure(view->xwayland_surface, x + ox, y + oy, ww, hh);
			}

			view->placed = true;

			x_cursor += x_step_dir * (w + spacing);
			if (h > line_extent)
				line_extent = h;
			continue;
		}

		/* Column flow. */
		int x = anchor_right ? x_cursor - w : x_cursor;
		int y = anchor_bottom ? y_cursor - h : y_cursor;

		if (anchor_bottom) {
			if (y < min_y && y_cursor != start_y) {
				y_cursor = start_y;
				x_cursor += x_step_dir * (line_extent + spacing);
				line_extent = 0;
				x = anchor_right ? x_cursor - w : x_cursor;
				y = anchor_bottom ? y_cursor - h : y_cursor;
			}
		} else {
			if (y + h > max_y && y_cursor != start_y) {
				y_cursor = start_y;
				x_cursor += x_step_dir * (line_extent + spacing);
				line_extent = 0;
				x = anchor_right ? x_cursor - w : x_cursor;
				y = anchor_bottom ? y_cursor - h : y_cursor;
			}
		}

		if (anchor_right) {
			if (x < min_x)
				x = min_x;
		} else {
			if (x + w > max_x)
				x = max_x - w;
			if (x < min_x)
				x = min_x;
		}

		if (anchor_bottom) {
			if (y < min_y)
				y = min_y;
		} else {
			if (y + h > max_y)
				y = max_y - h;
			if (y < min_y)
				y = min_y;
		}

		wlr_scene_node_set_position(&view->scene_tree->node, x, y);
		wlr_scene_node_raise_to_top(&view->scene_tree->node);

		if (view->xwayland_surface != NULL) {
			uint16_t ww = view->xwayland_surface->width;
			uint16_t hh = view->xwayland_surface->height;
			if (ww == 0)
				ww = (uint16_t)w;
			if (hh == 0)
				hh = (uint16_t)h;
			int ox = 0;
			int oy = 0;
			view_get_content_offset(view, &ox, &oy);
			wlr_xwayland_surface_configure(view->xwayland_surface, x + ox, y + oy, ww, hh);
		}

		view->placed = true;

		y_cursor += y_step_dir * (h + spacing);
		if (w > line_extent)
			line_extent = w;
	}

	free(items);
}

static void apply_layer_struts_top(struct aswl_server *server, struct wlr_output *output, struct wlr_box *usable)
{
	if (server == NULL || server->output_layout == NULL || output == NULL || usable == NULL)
		return;

	const int spacing = 3; /* Match AfterStep-ish NoCollidesSpacing feel. */
	int top_limit = usable->y;

	struct wlr_output *default_output = wlr_output_layout_get_center_output(server->output_layout);
	if (default_output == NULL)
		default_output = output;

	struct aswl_layer_surface *ls;
	wl_list_for_each(ls, &server->layer_surfaces, link) {
		struct wlr_layer_surface_v1 *surf = ls->layer_surface;
		if (surf == NULL || surf->surface == NULL || ls->scene == NULL || ls->scene->tree == NULL)
			continue;
		if (!surf->initialized || !surf->surface->mapped)
			continue;

		struct wlr_output *target = surf->output;
		if (target == NULL)
			target = default_output;
		if (target != output)
			continue;

		/* Only treat surfaces that want to reserve space. */
		if (surf->current.exclusive_zone <= 0)
			continue;

		/* Only treat wide/short surfaces as top "bars" (avoid right-side panels). */
		int sw = (int)surf->surface->current.width;
		int sh = (int)surf->surface->current.height;
		if (sw <= 0 || sh <= 0 || sw < sh)
			continue;

		if ((surf->current.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) == 0)
			continue;

		int sx = 0;
		int sy = 0;
		(void)wlr_scene_node_coords(&ls->scene->tree->node, &sx, &sy);

		int bottom = sy + sh + spacing;
		if (bottom > top_limit)
			top_limit = bottom;
	}

	int max_y = usable->y + usable->height;
	if (top_limit > max_y)
		top_limit = max_y;

	int delta = top_limit - usable->y;
	if (delta > 0) {
		usable->y = top_limit;
		usable->height -= delta;
	}
}

static void place_view(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL)
		return;
	struct aswl_server *server = view->server;

	if (view->placed || server->output_layout == NULL)
		return;

	struct wlr_output *output = NULL;
	if (server->cursor != NULL)
		output = wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
	if (output == NULL)
		output = wlr_output_layout_get_center_output(server->output_layout);

	struct wlr_box full = { 0 };
	if (output != NULL)
		wlr_output_layout_get_box(server->output_layout, output, &full);
	struct wlr_box usable = full;

	struct aswl_output *out = output_from_wlr_output(server, output);
	if (out != NULL && out->usable_box.width > 0 && out->usable_box.height > 0)
		usable = out->usable_box;

	int width = 0;
	int height = 0;
	view_get_frame_size(view, &width, &height);

	int border = 0;
	int title_h = 0;
	view_get_deco_metrics(view, &border, &title_h);

	int content_w = 0;
	int content_h = 0;
	view_get_current_size(view, &content_w, &content_h);

	if (view->xwayland_surface != NULL && !view->xwayland_surface->override_redirect && (content_w <= 1 || content_h <= 1)) {
		/*
		 * Some Xwayland clients can map before a meaningful size is known,
		 * leaving width/height at 0. Avoid forcing a huge default size (which
		 * can stick for clients like xterm); pick a smaller, terminal-like
		 * fallback that still keeps the window visible.
		 */
		if (content_w <= 1)
			content_w = 640;
		if (content_h <= 1)
			content_h = 400;
		width = content_w + 2 * border;
		height = content_h + title_h + border;
	}

	bool suite_popup = view_is_suite_popup(view);

	/* Avoid placing regular windows under top layer-shell bars (e.g. Wharf-ish docks). */
	if (!suite_popup)
		apply_layer_struts_top(server, output, &usable);

	/*
	 * Root-menu style: suite popups (aswlmenu) should appear near the pointer,
	 * not cascade like normal application windows.
	 */
	int x = usable.x + 40 + server->cascade_offset;
	int y = usable.y + 40 + server->cascade_offset;
	if (suite_popup && server->cursor != NULL) {
		x = (int)server->cursor->x;
		y = (int)server->cursor->y;
	}
	if (width > 0 && height > 0 && usable.width > 0 && usable.height > 0) {
		int max_x = usable.x + usable.width - width;
		int max_y = usable.y + usable.height - height;
		if (x > max_x)
			x = usable.x + 40;
		if (y > max_y)
			y = usable.y + 40;
		if (x < usable.x)
			x = usable.x;
		if (y < usable.y)
			y = usable.y;
		if (x > max_x)
			x = max_x;
		if (y > max_y)
			y = max_y;
	}

	wlr_scene_node_set_position(&view->scene_tree->node, x, y);
	if (view->xwayland_surface != NULL && width > 0 && height > 0) {
		int ox = border;
		int oy = title_h;
		int cw = width - 2 * border;
		int ch = height - title_h - border;
		if (cw < 1)
			cw = 1;
		if (ch < 1)
			ch = 1;
		wlr_xwayland_surface_configure(view->xwayland_surface,
		                               x + ox,
		                               y + oy,
		                               (uint16_t)cw,
		                               (uint16_t)ch);
		view_update_decorations(view);
	}
	view->placed = true;

	if (!suite_popup) {
		server->cascade_offset += 32;
		if (server->cascade_offset >= 256)
			server->cascade_offset = 0;
	}
}

static void begin_interactive(struct aswl_view *view, enum aswl_cursor_mode mode, uint32_t edges, uint32_t button)
{
	if (view == NULL || view->server == NULL)
		return;
	struct aswl_server *server = view->server;

	if (server->cursor == NULL || server->cursor_mgr == NULL)
		return;

	focus_view(view, NULL);

	server->cursor_mode = mode;
	server->grabbed_view = view;
	server->grab_lx = server->cursor->x;
	server->grab_ly = server->cursor->y;
	server->grab_edges = edges;
	server->grab_button = button;

	int lx = 0;
	int ly = 0;
	(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);
	server->grab_view_lx = lx;
	server->grab_view_ly = ly;

	view_get_frame_size(view, &server->grab_view_width, &server->grab_view_height);
	if (server->grab_view_width <= 0)
		server->grab_view_width = 1;
	if (server->grab_view_height <= 0)
		server->grab_view_height = 1;

	switch (mode) {
	case ASWL_CURSOR_MOVE:
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "grabbing");
		break;
	case ASWL_CURSOR_RESIZE:
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_resizing(view->xdg_surface->toplevel, true);
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, xcursor_for_resize_edges(edges));
		break;
	case ASWL_CURSOR_PASSTHROUGH:
	default:
		break;
	}
}

static void end_interactive(struct aswl_server *server)
{
	if (server == NULL)
		return;

	if (server->cursor_mode == ASWL_CURSOR_RESIZE) {
		struct aswl_view *view = server->grabbed_view;
		if (view != NULL && view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_resizing(view->xdg_surface->toplevel, false);
	}

	server->cursor_mode = ASWL_CURSOR_PASSTHROUGH;
	server->grabbed_view = NULL;
	server->grab_button = 0;
	if (server->cursor != NULL && server->cursor_mgr != NULL)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
}

static void handle_view_scene_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, scene_destroy);
	if (view == NULL)
		return;

	wl_list_remove(&view->scene_destroy.link);
	view->scene_destroy_listener_added = false;

	if (view->deco_titlebar_buf != NULL) {
		wlr_buffer_drop(view->deco_titlebar_buf);
		view->deco_titlebar_buf = NULL;
	}

	view->scene_tree = NULL;
	view->content_tree = NULL;
	view->surface_tree = NULL;
	view->deco_left = NULL;
	view->deco_right = NULL;
	view->deco_bottom = NULL;
	view->deco_titlebar = NULL;
	view->mapped = false;
}

static void handle_view_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, surface_destroy);
	if (view == NULL)
		return;

	wl_list_remove(&view->surface_destroy.link);
	view->surface_destroy_listener_added = false;

	/*
	 * If a wlr_surface is being destroyed before our higher-level destroy
	 * signals fire, avoid touching the surface's wl_signal lists later.
	 */
	if (view->type == ASWL_VIEW_XWAYLAND) {
		xwayland_detach_surface(view);
		return;
	}

	view->mapped = false;
	if (view->scene_tree != NULL)
		wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	if (view->surface_listeners_added) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		view->surface_listeners_added = false;
	}
	if (view->commit_listener_added) {
		wl_list_remove(&view->commit.link);
		view->commit_listener_added = false;
	}

	struct aswl_server *server = view->server;
	if (server != NULL && server->grabbed_view == view)
		end_interactive(server);

	if (server != NULL && server->focused_view == view) {
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		focus_topmost_view(server);
	}

	if (server != NULL)
		broadcast_window_state(server, view);

	/* Suite popups (root-menu style) should not become the "active" view. If the
	 * menu goes away, restore keyboard focus to the active view if any. */
	if (server != NULL && view_is_suite_popup(view)) {
		if (server->focused_view != NULL)
			focus_view(server->focused_view, NULL);
		else {
			wlr_seat_keyboard_notify_clear_focus(server->seat);
			aswl_ime_set_focus(server, NULL);
		}
	}
}

static void handle_view_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, commit);
	if (view == NULL || view->server == NULL || view->xdg_surface == NULL || view->xdg_surface->toplevel == NULL)
		return;

	/*
	 * Wayland clients typically wait for the compositor to send an initial
	 * xdg_toplevel configure (after an initial, buffer-less commit) before
	 * they commit a buffer and become mapped. Without this, native clients
	 * remain present but unmapped.
	 */
	if (!view->xdg_surface->initialized)
		return;

	if (!view->xdg_surface->configured) {
		(void)wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, 0, 0);
		aswl_schedule_flush(view->server);
	}

	view_update_decorations(view);
}

static void handle_view_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, map);
	struct aswl_server *server = view->server;

	view->mapped = true;
	view_maybe_mark_dock(view);
	view_update_decorations(view);
	view_toplevel_protocols_create(view);

	bool enabled = server != NULL && (view->is_dock || view->workspace == server->current_workspace);
	wlr_scene_node_set_enabled(&view->scene_tree->node, enabled);

	if (enabled) {
		if (view->is_dock) {
			arrange_dock_views(server);
		} else {
			place_view(view);
			focus_view(view, NULL);
		}
	} else if (server != NULL) {
		broadcast_window_state(server, view);
	}
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, unmap);
	struct aswl_server *server = view->server;

	view->mapped = false;
	view_toplevel_protocols_destroy(view);
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	if (server != NULL && server->grabbed_view == view) {
		end_interactive(server);
	}

	if (server != NULL && server->focused_view == view) {
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL)
			(void)wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, false);
		else if (view->xwayland_surface != NULL)
			wlr_xwayland_surface_activate(view->xwayland_surface, false);
		server->focused_view = NULL;
		view_update_decorations(view);
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		focus_topmost_view(server);
	}

	if (server != NULL)
		broadcast_window_state(server, view);

	/* If a suite popup held keyboard focus, restore focus to the active view. */
	if (server != NULL && view_is_suite_popup(view)) {
		if (server->focused_view != NULL)
			focus_view(server->focused_view, NULL);
		else {
			wlr_seat_keyboard_notify_clear_focus(server->seat);
			aswl_ime_set_focus(server, NULL);
		}
	}

	if (server != NULL && view->is_dock)
		arrange_dock_views(server);
}

static void handle_view_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, destroy);
	struct aswl_server *server = view->server;
	uint32_t id = view->id;
	bool was_focused = (server != NULL && server->focused_view == view);

	view_toplevel_protocols_destroy(view);

	if (view->type == ASWL_VIEW_XDG && view->xdg_surface != NULL)
		view->xdg_surface->data = NULL;
	if (view->type == ASWL_VIEW_XWAYLAND && view->xwayland_surface != NULL)
		view->xwayland_surface->data = NULL;

	/* Ensure we won't re-focus a view while it's being torn down. */
	view->mapped = false;
	if (view->scene_tree != NULL)
		wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	if (view->surface_listeners_added) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		view->surface_listeners_added = false;
	}
	if (view->commit_listener_added) {
		wl_list_remove(&view->commit.link);
		view->commit_listener_added = false;
	}
	if (view->surface_destroy_listener_added) {
		wl_list_remove(&view->surface_destroy.link);
		view->surface_destroy_listener_added = false;
	}
	if (server != NULL)
		broadcast_window_closed(server, id);

	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_minimize.link);
	if (view->type == ASWL_VIEW_XDG) {
		wl_list_remove(&view->set_title.link);
		wl_list_remove(&view->set_app_id.link);
	} else if (view->type == ASWL_VIEW_XWAYLAND) {
		wl_list_remove(&view->set_title.link);
		wl_list_remove(&view->set_class.link);
	}
	if (view->type == ASWL_VIEW_XWAYLAND) {
		wl_list_remove(&view->xwayland_associate.link);
		wl_list_remove(&view->xwayland_dissociate.link);
		wl_list_remove(&view->xwayland_map_request.link);
		wl_list_remove(&view->xwayland_request_configure.link);
	}
	wl_list_remove(&view->link);

	if (server != NULL && server->grabbed_view == view) {
		end_interactive(server);
	}

	if (server != NULL && was_focused) {
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		focus_topmost_view(server);
	}

	if (server != NULL && view->is_dock)
		arrange_dock_views(server);

	if (view->deco_titlebar_buf != NULL) {
		wlr_buffer_drop(view->deco_titlebar_buf);
		view->deco_titlebar_buf = NULL;
	}

	if (view->scene_tree != NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
	}

	free(view);
}

static void handle_view_set_title(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, set_title);
	view_update_decorations(view);
	view_update_toplevel_protocols(view);
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
	if (view->server != NULL && view->is_dock && view->mapped)
		arrange_dock_views(view->server);
}

static void handle_view_set_app_id(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, set_app_id);
	view_update_toplevel_protocols(view);
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
	if (view->server != NULL && view->is_dock && view->mapped)
		arrange_dock_views(view->server);
}

static void handle_view_set_class(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, set_class);
	view_update_toplevel_protocols(view);
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
	if (view->server != NULL && view->is_dock && view->mapped)
		arrange_dock_views(view->server);
}

static void handle_request_move(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, request_move);
	struct aswl_server *server = view->server;
	struct wlr_xdg_toplevel_move_event *event = data;

	if (server == NULL || server->cursor == NULL)
		return;

	if (event != NULL && !wlr_seat_validate_pointer_grab_serial(server->seat, view->xdg_surface->surface, event->serial))
		return;

	begin_interactive(view, ASWL_CURSOR_MOVE, 0, 0);
}

static void handle_request_resize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, request_resize);
	struct aswl_server *server = view->server;
	struct wlr_xdg_toplevel_resize_event *event = data;

	if (server == NULL || server->cursor == NULL)
		return;

	if (event == NULL)
		return;
	if (!wlr_seat_validate_pointer_grab_serial(server->seat, view->xdg_surface->surface, event->serial))
		return;

	begin_interactive(view, ASWL_CURSOR_RESIZE, event->edges, 0);
}

static void handle_request_fullscreen(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_fullscreen);
	if (view == NULL || view->xdg_surface == NULL || view->xdg_surface->toplevel == NULL)
		return;

	bool requested = view->xdg_surface->toplevel->requested.fullscreen;
	fprintf(stderr, "aswlcomp: xdg request_fullscreen=%d\n", requested ? 1 : 0);
	view_set_fullscreen(view, requested);

	aswl_schedule_flush(view->server);
}

static void handle_request_maximize(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_maximize);
	if (view == NULL || view->xdg_surface == NULL || view->xdg_surface->toplevel == NULL)
		return;

	bool requested = view->xdg_surface->toplevel->requested.maximized;
	fprintf(stderr, "aswlcomp: xdg request_maximize=%d\n", requested ? 1 : 0);
	view_set_maximized(view, requested);

	aswl_schedule_flush(view->server);
}

static void handle_request_minimize(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_minimize);
	if (view == NULL || view->xdg_surface == NULL || view->xdg_surface->toplevel == NULL)
		return;

	/*
	 * xdg-shell doesn't have a minimized state in configure, but we still need
	 * to send a configure event to acknowledge set_minimized requests.
	 */
	fprintf(stderr, "aswlcomp: xdg request_minimize\n");
	(void)wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, view->xdg_surface->toplevel->current.activated);

	aswl_schedule_flush(view->server);
}

static void xwayland_attach_surface(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL || view->xwayland_surface == NULL)
		return;
	struct wlr_xwayland_surface *xsurface = view->xwayland_surface;

	if (xsurface->surface == NULL)
		return;

	if (view->scene_tree != NULL)
		return;

	if (!view_create_frame_scene(view))
		return;

	view->surface_tree = wlr_scene_subsurface_tree_create(view->content_tree, xsurface->surface);
	if (view->surface_tree == NULL)
		return;

	view->scene_destroy.notify = handle_view_scene_destroy;
	wl_signal_add(&view->scene_tree->node.events.destroy, &view->scene_destroy);
	view->scene_destroy_listener_added = true;

	view_update_decorations(view);

	int ox = 0;
	int oy = 0;
	view_get_content_offset(view, &ox, &oy);
	wlr_scene_node_set_position(&view->scene_tree->node, xsurface->x - ox, xsurface->y - oy);

	/* Start hidden until the client maps. */
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	view->map.notify = handle_view_map;
	wl_signal_add(&xsurface->surface->events.map, &view->map);

	view->unmap.notify = handle_view_unmap;
	wl_signal_add(&xsurface->surface->events.unmap, &view->unmap);
	view->surface_listeners_added = true;

	/* wlr_surface may be destroyed before xwayland_surface destroy; guard listener cleanup. */
	view->surface_destroy.notify = handle_view_surface_destroy;
	wl_signal_add(&xsurface->surface->events.destroy, &view->surface_destroy);
	view->surface_destroy_listener_added = true;

	/*
	 * Some Xwayland clients can fully map (including committing a buffer) before
	 * we manage to attach our surface listeners. In that case we'd miss the
	 * wlr_surface map event and the view would remain "unmapped" forever.
	 */
	if (!view->mapped && xsurface->surface->mapped)
		handle_view_map(&view->map, NULL);
}

static void xwayland_detach_surface(struct aswl_view *view)
{
	if (view == NULL)
		return;

	if (view->surface_destroy_listener_added) {
		wl_list_remove(&view->surface_destroy.link);
		view->surface_destroy_listener_added = false;
	}

	if (view->surface_listeners_added) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		view->surface_listeners_added = false;
	}

	if (view->scene_tree != NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
	}

	view->mapped = false;
	view_toplevel_protocols_destroy(view);
	if (view->server != NULL)
		broadcast_window_state(view->server, view);
}

static void handle_xwayland_associate(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, xwayland_associate);
	xwayland_attach_surface(view);
}

static void handle_xwayland_dissociate(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, xwayland_dissociate);
	struct aswl_server *server = view->server;

	if (server != NULL && server->grabbed_view == view)
		end_interactive(server);

	if (server != NULL && server->focused_view == view) {
		server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		focus_topmost_view(server);
	}

	xwayland_detach_surface(view);
}

static void handle_xwayland_request_configure(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, xwayland_request_configure);
	struct wlr_xwayland_surface_configure_event *event = data;

	if (view == NULL || view->xwayland_surface == NULL || event == NULL)
		return;
	const bool have_scene = (view->scene_tree != NULL);

	if (view->is_dock) {
		if (view->server != NULL)
			arrange_dock_views(view->server);
		return;
	}

	view_update_decorations(view);

	int ox = 0;
	int oy = 0;
	view_get_content_offset(view, &ox, &oy);

	/*
	 * X11 ConfigureWindow requests may omit some fields; wlroots exposes a mask
	 * so we can distinguish "not requested" from zero values. In particular,
	 * some clients send move-only configures and wlroots may leave width/height
	 * as 0 unless explicitly requested, so blindly applying 0×0 would collapse
	 * the view to just its titlebar.
	 */
	int lx = 0;
	int ly = 0;
	if (have_scene)
		(void)wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);

	int cur_x = have_scene ? (lx + ox) : (int)view->xwayland_surface->x;
	int cur_y = have_scene ? (ly + oy) : (int)view->xwayland_surface->y;

	int x = cur_x;
	int y = cur_y;
	if (!view->placed) {
		if ((event->mask & XCB_CONFIG_WINDOW_X) != 0)
			x = event->x;
		if ((event->mask & XCB_CONFIG_WINDOW_Y) != 0)
			y = event->y;
	}

	int w = (int)view->xwayland_surface->width;
	int h = (int)view->xwayland_surface->height;
	if ((event->mask & XCB_CONFIG_WINDOW_WIDTH) != 0)
		w = (int)event->width;
	if ((event->mask & XCB_CONFIG_WINDOW_HEIGHT) != 0)
		h = (int)event->height;
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;

	/*
	 * Some Xwayland clients (notably xterm) may map before a meaningful size
	 * propagates, leaving width/height at 0 and causing us to shrink them to
	 * 1×1. Avoid making newly created windows invisible; they can still request
	 * a different size later.
	 */
	if (!view->xwayland_surface->override_redirect && (w <= 1 || h <= 1)) {
		if (w <= 1)
			w = 640;
		if (h <= 1)
			h = 400;
	}

	if (!view->placed && (x != cur_x || y != cur_y)) {
		if (have_scene)
			wlr_scene_node_set_position(&view->scene_tree->node, x - ox, y - oy);
		view->placed = true;
	}

	wlr_xwayland_surface_configure(view->xwayland_surface,
	                               clamp_i16(x),
	                               clamp_i16(y),
	                               (uint16_t)w,
	                               (uint16_t)h);
	view_update_decorations(view);
}

static void handle_xwayland_map_request(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, xwayland_map_request);
	struct wlr_xwayland_surface *xsurface = view != NULL ? view->xwayland_surface : NULL;

	if (view == NULL || xsurface == NULL)
		return;

	int x = (int)xsurface->x;
	int y = (int)xsurface->y;
	int w = (int)xsurface->width;
	int h = (int)xsurface->height;

	/*
	 * Some X11 clients (notably AfterStep modules like WinTabs) create their
	 * toplevel as 1×1 and rely on the window manager to configure an initial
	 * size before they render/commit. Without this, the wl_surface never maps.
	 */
	if ((w <= 1 || h <= 1) && xsurface->size_hints != NULL) {
		int hw = xsurface->size_hints->width;
		int hh = xsurface->size_hints->height;
		if (hw > 1 && hw <= 4096)
			w = hw;
		if (hh > 1 && hh <= 4096)
			h = hh;
	}

	if (w <= 1 || h <= 1) {
		int fallback_w = 640;
		int fallback_h = 400;
		if (xsurface->class != NULL && str_ieq(xsurface->class, "ASModule")) {
			fallback_w = 320;
			fallback_h = 80;
		}
		if (w <= 1)
			w = fallback_w;
		if (h <= 1)
			h = fallback_h;
	}

	wlr_xwayland_surface_configure(xsurface,
	                               clamp_i16(x),
	                               clamp_i16(y),
	                               clamp_u16(w, 320),
	                               clamp_u16(h, 80));

	if (view->scene_tree != NULL) {
		int ox = 0;
		int oy = 0;
		view_get_content_offset(view, &ox, &oy);
		wlr_scene_node_set_position(&view->scene_tree->node, x - ox, y - oy);
	}

	if (!view->placed && (x != 0 || y != 0))
		view->placed = true;
}

static void handle_xwayland_request_move(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_move);
	if (view->is_dock)
		return;
	if (view->server == NULL || view->server->cursor == NULL)
		return;
	begin_interactive(view, ASWL_CURSOR_MOVE, 0, 0);
}

static void handle_xwayland_request_resize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xwayland_resize_event *event = data;
	if (view->is_dock)
		return;
	if (view->server == NULL || view->server->cursor == NULL || event == NULL)
		return;
	begin_interactive(view, ASWL_CURSOR_RESIZE, event->edges, 0);
}

static void handle_xwayland_request_fullscreen(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_fullscreen);
	if (view == NULL || view->xwayland_surface == NULL)
		return;
	if (view->is_dock)
		return;

	bool requested = view->xwayland_surface->fullscreen;
	fprintf(stderr, "aswlcomp: xwayland request_fullscreen=%d\n", requested ? 1 : 0);
	view_set_fullscreen(view, requested);

	aswl_schedule_flush(view->server);
}

static void handle_xwayland_request_maximize(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_view *view = wl_container_of(listener, view, request_maximize);
	if (view == NULL || view->xwayland_surface == NULL)
		return;
	if (view->is_dock)
		return;

	bool requested = view->xwayland_surface->maximized_horz || view->xwayland_surface->maximized_vert;
	fprintf(stderr, "aswlcomp: xwayland request_maximize=%d\n", requested ? 1 : 0);
	view_set_maximized(view, requested);

	aswl_schedule_flush(view->server);
}

static void handle_xwayland_request_minimize(struct wl_listener *listener, void *data)
{
	struct aswl_view *view = wl_container_of(listener, view, request_minimize);
	struct wlr_xwayland_minimize_event *event = data;
	if (view == NULL || view->xwayland_surface == NULL || event == NULL)
		return;
	if (event->surface != view->xwayland_surface)
		return;
	if (view->server == NULL)
		return;

	fprintf(stderr, "aswlcomp: xwayland request_minimize=%d\n", event->minimize ? 1 : 0);
	wlr_xwayland_surface_set_minimized(view->xwayland_surface, event->minimize);

	if (view->scene_tree != NULL) {
		bool enabled = view->mapped && !event->minimize &&
		               (view->is_dock || view->workspace == view->server->current_workspace);
		wlr_scene_node_set_enabled(&view->scene_tree->node, enabled);
	}

	if (event->minimize && view->server->focused_view == view) {
		view->server->focused_view = NULL;
		wlr_seat_keyboard_notify_clear_focus(view->server->seat);
		aswl_ime_set_focus(view->server, NULL);
		focus_topmost_view(view->server);
	}

	view_update_toplevel_protocols(view);
	broadcast_window_state(view->server, view);
	aswl_schedule_flush(view->server);
}

static void handle_new_xwayland_surface(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *xsurface = data;

	if (xsurface == NULL)
		return;

	struct aswl_view *view = calloc(1, sizeof(*view));
	if (view == NULL)
		return;

	view->server = server;
	view->type = ASWL_VIEW_XWAYLAND;
	view->xwayland_surface = xsurface;
	view->workspace = server->current_workspace;
	view->id = server->next_view_id++;
	if (server->next_view_id == 0)
		server->next_view_id = 1;
	xsurface->data = view;

	wl_list_insert(server->views.prev, &view->link);

	view->destroy.notify = handle_view_destroy;
	wl_signal_add(&xsurface->events.destroy, &view->destroy);

	view->set_title.notify = handle_view_set_title;
	wl_signal_add(&xsurface->events.set_title, &view->set_title);

	view->set_class.notify = handle_view_set_class;
	wl_signal_add(&xsurface->events.set_class, &view->set_class);

	view->xwayland_associate.notify = handle_xwayland_associate;
	wl_signal_add(&xsurface->events.associate, &view->xwayland_associate);

	view->xwayland_dissociate.notify = handle_xwayland_dissociate;
	wl_signal_add(&xsurface->events.dissociate, &view->xwayland_dissociate);

	view->xwayland_map_request.notify = handle_xwayland_map_request;
	wl_signal_add(&xsurface->events.map_request, &view->xwayland_map_request);

	view->xwayland_request_configure.notify = handle_xwayland_request_configure;
	wl_signal_add(&xsurface->events.request_configure, &view->xwayland_request_configure);

	view->request_move.notify = handle_xwayland_request_move;
	wl_signal_add(&xsurface->events.request_move, &view->request_move);

	view->request_resize.notify = handle_xwayland_request_resize;
	wl_signal_add(&xsurface->events.request_resize, &view->request_resize);

	view->request_fullscreen.notify = handle_xwayland_request_fullscreen;
	wl_signal_add(&xsurface->events.request_fullscreen, &view->request_fullscreen);

	view->request_maximize.notify = handle_xwayland_request_maximize;
	wl_signal_add(&xsurface->events.request_maximize, &view->request_maximize);

	view->request_minimize.notify = handle_xwayland_request_minimize;
	wl_signal_add(&xsurface->events.request_minimize, &view->request_minimize);

	/* Xwayland may already have an associated wlr_surface. */
	xwayland_attach_surface(view);
}

static void handle_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *toplevel = data;
	struct wlr_xdg_surface *xdg_surface = toplevel != NULL ? toplevel->base : NULL;

	if (xdg_surface == NULL || xdg_surface->toplevel == NULL)
		return;

	struct aswl_view *view = calloc(1, sizeof(*view));
	if (view == NULL)
		return;

	view->server = server;
	view->type = ASWL_VIEW_XDG;
	view->xdg_surface = xdg_surface;
	view->workspace = server->current_workspace;
	view->id = server->next_view_id++;
	if (server->next_view_id == 0)
		server->next_view_id = 1;
	if (!view_create_frame_scene(view)) {
		free(view);
		return;
	}

	view->surface_tree = wlr_scene_xdg_surface_create(view->content_tree, xdg_surface);
	if (view->surface_tree == NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		free(view);
		return;
	}

	view->scene_destroy.notify = handle_view_scene_destroy;
	wl_signal_add(&view->scene_tree->node.events.destroy, &view->scene_destroy);
	view->scene_destroy_listener_added = true;

	xdg_surface->data = view;
	wl_list_insert(server->views.prev, &view->link);

	/* Start hidden until the client maps. */
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	view->map.notify = handle_view_map;
	wl_signal_add(&xdg_surface->surface->events.map, &view->map);

	view->unmap.notify = handle_view_unmap;
	wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
	view->surface_listeners_added = true;

	view->commit.notify = handle_view_commit;
	wl_signal_add(&xdg_surface->surface->events.commit, &view->commit);
	view->commit_listener_added = true;

	view->surface_destroy.notify = handle_view_surface_destroy;
	wl_signal_add(&xdg_surface->surface->events.destroy, &view->surface_destroy);
	view->surface_destroy_listener_added = true;

	view->destroy.notify = handle_view_destroy;
	/* Must listen to toplevel destroy to remove toplevel event listeners before wlroots frees it. */
	wl_signal_add(&xdg_surface->toplevel->events.destroy, &view->destroy);

	view->set_title.notify = handle_view_set_title;
	wl_signal_add(&xdg_surface->toplevel->events.set_title, &view->set_title);

	view->set_app_id.notify = handle_view_set_app_id;
	wl_signal_add(&xdg_surface->toplevel->events.set_app_id, &view->set_app_id);

	view->request_move.notify = handle_request_move;
	wl_signal_add(&xdg_surface->toplevel->events.request_move, &view->request_move);

	view->request_resize.notify = handle_request_resize;
	wl_signal_add(&xdg_surface->toplevel->events.request_resize, &view->request_resize);

	view->request_fullscreen.notify = handle_request_fullscreen;
	wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen, &view->request_fullscreen);

	view->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&xdg_surface->toplevel->events.request_maximize, &view->request_maximize);

	view->request_minimize.notify = handle_request_minimize;
	wl_signal_add(&xdg_surface->toplevel->events.request_minimize, &view->request_minimize);

	/* Initial placement (very naive for now). */
	wlr_scene_node_set_position(&view->scene_tree->node, 80, 120);
	view_update_decorations(view);
}

static void handle_xdg_deco_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_xdg_deco *xd = wl_container_of(listener, xd, destroy);
	if (xd == NULL)
		return;

	wl_list_remove(&xd->request_mode.link);
	wl_list_remove(&xd->destroy.link);
	free(xd);
}

static void handle_xdg_deco_request_mode(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_xdg_deco *xd = wl_container_of(listener, xd, request_mode);
	if (xd == NULL || xd->deco == NULL)
		return;

	/* Prefer server-side decorations so clients don't draw their own CSD frame. */
	wlr_xdg_toplevel_decoration_v1_set_mode(xd->deco, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void handle_new_xdg_decoration(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_xdg_decoration);
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	if (server == NULL || deco == NULL)
		return;

	struct aswl_xdg_deco *xd = calloc(1, sizeof(*xd));
	if (xd == NULL)
		return;

	xd->server = server;
	xd->deco = deco;
	deco->data = xd;

	xd->request_mode.notify = handle_xdg_deco_request_mode;
	wl_signal_add(&deco->events.request_mode, &xd->request_mode);

	xd->destroy.notify = handle_xdg_deco_destroy;
	wl_signal_add(&deco->events.destroy, &xd->destroy);

	/* Enforce server-side mode on creation. */
	wlr_xdg_toplevel_decoration_v1_set_mode(deco, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);

	struct wlr_xdg_surface *xdg_surface = deco->toplevel != NULL ? deco->toplevel->base : NULL;
	struct aswl_view *view = xdg_surface != NULL ? xdg_surface->data : NULL;
	fprintf(stderr, "aswlcomp: xdg-decoration: server-side for app_id=%s title=%s\n", view_app_id(view), view_title(view));
}

static void handle_keyboard_key(struct wl_listener *listener, void *data)
{
	struct aswl_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct aswl_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	aswl_ime_maybe_set_keyboard_grab(server, keyboard->wlr_keyboard);
	aswl_idle_note_activity(server);

	if (server->session_locked) {
		wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			server->last_user_serial = wl_display_get_serial(server->display);
			server->last_user_time_msec = aswl_now_msec();
		}
		return;
	}

	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		uint32_t mods = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
		uint32_t mods_masked = mods & (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT | WLR_MODIFIER_LOGO);
		uint32_t keycode = event->keycode + 8;

		const xkb_keysym_t *syms = NULL;
		int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);
		if (nsyms == 1) {
			xkb_keysym_t sym = syms[0];

			if (sym == XKB_KEY_Escape && (mods_masked & WLR_MODIFIER_ALT) != 0) {
				fprintf(stderr, "aswlcomp: Alt+Escape: exit\n");
				wl_display_terminate(server->display);
				return;
			}

			struct aswl_binding *b;
			wl_list_for_each(b, &server->bindings, link) {
				if (b->mods == mods_masked && b->keysym == sym) {
					fprintf(stderr, "aswlcomp: bind action=%s\n", binding_action_name(b->action));
					server->last_user_serial = wl_display_next_serial(server->display);
					server->last_user_time_msec = aswl_now_msec();
					switch (b->action) {
					case ASWL_BINDING_QUIT:
						wl_display_terminate(server->display);
						break;
					case ASWL_BINDING_CLOSE_FOCUSED:
						close_focused_view(server);
						break;
					case ASWL_BINDING_FOCUS_NEXT:
						focus_next_view(server);
						break;
					case ASWL_BINDING_FOCUS_PREV:
						focus_prev_view(server);
						break;
					case ASWL_BINDING_WORKSPACE_SET:
						set_workspace(server, b->workspace);
						break;
					case ASWL_BINDING_WORKSPACE_NEXT:
						workspace_next(server);
						break;
						case ASWL_BINDING_WORKSPACE_PREV:
							workspace_prev(server);
							break;
						case ASWL_BINDING_TOGGLE_FULLSCREEN:
							if (server->grabbed_view != NULL)
								end_interactive(server);
							if (server->focused_view != NULL)
								view_set_fullscreen(server->focused_view, !view_is_fullscreen(server->focused_view));
							break;
						case ASWL_BINDING_TOGGLE_MAXIMIZED:
								if (server->grabbed_view != NULL)
									end_interactive(server);
								if (server->focused_view != NULL)
									view_set_maximized(server->focused_view, !view_is_maximized(server->focused_view));
								break;
						case ASWL_BINDING_LOCK:
							spawn_lock(server);
							break;
						case ASWL_BINDING_EXEC:
						default:
								if (b->command != NULL && b->command[0] != '\0') {
									fprintf(stderr, "aswlcomp: exec: %s\n", b->command);
									spawn_command_with_activation(server, b->command);
						}
						break;
					}
					return;
				}
			}
		}
	}

	aswl_ime_notify_key(server, event);
	wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		server->last_user_serial = wl_display_get_serial(server->display);
		server->last_user_time_msec = aswl_now_msec();
	}
}

static void handle_keyboard_modifiers(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	struct aswl_server *server = keyboard->server;

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	aswl_ime_maybe_set_keyboard_grab(server, keyboard->wlr_keyboard);
	aswl_idle_note_activity(server);
	aswl_ime_notify_modifiers(server, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(server->seat, &keyboard->wlr_keyboard->modifiers);
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);

	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void handle_new_input(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_POINTER:
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_TABLET:
		wlr_cursor_attach_input_device(server->cursor, device);
		wlr_seat_set_capabilities(server->seat, server->seat->capabilities | WL_SEAT_CAPABILITY_POINTER);

		struct aswl_pointer_device *pd = calloc(1, sizeof(*pd));
		if (pd != NULL) {
			pd->server = server;
			pd->device = device;
			wl_list_insert(&server->pointer_devices, &pd->link);

			pd->destroy.notify = handle_pointer_device_destroy;
			wl_signal_add(&device->events.destroy, &pd->destroy);
		}

		aswl_apply_pointer_device_config(server, device);
		break;
	case WLR_INPUT_DEVICE_KEYBOARD:
	{
		if (getenv("ASWLCOMP_DISABLE_KEYBOARD") != NULL) {
			fprintf(stderr, "aswlcomp: ignoring keyboard device (ASWLCOMP_DISABLE_KEYBOARD)\n");
			break;
		}

		struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

		struct aswl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
		if (keyboard == NULL)
			return;

		keyboard->server = server;
		keyboard->wlr_keyboard = wlr_keyboard;
		keyboard->has_keymap = false;

		aswl_apply_keyboard_device_config(server, keyboard);

		keyboard->key.notify = handle_keyboard_key;
		wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

		keyboard->modifiers.notify = handle_keyboard_modifiers;
		wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

		keyboard->destroy.notify = handle_keyboard_destroy;
		wl_signal_add(&device->events.destroy, &keyboard->destroy);

		wl_list_insert(&server->keyboards, &keyboard->link);

		wlr_seat_set_keyboard(server->seat, wlr_keyboard);
		wlr_seat_set_capabilities(server->seat, server->seat->capabilities | WL_SEAT_CAPABILITY_KEYBOARD);
		break;
	}
	default:
		break;
	}
}

static void handle_request_cursor(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;

	if (server->cursor_mode != ASWL_CURSOR_PASSTHROUGH)
		return;

	if (server->seat->pointer_state.focused_client != event->seat_client)
		return;

	wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

static void handle_request_set_selection(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	if (server == NULL || server->seat == NULL || event == NULL)
		return;

	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void handle_request_set_primary_selection(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	if (server == NULL || server->seat == NULL || event == NULL)
		return;

	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

static void handle_xdg_activation_new_token(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, xdg_activation_new_token);
	struct wlr_xdg_activation_token_v1 *token = data;
	if (server == NULL || token == NULL || server->seat == NULL)
		return;

	/* Keep compositor-generated tokens as-is. */
	if (token->data != NULL)
		return;

	bool ok = true;
	if (token->seat == NULL || token->seat != server->seat || token->serial == 0)
		ok = false;
	if (token->serial != server->last_user_serial)
		ok = false;
	if (token->surface == NULL || !aswl_surface_is_focused_for_activation(server, token->surface))
		ok = false;
	if (!aswl_user_event_is_recent(server, aswl_now_msec()))
		ok = false;

	token->data = ok ? (void *)1 : NULL;

	if (getenv("ASWLCOMP_DEBUG_ACTIVATION") != NULL) {
		fprintf(stderr,
		        "aswlcomp: xdg-activation: new_token serial=%u focused=%d recent=%d -> %s\n",
		        token->serial,
		        token->surface != NULL && aswl_surface_is_focused_for_activation(server, token->surface),
		        aswl_user_event_is_recent(server, aswl_now_msec()),
		        ok ? "ok" : "reject");
	}
}

static void handle_xdg_activation_request_activate(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, xdg_activation_request_activate);
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	if (server == NULL || event == NULL)
		return;

	if (server->session_locked)
		return;

	if (event->token == NULL || event->surface == NULL)
		return;
	if (event->token->data == NULL)
		return;

	struct aswl_view *view = view_from_wlr_surface(event->surface);
	if (view == NULL)
		return;

	if (view->scene_tree == NULL)
		return;

	if (!view->is_dock && view->workspace != server->current_workspace)
		set_workspace(server, view->workspace);

	if (getenv("ASWLCOMP_DEBUG_ACTIVATION") != NULL) {
		fprintf(stderr,
		        "aswlcomp: xdg-activation: activate app_id=%s title=%s\n",
		        view_app_id(view),
		        view_title(view));
	}

	focus_view(view, NULL);
	aswl_schedule_flush(server);
}

static void process_cursor_motion(struct aswl_server *server, uint32_t time_msec)
{
	if (server->cursor_mode == ASWL_CURSOR_MOVE) {
		struct aswl_view *view = server->grabbed_view;
		if (view == NULL)
			return;

		double dx = server->cursor->x - server->grab_lx;
		double dy = server->cursor->y - server->grab_ly;

		int nx = server->grab_view_lx + (int)dx;
		int ny = server->grab_view_ly + (int)dy;
		wlr_scene_node_set_position(&view->scene_tree->node, nx, ny);
		if (view->xwayland_surface != NULL) {
			int w = server->grab_view_width;
			int h = server->grab_view_height;
			if (w <= 0)
				w = view->xwayland_surface->width;
			if (h <= 0)
				h = view->xwayland_surface->height;
			if (w > 0 && h > 0) {
				int border = 0;
				int title_h = 0;
				view_get_deco_metrics(view, &border, &title_h);
				int ox = border;
				int oy = title_h;
				int cw = w - 2 * border;
				int ch = h - title_h - border;
				if (cw < 1)
					cw = 1;
				if (ch < 1)
					ch = 1;
				wlr_xwayland_surface_configure(view->xwayland_surface,
				                               nx + ox,
				                               ny + oy,
				                               (uint16_t)cw,
				                               (uint16_t)ch);
			}
		}
		return;
	}

	if (server->cursor_mode == ASWL_CURSOR_RESIZE) {
		struct aswl_view *view = server->grabbed_view;
		if (view == NULL)
			return;

		int border = 0;
		int title_h = 0;
		view_get_deco_metrics(view, &border, &title_h);

		double dx = server->cursor->x - server->grab_lx;
		double dy = server->cursor->y - server->grab_ly;

		int dx_i = (int)dx;
		int dy_i = (int)dy;

		int right_edge = server->grab_view_lx + server->grab_view_width;
		int bottom_edge = server->grab_view_ly + server->grab_view_height;

		int nx = server->grab_view_lx;
		int ny = server->grab_view_ly;
		int nw = server->grab_view_width;
		int nh = server->grab_view_height;

		if ((server->grab_edges & WLR_EDGE_LEFT) != 0) {
			nx = server->grab_view_lx + dx_i;
			nw = server->grab_view_width - dx_i;
		} else if ((server->grab_edges & WLR_EDGE_RIGHT) != 0) {
			nw = server->grab_view_width + dx_i;
		}

		if ((server->grab_edges & WLR_EDGE_TOP) != 0) {
			ny = server->grab_view_ly + dy_i;
			nh = server->grab_view_height - dy_i;
		} else if ((server->grab_edges & WLR_EDGE_BOTTOM) != 0) {
			nh = server->grab_view_height + dy_i;
		}

		int min_w = 1;
		int min_h = 1;
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
			min_w = view->xdg_surface->toplevel->current.min_width;
			min_h = view->xdg_surface->toplevel->current.min_height;
			if (min_w <= 0)
				min_w = 1;
			if (min_h <= 0)
				min_h = 1;
		}

		int min_fw = min_w + 2 * border;
		int min_fh = min_h + title_h + border;

		if (nw < min_fw) {
			nw = min_fw;
			if ((server->grab_edges & WLR_EDGE_LEFT) != 0)
				nx = right_edge - nw;
		}
		if (nh < min_fh) {
			nh = min_fh;
			if ((server->grab_edges & WLR_EDGE_TOP) != 0)
				ny = bottom_edge - nh;
		}

		int cw = nw - 2 * border;
		int ch = nh - title_h - border;
		if (cw < 1)
			cw = 1;
		if (ch < 1)
			ch = 1;

		if ((server->grab_edges & WLR_EDGE_LEFT) != 0 || (server->grab_edges & WLR_EDGE_TOP) != 0)
			wlr_scene_node_set_position(&view->scene_tree->node, nx, ny);
		if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
			(void)wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, cw, ch);
		} else if (view->xwayland_surface != NULL) {
			wlr_xwayland_surface_configure(view->xwayland_surface,
			                               nx + border,
			                               ny + title_h,
			                               (uint16_t)cw,
			                               (uint16_t)ch);
		}
		return;
	}

	double sx = 0;
	double sy = 0;
	struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	if (surface == NULL) {
		wlr_seat_pointer_notify_clear_focus(server->seat);
		aswl_pointer_constraints_update(server, NULL);
		return;
	}

	wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
	aswl_pointer_constraints_update(server, surface);
	wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
}

static void aswl_confine_point_to_region(const pixman_region32_t *region,
                                        int width,
                                        int height,
                                        double *sx,
                                        double *sy)
{
	if (sx == NULL || sy == NULL)
		return;

	int px = (int)*sx;
	int py = (int)*sy;

	if (width > 0)
		px = clamp_int(px, 0, width - 1);
	if (height > 0)
		py = clamp_int(py, 0, height - 1);

	/* An empty region means the whole surface. */
	if (region == NULL || !pixman_region32_not_empty((pixman_region32_t *)region)) {
		*sx = (double)px;
		*sy = (double)py;
		return;
	}

	if (pixman_region32_contains_point((pixman_region32_t *)region, px, py, NULL)) {
		*sx = (double)px;
		*sy = (double)py;
		return;
	}

	int nrects = 0;
	pixman_box32_t *rects = pixman_region32_rectangles((pixman_region32_t *)region, &nrects);
	if (rects == NULL || nrects <= 0) {
		*sx = (double)px;
		*sy = (double)py;
		return;
	}

	int best_x = px;
	int best_y = py;
	int64_t best_d2 = INT64_MAX;

	for (int i = 0; i < nrects; i++) {
		int x1 = rects[i].x1;
		int y1 = rects[i].y1;
		int x2 = rects[i].x2 - 1;
		int y2 = rects[i].y2 - 1;
		if (x2 < x1 || y2 < y1)
			continue;

		int cx = clamp_int(px, x1, x2);
		int cy = clamp_int(py, y1, y2);
		int64_t dx = (int64_t)px - (int64_t)cx;
		int64_t dy = (int64_t)py - (int64_t)cy;
		int64_t d2 = dx * dx + dy * dy;
		if (d2 < best_d2) {
			best_d2 = d2;
			best_x = cx;
			best_y = cy;
		}
	}

	*sx = (double)best_x;
	*sy = (double)best_y;
}

static void aswl_relative_pointer_send_motion(struct aswl_server *server,
                                             uint32_t time_msec,
                                             double dx,
                                             double dy,
                                             double dx_unaccel,
                                             double dy_unaccel)
{
	if (server == NULL || server->relative_pointer_manager == NULL || server->seat == NULL)
		return;

	uint64_t time_usec = (uint64_t)time_msec * 1000u;
	wlr_relative_pointer_manager_v1_send_relative_motion(server->relative_pointer_manager,
	                                                     server->seat,
	                                                     time_usec,
	                                                     dx,
	                                                     dy,
	                                                     dx_unaccel,
	                                                     dy_unaccel);
}

static void aswl_cursor_apply_pointer_constraints_delta(struct aswl_server *server,
                                                       struct wlr_input_device *device,
                                                       double dx,
                                                       double dy)
{
	if (server == NULL || server->cursor == NULL || device == NULL)
		return;

	struct wlr_pointer_constraint_v1 *constraint = server->active_pointer_constraint;
	if (constraint == NULL || constraint->surface == NULL ||
	    server->session_locked || server->cursor_mode != ASWL_CURSOR_PASSTHROUGH) {
		wlr_cursor_move(server->cursor, device, dx, dy);
		return;
	}

	if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED && server->pointer_constraint_locked) {
		wlr_cursor_warp_closest(server->cursor, device, server->pointer_constraint_lx, server->pointer_constraint_ly);
		server->pointer_constraint_lx = server->cursor->x;
		server->pointer_constraint_ly = server->cursor->y;
		return;
	}

	if (constraint->type != WLR_POINTER_CONSTRAINT_V1_CONFINED) {
		wlr_cursor_move(server->cursor, device, dx, dy);
		return;
	}

	double lx = server->cursor->x;
	double ly = server->cursor->y;

	double sx = 0.0;
	double sy = 0.0;
	struct wlr_surface *surface = surface_at(server, lx, ly, &sx, &sy);
	if (surface == NULL || surface != constraint->surface) {
		wlr_cursor_move(server->cursor, device, dx, dy);
		return;
	}

	double new_sx = sx + dx;
	double new_sy = sy + dy;
	aswl_confine_point_to_region(&constraint->region, surface->current.width, surface->current.height, &new_sx, &new_sy);

	double origin_x = lx - sx;
	double origin_y = ly - sy;

	wlr_cursor_warp_closest(server->cursor, device, origin_x + new_sx, origin_y + new_sy);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	aswl_idle_note_activity(server);
	aswl_relative_pointer_send_motion(server,
	                                 event->time_msec,
	                                 event->delta_x,
	                                 event->delta_y,
	                                 event->unaccel_dx,
	                                 event->unaccel_dy);
	aswl_cursor_apply_pointer_constraints_delta(server, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;

	aswl_idle_note_activity(server);
	double old_lx = server->cursor->x;
	double old_ly = server->cursor->y;

	double target_lx = old_lx;
	double target_ly = old_ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->pointer->base, event->x, event->y, &target_lx, &target_ly);

	double dx = target_lx - old_lx;
	double dy = target_ly - old_ly;
	aswl_relative_pointer_send_motion(server, event->time_msec, dx, dy, dx, dy);
	aswl_cursor_apply_pointer_constraints_delta(server, &event->pointer->base, dx, dy);
	process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_button(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	aswl_idle_note_activity(server);
	uint32_t serial = wlr_seat_pointer_notify_button(server->seat,
	                                                event->time_msec,
	                                                event->button,
	                                                event->state);
	if (serial != 0 && event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		server->last_user_serial = serial;
		server->last_user_time_msec = aswl_now_msec();
	}

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED && server->cursor_mode != ASWL_CURSOR_PASSTHROUGH) {
		if (server->grab_button == 0 || server->grab_button == event->button)
			end_interactive(server);
		return;
	}

	if (event->state != WL_POINTER_BUTTON_STATE_PRESSED)
		return;

	double sx = 0.0;
	double sy = 0.0;
	struct wlr_surface *surface = NULL;
	struct aswl_view *view = NULL;

	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, server->cursor->x, server->cursor->y, &sx, &sy);
	if (node != NULL && node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
		struct wlr_scene_surface *scene_surface = scene_buffer != NULL ? wlr_scene_surface_try_from_buffer(scene_buffer) : NULL;
		if (scene_surface != NULL) {
			surface = scene_surface->surface;
			view = view_from_wlr_surface(surface);
		}
	}

	for (struct wlr_scene_node *n = node; view == NULL && n != NULL; n = n->parent != NULL ? &n->parent->node : NULL) {
		if (n->data != NULL)
			view = n->data;
	}

	if (view != NULL) {
		if (view->scene_tree != NULL)
			wlr_scene_node_raise_to_top(&view->scene_tree->node);
		if (!view->is_dock)
			focus_view(view, surface);
	}

	if (view != NULL && view->deco_titlebar != NULL && node == &view->deco_titlebar->node &&
	    event->button == BTN_LEFT && !view->is_dock) {
		int lx = (int)sx;
		int ly = (int)sy;
		if (lx >= view->deco_close_x && lx < view->deco_close_x + view->deco_close_w &&
		    ly >= view->deco_close_y && ly < view->deco_close_y + view->deco_close_h) {
			if (view->xdg_surface != NULL && view->xdg_surface->toplevel != NULL) {
				wlr_xdg_toplevel_send_close(view->xdg_surface->toplevel);
			} else if (view->xwayland_surface != NULL) {
				wlr_xwayland_surface_close(view->xwayland_surface);
			}
			return;
		}
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	uint32_t mods = keyboard != NULL ? wlr_keyboard_get_modifiers(keyboard) : 0;
	if ((mods & WLR_MODIFIER_ALT) == 0 || view == NULL || view->is_dock)
		return;

	if (event->button == BTN_LEFT) {
		begin_interactive(view, ASWL_CURSOR_MOVE, 0, event->button);
		return;
	}

	if (event->button == BTN_RIGHT) {
		int vx = 0;
		int vy = 0;
		(void)wlr_scene_node_coords(&view->scene_tree->node, &vx, &vy);

		int width = 0;
		int height = 0;
		view_get_frame_size(view, &width, &height);

		uint32_t edges = 0;
		if (width > 0) {
			int local_x = (int)server->cursor->x - vx;
			edges |= local_x < width / 2 ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
		} else {
			edges |= WLR_EDGE_RIGHT;
		}

		if (height > 0) {
			int local_y = (int)server->cursor->y - vy;
			edges |= local_y < height / 2 ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
		} else {
			edges |= WLR_EDGE_BOTTOM;
		}

		begin_interactive(view, ASWL_CURSOR_RESIZE, edges, event->button);
		return;
	}
}

static void handle_cursor_axis(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	aswl_idle_note_activity(server);
	wlr_seat_pointer_notify_axis(server->seat,
	                            event->time_msec,
	                            event->orientation,
	                            event->delta,
	                            event->delta_discrete,
	                            event->source,
	                            event->relative_direction);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

static struct wlr_scene_tree *layer_tree_for(struct aswl_server *server, enum zwlr_layer_shell_v1_layer layer)
{
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		return server->layer_trees[layer];
	default:
		return server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP];
	}
}

static void focus_lock_surface(struct aswl_server *server, struct wlr_surface *surface)
{
	if (server == NULL || surface == NULL || server->seat == NULL)
		return;

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	struct wlr_keyboard_modifiers mods = { 0 };
	if (keyboard != NULL) {
		mods = keyboard->modifiers;
		wlr_seat_set_keyboard(server->seat, keyboard);
	}

	wlr_seat_keyboard_notify_enter(server->seat,
	                              surface,
	                              keyboard != NULL ? keyboard->keycodes : NULL,
	                              keyboard != NULL ? keyboard->num_keycodes : 0,
	                              &mods);
	aswl_ime_set_focus(server, NULL);
}

static void focus_any_lock_surface(struct aswl_server *server)
{
	if (server == NULL || server->seat == NULL)
		return;

	struct aswl_lock_surface *ls;
	wl_list_for_each(ls, &server->lock_surfaces, link) {
		if (ls == NULL || ls->lock_surface == NULL || ls->lock_surface->surface == NULL)
			continue;
		if (!ls->lock_surface->surface->mapped)
			continue;
		focus_lock_surface(server, ls->lock_surface->surface);
		return;
	}

	wlr_seat_keyboard_notify_clear_focus(server->seat);
	aswl_ime_set_focus(server, NULL);
}

static void lock_surfaces_destroy(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_lock_surface *ls;
	struct aswl_lock_surface *tmp;
	wl_list_for_each_safe(ls, tmp, &server->lock_surfaces, link) {
		wl_list_remove(&ls->link);

		wl_list_remove(&ls->destroy.link);

		if (ls->surface_destroy_listener_added) {
			wl_list_remove(&ls->surface_destroy.link);
			ls->surface_destroy_listener_added = false;
		}
		if (ls->surface_listeners_added) {
			wl_list_remove(&ls->map.link);
			wl_list_remove(&ls->unmap.link);
			ls->surface_listeners_added = false;
		}

		if (ls->scene_tree != NULL)
			wlr_scene_node_destroy(&ls->scene_tree->node);

		free(ls);
	}
}

static void session_lock_apply_scene(struct aswl_server *server)
{
	if (server == NULL)
		return;

	bool locked = server->session_locked;
	if (server->lock_tree != NULL)
		wlr_scene_node_set_enabled(&server->lock_tree->node, locked);

	for (size_t i = 0; i < 4; i++) {
		if (server->layer_trees[i] != NULL)
			wlr_scene_node_set_enabled(&server->layer_trees[i]->node, !locked);
	}
	if (server->xdg_tree != NULL)
		wlr_scene_node_set_enabled(&server->xdg_tree->node, !locked);

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out == NULL || out->wlr_output == NULL)
			continue;
		if (out->lock_rect != NULL)
			wlr_scene_node_set_enabled(&out->lock_rect->node, locked);
		wlr_output_schedule_frame(out->wlr_output);
	}
}

static void session_lock_enter(struct aswl_server *server)
{
	if (server == NULL)
		return;

	server->session_locked = true;
	server->session_lock_sent_locked = false;

	if (server->grabbed_view != NULL)
		end_interactive(server);

	if (server->seat != NULL) {
		wlr_seat_pointer_notify_clear_focus(server->seat);
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		aswl_ime_set_focus(server, NULL);
		aswl_pointer_constraints_update(server, NULL);
	}

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out != NULL)
			out->lock_frame_presented = false;
	}

	if (server->lock_tree != NULL)
		wlr_scene_node_raise_to_top(&server->lock_tree->node);
	session_lock_apply_scene(server);
}

static void session_lock_exit(struct aswl_server *server)
{
	if (server == NULL)
		return;

	server->session_locked = false;
	server->session_lock_sent_locked = false;

	lock_surfaces_destroy(server);
	session_lock_apply_scene(server);

	focus_topmost_view(server);
	aswl_idle_lock_note_activity(server);
}

static void handle_lock_surface_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_lock_surface *ls = wl_container_of(listener, ls, surface_destroy);
	if (ls == NULL)
		return;

	wl_list_remove(&ls->surface_destroy.link);
	ls->surface_destroy_listener_added = false;

	if (ls->surface_listeners_added) {
		wl_list_remove(&ls->map.link);
		wl_list_remove(&ls->unmap.link);
		ls->surface_listeners_added = false;
	}
}

static void handle_lock_surface_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_lock_surface *ls = wl_container_of(listener, ls, map);
	if (ls == NULL || ls->server == NULL || ls->lock_surface == NULL)
		return;

	if (!ls->server->session_locked)
		return;

	if (ls->lock_surface->surface != NULL)
		focus_lock_surface(ls->server, ls->lock_surface->surface);
}

static void handle_lock_surface_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_lock_surface *ls = wl_container_of(listener, ls, unmap);
	if (ls == NULL || ls->server == NULL)
		return;

	if (!ls->server->session_locked)
		return;

	focus_any_lock_surface(ls->server);
}

static void handle_lock_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_lock_surface *ls = wl_container_of(listener, ls, destroy);
	if (ls == NULL)
		return;

	wl_list_remove(&ls->destroy.link);

	if (ls->surface_destroy_listener_added) {
		wl_list_remove(&ls->surface_destroy.link);
		ls->surface_destroy_listener_added = false;
	}
	if (ls->surface_listeners_added) {
		wl_list_remove(&ls->map.link);
		wl_list_remove(&ls->unmap.link);
		ls->surface_listeners_added = false;
	}

	if (ls->scene_tree != NULL)
		wlr_scene_node_destroy(&ls->scene_tree->node);

	wl_list_remove(&ls->link);
	free(ls);
}

static void arrange_lock_surfaces(struct aswl_server *server)
{
	if (server == NULL || server->output_layout == NULL)
		return;

	struct aswl_lock_surface *ls;
	wl_list_for_each(ls, &server->lock_surfaces, link) {
		if (ls == NULL || ls->lock_surface == NULL || ls->lock_surface->output == NULL || ls->scene_tree == NULL)
			continue;

		struct wlr_box box = { 0 };
		wlr_output_layout_get_box(server->output_layout, ls->lock_surface->output, &box);

		wlr_scene_node_set_position(&ls->scene_tree->node, box.x, box.y);

		uint32_t w = box.width > 0 ? (uint32_t)box.width : 1;
		uint32_t h = box.height > 0 ? (uint32_t)box.height : 1;
		if (ls->configured_width != w || ls->configured_height != h) {
			ls->configured_width = w;
			ls->configured_height = h;
			(void)wlr_session_lock_surface_v1_configure(ls->lock_surface, w, h);
		}
	}
}

static void handle_session_lock_new_surface(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, session_lock_new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	if (server == NULL || lock_surface == NULL || lock_surface->surface == NULL)
		return;

	struct aswl_lock_surface *ls = calloc(1, sizeof(*ls));
	if (ls == NULL)
		return;
	ls->server = server;
	ls->lock_surface = lock_surface;
	wl_list_insert(&server->lock_surfaces, &ls->link);

	ls->scene_tree = wlr_scene_subsurface_tree_create(server->lock_tree, lock_surface->surface);
	if (ls->scene_tree == NULL) {
		wl_list_remove(&ls->link);
		free(ls);
		return;
	}

	ls->destroy.notify = handle_lock_surface_destroy;
	wl_signal_add(&lock_surface->events.destroy, &ls->destroy);

	ls->map.notify = handle_lock_surface_map;
	wl_signal_add(&lock_surface->surface->events.map, &ls->map);

	ls->unmap.notify = handle_lock_surface_unmap;
	wl_signal_add(&lock_surface->surface->events.unmap, &ls->unmap);
	ls->surface_listeners_added = true;

	ls->surface_destroy.notify = handle_lock_surface_surface_destroy;
	wl_signal_add(&lock_surface->surface->events.destroy, &ls->surface_destroy);
	ls->surface_destroy_listener_added = true;

	arrange_lock_surfaces(server);
}

static void session_lock_detach(struct aswl_server *server)
{
	if (server == NULL)
		return;
	if (server->session_lock == NULL)
		return;

	wl_list_remove(&server->session_lock_new_surface.link);
	wl_list_remove(&server->session_lock_unlock.link);
	wl_list_remove(&server->session_lock_destroy.link);

	server->session_lock = NULL;
	server->session_lock_sent_locked = false;
}

static void handle_session_lock_unlock(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_server *server = wl_container_of(listener, server, session_lock_unlock);
	if (server == NULL)
		return;

	session_lock_detach(server);
	session_lock_exit(server);
}

static void handle_session_lock_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_server *server = wl_container_of(listener, server, session_lock_destroy);
	if (server == NULL)
		return;

	/*
	 * Client died or otherwise destroyed the lock without unlocking.
	 * Per protocol, do not unlock in response: remain locked and allow recovery
	 * via a new lock client.
	 */
	session_lock_detach(server);
	lock_surfaces_destroy(server);
	session_lock_apply_scene(server);
}

static void handle_new_session_lock(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_session_lock);
	struct wlr_session_lock_v1 *lock = data;
	if (server == NULL || lock == NULL)
		return;

	if (server->session_lock != NULL) {
		wlr_session_lock_v1_destroy(lock);
		return;
	}

	server->session_lock = lock;
	server->session_lock_sent_locked = false;

	server->session_lock_new_surface.notify = handle_session_lock_new_surface;
	wl_signal_add(&lock->events.new_surface, &server->session_lock_new_surface);

	server->session_lock_unlock.notify = handle_session_lock_unlock;
	wl_signal_add(&lock->events.unlock, &server->session_lock_unlock);

	server->session_lock_destroy.notify = handle_session_lock_destroy;
	wl_signal_add(&lock->events.destroy, &server->session_lock_destroy);

	session_lock_enter(server);
}

static void handle_layer_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, destroy);

	wl_list_remove(&ls->destroy.link);
	if (ls->surface_destroy_listener_added) {
		wl_list_remove(&ls->surface_destroy.link);
		ls->surface_destroy_listener_added = false;
	}
	if (ls->surface_listeners_added) {
		wl_list_remove(&ls->map.link);
		wl_list_remove(&ls->unmap.link);
		wl_list_remove(&ls->commit.link);
		ls->surface_listeners_added = false;
	}
	wl_list_remove(&ls->link);
	free(ls);
}

static void handle_layer_surface_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, surface_destroy);
	if (ls == NULL)
		return;

	wl_list_remove(&ls->surface_destroy.link);
	ls->surface_destroy_listener_added = false;

	if (ls->surface_listeners_added) {
		wl_list_remove(&ls->map.link);
		wl_list_remove(&ls->unmap.link);
		wl_list_remove(&ls->commit.link);
		ls->surface_listeners_added = false;
	}
}

static void handle_layer_surface_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, map);
	arrange_layers(ls->server);
}

static void handle_layer_surface_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, unmap);
	arrange_layers(ls->server);
}

static void handle_layer_surface_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, commit);
	arrange_layers(ls->server);
}

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

static void handle_new_layer_surface(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *layer_surface = data;

	struct aswl_layer_surface *ls = calloc(1, sizeof(*ls));
	if (ls == NULL)
		return;

	ls->server = server;
	ls->layer_surface = layer_surface;
	ls->scene = wlr_scene_layer_surface_v1_create(layer_tree_for(server, layer_surface->pending.layer), layer_surface);
	if (ls->scene == NULL) {
		free(ls);
		return;
	}

	wl_list_insert(&server->layer_surfaces, &ls->link);

	ls->destroy.notify = handle_layer_surface_destroy;
	wl_signal_add(&layer_surface->events.destroy, &ls->destroy);

	ls->map.notify = handle_layer_surface_map;
	wl_signal_add(&layer_surface->surface->events.map, &ls->map);

	ls->unmap.notify = handle_layer_surface_unmap;
	wl_signal_add(&layer_surface->surface->events.unmap, &ls->unmap);

	ls->commit.notify = handle_layer_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit, &ls->commit);
	ls->surface_listeners_added = true;

	ls->surface_destroy.notify = handle_layer_surface_surface_destroy;
	wl_signal_add(&layer_surface->surface->events.destroy, &ls->surface_destroy);
	ls->surface_destroy_listener_added = true;

	arrange_layers(server);
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

static void handle_output_manager_apply(struct wl_listener *listener, void *data)
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

static void handle_output_manager_test(struct wl_listener *listener, void *data)
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

static void handle_new_output(struct wl_listener *listener, void *data)
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

static void arrange_layers_for_output(struct aswl_output *out)
{
	if (out == NULL || out->server == NULL || out->wlr_output == NULL)
		return;

	struct aswl_server *server = out->server;
	struct wlr_output *output = out->wlr_output;

	struct wlr_box full = { 0 };
	wlr_output_layout_get_box(server->output_layout, output, &full);
	struct wlr_box usable = full;

	struct wlr_output *default_output = wlr_output_layout_get_center_output(server->output_layout);
	if (default_output == NULL)
		default_output = output;

	const enum zwlr_layer_shell_v1_layer order[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
		ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
	};

	for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		enum zwlr_layer_shell_v1_layer layer = order[i];

		struct aswl_layer_surface *ls;
		wl_list_for_each(ls, &server->layer_surfaces, link) {
			struct wlr_layer_surface_v1 *surf = ls->layer_surface;
			if (surf == NULL)
				continue;

			struct wlr_output *target = surf->output;
			if (target == NULL)
				target = default_output;
			if (target != output)
				continue;

			if (surf->current.layer != layer)
				continue;

			if (!surf->initialized)
				continue;

			wlr_scene_layer_surface_v1_configure(ls->scene, &full, &usable);
		}
	}

	if (out->lock_rect != NULL) {
		wlr_scene_rect_set_size(out->lock_rect, full.width, full.height);
		wlr_scene_node_set_position(&out->lock_rect->node, full.x, full.y);
		wlr_scene_node_set_enabled(&out->lock_rect->node, server->session_locked);
	}

	out->full_box = full;
	out->usable_box = usable;
}

static void arrange_layers(struct aswl_server *server)
{
	if (server == NULL || server->output_layout == NULL)
		return;

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link)
		arrange_layers_for_output(out);

	if (server->session_locked)
		arrange_lock_surfaces(server);

	arrange_dock_views(server);
}

int main(int argc, char **argv)
{
	const char *socket_name = NULL;
	const char *autostart_path = NULL;
	const char **spawn_cmds = NULL;
	size_t spawn_count = 0;
	const char *state_path_override = NULL;

	for (int i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
			usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--socket") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 2;
			}
			socket_name = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--autostart") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 2;
			}
			autostart_path = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--state") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 2;
			}
			state_path_override = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--spawn") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 2;
			}
			const char **new_spawn = realloc(spawn_cmds, (spawn_count + 1) * sizeof(*new_spawn));
			if (new_spawn == NULL) {
				fprintf(stderr, "aswlcomp: realloc failed\n");
				return 1;
			}
			spawn_cmds = new_spawn;
			spawn_cmds[spawn_count++] = argv[++i];
			continue;
		}

		fprintf(stderr, "aswlcomp: unknown argument: %s\n", argv[i]);
		usage(argv[0]);
		return 2;
	}

	wlr_log_init(WLR_INFO, NULL);

	struct aswl_server server = { 0 };
	server.current_workspace = 1;
	server.workspace_count = 9;
	server.next_view_id = 1;
	server.repeat_rate = 25;
	server.repeat_delay = 600;
	wl_list_init(&server.outputs_persist);
	wl_list_init(&server.idle_inhibitors);

	aswl_theme_init_default(&server.theme);
	(void)aswl_theme_load(&server.theme);
	aswl_font_init(&server.deco_font);
	aswl_font_init(&server.deco_font_inactive);
	const char *deco_font = server.theme.frame_font != NULL ? server.theme.frame_font : server.theme.panel_font;
	(void)aswl_font_load(&server.deco_font, deco_font);
	const char *deco_inactive_font = server.theme.frame_inactive_font != NULL ? server.theme.frame_inactive_font : deco_font;
	(void)aswl_font_load(&server.deco_font_inactive, deco_inactive_font);
	aswl_dock_config_init(&server.dock);

	bool workspace_count_from_env = false;
	const char *ws_env = getenv("ASWLCOMP_WORKSPACES");
	if (ws_env != NULL && ws_env[0] != '\0') {
		char *end = NULL;
		unsigned long n = strtoul(ws_env, &end, 10);
		if (end != ws_env && *end == '\0' && n > 0 && n <= 1000) {
			server.workspace_count = (uint32_t)n;
			workspace_count_from_env = true;
		}
	}

	server.state_path = aswl_state_path_resolve(state_path_override);
	aswl_state_load(&server, !workspace_count_from_env);

		server.display = wl_display_create();
		if (server.display == NULL) {
			fprintf(stderr, "aswlcomp: wl_display_create failed\n");
			return 1;
		}

		struct wl_event_loop *loop = wl_display_get_event_loop(server.display);

		const char *lock_cmd_env = getenv("ASWLCOMP_LOCK_CMD");
		if (lock_cmd_env != NULL && lock_cmd_env[0] != '\0')
			server.lock_command = strdup(lock_cmd_env);
		else
			server.lock_command = strdup("aswllock");

		const char *idle_lock_env = getenv("ASWLCOMP_IDLE_LOCK_SECONDS");
		if (idle_lock_env != NULL && idle_lock_env[0] != '\0') {
			char *end = NULL;
			long v = strtol(idle_lock_env, &end, 10);
			if (end != idle_lock_env && end != NULL && *end == '\0' && v >= 0 && v <= 86400)
				server.idle_lock_seconds = (int)v;
			else
				fprintf(stderr, "aswlcomp: bad ASWLCOMP_IDLE_LOCK_SECONDS=%s\n", idle_lock_env);
		}
		if (server.idle_lock_seconds > 0 && loop != NULL) {
			server.idle_lock_timer = wl_event_loop_add_timer(loop, aswl_idle_lock_timer_cb, &server);
			if (server.idle_lock_timer != NULL)
				(void)wl_event_source_timer_update(server.idle_lock_timer, server.idle_lock_seconds * 1000);
		}

		server.backend = wlr_backend_autocreate(loop, NULL);
		if (server.backend == NULL) {
			fprintf(stderr, "aswlcomp: wlr_backend_autocreate failed\n");
			return 1;
		}

	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		fprintf(stderr, "aswlcomp: wlr_renderer_autocreate failed\n");
		return 1;
	}
	wlr_renderer_init_wl_display(server.renderer, server.display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (server.allocator == NULL) {
		fprintf(stderr, "aswlcomp: wlr_allocator_autocreate failed\n");
		return 1;
	}

		server.compositor = wlr_compositor_create(server.display, 6, server.renderer);
		if (server.compositor == NULL) {
			fprintf(stderr, "aswlcomp: wlr_compositor_create failed\n");
			return 1;
		}
		(void)wlr_subcompositor_create(server.display);
		(void)wlr_data_device_manager_create(server.display);
			server.primary_selection_manager = wlr_primary_selection_v1_device_manager_create(server.display);
			if (server.primary_selection_manager == NULL) {
				fprintf(stderr, "aswlcomp: wlr_primary_selection_v1_device_manager_create failed\n");
			}

			server.linux_dmabuf = wlr_linux_dmabuf_v1_create_with_renderer(server.display, 5, server.renderer);
			if (server.linux_dmabuf == NULL) {
				fprintf(stderr, "aswlcomp: wlr_linux_dmabuf_v1_create_with_renderer failed\n");
			}

			server.screencopy_manager = wlr_screencopy_manager_v1_create(server.display);
			if (server.screencopy_manager == NULL) {
				fprintf(stderr, "aswlcomp: wlr_screencopy_manager_v1_create failed\n");
			}

			server.export_dmabuf_manager = wlr_export_dmabuf_manager_v1_create(server.display);
			if (server.export_dmabuf_manager == NULL) {
				fprintf(stderr, "aswlcomp: wlr_export_dmabuf_manager_v1_create failed\n");
			}

			server.ext_foreign_toplevel_list = wlr_ext_foreign_toplevel_list_v1_create(server.display, 1);
			if (server.ext_foreign_toplevel_list == NULL) {
				fprintf(stderr, "aswlcomp: wlr_ext_foreign_toplevel_list_v1_create failed\n");
			}

			server.foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(server.display);
			if (server.foreign_toplevel_manager == NULL) {
				fprintf(stderr, "aswlcomp: wlr_foreign_toplevel_manager_v1_create failed\n");
			}

			server.session_lock_manager = wlr_session_lock_manager_v1_create(server.display);
			if (server.session_lock_manager == NULL) {
				fprintf(stderr, "aswlcomp: wlr_session_lock_manager_v1_create failed\n");
			} else {
				server.new_session_lock.notify = handle_new_session_lock;
				wl_signal_add(&server.session_lock_manager->events.new_lock, &server.new_session_lock);
			}

			server.idle_notifier = wlr_idle_notifier_v1_create(server.display);
			if (server.idle_notifier == NULL) {
				fprintf(stderr, "aswlcomp: wlr_idle_notifier_v1_create failed\n");
			}

			server.idle_inhibit_manager = wlr_idle_inhibit_v1_create(server.display);
			if (server.idle_inhibit_manager == NULL) {
				fprintf(stderr, "aswlcomp: wlr_idle_inhibit_v1_create failed\n");
			} else {
				server.new_idle_inhibitor.notify = handle_new_idle_inhibitor;
				wl_signal_add(&server.idle_inhibit_manager->events.new_inhibitor, &server.new_idle_inhibitor);
			}

			aswl_idle_inhibit_refresh(&server);

			server.control_global = wl_global_create(server.display, &afterstep_control_v1_interface, 6, &server, aswl_control_bind);
			if (server.control_global == NULL) {
				fprintf(stderr, "aswlcomp: wl_global_create(afterstep_control_v1) failed\n");
				return 1;
			}

	server.output_layout = wlr_output_layout_create(server.display);
	if (server.output_layout == NULL) {
		fprintf(stderr, "aswlcomp: wlr_output_layout_create failed\n");
		return 1;
	}

	server.xdg_output_manager = wlr_xdg_output_manager_v1_create(server.display, server.output_layout);
	if (server.xdg_output_manager == NULL)
		fprintf(stderr, "aswlcomp: wlr_xdg_output_manager_v1_create failed\n");

	server.output_manager = wlr_output_manager_v1_create(server.display);
	if (server.output_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_output_manager_v1_create failed\n");
	} else {
		server.output_manager_apply.notify = handle_output_manager_apply;
		wl_signal_add(&server.output_manager->events.apply, &server.output_manager_apply);

		server.output_manager_test.notify = handle_output_manager_test;
		wl_signal_add(&server.output_manager->events.test, &server.output_manager_test);
	}

	server.scene = wlr_scene_create();
	if (server.scene == NULL) {
		fprintf(stderr, "aswlcomp: wlr_scene_create failed\n");
		return 1;
	}
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	server.seat = wlr_seat_create(server.display, "seat0");
	if (server.seat == NULL) {
		fprintf(stderr, "aswlcomp: wlr_seat_create failed\n");
		return 1;
	}

	server.text_input_manager = wlr_text_input_manager_v3_create(server.display);
	if (server.text_input_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_text_input_manager_v3_create failed\n");
	} else {
		server.new_text_input.notify = handle_new_text_input;
		wl_signal_add(&server.text_input_manager->events.text_input, &server.new_text_input);
	}

	server.input_method_manager = wlr_input_method_manager_v2_create(server.display);
	if (server.input_method_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_input_method_manager_v2_create failed\n");
	} else {
		server.new_input_method.notify = handle_new_input_method;
		wl_signal_add(&server.input_method_manager->events.input_method, &server.new_input_method);
	}

	server.virtual_keyboard_manager = wlr_virtual_keyboard_manager_v1_create(server.display);
	if (server.virtual_keyboard_manager == NULL) {
		fprintf(stderr, "aswlcomp: wlr_virtual_keyboard_manager_v1_create failed\n");
	} else {
		server.new_virtual_keyboard.notify = handle_new_virtual_keyboard;
		wl_signal_add(&server.virtual_keyboard_manager->events.new_virtual_keyboard, &server.new_virtual_keyboard);
	}

	server.relative_pointer_manager = wlr_relative_pointer_manager_v1_create(server.display);
	if (server.relative_pointer_manager == NULL)
		fprintf(stderr, "aswlcomp: wlr_relative_pointer_manager_v1_create failed\n");

	server.pointer_constraints = wlr_pointer_constraints_v1_create(server.display);
	if (server.pointer_constraints == NULL) {
		fprintf(stderr, "aswlcomp: wlr_pointer_constraints_v1_create failed\n");
	} else {
		server.new_pointer_constraint.notify = handle_new_pointer_constraint;
		wl_signal_add(&server.pointer_constraints->events.new_constraint, &server.new_pointer_constraint);
	}

	server.xdg_activation = wlr_xdg_activation_v1_create(server.display);
	if (server.xdg_activation == NULL) {
		fprintf(stderr, "aswlcomp: wlr_xdg_activation_v1_create failed\n");
	} else {
		server.xdg_activation->token_timeout_msec = 30000;

		server.xdg_activation_new_token.notify = handle_xdg_activation_new_token;
		wl_signal_add(&server.xdg_activation->events.new_token, &server.xdg_activation_new_token);

		server.xdg_activation_request_activate.notify = handle_xdg_activation_request_activate;
		wl_signal_add(&server.xdg_activation->events.request_activate, &server.xdg_activation_request_activate);
	}

	server.xwayland = wlr_xwayland_create(server.display, server.compositor, true);
	if (server.xwayland != NULL) {
		wlr_xwayland_set_seat(server.xwayland, server.seat);
		if (server.xwayland->display_name != NULL) {
			setenv("DISPLAY", server.xwayland->display_name, 1);
			fprintf(stderr, "aswlcomp: Xwayland DISPLAY=%s\n", server.xwayland->display_name);
		}
	} else {
		fprintf(stderr, "aswlcomp: Xwayland disabled/unavailable\n");
	}

	server.cursor = wlr_cursor_create();
	if (server.cursor == NULL) {
		fprintf(stderr, "aswlcomp: wlr_cursor_create failed\n");
		return 1;
	}
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	server.cursor_motion.notify = handle_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);

	server.cursor_motion_absolute.notify = handle_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);

	server.cursor_button.notify = handle_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);

	server.cursor_axis.notify = handle_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);

	server.cursor_frame.notify = handle_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	server.request_cursor.notify = handle_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);

	server.request_set_selection.notify = handle_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);

	server.request_set_primary_selection.notify = handle_request_set_primary_selection;
	wl_signal_add(&server.seat->events.request_set_primary_selection, &server.request_set_primary_selection);

	server.layer_shell = wlr_layer_shell_v1_create(server.display, 4);
	if (server.layer_shell == NULL) {
		fprintf(stderr, "aswlcomp: wlr_layer_shell_v1_create failed\n");
		return 1;
	}

	server.xdg_shell = wlr_xdg_shell_create(server.display, 6);
	if (server.xdg_shell == NULL) {
		fprintf(stderr, "aswlcomp: wlr_xdg_shell_create failed\n");
		return 1;
	}

	server.xdg_deco_mgr = wlr_xdg_decoration_manager_v1_create(server.display);
	if (server.xdg_deco_mgr == NULL) {
		fprintf(stderr, "aswlcomp: wlr_xdg_decoration_manager_v1_create failed (no xdg-decoration)\n");
	} else {
		server.new_xdg_decoration.notify = handle_new_xdg_decoration;
		wl_signal_add(&server.xdg_deco_mgr->events.new_toplevel_decoration, &server.new_xdg_decoration);
	}

	wl_list_init(&server.outputs);
	wl_list_init(&server.views);
	wl_list_init(&server.keyboards);
	wl_list_init(&server.pointer_devices);
	wl_list_init(&server.layer_surfaces);
	wl_list_init(&server.lock_surfaces);
	wl_list_init(&server.bindings);
	wl_list_init(&server.text_inputs);
	wl_list_init(&server.input_methods);
	wl_list_init(&server.input_popups);
	wl_list_init(&server.control_clients);

	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] = wlr_scene_tree_create(&server.scene->tree);
	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] = wlr_scene_tree_create(&server.scene->tree);
	server.xdg_tree = wlr_scene_tree_create(&server.scene->tree);
	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP] = wlr_scene_tree_create(&server.scene->tree);
	server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] = wlr_scene_tree_create(&server.scene->tree);
	server.lock_tree = wlr_scene_tree_create(&server.scene->tree);
	if (server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] != NULL)
		server.ime_popup_tree = wlr_scene_tree_create(server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);
	if (server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] == NULL ||
	    server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] == NULL ||
	    server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP] == NULL ||
	    server.layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] == NULL ||
	    server.xdg_tree == NULL ||
	    server.lock_tree == NULL) {
		fprintf(stderr, "aswlcomp: failed to create scene roots\n");
		return 1;
	}
	if (server.ime_popup_tree == NULL)
		fprintf(stderr, "aswlcomp: failed to create IME popup tree\n");
	wlr_scene_node_set_enabled(&server.lock_tree->node, false);

	server.new_output.notify = handle_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.new_input.notify = handle_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);

	server.new_xdg_toplevel.notify = handle_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);

	if (server.xwayland != NULL) {
		server.new_xwayland_surface.notify = handle_new_xwayland_surface;
		wl_signal_add(&server.xwayland->events.new_surface, &server.new_xwayland_surface);
	}

	server.new_layer_surface.notify = handle_new_layer_surface;
	wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

	const char *socket = NULL;
	if (socket_name != NULL) {
		if (wl_display_add_socket(server.display, socket_name) != 0) {
			fprintf(stderr, "aswlcomp: wl_display_add_socket(%s) failed: %s\n",
			        socket_name,
			        strerror(errno));
			return 1;
		}
		socket = socket_name;
	} else {
		socket = wl_display_add_socket_auto(server.display);
	}
	if (socket == NULL) {
		fprintf(stderr, "aswlcomp: wl_display_add_socket_auto failed\n");
		return 1;
	}

	if (!wlr_backend_start(server.backend)) {
		fprintf(stderr, "aswlcomp: wlr_backend_start failed\n");
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, 1);
	fprintf(stderr, "aswlcomp: running on WAYLAND_DISPLAY=%s\n", socket);

	if (autostart_path != NULL) {
		fprintf(stderr, "aswlcomp: autostart file: %s\n", autostart_path);
		load_config_file(&server, autostart_path, true);
	} else {
		char default_path[4096];
		if (default_autostart_path(default_path, sizeof(default_path)))
			load_config_file(&server, default_path, false);
	}

		for (size_t i = 0; i < spawn_count; i++) {
			fprintf(stderr, "aswlcomp: spawn: %s\n", spawn_cmds[i]);
			spawn_command(spawn_cmds[i]);
		}

		free(spawn_cmds);
		wl_display_run(server.display);

		/* Ensure protocol-side handles are destroyed before wlroots globals go away. */
		struct aswl_view *view;
			struct aswl_view *view_tmp;
			wl_list_for_each_safe(view, view_tmp, &server.views, link) {
				view_toplevel_protocols_destroy(view);
			}

			if (server.idle_lock_timer != NULL) {
				wl_event_source_remove(server.idle_lock_timer);
				server.idle_lock_timer = NULL;
			}
			if (server.flush_timer != NULL) {
				wl_event_source_remove(server.flush_timer);
				server.flush_timer = NULL;
			}

			if (server.session_lock != NULL)
				session_lock_detach(&server);
			lock_surfaces_destroy(&server);
			if (server.session_lock_manager != NULL)
				wl_list_remove(&server.new_session_lock.link);
			if (server.idle_inhibit_manager != NULL)
				wl_list_remove(&server.new_idle_inhibitor.link);
			if (server.text_input_manager != NULL)
				wl_list_remove(&server.new_text_input.link);
			if (server.input_method_manager != NULL)
				wl_list_remove(&server.new_input_method.link);
			if (server.virtual_keyboard_manager != NULL)
				wl_list_remove(&server.new_virtual_keyboard.link);
			if (server.pointer_constraints != NULL)
				wl_list_remove(&server.new_pointer_constraint.link);
			if (server.xdg_activation != NULL) {
				wl_list_remove(&server.xdg_activation_new_token.link);
				wl_list_remove(&server.xdg_activation_request_activate.link);
			}
			idle_inhibitors_destroy(&server);

			/* Detach listeners before wlroots globals are torn down. */
				if (server.cursor != NULL) {
					wl_list_remove(&server.cursor_motion.link);
					wl_list_remove(&server.cursor_motion_absolute.link);
			wl_list_remove(&server.cursor_button.link);
			wl_list_remove(&server.cursor_axis.link);
			wl_list_remove(&server.cursor_frame.link);
		}
		if (server.seat != NULL) {
			wl_list_remove(&server.request_cursor.link);
			wl_list_remove(&server.request_set_selection.link);
			wl_list_remove(&server.request_set_primary_selection.link);
		}
		wl_list_remove(&server.new_layer_surface.link);
		wl_list_remove(&server.new_xdg_toplevel.link);
		if (server.xdg_deco_mgr != NULL) {
			wl_list_remove(&server.new_xdg_decoration.link);
		}
		wl_list_remove(&server.new_input.link);
		wl_list_remove(&server.new_output.link);
		if (server.output_manager != NULL) {
			wl_list_remove(&server.output_manager_apply.link);
			wl_list_remove(&server.output_manager_test.link);
		}

	if (server.xwayland != NULL) {
		wl_list_remove(&server.new_xwayland_surface.link);
		wlr_xwayland_destroy(server.xwayland);
		server.xwayland = NULL;
	}

	/*
	 * During wl_display_destroy(), outputs and layer surfaces may emit destroy
	 * signals. Guard arrange_layers()/arrange_dock_views() against touching
	 * wlroots objects that may already be torn down.
	 */
		server.output_layout = NULL;

			aswl_outputs_persist_destroy(&server);
			aswl_deco_assets_destroy(&server.deco);
			aswl_font_destroy(&server.deco_font_inactive);
			aswl_font_destroy(&server.deco_font);
			aswl_dock_config_destroy(&server.dock);
			aswl_theme_destroy(&server.theme);
			free(server.xkb_rules);
			free(server.xkb_model);
			free(server.xkb_layout);
			free(server.xkb_variant);
			free(server.xkb_options);
			free(server.lock_command);
			free(server.state_path);
			wl_display_destroy(server.display);
		return 0;
	}

#else

int main(void)
{
	fprintf(stderr,
	        "aswlcomp: wlroots support not enabled.\n"
	        "Install wlroots development packages and rebuild (make -C wayland aswlcomp).\n");
	return 1;
}

#endif
