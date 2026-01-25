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

	*theme = (struct aswl_theme){
		.panel_bg = 0xFF202020u,
		.panel_border = 0xFF101010u,
		.panel_button_bg = 0xFF3A3A3Au,
		.panel_button_fg = 0xFFE0E0E0u,
		.panel_ws_inactive_bg = 0xFF3A3A3Au,
		.panel_ws_inactive_fg = 0xFFE0E0E0u,
		.panel_ws_active_bg = 0xFF2E4A7Au,
		.panel_ws_active_fg = 0xFFE0E0E0u,

		.menu_bg = 0xFF202020u,
		.menu_border = 0xFF101010u,
		.menu_header_bg = 0xFF2D2D2Du,
		.menu_header_fg = 0xFFE8E8E8u,
		.menu_item_bg = 0xFF262626u,
		.menu_item_fg = 0xFFE0E0E0u,
		.menu_item_sel_bg = 0xFF3A507Au,
		.menu_item_sel_fg = 0xFFE0E0E0u,
		.menu_footer_bg = 0xFF202020u,
		.menu_footer_fg = 0xFFB0B0B0u,
	};
}

uint32_t aswl_color_blend(uint32_t a, uint32_t b, uint8_t t)
{
	uint32_t ar = (a >> 16) & 0xFFu;
	uint32_t ag = (a >> 8) & 0xFFu;
	uint32_t ab = a & 0xFFu;
	uint32_t br = (b >> 16) & 0xFFu;
	uint32_t bg = (b >> 8) & 0xFFu;
	uint32_t bb = b & 0xFFu;

	uint32_t r = (ar * (255u - t) + br * t) / 255u;
	uint32_t g = (ag * (255u - t) + bg * t) / 255u;
	uint32_t bl = (ab * (255u - t) + bb * t) / 255u;
	return 0xFF000000u | (r << 16) | (g << 8) | bl;
}

uint32_t aswl_color_lighten(uint32_t c, uint8_t t)
{
	return aswl_color_blend(c, 0xFFFFFFFFu, t);
}

uint32_t aswl_color_darken(uint32_t c, uint8_t t)
{
	return aswl_color_blend(c, 0xFF000000u, t);
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

static bool aswl_is_file_readable(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return false;

	struct stat st;
	if (stat(path, &st) != 0)
		return false;
	if (!S_ISREG(st.st_mode))
		return false;
	return access(path, R_OK) == 0;
}

static char *aswl_trim(char *s)
{
	if (s == NULL)
		return NULL;
	while (*s != '\0' && isspace((unsigned char)*s))
		s++;
	char *end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return s;
}

static char *aswl_dup_unquoted(const char *s)
{
	if (s == NULL)
		return NULL;
	while (*s != '\0' && isspace((unsigned char)*s))
		s++;
	size_t len = strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1]))
		len--;
	if (len == 0)
		return NULL;

	if ((s[0] == '"' && len >= 2 && s[len - 1] == '"') || (s[0] == '\'' && len >= 2 && s[len - 1] == '\'')) {
		char *out = strndup(s + 1, len - 2);
		return out;
	}

	return strndup(s, len);
}

static bool aswl_parse_hex_color(const char *s, uint32_t *argb_out)
{
	if (argb_out != NULL)
		*argb_out = 0;
	if (s == NULL || s[0] != '#')
		return false;

	s++;
	size_t n = 0;
	while (isxdigit((unsigned char)s[n]))
		n++;

	if (n != 6 && n != 8)
		return false;

	uint32_t v = 0;
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		uint32_t x = 0;
		if (c >= '0' && c <= '9')
			x = (uint32_t)(c - '0');
		else if (c >= 'a' && c <= 'f')
			x = 10u + (uint32_t)(c - 'a');
		else if (c >= 'A' && c <= 'F')
			x = 10u + (uint32_t)(c - 'A');
		else
			return false;
		v = (v << 4) | x;
	}

	if (n == 6)
		v |= 0xFF000000u;

	if (argb_out != NULL)
		*argb_out = v;
	return true;
}

struct aswl_color_entry {
	char *name;
	uint32_t argb;
};

static void aswl_free_colors(struct aswl_color_entry *colors, size_t count)
{
	if (colors == NULL)
		return;
	for (size_t i = 0; i < count; i++)
		free(colors[i].name);
	free(colors);
}

static bool aswl_colors_set(struct aswl_color_entry **colors,
                            size_t *count,
                            size_t *cap,
                            const char *name,
                            uint32_t argb,
                            bool only_if_missing)
{
	if (colors == NULL || count == NULL || cap == NULL || name == NULL || name[0] == '\0')
		return false;

	for (size_t i = 0; i < *count; i++) {
		if (strcmp((*colors)[i].name, name) == 0) {
			if (!only_if_missing)
				(*colors)[i].argb = argb;
			return true;
		}
	}

	if (*count == *cap) {
		size_t next = *cap == 0 ? 32 : (*cap) * 2;
		struct aswl_color_entry *tmp = realloc(*colors, next * sizeof(**colors));
		if (tmp == NULL)
			return false;
		*colors = tmp;
		*cap = next;
	}

	(*colors)[*count] = (struct aswl_color_entry){ 0 };
	(*colors)[*count].name = strdup(name);
	(*colors)[*count].argb = argb;
	if ((*colors)[*count].name == NULL)
		return false;

	(*count)++;
	return true;
}

static bool aswl_colors_lookup(const struct aswl_color_entry *colors, size_t count, const char *name, uint32_t *argb_out)
{
	if (argb_out != NULL)
		*argb_out = 0;
	if (colors == NULL || name == NULL || name[0] == '\0')
		return false;
	for (size_t i = 0; i < count; i++) {
		if (strcmp(colors[i].name, name) == 0) {
			if (argb_out != NULL)
				*argb_out = colors[i].argb;
			return true;
		}
	}
	return false;
}

static bool aswl_load_colorscheme(const char *path, struct aswl_color_entry **colors_out, size_t *count_out)
{
	if (colors_out != NULL)
		*colors_out = NULL;
	if (count_out != NULL)
		*count_out = 0;

	if (!aswl_is_file_readable(path))
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	struct aswl_color_entry *colors = NULL;
	size_t count = 0;
	size_t cap = 0;

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len;

	while ((line_len = getline(&line, &line_cap, fp)) != -1) {
		(void)line_len;
		char *s = aswl_trim(line);
		if (s == NULL || s[0] == '\0')
			continue;

		bool disabled = false;
		static const char *disabled_prefix = "#~~DISABLED~~#";
		if (strncmp(s, disabled_prefix, strlen(disabled_prefix)) == 0) {
			s += strlen(disabled_prefix);
			s = aswl_trim(s);
			disabled = true;
		} else if (s[0] == '#') {
			continue;
		}

		char *name = s;
		while (*s != '\0' && !isspace((unsigned char)*s))
			s++;
		if (*s == '\0')
			continue;
		*s++ = '\0';
		s = aswl_trim(s);
		if (s == NULL || s[0] == '\0')
			continue;

		char *value = s;
		while (*s != '\0' && !isspace((unsigned char)*s))
			s++;
		*s = '\0';

		uint32_t argb = 0;
		if (!aswl_parse_hex_color(value, &argb))
			continue;

		if (!aswl_colors_set(&colors, &count, &cap, name, argb, disabled))
			goto fail;
	}

	free(line);
	fclose(fp);

	if (count == 0) {
		free(colors);
		return false;
	}

	if (colors_out != NULL)
		*colors_out = colors;
	if (count_out != NULL)
		*count_out = count;
	return true;

fail:
	free(line);
	fclose(fp);
	aswl_free_colors(colors, count);
	return false;
}

struct aswl_style {
	char *name;
	char *fore;
	char *back;
	char **inherits;
	size_t inherit_count;
};

struct aswl_look_directives {
	char *menu_item_style;
	char *menu_hilite_style;
	char *menu_title_style;
};

static void aswl_free_styles(struct aswl_style *styles, size_t count)
{
	if (styles == NULL)
		return;
	for (size_t i = 0; i < count; i++) {
		free(styles[i].name);
		free(styles[i].fore);
		free(styles[i].back);
		for (size_t j = 0; j < styles[i].inherit_count; j++)
			free(styles[i].inherits[j]);
		free(styles[i].inherits);
	}
	free(styles);
}

static void aswl_free_look_directives(struct aswl_look_directives *d)
{
	if (d == NULL)
		return;
	free(d->menu_item_style);
	free(d->menu_hilite_style);
	free(d->menu_title_style);
	*d = (struct aswl_look_directives){ 0 };
}

static struct aswl_style *aswl_find_style(struct aswl_style *styles, size_t count, const char *name)
{
	if (styles == NULL || name == NULL || name[0] == '\0')
		return NULL;
	for (size_t i = 0; i < count; i++) {
		if (styles[i].name != NULL && strcmp(styles[i].name, name) == 0)
			return &styles[i];
	}
	return NULL;
}

static bool aswl_style_add_inherit(struct aswl_style *st, const char *name)
{
	if (st == NULL || name == NULL || name[0] == '\0')
		return false;

	char **tmp = realloc(st->inherits, (st->inherit_count + 1) * sizeof(*st->inherits));
	if (tmp == NULL)
		return false;
	st->inherits = tmp;
	st->inherits[st->inherit_count] = strdup(name);
	if (st->inherits[st->inherit_count] == NULL)
		return false;
	st->inherit_count++;
	return true;
}

static char *aswl_parse_quoted_or_token_dup(const char *s)
{
	if (s == NULL)
		return NULL;
	s = aswl_trim((char *)s);
	if (s == NULL || s[0] == '\0')
		return NULL;
	if (s[0] == '"' || s[0] == '\'')
		return aswl_dup_unquoted(s);

	const char *end = s;
	while (*end != '\0' && !isspace((unsigned char)*end))
		end++;
	return strndup(s, (size_t)(end - s));
}

static bool aswl_load_look(const char *path,
                           struct aswl_style **styles_out,
                           size_t *style_count_out,
                           struct aswl_look_directives *dirs_out)
{
	if (styles_out != NULL)
		*styles_out = NULL;
	if (style_count_out != NULL)
		*style_count_out = 0;
	if (dirs_out != NULL)
		*dirs_out = (struct aswl_look_directives){ 0 };

	if (!aswl_is_file_readable(path))
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	struct aswl_style *styles = NULL;
	size_t count = 0;
	size_t cap = 0;
	struct aswl_look_directives dirs = { 0 };

	struct aswl_style *cur = NULL;

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len;

	while ((line_len = getline(&line, &line_cap, fp)) != -1) {
		(void)line_len;
		char *s = aswl_trim(line);
		if (s == NULL || s[0] == '\0' || s[0] == '#')
			continue;

		if (cur != NULL) {
			if (strncmp(s, "~MyStyle", 8) == 0 || strncmp(s, "EndStyle", 8) == 0) {
				cur = NULL;
				continue;
			}

			if (strncmp(s, "ForeColor", 9) == 0 && isspace((unsigned char)s[9])) {
				char *v = aswl_parse_quoted_or_token_dup(s + 9);
				if (v != NULL) {
					free(cur->fore);
					cur->fore = v;
				}
				continue;
			}
			if (strncmp(s, "BackColor", 9) == 0 && isspace((unsigned char)s[9])) {
				char *v = aswl_parse_quoted_or_token_dup(s + 9);
				if (v != NULL) {
					free(cur->back);
					cur->back = v;
				}
				continue;
			}
			if (strncmp(s, "Inherit", 7) == 0 && isspace((unsigned char)s[7])) {
				char *v = aswl_parse_quoted_or_token_dup(s + 7);
				if (v != NULL) {
					if (!aswl_style_add_inherit(cur, v)) {
						free(v);
						goto fail;
					}
					free(v);
				}
				continue;
			}

			continue;
		}

		if (strncmp(s, "MyStyle", 7) == 0 && isspace((unsigned char)s[7])) {
			char *name = aswl_parse_quoted_or_token_dup(s + 7);
			if (name == NULL)
				continue;

			struct aswl_style *existing = aswl_find_style(styles, count, name);
			if (existing != NULL) {
				cur = existing;
				free(name);
				continue;
			}

			if (count == cap) {
				size_t next = cap == 0 ? 64 : cap * 2;
				struct aswl_style *tmp = realloc(styles, next * sizeof(*styles));
				if (tmp == NULL) {
					free(name);
					goto fail;
				}
				styles = tmp;
				cap = next;
			}

			styles[count] = (struct aswl_style){ 0 };
			styles[count].name = name;
			cur = &styles[count];
			count++;
			continue;
		}

		if (strncmp(s, "MenuItemStyle", 13) == 0 && isspace((unsigned char)s[13])) {
			char *v = aswl_parse_quoted_or_token_dup(s + 13);
			if (v != NULL && dirs.menu_item_style == NULL)
				dirs.menu_item_style = v;
			else
				free(v);
			continue;
		}
		if (strncmp(s, "MenuHiliteStyle", 15) == 0 && isspace((unsigned char)s[15])) {
			char *v = aswl_parse_quoted_or_token_dup(s + 15);
			if (v != NULL && dirs.menu_hilite_style == NULL)
				dirs.menu_hilite_style = v;
			else
				free(v);
			continue;
		}
		if (strncmp(s, "MenuTitleStyle", 14) == 0 && isspace((unsigned char)s[14])) {
			char *v = aswl_parse_quoted_or_token_dup(s + 14);
			if (v != NULL && dirs.menu_title_style == NULL)
				dirs.menu_title_style = v;
			else
				free(v);
			continue;
		}
	}

	free(line);
	fclose(fp);

	if (styles_out != NULL)
		*styles_out = styles;
	if (style_count_out != NULL)
		*style_count_out = count;
	if (dirs_out != NULL)
		*dirs_out = dirs;
	else
		aswl_free_look_directives(&dirs);
	return count > 0;

fail:
	free(line);
	fclose(fp);
	aswl_free_styles(styles, count);
	aswl_free_look_directives(&dirs);
	return false;
}

static bool aswl_parse_color_token(const char *token,
                                  const struct aswl_color_entry *colors,
                                  size_t color_count,
                                  uint32_t *argb_out)
{
	if (argb_out != NULL)
		*argb_out = 0;
	if (token == NULL || token[0] == '\0')
		return false;

	uint32_t c = 0;
	if (aswl_parse_hex_color(token, &c)) {
		if (argb_out != NULL)
			*argb_out = c;
		return true;
	}

	if (aswl_colors_lookup(colors, color_count, token, &c)) {
		if (argb_out != NULL)
			*argb_out = c;
		return true;
	}

	return false;
}

static bool aswl_resolve_style_color_rec(struct aswl_style *styles,
                                        size_t style_count,
                                        const struct aswl_style *st,
                                        bool want_fore,
                                        const struct aswl_color_entry *colors,
                                        size_t color_count,
                                        const char **stack,
                                        size_t stack_len,
                                        uint32_t *argb_out)
{
	if (argb_out != NULL)
		*argb_out = 0;
	if (st == NULL)
		return false;

	for (size_t i = 0; i < stack_len; i++) {
		if (stack[i] != NULL && st->name != NULL && strcmp(stack[i], st->name) == 0)
			return false;
	}

	if (stack_len >= 16)
		return false;

	const char *next_stack[16];
	for (size_t i = 0; i < stack_len; i++)
		next_stack[i] = stack[i];
	next_stack[stack_len] = st->name;

	const char *tok = want_fore ? st->fore : st->back;
	uint32_t c = 0;
	if (tok != NULL && aswl_parse_color_token(tok, colors, color_count, &c)) {
		if (argb_out != NULL)
			*argb_out = c;
		return true;
	}

	for (size_t i = 0; i < st->inherit_count; i++) {
		struct aswl_style *parent = aswl_find_style(styles, style_count, st->inherits[i]);
		if (parent == NULL)
			continue;
		if (aswl_resolve_style_color_rec(styles,
		                                style_count,
		                                parent,
		                                want_fore,
		                                colors,
		                                color_count,
		                                next_stack,
		                                stack_len + 1,
		                                &c)) {
			if (argb_out != NULL)
				*argb_out = c;
			return true;
		}
	}

	return false;
}

static bool aswl_resolve_style_color(struct aswl_style *styles,
                                    size_t style_count,
                                    const char *style_name,
                                    bool want_fore,
                                    const struct aswl_color_entry *colors,
                                    size_t color_count,
                                    uint32_t *argb_out)
{
	if (argb_out != NULL)
		*argb_out = 0;
	if (style_name == NULL || style_name[0] == '\0')
		return false;

	struct aswl_style *st = aswl_find_style(styles, style_count, style_name);
	if (st == NULL)
		return false;

	const char *stack[16] = { 0 };
	return aswl_resolve_style_color_rec(styles, style_count, st, want_fore, colors, color_count, stack, 0, argb_out);
}

struct aswl_theme_cfg {
	char *look_path;
	char *colorscheme_path;

	char *panel_style;
	char *ws_active_style;
	char *ws_inactive_style;

	char *menu_item_style;
	char *menu_hilite_style;
	char *menu_title_style;
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
	free(cfg->menu_item_style);
	free(cfg->menu_hilite_style);
	free(cfg->menu_title_style);
	*cfg = (struct aswl_theme_cfg){ 0 };
}

static bool aswl_theme_cfg_load_file(struct aswl_theme_cfg *cfg, const char *path)
{
	if (cfg == NULL || !aswl_is_file_readable(path))
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
		} else if (strcmp(key, "MenuItemStyle") == 0) {
			free(cfg->menu_item_style);
			cfg->menu_item_style = v;
		} else if (strcmp(key, "MenuHiliteStyle") == 0) {
			free(cfg->menu_hilite_style);
			cfg->menu_hilite_style = v;
		} else if (strcmp(key, "MenuTitleStyle") == 0) {
			free(cfg->menu_title_style);
			cfg->menu_title_style = v;
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
	if (!aswl_is_file_readable(candidate))
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

	struct aswl_theme_cfg cfg = {
		.panel_style = strdup("*WharfTile"),
		.ws_active_style = strdup("*PagerActiveDesk"),
		.ws_inactive_style = strdup("*PagerInActiveDesk"),
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

	bool applied = false;
	uint32_t c = 0;

	if (aswl_colors_lookup(colors, color_count, "Base", &c)) {
		theme->panel_bg = c;
		applied = true;
	}

	if (aswl_resolve_style_color(styles, style_count, cfg.panel_style, false, colors, color_count, &c)) {
		theme->panel_button_bg = c;
		theme->panel_ws_inactive_bg = c;
		applied = true;
	}
	if (aswl_resolve_style_color(styles, style_count, cfg.panel_style, true, colors, color_count, &c)) {
		theme->panel_button_fg = c;
		theme->panel_ws_inactive_fg = c;
		applied = true;
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

	/* Borders and footer color are derived for now. */
	theme->panel_border = aswl_color_darken(theme->panel_bg, 170);
	theme->menu_border = aswl_color_darken(theme->menu_bg, 170);
	theme->menu_footer_fg = aswl_color_nudge(theme->menu_item_fg, 90);

	aswl_free_colors(colors, color_count);
	aswl_free_styles(styles, style_count);
	aswl_free_look_directives(&look_dirs);
	aswl_theme_cfg_free(&cfg);
	return applied;
}
