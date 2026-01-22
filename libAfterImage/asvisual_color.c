/*
 * Copyright (c) 2001,2000,1999 Sasha Vasko <sasha at aftercode.net>
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

#undef LOCAL_DEBUG
#undef DEBUG_SL2XIMAGE
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef _WIN32
# include "win32/afterbase.h"
#else
# include "afterbase.h"
#endif
#include "asvisual.h"
#include "scanline.h"
/***************************************************************************/
/* Screen color format -> AS color format conversion ; 					   */
/***************************************************************************/
/***************************************************************************/
/* Screen color format -> AS color format conversion ; 					   */
/***************************************************************************/
/* this functions convert encoded color values into real pixel values, and
 * return half of the quantization error so we can do error diffusion : */
/* encoding scheme : 0RRRrrrr rrrrGGgg ggggggBB bbbbbbbb
 * where RRR, GG and BB are overflow bits so we can do all kinds of funky
 * combined adding, note that we don't use 32'd bit as it is a sign bit */

#ifndef X_DISPLAY_MISSING
static inline void
query_pixel_color( ASVisual *asv, unsigned long pixel, CARD32 *r, CARD32 *g, CARD32 *b )
{
	XColor xcol ;
	xcol.flags = DoRed|DoGreen|DoBlue ;
	xcol.pixel = pixel ;
	if( XQueryColor( asv->dpy, asv->colormap, &xcol ) != 0 )
	{
		*r = xcol.red>>8 ;
		*g = xcol.green>>8 ;
		*b = xcol.blue>>8 ;
	}
}
#endif /*ifndef X_DISPLAY_MISSING */


CARD32 color2pixel32bgr(ASVisual *asv, CARD32 encoded_color, unsigned long *pixel)
{
	*pixel = ARGB32_RED8(encoded_color)|(ARGB32_GREEN8(encoded_color)<<8)|(ARGB32_BLUE8(encoded_color)<<16);
	return 0;
}
CARD32 color2pixel32rgb(ASVisual *asv, CARD32 encoded_color, unsigned long *pixel)
{
	*pixel = encoded_color&0x00FFFFFF;
	return 0;
}
CARD32 color2pixel24bgr(ASVisual *asv, CARD32 encoded_color, unsigned long *pixel)
{
	*pixel = encoded_color&0x00FFFFFF;
	return 0;
}
CARD32 color2pixel24rgb(ASVisual *asv, CARD32 encoded_color, unsigned long *pixel)
{
	*pixel = encoded_color&0x00FFFFFF;
	return 0;
}
CARD32 color2pixel16bgr(ASVisual *asv, CARD32 encoded_color, unsigned long *pixel)
{
	register CARD32 c = encoded_color ;
    *pixel = ((c&0x000000F8)<<8)|((c&0x0000FC00)>>5)|((c&0x00F80000)>>19);
	return (c>>1)&0x00030103;
}
CARD32 color2pixel16rgb(ASVisual *asv, CARD32 encoded_color, unsigned long *pixel)
{
	register CARD32 c = encoded_color ;
    *pixel = ((c&0x00F80000)>>8)|((c&0x0000FC00)>>5)|((c&0x000000F8)>>3);
	return (c>>1)&0x00030103;
}
CARD32 color2pixel15bgr(ASVisual *asv, CARD32 encoded_color, unsigned long *pixel)
{
	register CARD32 c = encoded_color ;
    *pixel = ((c&0x000000F8)<<7)|((c&0x0000F800)>>6)|((c&0x00F80000)>>19);
	return (c>>1)&0x00030303;
}
CARD32 color2pixel15rgb(ASVisual *asv, CARD32 encoded_color, unsigned long *pixel)
{
	register CARD32 c = encoded_color ;
    *pixel = ((c&0x00F80000)>>9)|((c&0x0000F800)>>6)|((c&0x000000F8)>>3);
	return (c>>1)&0x00030303;
}

CARD32 color2pixel_pseudo3bpp( ASVisual *asv, CARD32 encoded_color, unsigned long *pixel )
{
	register CARD32 c = encoded_color ;
	*pixel = asv->as_colormap[((c>>25)&0x0008)|((c>>16)&0x0002)|((c>>7)&0x0001)];
	return (c>>1)&0x003F3F3F;
}

CARD32 color2pixel_pseudo6bpp( ASVisual *asv, CARD32 encoded_color, unsigned long *pixel )
{
	register CARD32 c = encoded_color ;
	*pixel = asv->as_colormap[((c>>22)&0x0030)|((c>>14)&0x000C)|((c>>6)&0x0003)];
	return (c>>1)&0x001F1F1F;
}

CARD32 color2pixel_pseudo12bpp( ASVisual *asv, CARD32 encoded_color, unsigned long *pixel )
{
	register CARD32 c = encoded_color ;
	*pixel = asv->as_colormap[((c>>16)&0x0F00)|((c>>10)&0x00F0)|((c>>4)&0x000F)];
	return (c>>1)&0x00070707;
}

void pixel2color32rgb(ASVisual *asv, unsigned long pixel, CARD32 *red, CARD32 *green, CARD32 *blue)
{}
void pixel2color32bgr(ASVisual *asv, unsigned long pixel, CARD32 *red, CARD32 *green, CARD32 *blue)
{}
void pixel2color24rgb(ASVisual *asv, unsigned long pixel, CARD32 *red, CARD32 *green, CARD32 *blue)
{}
void pixel2color24bgr(ASVisual *asv, unsigned long pixel, CARD32 *red, CARD32 *green, CARD32 *blue)
{}
void pixel2color16rgb(ASVisual *asv, unsigned long pixel, CARD32 *red, CARD32 *green, CARD32 *blue)
{}
void pixel2color16bgr(ASVisual *asv, unsigned long pixel, CARD32 *red, CARD32 *green, CARD32 *blue)
{}
void pixel2color15rgb(ASVisual *asv, unsigned long pixel, CARD32 *red, CARD32 *green, CARD32 *blue)
{}
void pixel2color15bgr(ASVisual *asv, unsigned long pixel, CARD32 *red, CARD32 *green, CARD32 *blue)
{}

void ximage2scanline32(ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;
	register CARD32 *a = sl->alpha+sl->offset_x;
	int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x);
	register CARD32 *src = (CARD32*)xim_data ;
/*	src += sl->offset_x; */
/*fprintf( stderr, "%d: ", y);*/

#ifdef WORDS_BIGENDIAN
	if( !asv->msb_first )
#else
	if( asv->msb_first )
#endif
	{
		while (--i >= 0)
		{
			b[i] = (src[i]>>24)&0x0ff;
			g[i] = (src[i]>>16)&0x0ff;
			r[i] = (src[i]>>8)&0x0ff;
			a[i] = src[i]&0x0ff;
/*			fprintf( stderr, "[%d->%8.8X %8.8X %8.8X %8.8X = %8.8X]", i, r[i], g[i], b[i], a[i], src[i]);*/
		}
	}else
	{
		while (--i >= 0)
		{
			a[i] = (src[i]>>24)&0x0ff;
			r[i] = (src[i]>>16)&0x0ff;
			g[i] = (src[i]>>8)&0x0ff;
			b[i] =  src[i]&0x0ff;
		}
	}
/*fprintf( stderr, "\n");*/
}

void ximage2scanline16( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x)-1;
	register CARD16 *src = (CARD16*)xim_data ;
    register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;
#ifdef WORDS_BIGENDIAN
	if( !asv->msb_first )
#else
	if( asv->msb_first )
#endif
		do
		{
#define ENCODE_MSBF_565(r,gh3,gl3,b)	(((gh3)&0x0007)|((gl3)&0xE000)|((r)&0x00F8)|((b)&0x1F00))
			r[i] =  (src[i]&0x00F8);
			g[i] = ((src[i]&0x0007)<<5)|((src[i]&0xE000)>>11);
			b[i] =  (src[i]&0x1F00)>>5;
		}while( --i >= 0);
	else
		do
		{
#define ENCODE_LSBF_565(r,g,b) (((g)&0x07E0)|((r)&0xF800)|((b)&0x001F))
			r[i] =  (src[i]&0xF800)>>8;
			g[i] =  (src[i]&0x07E0)>>3;
			b[i] =  (src[i]&0x001F)<<3;
		}while( --i >= 0);

}
void ximage2scanline15( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x)-1;
	register CARD16 *src = (CARD16*)xim_data ;
    register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;
#ifdef WORDS_BIGENDIAN
	if( !asv->msb_first )
#else
	if( asv->msb_first )
#endif
		do
		{
#define ENCODE_MSBF_555(r,gh2,gl3,b)	(((gh2)&0x0003)|((gl3)&0xE000)|((r)&0x007C)|((b)&0x1F00))
			r[i] =  (src[i]&0x007C)<<1;
			g[i] = ((src[i]&0x0003)<<6)|((src[i]&0xE000)>>10);
			b[i] =  (src[i]&0x1F00)>>5;
		}while( --i >= 0);
	else
		do
		{
#define ENCODE_LSBF_555(r,g,b) (((g)&0x03E0)|((r)&0x7C00)|((b)&0x001F))
			r[i] =  (src[i]&0x7C00)>>7;
			g[i] =  (src[i]&0x03E0)>>2;
			b[i] =  (src[i]&0x001F)<<3;
		}while( --i >= 0);
}

#ifndef X_DISPLAY_MISSING

void
ximage2scanline_pseudo3bpp( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x)-1;
    register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;

	do
	{
		unsigned long pixel = XGetPixel( xim, i, y );
		ARGB32 c = asv->as_colormap_reverse.xref[pixel] ;
		if( c == 0 )
			query_pixel_color( asv, pixel, r+i, g+i, b+i );
		else
		{
			r[i] =  ARGB32_RED8(c);
			g[i] =  ARGB32_GREEN8(c);
			b[i] =  ARGB32_BLUE8(c);
		}
	}while( --i >= 0);

}

void
ximage2scanline_pseudo6bpp( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x)-1;
    register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;

	if( xim->bits_per_pixel == 8 )
	{
		register CARD8 *src = (CARD8*)xim_data ;
		do
		{
			ARGB32 c = asv->as_colormap_reverse.xref[src[i]] ;
			if( c == 0 )
				query_pixel_color( asv, src[i], r+i, g+i, b+i );
			else
			{
				r[i] =  ARGB32_RED8(c);
				g[i] =  ARGB32_GREEN8(c);
				b[i] =  ARGB32_BLUE8(c);
			}
		}while( --i >= 0);

	}else
		do
		{
			unsigned long pixel = XGetPixel( xim, i, y );
			ARGB32 c = asv->as_colormap_reverse.xref[pixel] ;
			if( c == 0 )
				query_pixel_color( asv, pixel, r+i, g+i, b+i );
			else
			{
				r[i] =  ARGB32_RED8(c);
				g[i] =  ARGB32_GREEN8(c);
				b[i] =  ARGB32_BLUE8(c);
			}
		}while( --i >= 0);
}

void
ximage2scanline_pseudo12bpp( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x)-1;
    register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;

	if( xim->bits_per_pixel == 16 )
	{
		register CARD16 *src = (CARD16*)xim_data ;
		do
		{
            ASHashData hdata ;
            ARGB32 c ;
            if( get_hash_item( asv->as_colormap_reverse.hash, AS_HASHABLE((unsigned long)src[i]), &hdata.vptr ) != ASH_Success )
				query_pixel_color( asv, src[i], r+i, g+i, b+i );
			else
			{
                c = hdata.c32;
				r[i] =  ARGB32_RED8(c);
				g[i] =  ARGB32_GREEN8(c);
				b[i] =  ARGB32_BLUE8(c);
			}
		}while( --i >= 0);

	}else
		do
		{
			unsigned long pixel = XGetPixel( xim, i, y );
            ASHashData hdata ;
			ARGB32 c ;
            if( get_hash_item( asv->as_colormap_reverse.hash, (ASHashableValue)pixel, &hdata.vptr ) != ASH_Success )
				query_pixel_color( asv, pixel, r+i, g+i, b+i );
			else
			{
                c = hdata.c32;
				r[i] =  ARGB32_RED8(c);
				g[i] =  ARGB32_GREEN8(c);
				b[i] =  ARGB32_BLUE8(c);
			}
		}while( --i >= 0);
}
#endif

void scanline2ximage32( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;
	register CARD32 *a = sl->alpha+sl->offset_x;
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x);
	register CARD32 *src = (CARD32*)xim_data;
/*	src += sl->offset_x ; */
/*fprintf( stderr, "%d: ", y);*/
#ifdef WORDS_BIGENDIAN
	if( !asv->msb_first )
#else
	if( asv->msb_first )
#endif
		while( --i >= 0)
		{
			src[i] = (b[i]<<24)|(g[i]<<16)|(r[i]<<8)|a[i];
/*			fprintf( stderr, "[%d->%8.8X %8.8X %8.8X %8.8X = %8.8X]", i, r[i], g[i], b[i], a[i], src[i]);  */
		}
	else
		while( --i >= 0) src[i] = (a[i]<<24)|(r[i]<<16)|(g[i]<<8)|b[i];
/*fprintf( stderr, "\n");*/
#ifdef DEBUG_SL2XIMAGE
	i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x);
	src = (CARD32*)xim_data;
	src += sl->offset_x;
	printf( "%d: xim->width = %d, sl->width = %d, sl->offset = %d: ", y, xim->width, sl->width, sl->offset_x );
	while(--i>=0 )	printf( "%8.8lX ", src[i] );
	printf( "\n" );
#endif
}

/* most LCD can only show 18bpp despite driver's claim to the opposite, hence this artificial mode:
 */
void scanline2ximage18( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;
	register CARD32 *a = sl->alpha+sl->offset_x;
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x);
	register CARD32 *src = (CARD32*)xim_data;
	register CARD32 c = (r[i]<<20) | (g[i]<<10) | (b[i]);

#ifdef WORDS_BIGENDIAN
	if( !asv->msb_first )
#else
	if( asv->msb_first )
#endif
		do
		{
			src[i] = (b[i]<<24)|(g[i]<<16)|(r[i]<<8)|a[i];
			if( --i < 0 )
				break;
			/* carry over quantization error allow for error diffusion:*/
			c = ((c>>1)&0x00400404)+((r[i]<<20) | (g[i]<<10) | (b[i]));
			{
				register CARD32 d = c&0x300C0300 ;
				if( d )
				{
					if( c&0x30000000 )
						d |= 0x0FF00000;
					if( c&0x000C0000 )
						d |= 0x0003FC00 ;
					if( c&0x00000300 )
						d |= 0x000000FF ;
					c ^= d;
				}
/*fprintf( stderr, "c = 0x%X, d = 0x%X, c^d = 0x%X\n", c, d, c^d );*/
			}
		}while(1);
	else
		while( --i >= 0) src[i] = (a[i]<<24)|(r[i]<<16)|(g[i]<<8)|b[i];
}

void scanline2ximage16( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x)-1;
	register CARD16 *src = (CARD16*)xim_data ;
    register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;
	register CARD32 c = (r[i]<<20) | (g[i]<<10) | (b[i]);
#ifdef WORDS_BIGENDIAN
	if( !asv->msb_first )
#else
	if( asv->msb_first )
#endif
		do
		{
			src[i] = ENCODE_MSBF_565((c>>20),(c>>15),(c<<1),(c<<5));
			if( --i < 0 )
				break;
			/* carry over quantization error allow for error diffusion:*/
			c = ((c>>1)&0x00300403)+((r[i]<<20) | (g[i]<<10) | (b[i]));
			{
				register CARD32 d = c&0x300C0300 ;
				if( d )
				{
					if( c&0x30000000 )
						d |= 0x0FF00000;
					if( c&0x000C0000 )
						d |= 0x0003FC00 ;
					if( c&0x00000300 )
						d |= 0x000000FF ;
					c ^= d;
				}
/*fprintf( stderr, "c = 0x%X, d = 0x%X, c^d = 0x%X\n", c, d, c^d );*/
			}
		}while(1);
	else
		do
		{
			src[i] = ENCODE_LSBF_565((c>>12),(c>>7),(c>>3));
			if( --i < 0 )
				break;
			/* carry over quantization error allow for error diffusion:*/
			c = ((c>>1)&0x00300403)+((r[i]<<20) | (g[i]<<10) | (b[i]));
			{
				register CARD32 d = c&0x300C0300 ;
				if( d )
				{
					if( c&0x30000000 )
						d |= 0x0FF00000;
					if( c&0x000C0000 )
						d |= 0x0003FC00 ;
					if( c&0x00000300 )
						d |= 0x000000FF ;
					c ^= d;
				}
/*fprintf( stderr, "c = 0x%X, d = 0x%X, c^d = 0x%X\n", c, d, c^d );*/
			}
		}while(1);
}

void scanline2ximage15( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x)-1;
	register CARD16 *src = (CARD16*)xim_data ;
    register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;
	register CARD32 c = (r[i]<<20) | (g[i]<<10) | (b[i]);
#ifdef WORDS_BIGENDIAN
	if( !asv->msb_first )
#else
	if( asv->msb_first )
#endif
		do
		{
			src[i] = ENCODE_MSBF_555((c>>21),(c>>16),c/*(c>>2)*/,(c<<5));
			if( --i < 0 )
				break;
			/* carry over quantization error allow for error diffusion:*/
			c = ((c>>1)&0x00300C03)+((r[i]<<20) | (g[i]<<10) | (b[i]));
/*fprintf( stderr, "%s:%d src[%d] = 0x%4.4X, c = 0x%X, color[%d] = #%2.2X%2.2X%2.2X\n", __FUNCTION__, __LINE__, i+1, src[i+1], c, i, r[i], g[i], b[i]);*/
			{
				register CARD32 d = c&0x300C0300 ;
				if( d )
				{
					if( c&0x30000000 )
						d |= 0x0FF00000;
					if( c&0x000C0000 )
						d |= 0x0003FC00 ;
					if( c&0x00000300 )
						d |= 0x000000FF ;
					c ^= d;
				}
/*fprintf( stderr, "%s:%d c = 0x%X, d = 0x%X, c^d = 0x%X\n", __FUNCTION__, __LINE__, c, d, c^d );*/
			}
		}while(1);
	else
	{
		do
		{
			src[i] = ENCODE_LSBF_555((c>>13),(c>>8),(c>>3));
			if( --i < 0 )
				break;
			/* carry over quantization error allow for error diffusion:*/
			c = ((c>>1)&0x00300C03)+((r[i]<<20) | (g[i]<<10) | (b[i]));
			{
				register CARD32 d = c&0x300C0300 ;
				if( d )
				{
					if( c&0x30000000 )
						d |= 0x0FF00000;
					if( c&0x000C0000 )
						d |= 0x0003FC00 ;
					if( c&0x00000300 )
						d |= 0x000000FF ;
					c ^= d;
				}
			}
		}while(1);
	}
}

#ifndef X_DISPLAY_MISSING
void
scanline2ximage_pseudo3bpp( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x)-1;
	register CARD32 c = (r[i]<<20) | (g[i]<<10) | (b[i]);

	do
	{
		XPutPixel( xim, i, y, asv->as_colormap[((c>>25)&0x0008)|((c>>16)&0x0002)|((c>>7)&0x0001)] );
		if( --i < 0 )
			break;
		c = ((c>>1)&0x03F0FC3F)+((r[i]<<20) | (g[i]<<10) | (b[i]));
		{/* handling possible overflow : */
			register CARD32 d = c&0x300C0300 ;
			if( d )
			{
				if( c&0x30000000 )
					d |= 0x0FF00000;
				if( c&0x000C0000 )
					d |= 0x0003FC00 ;
				if( c&0x00000300 )
					d |= 0x000000FF ;
				c ^= d;
			}
		}
	}while(i);
}

void
scanline2ximage_pseudo6bpp( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x)-1;
	register CARD32 c = (r[i]<<20) | (g[i]<<10) | (b[i]);

	if( xim->bits_per_pixel == 8 )
	{
		register CARD8 *dst = (CARD8*)xim_data ;
		do
		{
			dst[i] = asv->as_colormap[((c>>22)&0x0030)|((c>>14)&0x000C)|((c>>6)&0x0003)];
			if( --i < 0 )
				break;
			c = ((c>>1)&0x01F07C1F)+((r[i]<<20) | (g[i]<<10) | (b[i]));
			{/* handling possible overflow : */
				register CARD32 d = c&0x300C0300 ;
				if( d )
				{
					if( c&0x30000000 )
						d |= 0x0FF00000;
					if( c&0x000C0000 )
						d |= 0x0003FC00 ;
					if( c&0x00000300 )
						d |= 0x000000FF ;
					c ^= d;
				}
			}
		}while(i);
	}else
	{
		do
		{
			XPutPixel( xim, i, y, asv->as_colormap[((c>>22)&0x0030)|((c>>14)&0x000C)|((c>>6)&0x0003)] );
			if( --i < 0 )
				break;
			c = ((c>>1)&0x01F07C1F)+((r[i]<<20) | (g[i]<<10) | (b[i]));
			{/* handling possible overflow : */
				register CARD32 d = c&0x300C0300 ;
				if( d )
				{
					if( c&0x30000000 )
						d |= 0x0FF00000;
					if( c&0x000C0000 )
						d |= 0x0003FC00 ;
					if( c&0x00000300 )
						d |= 0x000000FF ;
					c ^= d;
				}
			}
		}while(i);
	}
}

void
scanline2ximage_pseudo12bpp( ASVisual *asv, XImage *xim, ASScanline *sl, int y,  register unsigned char *xim_data )
{
	register CARD32 *r = sl->xc1+sl->offset_x, *g = sl->xc2+sl->offset_x, *b = sl->xc3+sl->offset_x;
	register int i = MIN((unsigned int)(xim->width),sl->width-sl->offset_x)-1;
	register CARD32 c = (r[i]<<20) | (g[i]<<10) | (b[i]);

	if( xim->bits_per_pixel == 16 )
	{
		register CARD16 *dst = (CARD16*)xim_data ;
		do
		{
			dst[i] = asv->as_colormap[((c>>16)&0x0F00)|((c>>10)&0x00F0)|((c>>4)&0x000F)];
			if( --i < 0 )
				break;
			c = ((c>>1)&0x00701C07)+((r[i]<<20) | (g[i]<<10) | (b[i]));
			{/* handling possible overflow : */
				register CARD32 d = c&0x300C0300 ;
				if( d )
				{
					if( c&0x30000000 )
						d |= 0x0FF00000;
					if( c&0x000C0000 )
						d |= 0x0003FC00 ;
					if( c&0x00000300 )
						d |= 0x000000FF ;
					c ^= d;
				}
			}
		}while(i);
	}else
	{
		do
		{
			XPutPixel( xim, i, y, asv->as_colormap[((c>>16)&0x0F00)|((c>>10)&0x00F0)|((c>>4)&0x000F)] );
			if( --i < 0 )
				break;
			c = ((c>>1)&0x00701C07)+((r[i]<<20) | (g[i]<<10) | (b[i]));
			{/* handling possible overflow : */
				register CARD32 d = c&0x300C0300 ;
				if( d )
				{
					if( c&0x30000000 )
						d |= 0x0FF00000;
					if( c&0x000C0000 )
						d |= 0x0003FC00 ;
					if( c&0x00000300 )
						d |= 0x000000FF ;
					c ^= d;
				}
			}
		}while(i);
	}
}

#endif /* ifndef X_DISPLAY_MISSING */

