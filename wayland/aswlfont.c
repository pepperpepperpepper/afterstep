#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlfont.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#ifdef HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

static bool aswl_utf8_decode_next(const char *s, size_t n, size_t *i, uint32_t *cp_out)
{
	if (cp_out != NULL)
		*cp_out = 0;
	if (s == NULL || i == NULL || cp_out == NULL)
		return false;

	size_t idx = *i;
	if (idx >= n)
		return false;

	unsigned char b0 = (unsigned char)s[idx];
	if (b0 == '\0')
		return false;

	if (b0 < 0x80u) {
		*cp_out = (uint32_t)b0;
		*i = idx + 1;
		return true;
	}

	/* Reject 0x80..0xBF continuation bytes as leading bytes. */
	if ((b0 & 0xC0u) == 0x80u) {
		*cp_out = 0xFFFDu;
		*i = idx + 1;
		return true;
	}

	int need = 0;
	uint32_t cp = 0;
	uint32_t min = 0;

	if ((b0 & 0xE0u) == 0xC0u) {
		need = 2;
		cp = (uint32_t)(b0 & 0x1Fu);
		min = 0x80u;
	} else if ((b0 & 0xF0u) == 0xE0u) {
		need = 3;
		cp = (uint32_t)(b0 & 0x0Fu);
		min = 0x800u;
	} else if ((b0 & 0xF8u) == 0xF0u) {
		need = 4;
		cp = (uint32_t)(b0 & 0x07u);
		min = 0x10000u;
	} else {
		*cp_out = 0xFFFDu;
		*i = idx + 1;
		return true;
	}

	if (idx + (size_t)need > n) {
		/* Incomplete final sequence: stop (avoid splitting codepoints). */
		return false;
	}

	for (int k = 1; k < need; k++) {
		unsigned char bx = (unsigned char)s[idx + (size_t)k];
		if (bx == '\0')
			return false;
		if ((bx & 0xC0u) != 0x80u) {
			*cp_out = 0xFFFDu;
			*i = idx + 1;
			return true;
		}
		cp = (cp << 6) | (uint32_t)(bx & 0x3Fu);
	}

	/* Overlong sequences and invalid ranges. */
	if (cp < min || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
		*cp_out = 0xFFFDu;
		*i = idx + 1;
		return true;
	}

	*cp_out = cp;
	*i = idx + (size_t)need;
	return true;
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

static char *aswl_try_font_under_root(const char *root, const char *name)
{
	if (root == NULL || root[0] == '\0' || name == NULL || name[0] == '\0')
		return NULL;

	char *path = NULL;
	if (asprintf(&path, "%s/%s", root, name) < 0)
		return NULL;

	if (aswl_is_file_readable(path))
		return path;

	free(path);
	return NULL;
}

static char *aswl_try_afterstep_font_file(const char *name)
{
	if (name == NULL || name[0] == '\0')
		return NULL;

	/* Only try the AfterStep font directories for basename-like specs. */
	if (strchr(name, '/') != NULL)
		return NULL;

	const char *home = getenv("HOME");
	char *home_root = NULL;
	if (home != NULL && home[0] != '\0')
		(void)asprintf(&home_root, "%s/.afterstep/desktop/fonts", home);

	const char *roots[] = {
		home_root,
		"afterstep/desktop/fonts",
		"/usr/local/share/afterstep/desktop/fonts",
		"/usr/share/afterstep/desktop/fonts",
	};

	for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
		if (roots[i] == NULL || roots[i][0] == '\0')
			continue;
		char *p = aswl_try_font_under_root(roots[i], name);
		if (p != NULL) {
			free(home_root);
			return p;
		}
	}

	free(home_root);
	return NULL;
}

static char *aswl_dup_trim(const char *s)
{
	if (s == NULL)
		return NULL;

	while (*s != '\0' && isspace((unsigned char)*s))
		s++;

	size_t len = strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1]))
		len--;

	if (len == 0)
		return NULL;

	return strndup(s, len);
}

static char *aswl_normalize_font_spec(const char *spec)
{
	char *s = aswl_dup_trim(spec);
	if (s == NULL)
		return NULL;

	/* AfterStep often uses Xft-style "xft:..." prefixes. Strip it for fontconfig. */
	if ((s[0] == 'x' || s[0] == 'X') && (s[1] == 'f' || s[1] == 'F') && (s[2] == 't' || s[2] == 'T') &&
	    s[3] == ':') {
		char *out = aswl_dup_trim(s + 4);
		free(s);
		return out;
	}

	return s;
}

static int aswl_parse_trailing_px(const char *spec)
{
	if (spec == NULL || spec[0] == '\0')
		return 0;

	size_t len = strlen(spec);
	size_t i = len;
	while (i > 0 && isdigit((unsigned char)spec[i - 1]))
		i--;
	if (i > 1 && i < len && spec[i - 1] == '-') {
		char *end = NULL;
		long v = strtol(spec + i, &end, 10);
		if (end != NULL && *end == '\0') {
			if (v >= 4 && v <= 256)
				return (int)v;
		}
	}

	return 0;
}

static void aswl_fill_rect(uint32_t *dst_argb,
                           int dst_w,
                           int dst_h,
                           int dst_stride_px,
                           int x,
                           int y,
                           int w,
                           int h,
                           uint32_t argb)
{
	if (dst_argb == NULL)
		return;
	if (dst_w <= 0 || dst_h <= 0 || dst_stride_px <= 0)
		return;
	if (w <= 0 || h <= 0)
		return;

	int x0 = x;
	int y0 = y;
	int x1 = x + w;
	int y1 = y + h;
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > dst_w)
		x1 = dst_w;
	if (y1 > dst_h)
		y1 = dst_h;
	if (x1 <= x0 || y1 <= y0)
		return;

	uint32_t a = (argb >> 24) & 0xFFu;
	uint32_t premul = argb;
	if (a != 0 && a != 255u) {
		uint32_t r = (argb >> 16) & 0xFFu;
		uint32_t g = (argb >> 8) & 0xFFu;
		uint32_t b = argb & 0xFFu;
		r = (r * a + 127u) / 255u;
		g = (g * a + 127u) / 255u;
		b = (b * a + 127u) / 255u;
		premul = (a << 24) | (r << 16) | (g << 8) | b;
	}

	for (int cy = y0; cy < y1; cy++) {
		uint32_t *row = dst_argb + (size_t)cy * (size_t)dst_stride_px;
		for (int cx = x0; cx < x1; cx++)
			row[cx] = premul;
	}
}

static void aswl_blend_pixel(uint32_t *dst_argb, int dst_w, int dst_h, int dst_stride_px, int x, int y, uint32_t src_argb)
{
	if (dst_argb == NULL)
		return;
	if (x < 0 || y < 0 || x >= dst_w || y >= dst_h)
		return;

	uint32_t src_a = (src_argb >> 24) & 0xFFu;
	if (src_a == 0)
		return;

	uint32_t *d = dst_argb + (size_t)y * (size_t)dst_stride_px + (size_t)x;

	uint32_t src = src_argb;
	if (src_a != 255u) {
		uint32_t r = (src >> 16) & 0xFFu;
		uint32_t g = (src >> 8) & 0xFFu;
		uint32_t b = src & 0xFFu;
		r = (r * src_a + 127u) / 255u;
		g = (g * src_a + 127u) / 255u;
		b = (b * src_a + 127u) / 255u;
		src = (src_a << 24) | (r << 16) | (g << 8) | b;
	}

	if (src_a == 255u) {
		*d = src;
		return;
	}

	uint32_t dst = *d;
	uint32_t dst_a = (dst >> 24) & 0xFFu;
	uint32_t inv = 255u - src_a;

	uint32_t out_a = src_a + (dst_a * inv + 127u) / 255u;

	uint32_t dr = (dst >> 16) & 0xFFu;
	uint32_t dg = (dst >> 8) & 0xFFu;
	uint32_t db = dst & 0xFFu;

	uint32_t sr = (src >> 16) & 0xFFu;
	uint32_t sg = (src >> 8) & 0xFFu;
	uint32_t sb = src & 0xFFu;

	uint32_t out_r = sr + (dr * inv + 127u) / 255u;
	uint32_t out_g = sg + (dg * inv + 127u) / 255u;
	uint32_t out_b = sb + (db * inv + 127u) / 255u;

	*d = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
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
		static const uint8_t rows[7] = { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
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
		static const uint8_t rows[7] = { 0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0E };
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
		static const uint8_t rows[7] = { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E };
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
		static const uint8_t rows[7] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
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
	default:
		return as_font5x7_rows('?');
	}
}

int aswl_font5x7_glyph_w(int scale)
{
	return 5 * scale;
}

int aswl_font5x7_glyph_h(int scale)
{
	return 7 * scale;
}

static int as_font5x7_spacing(int scale)
{
	return scale;
}

static int as_font5x7_text_width_n(const char *s, size_t n, int scale)
{
	if (s == NULL || n == 0 || scale <= 0)
		return 0;

	int w = 0;
	int glyph_w = aswl_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);
	size_t i = 0;
	uint32_t cp = 0;
	while (aswl_utf8_decode_next(s, n, &i, &cp)) {
		if (w > 0)
			w += spacing;
		w += glyph_w;
	}
	return w;
}

static size_t as_font5x7_fit_bytes(const char *s, int scale, int max_w)
{
	if (s == NULL || max_w <= 0)
		return 0;

	int glyph_w = aswl_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);

	size_t len = strlen(s);
	size_t i = 0;
	size_t bytes = 0;
	int w = 0;
	uint32_t cp = 0;
	while (aswl_utf8_decode_next(s, len, &i, &cp)) {
		int add = glyph_w;
		if (w > 0)
			add += spacing;
		if (w + add > max_w)
			break;
		w += add;
		bytes = i;
	}
	return bytes;
}

void aswl_font5x7_draw_glyph(uint32_t *dst_argb,
                             int dst_w,
                             int dst_h,
                             int dst_stride_px,
                             int x,
                             int y,
                             char c,
                             int scale,
                             uint32_t argb)
{
	if (dst_argb == NULL)
		return;
	if (scale <= 0)
		return;

	const uint8_t *rows = as_font5x7_rows(c);

	for (int row = 0; row < 7; row++) {
		uint8_t bits = rows[row] & 0x1F;
		for (int col = 0; col < 5; col++) {
			bool on = ((bits >> (4 - col)) & 0x1) != 0;
			if (!on)
				continue;
			aswl_fill_rect(dst_argb,
			               dst_w,
			               dst_h,
			               dst_stride_px,
			               x + col * scale,
			               y + row * scale,
			               scale,
			               scale,
			               argb);
		}
	}
}

static char as_font5x7_map_codepoint(uint32_t cp)
{
	if (cp <= 0x1Fu || cp == 0x7Fu)
		return ' ';
	if (cp < 0x80u)
		return (char)cp;
	return '?';
}

static void as_font5x7_draw_text(uint32_t *dst_argb,
                                 int dst_w,
                                 int dst_h,
                                 int dst_stride_px,
                                 int x,
                                 int y,
                                 const char *s,
                                 int max_w,
                                 int scale,
                                 uint32_t argb)
{
	if (dst_argb == NULL)
		return;
	if (s == NULL || s[0] == '\0')
		return;
	if (scale <= 0)
		return;

	size_t n = strlen(s);

	size_t draw_n = n;
	bool need_ellipsis = false;
	int full_w = as_font5x7_text_width_n(s, n, scale);
	if (max_w > 0 && full_w > max_w) {
		int ell_w = as_font5x7_text_width_n("...", 3, scale);
		if (ell_w < max_w) {
			draw_n = as_font5x7_fit_bytes(s, scale, max_w - ell_w);
			need_ellipsis = true;
		} else {
			draw_n = as_font5x7_fit_bytes(s, scale, max_w);
		}
	}

	int cx = x;
	int glyph_w = aswl_font5x7_glyph_w(scale);
	int spacing = as_font5x7_spacing(scale);

	size_t i = 0;
	uint32_t cp = 0;
	size_t drawn = 0;
	while (drawn < draw_n && aswl_utf8_decode_next(s, draw_n, &i, &cp)) {
		if (drawn > 0)
			cx += spacing;
		char c = as_font5x7_map_codepoint(cp);
		aswl_font5x7_draw_glyph(dst_argb, dst_w, dst_h, dst_stride_px, cx, y, c, scale, argb);
		cx += glyph_w;
		drawn = i;
	}

	if (need_ellipsis) {
		if (draw_n > 0 && drawn > 0)
			cx += spacing;
		as_font5x7_draw_text(dst_argb,
		                     dst_w,
		                     dst_h,
		                     dst_stride_px,
		                     cx,
		                     y,
		                     "...",
		                     max_w > 0 ? max_w - (cx - x) : 0,
		                     scale,
		                     argb);
	}
}

#ifdef HAVE_FREETYPE

struct aswl_ft_cached_glyph {
	uint32_t codepoint;
	FT_UInt glyph_index;
	int advance;

	int bitmap_left;
	int bitmap_top;
	int width;
	int rows;
	uint8_t *coverage;
	bool rendered;

	uint64_t last_used;
};

struct aswl_ft_cache {
	struct aswl_ft_cached_glyph *glyphs;
	size_t count;
	size_t cap;
	uint64_t tick;
	int pixel_size;
};

static struct aswl_ft_cache *aswl_ft_cache_get(struct aswl_font *font)
{
	if (font == NULL)
		return NULL;
	return (struct aswl_ft_cache *)font->ft_cache;
}

static void aswl_ft_cache_clear(struct aswl_ft_cache *cache)
{
	if (cache == NULL)
		return;
	for (size_t i = 0; i < cache->count; i++)
		free(cache->glyphs[i].coverage);
	cache->count = 0;
}

static void aswl_ft_cache_destroy(struct aswl_font *font)
{
	if (font == NULL)
		return;
	struct aswl_ft_cache *cache = aswl_ft_cache_get(font);
	if (cache == NULL)
		return;
	aswl_ft_cache_clear(cache);
	free(cache->glyphs);
	free(cache);
	font->ft_cache = NULL;
}

static bool aswl_ft_cache_init(struct aswl_font *font)
{
	if (font == NULL)
		return false;
	if (font->ft_cache != NULL)
		return true;

	struct aswl_ft_cache *cache = calloc(1, sizeof(*cache));
	if (cache == NULL)
		return false;

	cache->cap = 512;
	cache->glyphs = calloc(cache->cap, sizeof(*cache->glyphs));
	if (cache->glyphs == NULL) {
		free(cache);
		return false;
	}
	cache->tick = 1;
	cache->pixel_size = 0;
	font->ft_cache = cache;
	return true;
}

static void aswl_ft_cache_set_pixel_size(struct aswl_font *font, int px)
{
	struct aswl_ft_cache *cache = aswl_ft_cache_get(font);
	if (cache == NULL)
		return;
	if (cache->pixel_size == px)
		return;
	aswl_ft_cache_clear(cache);
	cache->pixel_size = px;
}

static struct aswl_ft_cached_glyph *aswl_ft_cache_lookup(struct aswl_font *font, uint32_t codepoint)
{
	struct aswl_ft_cache *cache = aswl_ft_cache_get(font);
	if (cache == NULL)
		return NULL;

	for (size_t i = 0; i < cache->count; i++) {
		if (cache->glyphs[i].codepoint == codepoint) {
			cache->glyphs[i].last_used = cache->tick++;
			return &cache->glyphs[i];
		}
	}
	return NULL;
}

static struct aswl_ft_cached_glyph *aswl_ft_cache_alloc_slot(struct aswl_font *font)
{
	struct aswl_ft_cache *cache = aswl_ft_cache_get(font);
	if (cache == NULL || cache->glyphs == NULL || cache->cap == 0)
		return NULL;

	if (cache->count < cache->cap) {
		struct aswl_ft_cached_glyph *g = &cache->glyphs[cache->count++];
		*g = (struct aswl_ft_cached_glyph){ 0 };
		return g;
	}

	size_t lru = 0;
	uint64_t best = UINT64_MAX;
	for (size_t i = 0; i < cache->count; i++) {
		if (cache->glyphs[i].last_used < best) {
			best = cache->glyphs[i].last_used;
			lru = i;
		}
	}

	free(cache->glyphs[lru].coverage);
	cache->glyphs[lru] = (struct aswl_ft_cached_glyph){ 0 };
	return &cache->glyphs[lru];
}

static struct aswl_ft_cached_glyph *aswl_ft_get_cached_glyph(struct aswl_font *font, uint32_t codepoint)
{
	if (font == NULL || font->ft_face == NULL)
		return NULL;

	struct aswl_ft_cache *cache = aswl_ft_cache_get(font);
	if (cache == NULL)
		return NULL;

	struct aswl_ft_cached_glyph *g = aswl_ft_cache_lookup(font, codepoint);
	if (g != NULL)
		return g;

	FT_Face face = (FT_Face)font->ft_face;
	g = aswl_ft_cache_alloc_slot(font);
	if (g == NULL)
		return NULL;

	FT_UInt glyph = FT_Get_Char_Index(face, (FT_ULong)codepoint);
	g->codepoint = codepoint;
	g->glyph_index = glyph;
	g->advance = 0;
	g->rendered = false;
	g->last_used = cache->tick++;

	if (FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT) == 0)
		g->advance = (int)(face->glyph->advance.x >> 6);

	return g;
}

static void aswl_ft_ensure_rendered(struct aswl_font *font, struct aswl_ft_cached_glyph *g)
{
	if (font == NULL || font->ft_face == NULL || g == NULL)
		return;
	if (g->rendered)
		return;

	FT_Face face = (FT_Face)font->ft_face;
	if (FT_Load_Glyph(face, g->glyph_index, FT_LOAD_DEFAULT) != 0) {
		g->rendered = true;
		return;
	}
	g->advance = (int)(face->glyph->advance.x >> 6);

	if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
		g->rendered = true;
		return;
	}

	FT_GlyphSlot slot = face->glyph;
	FT_Bitmap *bm = &slot->bitmap;

	g->bitmap_left = slot->bitmap_left;
	g->bitmap_top = slot->bitmap_top;
	g->width = (int)bm->width;
	g->rows = (int)bm->rows;

	free(g->coverage);
	g->coverage = NULL;

	if (g->width > 0 && g->rows > 0) {
		size_t sz = (size_t)g->width * (size_t)g->rows;
		g->coverage = malloc(sz);
		if (g->coverage != NULL) {
			int pitch = bm->pitch;
			const unsigned char *buf = bm->buffer;
			for (int row = 0; row < g->rows; row++) {
				const unsigned char *rowp = NULL;
				if (pitch >= 0)
					rowp = buf + (size_t)row * (size_t)pitch;
				else
					rowp = buf + (size_t)(g->rows - 1 - row) * (size_t)(-pitch);

				uint8_t *dst = g->coverage + (size_t)row * (size_t)g->width;
				if (bm->pixel_mode == FT_PIXEL_MODE_GRAY) {
					memcpy(dst, rowp, (size_t)g->width);
				} else if (bm->pixel_mode == FT_PIXEL_MODE_MONO) {
					for (int col = 0; col < g->width; col++) {
						uint8_t byte = rowp[col >> 3];
						uint8_t bit = (byte >> (7 - (col & 7))) & 1u;
						dst[col] = bit ? 255u : 0u;
					}
				} else {
					memset(dst, 0, (size_t)g->width);
				}
			}
		}
	}

	g->rendered = true;
}

static bool aswl_freetype_init(struct aswl_font *font)
{
	if (font == NULL)
		return false;

	FT_Library lib = NULL;
	if (FT_Init_FreeType(&lib) != 0)
		return false;

	font->ft_lib = lib;
	if (!aswl_ft_cache_init(font)) {
		FT_Done_FreeType(lib);
		font->ft_lib = NULL;
		return false;
	}
	return true;
}

static void aswl_freetype_destroy(struct aswl_font *font)
{
	if (font == NULL)
		return;

	if (font->ft_face != NULL) {
		FT_Done_Face((FT_Face)font->ft_face);
		font->ft_face = NULL;
	}
	if (font->ft_lib != NULL) {
		FT_Done_FreeType((FT_Library)font->ft_lib);
		font->ft_lib = NULL;
	}
	aswl_ft_cache_destroy(font);
	font->ft_has_kerning = false;
}

#ifdef HAVE_FONTCONFIG
static char *aswl_fontconfig_match_file(const char *pattern)
{
	if (pattern == NULL || pattern[0] == '\0')
		return NULL;

	if (!FcInit())
		return NULL;

	FcPattern *pat = FcNameParse((const FcChar8 *)pattern);
	if (pat == NULL)
		return NULL;

	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);

	FcResult result = FcResultNoMatch;
	FcPattern *match = FcFontMatch(NULL, pat, &result);
	FcPatternDestroy(pat);

	if (match == NULL)
		return NULL;

	FcChar8 *file = NULL;
	if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch || file == NULL) {
		FcPatternDestroy(match);
		return NULL;
	}

	char *out = strdup((const char *)file);
	FcPatternDestroy(match);
	return out;
}
#endif

static char *aswl_resolve_font_path(const char *spec, int *base_px_out)
{
	if (base_px_out != NULL)
		*base_px_out = 0;

	char *norm = aswl_normalize_font_spec(spec);
	if (norm == NULL)
		return NULL;

	if (base_px_out != NULL)
		*base_px_out = aswl_parse_trailing_px(norm);

	if (aswl_is_file_readable(norm))
		return norm;

	/*
	 * AfterStep looks frequently refer to bundled TTFs by basename plus optional "-<size>"
	 * suffix (e.g. "DefaultSans.ttf-16"). Resolve those against the AfterStep font dirs.
	 */
	char *as_font = aswl_try_afterstep_font_file(norm);
	if (as_font != NULL) {
		free(norm);
		return as_font;
	}

	size_t len = strlen(norm);
	size_t i = len;
	while (i > 0 && isdigit((unsigned char)norm[i - 1]))
		i--;
	if (i > 1 && i < len && norm[i - 1] == '-') {
		char *base = strndup(norm, i - 1);
		if (base != NULL) {
			as_font = aswl_try_afterstep_font_file(base);
			free(base);
			if (as_font != NULL) {
				free(norm);
				return as_font;
			}
		}
	}

#ifdef HAVE_FONTCONFIG
	char *file = aswl_fontconfig_match_file(norm);
	if (file != NULL) {
		free(norm);
		return file;
	}

	/*
	 * Common AfterStep convention is "Family-12" (size suffix). If fontconfig doesn't
	 * match it as-is, try stripping the trailing "-<digits>".
	 */
	size_t len2 = strlen(norm);
	size_t i2 = len2;
	while (i2 > 0 && isdigit((unsigned char)norm[i2 - 1]))
		i2--;
	if (i2 > 1 && i2 < len2 && norm[i2 - 1] == '-') {
		char *tmp = strndup(norm, i2 - 1);
		if (tmp != NULL) {
			file = aswl_fontconfig_match_file(tmp);
			free(tmp);
		}
		if (file != NULL) {
			free(norm);
			return file;
		}
	}
#endif

	free(norm);
	return NULL;
}

static int aswl_freetype_text_width_utf8_n(struct aswl_font *font,
                                          const char *s,
                                          size_t n,
                                          FT_UInt prev_glyph,
                                          FT_UInt *last_glyph_out)
{
	if (last_glyph_out != NULL)
		*last_glyph_out = 0;
	if (font == NULL || font->ft_face == NULL || s == NULL || n == 0)
		return 0;

	FT_Face face = (FT_Face)font->ft_face;
	int pen_x = 0;

	size_t i = 0;
	uint32_t cp = 0;
	while (aswl_utf8_decode_next(s, n, &i, &cp)) {
		struct aswl_ft_cached_glyph *g = aswl_ft_get_cached_glyph(font, cp);
		if (g == NULL)
			continue;

		FT_UInt glyph = g->glyph_index;
		if (font->ft_has_kerning && prev_glyph != 0 && glyph != 0) {
			FT_Vector delta = { 0 };
			if (FT_Get_Kerning(face, prev_glyph, glyph, FT_KERNING_DEFAULT, &delta) == 0)
				pen_x += (int)(delta.x >> 6);
		}

		pen_x += g->advance;
		prev_glyph = glyph;
	}

	if (last_glyph_out != NULL)
		*last_glyph_out = prev_glyph;
	return pen_x;
}

static size_t aswl_freetype_fit_bytes_utf8(struct aswl_font *font, const char *s, int max_w)
{
	if (font == NULL || font->ft_face == NULL || s == NULL || max_w <= 0)
		return 0;

	FT_Face face = (FT_Face)font->ft_face;
	size_t len = strlen(s);

	size_t i = 0;
	size_t bytes = 0;
	int w = 0;
	FT_UInt prev = 0;

	uint32_t cp = 0;
	while (aswl_utf8_decode_next(s, len, &i, &cp)) {
		struct aswl_ft_cached_glyph *g = aswl_ft_get_cached_glyph(font, cp);
		if (g == NULL)
			continue;

		int add = 0;
		if (font->ft_has_kerning && prev != 0 && g->glyph_index != 0) {
			FT_Vector delta = { 0 };
			if (FT_Get_Kerning(face, prev, g->glyph_index, FT_KERNING_DEFAULT, &delta) == 0)
				add += (int)(delta.x >> 6);
		}
		add += g->advance;

		if (w + add > max_w)
			break;

		w += add;
		prev = g->glyph_index;
		bytes = i;
	}

	return bytes;
}

static size_t aswl_freetype_fit_bytes_utf8_with_suffix(struct aswl_font *font,
                                                       const char *s,
                                                       int max_w,
                                                       const char *suffix,
                                                       size_t suffix_len)
{
	if (font == NULL || font->ft_face == NULL || s == NULL || suffix == NULL || suffix_len == 0 || max_w <= 0)
		return 0;

	int suffix_w = aswl_freetype_text_width_utf8_n(font, suffix, suffix_len, 0, NULL);
	if (suffix_w >= max_w)
		return aswl_freetype_fit_bytes_utf8(font, s, max_w);

	FT_Face face = (FT_Face)font->ft_face;
	size_t len = strlen(s);

	size_t i = 0;
	size_t bytes = 0;
	int w = 0;
	FT_UInt prev = 0;

	uint32_t cp = 0;
	while (aswl_utf8_decode_next(s, len, &i, &cp)) {
		struct aswl_ft_cached_glyph *g = aswl_ft_get_cached_glyph(font, cp);
		if (g == NULL)
			continue;

		int add = 0;
		if (font->ft_has_kerning && prev != 0 && g->glyph_index != 0) {
			FT_Vector delta = { 0 };
			if (FT_Get_Kerning(face, prev, g->glyph_index, FT_KERNING_DEFAULT, &delta) == 0)
				add += (int)(delta.x >> 6);
		}
		add += g->advance;

		int new_w = w + add;
		int suff_prev_w = aswl_freetype_text_width_utf8_n(font, suffix, suffix_len, g->glyph_index, NULL);
		if (new_w + suff_prev_w > max_w)
			break;

		w = new_w;
		prev = g->glyph_index;
		bytes = i;
	}

	return bytes;
}

static int aswl_freetype_draw_text_utf8_n(struct aswl_font *font,
                                         uint32_t *dst_argb,
                                         int dst_w,
                                         int dst_h,
                                         int dst_stride_px,
                                         int x,
                                         int y,
                                         const char *s,
                                         size_t n,
                                         uint32_t argb,
                                         FT_UInt prev_glyph,
                                         FT_UInt *last_glyph_out)
{
	if (last_glyph_out != NULL)
		*last_glyph_out = 0;
	if (font == NULL || font->ft_face == NULL || dst_argb == NULL || s == NULL || n == 0)
		return x;

	FT_Face face = (FT_Face)font->ft_face;
	int pen_x = x;
	int baseline_y = y + font->ascent;

	uint32_t base_a = (argb >> 24) & 0xFFu;
	uint32_t rgb = argb & 0x00FFFFFFu;

	size_t i = 0;
	uint32_t cp = 0;
	while (aswl_utf8_decode_next(s, n, &i, &cp)) {
		struct aswl_ft_cached_glyph *g = aswl_ft_get_cached_glyph(font, cp);
		if (g == NULL)
			continue;

		FT_UInt glyph = g->glyph_index;
		if (font->ft_has_kerning && prev_glyph != 0 && glyph != 0) {
			FT_Vector delta = { 0 };
			if (FT_Get_Kerning(face, prev_glyph, glyph, FT_KERNING_DEFAULT, &delta) == 0)
				pen_x += (int)(delta.x >> 6);
		}

		aswl_ft_ensure_rendered(font, g);

		int gx = pen_x + g->bitmap_left;
		int gy = baseline_y - g->bitmap_top;

		if (g->coverage != NULL && g->width > 0 && g->rows > 0) {
			for (int row = 0; row < g->rows; row++) {
				const uint8_t *rowp = g->coverage + (size_t)row * (size_t)g->width;
				for (int col = 0; col < g->width; col++) {
					uint8_t cov = rowp[col];
					if (cov == 0)
						continue;
					uint32_t a = (base_a * (uint32_t)cov) / 255u;
					aswl_blend_pixel(dst_argb,
					                 dst_w,
					                 dst_h,
					                 dst_stride_px,
					                 gx + col,
					                 gy + row,
					                 (a << 24) | rgb);
				}
			}
		}

		pen_x += g->advance;
		prev_glyph = glyph;
	}

	if (last_glyph_out != NULL)
		*last_glyph_out = prev_glyph;
	return pen_x;
}

#endif /* HAVE_FREETYPE */

void aswl_font_init(struct aswl_font *font)
{
	if (font == NULL)
		return;

	*font = (struct aswl_font){ 0 };
	font->scale = 2;
	font->ascent = aswl_font5x7_glyph_h(font->scale);
	font->descent = 0;
	font->height = aswl_font5x7_glyph_h(font->scale);
}

void aswl_font_destroy(struct aswl_font *font)
{
	if (font == NULL)
		return;

#ifdef HAVE_FREETYPE
	aswl_freetype_destroy(font);
#endif

	*font = (struct aswl_font){ 0 };
}

bool aswl_font_load(struct aswl_font *font, const char *spec)
{
	if (font == NULL)
		return false;

#ifdef HAVE_FREETYPE
	aswl_freetype_destroy(font);
#endif
	font->use_freetype = false;
	font->base_px = 0;

	if (spec == NULL || spec[0] == '\0' || strcmp(spec, "builtin") == 0 || strcmp(spec, "5x7") == 0)
		return true;

#ifdef HAVE_FREETYPE
	int base_px = 0;
	char *path = aswl_resolve_font_path(spec, &base_px);
	if (path == NULL)
		return false;

	if (!aswl_freetype_init(font)) {
		free(path);
		return false;
	}

	FT_Face face = NULL;
	if (FT_New_Face((FT_Library)font->ft_lib, path, 0, &face) != 0) {
		free(path);
		aswl_freetype_destroy(font);
		return false;
	}
	free(path);

	font->ft_face = face;
	font->ft_has_kerning = FT_HAS_KERNING(face) != 0;
	font->use_freetype = true;
	font->base_px = base_px;
	if (font->base_px > 0)
		font->scale = 1;

	/* Prime metrics for current scale. */
	(void)aswl_font_set_scale(font, font->scale);
	return true;
#else
	(void)spec;
	return false;
#endif
}

bool aswl_font_set_scale(struct aswl_font *font, int scale)
{
	if (font == NULL)
		return false;

	if (scale < 1)
		scale = 1;
	if (scale > 12)
		scale = 12;

	font->scale = scale;

	if (!font->use_freetype) {
		font->ascent = aswl_font5x7_glyph_h(scale);
		font->descent = 0;
		font->height = aswl_font5x7_glyph_h(scale);
		return true;
	}

	#ifdef HAVE_FREETYPE
	if (font->ft_face == NULL)
		return false;

	FT_Face face = (FT_Face)font->ft_face;
	int px = 0;
	if (font->base_px > 0)
		px = font->base_px * scale;
	else
		px = 7 * scale;
	if (px < 6)
		px = 6;

	if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px) != 0)
		return false;

	aswl_ft_cache_set_pixel_size(font, px);

	int asc = (int)(face->size->metrics.ascender >> 6);
	int desc = (int)(-(face->size->metrics.descender >> 6));
	int h = (int)(face->size->metrics.height >> 6);
	if (h <= 0)
		h = asc + desc;
	if (h <= 0)
		h = px;

	font->ascent = asc;
	font->descent = desc;
	font->height = h;
	return true;
#else
	return false;
#endif
}

int aswl_font_height(const struct aswl_font *font)
{
	if (font == NULL)
		return 0;
	return font->height;
}

int aswl_font_text_width_n(struct aswl_font *font, const char *s, size_t n)
{
	if (font == NULL || s == NULL || n == 0)
		return 0;

	if (!font->use_freetype)
		return as_font5x7_text_width_n(s, n, font->scale);

#ifdef HAVE_FREETYPE
	return aswl_freetype_text_width_utf8_n(font, s, n, 0, NULL);
#else
	return as_font5x7_text_width_n(s, n, font->scale);
#endif
}

int aswl_font_text_width(struct aswl_font *font, const char *s)
{
	if (font == NULL || s == NULL)
		return 0;
	return aswl_font_text_width_n(font, s, strlen(s));
}

void aswl_font_draw_text(struct aswl_font *font,
                         uint32_t *dst_argb,
                         int dst_w,
                         int dst_h,
                         int dst_stride_px,
                         int x,
                         int y,
                         const char *s,
                         int max_w,
                         uint32_t argb)
{
	if (font == NULL || dst_argb == NULL)
		return;
	if (s == NULL || s[0] == '\0')
		return;

	size_t n = strlen(s);
	if (!font->use_freetype) {
		as_font5x7_draw_text(dst_argb,
		                     dst_w,
		                     dst_h,
		                     dst_stride_px,
		                     x,
		                     y,
		                     s,
		                     max_w,
		                     font->scale,
		                     argb);
		return;
	}

#ifdef HAVE_FREETYPE
	size_t draw_n = n;
	bool need_ellipsis = false;

	int full_w = aswl_freetype_text_width_utf8_n(font, s, n, 0, NULL);
	if (max_w > 0 && full_w > max_w) {
		int ell_w = aswl_freetype_text_width_utf8_n(font, "...", 3, 0, NULL);
		if (ell_w < max_w) {
			draw_n = aswl_freetype_fit_bytes_utf8_with_suffix(font, s, max_w, "...", 3);
			need_ellipsis = true;
		} else {
			draw_n = aswl_freetype_fit_bytes_utf8(font, s, max_w);
		}
	}

	FT_UInt last_glyph = 0;
	int pen_x = x;
	if (draw_n > 0)
		pen_x = aswl_freetype_draw_text_utf8_n(font, dst_argb, dst_w, dst_h, dst_stride_px, x, y, s, draw_n, argb, 0, &last_glyph);

	if (need_ellipsis)
		(void)aswl_freetype_draw_text_utf8_n(font, dst_argb, dst_w, dst_h, dst_stride_px, pen_x, y, "...", 3, argb, last_glyph, NULL);
#else
	as_font5x7_draw_text(dst_argb, dst_w, dst_h, dst_stride_px, x, y, s, max_w, font->scale, argb);
#endif
}
