#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <X11/Xlib.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "afterimage.h"

struct as_image {
	uint32_t *argb;
	int width;
	int height;
};

struct aswl_bg_snapshot_header {
	char magic[8]; /* "ASWLBG1\0" */
	uint32_t width;
	uint32_t height;
	uint32_t stride; /* bytes per row */
	uint32_t format; /* reserved; currently 0 = ARGB8888 */
};

static void as_image_destroy(struct as_image *img);

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

struct as_buffer {
	struct wl_buffer *wl_buffer;
	void *data;
	int width;
	int height;
	int stride;
	size_t size;
	bool busy;
};

struct as_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zwlr_layer_shell_v1 *layer_shell;

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;

	struct wl_callback *frame_cb;

	struct as_buffer *buffer;

	int width;
	int height;
	bool configured;
	bool running;

	struct as_image background;

	Display *x11_display;
	ASVisual *asv;
};

static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms);

static const struct wl_callback_listener frame_listener;

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

static bool aswl_write_full(int fd, const void *data, size_t len)
{
	const uint8_t *p = data;
	size_t left = len;
	while (left > 0) {
		ssize_t n = write(fd, p, left);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (n == 0)
			return false;
		p += (size_t)n;
		left -= (size_t)n;
	}
	return true;
}

static char *aswl_sanitize_filename_segment(const char *s)
{
	if (s == NULL || s[0] == '\0')
		return strdup("wayland");

	size_t len = strlen(s);
	if (len > 200)
		len = 200;

	char *out = malloc(len + 1);
	if (out == NULL)
		return NULL;

	for (size_t i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)s[i];
		if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
			out[i] = (char)ch;
		else
			out[i] = '_';
	}
	out[len] = '\0';
	return out;
}

static char *aswl_bg_snapshot_path(void)
{
	const char *env = getenv("ASWLBG_SNAPSHOT");
	if (env != NULL) {
		if (env[0] == '\0' || strcmp(env, "0") == 0 || strcasecmp(env, "off") == 0 || strcasecmp(env, "false") == 0)
			return NULL;
		return aswl_expand_tilde(env);
	}

	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime == NULL || runtime[0] == '\0')
		runtime = "/tmp";

	char *safe = aswl_sanitize_filename_segment(getenv("WAYLAND_DISPLAY"));
	if (safe == NULL)
		return NULL;

	char *out = NULL;
	if (asprintf(&out, "%s/afterstep.aswlbg.%s.argb", runtime, safe) < 0)
		out = NULL;
	free(safe);
	return out;
}

static void aswl_bg_snapshot_write(const struct as_state *state)
{
	if (state == NULL)
		return;
	if (state->background.argb == NULL || state->background.width <= 0 || state->background.height <= 0)
		return;

	char *path = aswl_bg_snapshot_path();
	if (path == NULL)
		return;

	char *tmp = NULL;
	if (asprintf(&tmp, "%s.tmp.%ld", path, (long)getpid()) < 0)
		tmp = NULL;

	int fd = -1;
	if (tmp != NULL)
		fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) {
		free(tmp);
		free(path);
		return;
	}

	struct aswl_bg_snapshot_header hdr = {
		.magic = { 'A', 'S', 'W', 'L', 'B', 'G', '1', '\0' },
		.width = (uint32_t)state->background.width,
		.height = (uint32_t)state->background.height,
		.stride = (uint32_t)state->background.width * 4u,
		.format = 0,
	};

	size_t pixels_bytes = (size_t)state->background.width * (size_t)state->background.height * 4u;
	bool ok = aswl_write_full(fd, &hdr, sizeof(hdr)) && aswl_write_full(fd, state->background.argb, pixels_bytes);
	(void)fsync(fd);
	close(fd);

	if (ok)
		(void)rename(tmp, path);
	else
		(void)unlink(tmp);

	free(tmp);
	free(path);
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

static bool aswl_render_background_afterimage(struct as_state *state, struct as_image *img, int width, int height)
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

		ASImageDecoder *dec = start_image_decoding(state->asv, im, SCL_DO_ALL, 0, 0, im->width, im->height, NULL);
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

		as_image_destroy(img);
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

static void as_image_destroy(struct as_image *img)
{
	if (img == NULL)
		return;
	free(img->argb);
	*img = (struct as_image){ 0 };
}

static int create_tmpfile(size_t size)
{
	int fd = -1;

#ifdef __linux__
	fd = memfd_create("aswlbg", MFD_CLOEXEC);
	if (fd >= 0) {
		if (ftruncate(fd, (off_t)size) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}
#endif

	char template[] = "/tmp/aswlbg-XXXXXX";
	fd = mkstemp(template);
	if (fd < 0)
		return -1;

	unlink(template);
	if (ftruncate(fd, (off_t)size) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	(void)wl_buffer;
	struct as_buffer *buf = data;
	buf->busy = false;
}

static void as_buffer_destroy(struct as_buffer *buf)
{
	if (buf == NULL)
		return;
	if (buf->wl_buffer != NULL)
		wl_buffer_destroy(buf->wl_buffer);
	if (buf->data != NULL && buf->size > 0)
		munmap(buf->data, buf->size);
	free(buf);
}

static struct as_buffer *as_buffer_create(struct as_state *state, int width, int height)
{
	if (state == NULL || state->shm == NULL)
		return NULL;
	if (width <= 0 || height <= 0)
		return NULL;

	struct as_buffer *buf = calloc(1, sizeof(*buf));
	if (buf == NULL)
		return NULL;

	buf->width = width;
	buf->height = height;
	buf->stride = width * 4;
	buf->size = (size_t)buf->stride * (size_t)height;

	int fd = create_tmpfile(buf->size);
	if (fd < 0) {
		as_buffer_destroy(buf);
		return NULL;
	}

	buf->data = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf->data == MAP_FAILED) {
		close(fd);
		as_buffer_destroy(buf);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, (int)buf->size);
	buf->wl_buffer = wl_shm_pool_create_buffer(pool,
	                                           0,
	                                           width,
	                                           height,
	                                           buf->stride,
	                                           WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	if (buf->wl_buffer == NULL) {
		as_buffer_destroy(buf);
		return NULL;
	}

	static const struct wl_buffer_listener wl_buf_listener = {
		.release = buffer_release,
	};
	wl_buffer_add_listener(buf->wl_buffer, &wl_buf_listener, buf);
	return buf;
}

static void draw_and_commit(struct as_state *state)
{
	if (state == NULL || state->surface == NULL)
		return;
	if (!state->configured)
		return;
	if (state->width <= 0 || state->height <= 0)
		return;

	if (state->buffer == NULL || state->buffer->width != state->width || state->buffer->height != state->height) {
		as_buffer_destroy(state->buffer);
		state->buffer = as_buffer_create(state, state->width, state->height);
	}

	struct as_buffer *buf = state->buffer;
	if (buf == NULL || buf->data == NULL) {
		state->running = false;
		return;
	}
	if (buf->busy)
		return;

	/* Render AfterStep's default background XML (fallback to simple wallpaper if needed). */
	if (state->background.argb == NULL || state->background.width != buf->width || state->background.height != buf->height) {
		if (!aswl_render_background_afterimage(state, &state->background, buf->width, buf->height)) {
			as_image_destroy(&state->background);
		}
	}

	if (state->background.argb != NULL) {
		aswl_bg_snapshot_write(state);
		uint32_t *dst = buf->data;
		int dst_stride_px = buf->stride / 4;
		for (int y = 0; y < buf->height; y++) {
			memcpy(dst + (size_t)y * (size_t)dst_stride_px,
			       state->background.argb + (size_t)y * (size_t)buf->width,
			       (size_t)buf->width * 4);
		}
	} else {
		memset(buf->data, 0, buf->size);
	}

	buf->busy = true;
	wl_surface_attach(state->surface, buf->wl_buffer, 0, 0);
	wl_surface_damage(state->surface, 0, 0, buf->width, buf->height);
	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);
	state->frame_cb = wl_surface_frame(state->surface);
	wl_callback_add_listener(state->frame_cb, &frame_listener, state);
	wl_surface_commit(state->surface);
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms)
{
	(void)time_ms;
	struct as_state *state = data;

	if (cb != NULL)
		wl_callback_destroy(cb);
	if (state->frame_cb == cb)
		state->frame_cb = NULL;

	/* Redraw only when needed (configure triggers draw). */
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial,
                                    uint32_t width,
                                    uint32_t height)
{
	struct as_state *state = data;

	zwlr_layer_surface_v1_ack_configure(surface, serial);
	state->configured = true;

	if ((int)width > 0)
		state->width = (int)width;
	if ((int)height > 0)
		state->height = (int)height;

	draw_and_commit(state);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
	(void)surface;
	struct as_state *state = data;
	state->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
	struct as_state *state = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version > 4 ? 4 : version);
		return;
	}
	if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		return;
	}
	if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		uint32_t bind_version = version > 4 ? 4 : version;
		state->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, bind_version);
		return;
	}
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void cleanup(struct as_state *state)
{
	if (state == NULL)
		return;

	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);

	as_buffer_destroy(state->buffer);
	state->buffer = NULL;

	as_image_destroy(&state->background);

	if (state->asv != NULL) {
		destroy_asvisual(state->asv, false);
		state->asv = NULL;
	}
	if (state->x11_display != NULL) {
		XCloseDisplay(state->x11_display);
		state->x11_display = NULL;
	}

	if (state->layer_surface != NULL)
		zwlr_layer_surface_v1_destroy(state->layer_surface);
	if (state->surface != NULL)
		wl_surface_destroy(state->surface);

	if (state->layer_shell != NULL)
		zwlr_layer_shell_v1_destroy(state->layer_shell);
	if (state->shm != NULL)
		wl_shm_destroy(state->shm);
	if (state->compositor != NULL)
		wl_compositor_destroy(state->compositor);
	if (state->registry != NULL)
		wl_registry_destroy(state->registry);
	if (state->display != NULL)
		wl_display_disconnect(state->display);
}

int main(void)
{
	struct as_state state = {
		.width = 1024,
		.height = 768,
		.running = true,
	};

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "aswlbg: wl_display_connect failed: %s\n", strerror(errno));
		return 1;
	}

	state.registry = wl_display_get_registry(state.display);
	if (state.registry == NULL) {
		fprintf(stderr, "aswlbg: wl_display_get_registry failed\n");
		cleanup(&state);
		return 1;
	}

	wl_registry_add_listener(state.registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);

	if (state.compositor == NULL || state.shm == NULL || state.layer_shell == NULL) {
		fprintf(stderr, "aswlbg: missing globals (compositor=%p shm=%p layer_shell=%p)\n",
		        (void *)state.compositor,
		        (void *)state.shm,
		        (void *)state.layer_shell);
		cleanup(&state);
		return 1;
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	if (state.surface == NULL) {
		fprintf(stderr, "aswlbg: wl_compositor_create_surface failed\n");
		cleanup(&state);
		return 1;
	}

	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell,
	                                                            state.surface,
	                                                            NULL,
	                                                            ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
	                                                            "afterstep-aswlbg");
	if (state.layer_surface == NULL) {
		fprintf(stderr, "aswlbg: zwlr_layer_shell_v1_get_layer_surface failed\n");
		cleanup(&state);
		return 1;
	}

	zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surface_listener, &state);

	zwlr_layer_surface_v1_set_anchor(state.layer_surface,
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
	                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(state.layer_surface, 0, 0);
	zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, 0);

	wl_surface_commit(state.surface);
	wl_display_roundtrip(state.display);

	while (state.running) {
		if (wl_display_dispatch(state.display) < 0)
			break;
	}

	cleanup(&state);
	return 0;
}
