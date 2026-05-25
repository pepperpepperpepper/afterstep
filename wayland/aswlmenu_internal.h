#ifndef ASWLMENU_INTERNAL_H
#define ASWLMENU_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef HAVE_XKBCOMMON
#include <xkbcommon/xkbcommon.h>
#endif

#include <wayland-client.h>

#include "afterstep-control-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "aswltheme.h"
#include "aswlfont.h"

/* Avoid pulling in linux headers just for BTN_LEFT/KEY_* values. */
#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif

#ifndef KEY_ESC
#define KEY_ESC 1
#endif
#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE 14
#endif
#ifndef KEY_ENTER
#define KEY_ENTER 28
#endif
#ifndef KEY_UP
#define KEY_UP 103
#endif
#ifndef KEY_PAGEUP
#define KEY_PAGEUP 104
#endif
#ifndef KEY_DOWN
#define KEY_DOWN 108
#endif
#ifndef KEY_PAGEDOWN
#define KEY_PAGEDOWN 109
#endif

enum {
	ASWL_WINDOW_FLAG_MAPPED = 1u << 0,
	ASWL_WINDOW_FLAG_FOCUSED = 1u << 1,
	ASWL_WINDOW_FLAG_XWAYLAND = 1u << 2,
};

struct as_menu_entry {
	char *label;
	char *icon_spec;
	char *command;
	bool pinned;
	bool icon_tried;
	uint32_t *icon_argb;
	int icon_w;
	int icon_h;
};

struct as_menu_stack_entry {
	char *section; /* NULL for root menu (outside @menu blocks) */
	char *title;   /* default title for this menu level (may be overridden by @title) */
	char *filter;
	int selected_index;
	int scroll;
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
	struct wl_keyboard *keyboard;

	struct xdg_wm_base *xdg_wm_base;
	struct afterstep_control_v1 *control;
	uint32_t control_version;

#ifdef HAVE_XKBCOMMON
	struct xkb_context *xkb_context;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;
#endif

	struct wl_surface *surface;
	struct wl_callback *frame_cb;

	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

	struct as_buffer *buffers[2];

	int width;
	int height;
	bool configured;
	bool running;

	bool needs_redraw;
	int pointer_x;
	int pointer_y;
	bool pointer_in_surface;
	bool keyboard_nav_active;

	int hover_index;   /* index in filtered list */
	int pressed_index; /* index in filtered list */
	int selected_index; /* index in filtered list */
	int scroll;        /* first visible filtered index */

	/* Header/titlebar close button (AfterStep-ish). */
	bool hover_close;
	bool pressed_close;
	int close_x;
	int close_y;
	int close_w;
	int close_h;
	bool close_icon_tried;
	uint32_t *close_icon_argb;
	int close_icon_w;
	int close_icon_h;
	bool close_icon_pressed_tried;
	uint32_t *close_icon_pressed_argb;
	int close_icon_pressed_w;
	int close_icon_pressed_h;

	/* Header/titlebar iconize (minimize) button (WinList-ish). */
	bool hover_iconize;
	bool pressed_iconize;
	int iconize_x;
	int iconize_y;
	int iconize_w;
	int iconize_h;
	bool iconize_icon_tried;
	uint32_t *iconize_icon_argb;
	int iconize_icon_w;
	int iconize_icon_h;
	bool iconize_icon_pressed_tried;
	uint32_t *iconize_icon_pressed_argb;
	int iconize_icon_pressed_w;
	int iconize_icon_pressed_h;

	/* Header/titlebar pin button (WinList-ish). */
	bool pinned_open;
	bool hover_pin;
	bool pressed_pin;
	int pin_x;
	int pin_y;
	int pin_w;
	int pin_h;
	bool pin_icon_tried;
	uint32_t *pin_icon_argb;
	int pin_icon_w;
	int pin_icon_h;
	bool pin_icon_pressed_tried;
	uint32_t *pin_icon_pressed_argb;
	int pin_icon_pressed_w;
	int pin_icon_pressed_h;

	char *filter;
	size_t filter_len;
	size_t filter_cap;

	struct as_menu_entry *entries;
	size_t entry_count;
	size_t entry_cap;
	size_t pinned_count;

	size_t *filtered;
	size_t filtered_count;
	size_t filtered_cap;

	bool include_desktop_entries;
	char *menu_config_path;
	char *menu_section;
	struct as_menu_stack_entry *menu_stack;
	size_t menu_stack_len;
	size_t menu_stack_cap;
	char *title;
	bool title_fixed;
	bool show_help;
	bool window_list_mode;
	bool window_list_in_progress;
	uint32_t current_workspace;

	struct aswl_theme theme;
	struct aswl_font font;
	struct aswl_font header_font;
	struct aswl_font hilite_font;
};

struct as_menu_layout {
	int pad;
	int header_h;
	int row_h;
	int text_scale;
	int help_scale;
	int icon_size;
	int icon_col_w;
};

static inline void rstrip(char *s)
{
	if (s == NULL)
		return;
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r'))
		s[--n] = '\0';
}

static inline char *lstrip(char *s)
{
	if (s == NULL)
		return NULL;
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

void schedule_redraw(struct as_state *state);
void draw_and_commit(struct as_state *state);

bool aswlmenu_wl_connect(struct as_state *state);
bool aswlmenu_wl_setup_surface(struct as_state *state);
void aswlmenu_cleanup(struct as_state *state);
void aswlmenu_update_window_list_title(struct as_state *state);

void aswlmenu_input_attach_pointer(struct wl_pointer *pointer, struct as_state *state);
void aswlmenu_input_attach_keyboard(struct wl_keyboard *keyboard, struct as_state *state);

bool as_state_ensure_buffers(struct as_state *state);
struct as_buffer *as_state_acquire_buffer(struct as_state *state);

bool as_state_go_back(struct as_state *state);
void as_state_activate_entry(struct as_state *state, size_t entry_idx);

bool menu_command_is_submenu(const char *command);
char *menu_command_submenu_target(const char *command, const char *fallback_label);
bool menu_config_section_exists(const char *path, const char *section);

void as_state_filter_clear_silent(struct as_state *state);
void as_state_menu_stack_clear(struct as_state *state);
bool as_state_menu_stack_push(struct as_state *state);

void as_state_free_entries(struct as_state *state);
bool as_state_append_entry(struct as_state *state, const char *label, const char *command, const char *icon_spec, bool pinned);

void as_state_free_filtered(struct as_state *state);
void as_state_rebuild_filtered(struct as_state *state);

bool as_state_filter_set(struct as_state *state, const char *text);
bool as_state_filter_append_utf8(struct as_state *state, const char *utf8);
void as_state_filter_backspace(struct as_state *state);

	bool as_state_get_layout(struct as_state *state, struct as_menu_layout *layout);
	void as_state_update_close_button_metrics(struct as_state *state, const struct as_menu_layout *layout);
	void as_state_update_iconize_button_metrics(struct as_state *state, const struct as_menu_layout *layout);
	void as_state_update_pin_button_metrics(struct as_state *state, const struct as_menu_layout *layout);

void as_state_autosize(struct as_state *state);
size_t as_state_visible_rows(struct as_state *state, const struct as_menu_layout *layout);
void as_state_ensure_selection_visible(struct as_state *state);
void as_state_select_delta(struct as_state *state, int delta);
int as_state_hit_test_layout(struct as_state *state, const struct as_menu_layout *layout, int x, int y);
void as_state_update_hover(struct as_state *state);

void as_state_add_desktop_entries(struct as_state *state);
void as_state_load_menu(struct as_state *state);
void as_state_finalize_menu(struct as_state *state);
bool as_state_reload_menu_from_config(struct as_state *state);

#endif /* ASWLMENU_INTERNAL_H */
