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

		.menu_font = NULL,
		.menu_title_font = NULL,
		.menu_hilite_font = NULL,
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
	aswl_gradient_destroy(&theme->menu_item_gradient);
	aswl_gradient_destroy(&theme->menu_item_sel_gradient);
	free(theme->panel_font);
	free(theme->menu_font);
	free(theme->menu_title_font);
	free(theme->menu_hilite_font);
	free(theme->frame_font);
	free(theme->frame_inactive_font);
	theme->panel_font = NULL;
	theme->menu_font = NULL;
	theme->menu_title_font = NULL;
	theme->menu_hilite_font = NULL;
	theme->frame_font = NULL;
	theme->frame_inactive_font = NULL;
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
	char *font;
	int back_grad_type;
	char **back_grad_colors;
	double *back_grad_offsets;
	size_t back_grad_count;
	char **inherits;
	size_t inherit_count;
};

struct aswl_look_directives {
	char *menu_item_style;
	char *menu_hilite_style;
	char *menu_title_style;
	char *fwindow_style;
	char *uwindow_style;
	char *swindow_style;
};

static void aswl_free_styles(struct aswl_style *styles, size_t count)
{
	if (styles == NULL)
		return;
	for (size_t i = 0; i < count; i++) {
		free(styles[i].name);
		free(styles[i].fore);
		free(styles[i].back);
		free(styles[i].font);
		for (size_t j = 0; j < styles[i].back_grad_count; j++)
			free(styles[i].back_grad_colors[j]);
		free(styles[i].back_grad_colors);
		free(styles[i].back_grad_offsets);
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
	free(d->fwindow_style);
	free(d->uwindow_style);
	free(d->swindow_style);
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

static void aswl_style_clear_back_gradient(struct aswl_style *st)
{
	if (st == NULL)
		return;
	for (size_t i = 0; i < st->back_grad_count; i++)
		free(st->back_grad_colors[i]);
	free(st->back_grad_colors);
	free(st->back_grad_offsets);
	st->back_grad_colors = NULL;
	st->back_grad_offsets = NULL;
	st->back_grad_count = 0;
	st->back_grad_type = 0;
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
			if (strncmp(s, "Font", 4) == 0 && isspace((unsigned char)s[4])) {
				char *v = aswl_parse_quoted_or_token_dup(s + 4);
				if (v != NULL) {
					free(cur->font);
					cur->font = v;
				}
				continue;
			}
			if (strncmp(s, "BackGradient", 12) == 0 && isspace((unsigned char)s[12])) {
				/* BackGradient <type> <from> <to> */
				char *args = aswl_trim(s + 12);
				if (args == NULL || args[0] == '\0')
					continue;

				char *p = args;
				char *tok_type = p;
				while (*p != '\0' && !isspace((unsigned char)*p))
					p++;
				if (*p != '\0')
					*p++ = '\0';

				p = aswl_trim(p);
				if (p == NULL || p[0] == '\0')
					continue;
				char *tok_from = p;
				while (*p != '\0' && !isspace((unsigned char)*p))
					p++;
				if (*p != '\0')
					*p++ = '\0';

				p = aswl_trim(p);
				if (p == NULL || p[0] == '\0')
					continue;
				char *tok_to = p;
				while (*p != '\0' && !isspace((unsigned char)*p))
					p++;
				*p = '\0';

				char *endptr = NULL;
				long type = strtol(tok_type, &endptr, 10);
				if (endptr == tok_type || type <= 0 || type > 9)
					continue;

				char *c0 = aswl_dup_unquoted(tok_from);
				char *c1 = aswl_dup_unquoted(tok_to);
				if (c0 == NULL || c1 == NULL) {
					free(c0);
					free(c1);
					continue;
				}

				aswl_style_clear_back_gradient(cur);
				cur->back_grad_type = (int)type;
				cur->back_grad_count = 2;
				cur->back_grad_colors = calloc(2, sizeof(*cur->back_grad_colors));
				cur->back_grad_offsets = calloc(2, sizeof(*cur->back_grad_offsets));
				if (cur->back_grad_colors == NULL || cur->back_grad_offsets == NULL) {
					free(c0);
					free(c1);
					aswl_style_clear_back_gradient(cur);
					continue;
				}
				cur->back_grad_colors[0] = c0;
				cur->back_grad_colors[1] = c1;
				cur->back_grad_offsets[0] = 0.0;
				cur->back_grad_offsets[1] = 1.0;
				continue;
			}
			if (strncmp(s, "BackMultiGradient", 17) == 0 && isspace((unsigned char)s[17])) {
				/* BackMultiGradient <type> <color0> <off0> <color1> <off1> ... */
				char *args = aswl_trim(s + 17);
				if (args == NULL || args[0] == '\0')
					continue;

				char *p = args;
				char *tok_type = p;
				while (*p != '\0' && !isspace((unsigned char)*p))
					p++;
				if (*p != '\0')
					*p++ = '\0';
				p = aswl_trim(p);
				if (p == NULL || p[0] == '\0')
					continue;

				char *endptr = NULL;
				long type = strtol(tok_type, &endptr, 10);
				if (endptr == tok_type || type <= 0 || type > 9)
					continue;

				char **colors = NULL;
				double *offsets = NULL;
				size_t grad_count = 0;
				size_t grad_cap = 0;

				while (p != NULL && p[0] != '\0') {
					char *tok_color = p;
					while (*p != '\0' && !isspace((unsigned char)*p))
						p++;
					if (*p != '\0')
						*p++ = '\0';

					char *color_dup = aswl_dup_unquoted(tok_color);
					if (color_dup == NULL)
						break;

					p = aswl_trim(p);
					double off = 0.0;
					if (p != NULL && p[0] != '\0') {
						char *tok_off = p;
						while (*p != '\0' && !isspace((unsigned char)*p))
							p++;
						if (*p != '\0')
							*p++ = '\0';
						char *off_end = NULL;
						off = strtod(tok_off, &off_end);
						if (off_end == tok_off)
							off = 0.0;
						p = aswl_trim(p);
					} else
						p = NULL;

					if (grad_count == grad_cap) {
						size_t next = grad_cap == 0 ? 8 : grad_cap * 2;
						char **c2 = realloc(colors, next * sizeof(*colors));
						double *o2 = realloc(offsets, next * sizeof(*offsets));
						if (c2 == NULL || o2 == NULL) {
							free(color_dup);
							free(c2);
							free(o2);
							break;
						}
						colors = c2;
						offsets = o2;
						grad_cap = next;
					}

					colors[grad_count] = color_dup;
					offsets[grad_count] = off;
					grad_count++;
				}

				if (grad_count < 2) {
					for (size_t i = 0; i < grad_count; i++)
						free(colors[i]);
					free(colors);
					free(offsets);
					continue;
				}

				/* AfterStep forces the last stop to 1.0; also clamp obvious outliers. */
				if (offsets[0] < 0.0 || offsets[0] > 1.0)
					offsets[0] = 0.0;
				for (size_t i = 1; i < grad_count; i++) {
					if (offsets[i] < offsets[i - 1])
						offsets[i] = offsets[i - 1];
					if (offsets[i] < 0.0)
						offsets[i] = 0.0;
					if (offsets[i] > 1.0)
						offsets[i] = 1.0;
				}
				offsets[grad_count - 1] = 1.0;

				aswl_style_clear_back_gradient(cur);
				cur->back_grad_type = (int)type;
				cur->back_grad_count = grad_count;
				cur->back_grad_colors = colors;
				cur->back_grad_offsets = offsets;
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
		if (strncmp(s, "FWindowStyle", 12) == 0 && isspace((unsigned char)s[12])) {
			char *v = aswl_parse_quoted_or_token_dup(s + 12);
			if (v != NULL && dirs.fwindow_style == NULL)
				dirs.fwindow_style = v;
			else
				free(v);
			continue;
		}
		if (strncmp(s, "UWindowStyle", 12) == 0 && isspace((unsigned char)s[12])) {
			char *v = aswl_parse_quoted_or_token_dup(s + 12);
			if (v != NULL && dirs.uwindow_style == NULL)
				dirs.uwindow_style = v;
			else
				free(v);
			continue;
		}
		if (strncmp(s, "SWindowStyle", 12) == 0 && isspace((unsigned char)s[12])) {
			char *v = aswl_parse_quoted_or_token_dup(s + 12);
			if (v != NULL && dirs.swindow_style == NULL)
				dirs.swindow_style = v;
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

	while (*token != '\0' && isspace((unsigned char)*token))
		token++;

	/* Support a small, AfterStep-compatible subset of the color expression language. */
	if ((token[0] == 'a' || token[0] == 'A') &&
	    (token[1] == 'l' || token[1] == 'L') &&
	    (token[2] == 'p' || token[2] == 'P') &&
	    (token[3] == 'h' || token[3] == 'H') &&
	    (token[4] == 'a' || token[4] == 'A') &&
	    token[5] == '(') {
		const char *p = token + 6;
		while (*p != '\0' && isspace((unsigned char)*p))
			p++;

		char *end = NULL;
		long pct = strtol(p, &end, 10);
		if (end == p)
			return false;
		if (pct < 0)
			pct = 0;
		if (pct > 100)
			pct = 100;

		p = end;
		while (*p != '\0' && isspace((unsigned char)*p))
			p++;
		if (*p != ',')
			return false;
		p++;
		while (*p != '\0' && isspace((unsigned char)*p))
			p++;

		const char *arg = p;
		int depth = 1;
		while (*p != '\0') {
			if (*p == '(')
				depth++;
			else if (*p == ')') {
				depth--;
				if (depth == 0)
					break;
			}
			p++;
		}

		size_t len = (size_t)(p - arg);
		while (len > 0 && isspace((unsigned char)arg[len - 1]))
			len--;
		char *inner = strndup(arg, len);
		if (inner == NULL)
			return false;

		uint32_t base = 0;
		bool ok = aswl_parse_color_token(inner, colors, color_count, &base);
		free(inner);
		if (!ok)
			return false;

		uint32_t a = (uint32_t)((pct * 255L) / 100L) & 0xFFu;
		if (argb_out != NULL)
			*argb_out = (base & 0x00FFFFFFu) | (a << 24);
		return true;
	}

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

	/* Some shipped looks have minor tokenization warts (e.g. "Inactive2Light)"); be forgiving. */
	char name[128];
	size_t n = 0;
	while (token[n] != '\0' && (isalnum((unsigned char)token[n]) || token[n] == '.' || token[n] == '_')) {
		if (n + 1 >= sizeof(name))
			break;
		name[n] = token[n];
		n++;
	}
	name[n] = '\0';
	if (n > 0 && strcmp(name, token) != 0) {
		if (aswl_colors_lookup(colors, color_count, name, &c)) {
			if (argb_out != NULL)
				*argb_out = c;
			return true;
		}
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

static bool aswl_resolve_style_font_rec(struct aswl_style *styles,
                                       size_t style_count,
                                       const struct aswl_style *st,
                                       const char **stack,
                                       size_t stack_len,
                                       const char **font_out)
{
	if (font_out != NULL)
		*font_out = NULL;
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

	if (st->font != NULL && st->font[0] != '\0') {
		if (font_out != NULL)
			*font_out = st->font;
		return true;
	}

	for (size_t i = 0; i < st->inherit_count; i++) {
		struct aswl_style *parent = aswl_find_style(styles, style_count, st->inherits[i]);
		if (parent == NULL)
			continue;
		const char *tok = NULL;
		if (aswl_resolve_style_font_rec(styles, style_count, parent, next_stack, stack_len + 1, &tok)) {
			if (font_out != NULL)
				*font_out = tok;
			return true;
		}
	}

	return false;
}

static bool aswl_resolve_style_font(struct aswl_style *styles,
                                   size_t style_count,
                                   const char *style_name,
                                   char **font_out)
{
	if (font_out != NULL)
		*font_out = NULL;
	if (style_name == NULL || style_name[0] == '\0')
		return false;

	struct aswl_style *st = aswl_find_style(styles, style_count, style_name);
	if (st == NULL)
		return false;

	const char *stack[16] = { 0 };
	const char *tok = NULL;
	if (!aswl_resolve_style_font_rec(styles, style_count, st, stack, 0, &tok))
		return false;
	if (tok == NULL || tok[0] == '\0')
		return false;

	if (font_out == NULL)
		return true;
	*font_out = strdup(tok);
	return *font_out != NULL;
}

static bool aswl_resolve_style_gradient_rec(struct aswl_style *styles,
                                           size_t style_count,
                                           const struct aswl_style *st,
                                           const char **stack,
                                           size_t stack_len,
                                           const struct aswl_style **grad_style_out)
{
	if (grad_style_out != NULL)
		*grad_style_out = NULL;
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

	if (st->back_grad_type != 0 && st->back_grad_count >= 2 && st->back_grad_colors != NULL && st->back_grad_offsets != NULL) {
		if (grad_style_out != NULL)
			*grad_style_out = st;
		return true;
	}

	for (size_t i = 0; i < st->inherit_count; i++) {
		struct aswl_style *parent = aswl_find_style(styles, style_count, st->inherits[i]);
		if (parent == NULL)
			continue;
		const struct aswl_style *found = NULL;
		if (aswl_resolve_style_gradient_rec(styles, style_count, parent, next_stack, stack_len + 1, &found)) {
			if (found != NULL) {
				if (grad_style_out != NULL)
					*grad_style_out = found;
				return true;
			}
		}
	}

	return false;
}

static bool aswl_resolve_style_gradient(struct aswl_style *styles,
                                       size_t style_count,
                                       const char *style_name,
                                       const struct aswl_color_entry *colors,
                                       size_t color_count,
                                       struct aswl_gradient *grad_out)
{
	if (grad_out != NULL)
		aswl_gradient_destroy(grad_out);
	if (style_name == NULL || style_name[0] == '\0')
		return false;

	struct aswl_style *st = aswl_find_style(styles, style_count, style_name);
	if (st == NULL)
		return false;

	const char *stack[16] = { 0 };
	const struct aswl_style *src = NULL;
	if (!aswl_resolve_style_gradient_rec(styles, style_count, st, stack, 0, &src))
		return false;
	if (src == NULL)
		return false;

	struct aswl_gradient g = { 0 };
	g.type = src->back_grad_type;
	g.count = src->back_grad_count;
	g.colors = calloc(g.count, sizeof(*g.colors));
	g.offsets = calloc(g.count, sizeof(*g.offsets));
	if (g.colors == NULL || g.offsets == NULL) {
		aswl_gradient_destroy(&g);
		return false;
	}

	for (size_t i = 0; i < g.count; i++) {
		uint32_t c = 0;
		if (!aswl_parse_color_token(src->back_grad_colors[i], colors, color_count, &c)) {
			aswl_gradient_destroy(&g);
			return false;
		}
		g.colors[i] = c;
		g.offsets[i] = src->back_grad_offsets[i];
		if (g.offsets[i] < 0.0)
			g.offsets[i] = 0.0;
		if (g.offsets[i] > 1.0)
			g.offsets[i] = 1.0;
		if (i > 0 && g.offsets[i] < g.offsets[i - 1])
			g.offsets[i] = g.offsets[i - 1];
	}

	/* Match AfterStep's documented behavior. */
	g.offsets[0] = 0.0;
	g.offsets[g.count - 1] = 1.0;

	if (grad_out != NULL)
		*grad_out = g;
	else
		aswl_gradient_destroy(&g);

	return true;
}

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

	free(theme->panel_font);
	theme->panel_font = NULL;
	free(theme->menu_font);
	theme->menu_font = NULL;
	free(theme->menu_title_font);
	theme->menu_title_font = NULL;
	free(theme->menu_hilite_font);
	theme->menu_hilite_font = NULL;
	free(theme->frame_font);
	theme->frame_font = NULL;
	free(theme->frame_inactive_font);
	theme->frame_inactive_font = NULL;

	aswl_gradient_destroy(&theme->panel_bg_gradient);
	aswl_gradient_destroy(&theme->panel_button_gradient);
	aswl_gradient_destroy(&theme->panel_ws_inactive_gradient);
	aswl_gradient_destroy(&theme->panel_ws_active_gradient);
	aswl_gradient_destroy(&theme->desk_gradient);
	aswl_gradient_destroy(&theme->frame_active_gradient);
	aswl_gradient_destroy(&theme->frame_inactive_gradient);
	aswl_gradient_destroy(&theme->menu_header_gradient);
	aswl_gradient_destroy(&theme->menu_item_gradient);
	aswl_gradient_destroy(&theme->menu_item_sel_gradient);

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
	if (aswl_gradient_is_valid(&theme->menu_header_gradient) || aswl_gradient_is_valid(&theme->menu_item_gradient) ||
	    aswl_gradient_is_valid(&theme->menu_item_sel_gradient))
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
	if (cfg.menu_hilite_style != NULL && aswl_resolve_style_font(styles, style_count, cfg.menu_hilite_style, &font)) {
		theme->menu_hilite_font = font;
		applied = true;
		font = NULL;
	}
	free(font);

	/* Borders and footer color are derived for now. */
	theme->panel_border = aswl_color_darken(theme->panel_bg, 170);
	theme->menu_border = aswl_color_darken(theme->menu_bg, 170);
	theme->frame_border = aswl_color_darken(theme->frame_inactive_bg, 170);
	theme->menu_footer_fg = aswl_color_nudge(theme->menu_item_fg, 90);

	aswl_free_colors(colors, color_count);
	aswl_free_styles(styles, style_count);
	aswl_free_look_directives(&look_dirs);
	aswl_theme_cfg_free(&cfg);
	return applied;
}
