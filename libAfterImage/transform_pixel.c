/*
 * Copyright (c) 2000,2001 Sasha Vasko <sasha at aftercode.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License.
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
/*#undef NO_DEBUG_OUTPUT */
#undef USE_STUPID_GIMP_WAY_DESTROYING_COLORS
#undef LOCAL_DEBUG
#undef DO_CLOCKING
#undef DEBUG_HSV_ADJUSTMENT
#define USE_64BIT_FPU
#undef NEED_RBITSHIFT_FUNCS

#ifdef _WIN32
#include "win32/config.h"
#else
#include "config.h"
#endif
//#undef HAVE_MMX

#ifdef DO_CLOCKING
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#include <math.h>
#include <string.h>

#ifdef HAVE_MMX
#include <mmintrin.h>
#endif

#ifdef _WIN32
# include "win32/afterbase.h"
#else
# include "afterbase.h"
#endif
#include "asvisual.h"
#include "blender.h"
#include "asimage.h"
#include "imencdec.h"
#include "transform_internal.h"

ASImage *
pixelize_asimage( ASVisual *asv, ASImage *src,
			      int clip_x, int clip_y, int clip_width, int clip_height,
				  int pixel_width, int pixel_height,
				  ASAltImFormats out_format, unsigned int compression_out, int quality )
{
	ASImage *dst = NULL ;
	ASImageDecoder *imdec ;
	ASImageOutput  *imout ;
	START_TIME(started);

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	if (src== NULL)
		return NULL;
		
	if (clip_width <= 0)
		clip_width = src->width;
	if (clip_height <= 0)
		clip_height = src->height;

	if (pixel_width <= 0)
		pixel_width = 1;
	else if (pixel_width > clip_width)
		pixel_width = clip_width;
		
	if (pixel_height <= 0)
		pixel_height = 1;
	else if (pixel_height > clip_height)
		pixel_height = clip_height;
		
LOCAL_DEBUG_CALLER_OUT( "src = %p, offset_x = %d, offset_y = %d, to_width = %d, to_height = %d, pixel_width = %d, pixel_height = %d", src, clip_x, clip_y, clip_width, clip_height, pixel_width, pixel_height );
	if( (imdec = start_image_decoding(asv, src, SCL_DO_ALL, clip_x, clip_y, clip_width, 0, NULL)) == NULL )
	{
		LOCAL_DEBUG_OUT( "failed to start image decoding%s", "");
		return NULL;
	}

	dst = create_destination_image( clip_width, clip_height, out_format, compression_out, src->back_color );

	if((imout = start_image_output( asv, dst, out_format, 0, quality)) == NULL )
	{
		LOCAL_DEBUG_OUT( "failed to start image output%s", "");
        destroy_asimage( &dst );
    }else
	{
		int y, max_y = clip_height;
LOCAL_DEBUG_OUT("pixelizing actually...%s", "");

		if( pixel_width > 1 || pixel_height > 1 )
		{
			int pixel_h_count = (clip_width+pixel_width-1)/pixel_width;
			ASScanline *pixels = prepare_scanline( pixel_h_count, 0, NULL, asv->BGR_mode );
			ASScanline *out_buf = prepare_scanline( clip_width, 0, NULL, asv->BGR_mode );
			int lines_count = 0;

			out_buf->flags = SCL_DO_ALL;
			
			for( y = 0 ; y < max_y ; y++  )
			{
				int pixel_x = 0, x ;
				imdec->decode_image_scanline( imdec );
				for (x = 0; x < clip_width; x += pixel_width)
				{
					int xx = x+pixel_width;
					ASScanline *srcsl = &(imdec->buffer);
					
					if (xx > clip_width)
						xx = clip_width;
					
					while ( --xx >= x)
					{
						pixels->red[pixel_x] += srcsl->red[xx];
						pixels->green[pixel_x] += srcsl->green[xx];
						pixels->blue[pixel_x] += srcsl->blue[xx];
						pixels->alpha[pixel_x] += srcsl->alpha[xx];
					}
					++pixel_x;
				}
				if (++lines_count >= pixel_height || y == max_y-1)
				{
					pixel_x = 0;
					
					for (x = 0; x < clip_width; x += pixel_width)
					{
						int xx = (x + pixel_width> clip_width) ? clip_width : x + pixel_width;
						int count = (xx - x) * lines_count;
						CARD32 r = pixels->red [pixel_x] / count;
						CARD32 g = pixels->green [pixel_x] / count;
						CARD32 b = pixels->blue [pixel_x] / count;
						CARD32 a = pixels->alpha [pixel_x] / count;
						
						pixels->red [pixel_x] = 0;
						pixels->green [pixel_x] = 0;
						pixels->blue [pixel_x] = 0;
						pixels->alpha [pixel_x] = 0;

						if (xx > clip_width)
							xx = clip_width;

						while ( --xx >= x)
						{
							out_buf->red[xx] 	= r;
							out_buf->green[xx]  = g;
							out_buf->blue[xx] 	= b;
							out_buf->alpha[xx]  = a;
						}

						++pixel_x;
					}
					while (lines_count--)
						imout->output_image_scanline( imout, out_buf, 1);
					lines_count = 0;
				}
			}
			free_scanline( out_buf, False );
			free_scanline( pixels, False );
		}else
			for( y = 0 ; y < max_y ; y++  )
			{
				imdec->decode_image_scanline( imdec );
				imout->output_image_scanline( imout, &(imdec->buffer), 1);
			}
		stop_image_output( &imout );
	}
	stop_image_decoding( &imdec );

	SHOW_TIME("", started);
	return dst;
}

ASImage *
color2alpha_asimage( ASVisual *asv, ASImage *src,
			         int clip_x, int clip_y, int clip_width, int clip_height,
				     ARGB32 color,
				     ASAltImFormats out_format, unsigned int compression_out, int quality )
{
	ASImage *dst = NULL ;
	ASImageDecoder *imdec ;
	ASImageOutput  *imout ;
	START_TIME(started);

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	if (src== NULL)
		return NULL;
		
	if (clip_width <= 0)
		clip_width = src->width;
	if (clip_height <= 0)
		clip_height = src->height;

		
LOCAL_DEBUG_CALLER_OUT( "src = %p, offset_x = %d, offset_y = %d, to_width = %d, to_height = %d, color = #%8.8x", src, clip_x, clip_y, clip_width, clip_height, color );
	if( (imdec = start_image_decoding(asv, src, SCL_DO_ALL, clip_x, clip_y, clip_width, 0, NULL)) == NULL )
	{
		LOCAL_DEBUG_OUT( "failed to start image decoding%s", "");
		return NULL;
	}

	dst = create_destination_image( clip_width, clip_height, out_format, compression_out, src->back_color );

	if((imout = start_image_output( asv, dst, out_format, 0, quality)) == NULL )
	{
		LOCAL_DEBUG_OUT( "failed to start image output%s", "");
        destroy_asimage( &dst );
    }else
	{
		int y, max_y = min(clip_height,(int)src->height);
		CARD32 cr = ARGB32_RED8(color);
		CARD32 cg = ARGB32_GREEN8(color);
		CARD32 cb = ARGB32_BLUE8(color);
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)					
fprintf (stderr, "color2alpha():%d: color: red = 0x%8.8X green = 0x%8.8X blue = 0x%8.8X\n", __LINE__, cr, cg, cb);
#endif

		for( y = 0 ; y < max_y ; y++  )
		{
			int x ;
			ASScanline *srcsl = &(imdec->buffer);
			imdec->decode_image_scanline( imdec );
			for (x = 0; x < imdec->buffer.width; ++x)
			{
				CARD32 r = srcsl->red[x];
				CARD32 g = srcsl->green[x];
				CARD32 b = srcsl->blue[x];
				CARD32 a = srcsl->alpha[x];
				/* the following logic is stolen from gimp and altered for our color format and beauty*/
				{
					CARD32 aa = a, ar, ag, ab;
					
#define AS_MIN_CHAN_VAL 	2			/* GIMP uses 0.0001 */
#define AS_MAX_CHAN_VAL 	255			/* GIMP uses 1.0 */
#define MAKE_CHAN_ALPHA_FROM_COL(chan) \
					((c##chan < AS_MIN_CHAN_VAL)? (chan)<<4 : \
						((chan > c##chan)? ((chan - c##chan)<<12) / (AS_MAX_CHAN_VAL - c##chan) : \
							((c##chan - chan)<<12) / c##chan))

					ar = MAKE_CHAN_ALPHA_FROM_COL(r);
					ag = MAKE_CHAN_ALPHA_FROM_COL(g);
					ab = MAKE_CHAN_ALPHA_FROM_COL(b);
#undef 	MAKE_CHAN_ALPHA_FROM_COL
			
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)					
fprintf (stderr, "color2alpha():%d: src(argb): %8.8X %8.8X %8.8X %8.8X; ", __LINE__, a, r, g, b);
#endif
  					a = (ar > ag) ? max(ar, ab) : max(ag,ab);
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)					
fprintf (stderr, "alpha: (%8.8X %8.8X %8.8X)->%8.8X; ", ar, ag, ab, a);
#endif

					if (a == 0) a = 1;
#if defined(USE_STUPID_GIMP_WAY_DESTROYING_COLORS)
#define APPLY_ALPHA_TO_CHAN(chan)  ({int __s = chan; int __c = c##chan; __c += (( __s - __c)*4096)/(int)a;(__c<=0)?0:((__c>=255)?255:__c);})
#else
#define APPLY_ALPHA_TO_CHAN(chan)	chan	
#endif
	  				srcsl->red[x] 	= APPLY_ALPHA_TO_CHAN(r);
					srcsl->green[x] 	= APPLY_ALPHA_TO_CHAN(g);
		  			srcsl->blue[x] 	= APPLY_ALPHA_TO_CHAN(b);
#undef APPLY_ALPHA_TO_CHAN
					a = a*aa>>12;
	  				srcsl->alpha[x] = (a>255)?255:a;

#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)					
fprintf (stderr, "result: %8.8X %8.8X %8.8X %8.8X.\n", src->alpha[x], src->red[x], src->green[x], src->blue[x]);
#endif

				}
				/* end of gimp code */
			}
			imout->output_image_scanline( imout, srcsl, 1);
		}
		stop_image_output( &imout );
	}
	stop_image_decoding( &imdec );

	SHOW_TIME("", started);
	return dst;
}


/* ********************************************************************************/
/* The end !!!! 																 */
/* ********************************************************************************/
