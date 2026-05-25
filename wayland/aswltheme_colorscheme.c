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

bool aswl_theme_is_file_readable(const char *path)
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

char *aswl_trim(char *s)
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

char *aswl_dup_unquoted(const char *s)
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

bool aswl_parse_hex_color(const char *s, uint32_t *argb_out)
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

void aswl_free_colors(struct aswl_color_entry *colors, size_t count)
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

bool aswl_colors_lookup(const struct aswl_color_entry *colors, size_t count, const char *name, uint32_t *argb_out)
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

bool aswl_load_colorscheme(const char *path, struct aswl_color_entry **colors_out, size_t *count_out)
{
	if (colors_out != NULL)
		*colors_out = NULL;
	if (count_out != NULL)
		*count_out = 0;

	if (!aswl_theme_is_file_readable(path))
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
