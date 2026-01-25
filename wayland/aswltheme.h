#ifndef ASWL_THEME_H
#define ASWL_THEME_H

#include <stdbool.h>
#include <stdint.h>

struct aswl_theme {
	uint32_t panel_bg;
	uint32_t panel_border;
	uint32_t panel_button_bg;
	uint32_t panel_button_fg;
	uint32_t panel_ws_inactive_bg;
	uint32_t panel_ws_inactive_fg;
	uint32_t panel_ws_active_bg;
	uint32_t panel_ws_active_fg;

	uint32_t menu_bg;
	uint32_t menu_border;
	uint32_t menu_header_bg;
	uint32_t menu_header_fg;
	uint32_t menu_item_bg;
	uint32_t menu_item_fg;
	uint32_t menu_item_sel_bg;
	uint32_t menu_item_sel_fg;
	uint32_t menu_footer_bg;
	uint32_t menu_footer_fg;
};

void aswl_theme_init_default(struct aswl_theme *theme);

/* Returns true if any AfterStep theme data was applied. */
bool aswl_theme_load(struct aswl_theme *theme);

uint32_t aswl_color_blend(uint32_t a, uint32_t b, uint8_t t);
uint32_t aswl_color_lighten(uint32_t c, uint8_t t);
uint32_t aswl_color_darken(uint32_t c, uint8_t t);
bool aswl_color_is_light(uint32_t c);
uint32_t aswl_color_nudge(uint32_t c, uint8_t t);

#endif

