#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlfont_internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#ifdef HAVE_FREETYPE

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

struct aswl_ft_cached_glyph *aswl_ft_get_cached_glyph(struct aswl_font *font, uint32_t codepoint)
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

void aswl_ft_ensure_rendered(struct aswl_font *font, struct aswl_ft_cached_glyph *g)
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

#endif /* HAVE_FREETYPE */

bool aswl_font_backend_load(struct aswl_font *font, const char *spec)
{
#ifdef HAVE_FREETYPE
	if (font == NULL || spec == NULL || spec[0] == '\0')
		return false;

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
	return true;
#else
	(void)font;
	(void)spec;
	return false;
#endif
}

void aswl_font_backend_destroy(struct aswl_font *font)
{
#ifdef HAVE_FREETYPE
	aswl_freetype_destroy(font);
#else
	(void)font;
#endif
}

bool aswl_font_backend_set_scale(struct aswl_font *font, int scale)
{
#ifdef HAVE_FREETYPE
	if (font == NULL)
		return false;
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
	(void)font;
	(void)scale;
	return false;
#endif
}

