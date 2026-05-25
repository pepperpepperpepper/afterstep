#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlmenu_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

static void pointer_enter(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface,
                          wl_fixed_t surface_x,
                          wl_fixed_t surface_y)
{
	(void)pointer;
	(void)serial;
	(void)surface;

	struct as_state *state = data;
	state->pointer_in_surface = true;
	state->pointer_x = (int)wl_fixed_to_double(surface_x);
	state->pointer_y = (int)wl_fixed_to_double(surface_y);
	as_state_update_hover(state);
}

static void pointer_leave(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface)
{
	(void)pointer;
	(void)serial;
	(void)surface;

	struct as_state *state = data;
	state->pointer_in_surface = false;
	state->hover_index = -1;
	state->hover_close = false;
	state->hover_iconize = false;
	state->hover_pin = false;
	state->pressed_index = -1;
	state->pressed_close = false;
	state->pressed_iconize = false;
	state->pressed_pin = false;
	schedule_redraw(state);
}

static void pointer_motion(void *data,
                           struct wl_pointer *pointer,
                           uint32_t time,
                           wl_fixed_t surface_x,
                           wl_fixed_t surface_y)
{
	(void)pointer;
	(void)time;
	struct as_state *state = data;

	state->pointer_x = (int)wl_fixed_to_double(surface_x);
	state->pointer_y = (int)wl_fixed_to_double(surface_y);
	as_state_update_hover(state);
}

static void pointer_button(void *data,
                           struct wl_pointer *pointer,
                           uint32_t serial,
                           uint32_t time,
                           uint32_t button,
                           uint32_t state_w)
{
	(void)pointer;
	(void)serial;
	(void)time;
	struct as_state *state = data;

	if (button != BTN_LEFT)
		return;

	/* Mouse interaction should override keyboard-selection hilites. */
	state->keyboard_nav_active = false;
	as_state_update_hover(state);

	if (state_w == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (state->hover_close) {
			state->pressed_close = true;
			state->pressed_iconize = false;
			state->pressed_pin = false;
			state->pressed_index = -1;
			schedule_redraw(state);
			return;
		}
		if (state->hover_iconize) {
			state->pressed_iconize = true;
			state->pressed_close = false;
			state->pressed_pin = false;
			state->pressed_index = -1;
			schedule_redraw(state);
			return;
		}
		if (state->hover_pin) {
			state->pressed_pin = true;
			state->pressed_close = false;
			state->pressed_iconize = false;
			state->pressed_index = -1;
			schedule_redraw(state);
			return;
		}
		state->pressed_index = state->hover_index;
		state->pressed_close = false;
		state->pressed_iconize = false;
		state->pressed_pin = false;
		schedule_redraw(state);
		return;
	}

	if (state_w != WL_POINTER_BUTTON_STATE_RELEASED)
		return;

	if (state->pressed_close) {
		bool clicked = state->hover_close;
		state->pressed_close = false;
		schedule_redraw(state);
		if (clicked)
			state->running = false;
		return;
	}

	if (state->pressed_iconize) {
		bool clicked = state->hover_iconize;
		state->pressed_iconize = false;
		schedule_redraw(state);
		if (clicked && state->xdg_toplevel != NULL)
			xdg_toplevel_set_minimized(state->xdg_toplevel);
		return;
	}

	if (state->pressed_pin) {
		bool clicked = state->hover_pin;
		state->pressed_pin = false;
		if (clicked)
			state->pinned_open = !state->pinned_open;
		schedule_redraw(state);
		return;
	}

	int clicked = state->pressed_index;
	state->pressed_index = -1;
	schedule_redraw(state);

	if (clicked < 0 || clicked != state->hover_index)
		return;
	if ((size_t)clicked >= state->filtered_count)
		return;

	state->selected_index = clicked;
	as_state_ensure_selection_visible(state);

	size_t entry_idx = state->filtered[clicked];
	if (entry_idx >= state->entry_count)
		return;

	as_state_activate_entry(state, entry_idx);
}

static void pointer_axis(void *data,
                         struct wl_pointer *pointer,
                         uint32_t time,
                         uint32_t axis,
                         wl_fixed_t value)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
	(void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
	(void)data;
	(void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source)
{
	(void)data;
	(void)pointer;
	(void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete)
{
	(void)data;
	(void)pointer;
	(void)axis;
	(void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

#ifdef HAVE_XKBCOMMON
static void keyboard_keymap(void *data,
                            struct wl_keyboard *keyboard,
                            uint32_t format,
                            int32_t fd,
                            uint32_t size)
{
	(void)keyboard;
	struct as_state *state = data;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}

	char *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return;
	}

	if (state->xkb_context == NULL)
		state->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	struct xkb_keymap *keymap = NULL;
	if (state->xkb_context != NULL) {
		keymap = xkb_keymap_new_from_string(state->xkb_context,
		                                    map,
		                                    XKB_KEYMAP_FORMAT_TEXT_V1,
		                                    XKB_KEYMAP_COMPILE_NO_FLAGS);
	}

	munmap(map, size);
	close(fd);

	if (keymap == NULL)
		return;

	struct xkb_state *xkb_state = xkb_state_new(keymap);
	if (xkb_state == NULL) {
		xkb_keymap_unref(keymap);
		return;
	}

	if (state->xkb_state != NULL)
		xkb_state_unref(state->xkb_state);
	if (state->xkb_keymap != NULL)
		xkb_keymap_unref(state->xkb_keymap);

	state->xkb_keymap = keymap;
	state->xkb_state = xkb_state;
}
#else
static void keyboard_keymap(void *data,
                            struct wl_keyboard *keyboard,
                            uint32_t format,
                            int32_t fd,
                            uint32_t size)
{
	(void)data;
	(void)keyboard;
	(void)format;
	(void)size;
	close(fd);
}
#endif

static void keyboard_enter(void *data,
                           struct wl_keyboard *keyboard,
                           uint32_t serial,
                           struct wl_surface *surface,
                           struct wl_array *keys)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
	(void)keys;
}

static void keyboard_leave(void *data,
                           struct wl_keyboard *keyboard,
                           uint32_t serial,
                           struct wl_surface *surface)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
}

static void keyboard_key(void *data,
                         struct wl_keyboard *keyboard,
                         uint32_t serial,
                         uint32_t time,
                         uint32_t key,
                         uint32_t state_w)
{
	(void)keyboard;
	(void)serial;
	(void)time;
	struct as_state *state = data;

	if (state_w != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	if (key == KEY_ESC) {
		if (state->filter_len > 0)
			(void)as_state_filter_set(state, "");
		else if (state->menu_stack_len > 0)
			(void)as_state_go_back(state);
		else
			state->running = false;
		/* Returning to an idle menu state should drop keyboard-selection hilites. */
		state->keyboard_nav_active = false;
		return;
	}

	if (key == KEY_BACKSPACE) {
		as_state_filter_backspace(state);
		return;
	}

	if (key == KEY_ENTER) {
		if (state->selected_index < 0 || (size_t)state->selected_index >= state->filtered_count)
			return;
		size_t entry_idx = state->filtered[state->selected_index];
		if (entry_idx >= state->entry_count)
			return;
		as_state_activate_entry(state, entry_idx);
		return;
	}

	if (key == KEY_UP) {
		state->keyboard_nav_active = true;
		as_state_select_delta(state, -1);
		return;
	}
	if (key == KEY_DOWN) {
		state->keyboard_nav_active = true;
		as_state_select_delta(state, +1);
		return;
	}
	if (key == KEY_PAGEUP) {
		state->keyboard_nav_active = true;
		as_state_select_delta(state, -10);
		return;
	}
	if (key == KEY_PAGEDOWN) {
		state->keyboard_nav_active = true;
		as_state_select_delta(state, +10);
		return;
	}

#ifdef HAVE_XKBCOMMON
	if (state->xkb_state != NULL) {
		uint32_t keycode = key + 8;
		char utf8[64] = { 0 };
		int n = xkb_state_key_get_utf8(state->xkb_state, keycode, utf8, (int)sizeof(utf8));
		if (n > 0) {
			utf8[(size_t)n] = '\0';
			bool any_printable = false;
			for (int i = 0; i < n; i++) {
				unsigned char ch = (unsigned char)utf8[i];
				if (ch >= 0x20 && ch != 0x7F)
					any_printable = true;
			}
			if (any_printable)
				(void)as_state_filter_append_utf8(state, utf8);
		}
	}
#else
	(void)state;
#endif
}

static void keyboard_modifiers(void *data,
                               struct wl_keyboard *keyboard,
                               uint32_t serial,
                               uint32_t mods_depressed,
                               uint32_t mods_latched,
                               uint32_t mods_locked,
                               uint32_t group)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)mods_depressed;
	(void)mods_latched;
	(void)mods_locked;
	(void)group;

#ifdef HAVE_XKBCOMMON
	struct as_state *state = data;
	if (state == NULL || state->xkb_state == NULL)
		return;

	xkb_state_update_mask(state->xkb_state,
	                      mods_depressed,
	                      mods_latched,
	                      mods_locked,
	                      0,
	                      0,
	                      group);
#endif
}

static void keyboard_repeat_info(void *data,
                                 struct wl_keyboard *keyboard,
                                 int32_t rate,
                                 int32_t delay)
{
	(void)data;
	(void)keyboard;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

void aswlmenu_input_attach_pointer(struct wl_pointer *pointer, struct as_state *state)
{
	if (pointer == NULL || state == NULL)
		return;
	(void)wl_pointer_add_listener(pointer, &pointer_listener, state);
}

void aswlmenu_input_attach_keyboard(struct wl_keyboard *keyboard, struct as_state *state)
{
	if (keyboard == NULL || state == NULL)
		return;
	(void)wl_keyboard_add_listener(keyboard, &keyboard_listener, state);
}
