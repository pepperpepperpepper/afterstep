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

unsigned char *
compress_glyph_pixmap( unsigned char *src, unsigned char *buffer,
					   unsigned int width, unsigned int height,
					   int src_step )
{
	unsigned char *pixmap ;
	register unsigned char *dst = buffer ;
	register int k = 0, i = 0 ;
	int count = -1;
	unsigned char last = src[0];
/* new way: if its FF we encode it as 01rrrrrr where rrrrrr is repitition count
 * if its 0 we encode it as 00rrrrrr. Any other symbol we bitshift right by 1
 * and then store it as 1sssssss where sssssss are topmost sugnificant bits.
 * Note  - single FFs and 00s are encoded as any other character. Its been noted
 * that in 99% of cases 0 or FF repeats, and very seldom anything else repeats
 */
	while ( height)
	{
		if( src[k] != last || (last != 0 && last != 0xFF) || count >= 63 )
		{
			if( count == 0 )
				dst[i++] = (last>>1)|0x80;
			else if( count > 0 )
			{
				if( last == 0xFF )
					count |= 0x40 ;
				dst[i++] = count;
				count = 0 ;
			}else
				count = 0 ;
			last = src[k] ;
		}else
		 	count++ ;
/*fprintf( stderr, "%2.2X ", src[k] ); */
		if( ++k >= (int)width )
		{
/*			fputc( '\n', stderr ); */
			--height ;
			k = 0 ;
			src += src_step ;
		}
	}
	if( count == 0 )
		dst[i++] = (last>>1)|0x80;
	else
	{
		if( last == 0xFF )
			count |= 0x40 ;
		dst[i++] = count;
	}
	pixmap  = safemalloc( i/*+(32-(i&0x01F) )*/);
/*fprintf( stderr, "pixmap alloced %p size %d(%d)", pixmap, i, i+(32-(i&0x01F) )); */
	memcpy( pixmap, buffer, i );

	return pixmap;
}

void
scale_down_glyph_width( unsigned char *buffer, int from_width, int to_width, int height )
{
	int smaller = to_width;
	int bigger  = from_width;
	register int i = 0, k = 0, l;
	/*fprintf( stderr, "scaling glyph down from %d to %d\n", from_width, to_width );*/
	/* LOCAL_DEBUG_OUT( "smaller %d, bigger %d, eps %d", smaller, bigger, eps ); */
	/* now using Bresengham algoritm to fiill the scales :
	 * since scaling is merely transformation
	 * from 0:bigger space (x) to 0:smaller space(y)*/
	for( l = 0 ; l < height ; ++l )
	{
		unsigned char *ptr = &(buffer[l*from_width]) ;
		CARD32 sum = 0;
		int count = 0 ;
		int eps = -bigger/2;

		k = 0 ;
		for ( i = 0 ; i < bigger ; i++ )
		{
			/* add next elem here */
			sum += (unsigned int)ptr[i] ;
			++count ;
			eps += smaller;

			if( eps+eps >= bigger )
			{
				/* divide here */
				/*fprintf( stderr, "i=%d, k=%d, sum=%d, count=%d\n", i, k, sum, count );*/
				ptr[k] = ( count > 1 )?sum/count:sum ;
				sum = 0 ;
				count = 0 ;
				++k ;
				eps -= bigger ;
			}
		}
	}
	/* now we need to compress the pixmap */

	l = to_width ;
	k = from_width ;
	do
	{
		for( i = 0 ; i < to_width; ++i )
			buffer[l+i] = buffer[k+i];
		l += to_width ;
		k += from_width ;
	}while( l < to_width*height );
}
