#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlpanel_internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "aswlicon.h"
#if HAVE_AFTERIMAGE
#include "afterimage.h"
#endif

static bool as_panel_edge_is_vertical(enum as_panel_edge edge)
{
	return edge == ASWL_PANEL_EDGE_LEFT || edge == ASWL_PANEL_EDGE_RIGHT;
}

int main(void)
{
	struct as_state state = {
		.width = 360,
		.height = 64,
		.item_height = 0,
		.pager_columns = 2,
		.pager_rows = 1,
		.running = true,
		.hover_index = -1,
		.pressed_index = -1,
		.current_workspace = 1,
		.workspace_count = 9,
		.edge = ASWL_PANEL_EDGE_TOP,
	};

	aswl_theme_init_default(&state.theme);
	(void)aswl_theme_load(&state.theme);
	if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
		char cwd[PATH_MAX] = { 0 };
		const char *cfg_path = getenv("ASWLTHEME_CONFIG");
		if (getcwd(cwd, sizeof(cwd)) == NULL)
			strcpy(cwd, "(getcwd failed)");
		fprintf(stderr,
		        "aswlpanel: theme: ASWLTHEME_CONFIG=%s cwd=%s panel_backpix_type=%d tint=0x%08X panel_bg=0x%08X button_bg=0x%08X\n",
		        cfg_path != NULL ? cfg_path : "(null)",
		        cwd,
		        state.theme.panel_back_pixmap_type,
		        state.theme.panel_back_pixmap_tint,
		        state.theme.panel_bg,
		        state.theme.panel_button_bg);
	}

	aswl_font_init(&state.font);
	const char *font_spec = getenv("ASWLPANEL_FONT");
	if (font_spec == NULL || font_spec[0] == '\0')
		font_spec = getenv("ASWL_FONT");
	if ((font_spec == NULL || font_spec[0] == '\0') && state.theme.panel_font != NULL && state.theme.panel_font[0] != '\0')
		font_spec = state.theme.panel_font;
	if (!aswl_font_load(&state.font, font_spec) && font_spec != NULL && font_spec[0] != '\0')
		fprintf(stderr, "aswlpanel: failed to load font '%s', using builtin 5x7\n", font_spec);

	as_state_load_buttons(&state);

	const char *winlist_mode = getenv("ASWLPANEL_WINDOW_LIST");
	if (winlist_mode != NULL && winlist_mode[0] != '\0') {
		if (strcasecmp(winlist_mode, "topmost") == 0 || strcasecmp(winlist_mode, "top") == 0 ||
		    strcasecmp(winlist_mode, "topmost-only") == 0) {
			state.window_list_topmost_only = true;
			state.window_list_focused_only = false;
		} else if (strcasecmp(winlist_mode, "focused") == 0 || strcasecmp(winlist_mode, "focused-only") == 0 ||
		           strcasecmp(winlist_mode, "focus") == 0) {
			state.window_list_focused_only = true;
			state.window_list_topmost_only = false;
		} else if (winlist_mode[0] == '1' || strcasecmp(winlist_mode, "true") == 0 || strcasecmp(winlist_mode, "yes") == 0) {
			state.window_list_focused_only = true;
			state.window_list_topmost_only = false;
		}
	}

	const char *excl_env = getenv("ASWLPANEL_EXCLUSIVE_ZONE");
	if (excl_env != NULL && excl_env[0] != '\0') {
		char *end = NULL;
		long v = strtol(excl_env, &end, 10);
		if (end != excl_env && end != NULL && *end == '\0' && v >= 0 && v <= 8192) {
			state.exclusive_zone_override_set = true;
			state.exclusive_zone_override = (int)v;
		}
	}

	const char *height_env = getenv("ASWLPANEL_HEIGHT");
	if (height_env != NULL && height_env[0] != '\0') {
		char *end = NULL;
		long h = strtol(height_env, &end, 10);
		if (end != height_env && end != NULL && *end == '\0' && h >= 16 && h <= 512) {
			state.height = (int)h;
			state.height_override_set = true;
		}
	} else if (!state.height_override_set && state.dock_mode && !as_panel_edge_is_vertical(state.edge)) {
		state.height = 48;
	}

	if (as_panel_edge_is_vertical(state.edge) && !state.dock_mode && !state.item_height_override_set)
		state.item_height = 48;

	const char *item_height_env = getenv("ASWLPANEL_ITEM_HEIGHT");
	if (item_height_env != NULL && item_height_env[0] != '\0') {
		char *end = NULL;
		long h = strtol(item_height_env, &end, 10);
		if (end != item_height_env && end != NULL && *end == '\0' && h >= 16 && h <= 512) {
			state.item_height = (int)h;
			state.item_height_override_set = true;
		}
	}

	const char *width_env = getenv("ASWLPANEL_WIDTH");
	bool width_set = state.width_override_set;
	if (width_env != NULL && width_env[0] != '\0') {
		char *end = NULL;
		long w = strtol(width_env, &end, 10);
		if (end != width_env && end != NULL && *end == '\0' && w >= 64 && w <= 8192)
			state.width = (int)w;
		width_set = true;
	}

	const char *pager_cols_env = getenv("ASWLPANEL_PAGER_COLUMNS");
	if (pager_cols_env != NULL && pager_cols_env[0] != '\0') {
		char *end = NULL;
		long v = strtol(pager_cols_env, &end, 10);
		if (end != pager_cols_env && end != NULL && *end == '\0' && v >= 1 && v <= 16)
			state.pager_columns = (int)v;
	}

	const char *pager_rows_env = getenv("ASWLPANEL_PAGER_ROWS");
	if (pager_rows_env != NULL && pager_rows_env[0] != '\0') {
		char *end = NULL;
		long v = strtol(pager_rows_env, &end, 10);
		if (end != pager_rows_env && end != NULL && *end == '\0' && v >= 1 && v <= 16)
			state.pager_rows = (int)v;
	}

	if (!width_set && state.dock_mode && !as_panel_edge_is_vertical(state.edge)) {
		int dock_w = as_state_calc_dock_main_axis_size(&state);
		if (dock_w >= 64 && dock_w <= 8192)
			state.width = dock_w;
	}

	if (!width_set && as_panel_edge_is_vertical(state.edge)) {
		if (state.dock_mode)
			state.width = 64;
		else if (state.pager_mode)
			state.width = 192;
		else
			state.width = 96;
	}

	uint32_t anchor_flags = as_state_anchor_flags(&state);
	if (as_panel_edge_is_vertical(state.edge) && state.dock_mode &&
	    !(((anchor_flags & ASWL_ANCHOR_TOP) != 0 && (anchor_flags & ASWL_ANCHOR_BOTTOM) != 0)) &&
	    !state.height_override_set) {
		int dock_h = as_state_calc_dock_main_axis_size(&state);
		if (dock_h >= 64 && dock_h <= 8192) {
			state.height = dock_h;
			state.height_override_set = true;
		}
	}

	if (!aswlpanel_wl_init(&state)) {
		aswlpanel_cleanup(&state);
		return 1;
	}

	while (state.running && wl_display_dispatch(state.display) != -1) {
		/* Event-driven. */
	}

	aswlpanel_cleanup(&state);
	return 0;
}
