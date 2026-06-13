#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswltheme_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct aswl_theme_cfg {
	char *look_path;
	char *colorscheme_path;

	char *panel_style;
	char *ws_active_style;
	char *ws_inactive_style;
	char *desk_style;

	char *win_active_style;
	char *win_inactive_style;
	char *win_sticky_style;

	char *menu_item_style;
	char *menu_hilite_style;
	char *menu_title_style;
	char *menu_hititle_style;
};

static void aswl_theme_cfg_free(struct aswl_theme_cfg *cfg)
{
	if (cfg == NULL)
		return;
	free(cfg->look_path);
	free(cfg->colorscheme_path);
	free(cfg->panel_style);
	free(cfg->ws_active_style);
	free(cfg->ws_inactive_style);
	free(cfg->desk_style);
	free(cfg->win_active_style);
	free(cfg->win_inactive_style);
	free(cfg->win_sticky_style);
	free(cfg->menu_item_style);
	free(cfg->menu_hilite_style);
	free(cfg->menu_title_style);
	free(cfg->menu_hititle_style);
	*cfg = (struct aswl_theme_cfg){ 0 };
}

static bool aswl_theme_cfg_load_file(struct aswl_theme_cfg *cfg, const char *path)
{
	if (cfg == NULL || !aswl_theme_is_file_readable(path))
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len;

	while ((line_len = getline(&line, &line_cap, fp)) != -1) {
		(void)line_len;
		char *s = aswl_trim(line);
		if (s == NULL || s[0] == '\0' || s[0] == '#')
			continue;

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq++ = '\0';

		char *key = aswl_trim(s);
		char *val = aswl_trim(eq);
		if (key == NULL || val == NULL || key[0] == '\0' || val[0] == '\0')
			continue;

		char *v = aswl_dup_unquoted(val);
		if (v == NULL)
			continue;

		if (strcmp(key, "LookPath") == 0) {
			free(cfg->look_path);
			cfg->look_path = v;
		} else if (strcmp(key, "ColorSchemePath") == 0) {
			free(cfg->colorscheme_path);
			cfg->colorscheme_path = v;
		} else if (strcmp(key, "PanelStyle") == 0) {
			free(cfg->panel_style);
			cfg->panel_style = v;
		} else if (strcmp(key, "WorkspaceActiveStyle") == 0) {
			free(cfg->ws_active_style);
			cfg->ws_active_style = v;
		} else if (strcmp(key, "WorkspaceInactiveStyle") == 0) {
			free(cfg->ws_inactive_style);
			cfg->ws_inactive_style = v;
		} else if (strcmp(key, "DeskStyle") == 0) {
			free(cfg->desk_style);
			cfg->desk_style = v;
		} else if (strcmp(key, "WindowActiveStyle") == 0) {
			free(cfg->win_active_style);
			cfg->win_active_style = v;
		} else if (strcmp(key, "WindowInactiveStyle") == 0) {
			free(cfg->win_inactive_style);
			cfg->win_inactive_style = v;
		} else if (strcmp(key, "WindowStickyStyle") == 0) {
			free(cfg->win_sticky_style);
			cfg->win_sticky_style = v;
		} else if (strcmp(key, "MenuItemStyle") == 0) {
			free(cfg->menu_item_style);
			cfg->menu_item_style = v;
		} else if (strcmp(key, "MenuHiliteStyle") == 0) {
			free(cfg->menu_hilite_style);
			cfg->menu_hilite_style = v;
		} else if (strcmp(key, "MenuTitleStyle") == 0) {
			free(cfg->menu_title_style);
			cfg->menu_title_style = v;
		} else if (strcmp(key, "MenuHiTitleStyle") == 0) {
			free(cfg->menu_hititle_style);
			cfg->menu_hititle_style = v;
		} else {
			free(v);
		}
	}

	free(line);
	fclose(fp);
	return true;
}

static bool aswl_try_set_path(char **dst, const char *candidate)
{
	if (dst == NULL || *dst != NULL)
		return false;
	if (!aswl_theme_is_file_readable(candidate))
		return false;
	*dst = strdup(candidate);
	return *dst != NULL;
}

static void aswl_theme_cfg_autofill_paths(struct aswl_theme_cfg *cfg)
{
	if (cfg == NULL)
		return;

	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		char *p = NULL;

		if (asprintf(&p, "%s/.afterstep/non-configurable/0_look", home) >= 0) {
			(void)aswl_try_set_path(&cfg->look_path, p);
			free(p);
		}
		if (asprintf(&p, "%s/.afterstep/non-configurable/0_colorscheme", home) >= 0) {
			(void)aswl_try_set_path(&cfg->colorscheme_path, p);
			free(p);
		}
	}

	(void)aswl_try_set_path(&cfg->look_path, "/usr/share/afterstep/non-configurable/0_look");
	(void)aswl_try_set_path(&cfg->colorscheme_path, "/usr/share/afterstep/non-configurable/0_colorscheme");

	/* Development/repo fallback (when run from repo root). */
	(void)aswl_try_set_path(&cfg->look_path, "afterstep/looks/look.DEFAULT");
	(void)aswl_try_set_path(&cfg->colorscheme_path, "afterstep/colorschemes/colorscheme.Stormy_Skies");
}

bool aswl_theme_load(struct aswl_theme *theme)
{
	if (theme == NULL)
		return false;

	free(theme->panel_font);
	theme->panel_font = NULL;
	free(theme->menu_font);
	theme->menu_font = NULL;
	free(theme->menu_title_font);
	theme->menu_title_font = NULL;
	free(theme->menu_hititle_font);
	theme->menu_hititle_font = NULL;
	free(theme->menu_hilite_font);
	theme->menu_hilite_font = NULL;
	free(theme->frame_font);
	theme->frame_font = NULL;
	free(theme->frame_inactive_font);
	theme->frame_inactive_font = NULL;
	theme->frame_active_text_style = 0;
	theme->frame_inactive_text_style = 0;

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
	theme->menu_item_text_style = 0;
	theme->menu_title_text_style = 0;
	theme->menu_hititle_text_style = 0;
	theme->menu_hilite_text_style = 0;

	theme->panel_back_pixmap_type = 0;
	theme->panel_back_pixmap_tint = 0;
	free(theme->panel_back_pixmap_path);
	theme->panel_back_pixmap_path = NULL;
	theme->menu_back_pixmap_type = 0;
	free(theme->menu_back_pixmap_path);
	theme->menu_back_pixmap_path = NULL;
	theme->frame_active_back_pixmap_type = 0;
	free(theme->frame_active_back_pixmap_path);
	theme->frame_active_back_pixmap_path = NULL;
	theme->frame_inactive_back_pixmap_type = 0;
	free(theme->frame_inactive_back_pixmap_path);
	theme->frame_inactive_back_pixmap_path = NULL;

		struct aswl_theme_cfg cfg = {
			.panel_style = strdup("*WharfTile"),
			.ws_active_style = strdup("*PagerActiveDesk"),
			.ws_inactive_style = strdup("*PagerInActiveDesk"),
			.desk_style = strdup("DeskStyle"),
			.win_active_style = NULL,
			.win_inactive_style = NULL,
			.win_sticky_style = NULL,
		};

	const char *cfg_path = getenv("ASWLTHEME_CONFIG");
	if (cfg_path != NULL && cfg_path[0] != '\0') {
		(void)aswl_theme_cfg_load_file(&cfg, cfg_path);
	} else {
		const char *home = getenv("HOME");
		if (home != NULL && home[0] != '\0') {
			char *p = NULL;
			if (asprintf(&p, "%s/.config/afterstep/aswltheme.conf", home) >= 0) {
				(void)aswl_theme_cfg_load_file(&cfg, p);
				free(p);
			}
		}
	}

	aswl_theme_cfg_autofill_paths(&cfg);

	struct aswl_color_entry *colors = NULL;
	size_t color_count = 0;
	(void)aswl_load_colorscheme(cfg.colorscheme_path, &colors, &color_count);

	/* Pager decoration defaults come from colorscheme tokens. */
	{
		uint32_t v = 0;
		if (aswl_colors_lookup(colors, color_count, "BaseDark", &v))
			theme->pager_border = v;
		if (aswl_colors_lookup(colors, color_count, "Inactive2Dark", &v))
			theme->pager_grid = v;
		if (aswl_colors_lookup(colors, color_count, "HighActiveLight", &v))
			theme->pager_selection = v;
	}

	struct aswl_style *styles = NULL;
	size_t style_count = 0;
	struct aswl_look_directives look_dirs = { 0 };
	(void)aswl_load_look(cfg.look_path, &styles, &style_count, &look_dirs);

	if (cfg.menu_item_style == NULL && look_dirs.menu_item_style != NULL)
		cfg.menu_item_style = strdup(look_dirs.menu_item_style);
	if (cfg.menu_hilite_style == NULL && look_dirs.menu_hilite_style != NULL)
		cfg.menu_hilite_style = strdup(look_dirs.menu_hilite_style);
	if (cfg.menu_title_style == NULL && look_dirs.menu_title_style != NULL)
		cfg.menu_title_style = strdup(look_dirs.menu_title_style);
	if (cfg.menu_hititle_style == NULL && look_dirs.menu_hititle_style != NULL)
		cfg.menu_hititle_style = strdup(look_dirs.menu_hititle_style);
	if (cfg.win_active_style == NULL && look_dirs.fwindow_style != NULL)
		cfg.win_active_style = strdup(look_dirs.fwindow_style);
	if (cfg.win_inactive_style == NULL && look_dirs.uwindow_style != NULL)
		cfg.win_inactive_style = strdup(look_dirs.uwindow_style);
	if (cfg.win_sticky_style == NULL && look_dirs.swindow_style != NULL)
		cfg.win_sticky_style = strdup(look_dirs.swindow_style);

	if (cfg.win_active_style == NULL)
		cfg.win_active_style = strdup("focused_window_style");
	if (cfg.win_inactive_style == NULL)
		cfg.win_inactive_style = strdup("unfocused_window_style");
	if (cfg.win_sticky_style == NULL)
		cfg.win_sticky_style = strdup("sticky_window_style");
	if (cfg.menu_hititle_style == NULL && cfg.menu_title_style != NULL)
		cfg.menu_hititle_style = strdup(cfg.menu_title_style);

	bool applied = false;
	uint32_t c = 0;

	bool panel_style_has_bg = false;

	if (aswl_resolve_style_color(styles, style_count, cfg.panel_style, false, colors, color_count, &c)) {
		theme->panel_button_bg = c;
		theme->panel_ws_inactive_bg = c;
		theme->panel_bg = c;
		panel_style_has_bg = true;
		applied = true;
	}
	if (aswl_resolve_style_color(styles, style_count, cfg.panel_style, true, colors, color_count, &c)) {
		theme->panel_button_fg = c;
		theme->panel_ws_inactive_fg = c;
		applied = true;
	}

	if (!panel_style_has_bg && aswl_colors_lookup(colors, color_count, "Base", &c)) {
		theme->panel_bg = c;
		applied = true;
	}

	{
		int bp_type = 0;
		uint32_t bp_tint = 0;
		if (aswl_resolve_style_back_pixmap_tint(styles, style_count, cfg.panel_style, colors, color_count, &bp_type, &bp_tint)) {
			theme->panel_back_pixmap_type = bp_type;
			theme->panel_back_pixmap_tint = bp_tint;
			applied = true;
		} else {
			/* Image-backed BackPixmap (127 scaled / 128 tiled). */
			char *bp_path = NULL;
			int bp_ptype = 0;
			if (aswl_resolve_style_back_pixmap_path(styles, style_count, cfg.panel_style, &bp_path, &bp_ptype)) {
				free(theme->panel_back_pixmap_path);
				theme->panel_back_pixmap_path = bp_path;
				theme->panel_back_pixmap_type = bp_ptype;
				applied = true;
			}
		}
	}

	if (aswl_resolve_style_color(styles, style_count, cfg.ws_inactive_style, false, colors, color_count, &c)) {
		theme->panel_ws_inactive_bg = c;
		applied = true;
	}
	if (aswl_resolve_style_color(styles, style_count, cfg.ws_inactive_style, true, colors, color_count, &c)) {
		theme->panel_ws_inactive_fg = c;
		applied = true;
	}
	if (aswl_resolve_style_color(styles, style_count, cfg.ws_active_style, false, colors, color_count, &c)) {
		theme->panel_ws_active_bg = c;
		applied = true;
	}
		if (aswl_resolve_style_color(styles, style_count, cfg.ws_active_style, true, colors, color_count, &c)) {
			theme->panel_ws_active_fg = c;
			applied = true;
		}

		if (cfg.desk_style != NULL && aswl_resolve_style_color(styles, style_count, cfg.desk_style, false, colors, color_count, &c)) {
			theme->desk_bg = c;
			applied = true;
		}

		if (cfg.win_active_style != NULL &&
		    aswl_resolve_style_color(styles, style_count, cfg.win_active_style, false, colors, color_count, &c)) {
		theme->frame_active_bg = c;
		applied = true;
	}
	if (cfg.win_active_style != NULL &&
	    aswl_resolve_style_color(styles, style_count, cfg.win_active_style, true, colors, color_count, &c)) {
		theme->frame_active_fg = c;
		applied = true;
	}

	if (cfg.win_inactive_style != NULL &&
	    aswl_resolve_style_color(styles, style_count, cfg.win_inactive_style, false, colors, color_count, &c)) {
		theme->frame_inactive_bg = c;
		applied = true;
	}
	if (cfg.win_inactive_style != NULL &&
	    aswl_resolve_style_color(styles, style_count, cfg.win_inactive_style, true, colors, color_count, &c)) {
		theme->frame_inactive_fg = c;
		applied = true;
	}
	if (cfg.win_active_style != NULL) {
		/* Image-backed BackPixmap (127 scaled / 128 tiled) for the active titlebar. */
		char *bp_path = NULL;
		int bp_ptype = 0;
		if (aswl_resolve_style_back_pixmap_path(styles, style_count, cfg.win_active_style, &bp_path, &bp_ptype)) {
			free(theme->frame_active_back_pixmap_path);
			theme->frame_active_back_pixmap_path = bp_path;
			theme->frame_active_back_pixmap_type = bp_ptype;
			applied = true;
		}
	}
	if (cfg.win_inactive_style != NULL) {
		char *bp_path = NULL;
		int bp_ptype = 0;
		if (aswl_resolve_style_back_pixmap_path(styles, style_count, cfg.win_inactive_style, &bp_path, &bp_ptype)) {
			free(theme->frame_inactive_back_pixmap_path);
			theme->frame_inactive_back_pixmap_path = bp_path;
			theme->frame_inactive_back_pixmap_type = bp_ptype;
			applied = true;
		}
	}

	if (cfg.menu_item_style != NULL &&
	    aswl_resolve_style_color(styles, style_count, cfg.menu_item_style, false, colors, color_count, &c)) {
		theme->menu_item_bg = c;
		theme->menu_bg = c;
		theme->menu_footer_bg = c;
		applied = true;
	}
	if (cfg.menu_item_style != NULL &&
	    aswl_resolve_style_color(styles, style_count, cfg.menu_item_style, true, colors, color_count, &c)) {
		theme->menu_item_fg = c;
		applied = true;
	}
	if (cfg.menu_item_style != NULL) {
		/* Image-backed BackPixmap (127 scaled / 128 tiled) for the menu body. */
		char *bp_path = NULL;
		int bp_ptype = 0;
		if (aswl_resolve_style_back_pixmap_path(styles, style_count, cfg.menu_item_style, &bp_path, &bp_ptype)) {
			free(theme->menu_back_pixmap_path);
			theme->menu_back_pixmap_path = bp_path;
			theme->menu_back_pixmap_type = bp_ptype;
			applied = true;
		}
	}

	if (cfg.menu_hilite_style != NULL &&
	    aswl_resolve_style_color(styles, style_count, cfg.menu_hilite_style, false, colors, color_count, &c)) {
		theme->menu_item_sel_bg = c;
		applied = true;
	}
	if (cfg.menu_hilite_style != NULL &&
	    aswl_resolve_style_color(styles, style_count, cfg.menu_hilite_style, true, colors, color_count, &c)) {
		theme->menu_item_sel_fg = c;
		applied = true;
	}

	if (cfg.menu_title_style != NULL &&
	    aswl_resolve_style_color(styles, style_count, cfg.menu_title_style, false, colors, color_count, &c)) {
		theme->menu_header_bg = c;
		applied = true;
	}
	if (cfg.menu_title_style != NULL &&
	    aswl_resolve_style_color(styles, style_count, cfg.menu_title_style, true, colors, color_count, &c)) {
		theme->menu_header_fg = c;
		applied = true;
	}
	if (cfg.menu_hititle_style != NULL &&
	    aswl_resolve_style_color(styles, style_count, cfg.menu_hititle_style, false, colors, color_count, &c)) {
		theme->menu_hititle_bg = c;
		applied = true;
	}
	if (cfg.menu_hititle_style != NULL &&
	    aswl_resolve_style_color(styles, style_count, cfg.menu_hititle_style, true, colors, color_count, &c)) {
		theme->menu_hititle_fg = c;
		applied = true;
	}

	if (aswl_resolve_style_gradient(styles, style_count, cfg.panel_style, colors, color_count, &theme->panel_bg_gradient))
		applied = true;
	/* Default: use the same style for the panel surface + basic button tiles. */
	(void)aswl_resolve_style_gradient(styles, style_count, cfg.panel_style, colors, color_count, &theme->panel_button_gradient);
	if (aswl_gradient_is_valid(&theme->panel_button_gradient))
		applied = true;
	(void)aswl_resolve_style_gradient(styles,
	                                 style_count,
	                                 cfg.ws_inactive_style,
	                                 colors,
	                                 color_count,
	                                 &theme->panel_ws_inactive_gradient);
		(void)aswl_resolve_style_gradient(styles,
		                                 style_count,
		                                 cfg.ws_active_style,
		                                 colors,
		                                 color_count,
		                                 &theme->panel_ws_active_gradient);
		if (aswl_gradient_is_valid(&theme->panel_ws_inactive_gradient) || aswl_gradient_is_valid(&theme->panel_ws_active_gradient))
			applied = true;

		if (cfg.desk_style != NULL)
			(void)aswl_resolve_style_gradient(styles, style_count, cfg.desk_style, colors, color_count, &theme->desk_gradient);
		if (aswl_gradient_is_valid(&theme->desk_gradient))
			applied = true;

		if (cfg.win_active_style != NULL)
			(void)aswl_resolve_style_gradient(styles,
			                                 style_count,
		                                 cfg.win_active_style,
		                                 colors,
		                                 color_count,
		                                 &theme->frame_active_gradient);
	if (cfg.win_inactive_style != NULL)
		(void)aswl_resolve_style_gradient(styles,
		                                 style_count,
		                                 cfg.win_inactive_style,
		                                 colors,
		                                 color_count,
		                                 &theme->frame_inactive_gradient);
	if (aswl_gradient_is_valid(&theme->frame_active_gradient) || aswl_gradient_is_valid(&theme->frame_inactive_gradient))
		applied = true;

	if (cfg.menu_title_style != NULL)
		(void)aswl_resolve_style_gradient(styles,
		                                 style_count,
		                                 cfg.menu_title_style,
		                                 colors,
		                                 color_count,
		                                 &theme->menu_header_gradient);
	if (cfg.menu_hititle_style != NULL)
		(void)aswl_resolve_style_gradient(styles,
		                                 style_count,
		                                 cfg.menu_hititle_style,
		                                 colors,
		                                 color_count,
		                                 &theme->menu_hititle_gradient);
	if (cfg.menu_item_style != NULL)
		(void)aswl_resolve_style_gradient(styles,
		                                 style_count,
		                                 cfg.menu_item_style,
		                                 colors,
		                                 color_count,
		                                 &theme->menu_item_gradient);
	if (cfg.menu_hilite_style != NULL)
		(void)aswl_resolve_style_gradient(styles,
		                                 style_count,
		                                 cfg.menu_hilite_style,
		                                 colors,
		                                 color_count,
		                                 &theme->menu_item_sel_gradient);
	if (aswl_gradient_is_valid(&theme->menu_header_gradient) || aswl_gradient_is_valid(&theme->menu_hititle_gradient) ||
	    aswl_gradient_is_valid(&theme->menu_item_gradient) || aswl_gradient_is_valid(&theme->menu_item_sel_gradient))
		applied = true;

	char *font = NULL;
	if (aswl_resolve_style_font(styles, style_count, cfg.panel_style, &font)) {
		theme->panel_font = font;
		applied = true;
		font = NULL;
	}
	if (cfg.win_active_style != NULL && aswl_resolve_style_font(styles, style_count, cfg.win_active_style, &font)) {
		theme->frame_font = font;
		applied = true;
		font = NULL;
	}
	if (cfg.win_inactive_style != NULL && aswl_resolve_style_font(styles, style_count, cfg.win_inactive_style, &font)) {
		theme->frame_inactive_font = font;
		applied = true;
		font = NULL;
	}
	if (cfg.menu_item_style != NULL && aswl_resolve_style_font(styles, style_count, cfg.menu_item_style, &font)) {
		theme->menu_font = font;
		applied = true;
		font = NULL;
	}
		if (cfg.menu_title_style != NULL && aswl_resolve_style_font(styles, style_count, cfg.menu_title_style, &font)) {
			theme->menu_title_font = font;
			applied = true;
			font = NULL;
		}
		if (cfg.menu_hititle_style != NULL && aswl_resolve_style_font(styles, style_count, cfg.menu_hititle_style, &font)) {
			theme->menu_hititle_font = font;
			applied = true;
			font = NULL;
		}
			if (cfg.menu_hilite_style != NULL && aswl_resolve_style_font(styles, style_count, cfg.menu_hilite_style, &font)) {
				theme->menu_hilite_font = font;
				applied = true;
				font = NULL;
		}
		free(font);

		int ts = -1;
		if (cfg.win_active_style != NULL && aswl_resolve_style_text_style(styles, style_count, cfg.win_active_style, &ts) && ts >= 0) {
			theme->frame_active_text_style = ts;
			applied = true;
		}
		ts = -1;
		if (cfg.win_inactive_style != NULL && aswl_resolve_style_text_style(styles, style_count, cfg.win_inactive_style, &ts) && ts >= 0) {
			theme->frame_inactive_text_style = ts;
			applied = true;
		}
		ts = -1;
		if (cfg.menu_item_style != NULL && aswl_resolve_style_text_style(styles, style_count, cfg.menu_item_style, &ts) && ts >= 0) {
			theme->menu_item_text_style = ts;
			applied = true;
		}
			ts = -1;
			if (cfg.menu_title_style != NULL && aswl_resolve_style_text_style(styles, style_count, cfg.menu_title_style, &ts) && ts >= 0) {
				theme->menu_title_text_style = ts;
				applied = true;
			}
			ts = -1;
			if (cfg.menu_hititle_style != NULL &&
			    aswl_resolve_style_text_style(styles, style_count, cfg.menu_hititle_style, &ts) && ts >= 0) {
				theme->menu_hititle_text_style = ts;
				applied = true;
			}
			ts = -1;
			if (cfg.menu_hilite_style != NULL && aswl_resolve_style_text_style(styles, style_count, cfg.menu_hilite_style, &ts) && ts >= 0) {
				theme->menu_hilite_text_style = ts;
				applied = true;
			}

		/* Borders and footer color are derived for now. */
		theme->panel_border = aswl_color_darken(theme->panel_bg, 170);
		/*
		 * AfterStep's look.DEFAULT window/menu frames are hard-edged and read
		 * as black borders in the X11 reference screenshots.
		 */
		theme->menu_border = 0xFF000000u;
		theme->frame_border = 0xFF000000u;
	theme->menu_footer_fg = aswl_color_nudge(theme->menu_item_fg, 90);

	aswl_free_colors(colors, color_count);
	aswl_free_styles(styles, style_count);
	aswl_free_look_directives(&look_dirs);
	aswl_theme_cfg_free(&cfg);
	return applied;
}
