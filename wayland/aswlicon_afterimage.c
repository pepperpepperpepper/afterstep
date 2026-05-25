#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlicon_internal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_AFTERIMAGE
#include <X11/Xlib.h>
#include "afterimage.h"
#endif

#ifdef HAVE_AFTERIMAGE
struct aswl_afterimage_state {
	bool init_attempted;
	Display *dpy;
	ASVisual *asv;
	ASImageManager *imman;
	ASFontManager *fontman;
	char *icon_root;
	char *alt_root;
	char *colorscheme_path;
	struct aswl_color_entry *colors;
	size_t color_count;
};

static struct aswl_afterimage_state aswl_ai;

struct aswl_color_entry {
	char *name;
	uint32_t argb;
};

static bool aswl_is_dir_readable(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return false;

	struct stat st;
	if (stat(path, &st) != 0)
		return false;
	if (!S_ISDIR(st.st_mode))
		return false;
	return access(path, R_OK) == 0;
}

static char *aswl_afterimage_icon_root_from_path(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return NULL;

	const char *needle = "/desktop/icons";
	const char *hit = strstr(path, needle);
	if (hit == NULL) {
		needle = "/desktop/buttons";
		hit = strstr(path, needle);
	}
	if (hit == NULL)
		return NULL;

	size_t len = (size_t)(hit - path) + strlen(needle);
	return strndup(path, len);
}

static char *aswl_afterimage_fonts_dir_from_icon_root(const char *icon_root)
{
	if (icon_root == NULL)
		return NULL;

	const char *needle = "/desktop/icons";
	const char *hit = strstr(icon_root, needle);
	if (hit == NULL) {
		needle = "/desktop/buttons";
		hit = strstr(icon_root, needle);
	}
	if (hit == NULL)
		return NULL;

	size_t prefix_len = (size_t)(hit - icon_root);
	const char *suffix = "/desktop/fonts";
	char *out = malloc(prefix_len + strlen(suffix) + 1);
	if (out == NULL)
		return NULL;
	memcpy(out, icon_root, prefix_len);
	memcpy(out + prefix_len, suffix, strlen(suffix) + 1);
	return out;
}

static char *aswl_afterimage_alt_root_from_icon_root(const char *icon_root)
{
	if (icon_root == NULL)
		return NULL;

	const char *needle = "/desktop/icons";
	const char *hit = strstr(icon_root, needle);
	const char *alt_suffix = "/desktop/buttons";
	if (hit == NULL) {
		needle = "/desktop/buttons";
		hit = strstr(icon_root, needle);
		alt_suffix = "/desktop/icons";
	}
	if (hit == NULL)
		return NULL;

	size_t prefix_len = (size_t)(hit - icon_root);
	char *out = malloc(prefix_len + strlen(alt_suffix) + 1);
	if (out == NULL)
		return NULL;
	memcpy(out, icon_root, prefix_len);
	memcpy(out + prefix_len, alt_suffix, strlen(alt_suffix) + 1);
	return out;
}

static char *aswl_afterimage_share_root_from_icon_root(const char *icon_root)
{
	if (icon_root == NULL)
		return NULL;

	const char *needle = "/desktop/icons";
	const char *hit = strstr(icon_root, needle);
	if (hit == NULL) {
		needle = "/desktop/buttons";
		hit = strstr(icon_root, needle);
	}
	if (hit == NULL)
		return NULL;

	return strndup(icon_root, (size_t)(hit - icon_root));
}

static void aswl_colors_free(struct aswl_color_entry *colors, size_t count)
{
	if (colors == NULL)
		return;
	for (size_t i = 0; i < count; i++)
		free(colors[i].name);
	free(colors);
}

static bool aswl_parse_hex_color(const char *value, uint32_t *argb_out)
{
	if (argb_out != NULL)
		*argb_out = 0;
	if (value == NULL)
		return false;
	if (value[0] != '#')
		return false;

	unsigned v = 0;
	size_t len = strlen(value + 1);
	if (len == 6) {
		if (sscanf(value + 1, "%x", &v) != 1)
			return false;
		if (argb_out != NULL)
			*argb_out = 0xFF000000u | (uint32_t)v;
		return true;
	}
	if (len == 8) {
		if (sscanf(value + 1, "%x", &v) != 1)
			return false;
		if (argb_out != NULL)
			*argb_out = (uint32_t)v;
		return true;
	}

	return false;
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
		char *s = aswl_lstrip_ws(line);
		if (s == NULL)
			continue;
		aswl_rstrip_ws(s);
		if (s[0] == '\0')
			continue;

		bool disabled = false;
		static const char *disabled_prefix = "#~~DISABLED~~#";
		if (strncmp(s, disabled_prefix, strlen(disabled_prefix)) == 0) {
			s += strlen(disabled_prefix);
			s = aswl_lstrip_ws(s);
			aswl_rstrip_ws(s);
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
		s = aswl_lstrip_ws(s);
		aswl_rstrip_ws(s);
		if (s[0] == '\0')
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
	else
		aswl_colors_free(colors, count);
	if (count_out != NULL)
		*count_out = count;
	return true;

fail:
	free(line);
	fclose(fp);
	aswl_colors_free(colors, count);
	return false;
}

static char *aswl_format_argb_hex(uint32_t argb)
{
	char *out = NULL;
	if (asprintf(&out, "#%08X", argb) < 0)
		return NULL;
	return out;
}

static bool aswl_is_ident_char(char c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-';
}

static char *aswl_str_replace_word_all(const char *in, const char *word, const char *replacement)
{
	if (in == NULL || word == NULL || word[0] == '\0' || replacement == NULL)
		return in == NULL ? NULL : strdup(in);

	size_t in_len = strlen(in);
	size_t word_len = strlen(word);
	size_t repl_len = strlen(replacement);

	size_t cap = in_len + 1;
	char *out = malloc(cap);
	if (out == NULL)
		return NULL;

	size_t o = 0;
	for (size_t i = 0; i < in_len;) {
		bool match = false;
		if (i + word_len <= in_len && memcmp(in + i, word, word_len) == 0) {
			char before = (i == 0) ? '\0' : in[i - 1];
			char after = (i + word_len >= in_len) ? '\0' : in[i + word_len];
			if (!aswl_is_ident_char(before) && !aswl_is_ident_char(after))
				match = true;
		}

		if (match) {
			if (o + repl_len + 1 > cap) {
				size_t next = cap * 2 + repl_len + 16;
				char *tmp = realloc(out, next);
				if (tmp == NULL) {
					free(out);
					return NULL;
				}
				out = tmp;
				cap = next;
			}
			memcpy(out + o, replacement, repl_len);
			o += repl_len;
			i += word_len;
			continue;
		}

		if (o + 2 > cap) {
			size_t next = cap * 2 + 16;
			char *tmp = realloc(out, next);
			if (tmp == NULL) {
				free(out);
				return NULL;
			}
			out = tmp;
			cap = next;
		}
		out[o++] = in[i++];
	}

	out[o] = '\0';
	return out;
}

static char *aswl_afterimage_default_colorscheme(const char *icon_root)
{
	/* Optional override for debugging/experimentation. */
	const char *override = getenv("ASWLICON_COLORSCHEME");
	if (override != NULL && override[0] != '\0') {
		char *expanded = aswl_expand_tilde(override);
		if (expanded != NULL && aswl_is_file_readable(expanded))
			return expanded;
		free(expanded);
	}

	/* Prefer a colorscheme colocated with the icon root (installed share tree or repo root). */
	char *share_root = aswl_afterimage_share_root_from_icon_root(icon_root);
	if (share_root != NULL) {
		char *p = NULL;

		if (asprintf(&p, "%s/non-configurable/0_colorscheme", share_root) >= 0) {
			if (aswl_is_file_readable(p)) {
				free(share_root);
				return p;
			}
			free(p);
		}

		if (asprintf(&p, "%s/colorschemes/colorscheme.Stormy_Skies", share_root) >= 0) {
			if (aswl_is_file_readable(p)) {
				free(share_root);
				return p;
			}
			free(p);
		}

		free(share_root);
	}

	/* Fallbacks: user + system + repo. */
	static const char *candidates[] = {
		"~/.afterstep/non-configurable/0_colorscheme",
		"/usr/share/afterstep/non-configurable/0_colorscheme",
		"_install/share/afterstep/non-configurable/0_colorscheme",
		"afterstep/colorschemes/colorscheme.Stormy_Skies",
	};

	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		char *expanded = aswl_expand_tilde(candidates[i]);
		if (expanded == NULL)
			continue;
		bool ok = aswl_is_file_readable(expanded);
		if (ok)
			return expanded;
		free(expanded);
	}

	return NULL;
}

static void aswl_afterimage_load_colorscheme_if_needed(void)
{
	if (aswl_ai.icon_root == NULL)
		return;

	char *path = aswl_afterimage_default_colorscheme(aswl_ai.icon_root);
	if (path == NULL)
		return;

	if (aswl_ai.colorscheme_path != NULL && strcmp(aswl_ai.colorscheme_path, path) == 0) {
		free(path);
		return;
	}

	aswl_colors_free(aswl_ai.colors, aswl_ai.color_count);
	aswl_ai.colors = NULL;
	aswl_ai.color_count = 0;
	free(aswl_ai.colorscheme_path);
	aswl_ai.colorscheme_path = NULL;

	struct aswl_color_entry *colors = NULL;
	size_t count = 0;
	if (!aswl_load_colorscheme(path, &colors, &count) || count == 0) {
		aswl_colors_free(colors, count);
		free(path);
		return;
	}

	aswl_ai.colorscheme_path = path;
	aswl_ai.colors = colors;
	aswl_ai.color_count = count;
}

static char *aswl_afterimage_apply_colorscheme_tokens(const char *xml)
{
	if (xml == NULL)
		return NULL;
	if (aswl_ai.colors == NULL || aswl_ai.color_count == 0)
		return strdup(xml);

	char *processed = strdup(xml);
	if (processed == NULL)
		return NULL;

	for (size_t i = 0; i < aswl_ai.color_count; i++) {
		if (aswl_ai.colors[i].name == NULL || aswl_ai.colors[i].name[0] == '\0')
			continue;
		char *hex = aswl_format_argb_hex(aswl_ai.colors[i].argb);
		if (hex == NULL)
			continue;
		char *tmp = aswl_str_replace_word_all(processed, aswl_ai.colors[i].name, hex);
		free(processed);
		processed = tmp;
		free(hex);
		if (processed == NULL)
			return NULL;
	}

	return processed;
}

static void aswl_afterimage_reset_managers(void)
{
	if (aswl_ai.imman != NULL)
		set_xml_image_manager(NULL);
	if (aswl_ai.fontman != NULL)
		set_xml_font_manager(NULL);

	if (aswl_ai.imman != NULL) {
		destroy_image_manager(aswl_ai.imman, False);
		aswl_ai.imman = NULL;
	}
	if (aswl_ai.fontman != NULL) {
		destroy_font_manager(aswl_ai.fontman, False);
		aswl_ai.fontman = NULL;
	}
	free(aswl_ai.icon_root);
	aswl_ai.icon_root = NULL;
	free(aswl_ai.alt_root);
	aswl_ai.alt_root = NULL;
	free(aswl_ai.colorscheme_path);
	aswl_ai.colorscheme_path = NULL;
	aswl_colors_free(aswl_ai.colors, aswl_ai.color_count);
	aswl_ai.colors = NULL;
	aswl_ai.color_count = 0;
}

static void aswl_afterimage_seed_menu_folder_pixmap(void)
{
	if (aswl_ai.asv == NULL || aswl_ai.imman == NULL || aswl_ai.icon_root == NULL)
		return;

	if (query_asimage(aswl_ai.imman, "menu.folder_pixmap") != NULL)
		return;

	static const char *candidates[] = {
		"large/FolderAquaBlue", "large/png/FolderAquaBlue",
	};

	const char *src = NULL;
	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		char *p = NULL;
		if (asprintf(&p, "%s/%s", aswl_ai.icon_root, candidates[i]) >= 0 && aswl_is_file_readable(p)) {
			src = candidates[i];
			free(p);
			break;
		}
		free(p);
	}

	if (src == NULL)
		return;

	char *xml = NULL;
	if (asprintf(&xml, "<img id=\"menu.folder_pixmap\" src=\"%s\"/>", src) < 0)
		return;

	ASImage *im = compose_asimage_xml(aswl_ai.asv,
	                                 aswl_ai.imman,
	                                 aswl_ai.fontman,
	                                 xml,
	                                 ASFLAGS_EVERYTHING,
	                                 0,
	                                 None,
	                                 aswl_ai.icon_root);
	free(xml);
	if (im != NULL)
		safe_asimage_destroy(im);
}

static void aswl_afterimage_init(const char *xml_path)
{
	if (!aswl_ai.init_attempted) {
		aswl_ai.init_attempted = true;

		/*
		 * Always use an offscreen visual. Attempting to connect to an X server
		 * (e.g., Xwayland) can block during startup and stall Wayland clients,
		 * which then stalls the compositor when it tries to flush responses.
		 */
		aswl_ai.dpy = NULL;
		aswl_ai.asv = create_asvisual(NULL, 0, 32, NULL);
	}

	if (aswl_ai.asv == NULL)
		return;

	char *icon_root = aswl_afterimage_icon_root_from_path(xml_path);
	if (icon_root == NULL)
		return;

	if (aswl_ai.icon_root != NULL && strcmp(aswl_ai.icon_root, icon_root) == 0) {
		if (aswl_ai.imman != NULL)
			set_xml_image_manager(aswl_ai.imman);
		if (aswl_ai.fontman != NULL)
			set_xml_font_manager(aswl_ai.fontman);
		free(icon_root);
		return;
	}

	aswl_afterimage_reset_managers();
	aswl_ai.icon_root = icon_root;

	aswl_ai.alt_root = aswl_afterimage_alt_root_from_icon_root(aswl_ai.icon_root);
	if (aswl_ai.alt_root != NULL && !aswl_is_dir_readable(aswl_ai.alt_root)) {
		free(aswl_ai.alt_root);
		aswl_ai.alt_root = NULL;
	}

	/* Prefer a deterministic search path (buttons + icons) over ASIMAGE_PATH. */
	if (aswl_ai.alt_root != NULL)
		aswl_ai.imman = create_image_manager(NULL, SCREEN_GAMMA, aswl_ai.icon_root, aswl_ai.alt_root, NULL);
	else
		aswl_ai.imman = create_image_manager(NULL, SCREEN_GAMMA, aswl_ai.icon_root, NULL);
	if (aswl_ai.imman != NULL)
		set_xml_image_manager(aswl_ai.imman);

	aswl_afterimage_load_colorscheme_if_needed();

	char *fonts_dir = aswl_afterimage_fonts_dir_from_icon_root(aswl_ai.icon_root);
	if (fonts_dir != NULL && aswl_is_dir_readable(fonts_dir))
		aswl_ai.fontman = create_generic_fontman(aswl_ai.dpy, fonts_dir);
	free(fonts_dir);
	if (aswl_ai.fontman != NULL)
		set_xml_font_manager(aswl_ai.fontman);
}

bool aswl_icon_load_xml_afterimage(const char *xml_path, uint32_t **out_argb, int *out_w, int *out_h)
{
	if (out_argb != NULL)
		*out_argb = NULL;
	if (out_w != NULL)
		*out_w = 0;
	if (out_h != NULL)
		*out_h = 0;

	if (xml_path == NULL || xml_path[0] == '\0')
		return false;

	aswl_afterimage_init(xml_path);
	if (aswl_ai.asv == NULL || aswl_ai.icon_root == NULL)
		return false;

	char *xml = NULL;
	size_t xml_len = 0;
	if (!aswl_read_file(xml_path, &xml, &xml_len))
		return false;

	char *processed = aswl_afterimage_apply_colorscheme_tokens(xml);
	char *xml_src = processed != NULL ? processed : xml;

	aswl_afterimage_seed_menu_folder_pixmap();

	ASImage *im = compose_asimage_xml(aswl_ai.asv,
	                                 aswl_ai.imman,
	                                 aswl_ai.fontman,
	                                 xml_src,
	                                 ASFLAGS_EVERYTHING,
	                                 0,
	                                 None,
	                                 aswl_ai.icon_root);
	free(processed);
	free(xml);
	if (im == NULL)
		return false;

	const int w = (int)im->width;
	const int h = (int)im->height;

	if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
		safe_asimage_destroy(im);
		return false;
	}

	uint32_t *argb = calloc((size_t)w * (size_t)h, sizeof(*argb));
	if (argb == NULL) {
		safe_asimage_destroy(im);
		return false;
	}

	ASImageDecoder *dec = start_image_decoding(aswl_ai.asv, im, SCL_DO_ALL, 0, 0, im->width, im->height, NULL);
	if (dec == NULL) {
		free(argb);
		safe_asimage_destroy(im);
		return false;
	}

	for (unsigned int y = 0; y < im->height; y++) {
		dec->decode_image_scanline(dec);
		for (unsigned int x = 0; x < im->width; x++) {
			uint32_t a = dec->buffer.alpha[x] & 0xFFu;
			uint32_t r = dec->buffer.red[x] & 0xFFu;
			uint32_t g = dec->buffer.green[x] & 0xFFu;
			uint32_t b = dec->buffer.blue[x] & 0xFFu;
			argb[y * (size_t)w + x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	stop_image_decoding(&dec);
	safe_asimage_destroy(im);

	if (out_argb != NULL)
		*out_argb = argb;
	else
		free(argb);
	if (out_w != NULL)
		*out_w = w;
	if (out_h != NULL)
		*out_h = h;
	return out_argb == NULL || *out_argb != NULL;
}

static const char *aswl_afterimage_relpath(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return path;

	if (aswl_ai.icon_root != NULL) {
		size_t n = strlen(aswl_ai.icon_root);
		if (strncmp(path, aswl_ai.icon_root, n) == 0 && path[n] == '/')
			return path + n + 1;
	}

	if (aswl_ai.alt_root != NULL) {
		size_t n = strlen(aswl_ai.alt_root);
		if (strncmp(path, aswl_ai.alt_root, n) == 0 && path[n] == '/')
			return path + n + 1;
	}

	return path;
}

bool aswl_icon_load_file_afterimage(const char *path, uint32_t **out_argb, int *out_w, int *out_h)
{
	if (out_argb != NULL)
		*out_argb = NULL;
	if (out_w != NULL)
		*out_w = 0;
	if (out_h != NULL)
		*out_h = 0;

	if (path == NULL || path[0] == '\0')
		return false;

	aswl_afterimage_init(path);
	if (aswl_ai.asv == NULL)
		return false;

	ASImage *im = NULL;
	if (aswl_ai.imman != NULL)
		im = get_asimage(aswl_ai.imman, aswl_afterimage_relpath(path), ASFLAGS_EVERYTHING, 0);
	if (im == NULL)
		im = file2ASImage(path, ASFLAGS_EVERYTHING, SCREEN_GAMMA, 0, NULL);
	if (im == NULL)
		return false;

	const int w = (int)im->width;
	const int h = (int)im->height;

	if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
		safe_asimage_destroy(im);
		return false;
	}

	uint32_t *argb = calloc((size_t)w * (size_t)h, sizeof(*argb));
	if (argb == NULL) {
		safe_asimage_destroy(im);
		return false;
	}

	ASImageDecoder *dec = start_image_decoding(aswl_ai.asv, im, SCL_DO_ALL, 0, 0, im->width, im->height, NULL);
	if (dec == NULL) {
		free(argb);
		safe_asimage_destroy(im);
		return false;
	}

	for (unsigned int y = 0; y < im->height; y++) {
		dec->decode_image_scanline(dec);
		for (unsigned int x = 0; x < im->width; x++) {
			uint32_t a = dec->buffer.alpha[x] & 0xFFu;
			uint32_t r = dec->buffer.red[x] & 0xFFu;
			uint32_t g = dec->buffer.green[x] & 0xFFu;
			uint32_t b = dec->buffer.blue[x] & 0xFFu;
			argb[y * (size_t)w + x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	stop_image_decoding(&dec);
	safe_asimage_destroy(im);

	if (out_argb != NULL)
		*out_argb = argb;
	else
		free(argb);
	if (out_w != NULL)
		*out_w = w;
	if (out_h != NULL)
		*out_h = h;
	return out_argb == NULL || *out_argb != NULL;
}
#endif
