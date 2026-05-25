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

void aswl_free_styles(struct aswl_style *styles, size_t count)
{
	if (styles == NULL)
		return;
	for (size_t i = 0; i < count; i++) {
		free(styles[i].name);
		free(styles[i].fore);
		free(styles[i].back);
		free(styles[i].font);
		free(styles[i].back_pixmap);
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

void aswl_free_look_directives(struct aswl_look_directives *d)
{
	if (d == NULL)
		return;
	free(d->menu_item_style);
	free(d->menu_hilite_style);
	free(d->menu_title_style);
	free(d->menu_hititle_style);
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

bool aswl_load_look(const char *path,
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

	if (!aswl_theme_is_file_readable(path))
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
			if (strncmp(s, "TextStyle", 9) == 0 && isspace((unsigned char)s[9])) {
				/* TextStyle <0..9> */
				char *args = aswl_trim(s + 9);
				if (args == NULL || args[0] == '\0')
					continue;

				char *endptr = NULL;
				long v = strtol(args, &endptr, 10);
				if (endptr == args)
					continue;
				if (v < 0 || v > 9)
					continue;

				cur->text_style = (int)v;
				continue;
			}
			if (strncmp(s, "BackPixmap", 10) == 0 && isspace((unsigned char)s[10])) {
				/* BackPixmap <type> <pixmap_name|color_name> */
				char *args = aswl_trim(s + 10);
				if (args == NULL || args[0] == '\0')
					continue;

				char *p = args;
				char *tok_type = p;
				while (*p != '\0' && !isspace((unsigned char)*p))
					p++;
				if (*p != '\0')
					*p++ = '\0';

				char *endptr = NULL;
				long type = strtol(tok_type, &endptr, 10);
				if (endptr == tok_type || type < 0 || type > 255)
					continue;

				p = aswl_trim(p);
				char *tok = NULL;
				if (p != NULL && p[0] != '\0')
					tok = aswl_parse_quoted_or_token_dup(p);

				cur->back_pixmap_type = (int)type;
				free(cur->back_pixmap);
				cur->back_pixmap = tok;
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
				styles[count].text_style = -1;
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
			if (strncmp(s, "MenuHiTitleStyle", 16) == 0 && isspace((unsigned char)s[16])) {
				char *v = aswl_parse_quoted_or_token_dup(s + 16);
				if (v != NULL && dirs.menu_hititle_style == NULL)
					dirs.menu_hititle_style = v;
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

bool aswl_resolve_style_color(struct aswl_style *styles,
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

bool aswl_resolve_style_font(struct aswl_style *styles, size_t style_count, const char *style_name, char **font_out)
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

static bool aswl_resolve_style_text_style_rec(struct aswl_style *styles,
                                             size_t style_count,
                                             const struct aswl_style *st,
                                             const char **stack,
                                             size_t stack_len,
                                             int *text_style_out)
{
	if (text_style_out != NULL)
		*text_style_out = -1;
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

	if (st->text_style >= 0) {
		if (text_style_out != NULL)
			*text_style_out = st->text_style;
		return true;
	}

	for (size_t i = 0; i < st->inherit_count; i++) {
		struct aswl_style *parent = aswl_find_style(styles, style_count, st->inherits[i]);
		if (parent == NULL)
			continue;
		int v = -1;
		if (aswl_resolve_style_text_style_rec(styles, style_count, parent, next_stack, stack_len + 1, &v)) {
			if (v >= 0) {
				if (text_style_out != NULL)
					*text_style_out = v;
				return true;
			}
		}
	}

	return false;
}

bool aswl_resolve_style_text_style(struct aswl_style *styles, size_t style_count, const char *style_name, int *text_style_out)
{
	if (text_style_out != NULL)
		*text_style_out = -1;
	if (style_name == NULL || style_name[0] == '\0')
		return false;

	struct aswl_style *st = aswl_find_style(styles, style_count, style_name);
	if (st == NULL)
		return false;

	const char *stack[16] = { 0 };
	int v = -1;
	if (!aswl_resolve_style_text_style_rec(styles, style_count, st, stack, 0, &v))
		return false;
	if (v < 0)
		return false;

	if (text_style_out != NULL)
		*text_style_out = v;
	return true;
}

static bool aswl_resolve_style_back_pixmap_rec(struct aswl_style *styles,
                                               size_t style_count,
                                               const struct aswl_style *st,
                                               const char **stack,
                                               size_t stack_len,
                                               const struct aswl_style **src_out)
{
	if (src_out != NULL)
		*src_out = NULL;
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

	if (st->back_pixmap_type != 0) {
		if (src_out != NULL)
			*src_out = st;
		return true;
	}

	for (size_t i = 0; i < st->inherit_count; i++) {
		struct aswl_style *parent = aswl_find_style(styles, style_count, st->inherits[i]);
		if (parent == NULL)
			continue;
		const struct aswl_style *src = NULL;
		if (aswl_resolve_style_back_pixmap_rec(styles, style_count, parent, next_stack, stack_len + 1, &src)) {
			if (src != NULL) {
				if (src_out != NULL)
					*src_out = src;
				return true;
			}
		}
	}

	return false;
}

bool aswl_resolve_style_back_pixmap_tint(struct aswl_style *styles,
                                         size_t style_count,
                                         const char *style_name,
                                         const struct aswl_color_entry *colors,
                                         size_t color_count,
                                         int *type_out,
                                         uint32_t *tint_out)
{
	if (type_out != NULL)
		*type_out = 0;
	if (tint_out != NULL)
		*tint_out = 0;
	if (style_name == NULL || style_name[0] == '\0')
		return false;

	struct aswl_style *st = aswl_find_style(styles, style_count, style_name);
	if (st == NULL)
		return false;

	const char *stack[16] = { 0 };
	const struct aswl_style *src = NULL;
	if (!aswl_resolve_style_back_pixmap_rec(styles, style_count, st, stack, 0, &src))
		return false;
	if (src == NULL)
		return false;

	if (src->back_pixmap_type != 129 && src->back_pixmap_type != 149)
		return false;

	uint32_t tint = 0x7F7F7F7Fu; /* TINT_LEAVE_SAME */
	uint32_t parsed = 0;
	if (src->back_pixmap != NULL && aswl_parse_color_token(src->back_pixmap, colors, color_count, &parsed)) {
		tint = parsed;
		if (src->back_pixmap_type == 129)
			tint = (tint >> 1) & 0x7F7F7F7Fu; /* match AfterStep's "old style" tint conversion */
	}

	if (type_out != NULL)
		*type_out = src->back_pixmap_type;
	if (tint_out != NULL)
		*tint_out = tint;
	return true;
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

bool aswl_resolve_style_gradient(struct aswl_style *styles,
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
