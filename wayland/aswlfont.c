#include "aswlfont_internal.h"

#include <string.h>

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
	int full_w = aswl_font5x7_text_width_n(s, n, scale);
	if (max_w > 0 && full_w > max_w) {
		int ell_w = aswl_font5x7_text_width_n("...", 3, scale);
		if (ell_w < max_w) {
			draw_n = aswl_font5x7_fit_bytes(s, scale, max_w - ell_w);
			need_ellipsis = true;
		} else {
			draw_n = aswl_font5x7_fit_bytes(s, scale, max_w);
		}
	}

	int cx = x;
	int glyph_w = aswl_font5x7_glyph_w(scale);
	int spacing = scale;

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

	aswl_font_backend_destroy(font);

	*font = (struct aswl_font){ 0 };
}

bool aswl_font_load(struct aswl_font *font, const char *spec)
{
	if (font == NULL)
		return false;

	aswl_font_backend_destroy(font);
	font->use_freetype = false;
	font->base_px = 0;

	if (spec == NULL || spec[0] == '\0' || strcmp(spec, "builtin") == 0 || strcmp(spec, "5x7") == 0)
		return true;

	if (!aswl_font_backend_load(font, spec))
		return false;

	if (font->base_px > 0)
		font->scale = 1;

	/* Prime metrics for current scale. */
	(void)aswl_font_set_scale(font, font->scale);
	return true;
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

	return aswl_font_backend_set_scale(font, scale);
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
		return aswl_font5x7_text_width_n(s, n, font->scale);

#ifdef HAVE_FREETYPE
	return aswl_freetype_text_width_utf8_n(font, s, n, 0, NULL);
#else
	return aswl_font5x7_text_width_n(s, n, font->scale);
#endif
}

int aswl_font_text_width(struct aswl_font *font, const char *s)
{
	if (font == NULL || s == NULL)
		return 0;
	return aswl_font_text_width_n(font, s, strlen(s));
}

static int aswl_text_style_sanitize(int text_style)
{
	if (text_style < 0 || text_style > 9)
		return 0;
	return text_style;
}

static int aswl_text_style_extra_px(int text_style)
{
	switch (aswl_text_style_sanitize(text_style)) {
	case 1: /* embossed */
	case 2: /* sunken */
	case 9: /* outline full */
		return 2;
	case 3: /* shade above */
	case 4: /* shade below */
	case 5: /* embossed thick */
	case 6: /* sunken thick */
		return 3;
	case 7: /* outline above */
	case 8: /* outline below */
		return 1;
	default:
		return 0;
	}
}

int aswl_font_height_styled(const struct aswl_font *font, int text_style)
{
	if (font == NULL)
		return 0;
	return aswl_font_height(font) + aswl_text_style_extra_px(text_style);
}

#ifdef HAVE_FREETYPE
static int aswl_freetype_text_width_utf8_n_styled(struct aswl_font *font,
                                                  const char *s,
                                                  size_t n,
                                                  FT_UInt prev_glyph,
                                                  FT_UInt *last_glyph_out,
                                                  int extra_dx)
{
	if (last_glyph_out != NULL)
		*last_glyph_out = 0;
	if (font == NULL || font->ft_face == NULL || s == NULL || n == 0)
		return 0;

	if (extra_dx < 0)
		extra_dx = 0;

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

		pen_x += g->advance + extra_dx;
		prev_glyph = glyph;
	}

	if (last_glyph_out != NULL)
		*last_glyph_out = prev_glyph;
	return pen_x;
}

static size_t aswl_freetype_fit_bytes_utf8_styled(struct aswl_font *font, const char *s, int max_w, int extra_dx)
{
	if (font == NULL || font->ft_face == NULL || s == NULL || max_w <= 0)
		return 0;
	if (extra_dx < 0)
		extra_dx = 0;

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
		add += g->advance + extra_dx;

		if (w + add > max_w)
			break;

		w += add;
		prev = g->glyph_index;
		bytes = i;
	}

	return bytes;
}

static size_t aswl_freetype_fit_bytes_utf8_with_suffix_styled(struct aswl_font *font,
                                                              const char *s,
                                                              int max_w,
                                                              const char *suffix,
                                                              size_t suffix_len,
                                                              int extra_dx)
{
	if (font == NULL || font->ft_face == NULL || s == NULL || suffix == NULL || suffix_len == 0 || max_w <= 0)
		return 0;

	int suffix_w = aswl_freetype_text_width_utf8_n_styled(font, suffix, suffix_len, 0, NULL, extra_dx);
	if (suffix_w >= max_w)
		return aswl_freetype_fit_bytes_utf8_styled(font, s, max_w, extra_dx);

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
		add += g->advance + extra_dx;

		int new_w = w + add;
		int suff_prev_w = aswl_freetype_text_width_utf8_n_styled(font, suffix, suffix_len, g->glyph_index, NULL, extra_dx);
		if (new_w + suff_prev_w > max_w)
			break;

		w = new_w;
		prev = g->glyph_index;
		bytes = i;
	}

	return bytes;
}
#endif /* HAVE_FREETYPE */

int aswl_font_text_width_styled_n(struct aswl_font *font, const char *s, size_t n, int text_style)
{
	if (font == NULL || s == NULL || n == 0)
		return 0;

	int extra = aswl_text_style_extra_px(text_style);

	if (!font->use_freetype) {
		/* Built-in: approximate by adding the effect margin per glyph. */
		int base = aswl_font5x7_text_width_n(s, n, font->scale);
		size_t i = 0;
		uint32_t cp = 0;
		int glyphs = 0;
		while (aswl_utf8_decode_next(s, n, &i, &cp))
			glyphs++;
		return base + extra * glyphs;
	}

#ifdef HAVE_FREETYPE
	return aswl_freetype_text_width_utf8_n_styled(font, s, n, 0, NULL, extra);
#else
	int base = aswl_font5x7_text_width_n(s, n, font->scale);
	size_t i = 0;
	uint32_t cp = 0;
	int glyphs = 0;
	while (aswl_utf8_decode_next(s, n, &i, &cp))
		glyphs++;
	return base + extra * glyphs;
#endif
}

int aswl_font_text_width_styled(struct aswl_font *font, const char *s, int text_style)
{
	if (font == NULL || s == NULL)
		return 0;
	return aswl_font_text_width_styled_n(font, s, strlen(s), text_style);
}
static uint32_t aswl_text_contrast_rgb24(uint32_t fg_argb)
{
	uint32_t r = (fg_argb >> 16) & 0xFFu;
	uint32_t g = (fg_argb >> 8) & 0xFFu;
	uint32_t b = fg_argb & 0xFFu;
	/* Match libAfterImage's contrast heuristic (roughly midpoint luminance). */
	uint32_t y = r * 222u + g * 707u + b * 71u;
	if (y < 127500u)
		return 0x00FFFFFFu; /* white outline for dark text */
	return 0x00000000u; /* black outline for light text */
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

#ifdef HAVE_FREETYPE
/*
 * Accumulate one TextStyle pass into scratch buffers, matching AfterStep's
 * asfont.c model: glyph coverage is composited into the alpha buffer with
 * MAX/lighten (render_asglyph: dst = max(dst, scaled)), NOT alpha-over. For
 * the outline body pass, the fore color is composited OVER a pre-filled
 * backing color in a separate RGB buffer (render_asglyph_over), weighted by
 * the raw glyph coverage. Scratch (0,0) maps to the caller's (x,y); this pass
 * is placed at (off_x, off_y). Returns the pen x for ellipsis chaining.
 */
static int aswl_styled_accumulate_layer(struct aswl_font *font,
                                        const char *s, size_t n,
                                        int off_x, int off_y,
                                        uint8_t alpha_scale, int extra_dx,
                                        uint8_t *cov, uint32_t *colbuf, int bw, int bh,
                                        bool body, uint32_t fore_rgb,
                                        FT_UInt prev_glyph, FT_UInt *last_glyph_out)
{
	if (last_glyph_out != NULL)
		*last_glyph_out = 0;
	if (font == NULL || font->ft_face == NULL || s == NULL || n == 0)
		return off_x;
	if (extra_dx < 0)
		extra_dx = 0;

	FT_Face face = (FT_Face)font->ft_face;
	int pen_x = off_x;
	int baseline_y = off_y + font->ascent;

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
				int sy = gy + row;
				if (sy < 0 || sy >= bh)
					continue;
				const uint8_t *rowp = g->coverage + (size_t)row * (size_t)g->width;
				for (int col = 0; col < g->width; col++) {
					uint8_t gcov = rowp[col];
					if (gcov == 0)
						continue;
					int sx = gx + col;
					if (sx < 0 || sx >= bw)
						continue;
					size_t idx = (size_t)sy * (size_t)bw + (size_t)sx;
					uint8_t scov = (uint8_t)(((uint32_t)gcov * alpha_scale) / 255u);
					if (scov > cov[idx])
						cov[idx] = scov; /* MAX coverage (render_asglyph) */
					if (body && colbuf != NULL) {
						/* fore OVER backing, weighted by raw glyph coverage */
						uint32_t bgp = colbuf[idx];
						uint32_t w = gcov;
						uint32_t iw = 255u - w;
						uint32_t r = ((((fore_rgb >> 16) & 0xFFu) * w) + (((bgp >> 16) & 0xFFu) * iw) + 127u) / 255u;
						uint32_t gg = ((((fore_rgb >> 8) & 0xFFu) * w) + (((bgp >> 8) & 0xFFu) * iw) + 127u) / 255u;
						uint32_t b = (((fore_rgb & 0xFFu) * w) + ((bgp & 0xFFu) * iw) + 127u) / 255u;
						colbuf[idx] = (r << 16) | (gg << 8) | b;
					}
				}
			}
		}
		pen_x += g->advance + extra_dx;
		prev_glyph = glyph;
	}
	if (last_glyph_out != NULL)
		*last_glyph_out = prev_glyph;
	return pen_x;
}
#endif /* HAVE_FREETYPE */

void aswl_font_draw_text_styled(struct aswl_font *font,
                                uint32_t *dst_argb,
                                int dst_w,
                                int dst_h,
                                int dst_stride_px,
                                int x,
                                int y,
                                const char *s,
                                int max_w,
                                uint32_t argb,
                                int text_style)
{
	if (font == NULL || dst_argb == NULL)
		return;
	if (s == NULL || s[0] == '\0')
		return;

	int style = aswl_text_style_sanitize(text_style);
	if (style == 0) {
		aswl_font_draw_text(font, dst_argb, dst_w, dst_h, dst_stride_px, x, y, s, max_w, argb);
		return;
	}

	if (!font->use_freetype) {
		/*
		 * Styling is only needed for AfterStep look parity (TTF fonts). If we
		 * fell back to the built-in font, just draw plain to avoid surprises.
		 */
		as_font5x7_draw_text(dst_argb, dst_w, dst_h, dst_stride_px, x, y, s, max_w, font->scale, argb);
		return;
	}

#ifdef HAVE_FREETYPE
	size_t n = strlen(s);
	size_t draw_n = n;
	bool need_ellipsis = false;

	int extra = aswl_text_style_extra_px(style);

	int full_w = aswl_freetype_text_width_utf8_n_styled(font, s, n, 0, NULL, extra);
	if (max_w > 0 && full_w > max_w) {
		int ell_w = aswl_freetype_text_width_utf8_n_styled(font, "...", 3, 0, NULL, extra);
		if (ell_w < max_w) {
			draw_n = aswl_freetype_fit_bytes_utf8_with_suffix_styled(font, s, max_w, "...", 3, extra);
			need_ellipsis = true;
		} else {
			draw_n = aswl_freetype_fit_bytes_utf8_styled(font, s, max_w, extra);
		}
	}

	struct aswl_text_layer {
		int dx;
		int dy;
		uint8_t alpha_scale;
		bool contrast;
	};
	struct aswl_text_layer layers[4];
	size_t layer_count = 0;

	switch (style) {
	case 1: /* embossed */
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 0, .dy = 0, .alpha_scale = 0xFF, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 2, .dy = 2, .alpha_scale = 0x9F, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 1, .dy = 1, .alpha_scale = 0xCF, .contrast = false };
		break;
	case 2: /* sunken */
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 0, .dy = 0, .alpha_scale = 0x9F, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 2, .dy = 2, .alpha_scale = 0xFF, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 1, .dy = 1, .alpha_scale = 0xCF, .contrast = false };
		break;
	case 3: /* shade above */
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 0, .dy = 0, .alpha_scale = 0x7F, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 3, .dy = 3, .alpha_scale = 0xFF, .contrast = false };
		break;
	case 4: /* shade below */
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 3, .dy = 3, .alpha_scale = 0x7F, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 0, .dy = 0, .alpha_scale = 0xFF, .contrast = false };
		break;
	case 5: /* embossed thick */
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 0, .dy = 0, .alpha_scale = 0xFF, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 1, .dy = 1, .alpha_scale = 0xEF, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 3, .dy = 3, .alpha_scale = 0x7F, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 2, .dy = 2, .alpha_scale = 0xCF, .contrast = false };
		break;
	case 6: /* sunken thick */
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 0, .dy = 0, .alpha_scale = 0x7F, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 1, .dy = 1, .alpha_scale = 0xAF, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 3, .dy = 3, .alpha_scale = 0xFF, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 2, .dy = 2, .alpha_scale = 0xCF, .contrast = false };
		break;
	case 7: /* outline above */
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 0, .dy = 0, .alpha_scale = 0xAF, .contrast = true };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 1, .dy = 1, .alpha_scale = 0xFF, .contrast = false };
		break;
	case 8: /* outline below */
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 0, .dy = 0, .alpha_scale = 0xFF, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 1, .dy = 1, .alpha_scale = 0xAF, .contrast = true };
		break;
	case 9: /* outline full */
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 0, .dy = 0, .alpha_scale = 0xAF, .contrast = true };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 1, .dy = 1, .alpha_scale = 0xFF, .contrast = false };
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 2, .dy = 2, .alpha_scale = 0xAF, .contrast = true };
		break;
	default:
		layers[layer_count++] = (struct aswl_text_layer){ .dx = 0, .dy = 0, .alpha_scale = 0xFF, .contrast = false };
		break;
	}

	/*
	 * AfterStep composites all TextStyle passes into a single coverage (alpha)
	 * buffer with MAX/lighten, then colors it once. Replicate that: accumulate
	 * the passes into a scratch (cov = MAX; outline body color OVER a backing
	 * fill in colbuf), then alpha-over the result onto dst a single time. This
	 * avoids the over-darkening that per-pass alpha-over produced at the
	 * overlapping, antialiased glyph edges.
	 */
	bool is_outline = (style >= 7); /* 7/8/9 carry a contrasting backing color */
	uint32_t fore_rgb = argb & 0x00FFFFFFu;
	uint32_t base_a = (argb >> 24) & 0xFFu;

	int maxoff = 0;
	for (size_t li = 0; li < layer_count; li++) {
		if (layers[li].dx > maxoff)
			maxoff = layers[li].dx;
		if (layers[li].dy > maxoff)
			maxoff = layers[li].dy;
	}

	int text_w = aswl_freetype_text_width_utf8_n_styled(font, s, draw_n, 0, NULL, extra);
	int ell_w = need_ellipsis ? aswl_freetype_text_width_utf8_n_styled(font, "...", 3, 0, NULL, extra) : 0;
	int bw = text_w + ell_w + maxoff + 2;
	int bh = aswl_font_height(font) + extra + maxoff + 2;
	if (bw < 1)
		bw = 1;
	if (bh < 1)
		bh = 1;
	if (bw > 8192)
		bw = 8192;
	if (bh > 1024)
		bh = 1024;

	uint8_t *cov = calloc((size_t)bw * (size_t)bh, 1);
	if (cov == NULL)
		return;
	uint32_t *colbuf = NULL;
	if (is_outline) {
		uint32_t backing_rgb = aswl_text_contrast_rgb24(argb);
		colbuf = malloc((size_t)bw * (size_t)bh * sizeof(*colbuf));
		if (colbuf != NULL) {
			for (size_t p = 0; p < (size_t)bw * (size_t)bh; p++)
				colbuf[p] = backing_rgb;
		}
	}

	for (size_t li = 0; li < layer_count; li++) {
		bool body = is_outline ? !layers[li].contrast : true;
		FT_UInt last_glyph = 0;
		int pen_x = aswl_styled_accumulate_layer(font, s, draw_n,
		                                         layers[li].dx, layers[li].dy,
		                                         layers[li].alpha_scale, extra,
		                                         cov, colbuf, bw, bh,
		                                         body, fore_rgb, 0, &last_glyph);
		if (need_ellipsis)
			(void)aswl_styled_accumulate_layer(font, "...", 3,
			                                   pen_x, layers[li].dy,
			                                   layers[li].alpha_scale, extra,
			                                   cov, colbuf, bw, bh,
			                                   body, fore_rgb, last_glyph, NULL);
	}

	for (int py = 0; py < bh; py++) {
		for (int px = 0; px < bw; px++) {
			size_t idx = (size_t)py * (size_t)bw + (size_t)px;
			uint8_t c = cov[idx];
			if (c == 0)
				continue;
			uint32_t rgb = (colbuf != NULL) ? colbuf[idx] : fore_rgb;
			uint32_t a = (base_a * (uint32_t)c) / 255u;
			aswl_blend_pixel(dst_argb, dst_w, dst_h, dst_stride_px, x + px, y + py, (a << 24) | rgb);
		}
	}

	free(cov);
	free(colbuf);
#else
	as_font5x7_draw_text(dst_argb, dst_w, dst_h, dst_stride_px, x, y, s, max_w, font->scale, argb);
#endif /* HAVE_FREETYPE */
}
