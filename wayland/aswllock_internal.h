#ifndef ASWL_LOCK_INTERNAL_H
#define ASWL_LOCK_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wl_buffer;

struct aswl_font;
struct aswl_theme;

struct as_buffer {
	struct wl_buffer *wl_buffer;
	void *data;
	int width;
	int height;
	int stride;
	size_t size;
	bool busy;
	bool destroy_on_release;
};

void aswllock_draw_ui(const struct aswl_theme *theme, struct aswl_font *font, struct as_buffer *buf, int scale);

#endif /* ASWL_LOCK_INTERNAL_H */
