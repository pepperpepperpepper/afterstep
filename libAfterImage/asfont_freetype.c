/*
 * Copyright (c) 2001 Sasha Vasko <sasha at aftercode.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#undef LOCAL_DEBUG

#ifdef _WIN32
#include "win32/config.h"
#else
#include "config.h"
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <ctype.h>
#include <string.h>

#ifdef HAVE_FREETYPE
# ifdef HAVE_FT2BUILD_H
#  include <ft2build.h>
#  include FT_FREETYPE_H
# endif
# ifdef HAVE_FREETYPE_FREETYPE
#  include <freetype/freetype.h>
# else
#  include <freetype.h>
# endif
# if (FREETYPE_MAJOR == 2) && ((FREETYPE_MINOR == 0) || ((FREETYPE_MINOR == 1) && (FREETYPE_PATCH < 3)))
#  define FT_KERNING_DEFAULT ft_kerning_default
# endif
#endif

#ifdef _WIN32
# include "win32/afterbase.h"
#else
# include "afterbase.h"
#endif

#include "asfont_internal.h"

#undef MAX_GLYPHS_PER_FONT

void asglyph_destroy (ASHashableValue value, void *data);

#ifdef HAVE_FREETYPE
static int load_freetype_glyphs( ASFont *font );
#endif

ASFont *
asfont_open_freetype_font_internal( ASFontManager *fontman, const char *font_string, int face_no, int size, Bool verbose, ASFlagType flags)
{
	ASFont *font = NULL ;
#ifdef HAVE_FREETYPE
	if( fontman && fontman->ft_ok )
	{
		char *realfilename;
		FT_Face face ;
		LOCAL_DEBUG_OUT( "looking for \"%s\" in \"%s\"", font_string, fontman->font_path );
		if( (realfilename = find_file( font_string, fontman->font_path, R_OK )) == NULL )
		{/* we might have face index specifier at the end of the filename */
			char *tmp = mystrdup( font_string );
			register int i = 0;
			while(tmp[i] != '\0' ) ++i ;
			while( --i >= 0 )
				if( !isdigit( tmp[i] ) )
				{
					if( tmp[i] == '.' )
					{
						face_no = atoi( &tmp[i+1] );
						tmp[i] = '\0' ;
					}
					break;
				}
			if( i >= 0 && font_string[i] != '\0' )
				realfilename = find_file( tmp, fontman->font_path, R_OK );
			free( tmp );
		}

		if( realfilename )
		{
			face = NULL ;
LOCAL_DEBUG_OUT( "font file found : \"%s\", trying to load face #%d, using library %p", realfilename, face_no, fontman->ft_library );
			if( FT_New_Face( fontman->ft_library, realfilename, face_no, &face ) )
			{
LOCAL_DEBUG_OUT( "face load failed.%s", "" );

				if( face_no  > 0  )
				{
					show_warning( "face %d is not available in font \"%s\" - falling back to first available.", face_no, realfilename );
					FT_New_Face( fontman->ft_library, realfilename, 0, &face );
				}
			}
LOCAL_DEBUG_OUT( "face found : %p", face );
			if( face != NULL )
			{
#ifdef MAX_GLYPHS_PER_FONT
				if( face->num_glyphs >  MAX_GLYPHS_PER_FONT )
					show_error( "Font \"%s\" contains too many glyphs - %d. Max allowed is %d", realfilename, face->num_glyphs, MAX_GLYPHS_PER_FONT );
				else
#endif
				{
					font = safecalloc( 1, sizeof(ASFont));
					font->magic = MAGIC_ASFONT ;
					font->fontman = fontman;
					font->type = ASF_Freetype ;
					font->flags = flags ;
					font->ft_face = face ;
					if( FT_HAS_KERNING( face ) )
					{
						set_flags( font->flags, ASF_HasKerning );
						/* fprintf( stderr, "@@@@@@@font %s has kerning!!!\n", realfilename );*/
					}/*else
						fprintf( stderr, "@@@@@@@font %s don't have kerning!!!\n", realfilename ); */
					/* lets confine the font to square cell */
					FT_Set_Pixel_Sizes( font->ft_face, size, size );
					/* but let make our own cell width smaller then height */
					font->space_size = size*2/3 ;
	   				load_freetype_glyphs( font );
				}
			}else if( verbose )
				show_error( "FreeType library failed to load font \"%s\"", realfilename );

			if( realfilename != font_string )
				free( realfilename );
		}
	}
#endif
	return font;
}

ASFont *
open_freetype_font( ASFontManager *fontman, const char *font_string, int face_no, int size, Bool verbose)
{
	return asfont_open_freetype_font_internal( fontman, font_string, face_no, size, verbose, 0);
}

#ifdef HAVE_FREETYPE

static void
load_glyph_freetype( ASFont *font, ASGlyph *asg, int glyph, UNICODE_CHAR uc )
{
	register FT_Face face ;
	static CARD8 *glyph_compress_buf = NULL, *glyph_scaling_buf = NULL ;
	static int glyph_compress_buf_size = 0, glyph_scaling_buf_size = 0;

	if( font == NULL )
	{
		if( glyph_compress_buf )
		{
			free( glyph_compress_buf );
			glyph_compress_buf = NULL ;
		}
		if( glyph_scaling_buf )
		{
			free( glyph_scaling_buf );
			glyph_scaling_buf = NULL ;
		}
		glyph_compress_buf_size = 0 ;
		glyph_scaling_buf_size = 0 ;
		return;
	}

	face = font->ft_face;
	if( FT_Load_Glyph( face, glyph, FT_LOAD_DEFAULT ) )
		return;

	if( FT_Render_Glyph( face->glyph, ft_render_mode_normal ) )
		return;

	if( face->glyph->bitmap.buffer )
	{
		FT_Bitmap 	*bmap = &(face->glyph->bitmap) ;
		register CARD8 *src = bmap->buffer ;
		int src_step ;
/* 		int hpad = (face->glyph->bitmap_left<0)? -face->glyph->bitmap_left: face->glyph->bitmap_left ;
*/
		asg->font_gid = glyph ;
		asg->width   = bmap->width ;
		asg->height  = bmap->rows ;
		/* Combining Diacritical Marks : */
		if( uc >= 0x0300 && uc <= 0x0362 )
			asg->step = 0 ;
		else
			asg->step = (short)face->glyph->advance.x>>6 ;

		/* we only want to keep lead if it was negative */
		if( uc >= 0x0300 && uc <= 0x0362 && face->glyph->bitmap_left >= 0 )
			asg->lead    = -((int)font->space_size - (int)face->glyph->bitmap_left) ;
		else
			asg->lead    = face->glyph->bitmap_left;

		if( bmap->pitch < 0 )
			src += -bmap->pitch*bmap->rows ;
		src_step = bmap->pitch ;

		/* TODO: insert monospaced adjastments here */
		if( get_flags( font->flags, ASF_Monospaced ) && ( uc < 0x0300 || uc > 0x0362 ) )
		{
			if( asg->lead < 0 )
			{
				if( asg->lead < -(int)font->space_size/8 )
					asg->lead = -(int)font->space_size/8 ;
				if( (int)asg->width + asg->lead <= (int)font->space_size )
				{
					asg->lead = (int)font->space_size - (int)asg->width ;
					if( asg->lead > 0 )
						asg->lead /= 2 ;
				}
			}else
			{
				if( (int)asg->width + (int)asg->lead > (int)font->space_size )
				{
					if( asg->lead > (int)font->space_size/8 )
						asg->lead = (int)font->space_size/8 ;
				}else                          /* centering the glyph : */
					asg->lead += ((int)font->space_size - ((int)asg->width+asg->lead))/2 ;
			}
			if( (int)asg->width + asg->lead > (int)font->space_size )
			{
				register CARD8 *buf ;
				int i ;
				asg->width = (int)font->space_size - asg->lead ;
				if( glyph_scaling_buf_size  < bmap->width*bmap->rows*2 )
				{
					glyph_scaling_buf_size = bmap->width*bmap->rows*2;
					glyph_scaling_buf = realloc( glyph_scaling_buf, glyph_scaling_buf_size );
				}
				buf = &(glyph_scaling_buf[0]);
				for( i = 0 ; i < bmap->rows ; ++i )
				{
					int k = bmap->width;
					while( --k >= 0 )
						buf[k] = src[k] ;
					buf += bmap->width ;
					src += src_step ;
				}
				src = &(glyph_scaling_buf[0]);
				scale_down_glyph_width( src, bmap->width, asg->width, asg->height );
				src_step = asg->width ;
/*					fprintf(stderr, "lead = %d, space_size = %d, width = %d, to_width = %d\n",
						r->glyphs[i].lead, font->space_size, width, r->glyphs[i].width ); */
			}
			/*else
			{
				fprintf(stderr, "lead = %d, space_size = %d, width = %d\n",
						r->glyphs[i].lead, font->space_size, width );
			}	 */
			asg->step = font->space_size ;
		}


		if( glyph_compress_buf_size  < asg->width*asg->height*3 )
		{
			glyph_compress_buf_size = asg->width*asg->height*3;
			glyph_compress_buf = realloc( glyph_compress_buf, glyph_compress_buf_size );
		}

		/* we better do some RLE encoding in attempt to preserv memory */
		asg->pixmap  = compress_glyph_pixmap( src, glyph_compress_buf, asg->width, asg->height, src_step );
		asg->ascend  = face->glyph->bitmap_top;
		asg->descend = bmap->rows - asg->ascend;
		LOCAL_DEBUG_OUT( "glyph %p with FT index %u is %dx%d ascend = %d, lead = %d, bmap_top = %d",
							asg, glyph, asg->width, asg->height, asg->ascend, asg->lead,
							face->glyph->bitmap_top );
	}
}

static ASGlyphRange *
split_freetype_glyph_range( unsigned long min_char, unsigned long max_char, FT_Face face )
{
	ASGlyphRange *first = NULL, **r = &first;
LOCAL_DEBUG_CALLER_OUT( "min_char = %lu, max_char = %lu, face = %p", min_char, max_char, face );
	while( min_char <= max_char )
	{
		register unsigned long i = min_char;
		while( i <= max_char && FT_Get_Char_Index( face, CHAR2UNICODE(i)) == 0 ) i++ ;
		if( i <= max_char )
		{
			*r = safecalloc( 1, sizeof(ASGlyphRange));
			(*r)->min_char = i ;
			while( i <= max_char && FT_Get_Char_Index( face, CHAR2UNICODE(i)) != 0 ) i++ ;
			(*r)->max_char = i ;
LOCAL_DEBUG_OUT( "created glyph range from %lu to %lu", (*r)->min_char, (*r)->max_char );
			r = &((*r)->above);
		}
		min_char = i ;
	}
	return first;
}

static ASGlyph *
load_freetype_locale_glyph( ASFont *font, UNICODE_CHAR uc )
{
	ASGlyph *asg = NULL ;
	if( FT_Get_Char_Index( font->ft_face, uc) != 0 )
	{
		asg = safecalloc( 1, sizeof(ASGlyph));
		load_glyph_freetype( font, asg, FT_Get_Char_Index( font->ft_face, uc), uc);
		if( add_hash_item( font->locale_glyphs, AS_HASHABLE(uc), asg ) != ASH_Success )
		{
			LOCAL_DEBUG_OUT( "Failed to add glyph %p for char %ld to hash", asg, uc );
			asglyph_destroy( 0, asg);
			asg = NULL ;
		}else
		{
			LOCAL_DEBUG_OUT( "added glyph %p for char %ld to hash font attr(%d,%d,%d) glyph attr (%d,%d)", asg, uc, font->max_ascend, font->max_descend, font->max_height, asg->ascend, asg->descend );

			if( asg->ascend > font->max_ascend )
				font->max_ascend = asg->ascend ;
			if( asg->descend > font->max_descend )
				font->max_descend = asg->descend ;
			font->max_height = font->max_ascend+font->max_descend ;
			LOCAL_DEBUG_OUT( "font attr(%d,%d,%d) glyph attr (%d,%d)", font->max_ascend, font->max_descend, font->max_height, asg->ascend, asg->descend );
		}
	}else
		add_hash_item( font->locale_glyphs, AS_HASHABLE(uc), NULL );
	return asg;
}

ASGlyph *
asfont_freetype_load_locale_glyph( ASFont *font, UNICODE_CHAR uc )
{
	if( font == NULL || font->ft_face == NULL )
		return NULL;
	return load_freetype_locale_glyph( font, uc );
}

static void
load_freetype_locale_glyphs( unsigned long min_char, unsigned long max_char, ASFont *font )
{
	register unsigned long i = min_char ;
LOCAL_DEBUG_CALLER_OUT( "min_char = %lu, max_char = %lu, font = %p", min_char, max_char, font );
	if( font->locale_glyphs == NULL )
		font->locale_glyphs = create_ashash( 0, NULL, NULL, asglyph_destroy );
	while( i <= max_char )
	{
		load_freetype_locale_glyph( font, CHAR2UNICODE(i));
		++i;
	}
	LOCAL_DEBUG_OUT( "font attr(%d,%d,%d)", font->max_ascend, font->max_descend, font->max_height );
}

static int
load_freetype_glyphs( ASFont *font )
{
	int max_ascend = 0, max_descend = 0;
	ASGlyphRange *r ;

	/* we preload only codes in range 0x21-0xFF in current charset */
	/* if draw_unicode_text is used and we need some other glyphs
	 * we'll just need to add them on demand */
	font->codemap = split_freetype_glyph_range( 0x0021, 0x007F, font->ft_face );

	load_glyph_freetype( font, &(font->default_glyph), 0, 0);/* special no-symbol glyph */
	load_freetype_locale_glyphs( 0x0080, 0x00FF, font );
	if( font->codemap == NULL )
	{
		font->max_height = font->default_glyph.ascend+font->default_glyph.descend;
		if( font->max_height <= 0 )
			font->max_height = 1 ;
		font->max_ascend = MAX((int)font->default_glyph.ascend,1);
		font->max_descend = MAX((int)font->default_glyph.descend,1);
	}else
	{
		for( r = font->codemap ; r != NULL ; r = r->above )
		{
			long min_char = r->min_char ;
			long max_char = r->max_char ;
			long i ;
			if( max_char < min_char )
			{
				i = max_char ;
				max_char = min_char ;
				min_char = i ;
			}
			r->glyphs = safecalloc( (max_char - min_char) + 1, sizeof(ASGlyph));
			for( i = min_char ; i < max_char ; ++i )
			{
				if( i != ' ' && i != '\t' && i!= '\n' )
				{
					ASGlyph *asg = &(r->glyphs[i-min_char]);
					UNICODE_CHAR uc = CHAR2UNICODE(i);
					load_glyph_freetype( font, asg, FT_Get_Char_Index( font->ft_face, uc), uc);
/* Not needed ?
 * 					if( asg->lead >= 0 || asg->lead+asg->width > 3 )
 *						font->pen_move_dir = LEFT_TO_RIGHT ;
 */
					if( asg->ascend > max_ascend )
						max_ascend = asg->ascend ;
					if( asg->descend > max_descend )
						max_descend = asg->descend ;
				}
			}
		}
		if( (int)font->max_ascend <= max_ascend )
			font->max_ascend = MAX(max_ascend,1);
		if( (int)font->max_descend <= max_descend )
			font->max_descend = MAX(max_descend,1);
		font->max_height = font->max_ascend+font->max_descend;
	}
	/* flushing out compression buffer : */
	load_glyph_freetype(NULL, NULL, 0, 0);
	return max_ascend+max_descend;
}

#else  /* HAVE_FREETYPE */

ASGlyph *
asfont_freetype_load_locale_glyph( ASFont *font, UNICODE_CHAR uc )
{
	(void)font;
	(void)uc;
	return NULL;
}

#endif /* HAVE_FREETYPE */
