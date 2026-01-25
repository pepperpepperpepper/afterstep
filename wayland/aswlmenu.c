#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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

#ifdef HAVE_XKBCOMMON
#include <xkbcommon/xkbcommon.h>
#endif

#include <wayland-client.h>

#include "afterstep-control-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "aswltheme.h"

/* Avoid pulling in linux headers just for BTN_LEFT/KEY_* values. */
#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif

#ifndef KEY_ESC
#define KEY_ESC 1
#endif
#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE 14
#endif
#ifndef KEY_ENTER
#define KEY_ENTER 28
#endif
#ifndef KEY_UP
#define KEY_UP 103
#endif
#ifndef KEY_PAGEUP
#define KEY_PAGEUP 104
#endif
#ifndef KEY_DOWN
#define KEY_DOWN 108
#endif
#ifndef KEY_PAGEDOWN
#define KEY_PAGEDOWN 109
#endif

struct as_menu_entry {
	char *label;
	char *command;
	bool pinned;
};

struct as_buffer {
	struct wl_buffer *wl_buffer;
	void *data;
	int width;
	int height;
	int stride;
	size_t size;
	bool busy;
	struct as_state *state;
};

struct as_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;

	struct xdg_wm_base *xdg_wm_base;
	struct afterstep_control_v1 *control;
	uint32_t control_version;

#ifdef HAVE_XKBCOMMON
	struct xkb_context *xkb_context;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;
#endif

	struct wl_surface *surface;
	struct wl_callback *frame_cb;

	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

	struct as_buffer *buffers[2];

	int width;
	int height;
	bool configured;
	bool running;

	bool needs_redraw;
	int pointer_x;
	int pointer_y;
	bool pointer_in_surface;

	int hover_index;   /* index in filtered list */
	int pressed_index; /* index in filtered list */
	int selected_index; /* index in filtered list */
	int scroll;        /* first visible filtered index */

	char *filter;
	size_t filter_len;
	size_t filter_cap;

	struct as_menu_entry *entries;
	size_t entry_count;
	size_t entry_cap;
	size_t pinned_count;

	size_t *filtered;
	size_t filtered_count;
	size_t filtered_cap;

	bool include_desktop_entries;
	char *menu_config_path;

	struct aswl_theme theme;
};

static void schedule_redraw(struct as_state *state);
static void draw_and_commit(struct as_state *state);
static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms);

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

static int create_tmpfile(size_t size)
{
	int fd = -1;

#ifdef __linux__
	fd = memfd_create("aswlmenu", MFD_CLOEXEC);
	if (fd >= 0) {
		if (ftruncate(fd, (off_t)size) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}
#endif

	char template[] = "/tmp/aswlmenu-XXXXXX";
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

	if (buf->state != NULL && buf->state->needs_redraw && buf->state->frame_cb == NULL)
		draw_and_commit(buf->state);
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
	if (state->shm == NULL)
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

	buf->state = state;
	static const struct wl_buffer_listener wl_buf_listener = {
		.release = buffer_release,
	};
	wl_buffer_add_listener(buf->wl_buffer, &wl_buf_listener, buf);
	return buf;
}

static void as_buffer_paint_solid(struct as_buffer *buf, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	uint32_t *pixels = buf->data;
	size_t count = (size_t)buf->width * (size_t)buf->height;
	for (size_t i = 0; i < count; i++)
		pixels[i] = argb;
}

static void as_buffer_fill_rect(struct as_buffer *buf, int x, int y, int w, int h, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	if (w <= 0 || h <= 0)
		return;

	int x1 = x;
	int y1 = y;
	int x2 = x + w;
	int y2 = y + h;

	if (x1 < 0)
		x1 = 0;
	if (y1 < 0)
		y1 = 0;
	if (x2 > buf->width)
		x2 = buf->width;
	if (y2 > buf->height)
		y2 = buf->height;

	if (x2 <= x1 || y2 <= y1)
		return;

	for (int yy = y1; yy < y2; yy++) {
		uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)yy * (size_t)buf->stride);
		for (int xx = x1; xx < x2; xx++)
			row[xx] = argb;
	}
}

static const uint8_t *as_font5x7_rows(char c)
{
	if (c >= 'a' && c <= 'z')
		c = (char)('A' + (c - 'a'));

	switch (c) {
	case ' ':
	{
		static const uint8_t rows[7] = { 0, 0, 0, 0, 0, 0, 0 };
		return rows;
	}
	case '-':
	{
		static const uint8_t rows[7] = { 0, 0, 0, 0x1F, 0, 0, 0 };
		return rows;
	}
	case '_':
	{
		static const uint8_t rows[7] = { 0, 0, 0, 0, 0, 0, 0x1F };
		return rows;
	}
	case '.':
	{
		static const uint8_t rows[7] = { 0, 0, 0, 0, 0, 0, 0x04 };
		return rows;
	}
	case ':':
	{
		static const uint8_t rows[7] = { 0, 0x04, 0, 0, 0x04, 0, 0 };
		return rows;
	}
	case '+':
	{
		static const uint8_t rows[7] = { 0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0 };
		return rows;
	}
	case '/':
	{
		static const uint8_t rows[7] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0, 0 };
		return rows;
	}
	case '?':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0, 0x04 };
		return rows;
	}
	case '0':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
		return rows;
	}
	case '1':
	{
		static const uint8_t rows[7] = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
		return rows;
	}
	case '2':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
		return rows;
	}
	case '3':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E };
		return rows;
	}
	case '4':
	{
		static const uint8_t rows[7] = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
		return rows;
	}
	case '5':
	{
		static const uint8_t rows[7] = { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
		return rows;
	}
	case '6':
	{
		static const uint8_t rows[7] = { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
		return rows;
	}
	case '7':
	{
		static const uint8_t rows[7] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
		return rows;
	}
	case '8':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
		return rows;
	}
	case '9':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
		return rows;
	}
	case 'A':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
		return rows;
	}
	case 'B':
	{
		static const uint8_t rows[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
		return rows;
	}
	case 'C':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
		return rows;
	}
	case 'D':
	{
		static const uint8_t rows[7] = { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C };
		return rows;
	}
	case 'E':
	{
		static const uint8_t rows[7] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
		return rows;
	}
	case 'F':
	{
		static const uint8_t rows[7] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
		return rows;
	}
	case 'G':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
		return rows;
	}
	case 'H':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
		return rows;
	}
	case 'I':
	{
		static const uint8_t rows[7] = { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
		return rows;
	}
	case 'J':
	{
		static const uint8_t rows[7] = { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C };
		return rows;
	}
	case 'K':
	{
		static const uint8_t rows[7] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
		return rows;
	}
	case 'L':
	{
		static const uint8_t rows[7] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
		return rows;
	}
	case 'M':
	{
		static const uint8_t rows[7] = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
		return rows;
	}
	case 'N':
	{
		static const uint8_t rows[7] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
		return rows;
	}
	case 'O':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
		return rows;
	}
	case 'P':
	{
		static const uint8_t rows[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
		return rows;
	}
	case 'Q':
	{
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
		return rows;
	}
	case 'R':
	{
		static const uint8_t rows[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
		return rows;
	}
	case 'S':
	{
		static const uint8_t rows[7] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
		return rows;
	}
	case 'T':
	{
		static const uint8_t rows[7] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
		return rows;
	}
	case 'U':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
		return rows;
	}
	case 'V':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
		return rows;
	}
	case 'W':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A };
		return rows;
	}
	case 'X':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
		return rows;
	}
	case 'Y':
	{
		static const uint8_t rows[7] = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
		return rows;
	}
	case 'Z':
	{
		static const uint8_t rows[7] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
		return rows;
	}
	case '!':
	{
		static const uint8_t rows[7] = { 0x04, 0x04, 0x04, 0x04, 0x04, 0, 0x04 };
		return rows;
	}
	case '(':
	{
		static const uint8_t rows[7] = { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
		return rows;
	}
	case ')':
	{
		static const uint8_t rows[7] = { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
		return rows;
	}
	case ',':
	{
		static const uint8_t rows[7] = { 0, 0, 0, 0, 0, 0x04, 0x08 };
		return rows;
	}
	case '=':
	{
		static const uint8_t rows[7] = { 0, 0, 0x1F, 0, 0x1F, 0, 0 };
		return rows;
	}
	case '\'':
	{
		static const uint8_t rows[7] = { 0x04, 0x04, 0, 0, 0, 0, 0 };
		return rows;
	}
	case '"':
	{
		static const uint8_t rows[7] = { 0x0A, 0x0A, 0, 0, 0, 0, 0 };
		return rows;
	}
	case '#':
	{
		static const uint8_t rows[7] = { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A };
		return rows;
	}
	case '$':
	{
		static const uint8_t rows[7] = { 0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04 };
		return rows;
	}
	case '%':
	{
		static const uint8_t rows[7] = { 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13 };
		return rows;
	}
	case '&':
	{
		static const uint8_t rows[7] = { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D };
		return rows;
	}
	case '*':
	{
		static const uint8_t rows[7] = { 0, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0 };
		return rows;
	}
	case '<':
	{
		static const uint8_t rows[7] = { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 };
		return rows;
	}
	case '>':
	{
		static const uint8_t rows[7] = { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 };
		return rows;
	}
	case '[':
	{
		static const uint8_t rows[7] = { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E };
		return rows;
	}
	case ']':
	{
		static const uint8_t rows[7] = { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E };
		return rows;
	}
	default:
		return as_font5x7_rows('?');
	}
}

static int as_font5x7_glyph_w(int scale)
{
	return 5 * scale;
}

static int as_font5x7_glyph_h(int scale)
{
	return 7 * scale;
}

static int as_font5x7_spacing(int scale)
{
	return 1 * scale;
}

static int as_font5x7_text_width_n(const char *s, size_t n, int scale)
{
	if (s == NULL || n == 0)
		return 0;

	int glyph_w = as_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);
	int w = 0;

	for (size_t i = 0; i < n && s[i] != '\0'; i++) {
		if (i > 0)
			w += spacing;
		w += glyph_w;
	}
	return w;
}

static void as_buffer_draw_glyph5x7(struct as_buffer *buf, int x, int y, char c, int scale, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	const uint8_t *rows = as_font5x7_rows(c);
	int gw = as_font5x7_glyph_w(scale);
	int gh = as_font5x7_glyph_h(scale);

	for (int yy = 0; yy < gh; yy++) {
		uint8_t mask = rows[yy / scale];
		for (int xx = 0; xx < gw; xx++) {
			int bit = xx / scale;
			if ((mask & (1u << (4 - bit))) == 0)
				continue;
			int px = x + xx;
			int py = y + yy;
			if (px < 0 || py < 0 || px >= buf->width || py >= buf->height)
				continue;
			uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)py * (size_t)buf->stride);
			row[px] = argb;
		}
	}
}

static size_t as_font5x7_fit_chars(const char *s, int scale, int max_w)
{
	if (s == NULL || max_w <= 0)
		return 0;

	int glyph_w = as_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);
	int w = 0;
	size_t i = 0;

	for (; s[i] != '\0'; i++) {
		int add = glyph_w;
		if (i > 0)
			add += spacing;
		if (w + add > max_w)
			break;
		w += add;
	}
	return i;
}

static void as_buffer_draw_text5x7(struct as_buffer *buf, int x, int y, const char *s, int max_w, int scale, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL || s == NULL)
		return;

	size_t n = strlen(s);
	if (n == 0)
		return;

	int full_w = as_font5x7_text_width_n(s, n, scale);
	size_t draw_n = n;

	if (max_w > 0 && full_w > max_w) {
		int ell_w = as_font5x7_text_width_n("...", 3, scale);
		if (max_w > ell_w)
			draw_n = as_font5x7_fit_chars(s, scale, max_w - ell_w);
		else
			draw_n = as_font5x7_fit_chars(s, scale, max_w);
	}

	int glyph_w = as_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);
	int cx = x;

	for (size_t i = 0; i < draw_n; i++) {
		if (i > 0)
			cx += spacing;
		as_buffer_draw_glyph5x7(buf, cx, y, s[i], scale, argb);
		cx += glyph_w;
	}

	if (draw_n < n && max_w > 0) {
		if (cx > x)
			cx += spacing;
		as_buffer_draw_text5x7(buf, cx, y, "...", max_w > 0 ? max_w - (cx - x) : 0, scale, argb);
	}
}

static void spawn_command(const char *command)
{
	if (command == NULL || command[0] == '\0')
		return;

	pid_t pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}
}

static void as_state_launch_command(struct as_state *state, const char *command)
{
	if (command == NULL || command[0] == '\0')
		return;

	if (state != NULL && state->control != NULL) {
		if (command[0] == '@') {
			const char *action = command + 1;
			if (strcmp(action, "quit") == 0 || strcmp(action, "exit") == 0) {
				fprintf(stderr, "aswlmenu: compositor quit\n");
				afterstep_control_v1_quit(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "close") == 0 || strcmp(action, "close_focused") == 0) {
				fprintf(stderr, "aswlmenu: compositor close_focused\n");
				afterstep_control_v1_close_focused(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "focus_next") == 0 || strcmp(action, "next") == 0) {
				fprintf(stderr, "aswlmenu: compositor focus_next\n");
				afterstep_control_v1_focus_next(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if (strcmp(action, "focus_prev") == 0 || strcmp(action, "prev") == 0) {
				fprintf(stderr, "aswlmenu: compositor focus_prev\n");
				afterstep_control_v1_focus_prev(state->control);
				(void)wl_display_flush(state->display);
				return;
			}

			if ((strcmp(action, "workspace_next") == 0 || strcmp(action, "ws_next") == 0 || strcmp(action, "ws+") == 0) &&
			    state->control_version >= 2) {
				fprintf(stderr, "aswlmenu: compositor workspace_next\n");
				afterstep_control_v1_workspace_next(state->control);
				(void)wl_display_flush(state->display);
				return;
			}
			if ((strcmp(action, "workspace_prev") == 0 || strcmp(action, "ws_prev") == 0 || strcmp(action, "ws-") == 0) &&
			    state->control_version >= 2) {
				fprintf(stderr, "aswlmenu: compositor workspace_prev\n");
				afterstep_control_v1_workspace_prev(state->control);
				(void)wl_display_flush(state->display);
				return;
			}

			const char *ws_arg = NULL;
			if (strncmp(action, "workspace", 9) == 0) {
				ws_arg = action + 9;
			} else if (strncmp(action, "ws", 2) == 0) {
				ws_arg = action + 2;
			}
			if (ws_arg != NULL) {
				while (*ws_arg == ':' || *ws_arg == '=' || isspace((unsigned char)*ws_arg))
					ws_arg++;

				if (*ws_arg != '\0' && state->control_version >= 2) {
					char *end = NULL;
					unsigned long ws = strtoul(ws_arg, &end, 10);
					while (end != NULL && isspace((unsigned char)*end))
						end++;

					if (end != ws_arg && end != NULL && *end == '\0' && ws >= 1 && ws <= 1000) {
						fprintf(stderr, "aswlmenu: compositor set_workspace=%lu\n", ws);
						afterstep_control_v1_set_workspace(state->control, (uint32_t)ws);
						(void)wl_display_flush(state->display);
						return;
					}
				}
			}
		}

		fprintf(stderr, "aswlmenu: compositor exec %s\n", command);
		afterstep_control_v1_exec(state->control, command);
		(void)wl_display_flush(state->display);
		return;
	}

	spawn_command(command);
}

static void rstrip(char *s)
{
	if (s == NULL)
		return;
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r'))
		s[--n] = '\0';
}

static char *lstrip(char *s)
{
	if (s == NULL)
		return NULL;
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

static bool str_case_contains(const char *haystack, const char *needle)
{
	if (haystack == NULL || needle == NULL)
		return false;
	if (needle[0] == '\0')
		return true;

	for (size_t i = 0; haystack[i] != '\0'; i++) {
		size_t j = 0;
		while (needle[j] != '\0' && haystack[i + j] != '\0') {
			char a = (char)tolower((unsigned char)haystack[i + j]);
			char b = (char)tolower((unsigned char)needle[j]);
			if (a != b)
				break;
			j++;
		}
		if (needle[j] == '\0')
			return true;
	}
	return false;
}

static void as_state_free_entries(struct as_state *state)
{
	if (state == NULL)
		return;
	for (size_t i = 0; i < state->entry_count; i++) {
		free(state->entries[i].label);
		free(state->entries[i].command);
	}
	free(state->entries);
	state->entries = NULL;
	state->entry_count = 0;
	state->entry_cap = 0;
	state->pinned_count = 0;
}

static bool as_state_append_entry(struct as_state *state, const char *label, const char *command, bool pinned)
{
	if (state == NULL || label == NULL || label[0] == '\0' || command == NULL || command[0] == '\0')
		return false;

	if (state->entry_count == state->entry_cap) {
		size_t next = state->entry_cap == 0 ? 64 : state->entry_cap * 2;
		struct as_menu_entry *tmp = realloc(state->entries, next * sizeof(*tmp));
		if (tmp == NULL)
			return false;
		state->entries = tmp;
		state->entry_cap = next;
	}

	state->entries[state->entry_count] = (struct as_menu_entry){ 0 };
	state->entries[state->entry_count].label = strdup(label);
	state->entries[state->entry_count].command = strdup(command);
	state->entries[state->entry_count].pinned = pinned;
	if (state->entries[state->entry_count].label == NULL || state->entries[state->entry_count].command == NULL) {
		free(state->entries[state->entry_count].label);
		free(state->entries[state->entry_count].command);
		state->entries[state->entry_count] = (struct as_menu_entry){ 0 };
		return false;
	}

	state->entry_count++;
	if (pinned)
		state->pinned_count++;
	return true;
}

static void as_state_free_filtered(struct as_state *state)
{
	if (state == NULL)
		return;
	free(state->filtered);
	state->filtered = NULL;
	state->filtered_count = 0;
	state->filtered_cap = 0;
}

static bool as_state_ensure_filtered(struct as_state *state, size_t cap)
{
	if (state == NULL)
		return false;
	if (cap <= state->filtered_cap)
		return true;

	size_t next = state->filtered_cap == 0 ? 128 : state->filtered_cap;
	while (next < cap)
		next *= 2;

	size_t *tmp = realloc(state->filtered, next * sizeof(*tmp));
	if (tmp == NULL)
		return false;
	state->filtered = tmp;
	state->filtered_cap = next;
	return true;
}

static void as_state_rebuild_filtered(struct as_state *state)
{
	if (state == NULL)
		return;

	state->filtered_count = 0;
	if (!as_state_ensure_filtered(state, state->entry_count))
		return;

	const char *needle = state->filter != NULL ? state->filter : "";
	for (size_t i = 0; i < state->entry_count; i++) {
		const struct as_menu_entry *e = &state->entries[i];
		if (needle[0] == '\0' || str_case_contains(e->label, needle) || str_case_contains(e->command, needle)) {
			state->filtered[state->filtered_count++] = i;
		}
	}

	if (state->filtered_count == 0) {
		state->selected_index = -1;
		state->hover_index = -1;
		state->pressed_index = -1;
		state->scroll = 0;
	} else {
		if (state->selected_index < 0)
			state->selected_index = 0;
		if ((size_t)state->selected_index >= state->filtered_count)
			state->selected_index = (int)(state->filtered_count - 1);
		if (state->scroll < 0)
			state->scroll = 0;
		if ((size_t)state->scroll > state->filtered_count)
			state->scroll = 0;
	}

	schedule_redraw(state);
}

static bool as_state_filter_set(struct as_state *state, const char *text)
{
	if (state == NULL)
		return false;
	if (text == NULL)
		text = "";

	size_t n = strlen(text);
	if (n + 1 > state->filter_cap) {
		size_t next = state->filter_cap == 0 ? 64 : state->filter_cap;
		while (next < n + 1)
			next *= 2;
		char *tmp = realloc(state->filter, next);
		if (tmp == NULL)
			return false;
		state->filter = tmp;
		state->filter_cap = next;
	}

	memcpy(state->filter, text, n + 1);
	state->filter_len = n;
	as_state_rebuild_filtered(state);
	return true;
}

static bool as_state_filter_append_utf8(struct as_state *state, const char *utf8)
{
	if (state == NULL || utf8 == NULL || utf8[0] == '\0')
		return false;

	size_t add = strlen(utf8);
	if (state->filter_len + add + 1 > 256)
		return false;

	if (state->filter_len + add + 1 > state->filter_cap) {
		size_t next = state->filter_cap == 0 ? 64 : state->filter_cap;
		while (next < state->filter_len + add + 1)
			next *= 2;
		char *tmp = realloc(state->filter, next);
		if (tmp == NULL)
			return false;
		state->filter = tmp;
		state->filter_cap = next;
	}

	memcpy(state->filter + state->filter_len, utf8, add);
	state->filter_len += add;
	state->filter[state->filter_len] = '\0';
	as_state_rebuild_filtered(state);
	return true;
}

static void as_state_filter_backspace(struct as_state *state)
{
	if (state == NULL || state->filter == NULL || state->filter_len == 0)
		return;

	/* Remove last UTF-8 byte sequence (best-effort). */
	size_t i = state->filter_len;
	while (i > 0 && ((unsigned char)state->filter[i - 1] & 0xC0u) == 0x80u)
		i--;
	if (i == 0)
		i = state->filter_len - 1;
	state->filter[i] = '\0';
	state->filter_len = i;
	as_state_rebuild_filtered(state);
}

static void as_state_destroy_buffers(struct as_state *state)
{
	for (size_t i = 0; i < sizeof(state->buffers) / sizeof(state->buffers[0]); i++) {
		as_buffer_destroy(state->buffers[i]);
		state->buffers[i] = NULL;
	}
}

static bool as_state_ensure_buffers(struct as_state *state)
{
	if (state->width <= 0 || state->height <= 0)
		return false;

	if (state->buffers[0] != NULL && state->buffers[0]->width == state->width && state->buffers[0]->height == state->height)
		return true;

	as_state_destroy_buffers(state);

	for (size_t i = 0; i < sizeof(state->buffers) / sizeof(state->buffers[0]); i++) {
		state->buffers[i] = as_buffer_create(state, state->width, state->height);
		if (state->buffers[i] == NULL) {
			fprintf(stderr, "aswlmenu: failed to create shm buffers (%dx%d): %s\n",
			        state->width,
			        state->height,
			        strerror(errno));
			as_state_destroy_buffers(state);
			return false;
		}
	}

	return true;
}

static struct as_buffer *as_state_acquire_buffer(struct as_state *state)
{
	for (size_t i = 0; i < sizeof(state->buffers) / sizeof(state->buffers[0]); i++) {
		struct as_buffer *buf = state->buffers[i];
		if (buf != NULL && !buf->busy)
			return buf;
	}
	return NULL;
}

struct as_menu_layout {
	int pad;
	int header_h;
	int row_h;
	int text_scale;
	int help_scale;
};

static bool as_state_get_layout(struct as_state *state, struct as_menu_layout *layout)
{
	if (state == NULL || layout == NULL)
		return false;

	layout->pad = 10;
	layout->text_scale = 2;
	layout->help_scale = 1;

	int text_h = as_font5x7_glyph_h(layout->text_scale);
	layout->header_h = text_h + 2 * 8;
	layout->row_h = text_h + 2 * 6;
	if (layout->row_h < text_h + 4)
		layout->row_h = text_h + 4;

	return true;
}

static size_t as_state_visible_rows(struct as_state *state, const struct as_menu_layout *layout)
{
	if (state == NULL || layout == NULL)
		return 0;

	int usable_h = state->height - layout->header_h - layout->pad * 2;
	if (usable_h <= 0 || layout->row_h <= 0)
		return 0;
	return (size_t)(usable_h / layout->row_h);
}

static void as_state_ensure_selection_visible(struct as_state *state)
{
	if (state == NULL)
		return;
	if (state->filtered_count == 0 || state->selected_index < 0)
		return;

	struct as_menu_layout layout;
	if (!as_state_get_layout(state, &layout))
		return;

	size_t rows = as_state_visible_rows(state, &layout);
	if (rows == 0)
		return;

	int sel = state->selected_index;
	if (sel < state->scroll)
		state->scroll = sel;
	else if ((size_t)sel >= (size_t)state->scroll + rows)
		state->scroll = sel - (int)rows + 1;

	if (state->scroll < 0)
		state->scroll = 0;
	if ((size_t)state->scroll > state->filtered_count)
		state->scroll = 0;
}

static void as_state_select_delta(struct as_state *state, int delta)
{
	if (state == NULL)
		return;
	if (state->filtered_count == 0)
		return;

	int sel = state->selected_index;
	if (sel < 0)
		sel = 0;
	sel += delta;
	if (sel < 0)
		sel = 0;
	if ((size_t)sel >= state->filtered_count)
		sel = (int)(state->filtered_count - 1);

	state->selected_index = sel;
	as_state_ensure_selection_visible(state);
	schedule_redraw(state);
}

static int as_state_hit_test(struct as_state *state, int x, int y)
{
	struct as_menu_layout layout;
	if (!as_state_get_layout(state, &layout))
		return -1;

	if (y < layout.header_h)
		return -1;

	int list_y = y - layout.header_h - layout.pad;
	if (list_y < 0)
		return -1;

	int row = list_y / layout.row_h;
	if (row < 0)
		return -1;

	size_t visible = as_state_visible_rows(state, &layout);
	if ((size_t)row >= visible)
		return -1;

	int idx = state->scroll + row;
	if (idx < 0 || (size_t)idx >= state->filtered_count)
		return -1;

	int row_y = layout.header_h + layout.pad + row * layout.row_h;
	if (y < row_y || y >= row_y + layout.row_h)
		return -1;

	(void)x;
	return idx;
}

static void as_state_draw(struct as_state *state, struct as_buffer *buf)
{
	as_buffer_paint_solid(buf, state->theme.menu_bg);

	struct as_menu_layout layout;
	if (!as_state_get_layout(state, &layout))
		return;

	/* Border */
	as_buffer_fill_rect(buf, 0, 0, buf->width, 1, state->theme.menu_border);
	as_buffer_fill_rect(buf, 0, buf->height - 1, buf->width, 1, state->theme.menu_border);
	as_buffer_fill_rect(buf, 0, 0, 1, buf->height, state->theme.menu_border);
	as_buffer_fill_rect(buf, buf->width - 1, 0, 1, buf->height, state->theme.menu_border);

	/* Header/filter bar */
	as_buffer_fill_rect(buf, 0, 0, buf->width, layout.header_h, state->theme.menu_header_bg);
	as_buffer_fill_rect(buf, 0, layout.header_h - 1, buf->width, 1, state->theme.menu_border);

	char header[512];
	const char *filter = state->filter != NULL ? state->filter : "";
	(void)snprintf(header, sizeof(header), "> %s", filter);
	int tx = layout.pad;
	int ty = (layout.header_h - as_font5x7_glyph_h(layout.text_scale)) / 2;
	as_buffer_draw_text5x7(buf, tx, ty, header, buf->width - 2 * layout.pad, layout.text_scale, state->theme.menu_header_fg);

	/* List */
	size_t rows = as_state_visible_rows(state, &layout);
	int list_x = layout.pad;
	int list_w = buf->width - 2 * layout.pad;
	int row_y0 = layout.header_h + layout.pad;

	for (size_t row = 0; row < rows; row++) {
		int idx = state->scroll + (int)row;
		if (idx < 0 || (size_t)idx >= state->filtered_count)
			break;

		size_t entry_idx = state->filtered[idx];
		if (entry_idx >= state->entry_count)
			continue;
		const struct as_menu_entry *e = &state->entries[entry_idx];

		int y = row_y0 + (int)row * layout.row_h;

		uint32_t bg = state->theme.menu_item_bg;
		uint32_t fg = state->theme.menu_item_fg;
		if (idx == state->selected_index) {
			bg = state->theme.menu_item_sel_bg;
			fg = state->theme.menu_item_sel_fg;
		} else if (idx == state->pressed_index) {
			bg = aswl_color_nudge(bg, 48);
		} else if (idx == state->hover_index) {
			bg = aswl_color_nudge(bg, 24);
		}

		as_buffer_fill_rect(buf, list_x, y, list_w, layout.row_h, bg);
		as_buffer_fill_rect(buf, list_x, y + layout.row_h - 1, list_w, 1, state->theme.menu_border);

		char line[512];
		if (e->pinned)
			(void)snprintf(line, sizeof(line), "* %s", e->label);
		else
			(void)snprintf(line, sizeof(line), "  %s", e->label);

		int ly = y + (layout.row_h - as_font5x7_glyph_h(layout.text_scale)) / 2;
		as_buffer_draw_text5x7(buf, list_x + 8, ly, line, list_w - 16, layout.text_scale, fg);
	}

	/* Footer/help (small) */
	char footer[256];
	(void)snprintf(footer, sizeof(footer), "Enter: run   Esc: clear/close   Up/Down: select   Backspace: delete   (%zu items)",
	               state->filtered_count);
	int fh = as_font5x7_glyph_h(layout.help_scale);
	int fy = buf->height - fh - 6;
	if (fy > layout.header_h) {
		as_buffer_fill_rect(buf, 0, fy - 6, buf->width, fh + 12, state->theme.menu_footer_bg);
		as_buffer_draw_text5x7(buf, layout.pad, fy, footer, buf->width - 2 * layout.pad, layout.help_scale, state->theme.menu_footer_fg);
	}
}

static void draw_and_commit(struct as_state *state)
{
	if (state->surface == NULL)
		return;
	if (!state->configured)
		return;
	if (state->width <= 0 || state->height <= 0)
		return;

	if (!as_state_ensure_buffers(state)) {
		state->running = false;
		return;
	}

	struct as_buffer *buf = as_state_acquire_buffer(state);
	if (buf == NULL) {
		state->needs_redraw = true;
		return;
	}

	state->needs_redraw = false;
	as_state_draw(state, buf);

	buf->busy = true;
	wl_surface_attach(state->surface, buf->wl_buffer, 0, 0);
	wl_surface_damage(state->surface, 0, 0, buf->width, buf->height);
	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);
	state->frame_cb = wl_surface_frame(state->surface);
	wl_callback_add_listener(state->frame_cb, &frame_listener, state);
	wl_surface_commit(state->surface);
}

static void schedule_redraw(struct as_state *state)
{
	if (state == NULL)
		return;
	state->needs_redraw = true;
	if (state->frame_cb == NULL)
		draw_and_commit(state);
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms)
{
	(void)time_ms;
	struct as_state *state = data;

	if (cb != NULL)
		wl_callback_destroy(cb);
	if (state->frame_cb == cb)
		state->frame_cb = NULL;

	if (state->needs_redraw)
		draw_and_commit(state);
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	.ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
	struct as_state *state = data;
	xdg_surface_ack_configure(surface, serial);
	state->configured = true;
	schedule_redraw(state);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data,
                                   struct xdg_toplevel *toplevel,
                                   int32_t width,
                                   int32_t height,
                                   struct wl_array *states)
{
	(void)toplevel;
	(void)states;

	struct as_state *state = data;

	/* Compositor may send 0x0 to mean “no preference”. */
	if (width > 0)
		state->width = width;
	if (height > 0)
		state->height = height;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
	(void)toplevel;
	struct as_state *state = data;
	state->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure,
	.close = xdg_toplevel_close,
};

static void pointer_enter(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface,
                          wl_fixed_t surface_x,
                          wl_fixed_t surface_y)
{
	(void)pointer;
	(void)serial;
	(void)surface;

	struct as_state *state = data;
	state->pointer_in_surface = true;
	state->pointer_x = (int)wl_fixed_to_double(surface_x);
	state->pointer_y = (int)wl_fixed_to_double(surface_y);
	state->hover_index = as_state_hit_test(state, state->pointer_x, state->pointer_y);
	schedule_redraw(state);
}

static void pointer_leave(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface)
{
	(void)pointer;
	(void)serial;
	(void)surface;

	struct as_state *state = data;
	state->pointer_in_surface = false;
	state->hover_index = -1;
	state->pressed_index = -1;
	schedule_redraw(state);
}

static void pointer_motion(void *data,
                           struct wl_pointer *pointer,
                           uint32_t time,
                           wl_fixed_t surface_x,
                           wl_fixed_t surface_y)
{
	(void)pointer;
	(void)time;
	struct as_state *state = data;

	state->pointer_x = (int)wl_fixed_to_double(surface_x);
	state->pointer_y = (int)wl_fixed_to_double(surface_y);

	int old = state->hover_index;
	state->hover_index = state->pointer_in_surface ? as_state_hit_test(state, state->pointer_x, state->pointer_y) : -1;
	if (state->hover_index != old)
		schedule_redraw(state);
}

static void pointer_button(void *data,
                           struct wl_pointer *pointer,
                           uint32_t serial,
                           uint32_t time,
                           uint32_t button,
                           uint32_t state_w)
{
	(void)pointer;
	(void)serial;
	(void)time;
	struct as_state *state = data;

	if (button != BTN_LEFT)
		return;

	if (state_w == WL_POINTER_BUTTON_STATE_PRESSED) {
		state->pressed_index = state->hover_index;
		schedule_redraw(state);
		return;
	}

	if (state_w != WL_POINTER_BUTTON_STATE_RELEASED)
		return;

	int clicked = state->pressed_index;
	state->pressed_index = -1;
	schedule_redraw(state);

	if (clicked < 0 || clicked != state->hover_index)
		return;
	if ((size_t)clicked >= state->filtered_count)
		return;

	state->selected_index = clicked;
	as_state_ensure_selection_visible(state);

	size_t entry_idx = state->filtered[clicked];
	if (entry_idx >= state->entry_count)
		return;

	fprintf(stderr, "aswlmenu: launch %s: %s\n",
	        state->entries[entry_idx].label,
	        state->entries[entry_idx].command);
	as_state_launch_command(state, state->entries[entry_idx].command);
	state->running = false;
}

static void pointer_axis(void *data,
                         struct wl_pointer *pointer,
                         uint32_t time,
                         uint32_t axis,
                         wl_fixed_t value)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
	(void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
	(void)data;
	(void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source)
{
	(void)data;
	(void)pointer;
	(void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete)
{
	(void)data;
	(void)pointer;
	(void)axis;
	(void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

#ifdef HAVE_XKBCOMMON
static void keyboard_keymap(void *data,
                            struct wl_keyboard *keyboard,
                            uint32_t format,
                            int32_t fd,
                            uint32_t size)
{
	(void)keyboard;
	struct as_state *state = data;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}

	char *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return;
	}

	if (state->xkb_context == NULL)
		state->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	struct xkb_keymap *keymap = NULL;
	if (state->xkb_context != NULL) {
		keymap = xkb_keymap_new_from_string(state->xkb_context,
		                                    map,
		                                    XKB_KEYMAP_FORMAT_TEXT_V1,
		                                    XKB_KEYMAP_COMPILE_NO_FLAGS);
	}

	munmap(map, size);
	close(fd);

	if (keymap == NULL)
		return;

	struct xkb_state *xkb_state = xkb_state_new(keymap);
	if (xkb_state == NULL) {
		xkb_keymap_unref(keymap);
		return;
	}

	if (state->xkb_state != NULL)
		xkb_state_unref(state->xkb_state);
	if (state->xkb_keymap != NULL)
		xkb_keymap_unref(state->xkb_keymap);

	state->xkb_keymap = keymap;
	state->xkb_state = xkb_state;
}
#else
static void keyboard_keymap(void *data,
                            struct wl_keyboard *keyboard,
                            uint32_t format,
                            int32_t fd,
                            uint32_t size)
{
	(void)data;
	(void)keyboard;
	(void)format;
	(void)size;
	close(fd);
}
#endif

static void keyboard_enter(void *data,
                           struct wl_keyboard *keyboard,
                           uint32_t serial,
                           struct wl_surface *surface,
                           struct wl_array *keys)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
	(void)keys;
}

static void keyboard_leave(void *data,
                           struct wl_keyboard *keyboard,
                           uint32_t serial,
                           struct wl_surface *surface)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
}

static void keyboard_key(void *data,
                         struct wl_keyboard *keyboard,
                         uint32_t serial,
                         uint32_t time,
                         uint32_t key,
                         uint32_t state_w)
{
	(void)keyboard;
	(void)serial;
	(void)time;
	struct as_state *state = data;

	if (state_w != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	if (key == KEY_ESC) {
		if (state->filter_len > 0)
			(void)as_state_filter_set(state, "");
		else
			state->running = false;
		return;
	}

	if (key == KEY_BACKSPACE) {
		as_state_filter_backspace(state);
		return;
	}

	if (key == KEY_ENTER) {
		if (state->selected_index < 0 || (size_t)state->selected_index >= state->filtered_count)
			return;
		size_t entry_idx = state->filtered[state->selected_index];
		if (entry_idx >= state->entry_count)
			return;
		fprintf(stderr, "aswlmenu: launch %s: %s\n",
		        state->entries[entry_idx].label,
		        state->entries[entry_idx].command);
		as_state_launch_command(state, state->entries[entry_idx].command);
		state->running = false;
		return;
	}

	if (key == KEY_UP) {
		as_state_select_delta(state, -1);
		return;
	}
	if (key == KEY_DOWN) {
		as_state_select_delta(state, +1);
		return;
	}
	if (key == KEY_PAGEUP) {
		as_state_select_delta(state, -10);
		return;
	}
	if (key == KEY_PAGEDOWN) {
		as_state_select_delta(state, +10);
		return;
	}

#ifdef HAVE_XKBCOMMON
	if (state->xkb_state != NULL) {
		uint32_t keycode = key + 8;
		char utf8[64] = { 0 };
		int n = xkb_state_key_get_utf8(state->xkb_state, keycode, utf8, (int)sizeof(utf8));
		if (n > 0) {
			utf8[(size_t)n] = '\0';
			bool any_printable = false;
			for (int i = 0; i < n; i++) {
				unsigned char ch = (unsigned char)utf8[i];
				if (ch >= 0x20 && ch != 0x7F)
					any_printable = true;
			}
			if (any_printable)
				(void)as_state_filter_append_utf8(state, utf8);
		}
	}
#else
	(void)state;
#endif
}

static void keyboard_modifiers(void *data,
                               struct wl_keyboard *keyboard,
                               uint32_t serial,
                               uint32_t mods_depressed,
                               uint32_t mods_latched,
                               uint32_t mods_locked,
                               uint32_t group)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)mods_depressed;
	(void)mods_latched;
	(void)mods_locked;
	(void)group;

#ifdef HAVE_XKBCOMMON
	struct as_state *state = data;
	if (state == NULL || state->xkb_state == NULL)
		return;

	xkb_state_update_mask(state->xkb_state,
	                      mods_depressed,
	                      mods_latched,
	                      mods_locked,
	                      0,
	                      0,
	                      group);
#endif
}

static void keyboard_repeat_info(void *data,
                                 struct wl_keyboard *keyboard,
                                 int32_t rate,
                                 int32_t delay)
{
	(void)data;
	(void)keyboard;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
	struct as_state *state = data;

	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0) {
		if (state->pointer == NULL) {
			state->pointer = wl_seat_get_pointer(seat);
			if (state->pointer != NULL)
				wl_pointer_add_listener(state->pointer, &pointer_listener, state);
		}
	} else if (state->pointer != NULL) {
		wl_pointer_destroy(state->pointer);
		state->pointer = NULL;
	}

	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0) {
		if (state->keyboard == NULL) {
			state->keyboard = wl_seat_get_keyboard(seat);
			if (state->keyboard != NULL)
				wl_keyboard_add_listener(state->keyboard, &keyboard_listener, state);
		}
	} else if (state->keyboard != NULL) {
		wl_keyboard_destroy(state->keyboard);
		state->keyboard = NULL;
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

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
	(void)as_state_append_entry(state, tmp->name, tmp->exec, false);
}

static void desktop_tmp_reset(struct desktop_tmp_entry *tmp)
{
	if (tmp == NULL)
		return;
	free(tmp->name);
	free(tmp->exec);
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

static void as_state_add_desktop_entries(struct as_state *state)
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
		for (char *tok = strtok_r(dirs, ":", &saveptr); tok != NULL; tok = strtok_r(NULL, ":", &saveptr)) {
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

static int cmp_entry_label_ci(const void *a, const void *b)
{
	const struct as_menu_entry *ea = a;
	const struct as_menu_entry *eb = b;
	if (ea->label == NULL && eb->label == NULL)
		return 0;
	if (ea->label == NULL)
		return -1;
	if (eb->label == NULL)
		return 1;
	return strcasecmp(ea->label, eb->label);
}

static bool load_menu_from_file(struct as_state *state, const char *path)
{
	if (state == NULL || path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return false;

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len;
	bool any = false;

	while ((line_len = getline(&line, &line_cap, fp)) != -1) {
		while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
			line[--line_len] = '\0';

		char *s = lstrip(line);
		if (*s == '\0' || *s == '#')
			continue;

		if (*s == '@') {
			s++;
			if (strcmp(s, "desktop_entries") == 0 || strcmp(s, "desktop") == 0) {
				state->include_desktop_entries = true;
				any = true;
				continue;
			}
			if (strcmp(s, "no_desktop_entries") == 0 || strcmp(s, "no_desktop") == 0) {
				state->include_desktop_entries = false;
				any = true;
				continue;
			}
		}

		char *eq = strchr(s, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';

		char *label = s;
		char *command = eq + 1;
		rstrip(label);
		label = lstrip(label);
		command = lstrip(command);
		rstrip(command);
		if (label[0] == '\0' || command[0] == '\0')
			continue;

		any |= as_state_append_entry(state, label, command, true);
	}

	free(line);
	fclose(fp);
	return any;
}

static void as_state_load_menu(struct as_state *state)
{
	state->include_desktop_entries = true;
	free(state->menu_config_path);
	state->menu_config_path = NULL;

	const char *path = getenv("ASWLMENU_CONFIG");
	if (path != NULL && load_menu_from_file(state, path)) {
		state->menu_config_path = strdup(path);
		return;
	}

	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg != NULL && xdg[0] != '\0') {
		char *xdg_path = NULL;
		if (asprintf(&xdg_path, "%s/afterstep/aswlmenu.conf", xdg) >= 0) {
			bool ok = load_menu_from_file(state, xdg_path);
			if (ok)
				state->menu_config_path = strdup(xdg_path);
			free(xdg_path);
			if (ok)
				return;
		}
	}

	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		char *home_path = NULL;
		if (asprintf(&home_path, "%s/.config/afterstep/aswlmenu.conf", home) >= 0) {
			bool ok = load_menu_from_file(state, home_path);
			if (ok)
				state->menu_config_path = strdup(home_path);
			free(home_path);
			if (ok)
				return;
		}
	}

	/* Defaults if no config exists. */
	(void)as_state_append_entry(state, "Terminal", "${TERMINAL:-foot}", true);
	(void)as_state_append_entry(state, "Close focused", "@close", true);
	(void)as_state_append_entry(state, "Quit compositor", "@quit", true);
}

static void as_state_finalize_menu(struct as_state *state)
{
	if (state == NULL)
		return;

	if (state->include_desktop_entries)
		as_state_add_desktop_entries(state);

	if (state->entry_count > state->pinned_count) {
		qsort(state->entries + state->pinned_count,
		      state->entry_count - state->pinned_count,
		      sizeof(state->entries[0]),
		      cmp_entry_label_ci);
	}

	as_state_rebuild_filtered(state);
}

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
	struct as_state *state = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		uint32_t bind_version = version < 4 ? version : 4;
		state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, bind_version);
		return;
	}

	if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		return;
	}

	if (strcmp(interface, afterstep_control_v1_interface.name) == 0) {
		uint32_t bind_version = version < 3 ? version : 3;
		if (bind_version < 1)
			bind_version = 1;
		state->control_version = bind_version;
		state->control = wl_registry_bind(registry, name, &afterstep_control_v1_interface, bind_version);
		return;
	}

	if (strcmp(interface, wl_seat_interface.name) == 0) {
		uint32_t bind_version = version < 5 ? version : 5;
		state->seat = wl_registry_bind(registry, name, &wl_seat_interface, bind_version);
		wl_seat_add_listener(state->seat, &seat_listener, state);
		return;
	}

	if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		state->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
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

static bool setup_xdg(struct as_state *state)
{
	if (state->xdg_wm_base == NULL) {
		fprintf(stderr, "aswlmenu: compositor does not advertise xdg_wm_base\n");
		return false;
	}

	state->xdg_surface = xdg_wm_base_get_xdg_surface(state->xdg_wm_base, state->surface);
	if (state->xdg_surface == NULL)
		return false;

	xdg_surface_add_listener(state->xdg_surface, &xdg_surface_listener, state);

	state->xdg_toplevel = xdg_surface_get_toplevel(state->xdg_surface);
	if (state->xdg_toplevel == NULL)
		return false;

	xdg_toplevel_set_title(state->xdg_toplevel, "AfterStep Launcher (aswlmenu)");
	xdg_toplevel_set_app_id(state->xdg_toplevel, "afterstep.aswlmenu");
	xdg_toplevel_add_listener(state->xdg_toplevel, &xdg_toplevel_listener, state);

	wl_surface_commit(state->surface);
	return true;
}

static void cleanup(struct as_state *state)
{
	if (state->frame_cb != NULL)
		wl_callback_destroy(state->frame_cb);

	as_state_destroy_buffers(state);
	as_state_free_filtered(state);
	as_state_free_entries(state);
	free(state->menu_config_path);
	state->menu_config_path = NULL;
	free(state->filter);
	state->filter = NULL;
	state->filter_len = 0;
	state->filter_cap = 0;

#ifdef HAVE_XKBCOMMON
	if (state->xkb_state != NULL)
		xkb_state_unref(state->xkb_state);
	if (state->xkb_keymap != NULL)
		xkb_keymap_unref(state->xkb_keymap);
	if (state->xkb_context != NULL)
		xkb_context_unref(state->xkb_context);
	state->xkb_state = NULL;
	state->xkb_keymap = NULL;
	state->xkb_context = NULL;
#endif

	if (state->xdg_toplevel != NULL)
		xdg_toplevel_destroy(state->xdg_toplevel);
	if (state->xdg_surface != NULL)
		xdg_surface_destroy(state->xdg_surface);
	if (state->xdg_wm_base != NULL)
		xdg_wm_base_destroy(state->xdg_wm_base);

	if (state->control != NULL)
		afterstep_control_v1_destroy(state->control);

	if (state->surface != NULL)
		wl_surface_destroy(state->surface);
	if (state->keyboard != NULL)
		wl_keyboard_destroy(state->keyboard);
	if (state->pointer != NULL)
		wl_pointer_destroy(state->pointer);
	if (state->seat != NULL)
		wl_seat_destroy(state->seat);
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
		.width = 640,
		.height = 520,
		.running = true,
		.hover_index = -1,
		.pressed_index = -1,
		.selected_index = 0,
		.scroll = 0,
	};

	aswl_theme_init_default(&state.theme);
	(void)aswl_theme_load(&state.theme);

	as_state_load_menu(&state);
	as_state_finalize_menu(&state);

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "aswlmenu: wl_display_connect failed: %s\n", strerror(errno));
		cleanup(&state);
		return 1;
	}

	state.registry = wl_display_get_registry(state.display);
	if (state.registry == NULL) {
		fprintf(stderr, "aswlmenu: wl_display_get_registry failed\n");
		cleanup(&state);
		return 1;
	}

	wl_registry_add_listener(state.registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);

	if (state.compositor == NULL || state.shm == NULL) {
		fprintf(stderr, "aswlmenu: missing required globals (compositor=%p shm=%p)\n",
		        (void *)state.compositor,
		        (void *)state.shm);
		cleanup(&state);
		return 1;
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	if (state.surface == NULL) {
		fprintf(stderr, "aswlmenu: wl_compositor_create_surface failed\n");
		cleanup(&state);
		return 1;
	}

	if (!setup_xdg(&state)) {
		fprintf(stderr, "aswlmenu: failed to set up xdg-shell surface\n");
		cleanup(&state);
		return 1;
	}

	/* Ensure listeners see initial seat/keymap events. */
	wl_display_roundtrip(state.display);

	while (state.running && wl_display_dispatch(state.display) != -1) {
		if (state.needs_redraw && state.frame_cb == NULL)
			draw_and_commit(&state);
	}

	cleanup(&state);
	return 0;
}
