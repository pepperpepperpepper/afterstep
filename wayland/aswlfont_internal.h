#ifndef ASWL_FONT_INTERNAL_H
#define ASWL_FONT_INTERNAL_H

#include "aswlfont.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool aswl_utf8_decode_next(const char *s, size_t n, size_t *i, uint32_t *cp_out);

int aswl_font5x7_text_width_n(const char *s, size_t n, int scale);
size_t aswl_font5x7_fit_bytes(const char *s, int scale, int max_w);

bool aswl_font_backend_load(struct aswl_font *font, const char *spec);
void aswl_font_backend_destroy(struct aswl_font *font);
bool aswl_font_backend_set_scale(struct aswl_font *font, int scale);

#ifdef HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H

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

struct aswl_ft_cached_glyph *aswl_ft_get_cached_glyph(struct aswl_font *font, uint32_t codepoint);
void aswl_ft_ensure_rendered(struct aswl_font *font, struct aswl_ft_cached_glyph *g);

int aswl_freetype_text_width_utf8_n(struct aswl_font *font,
                                    const char *s,
                                    size_t n,
                                    FT_UInt prev_glyph,
                                    FT_UInt *last_glyph_out);
size_t aswl_freetype_fit_bytes_utf8(struct aswl_font *font, const char *s, int max_w);
size_t aswl_freetype_fit_bytes_utf8_with_suffix(struct aswl_font *font,
                                                const char *s,
                                                int max_w,
                                                const char *suffix,
                                                size_t suffix_len);
#endif /* HAVE_FREETYPE */

#endif /* ASWL_FONT_INTERNAL_H */

