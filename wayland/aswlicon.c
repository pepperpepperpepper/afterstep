#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlicon.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_LIBPNG
#include <png.h>
#endif

#ifdef HAVE_AFTERIMAGE
#include <X11/Xlib.h>
#include "afterimage.h"
#endif

enum aswl_icon_kind {
	ASWL_ICON_KIND_UNKNOWN = 0,
	ASWL_ICON_KIND_PNG,
	ASWL_ICON_KIND_XML,
};

struct aswl_icon_layer {
	char *src;
	int x;
	int y;
	int w;
	int h;
};

struct aswl_scale_ctx {
	int x;
	int y;
	int w;
	int h;
};

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

static bool aswl_path_has_extension(const char *p)
{
	if (p == NULL)
		return false;
	const char *slash = strrchr(p, '/');
	const char *base = slash != NULL ? slash + 1 : p;
	return strchr(base, '.') != NULL;
}

static bool aswl_try_icon_variants(char **out_path, const char *candidate)
{
	if (out_path != NULL)
		*out_path = NULL;
	if (candidate == NULL || candidate[0] == '\0')
		return false;

	if (aswl_is_file_readable(candidate)) {
		if (out_path != NULL)
			*out_path = strdup(candidate);
		return out_path == NULL || *out_path != NULL;
	}

	if (aswl_path_has_extension(candidate))
		return false;

	static const char *suffixes[] = { ".png", ".xpm" };
	for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
		char *p = NULL;
		if (asprintf(&p, "%s%s", candidate, suffixes[i]) < 0)
			continue;
		if (aswl_is_file_readable(p)) {
			if (out_path != NULL)
				*out_path = p;
			else
				free(p);
			return out_path == NULL || *out_path != NULL;
		}
		free(p);
	}

	return false;
}

static enum aswl_icon_kind aswl_detect_icon_kind(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return ASWL_ICON_KIND_UNKNOWN;

	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return ASWL_ICON_KIND_UNKNOWN;

	unsigned char buf[64];
	size_t n = fread(buf, 1, sizeof(buf), fp);
	fclose(fp);

	if (n >= 8 && memcmp(buf, "\x89PNG\r\n\x1a\n", 8) == 0)
		return ASWL_ICON_KIND_PNG;

	size_t i = 0;
	while (i < n && isspace((unsigned char)buf[i]))
		i++;
	if (i < n && buf[i] == '<')
		return ASWL_ICON_KIND_XML;

	return ASWL_ICON_KIND_UNKNOWN;
}

static bool aswl_try_icon_under_root(char **out_path, const char *root, const char *subdir, const char *name)
{
	if (out_path != NULL)
		*out_path = NULL;
	if (root == NULL || root[0] == '\0' || name == NULL || name[0] == '\0')
		return false;

	char *candidate = NULL;
	if (subdir != NULL && subdir[0] != '\0') {
		if (asprintf(&candidate, "%s/%s/%s", root, subdir, name) < 0)
			return false;
	} else {
		if (asprintf(&candidate, "%s/%s", root, name) < 0)
			return false;
	}

	bool ok = aswl_try_icon_variants(out_path, candidate);
	free(candidate);
	return ok;
}

static void aswl_rstrip_ws(char *s)
{
	if (s == NULL)
		return;
	size_t n = strlen(s);
	while (n > 0 && isspace((unsigned char)s[n - 1]))
		s[--n] = '\0';
}

static char *aswl_lstrip_ws(char *s)
{
	if (s == NULL)
		return NULL;
	while (*s != '\0' && isspace((unsigned char)*s))
		s++;
	return s;
}

static char *aswl_parse_gtk_icon_theme_from_file(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return NULL;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return NULL;

	char *line = NULL;
	size_t cap = 0;
	ssize_t len;
	char *out = NULL;

	while ((len = getline(&line, &cap, fp)) != -1) {
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		char *s = aswl_lstrip_ws(line);
		if (*s == '\0' || *s == '#' || *s == ';' || *s == '[')
			continue;

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';

		char *key = s;
		char *val = eq + 1;
		aswl_rstrip_ws(key);
		val = aswl_lstrip_ws(val);
		aswl_rstrip_ws(val);

		if (strcmp(key, "gtk-icon-theme-name") != 0)
			continue;
		if (val[0] == '\0')
			continue;

		if ((val[0] == '"' && val[strlen(val) - 1] == '"') ||
		    (val[0] == '\'' && val[strlen(val) - 1] == '\'')) {
			val[strlen(val) - 1] = '\0';
			val++;
		}

		out = strdup(val);
		break;
	}

	free(line);
	fclose(fp);
	return out;
}

static char *aswl_get_preferred_xdg_icon_theme(void)
{
	const char *override = getenv("ASWL_ICON_THEME");
	if (override != NULL && override[0] != '\0')
		return strdup(override);

	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");

	char *path = NULL;
	char *out = NULL;

	if (xdg != NULL && xdg[0] != '\0') {
		if (asprintf(&path, "%s/gtk-3.0/settings.ini", xdg) >= 0) {
			out = aswl_parse_gtk_icon_theme_from_file(path);
			free(path);
			path = NULL;
			if (out != NULL)
				return out;
		}
	}

	if (home != NULL && home[0] != '\0') {
		if (asprintf(&path, "%s/.config/gtk-3.0/settings.ini", home) >= 0) {
			out = aswl_parse_gtk_icon_theme_from_file(path);
			free(path);
			path = NULL;
			if (out != NULL)
				return out;
		}
	}

	return NULL;
}

static bool aswl_try_xdg_icon_in_theme(char **out_path, const char *icons_root, const char *theme, const char *name)
{
	if (out_path != NULL)
		*out_path = NULL;
	if (icons_root == NULL || icons_root[0] == '\0' || theme == NULL || theme[0] == '\0')
		return false;
	if (name == NULL || name[0] == '\0')
		return false;

	static const char *sizes[] = {
		"512x512", "256x256", "192x192", "128x128", "96x96", "72x72", "64x64", "48x48", "36x36", "32x32", "24x24", "22x22", "16x16",
		"scalable",
	};
	static const char *contexts[] = {
		"apps", "mimetypes", "places", "categories", "actions", "devices", "status", "emblems",
	};

	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		for (size_t j = 0; j < sizeof(contexts) / sizeof(contexts[0]); j++) {
			char *candidate = NULL;
			if (asprintf(&candidate, "%s/%s/%s/%s/%s", icons_root, theme, sizes[i], contexts[j], name) < 0)
				continue;
			bool ok = aswl_try_icon_variants(out_path, candidate);
			free(candidate);
			if (ok)
				return true;
		}
	}
	return false;
}

static char *aswl_resolve_xdg_icon_name(const char *name)
{
	if (name == NULL || name[0] == '\0')
		return NULL;

	/* XDG icon theme lookups are for simple names ("firefox"), not paths/specs. */
	if (strchr(name, '/') != NULL)
		return NULL;

	char *preferred_theme = aswl_get_preferred_xdg_icon_theme();
	const char *themes[4] = { 0 };
	size_t theme_count = 0;
	if (preferred_theme != NULL && preferred_theme[0] != '\0') {
		themes[theme_count++] = preferred_theme;
	}
	themes[theme_count++] = "hicolor";
	themes[theme_count++] = "Adwaita";

	const char *home = getenv("HOME");
	const char *xdg_data_home = getenv("XDG_DATA_HOME");

	char *default_data_home = NULL;
	if ((xdg_data_home == NULL || xdg_data_home[0] == '\0') && home != NULL && home[0] != '\0') {
		if (asprintf(&default_data_home, "%s/.local/share", home) >= 0)
			xdg_data_home = default_data_home;
	}

	const char *xdg_dirs = getenv("XDG_DATA_DIRS");
	if (xdg_dirs == NULL || xdg_dirs[0] == '\0')
		xdg_dirs = "/usr/local/share:/usr/share";

	/* Search XDG icon roots. */
	const char *roots[64] = { 0 };
	size_t root_count = 0;
	char *roots_owned[64] = { 0 };
	size_t roots_owned_count = 0;

	if (home != NULL && home[0] != '\0' && root_count < sizeof(roots) / sizeof(roots[0])) {
		char *p = NULL;
		if (asprintf(&p, "%s/.icons", home) >= 0) {
			roots[root_count++] = p;
			roots_owned[roots_owned_count++] = p;
		}
	}

	if (xdg_data_home != NULL && xdg_data_home[0] != '\0' && root_count < sizeof(roots) / sizeof(roots[0])) {
		char *p = NULL;
		if (asprintf(&p, "%s/icons", xdg_data_home) >= 0) {
			roots[root_count++] = p;
			roots_owned[roots_owned_count++] = p;
		}
	}

	char *dirs = strdup(xdg_dirs);
	if (dirs != NULL) {
		char *saveptr = NULL;
		for (char *tok = strtok_r(dirs, ":", &saveptr); tok != NULL; tok = strtok_r(NULL, ":", &saveptr)) {
			if (tok[0] == '\0')
				continue;
			if (root_count >= sizeof(roots) / sizeof(roots[0]))
				break;
			char *p = NULL;
			if (asprintf(&p, "%s/icons", tok) >= 0) {
				roots[root_count++] = p;
				roots_owned[roots_owned_count++] = p;
			}
		}
		free(dirs);
	}

	for (size_t r = 0; r < root_count; r++) {
		for (size_t t = 0; t < theme_count; t++) {
			char *found = NULL;
			if (aswl_try_xdg_icon_in_theme(&found, roots[r], themes[t], name)) {
				for (size_t i = 0; i < roots_owned_count; i++)
					free(roots_owned[i]);
				free(default_data_home);
				free(preferred_theme);
				return found;
			}
		}
	}

	/* Fallback: /usr/share/pixmaps (and /usr/local/share/pixmaps). */
	static const char *pixmaps_roots[] = { "/usr/local/share/pixmaps", "/usr/share/pixmaps" };
	for (size_t i = 0; i < sizeof(pixmaps_roots) / sizeof(pixmaps_roots[0]); i++) {
		char *found = NULL;
		if (aswl_try_icon_under_root(&found, pixmaps_roots[i], NULL, name)) {
			for (size_t j = 0; j < roots_owned_count; j++)
				free(roots_owned[j]);
			free(default_data_home);
			free(preferred_theme);
			return found;
		}
	}

	for (size_t i = 0; i < roots_owned_count; i++)
		free(roots_owned[i]);
	free(default_data_home);
	free(preferred_theme);
	return NULL;
}

static char *aswl_resolve_icon_spec(const char *spec)
{
	if (spec == NULL || spec[0] == '\0')
		return NULL;

	/* First, treat it as a path (absolute, relative, or ~). */
	char *expanded = aswl_expand_tilde(spec);
	if (expanded != NULL) {
		char *direct = NULL;
		if (aswl_try_icon_variants(&direct, expanded)) {
			free(expanded);
			return direct;
		}
		free(expanded);
	}

	const char *home = getenv("HOME");
	char *home_icons = NULL;
	char *home_buttons = NULL;
	if (home != NULL && home[0] != '\0') {
		(void)asprintf(&home_icons, "%s/.afterstep/desktop/icons", home);
		(void)asprintf(&home_buttons, "%s/.afterstep/desktop/buttons", home);
	}

	const char *roots[8] = { 0 };
	size_t root_count = 0;
	if (home_icons != NULL)
		roots[root_count++] = home_icons;
	if (home_buttons != NULL)
		roots[root_count++] = home_buttons;
	roots[root_count++] = "_install/share/afterstep/desktop/icons";
	roots[root_count++] = "_install/share/afterstep/desktop/buttons";
	roots[root_count++] = "afterstep/desktop/icons";
	roots[root_count++] = "afterstep/desktop/buttons";
	roots[root_count++] = "/usr/share/afterstep/desktop/icons";
	roots[root_count++] = "/usr/share/afterstep/desktop/buttons";

	static const char *dirs[] = { "mini", "normal", "large", "logos", "dots", "128x128" };

	char *dir_part = NULL;
	const char *name_part = spec;

	const char *slash = strchr(spec, '/');
	if (slash != NULL && slash != spec) {
		dir_part = strndup(spec, (size_t)(slash - spec));
		name_part = slash + 1;
	}

	for (size_t r = 0; r < root_count; r++) {
		const char *root = roots[r];
		char *found = NULL;

		if (aswl_try_icon_under_root(&found, root, NULL, spec))
			goto done;

		/* Common installed layout has composites under root/xml/. */
		if (aswl_try_icon_under_root(&found, root, "xml", spec))
			goto done;

		if (dir_part != NULL && name_part[0] != '\0') {
			char sub_png[64];
			char sub_xml[64];
			(void)snprintf(sub_png, sizeof(sub_png), "%s/png", dir_part);
			(void)snprintf(sub_xml, sizeof(sub_xml), "%s/xml", dir_part);

			if (aswl_try_icon_under_root(&found, root, sub_png, name_part))
				goto done;
			if (aswl_try_icon_under_root(&found, root, sub_xml, name_part))
				goto done;
			if (aswl_try_icon_under_root(&found, root, dir_part, name_part))
				goto done;

			/* Fallback: some configs reference "normal/Foo" but only "large/Foo" exists. */
			for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
				if (strcmp(dirs[i], dir_part) == 0)
					continue;
				char alt_png[64];
				char alt_xml[64];
				(void)snprintf(alt_png, sizeof(alt_png), "%s/png", dirs[i]);
				(void)snprintf(alt_xml, sizeof(alt_xml), "%s/xml", dirs[i]);
				if (aswl_try_icon_under_root(&found, root, alt_png, name_part))
					goto done;
				if (aswl_try_icon_under_root(&found, root, alt_xml, name_part))
					goto done;
				if (aswl_try_icon_under_root(&found, root, dirs[i], name_part))
					goto done;
			}
		} else {
			for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
				char sub_png[64];
				char sub_xml[64];
				(void)snprintf(sub_png, sizeof(sub_png), "%s/png", dirs[i]);
				(void)snprintf(sub_xml, sizeof(sub_xml), "%s/xml", dirs[i]);
				if (aswl_try_icon_under_root(&found, root, sub_png, spec))
					goto done;
				if (aswl_try_icon_under_root(&found, root, sub_xml, spec))
					goto done;
				if (aswl_try_icon_under_root(&found, root, dirs[i], spec))
					goto done;
			}
		}

	done:
		if (found != NULL) {
			free(home_icons);
			free(home_buttons);
			free(dir_part);
			return found;
		}
	}

	free(home_icons);
	free(home_buttons);
	free(dir_part);

	/* Last resort: try XDG icon theme lookup for common Icon= names from .desktop entries. */
	return aswl_resolve_xdg_icon_name(spec);
}

#ifdef HAVE_LIBPNG
static void aswl_png_error(png_structp png, png_const_charp msg)
{
	(void)msg;
	png_longjmp(png, 1);
}

static void aswl_png_warning(png_structp png, png_const_charp msg)
{
	(void)png;
	(void)msg;
}

static bool aswl_load_png_argb(const char *path, uint32_t **out_argb, int *out_w, int *out_h)
{
	if (out_argb != NULL)
		*out_argb = NULL;
	if (out_w != NULL)
		*out_w = 0;
	if (out_h != NULL)
		*out_h = 0;

	if (path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return false;

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, aswl_png_error, aswl_png_warning);
	if (png == NULL) {
		fclose(fp);
		return false;
	}

	png_infop info = png_create_info_struct(png);
	if (info == NULL) {
		png_destroy_read_struct(&png, NULL, NULL);
		fclose(fp);
		return false;
	}

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_read_struct(&png, &info, NULL);
		fclose(fp);
		return false;
	}

	png_init_io(png, fp);
	png_read_info(png, info);

	png_uint_32 w = png_get_image_width(png, info);
	png_uint_32 h = png_get_image_height(png, info);
	int color_type = png_get_color_type(png, info);
	int bit_depth = png_get_bit_depth(png, info);

	if (w == 0 || h == 0) {
		png_destroy_read_struct(&png, &info, NULL);
		fclose(fp);
		return false;
	}
	if (w > 4096 || h > 4096) {
		png_destroy_read_struct(&png, &info, NULL);
		fclose(fp);
		return false;
	}

	if (bit_depth == 16)
		png_set_strip_16(png);
	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png);
	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);
	if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

	png_read_update_info(png, info);

	png_size_t rowbytes = png_get_rowbytes(png, info);
	if (rowbytes == 0 || rowbytes / 4 != w) {
		png_destroy_read_struct(&png, &info, NULL);
		fclose(fp);
		return false;
	}

	uint8_t *rgba = malloc((size_t)rowbytes * (size_t)h);
	png_bytep *rows = malloc(sizeof(*rows) * (size_t)h);
	uint32_t *argb = NULL;
	if (rgba == NULL || rows == NULL)
		goto fail;

	for (png_uint_32 y = 0; y < h; y++)
		rows[y] = (png_bytep)(rgba + (size_t)y * (size_t)rowbytes);

	png_read_image(png, rows);
	png_read_end(png, NULL);

	argb = malloc((size_t)w * (size_t)h * sizeof(*argb));
	if (argb == NULL)
		goto fail;

	for (png_uint_32 y = 0; y < h; y++) {
		const uint8_t *src = rgba + (size_t)y * (size_t)rowbytes;
		for (png_uint_32 x = 0; x < w; x++) {
			uint8_t r = src[x * 4 + 0];
			uint8_t g = src[x * 4 + 1];
			uint8_t b = src[x * 4 + 2];
			uint8_t a = src[x * 4 + 3];
			argb[y * (size_t)w + x] = ((uint32_t)a << 24) |
			                         ((uint32_t)r << 16) |
			                         ((uint32_t)g << 8) |
			                         (uint32_t)b;
		}
	}

	free(rgba);
	free(rows);
	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);

	if (out_argb != NULL)
		*out_argb = argb;
	else
		free(argb);
	if (out_w != NULL)
		*out_w = (int)w;
	if (out_h != NULL)
		*out_h = (int)h;
	return out_argb == NULL || *out_argb != NULL;

fail:
	free(argb);
	free(rgba);
	free(rows);
	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);
	return false;
}
#endif

static bool aswl_read_file(const char *path, char **out, size_t *out_len)
{
	if (out != NULL)
		*out = NULL;
	if (out_len != NULL)
		*out_len = 0;
	if (path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return false;

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return false;
	}
	long n = ftell(fp);
	if (n < 0 || n > 8 * 1024 * 1024) {
		fclose(fp);
		return false;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return false;
	}

	char *buf = malloc((size_t)n + 1);
	if (buf == NULL) {
		fclose(fp);
		return false;
	}
	size_t got = fread(buf, 1, (size_t)n, fp);
	fclose(fp);
	if (got != (size_t)n) {
		free(buf);
		return false;
	}
	buf[n] = '\0';

	if (out != NULL)
		*out = buf;
	else
		free(buf);
	if (out_len != NULL)
		*out_len = (size_t)n;
	return out == NULL || *out != NULL;
}

static bool aswl_xml_attr_strdup(const char *tag, const char *attr, char **out_val)
{
	if (out_val != NULL)
		*out_val = NULL;
	if (tag == NULL || attr == NULL || out_val == NULL)
		return false;

	const char *end = strchr(tag, '>');
	if (end == NULL)
		end = tag + strlen(tag);

	const char *p = tag;
	while (p < end) {
		while (p < end && (isspace((unsigned char)*p) || *p == '<' || *p == '/'))
			p++;
		const char *key = p;
		while (p < end && (isalnum((unsigned char)*p) || *p == '_' || *p == '-'))
			p++;
		size_t keylen = (size_t)(p - key);
		if (keylen == 0) {
			p++;
			continue;
		}

		while (p < end && isspace((unsigned char)*p))
			p++;
		if (p >= end || *p != '=') {
			while (p < end && !isspace((unsigned char)*p) && *p != '>')
				p++;
			continue;
		}

		p++; /* '=' */
		while (p < end && isspace((unsigned char)*p))
			p++;
		if (p >= end)
			break;

		char quote = 0;
		if (*p == '"' || *p == '\'')
			quote = *p++;
		const char *val = p;
		if (quote != 0) {
			while (p < end && *p != quote)
				p++;
		} else {
			while (p < end && !isspace((unsigned char)*p) && *p != '>')
				p++;
		}
		size_t vallen = (size_t)(p - val);
		if (quote != 0 && p < end && *p == quote)
			p++;

		if (strlen(attr) == keylen && strncmp(key, attr, keylen) == 0) {
			*out_val = strndup(val, vallen);
			return *out_val != NULL;
		}
	}

	return false;
}

static bool aswl_xml_attr_int(const char *tag, const char *attr, int *out)
{
	if (out != NULL)
		*out = 0;
	char *s = NULL;
	if (!aswl_xml_attr_strdup(tag, attr, &s))
		return false;

	char *end = NULL;
	errno = 0;
	long v = strtol(s, &end, 10);
	while (end != NULL && isspace((unsigned char)*end))
		end++;
	bool ok = (errno == 0 && end != NULL && end != s && *end == '\0');
	free(s);
	if (!ok)
		return false;
	if (out != NULL)
		*out = (int)v;
	return true;
}

static bool aswl_str_starts_with_tag(const char *p, const char *tag)
{
	if (p == NULL || tag == NULL)
		return false;
	size_t n = strlen(tag);
	if (strncmp(p, tag, n) != 0)
		return false;
	char c = p[n];
	return c == '\0' || c == '>' || c == '/' || isspace((unsigned char)c);
}

static uint32_t aswl_blend_over(uint32_t dst, uint32_t src)
{
	uint32_t sa = (src >> 24) & 0xFFu;
	if (sa == 0)
		return dst;
	if (sa == 255)
		return src;

	uint32_t da = (dst >> 24) & 0xFFu;
	uint32_t inv = 255u - sa;

	uint32_t out_a = sa + (da * inv + 127u) / 255u;
	if (out_a == 0)
		return 0;

	uint32_t sr = (src >> 16) & 0xFFu;
	uint32_t sg = (src >> 8) & 0xFFu;
	uint32_t sb = src & 0xFFu;

	uint32_t dr = (dst >> 16) & 0xFFu;
	uint32_t dg = (dst >> 8) & 0xFFu;
	uint32_t db = dst & 0xFFu;

	uint32_t out_prem_r = sr * sa + ((dr * da) * inv + 127u) / 255u;
	uint32_t out_prem_g = sg * sa + ((dg * da) * inv + 127u) / 255u;
	uint32_t out_prem_b = sb * sa + ((db * da) * inv + 127u) / 255u;

	uint32_t out_r = (out_prem_r + out_a / 2u) / out_a;
	uint32_t out_g = (out_prem_g + out_a / 2u) / out_a;
	uint32_t out_b = (out_prem_b + out_a / 2u) / out_a;

	if (out_r > 255)
		out_r = 255;
	if (out_g > 255)
		out_g = 255;
	if (out_b > 255)
		out_b = 255;

	return (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

static bool aswl_icon_load_argb_rec(const char *spec, int depth, uint32_t **out_argb, int *out_w, int *out_h);

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

static bool aswl_icon_load_xml_afterimage(const char *xml_path, uint32_t **out_argb, int *out_w, int *out_h)
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
#endif

static bool aswl_icon_load_xml(const char *xml_path, int depth, uint32_t **out_argb, int *out_w, int *out_h)
{
	if (out_argb != NULL)
		*out_argb = NULL;
	if (out_w != NULL)
		*out_w = 0;
	if (out_h != NULL)
		*out_h = 0;

	if (xml_path == NULL || xml_path[0] == '\0')
		return false;

	char *text = NULL;
	size_t text_len = 0;
	if (!aswl_read_file(xml_path, &text, &text_len))
		return false;

	const char *p = text;
	while (*p != '\0' && isspace((unsigned char)*p))
		p++;
	if (*p != '<') {
		free(text);
		return false;
	}

	struct aswl_icon_layer *layers = NULL;
	size_t layer_count = 0;
	size_t layer_cap = 0;
	uint32_t **pix = NULL;
	int *sw = NULL;
	int *sh = NULL;
	int *dw = NULL;
	int *dh = NULL;
	uint32_t *canvas = NULL;

	struct aswl_scale_ctx stack[16];
	size_t stack_len = 0;

	const char *cur = text;
	while ((cur = strchr(cur, '<')) != NULL) {
		if (aswl_str_starts_with_tag(cur, "</scale")) {
			if (stack_len > 0)
				stack_len--;
			cur++;
			continue;
		}

		if (aswl_str_starts_with_tag(cur, "<scale")) {
			if (stack_len < sizeof(stack) / sizeof(stack[0])) {
				struct aswl_scale_ctx ctx = { 0 };
				ctx.w = -1;
				ctx.h = -1;
				(void)aswl_xml_attr_int(cur, "x", &ctx.x);
				(void)aswl_xml_attr_int(cur, "y", &ctx.y);
				(void)aswl_xml_attr_int(cur, "width", &ctx.w);
				(void)aswl_xml_attr_int(cur, "height", &ctx.h);
				stack[stack_len++] = ctx;
			}
			cur++;
			continue;
		}

		if (aswl_str_starts_with_tag(cur, "<img")) {
			char *src = NULL;
			if (aswl_xml_attr_strdup(cur, "src", &src) && src != NULL && src[0] != '\0') {
				int x = 0;
				int y = 0;
				int w = -1;
				int h = -1;
				(void)aswl_xml_attr_int(cur, "x", &x);
				(void)aswl_xml_attr_int(cur, "y", &y);
				(void)aswl_xml_attr_int(cur, "width", &w);
				(void)aswl_xml_attr_int(cur, "height", &h);

				for (size_t i = 0; i < stack_len; i++) {
					x += stack[i].x;
					y += stack[i].y;
				}
				if (stack_len > 0) {
					int scale_w = stack[stack_len - 1].w;
					int scale_h = stack[stack_len - 1].h;
					if (w <= 0 && scale_w > 0)
						w = scale_w;
					if (h <= 0 && scale_h > 0)
						h = scale_h;
				}

				if (layer_count == layer_cap) {
					size_t next = layer_cap == 0 ? 8 : layer_cap * 2;
					struct aswl_icon_layer *tmp = realloc(layers, next * sizeof(*layers));
					if (tmp == NULL) {
						free(src);
						goto fail;
					}
					layers = tmp;
					layer_cap = next;
				}

				layers[layer_count++] = (struct aswl_icon_layer){
					.src = src,
					.x = x,
					.y = y,
					.w = w,
					.h = h,
				};
			} else {
				free(src);
			}
			cur++;
			continue;
		}

		cur++;
	}

	if (layer_count == 0)
		goto fail;

	/* Load layers and compute bounds. */
	pix = calloc(layer_count, sizeof(*pix));
	sw = calloc(layer_count, sizeof(*sw));
	sh = calloc(layer_count, sizeof(*sh));
	dw = calloc(layer_count, sizeof(*dw));
	dh = calloc(layer_count, sizeof(*dh));
	if (pix == NULL || sw == NULL || sh == NULL || dw == NULL || dh == NULL)
		goto fail;

	int min_x = 0;
	int min_y = 0;
	int max_x = 0;
	int max_y = 0;
	bool any_loaded = false;

	for (size_t i = 0; i < layer_count; i++) {
		if (!aswl_icon_load_argb_rec(layers[i].src, depth + 1, &pix[i], &sw[i], &sh[i]))
			continue;
		if (sw[i] <= 0 || sh[i] <= 0)
			continue;

		dw[i] = layers[i].w > 0 ? layers[i].w : sw[i];
		dh[i] = layers[i].h > 0 ? layers[i].h : sh[i];
		if (dw[i] <= 0)
			dw[i] = sw[i];
		if (dh[i] <= 0)
			dh[i] = sh[i];
		if (dw[i] <= 0 || dh[i] <= 0)
			continue;

		int lx = layers[i].x;
		int ly = layers[i].y;
		int rx = lx + dw[i];
		int by = ly + dh[i];
		if (!any_loaded) {
			min_x = lx;
			min_y = ly;
			max_x = rx;
			max_y = by;
			any_loaded = true;
		} else {
			if (lx < min_x)
				min_x = lx;
			if (ly < min_y)
				min_y = ly;
			if (rx > max_x)
				max_x = rx;
			if (by > max_y)
				max_y = by;
		}
	}

	if (!any_loaded)
		goto fail;

	int canvas_w = max_x - min_x;
	int canvas_h = max_y - min_y;
	if (canvas_w <= 0 || canvas_h <= 0 || canvas_w > 2048 || canvas_h > 2048)
		goto fail;

	canvas = calloc((size_t)canvas_w * (size_t)canvas_h, sizeof(*canvas));
	if (canvas == NULL)
		goto fail;

	for (size_t i = 0; i < layer_count; i++) {
		if (pix[i] == NULL || sw[i] <= 0 || sh[i] <= 0)
			continue;

		int lx = layers[i].x - min_x;
		int ly = layers[i].y - min_y;

		for (int y = 0; y < dh[i]; y++) {
			int dy = ly + y;
			if (dy < 0 || dy >= canvas_h)
				continue;
			int sy = (int)((int64_t)y * sh[i] / dh[i]);
			if (sy < 0)
				sy = 0;
			if (sy >= sh[i])
				sy = sh[i] - 1;
			for (int x = 0; x < dw[i]; x++) {
				int dx = lx + x;
				if (dx < 0 || dx >= canvas_w)
					continue;
				int sx = (int)((int64_t)x * sw[i] / dw[i]);
				if (sx < 0)
					sx = 0;
				if (sx >= sw[i])
					sx = sw[i] - 1;
				size_t dpos = (size_t)dy * (size_t)canvas_w + (size_t)dx;
				size_t spos = (size_t)sy * (size_t)sw[i] + (size_t)sx;
				canvas[dpos] = aswl_blend_over(canvas[dpos], pix[i][spos]);
			}
		}
	}

	for (size_t i = 0; i < layer_count; i++)
		free(pix[i]);
	free(pix);
	pix = NULL;
	free(sw);
	sw = NULL;
	free(sh);
	sh = NULL;
	free(dw);
	dw = NULL;
	free(dh);
	dh = NULL;

	for (size_t i = 0; i < layer_count; i++)
		free(layers[i].src);
	free(layers);
	free(text);

	if (out_argb != NULL)
		*out_argb = canvas;
	else
		free(canvas);
	canvas = NULL;
	if (out_w != NULL)
		*out_w = canvas_w;
	if (out_h != NULL)
		*out_h = canvas_h;
	return out_argb == NULL || *out_argb != NULL;

fail:
	if (canvas != NULL)
		free(canvas);
	if (pix != NULL) {
		for (size_t i = 0; i < layer_count; i++)
			free(pix[i]);
		free(pix);
	}
	free(sw);
	free(sh);
	free(dw);
	free(dh);
	if (layers != NULL) {
		for (size_t i = 0; i < layer_count; i++)
			free(layers[i].src);
		free(layers);
	}
	free(text);
	return false;
}

static bool aswl_icon_load_argb_rec(const char *spec, int depth, uint32_t **out_argb, int *out_w, int *out_h)
{
	if (out_argb != NULL)
		*out_argb = NULL;
	if (out_w != NULL)
		*out_w = 0;
	if (out_h != NULL)
		*out_h = 0;

	if (spec == NULL || spec[0] == '\0')
		return false;

	if (depth > 8)
		return false;

	char *path = aswl_resolve_icon_spec(spec);
	if (path == NULL)
		return false;

	bool ok = false;
	enum aswl_icon_kind kind = aswl_detect_icon_kind(path);

#ifdef HAVE_LIBPNG
	if (kind == ASWL_ICON_KIND_PNG)
		ok = aswl_load_png_argb(path, out_argb, out_w, out_h);
#endif

	if (!ok)
#ifdef HAVE_AFTERIMAGE
		ok = aswl_icon_load_xml_afterimage(path, out_argb, out_w, out_h);
	if (!ok)
#endif
		ok = aswl_icon_load_xml(path, depth, out_argb, out_w, out_h);

#ifdef HAVE_LIBPNG
	if (!ok)
		ok = aswl_load_png_argb(path, out_argb, out_w, out_h);
#endif

	free(path);
	return ok;
}

bool aswl_icon_load_argb(const char *spec, uint32_t **out_argb, int *out_w, int *out_h)
{
	return aswl_icon_load_argb_rec(spec, 0, out_argb, out_w, out_h);
}
