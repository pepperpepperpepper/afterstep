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
#endif

#ifdef _WIN32
# include "win32/afterbase.h"
#else
# include "afterbase.h"
#endif

#include "asfont_internal.h"

#define DO_X11_ANTIALIASING
#define DO_2STEP_X11_ANTIALIASING
#define DO_3STEP_X11_ANTIALIASING
#define X11_AA_HEIGHT_THRESHOLD 10
#define X11_2STEP_AA_HEIGHT_THRESHOLD 15
#define X11_3STEP_AA_HEIGHT_THRESHOLD 15

#ifdef DO_X11_ANTIALIASING
void antialias_glyph( unsigned char *buffer, unsigned int width, unsigned int height );
#endif

#ifndef X_DISPLAY_MISSING
static int load_X11_glyphs( Display *dpy, ASFont *font, XFontStruct *xfs );
#endif

ASFont *
asfont_open_X11_font_internal( ASFontManager *fontman, const char *font_string, ASFlagType flags)
{
	ASFont *font = NULL ;
#ifndef X_DISPLAY_MISSING
/* 
    #ifdef I18N
     TODO: we have to use FontSet and loop through fonts instead filling
           up 2 bytes per character table with glyphs 
    #else 
*/
    /* for now assume ISO Latin 1 encoding */
	XFontStruct *xfs ;
	if( fontman->dpy == NULL )
		return NULL;
	if( (xfs = XLoadQueryFont( fontman->dpy, font_string )) == NULL )
	{
		show_warning( "failed to load X11 font \"%s\". Sorry about that.", font_string );
		return NULL;
	}
	font = safecalloc( 1, sizeof(ASFont));
	font->magic = MAGIC_ASFONT ;
	font->fontman = fontman;
	font->type = ASF_X11 ;
	font->flags = flags ;
	load_X11_glyphs( fontman->dpy, font, xfs );
	XFreeFont( fontman->dpy, xfs );
#endif /* #ifndef X_DISPLAY_MISSING */
	return font;
}

ASFont *
open_X11_font( ASFontManager *fontman, const char *font_string)
{
	return asfont_open_X11_font_internal( fontman, font_string, 0);
}

/*********************************************************************************/
/* encoding/locale handling						   								 */
/*********************************************************************************/

/* Now, this is the mess, I know :
 * Internally we store everything in current locale;
 * WE then need to convert it into Unicode 4 byte codes
 *
 * TODO: think about incoming data - does it has to be made local friendly ???
 * Definately
 */

#ifndef X_DISPLAY_MISSING

static ASGlyphRange *
split_X11_glyph_range( unsigned int min_char, unsigned int max_char, XCharStruct *chars )
{
	ASGlyphRange *first = NULL, **r = &first;
	int c = 0, delta = (max_char-min_char)+1;
LOCAL_DEBUG_CALLER_OUT( "min_char = %u, max_char = %u, chars = %p", min_char, max_char, chars );
	while( c < delta )
	{
		while( c < delta && chars[c].width == 0 ) ++c;

		if( c < delta )
		{
			*r = safecalloc( 1, sizeof(ASGlyphRange));
			(*r)->min_char = c+min_char ;
			while( c < delta && chars[c].width  != 0 ) ++c ;
			(*r)->max_char = (c-1)+min_char;
LOCAL_DEBUG_OUT( "created glyph range from %lu to %lu", (*r)->min_char, (*r)->max_char );
			r = &((*r)->above);
		}
	}
	return first;
}

void
load_X11_glyph_range( Display *dpy, ASFont *font, XFontStruct *xfs, size_t char_offset,
													  unsigned char byte1,
                                                      unsigned char min_byte2,
													  unsigned char max_byte2, GC *gc )
{
	ASGlyphRange  *all, *r ;
	unsigned long  min_char = (byte1<<8)|min_byte2;
	unsigned char *buffer, *compressed_buf ;
	unsigned int   height = xfs->ascent+xfs->descent ;
	static XGCValues gcv;

	buffer = safemalloc( xfs->max_bounds.width*height*2);
	compressed_buf = safemalloc( xfs->max_bounds.width*height*4);
	all = split_X11_glyph_range( min_char, (byte1<<8)|max_byte2, &(xfs->per_char[char_offset]));
	for( r = all ; r != NULL ; r = r->above )
	{
		XCharStruct *chars = &(xfs->per_char[char_offset+r->min_char-min_char]);
		int len = ((int)r->max_char-(int)r->min_char)+1;
		unsigned char char_base = r->min_char&0x00FF;
		register int i ;
		Pixmap p;
		XImage *xim;
		unsigned int total_width = 0 ;
		int pen_x = 0;
LOCAL_DEBUG_OUT( "loading glyph range of %lu-%lu", r->min_char, r->max_char );
		r->glyphs = safecalloc( len, sizeof(ASGlyph) );
		for( i = 0 ; i < len ; i++ )
		{
			int w = chars[i].rbearing - chars[i].lbearing ;
			r->glyphs[i].lead = chars[i].lbearing ;
			r->glyphs[i].width = MAX(w,(int)chars[i].width) ;
			r->glyphs[i].step = chars[i].width;
			total_width += r->glyphs[i].width ;
			if( chars[i].lbearing > 0 )
				total_width += chars[i].lbearing ;
		}
		p = XCreatePixmap( dpy, DefaultRootWindow(dpy), total_width, height, 1 );
		if( *gc == NULL )
		{
			gcv.font = xfs->fid;
			gcv.foreground = 1;
			*gc = XCreateGC( dpy, p, GCFont|GCForeground, &gcv);
		}else
			XSetForeground( dpy, *gc, 1 );
		XFillRectangle( dpy, p, *gc, 0, 0, total_width, height );
		XSetForeground( dpy, *gc, 0 );

		for( i = 0 ; i < len ; i++ )
		{
			XChar2b test_char ;
			int offset = MIN(0,(int)chars[i].lbearing);

			test_char.byte1 = byte1 ;
			test_char.byte2 = char_base+i ;
			/* we cannot draw string at once since in some fonts charcters may
			 * overlap each other : */
			XDrawImageString16( dpy, p, *gc, pen_x-offset, xfs->ascent, &test_char, 1 );
			pen_x += r->glyphs[i].width ;
			if( chars[i].lbearing > 0 )
				pen_x += chars[i].lbearing ;
		}
		/*XDrawImageString( dpy, p, *gc, 0, xfs->ascent, test_str_char, len );*/
		xim = XGetImage( dpy, p, 0, 0, total_width, height, 0xFFFFFFFF, ZPixmap );
		XFreePixmap( dpy, p );
		pen_x = 0 ;
		for( i = 0 ; i < len ; i++ )
		{
			register int x, y ;
			int width = r->glyphs[i].width;
			unsigned char *row = &(buffer[0]);

			if( chars[i].lbearing > 0 )
				pen_x += chars[i].lbearing ;
			for( y = 0 ; y < height ; y++ )
			{
				for( x = 0 ; x < width ; x++ )
				{
/*					fprintf( stderr, "glyph %d (%c): (%d,%d) 0x%X\n", i, (char)(i+r->min_char), x, y, XGetPixel( xim, pen_x+x, y ));*/
					/* remember default GC colors are black on white - 0 on 1 - and we need
					* quite the opposite - 0xFF on 0x00 */
					row[x] = ( XGetPixel( xim, pen_x+x, y ) != 0 )? 0x00:0xFF;
				}
				row += width;
			}

#ifdef DO_X11_ANTIALIASING
			if( height > X11_AA_HEIGHT_THRESHOLD )
				antialias_glyph( buffer, width, height );
#endif
			if( get_flags( font->flags, ASF_Monospaced ) )
			{
				if( r->glyphs[i].lead > 0 && (int)width + (int)r->glyphs[i].lead > (int)font->space_size )
					if( r->glyphs[i].lead > (int)font->space_size/8 )
						r->glyphs[i].lead = (int)font->space_size/8 ;
				if( (int)width + r->glyphs[i].lead > (int)font->space_size )
				{
					r->glyphs[i].width = (int)font->space_size - r->glyphs[i].lead ;
/*					fprintf(stderr, "lead = %d, space_size = %d, width = %d, to_width = %d\n",
							r->glyphs[i].lead, font->space_size, width, r->glyphs[i].width ); */
					scale_down_glyph_width( buffer, width, r->glyphs[i].width, height );
				}
				/*else
				{
					fprintf(stderr, "lead = %d, space_size = %d, width = %d\n",
							r->glyphs[i].lead, font->space_size, width );
				}	 */
				r->glyphs[i].step = font->space_size ;
			}
			r->glyphs[i].pixmap = compress_glyph_pixmap( buffer, compressed_buf, r->glyphs[i].width, height, r->glyphs[i].width );
			r->glyphs[i].height = height ;
			r->glyphs[i].ascend = xfs->ascent ;
			r->glyphs[i].descend = xfs->descent ;
LOCAL_DEBUG_OUT( "glyph %u(range %lu-%lu) (%c) is %dx%d ascend = %d, lead = %d",  i, r->min_char, r->max_char, (char)(i+r->min_char), r->glyphs[i].width, r->glyphs[i].height, r->glyphs[i].ascend, r->glyphs[i].lead );
			pen_x += width ;
		}
		if( xim )
			XDestroyImage( xim );
	}
LOCAL_DEBUG_OUT( "done loading glyphs. Attaching set of glyph ranges to the codemap...%s", "" );
	if( all != NULL )
	{
		if( font->codemap == NULL )
			font->codemap = all ;
		else
		{
			for( r = font->codemap ; r != NULL ; r = r->above )
			{
				if( r->min_char > all->min_char )
				{
					if( r->below )
						r->below->above = all ;
					r->below = all ;
					while ( all->above != NULL )
						all = all->above ;
					all->above = r ;
					r->below = all ;
					break;
				}
				all->below = r ;
			}
			if( r == NULL && all->below->above == NULL )
				all->below->above = all ;
		}
	}
	free( buffer ) ;
	free( compressed_buf ) ;
LOCAL_DEBUG_OUT( "all don%s", "" );
}


void
make_X11_default_glyph( ASFont *font, XFontStruct *xfs )
{
	unsigned char *buf, *compressed_buf ;
	int width, height ;
	int x, y;
	unsigned char *row ;


	height = xfs->ascent+xfs->descent ;
	width = xfs->max_bounds.width ;

	if( height <= 0 ) height = 4;
	if( width <= 0 ) width = 4;
	buf = safecalloc( height*width, sizeof(unsigned char) );
	compressed_buf = safemalloc( height*width*2 );
	row = buf;
	for( x = 0 ; x < width ; ++x )
		row[x] = 0xFF;
	for( y = 1 ; y < height-1 ; ++y )
	{
		row += width ;
		row[0] = 0xFF ; row[width-1] = 0xFF ;
	}
	for( x = 0 ; x < width ; ++x )
		row[x] = 0xFF;
	font->default_glyph.pixmap = compress_glyph_pixmap( buf, compressed_buf, width, height, width );
	font->default_glyph.width = width ;
	font->default_glyph.step = width ;
	font->default_glyph.height = height ;
	font->default_glyph.lead = 0 ;
	font->default_glyph.ascend = xfs->ascent ;
	font->default_glyph.descend = xfs->descent ;

	free( buf ) ;
	free( compressed_buf ) ;
}

static int
load_X11_glyphs( Display *dpy, ASFont *font, XFontStruct *xfs )
{
	GC gc = NULL;
	font->max_height = xfs->ascent+xfs->descent;
	font->max_ascend = xfs->ascent;
	font->max_descend = xfs->descent;
	font->space_size = xfs->max_bounds.width ;
	if( !get_flags( font->flags, ASF_Monospaced) )
		font->space_size = font->space_size*2/3 ;

	{
		/* we blame X consortium for the following mess : */
		int min_char, max_char, our_min_char = 0x0021, our_max_char = 0x00FF ;
		int byte1 = xfs->min_byte1;
		if( xfs->min_byte1 > 0 )
		{
			min_char = xfs->min_char_or_byte2 ;
			max_char = xfs->max_char_or_byte2 ;
			if( min_char > 0x00FF )
			{
				byte1 = (min_char>>8)&0x00FF;
				min_char &=  0x00FF;
				if( ((max_char>>8)&0x00FF) > byte1 )
					max_char =  0x00FF;
				else
					max_char &= 0x00FF;
			}
		}else
		{
			min_char = ((xfs->min_byte1<<8)&0x00FF00)|(xfs->min_char_or_byte2&0x00FF);
			max_char = ((xfs->min_byte1<<8)&0x00FF00)|(xfs->max_char_or_byte2&0x00FF);
			our_min_char |= ((xfs->min_byte1<<8)&0x00FF00) ;
			our_max_char |= ((xfs->min_byte1<<8)&0x00FF00) ;
		}
		our_min_char = MAX(our_min_char,min_char);
		our_max_char = MIN(our_max_char,max_char);

		load_X11_glyph_range( dpy, font, xfs, (int)our_min_char-(int)min_char, byte1, our_min_char&0x00FF, our_max_char&0x00FF, &gc );
	}
	if( font->default_glyph.pixmap == NULL )
		make_X11_default_glyph( font, xfs );
	if( gc )
		XFreeGC( dpy, gc );
	return xfs->ascent+xfs->descent;
}

#endif /* #ifndef X_DISPLAY_MISSING */

#ifdef DO_X11_ANTIALIASING
void
antialias_glyph( unsigned char *buffer, unsigned int width, unsigned int height )
{
	unsigned char *row1, *row2 ;
	register unsigned char *row ;
	register int x;
	int y;

	row1 = &(buffer[0]);
	row = &(buffer[width]);
	row2 = &(buffer[width+width]);
	for( x = 1 ; x < (int)width-1 ; x++ )
		if( row1[x] == 0 )
		{/* antialiasing here : */
			unsigned int c = (unsigned int)row[x]+
							(unsigned int)row1[x-1]+
							(unsigned int)row1[x+1];
			if( c >= 0x01FE )  /* we cut off secondary aliases */
				row1[x] = c>>2;
		}
	for( y = 1 ; y < (int)height-1 ; y++ )
	{
		if( row[0] == 0 )
		{/* antialiasing here : */
			unsigned int c = (unsigned int)row1[0]+
							(unsigned int)row[1]+
							(unsigned int)row2[0];
			if( c >= 0x01FE )  /* we cut off secondary aliases */
				row[0] = c>>2;
		}
		for( x = 1 ; x < (int)width-1 ; x++ )
		{
			if( row[x] == 0 )
			{/* antialiasing here : */
				unsigned int c = (unsigned int)row1[x]+
								(unsigned int)row[x-1]+
								(unsigned int)row[x+1]+
								(unsigned int)row2[x];
				if( row1[x] != 0 && row[x-1] != 0 && row[x+1] != 0 && row2[x] != 0 &&
					c >= 0x01FE )
					row[x] = c>>3;
				else if( c >= 0x01FE )  /* we cut off secondary aliases */
					row[x] = c>>2;
			}
		}
		if( row[x] == 0 )
		{/* antialiasing here : */
			unsigned int c = (unsigned int)row1[x]+
							(unsigned int)row[x-1]+
							(unsigned int)row2[x];
			if( c >= 0x01FE )  /* we cut off secondary aliases */
				row[x] = c>>2;
		}
		row  += width ;
		row1 += width ;
		row2 += width ;
	}
	for( x = 1 ; x < (int)width-1 ; x++ )
		if( row[x] == 0 )
		{/* antialiasing here : */
			unsigned int c = (unsigned int)row1[x]+
							(unsigned int)row[x-1]+
							(unsigned int)row[x+1];
			if( c >= 0x01FE )  /* we cut off secondary aliases */
				row[x] = c>>2;
		}
#ifdef DO_2STEP_X11_ANTIALIASING
	if( height  > X11_2STEP_AA_HEIGHT_THRESHOLD )
	{
		row1 = &(buffer[0]);
		row = &(buffer[width]);
		row2 = &(buffer[width+width]);
		for( y = 1 ; y < (int)height-1 ; y++ )
		{
			for( x = 1 ; x < (int)width-1 ; x++ )
			{
				if( row[x] == 0 )
				{/* antialiasing here : */
					unsigned int c = (unsigned int)row1[x]+
									(unsigned int)row[x-1]+
									(unsigned int)row[x+1]+
									(unsigned int)row2[x];
					if( row1[x] != 0 && row[x-1] != 0 && row[x+1] != 0 && row2[x] != 0
						&& c >= 0x00FF+0x007F)
						row[x] = c>>3;
					else if( (c >= 0x00FF+0x007F)|| c == 0x00FE )  /* we cut off secondary aliases */
						row[x] = c>>2;
				}
			}
			row  += width ;
			row1 += width ;
			row2 += width ;
		}
	}
#endif
#ifdef DO_3STEP_X11_ANTIALIASING
	if( height  > X11_3STEP_AA_HEIGHT_THRESHOLD )
	{
		row1 = &(buffer[0]);
		row = &(buffer[width]);
		row2 = &(buffer[width+width]);
		for( y = 1 ; y < (int)height-1 ; y++ )
		{
			for( x = 1 ; x < (int)width-1 ; x++ )
			{
				if( row[x] == 0xFF )
				{/* antialiasing here : */
					if( row1[x] < 0xFE || row2[x] < 0xFE )
						if( row[x+1] < 0xFE || row[x-1] < 0xFE )
							row[x] = 0xFE;
				}
			}
			row  += width ;
			row1 += width ;
			row2 += width ;
		}
		row = &(buffer[width]);
		for( y = 1 ; y < (int)height-1 ; y++ )
		{
			for( x = 1 ; x < (int)width-1 ; x++ )
				if( row[x] == 0xFE )
					row[x] = 0xBF ;
			row  += width ;
		}
	}

#endif
}
#endif
