#ifndef ASWL_THEME_H
#define ASWL_THEME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct aswl_gradient {
	int type; /* AfterStep BackGradient type */
	uint32_t *colors; /* ARGB colors (not premultiplied) */
	double *offsets; /* 0..1 */
	size_t count;
};

struct aswl_theme {
	uint32_t panel_bg;
	uint32_t panel_border;
	uint32_t panel_button_bg;
	uint32_t panel_button_fg;
	uint32_t panel_ws_inactive_bg;
	uint32_t panel_ws_inactive_fg;
	uint32_t panel_ws_active_bg;
	uint32_t panel_ws_active_fg;

	struct aswl_gradient panel_bg_gradient;
	struct aswl_gradient panel_button_gradient;
	struct aswl_gradient panel_ws_inactive_gradient;
	struct aswl_gradient panel_ws_active_gradient;

	uint32_t desk_bg;
	struct aswl_gradient desk_gradient;

	char *panel_font;

	uint32_t frame_active_bg;
	uint32_t frame_active_fg;
	uint32_t frame_inactive_bg;
	uint32_t frame_inactive_fg;
	uint32_t frame_border;
	char *frame_font;
	char *frame_inactive_font;

	struct aswl_gradient frame_active_gradient;
	struct aswl_gradient frame_inactive_gradient;

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

	struct aswl_gradient menu_header_gradient;
	struct aswl_gradient menu_item_gradient;
	struct aswl_gradient menu_item_sel_gradient;

	char *menu_font;
	char *menu_title_font;
	char *menu_hilite_font;
};

void aswl_theme_init_default(struct aswl_theme *theme);
void aswl_theme_destroy(struct aswl_theme *theme);

/* Returns true if any AfterStep theme data was applied. */
bool aswl_theme_load(struct aswl_theme *theme);

uint32_t aswl_color_blend(uint32_t a, uint32_t b, uint8_t t);
uint32_t aswl_color_lighten(uint32_t c, uint8_t t);
uint32_t aswl_color_darken(uint32_t c, uint8_t t);
bool aswl_color_is_light(uint32_t c);
uint32_t aswl_color_nudge(uint32_t c, uint8_t t);

void aswl_gradient_destroy(struct aswl_gradient *grad);
bool aswl_gradient_is_valid(const struct aswl_gradient *grad);

#endif
