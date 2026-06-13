#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswltheme.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void aswl_theme_init_default(struct aswl_theme *theme)
{
	if (theme == NULL)
		return;

	aswl_theme_destroy(theme);

		*theme = (struct aswl_theme){
			.panel_bg = 0xFF202020u,
			.panel_border = 0xFF101010u,
			.panel_button_bg = 0xFF3A3A3Au,
		.panel_button_fg = 0xFFE0E0E0u,
		.panel_ws_inactive_bg = 0xFF3A3A3Au,
		.panel_ws_inactive_fg = 0xFFE0E0E0u,
			.panel_ws_active_bg = 0xFF2E4A7Au,
			.panel_ws_active_fg = 0xFFE0E0E0u,

			.desk_bg = 0x77222222u,
			.pager_border = 0xFF000000u,
			.pager_grid = 0xFF2D3332u,
			.pager_selection = 0xFFCCAD8Du,

			.panel_font = NULL,

		.frame_active_bg = 0xFF2E4A7Au,
		.frame_active_fg = 0xFFE0E0E0u,
		.frame_inactive_bg = 0xFF3A3A3Au,
		.frame_inactive_fg = 0xFFE0E0E0u,
		.frame_border = 0xFF101010u,
		.frame_font = NULL,
		.frame_inactive_font = NULL,
		.frame_active_text_style = 0,
		.frame_inactive_text_style = 0,

		.menu_bg = 0xFF202020u,
		.menu_border = 0xFF101010u,
		.menu_header_bg = 0xFF2D2D2Du,
		.menu_header_fg = 0xFFE8E8E8u,
		.menu_hititle_bg = 0xFF2D2D2Du,
		.menu_hititle_fg = 0xFFE8E8E8u,
		.menu_item_bg = 0xFF262626u,
		.menu_item_fg = 0xFFE0E0E0u,
		.menu_item_sel_bg = 0xFF3A507Au,
		.menu_item_sel_fg = 0xFFE0E0E0u,
		.menu_footer_bg = 0xFF202020u,
		.menu_footer_fg = 0xFFB0B0B0u,

		.menu_font = NULL,
		.menu_title_font = NULL,
		.menu_hititle_font = NULL,
		.menu_hilite_font = NULL,
		.menu_item_text_style = 0,
		.menu_title_text_style = 0,
		.menu_hititle_text_style = 0,
		.menu_hilite_text_style = 0,
	};
}

void aswl_theme_destroy(struct aswl_theme *theme)
{
	if (theme == NULL)
		return;
	aswl_gradient_destroy(&theme->panel_bg_gradient);
	aswl_gradient_destroy(&theme->panel_button_gradient);
	aswl_gradient_destroy(&theme->panel_ws_inactive_gradient);
	aswl_gradient_destroy(&theme->panel_ws_active_gradient);
	aswl_gradient_destroy(&theme->desk_gradient);
	aswl_gradient_destroy(&theme->frame_active_gradient);
	aswl_gradient_destroy(&theme->frame_inactive_gradient);
	aswl_gradient_destroy(&theme->menu_header_gradient);
	aswl_gradient_destroy(&theme->menu_hititle_gradient);
	aswl_gradient_destroy(&theme->menu_item_gradient);
	aswl_gradient_destroy(&theme->menu_item_sel_gradient);
	free(theme->panel_font);
	free(theme->menu_font);
	free(theme->menu_title_font);
	free(theme->menu_hititle_font);
	free(theme->menu_hilite_font);
	free(theme->frame_font);
	free(theme->frame_inactive_font);
	free(theme->panel_back_pixmap_path);
	theme->panel_font = NULL;
	theme->menu_font = NULL;
	theme->menu_title_font = NULL;
	theme->menu_hititle_font = NULL;
	theme->menu_hilite_font = NULL;
	theme->frame_font = NULL;
	theme->frame_inactive_font = NULL;
	theme->panel_back_pixmap_path = NULL;
}

void aswl_gradient_destroy(struct aswl_gradient *grad)
{
	if (grad == NULL)
		return;
	free(grad->colors);
	free(grad->offsets);
	*grad = (struct aswl_gradient){ 0 };
}

bool aswl_gradient_is_valid(const struct aswl_gradient *grad)
{
	return grad != NULL && grad->type != 0 && grad->colors != NULL && grad->offsets != NULL && grad->count >= 2;
}

uint32_t aswl_color_blend(uint32_t a, uint32_t b, uint8_t t)
{
	uint32_t ar = (a >> 16) & 0xFFu;
	uint32_t ag = (a >> 8) & 0xFFu;
	uint32_t ab = a & 0xFFu;
	uint32_t aa = (a >> 24) & 0xFFu;
	uint32_t br = (b >> 16) & 0xFFu;
	uint32_t bg = (b >> 8) & 0xFFu;
	uint32_t bb = b & 0xFFu;
	uint32_t ba = (b >> 24) & 0xFFu;

	uint32_t r = (ar * (255u - t) + br * t) / 255u;
	uint32_t g = (ag * (255u - t) + bg * t) / 255u;
	uint32_t bl = (ab * (255u - t) + bb * t) / 255u;
	uint32_t alpha = (aa * (255u - t) + ba * t) / 255u;
	return (alpha << 24) | (r << 16) | (g << 8) | bl;
}

uint32_t aswl_color_lighten(uint32_t c, uint8_t t)
{
	return aswl_color_blend(c, (c & 0xFF000000u) | 0x00FFFFFFu, t);
}

uint32_t aswl_color_darken(uint32_t c, uint8_t t)
{
	return aswl_color_blend(c, c & 0xFF000000u, t);
}

bool aswl_color_is_light(uint32_t c)
{
	/* Similar weighting to AfterStep's black/white criteria, but in 8-bit space. */
	uint32_t r = (c >> 16) & 0xFFu;
	uint32_t g = (c >> 8) & 0xFFu;
	uint32_t b = c & 0xFFu;
	uint32_t y = r * 222u + g * 707u + b * 71u;
	return y > 160000u;
}

uint32_t aswl_color_nudge(uint32_t c, uint8_t t)
{
	if (aswl_color_is_light(c))
		return aswl_color_darken(c, t);
	return aswl_color_lighten(c, t);
}

static uint8_t aswl_make_component_hilite(uint8_t cmp)
{
	if (cmp < 51)
		cmp = 51;
	int v = ((int)cmp * 12) / 10;
	if (v > 255)
		v = 255;
	return (uint8_t)v;
}

uint32_t aswl_color_hilite(uint32_t background)
{
	uint8_t a = (background >> 24) & 0xFFu;
	uint8_t r = aswl_make_component_hilite((background >> 16) & 0xFFu);
	uint8_t g = aswl_make_component_hilite((background >> 8) & 0xFFu);
	uint8_t b = aswl_make_component_hilite(background & 0xFFu);
	return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t aswl_color_shadow(uint32_t background)
{
	uint32_t a = (background >> 24) & 0xFFu;
	uint32_t r = ((background >> 16) & 0xFFu) * 3u / 4u;
	uint32_t g = ((background >> 8) & 0xFFu) * 3u / 4u;
	uint32_t b = (background & 0xFFu) * 3u / 4u;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t aswl_color_average(uint32_t foreground, uint32_t background)
{
	uint32_t a = (((foreground >> 24) & 0xFFu) + ((background >> 24) & 0xFFu)) / 2u;
	uint32_t r = (((foreground >> 16) & 0xFFu) + ((background >> 16) & 0xFFu)) / 2u;
	uint32_t g = (((foreground >> 8) & 0xFFu) + ((background >> 8) & 0xFFu)) / 2u;
	uint32_t b = ((foreground & 0xFFu) + (background & 0xFFu)) / 2u;
	return (a << 24) | (r << 16) | (g << 8) | b;
}
