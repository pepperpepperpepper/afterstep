#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlmenu_internal.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

static char *desktop_exec_sanitize(const char *exec)
{
	if (exec == NULL)
		return NULL;

	size_t n = strlen(exec);
	char *out = malloc(n + 1);
	if (out == NULL)
		return NULL;

	char *d = out;
	bool prev_space = true;
	for (const char *p = exec; *p != '\0'; p++) {
		if (*p == '%') {
			p++;
			if (*p == '\0')
				break;
			if (*p == '%') {
				*d++ = '%';
				prev_space = false;
			}
			continue;
		}

		if (*p == '\t' || *p == '\n' || *p == '\r')
			continue;

		if (*p == ' ') {
			if (prev_space)
				continue;
			*d++ = ' ';
			prev_space = true;
			continue;
		}

		*d++ = *p;
		prev_space = false;
	}

	*d = '\0';
	rstrip(out);
	char *s = lstrip(out);
	if (s != out)
		memmove(out, s, strlen(s) + 1);
	if (out[0] == '\0') {
		free(out);
		return NULL;
	}
	return out;
}

static bool parse_bool(const char *s)
{
	if (s == NULL)
		return false;
	if (strcasecmp(s, "true") == 0 || strcmp(s, "1") == 0 || strcasecmp(s, "yes") == 0)
		return true;
	return false;
}

struct desktop_tmp_entry {
	bool in_entry;
	char *name;
	char *exec;
	char *icon;
	char *type;
	bool hidden;
	bool nodisplay;
};

static void desktop_tmp_finalize(struct as_state *state, const struct desktop_tmp_entry *tmp)
{
	if (state == NULL || tmp == NULL)
		return;
	if (!tmp->in_entry)
		return;
	if (tmp->name == NULL || tmp->exec == NULL)
		return;
	if (tmp->hidden || tmp->nodisplay)
		return;
	if (tmp->type != NULL && tmp->type[0] != '\0' && strcasecmp(tmp->type, "Application") != 0)
		return;
	(void)as_state_append_entry(state, tmp->name, tmp->exec, tmp->icon, false);
}

static void desktop_tmp_reset(struct desktop_tmp_entry *tmp)
{
	if (tmp == NULL)
		return;
	free(tmp->name);
	free(tmp->exec);
	free(tmp->icon);
	free(tmp->type);
	*tmp = (struct desktop_tmp_entry){ 0 };
}

static void as_state_add_desktop_file(struct as_state *state, const char *path)
{
	if (state == NULL || path == NULL || path[0] == '\0')
		return;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return;

	char *line = NULL;
	size_t cap = 0;
	ssize_t len;

	struct desktop_tmp_entry tmp = { 0 };

	while ((len = getline(&line, &cap, fp)) != -1) {
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		char *s = lstrip(line);
		if (*s == '\0' || *s == '#')
			continue;

		if (*s == '[') {
			char *end = strchr(s, ']');
			if (end == NULL)
				continue;
			*end = '\0';

			/* New group: flush previous Desktop Entry group if any. */
			desktop_tmp_finalize(state, &tmp);
			desktop_tmp_reset(&tmp);

			tmp.in_entry = strcmp(s + 1, "Desktop Entry") == 0;
			continue;
		}

		if (!tmp.in_entry)
			continue;

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';
		char *key = s;
		char *val = eq + 1;
		rstrip(key);
		val = lstrip(val);
		rstrip(val);

		if (strcmp(key, "Name") == 0) {
			free(tmp.name);
			tmp.name = strdup(val);
			continue;
		}
		if (strcmp(key, "Exec") == 0) {
			free(tmp.exec);
			tmp.exec = desktop_exec_sanitize(val);
			continue;
		}
		if (strcmp(key, "Type") == 0) {
			free(tmp.type);
			tmp.type = strdup(val);
			continue;
		}
		if (strcmp(key, "Icon") == 0) {
			free(tmp.icon);
			tmp.icon = strdup(val);
			continue;
		}
		if (strcmp(key, "Hidden") == 0) {
			tmp.hidden = parse_bool(val);
			continue;
		}
		if (strcmp(key, "NoDisplay") == 0) {
			tmp.nodisplay = parse_bool(val);
			continue;
		}
	}

	/* Flush last group. */
	desktop_tmp_finalize(state, &tmp);
	desktop_tmp_reset(&tmp);

	free(line);
	fclose(fp);
}

static void as_state_scan_desktop_dir(struct as_state *state, const char *dir_path)
{
	if (state == NULL || dir_path == NULL || dir_path[0] == '\0')
		return;

	DIR *dir = opendir(dir_path);
	if (dir == NULL)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		const char *name = ent->d_name;
		size_t n = strlen(name);
		if (n < 8)
			continue;
		if (strcmp(name + (n - 8), ".desktop") != 0)
			continue;

		char *path = NULL;
		if (asprintf(&path, "%s/%s", dir_path, name) < 0)
			continue;
		as_state_add_desktop_file(state, path);
		free(path);
	}

	closedir(dir);
}

void as_state_add_desktop_entries(struct as_state *state)
{
	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		char *as_home = NULL;
		if (asprintf(&as_home, "%s/.afterstep/applications", home) >= 0) {
			as_state_scan_desktop_dir(state, as_home);
			free(as_home);
		}
	}

	/* Dev convenience: in-tree applications database. */
	as_state_scan_desktop_dir(state, "afterstep/applications");

	/* System AfterStep apps DB (when installed). */
	as_state_scan_desktop_dir(state, "/usr/share/afterstep/applications");

	/* Standard XDG app dirs. */
	const char *xdg_data_home = getenv("XDG_DATA_HOME");
	char *default_data_home = NULL;
	if ((xdg_data_home == NULL || xdg_data_home[0] == '\0') && home != NULL && home[0] != '\0') {
		if (asprintf(&default_data_home, "%s/.local/share", home) >= 0)
			xdg_data_home = default_data_home;
	}
	if (xdg_data_home != NULL && xdg_data_home[0] != '\0') {
		char *appdir = NULL;
		if (asprintf(&appdir, "%s/applications", xdg_data_home) >= 0) {
			as_state_scan_desktop_dir(state, appdir);
			free(appdir);
		}
	}

	const char *xdg_dirs = getenv("XDG_DATA_DIRS");
	if (xdg_dirs == NULL || xdg_dirs[0] == '\0')
		xdg_dirs = "/usr/local/share:/usr/share";

	char *dirs = strdup(xdg_dirs);
	if (dirs != NULL) {
		char *saveptr = NULL;
		for (char *tok = strtok_r(dirs, ":", &saveptr); tok != NULL;
		     tok = strtok_r(NULL, ":", &saveptr)) {
			if (tok[0] == '\0')
				continue;
			char *appdir = NULL;
			if (asprintf(&appdir, "%s/applications", tok) >= 0) {
				as_state_scan_desktop_dir(state, appdir);
				free(appdir);
			}
		}
		free(dirs);
	}

	free(default_data_home);
}
