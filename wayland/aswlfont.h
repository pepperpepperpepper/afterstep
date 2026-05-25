#ifndef ASWL_FONT_H
#define ASWL_FONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct aswl_font {
	bool use_freetype;
	int scale;
	int base_px;
	int ascent;
	int descent;
	int height;

#ifdef HAVE_FREETYPE
	void *ft_lib;
	void *ft_face;
	bool ft_has_kerning;
	void *ft_cache;
#endif
};

void aswl_font_init(struct aswl_font *font);
void aswl_font_destroy(struct aswl_font *font);

/*
 * Loads a font spec into the font object.
 *
 * Supported specs:
 * - NULL/empty/"5x7"/"builtin": use the built-in 5x7 font.
 * - A readable font file path (TTF/OTF/etc): use FreeType if compiled in.
 * - A fontconfig pattern (e.g. "DejaVu Sans"): resolved via fontconfig if available.
 *
 * Returns true if a FreeType-backed font is active (or builtin selected),
 * false if the spec failed and we fell back to builtin.
 */
bool aswl_font_load(struct aswl_font *font, const char *spec);

/* Sets the logical "UI scale". Built-in uses it directly; FreeType maps it to pixels. */
bool aswl_font_set_scale(struct aswl_font *font, int scale);

int aswl_font_height(const struct aswl_font *font);
int aswl_font_text_width_n(struct aswl_font *font, const char *s, size_t n);
int aswl_font_text_width(struct aswl_font *font, const char *s);

/* AfterStep-compatible TextStyle rendering (0..9). */
int aswl_font_height_styled(const struct aswl_font *font, int text_style);
int aswl_font_text_width_styled_n(struct aswl_font *font, const char *s, size_t n, int text_style);
int aswl_font_text_width_styled(struct aswl_font *font, const char *s, int text_style);

void aswl_font_draw_text(struct aswl_font *font,
                         uint32_t *dst_argb,
                         int dst_w,
                         int dst_h,
                         int dst_stride_px,
                         int x,
                         int y,
                         const char *s,
                         int max_w,
                         uint32_t argb);

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
                                int text_style);

/* Built-in 5x7 helpers (used for icon placeholders/badges). */
int aswl_font5x7_glyph_w(int scale);
int aswl_font5x7_glyph_h(int scale);
void aswl_font5x7_draw_glyph(uint32_t *dst_argb,
                             int dst_w,
                             int dst_h,
                             int dst_stride_px,
                             int x,
                             int y,
                             char c,
                             int scale,
                             uint32_t argb);

#endif
