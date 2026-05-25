#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlpanel_internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if HAVE_AFTERIMAGE
#include "afterimage.h"
#endif

uint32_t as_premul_argb(uint32_t argb)
{
	uint32_t a = (argb >> 24) & 0xFFu;
	if (a == 0)
		return 0;
	if (a == 255u)
		return argb;

	uint32_t r = (argb >> 16) & 0xFFu;
	uint32_t g = (argb >> 8) & 0xFFu;
	uint32_t b = argb & 0xFFu;

	r = (r * a + 127u) / 255u;
	g = (g * a + 127u) / 255u;
	b = (b * a + 127u) / 255u;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static uint32_t as_unpremul_argb(uint32_t argb)
{
	uint32_t a = (argb >> 24) & 0xFFu;
	if (a == 0)
		return 0;
	if (a == 255u)
		return argb;

	uint32_t r = (argb >> 16) & 0xFFu;
	uint32_t g = (argb >> 8) & 0xFFu;
	uint32_t b = argb & 0xFFu;

	r = (r * 255u + a / 2u) / a;
	g = (g * 255u + a / 2u) / a;
	b = (b * 255u + a / 2u) / a;

	if (r > 255u)
		r = 255u;
	if (g > 255u)
		g = 255u;
	if (b > 255u)
		b = 255u;

	return (a << 24) | (r << 16) | (g << 8) | b;
}

void as_bg_snapshot_destroy(struct as_bg_snapshot *snap)
{
	if (snap == NULL)
		return;
	if (snap->map != NULL && snap->size > 0)
		munmap(snap->map, snap->size);
	free(snap->path);
	*snap = (struct as_bg_snapshot){ 0 };
}

static char *as_expand_tilde_dup(const char *path)
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

static char *as_sanitize_filename_segment(const char *s)
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

static char *as_bg_snapshot_path(void)
{
	const char *env = getenv("ASWLBG_SNAPSHOT");
	if (env != NULL) {
		if (env[0] == '\0' || strcmp(env, "0") == 0 || strcasecmp(env, "off") == 0 || strcasecmp(env, "false") == 0)
			return NULL;
		return as_expand_tilde_dup(env);
	}

	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime == NULL || runtime[0] == '\0')
		runtime = "/tmp";

	char *safe = as_sanitize_filename_segment(getenv("WAYLAND_DISPLAY"));
	if (safe == NULL)
		return NULL;

	char *out = NULL;
	if (asprintf(&out, "%s/afterstep.aswlbg.%s.argb", runtime, safe) < 0)
		out = NULL;
	free(safe);
	return out;
}

static bool as_bg_snapshot_load(struct as_bg_snapshot *snap, const char *path)
{
	if (snap == NULL || path == NULL || path[0] == '\0')
		return false;

	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
			fprintf(stderr, "aswlpanel: bg_snapshot: open %s failed: %s\n", path, strerror(errno));
		}
		return false;
	}

	struct stat st;
	if (fstat(fd, &st) != 0) {
		if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
			fprintf(stderr, "aswlpanel: bg_snapshot: fstat %s failed: %s\n", path, strerror(errno));
		}
		close(fd);
		return false;
	}
	if (st.st_size < (off_t)sizeof(struct aswl_bg_snapshot_header)) {
		if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
			fprintf(stderr, "aswlpanel: bg_snapshot: %s too small (%ld bytes)\n", path, (long)st.st_size);
		}
		close(fd);
		return false;
	}

	size_t size = (size_t)st.st_size;
	void *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
			fprintf(stderr, "aswlpanel: bg_snapshot: mmap %s failed: %s\n", path, strerror(errno));
		}
		return false;
	}

	const struct aswl_bg_snapshot_header *hdr = (const struct aswl_bg_snapshot_header *)map;
	static const char want_magic[8] = { 'A', 'S', 'W', 'L', 'B', 'G', '1', '\0' };
	if (memcmp(hdr->magic, want_magic, sizeof(want_magic)) != 0) {
		if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
			fprintf(stderr, "aswlpanel: bg_snapshot: bad magic for %s\n", path);
		}
		munmap(map, size);
		return false;
	}

	if (hdr->width == 0 || hdr->height == 0 || hdr->width > 16384 || hdr->height > 16384) {
		if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
			fprintf(stderr, "aswlpanel: bg_snapshot: invalid dims for %s (%ux%u)\n", path, hdr->width, hdr->height);
		}
		munmap(map, size);
		return false;
	}
	if (hdr->stride < hdr->width * 4u) {
		if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
			fprintf(stderr, "aswlpanel: bg_snapshot: invalid stride for %s (%u)\n", path, hdr->stride);
		}
		munmap(map, size);
		return false;
	}

	size_t needed = sizeof(*hdr);
	if (hdr->height > 0 && (size_t)hdr->stride > (SIZE_MAX - needed) / (size_t)hdr->height) {
		if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
			fprintf(stderr, "aswlpanel: bg_snapshot: size overflow for %s\n", path);
		}
		munmap(map, size);
		return false;
	}
	needed += (size_t)hdr->stride * (size_t)hdr->height;
	if (needed > size) {
		if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
			fprintf(stderr,
			        "aswlpanel: bg_snapshot: truncated %s (need %zu bytes, have %zu)\n",
			        path,
			        needed,
			        size);
		}
		munmap(map, size);
		return false;
	}

	char *saved_path = strdup(path);
	if (saved_path == NULL) {
		if (getenv("ASWLPANEL_DEBUG_BACKPIX") != NULL) {
			fprintf(stderr, "aswlpanel: bg_snapshot: strdup failed for %s\n", path);
		}
		munmap(map, size);
		return false;
	}

	*snap = (struct as_bg_snapshot){
		.map = map,
		.size = size,
		.argb = (uint32_t *)((uint8_t *)map + sizeof(*hdr)),
		.width = (int)hdr->width,
		.height = (int)hdr->height,
		.stride = (int)hdr->stride,
		.path = saved_path,
	};
	return true;
}

bool as_state_ensure_bg_snapshot(struct as_state *state)
{
	if (state == NULL)
		return false;

	char *path = as_bg_snapshot_path();
	if (path == NULL)
		return false;

	bool ok = false;
	if (state->bg_snapshot.map != NULL && state->bg_snapshot.path != NULL && strcmp(state->bg_snapshot.path, path) == 0) {
		ok = true;
	} else {
		as_bg_snapshot_destroy(&state->bg_snapshot);
		ok = as_bg_snapshot_load(&state->bg_snapshot, path);
	}

	free(path);
	return ok;
}

static inline uint8_t as_tint_u8(uint8_t v, uint8_t tint)
{
	unsigned ratio = (unsigned)tint << 1;
	unsigned out = ((unsigned)v * ratio + 128u) >> 8;
	if (out > 255u)
		out = 255u;
	return (uint8_t)out;
}

static uint32_t as_apply_backpixmap_tint(uint32_t bg, uint32_t tint)
{
	uint8_t ba = (uint8_t)((bg >> 24) & 0xFFu);
	uint8_t br = (uint8_t)((bg >> 16) & 0xFFu);
	uint8_t bgc = (uint8_t)((bg >> 8) & 0xFFu);
	uint8_t bb = (uint8_t)(bg & 0xFFu);

	uint8_t ta = (uint8_t)((tint >> 24) & 0xFFu);
	uint8_t tr = (uint8_t)((tint >> 16) & 0xFFu);
	uint8_t tg = (uint8_t)((tint >> 8) & 0xFFu);
	uint8_t tb = (uint8_t)(tint & 0xFFu);

	uint8_t oa = as_tint_u8(ba, ta);
	uint8_t orr = as_tint_u8(br, tr);
	uint8_t og = as_tint_u8(bgc, tg);
	uint8_t ob = as_tint_u8(bb, tb);

	return ((uint32_t)oa << 24) | ((uint32_t)orr << 16) | ((uint32_t)og << 8) | (uint32_t)ob;
}

void as_buffer_fill_rect(struct as_buffer *buf, int x, int y, int w, int h, uint32_t argb);

void as_buffer_paint_vertical_gradient(struct as_buffer *buf, uint32_t top_argb, uint32_t bottom_argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	if (buf->height <= 0 || buf->width <= 0)
		return;

	for (int y = 0; y < buf->height; y++) {
		uint8_t t = 0;
		if (buf->height > 1) {
			t = (uint8_t)((uint32_t)y * 255u / (uint32_t)(buf->height - 1));
		}
		uint32_t c = as_premul_argb(aswl_color_blend(top_argb, bottom_argb, t));
		uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)y * (size_t)buf->stride);
		for (int x = 0; x < buf->width; x++)
			row[x] = c;
	}
}

#if HAVE_AFTERIMAGE
struct as_gradient_cache_entry {
	const struct aswl_gradient *grad;
	int width;
	int height;
	uint32_t *argb;
	uint64_t last_use;
};

enum { AS_GRADIENT_CACHE_MAX = 16 };

static struct as_gradient_cache_entry gradient_cache[AS_GRADIENT_CACHE_MAX];
static uint64_t gradient_cache_tick = 0;

void as_gradient_cache_destroy(void)
{
	for (size_t i = 0; i < sizeof(gradient_cache) / sizeof(gradient_cache[0]); i++) {
		free(gradient_cache[i].argb);
		gradient_cache[i] = (struct as_gradient_cache_entry){ 0 };
	}
	gradient_cache_tick = 0;
}

static int as_afterimage_gradient_type(int type)
{
	/* Normalize the legacy aliases AfterStep documents. */
	switch (type) {
	case 1:
		type = 6;
		break;
	case 2:
		type = 8;
		break;
	case 4:
		type = 9;
		break;
	default:
		break;
	}

	switch (type) {
	case 6:
		return GRADIENT_TopLeft2BottomRight;
	case 7:
		return GRADIENT_BottomLeft2TopRight;
	case 8:
		return GRADIENT_Top2Bottom;
	case 9:
		return GRADIENT_Left2Right;
	default:
		return -1;
	}
}

static uint32_t *as_afterimage_make_gradient_argb(const struct aswl_gradient *grad, int width, int height)
{
	if (!aswl_gradient_is_valid(grad))
		return NULL;
	if (width <= 0 || height <= 0)
		return NULL;

	int ai_type = as_afterimage_gradient_type(grad->type);
	if (ai_type < 0)
		return NULL;

	if (grad->count < 2 || grad->count > 1024)
		return NULL;

	ASGradient ai_grad = { 0 };
	ai_grad.type = ai_type;
	ai_grad.npoints = (int)grad->count;

	ai_grad.color = calloc(grad->count, sizeof(ARGB32));
	ai_grad.offset = calloc(grad->count, sizeof(double));
	if (ai_grad.color == NULL || ai_grad.offset == NULL) {
		free(ai_grad.color);
		free(ai_grad.offset);
		return NULL;
	}

	for (size_t i = 0; i < grad->count; i++) {
		ai_grad.color[i] = (ARGB32)grad->colors[i];
		ai_grad.offset[i] = grad->offsets[i];
	}

	ASImage *im =
		make_gradient(NULL, &ai_grad, width, height, SCL_DO_ALL, ASA_ASImage, 0, ASIMAGE_QUALITY_DEFAULT);

	free(ai_grad.color);
	free(ai_grad.offset);

	if (im == NULL)
		return NULL;

	uint8_t fill_r = ARGB32_RED8(im->back_color);
	uint8_t fill_g = ARGB32_GREEN8(im->back_color);
	uint8_t fill_b = ARGB32_BLUE8(im->back_color);
	uint8_t fill_a = ARGB32_ALPHA8(im->back_color);

	uint32_t *argb = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
	CARD32 *red = calloc((size_t)width, sizeof(CARD32));
	CARD32 *green = calloc((size_t)width, sizeof(CARD32));
	CARD32 *blue = calloc((size_t)width, sizeof(CARD32));
	CARD32 *alpha = calloc((size_t)width, sizeof(CARD32));

	if (argb == NULL || red == NULL || green == NULL || blue == NULL || alpha == NULL) {
		free(argb);
		free(red);
		free(green);
		free(blue);
		free(alpha);
		destroy_asimage(&im);
		return NULL;
	}

	for (int y = 0; y < height; y++) {
		int n_r = asimage_decode_line(im, IC_RED, red, (unsigned)y, 0, (unsigned)width);
		int n_g = asimage_decode_line(im, IC_GREEN, green, (unsigned)y, 0, (unsigned)width);
		int n_b = asimage_decode_line(im, IC_BLUE, blue, (unsigned)y, 0, (unsigned)width);
		int n_a = asimage_decode_line(im, IC_ALPHA, alpha, (unsigned)y, 0, (unsigned)width);

		for (int x = 0; x < width; x++) {
			uint32_t r = (x < n_r) ? (red[x] & 0xFFu) : (uint32_t)fill_r;
			uint32_t g = (x < n_g) ? (green[x] & 0xFFu) : (uint32_t)fill_g;
			uint32_t b = (x < n_b) ? (blue[x] & 0xFFu) : (uint32_t)fill_b;
			uint32_t a = (x < n_a) ? (alpha[x] & 0xFFu) : (uint32_t)fill_a;

			argb[(size_t)y * (size_t)width + (size_t)x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	free(red);
	free(green);
	free(blue);
	free(alpha);
	destroy_asimage(&im);
	return argb;
}

static const uint32_t *as_gradient_cache_get(const struct aswl_gradient *grad, int width, int height)
{
	if (!aswl_gradient_is_valid(grad) || width <= 0 || height <= 0)
		return NULL;

	gradient_cache_tick++;
	uint64_t now = gradient_cache_tick;

	struct as_gradient_cache_entry *oldest = NULL;
	struct as_gradient_cache_entry *slot = NULL;

	for (size_t i = 0; i < sizeof(gradient_cache) / sizeof(gradient_cache[0]); i++) {
		struct as_gradient_cache_entry *ent = &gradient_cache[i];
		if (ent->argb != NULL && ent->grad == grad && ent->width == width && ent->height == height) {
			ent->last_use = now;
			return ent->argb;
		}

		if (ent->argb == NULL && slot == NULL)
			slot = ent;
		if (ent->argb != NULL && (oldest == NULL || ent->last_use < oldest->last_use))
			oldest = ent;
	}

	if (slot == NULL)
		slot = oldest;
	if (slot == NULL)
		return NULL;

	free(slot->argb);
	*slot = (struct as_gradient_cache_entry){ 0 };

	uint32_t *argb = as_afterimage_make_gradient_argb(grad, width, height);
	if (argb == NULL)
		return NULL;

	slot->grad = grad;
	slot->width = width;
	slot->height = height;
	slot->argb = argb;
	slot->last_use = now;
	return slot->argb;
}
#endif

void as_buffer_fill_style_rect(struct as_buffer *buf,
                               int x,
                               int y,
                               int w,
                               int h,
                               const struct aswl_gradient *grad,
                               uint32_t base_argb,
                               uint8_t nudge)
{
	if (buf == NULL || buf->data == NULL)
		return;
	if (w <= 0 || h <= 0)
		return;

	if (!aswl_gradient_is_valid(grad)) {
		uint32_t c = base_argb;
		if (nudge != 0)
			c = aswl_color_nudge(c, nudge);
		as_buffer_fill_rect(buf, x, y, w, h, c);
		return;
	}

#if HAVE_AFTERIMAGE
	const uint32_t *src = as_gradient_cache_get(grad, w, h);
	if (src == NULL) {
		as_buffer_fill_rect(buf, x, y, w, h, base_argb);
		return;
	}

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
		const uint32_t *src_row = src + (size_t)(yy - y) * (size_t)w + (size_t)(x1 - x);
		for (int xx = x1; xx < x2; xx++) {
			uint32_t c = src_row[xx - x1];
			if (nudge != 0)
				c = aswl_color_nudge(c, nudge);
			row[xx] = as_premul_argb(c);
		}
	}
#else
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
		for (int xx = x1; xx < x2; xx++) {
			uint32_t c = grad->colors[0];
			if (nudge != 0)
				c = aswl_color_nudge(c, nudge);
			row[xx] = as_premul_argb(c);
		}
	}
#endif
}

void as_buffer_fill_rect(struct as_buffer *buf, int x, int y, int w, int h, uint32_t argb)
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

	uint32_t premul = as_premul_argb(argb);

	for (int yy = y1; yy < y2; yy++) {
		uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)yy * (size_t)buf->stride);
		for (int xx = x1; xx < x2; xx++)
			row[xx] = premul;
	}
}

bool as_buffer_fill_backpixmap_tint(struct as_buffer *buf,
                                   const struct as_bg_snapshot *snap,
                                   int src_x,
                                   int src_y,
                                   uint32_t tint)
{
	if (buf == NULL || buf->data == NULL)
		return false;
	if (snap == NULL || snap->argb == NULL)
		return false;
	if (snap->width <= 0 || snap->height <= 0 || snap->stride <= 0)
		return false;

	for (int y = 0; y < buf->height; y++) {
		int sy = src_y + y;
		uint32_t *dst_row = (uint32_t *)((uint8_t *)buf->data + (size_t)y * (size_t)buf->stride);
		if (sy < 0 || sy >= snap->height) {
			for (int x = 0; x < buf->width; x++)
				dst_row[x] = 0;
			continue;
		}

		const uint32_t *src_row = (const uint32_t *)((const uint8_t *)snap->argb + (size_t)sy * (size_t)snap->stride);
		for (int x = 0; x < buf->width; x++) {
			int sx = src_x + x;
			if (sx < 0 || sx >= snap->width) {
				dst_row[x] = 0;
				continue;
			}
			uint32_t bg = src_row[sx];
			uint32_t out = as_apply_backpixmap_tint(bg, tint);
			dst_row[x] = as_premul_argb(out);
		}
	}

	return true;
}

void as_buffer_draw_bevel_rect(struct as_buffer *buf, int x, int y, int w, int h, uint32_t base_argb, bool sunken)
{
	if (buf == NULL || buf->data == NULL)
		return;
	if (w <= 1 || h <= 1)
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
	if (x2 - x1 <= 1 || y2 - y1 <= 1)
		return;

	uint32_t *pixels = (uint32_t *)buf->data;
	int stride_px = buf->stride / 4;

	int top = y1;
	int bottom = y2 - 1;
	int left = x1;
	int right = x2 - 1;

	uint32_t relief_fore = aswl_color_hilite(base_argb);
	uint32_t relief_back = aswl_color_shadow(base_argb);

	uint32_t hi_color = sunken ? relief_back : relief_fore;
	uint32_t lo_color = sunken ? relief_fore : relief_back;
	uint32_t hihi_color = aswl_color_hilite(relief_fore);
	uint32_t lolo_color = relief_back;
	uint32_t hilo_color = aswl_color_average(hi_color, lo_color);

	uint32_t hi_premul = as_premul_argb(hi_color);
	uint32_t lo_premul = as_premul_argb(lo_color);

	/* Top/bottom edges */
	uint32_t *row_top = pixels + (size_t)top * (size_t)stride_px;
	uint32_t *row_bot = pixels + (size_t)bottom * (size_t)stride_px;
	for (int xx = left; xx <= right; xx++) {
		row_top[xx] = hi_premul;
		row_bot[xx] = lo_premul;
	}

	/* Left/right edges (excluding corners). */
	for (int yy = top + 1; yy <= bottom - 1; yy++) {
		uint32_t *row = pixels + (size_t)yy * (size_t)stride_px;
		row[left] = hi_premul;
		row[right] = lo_premul;
	}

	/* Corners. */
	row_top[left] = as_premul_argb(sunken ? lolo_color : hihi_color);
	row_top[right] = as_premul_argb(hilo_color);
	row_bot[left] = as_premul_argb(hilo_color);
	row_bot[right] = as_premul_argb(sunken ? hihi_color : lolo_color);
}

static void as_buffer_blend_pixel(struct as_buffer *buf, int x, int y, uint32_t src_argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	if (x < 0 || y < 0 || x >= buf->width || y >= buf->height)
		return;

	uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)y * (size_t)buf->stride);
	uint32_t src = as_premul_argb(src_argb);
	uint32_t sa = (src >> 24) & 0xFFu;
	if (sa == 0)
		return;
	if (sa == 255u) {
		row[x] = src;
		return;
	}

	uint32_t dst = row[x];
	uint32_t da = (dst >> 24) & 0xFFu;
	uint32_t inv = 255u - sa;

	uint32_t out_a = sa + (da * inv + 127u) / 255u;
	uint32_t dr = (dst >> 16) & 0xFFu;
	uint32_t dg = (dst >> 8) & 0xFFu;
	uint32_t db = dst & 0xFFu;
	uint32_t sr = (src >> 16) & 0xFFu;
	uint32_t sg = (src >> 8) & 0xFFu;
	uint32_t sb = src & 0xFFu;

	uint32_t out_r = sr + (dr * inv + 127u) / 255u;
	uint32_t out_g = sg + (dg * inv + 127u) / 255u;
	uint32_t out_b = sb + (db * inv + 127u) / 255u;

	row[x] = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

static uint32_t as_sample_image_bilinear_unpremul(const uint32_t *src_argb, int sw, int sh, double gx, double gy)
{
	if (src_argb == NULL || sw <= 0 || sh <= 0)
		return 0;

	if (gx < 0.0)
		gx = 0.0;
	if (gy < 0.0)
		gy = 0.0;

	double max_x = (double)(sw - 1);
	double max_y = (double)(sh - 1);
	if (gx > max_x)
		gx = max_x;
	if (gy > max_y)
		gy = max_y;

	int x0 = (int)gx;
	int y0 = (int)gy;
	int x1 = x0 + 1;
	int y1 = y0 + 1;
	if (x1 >= sw)
		x1 = sw - 1;
	if (y1 >= sh)
		y1 = sh - 1;

	double tx = gx - (double)x0;
	double ty = gy - (double)y0;
	if (tx < 0.0)
		tx = 0.0;
	if (ty < 0.0)
		ty = 0.0;
	if (tx > 1.0)
		tx = 1.0;
	if (ty > 1.0)
		ty = 1.0;

	uint32_t p00 = as_premul_argb(src_argb[(size_t)y0 * (size_t)sw + (size_t)x0]);
	uint32_t p10 = as_premul_argb(src_argb[(size_t)y0 * (size_t)sw + (size_t)x1]);
	uint32_t p01 = as_premul_argb(src_argb[(size_t)y1 * (size_t)sw + (size_t)x0]);
	uint32_t p11 = as_premul_argb(src_argb[(size_t)y1 * (size_t)sw + (size_t)x1]);

	double w00 = (1.0 - tx) * (1.0 - ty);
	double w10 = tx * (1.0 - ty);
	double w01 = (1.0 - tx) * ty;
	double w11 = tx * ty;

	double a = (double)((p00 >> 24) & 0xFFu) * w00 + (double)((p10 >> 24) & 0xFFu) * w10 +
	           (double)((p01 >> 24) & 0xFFu) * w01 + (double)((p11 >> 24) & 0xFFu) * w11;
	double r = (double)((p00 >> 16) & 0xFFu) * w00 + (double)((p10 >> 16) & 0xFFu) * w10 +
	           (double)((p01 >> 16) & 0xFFu) * w01 + (double)((p11 >> 16) & 0xFFu) * w11;
	double g = (double)((p00 >> 8) & 0xFFu) * w00 + (double)((p10 >> 8) & 0xFFu) * w10 +
	           (double)((p01 >> 8) & 0xFFu) * w01 + (double)((p11 >> 8) & 0xFFu) * w11;
	double b = (double)(p00 & 0xFFu) * w00 + (double)(p10 & 0xFFu) * w10 + (double)(p01 & 0xFFu) * w01 +
	           (double)(p11 & 0xFFu) * w11;

	uint32_t ia = (uint32_t)(a + 0.5);
	uint32_t ir = (uint32_t)(r + 0.5);
	uint32_t ig = (uint32_t)(g + 0.5);
	uint32_t ib = (uint32_t)(b + 0.5);
	if (ia > 255u)
		ia = 255u;
	if (ir > 255u)
		ir = 255u;
	if (ig > 255u)
		ig = 255u;
	if (ib > 255u)
		ib = 255u;

	uint32_t premul = (ia << 24) | (ir << 16) | (ig << 8) | ib;
	return as_unpremul_argb(premul);
}

void as_buffer_draw_image_bilinear(struct as_buffer *buf,
                                   int dx,
                                   int dy,
                                   int dw,
                                   int dh,
                                   const uint32_t *src_argb,
                                   int sw,
                                   int sh)
{
	if (buf == NULL || buf->data == NULL)
		return;
	if (src_argb == NULL || sw <= 0 || sh <= 0)
		return;
	if (dw <= 0 || dh <= 0)
		return;

	double sx_scale = 0.0;
	double sy_scale = 0.0;
	if (dw > 1 && sw > 1)
		sx_scale = (double)(sw - 1) / (double)(dw - 1);
	if (dh > 1 && sh > 1)
		sy_scale = (double)(sh - 1) / (double)(dh - 1);

	for (int y = 0; y < dh; y++) {
		double gy = (double)y * sy_scale;
		for (int x = 0; x < dw; x++) {
			double gx = (double)x * sx_scale;
			uint32_t c = as_sample_image_bilinear_unpremul(src_argb, sw, sh, gx, gy);
			as_buffer_blend_pixel(buf, dx + x, dy + y, c);
		}
	}
}
