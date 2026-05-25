#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlicon_internal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool aswl_is_file_readable(const char *path)
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

char *aswl_expand_tilde(const char *path)
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

static bool aswl_path_has_suffix_case(const char *path, const char *suffix)
{
	if (path == NULL || suffix == NULL)
		return false;

	size_t plen = strlen(path);
	size_t slen = strlen(suffix);
	if (plen < slen)
		return false;

	const char *p = path + (plen - slen);
	for (size_t i = 0; i < slen; i++) {
		if (tolower((unsigned char)p[i]) != tolower((unsigned char)suffix[i]))
			return false;
	}
	return true;
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

enum aswl_icon_kind aswl_detect_icon_kind(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return ASWL_ICON_KIND_UNKNOWN;

	if (aswl_path_has_suffix_case(path, ".xpm"))
		return ASWL_ICON_KIND_XPM;

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
	if (i + 6 < n && buf[i] == '/' && buf[i + 1] == '*' && buf[i + 2] == ' ' &&
	    (buf[i + 3] == 'X' || buf[i + 3] == 'x') && (buf[i + 4] == 'P' || buf[i + 4] == 'p') &&
	    (buf[i + 5] == 'M' || buf[i + 5] == 'm'))
		return ASWL_ICON_KIND_XPM;

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

void aswl_rstrip_ws(char *s)
{
	if (s == NULL)
		return;
	size_t n = strlen(s);
	while (n > 0 && isspace((unsigned char)s[n - 1]))
		s[--n] = '\0';
}

char *aswl_lstrip_ws(char *s)
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

char *aswl_resolve_icon_spec(const char *spec)
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
