#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlmenu_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--windows]\n", prog);
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  --windows, --window-list   Show a simple window list (focus on selection)\n");
	fprintf(stderr, "  --help, -h                 Show this help\n");
}

int main(int argc, char **argv)
{
	struct as_state state = {
		.width = 640,
		.height = 520,
		.running = true,
		.hover_index = -1,
		.pressed_index = -1,
		.selected_index = 0,
		.scroll = 0,
	};

	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (strcmp(arg, "--windows") == 0 || strcmp(arg, "--window-list") == 0) {
			state.window_list_mode = true;
			continue;
		}
		if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
			usage(argv[0]);
			return 0;
		}

		fprintf(stderr, "aswlmenu: unknown argument: %s\n", arg);
		usage(argv[0]);
		return 2;
	}

	aswl_theme_init_default(&state.theme);
	(void)aswl_theme_load(&state.theme);

	const char *title_env = getenv("ASWLMENU_TITLE");
	if (title_env != NULL && title_env[0] != '\0') {
		state.title = strdup(title_env);
		state.title_fixed = true;
	} else if (state.window_list_mode) {
		state.title = strdup("Windows");
	} else {
		state.title = strdup("AfterStep");
	}

	const char *show_help = getenv("ASWLMENU_SHOW_HELP");
	if (show_help != NULL && show_help[0] != '\0' && strcmp(show_help, "0") != 0)
		state.show_help = true;

	aswl_font_init(&state.font);
	aswl_font_init(&state.header_font);
	aswl_font_init(&state.hilite_font);
	const char *menu_font_env = getenv("ASWLMENU_FONT");
	const char *global_font_env = getenv("ASWL_FONT");
	bool env_override = (menu_font_env != NULL && menu_font_env[0] != '\0') ||
	                    (global_font_env != NULL && global_font_env[0] != '\0');

	const char *font_spec = menu_font_env;
	if (font_spec == NULL || font_spec[0] == '\0')
		font_spec = global_font_env;
	if ((font_spec == NULL || font_spec[0] == '\0') && state.theme.menu_font != NULL && state.theme.menu_font[0] != '\0')
		font_spec = state.theme.menu_font;
	if (!aswl_font_load(&state.font, font_spec) && font_spec != NULL && font_spec[0] != '\0')
		fprintf(stderr, "aswlmenu: failed to load font '%s', using builtin 5x7\n", font_spec);

	const char *header_font_spec = font_spec;
	const char *hilite_font_spec = font_spec;
	if (!env_override) {
		if (state.window_list_mode) {
			/* Match classic AfterStep: window list popups use the focused menu-title styling. */
			if (state.theme.menu_hititle_font != NULL && state.theme.menu_hititle_font[0] != '\0')
				header_font_spec = state.theme.menu_hititle_font;
			else if (state.theme.menu_title_font != NULL && state.theme.menu_title_font[0] != '\0')
				header_font_spec = state.theme.menu_title_font;
			else if (state.theme.frame_font != NULL && state.theme.frame_font[0] != '\0')
				header_font_spec = state.theme.frame_font;
		} else if (state.theme.menu_title_font != NULL && state.theme.menu_title_font[0] != '\0') {
			header_font_spec = state.theme.menu_title_font;
		}
		if (state.theme.menu_hilite_font != NULL && state.theme.menu_hilite_font[0] != '\0')
			hilite_font_spec = state.theme.menu_hilite_font;
	}

	if (!aswl_font_load(&state.header_font, header_font_spec) && header_font_spec != NULL && header_font_spec[0] != '\0')
		fprintf(stderr, "aswlmenu: failed to load header font '%s', using builtin 5x7\n", header_font_spec);
	if (!aswl_font_load(&state.hilite_font, hilite_font_spec) && hilite_font_spec != NULL && hilite_font_spec[0] != '\0')
		fprintf(stderr, "aswlmenu: failed to load hilite font '%s', using builtin 5x7\n", hilite_font_spec);

	if (!state.window_list_mode) {
		as_state_load_menu(&state);
		as_state_finalize_menu(&state);
		as_state_autosize(&state);
	}

	if (!aswlmenu_wl_connect(&state)) {
		aswlmenu_cleanup(&state);
		return 1;
	}

	if (state.window_list_mode) {
		aswlmenu_update_window_list_title(&state);
		as_state_finalize_menu(&state);
		as_state_autosize(&state);
	}

	if (!aswlmenu_wl_setup_surface(&state)) {
		aswlmenu_cleanup(&state);
		return 1;
	}

	while (state.running && wl_display_dispatch(state.display) != -1) {
		if (state.needs_redraw && state.frame_cb == NULL)
			draw_and_commit(&state);
	}

	aswlmenu_cleanup(&state);
	return 0;
}
