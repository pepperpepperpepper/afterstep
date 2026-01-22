#ifndef ASFONT_INTERNAL_H_INCLUDED
#define ASFONT_INTERNAL_H_INCLUDED

#include "asfont.h"

struct ASFont *asfont_open_freetype_font_internal(struct ASFontManager *fontman,
												  const char *font_string,
												  int face_no,
												  int size,
												  Bool verbose,
												  ASFlagType flags);

struct ASFont *asfont_open_X11_font_internal(struct ASFontManager *fontman,
											 const char *font_string,
											 ASFlagType flags);

ASGlyph *asfont_freetype_load_locale_glyph(struct ASFont *font, UNICODE_CHAR uc);

unsigned char *compress_glyph_pixmap(unsigned char *src,
									 unsigned char *buffer,
									 unsigned int width,
									 unsigned int height,
									 int src_step);

void scale_down_glyph_width(unsigned char *buffer,
							int from_width,
							int to_width,
							int height);

#endif /* ASFONT_INTERNAL_H_INCLUDED */
