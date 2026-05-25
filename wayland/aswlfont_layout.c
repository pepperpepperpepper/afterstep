#include "aswlfont_internal.h"

#include <string.h>

bool aswl_utf8_decode_next(const char *s, size_t n, size_t *i, uint32_t *cp_out)
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

static int as_font5x7_spacing(int scale)
{
	return scale;
}

int aswl_font5x7_text_width_n(const char *s, size_t n, int scale)
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

size_t aswl_font5x7_fit_bytes(const char *s, int scale, int max_w)
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

#ifdef HAVE_FREETYPE

int aswl_freetype_text_width_utf8_n(struct aswl_font *font,
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

size_t aswl_freetype_fit_bytes_utf8(struct aswl_font *font, const char *s, int max_w)
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

size_t aswl_freetype_fit_bytes_utf8_with_suffix(struct aswl_font *font,
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

#endif /* HAVE_FREETYPE */

