#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <wlr/backend.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include "aswlcomp_internal.h"

static bool str_ieq(const char *a, const char *b)
{
	if (a == NULL || b == NULL)
		return false;
	while (*a != '\0' && *b != '\0') {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return false;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static char *lstrip(char *s)
{
	if (s == NULL)
		return NULL;
	while (*s != '\0' && isspace((unsigned char)*s))
		s++;
	return s;
}

static void rstrip_inplace(char *s)
{
	if (s == NULL)
		return;
	size_t len = strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1]))
		s[--len] = '\0';
}

static char *xstrdup_printf(const char *fmt, ...)
{
	if (fmt == NULL)
		return NULL;

	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0)
		return NULL;

	char *buf = malloc((size_t)n + 1);
	if (buf == NULL)
		return NULL;

	va_start(ap, fmt);
	(void)vsnprintf(buf, (size_t)n + 1, fmt, ap);
	va_end(ap);
	return buf;
}

static int aswl_mkdir_p(const char *dir, mode_t mode)
{
	if (dir == NULL || dir[0] == '\0')
		return -1;

	char *path = strdup(dir);
	if (path == NULL)
		return -1;

	for (char *p = path + 1; *p != '\0'; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(path, mode) < 0 && errno != EEXIST) {
			free(path);
			return -1;
		}
		*p = '/';
	}

	if (mkdir(path, mode) < 0 && errno != EEXIST) {
		free(path);
		return -1;
	}

	free(path);
	return 0;
}

static int aswl_ensure_parent_dir(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return -1;

	char *dup = strdup(path);
	if (dup == NULL)
		return -1;

	char *slash = strrchr(dup, '/');
	if (slash == NULL) {
		free(dup);
		return 0;
	}
	if (slash == dup) {
		free(dup);
		return 0;
	}
	*slash = '\0';
	int rc = aswl_mkdir_p(dup, 0700);
	free(dup);
	return rc;
}

static bool parse_u32_strict(const char *s, uint32_t min, uint32_t max, uint32_t *out)
{
	if (out == NULL)
		return false;
	*out = 0;

	if (s == NULL)
		return false;

	char *end = NULL;
	errno = 0;
	unsigned long n = strtoul(s, &end, 10);
	if (errno != 0)
		return false;
	if (end == s || end == NULL || *end != '\0')
		return false;
	if (n < min || n > max)
		return false;

	*out = (uint32_t)n;
	return true;
}

static bool parse_i32_strict(const char *s, int min, int max, int *out)
{
	if (out != NULL)
		*out = 0;
	if (s == NULL)
		return false;

	char *end = NULL;
	errno = 0;
	long n = strtol(s, &end, 10);
	if (errno != 0)
		return false;
	if (end == s || end == NULL || *end != '\0')
		return false;
	if (n < min || n > max)
		return false;
	if (out != NULL)
		*out = (int)n;
	return true;
}

static bool parse_float_strict(const char *s, float min, float max, float *out)
{
	if (out != NULL)
		*out = 0.0f;
	if (s == NULL)
		return false;

	char *end = NULL;
	errno = 0;
	float n = strtof(s, &end);
	if (errno != 0)
		return false;
	if (end == s || end == NULL || *end != '\0')
		return false;
	if (!(n >= min && n <= max))
		return false;
	if (out != NULL)
		*out = n;
	return true;
}

static bool parse_bool_strict(const char *s, bool *out)
{
	if (out != NULL)
		*out = false;
	if (s == NULL || s[0] == '\0')
		return false;

	if (strcmp(s, "1") == 0 || str_ieq(s, "true") || str_ieq(s, "yes") || str_ieq(s, "on")) {
		if (out != NULL)
			*out = true;
		return true;
	}
	if (strcmp(s, "0") == 0 || str_ieq(s, "false") || str_ieq(s, "no") || str_ieq(s, "off")) {
		if (out != NULL)
			*out = false;
		return true;
	}
	return false;
}

static bool parse_output_transform_strict(const char *s, enum wl_output_transform *out)
{
	if (out != NULL)
		*out = WL_OUTPUT_TRANSFORM_NORMAL;
	if (s == NULL)
		return false;

	while (*s != '\0' && isspace((unsigned char)*s))
		s++;

	char token[32];
	size_t n = 0;
	while (*s != '\0' && !isspace((unsigned char)*s) && n + 1 < sizeof(token)) {
		char c = (char)tolower((unsigned char)*s);
		if (c == '_')
			c = '-';
		token[n++] = c;
		s++;
	}
	token[n] = '\0';

	enum wl_output_transform t = WL_OUTPUT_TRANSFORM_NORMAL;
	bool ok = true;

	if (strcmp(token, "normal") == 0 || strcmp(token, "0") == 0) {
		t = WL_OUTPUT_TRANSFORM_NORMAL;
	} else if (strcmp(token, "90") == 0 || strcmp(token, "rot90") == 0 || strcmp(token, "rotate90") == 0) {
		t = WL_OUTPUT_TRANSFORM_90;
	} else if (strcmp(token, "180") == 0 || strcmp(token, "rot180") == 0 || strcmp(token, "rotate180") == 0) {
		t = WL_OUTPUT_TRANSFORM_180;
	} else if (strcmp(token, "270") == 0 || strcmp(token, "rot270") == 0 || strcmp(token, "rotate270") == 0) {
		t = WL_OUTPUT_TRANSFORM_270;
	} else if (strcmp(token, "flipped") == 0 || strcmp(token, "flip") == 0) {
		t = WL_OUTPUT_TRANSFORM_FLIPPED;
	} else if (strcmp(token, "flipped-90") == 0 || strcmp(token, "flip-90") == 0) {
		t = WL_OUTPUT_TRANSFORM_FLIPPED_90;
	} else if (strcmp(token, "flipped-180") == 0 || strcmp(token, "flip-180") == 0) {
		t = WL_OUTPUT_TRANSFORM_FLIPPED_180;
	} else if (strcmp(token, "flipped-270") == 0 || strcmp(token, "flip-270") == 0) {
		t = WL_OUTPUT_TRANSFORM_FLIPPED_270;
	} else {
		ok = false;
	}

	if (ok && out != NULL)
		*out = t;
	return ok;
}

static const char *output_transform_to_string(enum wl_output_transform t)
{
	switch (t) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		return "normal";
	case WL_OUTPUT_TRANSFORM_90:
		return "90";
	case WL_OUTPUT_TRANSFORM_180:
		return "180";
	case WL_OUTPUT_TRANSFORM_270:
		return "270";
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		return "flipped";
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		return "flipped-90";
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		return "flipped-180";
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		return "flipped-270";
	}
	return "normal";
}

static bool parse_output_mode_strict(const char *s, bool *out_preferred, int *out_w, int *out_h, int *out_refresh_mhz)
{
	if (out_preferred != NULL)
		*out_preferred = false;
	if (out_w != NULL)
		*out_w = 0;
	if (out_h != NULL)
		*out_h = 0;
	if (out_refresh_mhz != NULL)
		*out_refresh_mhz = 0;
	if (s == NULL)
		return false;

	if (str_ieq(s, "preferred")) {
		if (out_preferred != NULL)
			*out_preferred = true;
		return true;
	}

	const char *x = strchr(s, 'x');
	if (x == NULL)
		x = strchr(s, 'X');
	if (x == NULL)
		return false;

	char wbuf[32];
	size_t wlen = (size_t)(x - s);
	if (wlen == 0 || wlen >= sizeof(wbuf))
		return false;
	memcpy(wbuf, s, wlen);
	wbuf[wlen] = '\0';

	const char *rest = x + 1;
	const char *at = strchr(rest, '@');

	char hbuf[32];
	size_t hlen = at != NULL ? (size_t)(at - rest) : strlen(rest);
	if (hlen == 0 || hlen >= sizeof(hbuf))
		return false;
	memcpy(hbuf, rest, hlen);
	hbuf[hlen] = '\0';

	int w = 0;
	int h = 0;
	if (!parse_i32_strict(wbuf, 1, 16384, &w))
		return false;
	if (!parse_i32_strict(hbuf, 1, 16384, &h))
		return false;

	int refresh_mhz = 0;
	if (at != NULL) {
		int refresh = 0;
		if (!parse_i32_strict(at + 1, 0, 1000000, &refresh))
			return false;
		if (refresh < 1000)
			refresh_mhz = refresh * 1000;
		else
			refresh_mhz = refresh;
	}

	if (out_w != NULL)
		*out_w = w;
	if (out_h != NULL)
		*out_h = h;
	if (out_refresh_mhz != NULL)
		*out_refresh_mhz = refresh_mhz;
	if (out_preferred != NULL)
		*out_preferred = false;
	return true;
}

static char *aswl_state_path_default(void)
{
	const char *state_home = getenv("XDG_STATE_HOME");
	if (state_home != NULL && state_home[0] != '\0')
		return xstrdup_printf("%s/afterstep/aswlcomp.state", state_home);

	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0')
		return xstrdup_printf("%s/.local/state/afterstep/aswlcomp.state", home);

	return NULL;
}

char *aswl_state_path_resolve(const char *override)
{
	if (override != NULL && override[0] != '\0')
		return strdup(override);

	const char *env = getenv("ASWLCOMP_STATE");
	if (env != NULL && env[0] != '\0')
		return strdup(env);

	return aswl_state_path_default();
}

static struct aswl_output_persist *aswl_output_persist_find(struct aswl_server *server, const char *name)
{
	if (server == NULL || name == NULL || name[0] == '\0')
		return NULL;

	struct aswl_output_persist *po;
	wl_list_for_each(po, &server->outputs_persist, link) {
		if (po == NULL || po->name == NULL)
			continue;
		if (strcmp(po->name, name) == 0)
			return po;
	}
	return NULL;
}

static struct aswl_output_persist *aswl_output_persist_get(struct aswl_server *server, const char *name)
{
	struct aswl_output_persist *po = aswl_output_persist_find(server, name);
	if (po != NULL)
		return po;
	if (server == NULL || name == NULL || name[0] == '\0')
		return NULL;

	po = calloc(1, sizeof(*po));
	if (po == NULL)
		return NULL;
	po->name = strdup(name);
	if (po->name == NULL) {
		free(po);
		return NULL;
	}
	po->scale = 1.0f;
	po->transform = WL_OUTPUT_TRANSFORM_NORMAL;

	/*
	 * Keep insertion order stable: append at the end so state files remain
	 * readable and deterministic as outputs come and go.
	 */
	wl_list_insert(server->outputs_persist.prev, &po->link);
	return po;
}

void aswl_outputs_persist_destroy(struct aswl_server *server)
{
	if (server == NULL)
		return;

	struct aswl_output_persist *po;
	struct aswl_output_persist *tmp;
	wl_list_for_each_safe(po, tmp, &server->outputs_persist, link) {
		wl_list_remove(&po->link);
		free(po->name);
		free(po);
	}
	wl_list_init(&server->outputs_persist);
}

static void aswl_outputs_persist_sync_from_live(struct aswl_server *server)
{
	if (server == NULL)
		return;
	if (server->output_layout == NULL)
		return;

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link) {
		if (out == NULL || out->wlr_output == NULL || out->wlr_output->name[0] == '\0')
			continue;

		struct wlr_output *wo = out->wlr_output;
		struct aswl_output_persist *po = aswl_output_persist_get(server, wo->name);
		if (po == NULL)
			continue;

		po->have_enabled = true;
		po->enabled = wo->enabled;

		struct wlr_output_layout_output *lo = wlr_output_layout_get(server->output_layout, wo);
		if (lo != NULL) {
			po->have_pos = true;
			po->x = lo->x;
			po->y = lo->y;
		}

		po->have_scale = true;
		po->scale = wo->scale > 0.0f ? wo->scale : 1.0f;

		po->have_transform = true;
		po->transform = wo->transform;

		struct wlr_output_mode *preferred = wlr_output_preferred_mode(wo);
		if (wo->current_mode != NULL) {
			po->have_mode = true;
			po->mode_width = wo->current_mode->width;
			po->mode_height = wo->current_mode->height;
			po->mode_refresh_mhz = wo->current_mode->refresh;
			po->mode_preferred = (preferred != NULL && wo->current_mode == preferred);
		} else if (wo->width > 0 && wo->height > 0) {
			po->have_mode = true;
			po->mode_width = wo->width;
			po->mode_height = wo->height;
			po->mode_refresh_mhz = (int)wo->refresh;
			po->mode_preferred = (preferred != NULL &&
			                      preferred->width == wo->width &&
			                      preferred->height == wo->height &&
			                      preferred->refresh == wo->refresh);
		}
	}
}

static struct wlr_output_mode *find_output_mode(struct wlr_output *output, int w, int h, int refresh_mhz)
{
	if (output == NULL || w <= 0 || h <= 0)
		return NULL;

	struct wlr_output_mode *best = NULL;
	int best_delta = 0;

	struct wlr_output_mode *mode;
	wl_list_for_each(mode, &output->modes, link) {
		if (mode == NULL)
			continue;
		if (mode->width != w || mode->height != h)
			continue;

		if (refresh_mhz <= 0)
			return mode;

		int delta = mode->refresh - refresh_mhz;
		if (delta < 0)
			delta = -delta;
		if (best == NULL || delta < best_delta) {
			best = mode;
			best_delta = delta;
		}
	}

	return best;
}

bool aswl_output_persist_apply(struct aswl_server *server,
                               struct wlr_output *output,
                               struct wlr_output_state *state,
                               int *out_x,
                               int *out_y,
                               bool *out_have_pos)
{
	if (out_x != NULL)
		*out_x = 0;
	if (out_y != NULL)
		*out_y = 0;
	if (out_have_pos != NULL)
		*out_have_pos = false;

	if (server == NULL || output == NULL || state == NULL)
		return false;

	struct aswl_output_persist *po = aswl_output_persist_find(server, output->name);
	if (po == NULL)
		return false;

	if (po->have_enabled)
		wlr_output_state_set_enabled(state, po->enabled);
	if (po->have_scale)
		wlr_output_state_set_scale(state, po->scale);
	if (po->have_transform)
		wlr_output_state_set_transform(state, po->transform);

	if (po->have_pos) {
		if (out_x != NULL)
			*out_x = po->x;
		if (out_y != NULL)
			*out_y = po->y;
		if (out_have_pos != NULL)
			*out_have_pos = true;
	}

	struct wlr_output_mode *mode = NULL;
	if (!po->have_mode || po->mode_preferred) {
		mode = wlr_output_preferred_mode(output);
	} else {
		mode = find_output_mode(output, po->mode_width, po->mode_height, po->mode_refresh_mhz);
		if (mode == NULL && wl_list_empty(&output->modes)) {
			wlr_output_state_set_custom_mode(state, po->mode_width, po->mode_height, po->mode_refresh_mhz);
			return true;
		}
		if (mode == NULL)
			mode = wlr_output_preferred_mode(output);
	}

	if (mode != NULL)
		wlr_output_state_set_mode(state, mode);
	return true;
}

void aswl_state_load(struct aswl_server *server, bool allow_workspace_count_override)
{
	if (server == NULL || server->state_path == NULL)
		return;

	FILE *fp = fopen(server->state_path, "r");
	if (fp == NULL)
		return;

	char *line = NULL;
	size_t cap = 0;
	uint32_t loaded_ws = 0;
	uint32_t loaded_count = 0;
	bool have_ws = false;
	bool have_count = false;

	while (getline(&line, &cap, fp) != -1) {
		rstrip_inplace(line);
		char *s = lstrip(line);
		if (s[0] == '\0' || s[0] == '#' || s[0] == ';')
			continue;

		char *scan = s;
		char word[32];
		size_t wn = 0;
		while (*scan != '\0' && !isspace((unsigned char)*scan) && wn + 1 < sizeof(word)) {
			word[wn++] = *scan++;
		}
		word[wn] = '\0';
		if (wn > 0 && str_ieq(word, "output")) {
			while (*scan != '\0' && isspace((unsigned char)*scan))
				scan++;
			char *name = scan;
			if (name[0] == '\0')
				continue;
			while (*scan != '\0' && !isspace((unsigned char)*scan))
				scan++;
			char saved = *scan;
			*scan = '\0';
			struct aswl_output_persist *po = aswl_output_persist_get(server, name);
			*scan = saved;
			if (po == NULL)
				continue;
			while (*scan != '\0' && isspace((unsigned char)*scan))
				scan++;

			char *saveptr = NULL;
			for (char *tok = strtok_r(scan, " \t", &saveptr); tok != NULL; tok = strtok_r(NULL, " \t", &saveptr)) {
				char *eq = strchr(tok, '=');
				if (eq == NULL)
					continue;
				*eq = '\0';
				const char *key = tok;
				const char *val = eq + 1;

				if (str_ieq(key, "enabled")) {
					bool b = false;
					if (parse_bool_strict(val, &b)) {
						po->have_enabled = true;
						po->enabled = b;
					}
					continue;
				}

				if (str_ieq(key, "x")) {
					int v = 0;
					if (parse_i32_strict(val, -100000, 100000, &v)) {
						po->have_pos = true;
						po->x = v;
					}
					continue;
				}

				if (str_ieq(key, "y")) {
					int v = 0;
					if (parse_i32_strict(val, -100000, 100000, &v)) {
						po->have_pos = true;
						po->y = v;
					}
					continue;
				}

				if (str_ieq(key, "scale")) {
					float v = 1.0f;
					if (parse_float_strict(val, 0.1f, 16.0f, &v)) {
						po->have_scale = true;
						po->scale = v;
					}
					continue;
				}

				if (str_ieq(key, "transform")) {
					enum wl_output_transform t = WL_OUTPUT_TRANSFORM_NORMAL;
					if (parse_output_transform_strict(val, &t)) {
						po->have_transform = true;
						po->transform = t;
					}
					continue;
				}

				if (str_ieq(key, "mode")) {
					bool preferred = false;
					int mw = 0;
					int mh = 0;
					int mr = 0;
					if (parse_output_mode_strict(val, &preferred, &mw, &mh, &mr)) {
						po->have_mode = true;
						po->mode_preferred = preferred;
						po->mode_width = mw;
						po->mode_height = mh;
						po->mode_refresh_mhz = mr;
					}
					continue;
				}
			}
			continue;
		}

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';

		char *key = lstrip(s);
		rstrip_inplace(key);

		char *val = lstrip(eq + 1);
		rstrip_inplace(val);

		if (str_ieq(key, "current_workspace")) {
			uint32_t ws = 0;
			if (parse_u32_strict(val, 1, 1000, &ws)) {
				loaded_ws = ws;
				have_ws = true;
			}
			continue;
		}

		if (str_ieq(key, "workspace_count")) {
			uint32_t count = 0;
			if (parse_u32_strict(val, 1, 1000, &count)) {
				loaded_count = count;
				have_count = true;
			}
			continue;
		}
	}

	free(line);
	fclose(fp);

	if (have_count && allow_workspace_count_override)
		server->workspace_count = loaded_count;

	if (have_ws)
		server->current_workspace = loaded_ws;
	server->current_workspace = normalize_workspace(server, server->current_workspace);
}

void aswl_state_save(struct aswl_server *server)
{
	if (server == NULL || server->state_path == NULL)
		return;

	aswl_outputs_persist_sync_from_live(server);

	if (aswl_ensure_parent_dir(server->state_path) != 0) {
		fprintf(stderr, "aswlcomp: state: ensure dir failed: %s\n", server->state_path);
		return;
	}

	char *tmp = xstrdup_printf("%s.XXXXXX", server->state_path);
	if (tmp == NULL)
		return;

	int fd = mkstemp(tmp);
	if (fd < 0) {
		fprintf(stderr, "aswlcomp: state: mkstemp failed: %s: %s\n", tmp, strerror(errno));
		free(tmp);
		return;
	}

	FILE *fp = fdopen(fd, "w");
	if (fp == NULL) {
		(void)close(fd);
		(void)unlink(tmp);
		free(tmp);
		return;
	}

	fprintf(fp, "# aswlcomp state v2\n");
	fprintf(fp, "workspace_count=%u\n", server->workspace_count);
	fprintf(fp, "current_workspace=%u\n", server->current_workspace);

	struct aswl_output_persist *po;
	wl_list_for_each(po, &server->outputs_persist, link) {
		if (po == NULL || po->name == NULL || po->name[0] == '\0')
			continue;

		const bool enabled = po->have_enabled ? po->enabled : true;
		const float scale = po->have_scale ? po->scale : 1.0f;
		const enum wl_output_transform transform = po->have_transform ? po->transform : WL_OUTPUT_TRANSFORM_NORMAL;

		char mode[64];
		const char *mode_s = "preferred";
		if (po->have_mode) {
			if (po->mode_preferred) {
				mode_s = "preferred";
			} else {
				if (po->mode_refresh_mhz > 0)
					(void)snprintf(mode, sizeof(mode), "%dx%d@%d", po->mode_width, po->mode_height, po->mode_refresh_mhz);
				else
					(void)snprintf(mode, sizeof(mode), "%dx%d", po->mode_width, po->mode_height);
				mode_s = mode;
			}
		}

		fprintf(fp,
		        "output %s enabled=%d x=%d y=%d scale=%.3f transform=%s mode=%s\n",
		        po->name,
		        enabled ? 1 : 0,
		        po->have_pos ? po->x : 0,
		        po->have_pos ? po->y : 0,
		        (double)scale,
		        output_transform_to_string(transform),
		        mode_s);
	}
	(void)fflush(fp);

	bool ok = !ferror(fp);
	if (ok)
		ok = fsync(fd) == 0;

	(void)fclose(fp);

	if (!ok) {
		(void)unlink(tmp);
		free(tmp);
		return;
	}

	if (rename(tmp, server->state_path) < 0) {
		fprintf(stderr, "aswlcomp: state: rename failed: %s -> %s: %s\n", tmp, server->state_path, strerror(errno));
		(void)unlink(tmp);
	}

	free(tmp);
}
