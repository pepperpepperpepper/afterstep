#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlbg_internal.h"

#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <X11/Xlib.h>

#include "afterimage.h"

static unsigned aswl_env_uint_clamped(const char *name, unsigned def, unsigned max_inclusive)
{
	const char *v = getenv(name);
	if (v == NULL || v[0] == '\0')
		return def;

	errno = 0;
	char *end = NULL;
	long parsed = strtol(v, &end, 10);
	if (errno != 0 || end == v || (end != NULL && *end != '\0'))
		return def;

	if (parsed < 0)
		return 0;

	unsigned long u = (unsigned long)parsed;
	if (u > max_inclusive)
		return max_inclusive;

	return (unsigned)u;
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

static char *aswl_expand_tilde(const char *path)
{
	if (path == NULL)
		return NULL;

	if (path[0] != '~')
		return strdup(path);

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		return strdup(path);

	if (path[1] == '\0')
		return strdup(home);
	if (path[1] != '/')
		return strdup(path);

	size_t home_len = strlen(home);
	size_t rest_len = strlen(path + 1);
	char *out = malloc(home_len + rest_len + 1);
	if (out == NULL)
		return NULL;

	memcpy(out, home, home_len);
	memcpy(out + home_len, path + 1, rest_len + 1);
	return out;
}

static char *aswl_dirname_dup(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return strdup(".");

	const char *slash = strrchr(path, '/');
	if (slash == NULL)
		return strdup(".");
	if (slash == path)
		return strdup("/");

	size_t len = (size_t)(slash - path);
	char *out = malloc(len + 1);
	if (out == NULL)
		return NULL;
	memcpy(out, path, len);
	out[len] = '\0';
	return out;
}

static char *aswl_guess_share_root(const char *background_path)
{
	if (background_path == NULL)
		return NULL;

	const char *p = strstr(background_path, "/share/afterstep/");
	if (p != NULL) {
		size_t len = (size_t)(p - background_path) + strlen("/share/afterstep");
		char *out = malloc(len + 1);
		if (out == NULL)
			return NULL;
		memcpy(out, background_path, len);
		out[len] = '\0';
		return out;
	}

	if (strncmp(background_path, "/usr/share/afterstep/", strlen("/usr/share/afterstep/")) == 0)
		return strdup("/usr/share/afterstep");

	if (strncmp(background_path, "afterstep/", strlen("afterstep/")) == 0)
		return strdup("afterstep");

	return NULL;
}

static const char *aswl_default_background_xml(void)
{
	static const char *candidates[] = {
		/* User-installed default background (preferred). */
		"~/.afterstep/non-configurable/0_background",
		"/usr/share/afterstep/non-configurable/0_background",

		/* Common repo local installs (dev). */
		"_install/share/afterstep/non-configurable/0_background",

		/* Repo fallback. */
		"afterstep/backgrounds/xml/Default",
	};

	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		char *expanded = aswl_expand_tilde(candidates[i]);
		if (expanded == NULL)
			continue;
		bool ok = aswl_is_file_readable(expanded);
		free(expanded);
		if (ok)
			return candidates[i];
	}

	return NULL;
}

static const char *aswl_default_colorscheme(void)
{
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
		free(expanded);
		if (ok)
			return candidates[i];
	}

	return NULL;
}

struct aswl_color_entry {
	char *name;
	uint32_t argb;
};

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

static bool aswl_load_colorscheme_file(const char *path, struct aswl_color_entry **colors_out, size_t *count_out)
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
		char *s = line;
		while (*s != '\0' && isspace((unsigned char)*s))
			s++;
		if (*s == '\0')
			continue;

		static const char *disabled_prefix = "#~~DISABLED~~#";
		if (strncmp(s, disabled_prefix, strlen(disabled_prefix)) == 0) {
			s += strlen(disabled_prefix);
			while (*s != '\0' && isspace((unsigned char)*s))
				s++;
		} else if (*s == '#') {
			continue;
		}

		char *name = s;
		while (*s != '\0' && !isspace((unsigned char)*s))
			s++;
		if (*s == '\0')
			continue;
		*s++ = '\0';

		while (*s != '\0' && isspace((unsigned char)*s))
			s++;
		if (*s == '\0')
			continue;

		char *value = s;
		while (*s != '\0' && !isspace((unsigned char)*s))
			s++;
		*s = '\0';

		uint32_t argb = 0;
		if (!aswl_parse_hex_color(value, &argb))
			continue;

		if (count == cap) {
			size_t next = cap == 0 ? 32 : cap * 2;
			struct aswl_color_entry *tmp = realloc(colors, next * sizeof(*colors));
			if (tmp == NULL)
				goto fail;
			colors = tmp;
			cap = next;
		}

		colors[count].name = strdup(name);
		colors[count].argb = argb;
		if (colors[count].name == NULL)
			goto fail;
		count++;
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

static char *aswl_str_replace_all(const char *in, const char *needle, const char *replacement)
{
	if (in == NULL || needle == NULL || needle[0] == '\0' || replacement == NULL)
		return in == NULL ? NULL : strdup(in);

	size_t in_len = strlen(in);
	size_t needle_len = strlen(needle);
	size_t repl_len = strlen(replacement);

	size_t cap = in_len + 1;
	char *out = malloc(cap);
	if (out == NULL)
		return NULL;

	size_t o = 0;
	for (size_t i = 0; i < in_len;) {
		if (i + needle_len <= in_len && memcmp(in + i, needle, needle_len) == 0) {
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
			i += needle_len;
		} else {
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
	}

	out[o] = '\0';
	return out;
}

static char *aswl_resolve_asset(const char *share_root, const char *src)
{
	if (src == NULL || src[0] == '\0')
		return NULL;

	/* Absolute or already-relative to CWD. */
	if (aswl_is_file_readable(src))
		return strdup(src);

	char *p = NULL;
	bool ok = false;

	if (share_root != NULL && share_root[0] != '\0') {
		/* backgrounds/.StormySkies (installed) */
		if (asprintf(&p, "%s/backgrounds/%s", share_root, src) >= 0) {
			ok = aswl_is_file_readable(p);
			if (ok)
				return p;
			free(p);
		}

		/* backgrounds/jpg/.StormySkies (repo) */
		if (asprintf(&p, "%s/backgrounds/jpg/%s", share_root, src) >= 0) {
			ok = aswl_is_file_readable(p);
			if (ok)
				return p;
			free(p);
		}
	}

	/* Repo fallbacks. */
	if (asprintf(&p, "afterstep/backgrounds/jpg/%s", src) >= 0) {
		ok = aswl_is_file_readable(p);
		if (ok)
			return p;
		free(p);
	}
	if (asprintf(&p, "_install/share/afterstep/backgrounds/%s", src) >= 0) {
		ok = aswl_is_file_readable(p);
		if (ok)
			return p;
		free(p);
	}
	if (asprintf(&p, "/usr/share/afterstep/backgrounds/%s", src) >= 0) {
		ok = aswl_is_file_readable(p);
		if (ok)
			return p;
		free(p);
	}

	return strdup(src);
}

static char *aswl_resolve_tiles_asset(const char *share_root, const char *src)
{
	if (src == NULL || src[0] == '\0')
		return NULL;

	if (aswl_is_file_readable(src))
		return strdup(src);

	if (strncmp(src, "tiles/", 6) != 0)
		return aswl_resolve_asset(share_root, src);

	const char *leaf = src + 6;

	char *p = NULL;
	bool ok = false;

	if (share_root != NULL && share_root[0] != '\0') {
		/* installed: desktop/tiles/SimpleTexture */
		if (asprintf(&p, "%s/desktop/tiles/%s", share_root, leaf) >= 0) {
			ok = aswl_is_file_readable(p);
			if (ok)
				return p;
			free(p);
		}

		/* repo-style: desktop/tiles/png/SimpleTexture */
		if (asprintf(&p, "%s/desktop/tiles/png/%s", share_root, leaf) >= 0) {
			ok = aswl_is_file_readable(p);
			if (ok)
				return p;
			free(p);
		}
		if (asprintf(&p, "%s/desktop/tiles/jpg/%s", share_root, leaf) >= 0) {
			ok = aswl_is_file_readable(p);
			if (ok)
				return p;
			free(p);
		}
	}

	if (asprintf(&p, "afterstep/desktop/tiles/png/%s", leaf) >= 0) {
		ok = aswl_is_file_readable(p);
		if (ok)
			return p;
		free(p);
	}
	if (asprintf(&p, "_install/share/afterstep/desktop/tiles/%s", leaf) >= 0) {
		ok = aswl_is_file_readable(p);
		if (ok)
			return p;
		free(p);
	}
	if (asprintf(&p, "/usr/share/afterstep/desktop/tiles/%s", leaf) >= 0) {
		ok = aswl_is_file_readable(p);
		if (ok)
			return p;
		free(p);
	}

	return strdup(src);
}

static char *aswl_read_file(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return NULL;
	if (!aswl_is_file_readable(path))
		return NULL;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return NULL;

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return NULL;
	}
	long len = ftell(fp);
	if (len < 0) {
		fclose(fp);
		return NULL;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return NULL;
	}

	char *buf = malloc((size_t)len + 1);
	if (buf == NULL) {
		fclose(fp);
		return NULL;
	}

	size_t got = fread(buf, 1, (size_t)len, fp);
	fclose(fp);
	buf[got] = '\0';
	return buf;
}

static void aswl_afterimage_init(struct as_state *state)
{
	if (state == NULL)
		return;
	if (state->asv != NULL)
		return;

	Display *dpy = XOpenDisplay(NULL);
	state->x11_display = dpy;
	if (dpy != NULL) {
		int screen = DefaultScreen(dpy);
		int depth = DefaultDepth(dpy, screen);
		state->asv = create_asvisual(dpy, screen, depth, NULL);
		if (getenv("ASWLBG_DEBUG_SAMPLES") != NULL) {
			fprintf(stderr, "aswlbg: X display=%s depth=%d screen=%d\n",
			        DisplayString(dpy),
			        depth,
			        screen);
		}
	} else {
		/* Match `ascompose`: build an offscreen 32bpp visual when no X display is available. */
		state->asv = create_asvisual(NULL, 0, 32, NULL);
	}
}

bool aswlbg_render_background_afterimage(struct as_state *state, struct as_image *img, int width, int height)
{
	if (state == NULL || img == NULL)
		return false;
	if (width <= 0 || height <= 0)
		return false;

	const bool debug_samples = getenv("ASWLBG_DEBUG_SAMPLES") != NULL;
	size_t alpha_not_ff = 0;
	unsigned alpha_not_ff_min_x = 0, alpha_not_ff_min_y = 0, alpha_not_ff_max_x = 0, alpha_not_ff_max_y = 0;
	bool alpha_not_ff_bbox_set = false;
	unsigned alpha_not_ff_first_x = 0, alpha_not_ff_first_y = 0;
	uint32_t alpha_not_ff_first_a = 0, alpha_not_ff_first_r = 0, alpha_not_ff_first_g = 0,
	         alpha_not_ff_first_b = 0;
	bool alpha_not_ff_first_set = false;
	uint32_t sample_raw_a = 0, sample_raw_r = 0, sample_raw_g = 0, sample_raw_b = 0;
	uint32_t sample_out_a = 0, sample_out_r = 0, sample_out_g = 0, sample_out_b = 0;
	bool sample_set = false;
	const unsigned max_x = width > 0 ? (unsigned)(width - 1) : 0;
	const unsigned max_y = height > 0 ? (unsigned)(height - 1) : 0;
	const unsigned sample_x = aswl_env_uint_clamped("ASWLBG_SAMPLE_X", 800, max_x);
	const unsigned sample_y = aswl_env_uint_clamped("ASWLBG_SAMPLE_Y", 450, max_y);

	const char *bg_env = getenv("ASWLBG_BACKGROUND");
	const char *bg_path_raw = (bg_env != NULL && bg_env[0] != '\0') ? bg_env : aswl_default_background_xml();
	if (bg_path_raw == NULL)
		return false;

	char *bg_path = aswl_expand_tilde(bg_path_raw);
	if (bg_path == NULL)
		return false;

	const char *cs_env = getenv("ASWLBG_COLORSCHEME");
	const char *cs_path_raw = (cs_env != NULL && cs_env[0] != '\0') ? cs_env : aswl_default_colorscheme();

	char *cs_path = NULL;
	if (cs_path_raw != NULL)
		cs_path = aswl_expand_tilde(cs_path_raw);

	char *xml = aswl_read_file(bg_path);
	if (xml == NULL) {
		free(cs_path);
		free(bg_path);
		return false;
	}

	struct aswl_color_entry *colors = NULL;
	size_t color_count = 0;
	if (cs_path != NULL)
		(void)aswl_load_colorscheme_file(cs_path, &colors, &color_count);

	char *share_root = aswl_guess_share_root(bg_path);
	if (share_root == NULL && aswl_is_file_readable("afterstep/backgrounds/xml/Default"))
		share_root = strdup("afterstep");

	char *stormy = aswl_resolve_asset(share_root, ".StormySkies");
	char *tile = aswl_resolve_tiles_asset(share_root, "tiles/SimpleTexture");
	char *fonts_dir = NULL;
	if (share_root != NULL && share_root[0] != '\0') {
		if (asprintf(&fonts_dir, "%s/desktop/fonts", share_root) < 0)
			fonts_dir = NULL;
	}

	char *processed = strdup(xml);
	if (processed == NULL)
		goto fail;

	/* Fix up assets for repo layouts (and for running outside installed share trees). */
	if (stormy != NULL) {
		char *rep = NULL;
		if (asprintf(&rep, "src=\"%s\"", stormy) >= 0) {
			char *tmp = aswl_str_replace_all(processed, "src=\".StormySkies\"", rep);
			free(processed);
			processed = tmp;
			free(rep);
			if (processed == NULL)
				goto fail;
		} else {
			free(rep);
			goto fail;
		}
	}
	if (tile != NULL) {
		char *rep = NULL;
		if (asprintf(&rep, "src=\"%s\"", tile) >= 0) {
			char *tmp = aswl_str_replace_all(processed, "src=\"tiles/SimpleTexture\"", rep);
			free(processed);
			processed = tmp;
			free(rep);
			if (processed == NULL)
				goto fail;
		} else {
			free(rep);
			goto fail;
		}
	}

	/* Replace known colorscheme tokens with ARGB hex so libAfterImage can parse them. */
	static const char *color_names[] = { "BaseLight", "BaseDark", "Inactive1" };
	for (size_t i = 0; i < sizeof(color_names) / sizeof(color_names[0]); i++) {
		uint32_t argb = 0;
		if (!aswl_colors_lookup(colors, color_count, color_names[i], &argb))
			continue;
		char *hex = aswl_format_argb_hex(argb);
		if (hex == NULL)
			continue;
		char *tmp = aswl_str_replace_word_all(processed, color_names[i], hex);
		free(processed);
		processed = tmp;
		free(hex);
		if (processed == NULL)
			goto fail;
	}

	aswl_afterimage_init(state);
	if (state->asv == NULL)
		goto fail;

	struct ASFontManager *fontman = NULL;
	if (fonts_dir != NULL && fonts_dir[0] != '\0')
		fontman = create_generic_fontman(state->x11_display, fonts_dir);

	struct ASImageManager *imman = NULL;
	if (share_root != NULL && share_root[0] != '\0')
		imman = create_generic_imageman(share_root);

	asxml_var_init();
	asxml_var_insert("xroot.width", width);
	asxml_var_insert("xroot.height", height);

	char *bg_dir = aswl_dirname_dup(bg_path);
	if (bg_dir == NULL)
		bg_dir = strdup(".");

	ASImage *im = compose_asimage_xml_at_size(state->asv,
	                                          imman,
	                                          fontman,
	                                          processed,
	                                          ASFLAGS_EVERYTHING,
	                                          0,
	                                          None,
	                                          bg_dir,
	                                          width,
	                                          height);
	if (fontman != NULL)
		destroy_font_manager(fontman, False);
	if (imman != NULL)
		destroy_image_manager(imman, False);
	free(bg_dir);
	if (im == NULL)
		goto fail;

	unsigned int im_width = im->width;
	unsigned int im_height = im->height;

	uint32_t *argb = calloc((size_t)im->width * (size_t)im->height, sizeof(*argb));
	if (argb == NULL) {
		safe_asimage_destroy(im);
		goto fail;
	}

	ASImageDecoder *dec =
	        start_image_decoding(state->asv, im, SCL_DO_ALL, 0, 0, im->width, im->height, NULL);
	if (dec == NULL) {
		free(argb);
		safe_asimage_destroy(im);
		goto fail;
	}

	for (unsigned int y = 0; y < im->height; y++) {
		dec->decode_image_scanline(dec);
		for (unsigned int x = 0; x < im->width; x++) {
			uint32_t a = dec->buffer.alpha[x] & 0xFFu;
			uint32_t r = dec->buffer.red[x] & 0xFFu;
			uint32_t g = dec->buffer.green[x] & 0xFFu;
			uint32_t b = dec->buffer.blue[x] & 0xFFu;
			uint32_t raw_a = a;
			uint32_t raw_r = r;
			uint32_t raw_g = g;
			uint32_t raw_b = b;
			if (debug_samples && x == sample_x && y == sample_y) {
				sample_raw_a = raw_a;
				sample_raw_r = raw_r;
				sample_raw_g = raw_g;
				sample_raw_b = raw_b;
			}

			/*
			 * The root/background image is expected to be opaque (like the X11 root pixmap).
			 * Some libAfterImage pipelines preserve coverage alpha for antialiased text and
			 * similar effects; passing that alpha through makes wlroots blend against black,
			 * producing a visibly darker watermark than the X11 reference.
			 */
			if (a != 0xFFu) {
				alpha_not_ff++;
				if (!alpha_not_ff_first_set) {
					alpha_not_ff_first_set = true;
					alpha_not_ff_first_x = x;
					alpha_not_ff_first_y = y;
					alpha_not_ff_first_a = raw_a;
					alpha_not_ff_first_r = raw_r;
					alpha_not_ff_first_g = raw_g;
					alpha_not_ff_first_b = raw_b;
				}
				if (!alpha_not_ff_bbox_set) {
					alpha_not_ff_min_x = x;
					alpha_not_ff_max_x = x;
					alpha_not_ff_min_y = y;
					alpha_not_ff_max_y = y;
					alpha_not_ff_bbox_set = true;
				} else {
					if (x < alpha_not_ff_min_x)
						alpha_not_ff_min_x = x;
					if (x > alpha_not_ff_max_x)
						alpha_not_ff_max_x = x;
					if (y < alpha_not_ff_min_y)
						alpha_not_ff_min_y = y;
					if (y > alpha_not_ff_max_y)
						alpha_not_ff_max_y = y;
				}
				a = 0xFFu;
			}
			if (debug_samples && x == sample_x && y == sample_y) {
				sample_out_a = a;
				sample_out_r = r;
				sample_out_g = g;
				sample_out_b = b;
				sample_set = true;
			}
			argb[(size_t)y * (size_t)im->width + (size_t)x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	stop_image_decoding(&dec);
	safe_asimage_destroy(im);

	if (debug_samples && sample_set) {
		if (alpha_not_ff_bbox_set) {
			fprintf(stderr,
			        "aswlbg: alpha_not_ff=%zu alpha_bbox=(%u,%u)-(%u,%u) alpha_first(%u,%u)=%02X/%02X%02X%02X sample(%u,%u): raw=%02X/%02X%02X%02X out=%02X/%02X%02X%02X\n",
			        alpha_not_ff,
			        alpha_not_ff_min_x,
			        alpha_not_ff_min_y,
			        alpha_not_ff_max_x,
			        alpha_not_ff_max_y,
			        alpha_not_ff_first_x,
			        alpha_not_ff_first_y,
			        alpha_not_ff_first_a,
			        alpha_not_ff_first_r,
			        alpha_not_ff_first_g,
			        alpha_not_ff_first_b,
			        sample_x,
			        sample_y,
			        sample_raw_a,
			        sample_raw_r,
			        sample_raw_g,
			        sample_raw_b,
			        sample_out_a,
			        sample_out_r,
			        sample_out_g,
			        sample_out_b);
		} else {
			fprintf(stderr,
			        "aswlbg: alpha_not_ff=%zu sample(%u,%u): raw=%02X/%02X%02X%02X out=%02X/%02X%02X%02X\n",
			        alpha_not_ff,
			        sample_x,
			        sample_y,
			        sample_raw_a,
			        sample_raw_r,
			        sample_raw_g,
			        sample_raw_b,
			        sample_out_a,
			        sample_out_r,
			        sample_out_g,
			        sample_out_b);
		}
	}

	aswlbg_image_destroy(img);
	img->argb = argb;
	img->width = (int)im_width;
	img->height = (int)im_height;

	aswl_colors_free(colors, color_count);
	free(processed);
	free(fonts_dir);
	free(tile);
	free(stormy);
	free(share_root);
	free(xml);
	free(cs_path);
	free(bg_path);
	return true;

fail:
	aswl_colors_free(colors, color_count);
	free(processed);
	free(fonts_dir);
	free(tile);
	free(stormy);
	free(share_root);
	free(xml);
	free(cs_path);
	free(bg_path);
	return false;
}

void aswlbg_image_destroy(struct as_image *img)
{
	if (img == NULL)
		return;
	free(img->argb);
	*img = (struct as_image){ 0 };
}

