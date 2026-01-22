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
/***********************************************************************
 * Hue,saturation and lightness adjustments.
 **********************************************************************/
ASImage*
adjust_asimage_hsv( ASVisual *asv, ASImage *src,
				    int offset_x, int offset_y,
	  			    int to_width, int to_height,
					int affected_hue, int affected_radius,
					int hue_offset, int saturation_offset, int value_offset,
					ASAltImFormats out_format,
					unsigned int compression_out, int quality )
{
	ASImage *dst = NULL ;
	ASImageDecoder *imdec ;
	ASImageOutput  *imout ;
	START_TIME(started);

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

LOCAL_DEBUG_CALLER_OUT( "offset_x = %d, offset_y = %d, to_width = %d, to_height = %d, hue = %u", offset_x, offset_y, to_width, to_height, affected_hue );
	if( src == NULL ) 
		return NULL;
	if( (imdec = start_image_decoding(asv, src, SCL_DO_ALL, offset_x, offset_y, to_width, 0, NULL)) == NULL )
		return NULL;

	dst = create_destination_image( to_width, to_height, out_format, compression_out, src->back_color);
	set_decoder_shift(imdec,8);
	if((imout = start_image_output( asv, dst, out_format, 8, quality)) == NULL )
	{
        destroy_asimage( &dst );
    }else
	{
	    CARD32 from_hue1 = 0, from_hue2 = 0, to_hue1 = 0, to_hue2 = 0 ;
		int y, max_y = to_height;
		Bool do_greyscale = False ; 

		affected_hue = normalize_degrees_val( affected_hue );
		affected_radius = normalize_degrees_val( affected_radius );
		if( value_offset != 0 )
			do_greyscale = (affected_hue+affected_radius >= 360 || affected_hue-affected_radius <= 0 );
		if( affected_hue > affected_radius )
		{
			from_hue1 = degrees2hue16(affected_hue-affected_radius);
			if( affected_hue+affected_radius >= 360 )
			{
				to_hue1 = MAX_HUE16 ;
				from_hue2 = MIN_HUE16 ;
				to_hue2 = degrees2hue16(affected_hue+affected_radius-360);
			}else
				to_hue1 = degrees2hue16(affected_hue+affected_radius);
		}else
		{
			from_hue1 = degrees2hue16(affected_hue+360-affected_radius);
			to_hue1 = MAX_HUE16 ;
			from_hue2 = MIN_HUE16 ;
			to_hue2 = degrees2hue16(affected_hue+affected_radius);
		}
		hue_offset = degrees2hue16(hue_offset);
		saturation_offset = (saturation_offset<<16) / 100;
		value_offset = (value_offset<<16)/100 ;
LOCAL_DEBUG_OUT("adjusting actually...%s", "");
		if( to_height > src->height )
		{
			imout->tiling_step = src->height ;
			max_y = src->height ;
		}
		for( y = 0 ; y < max_y ; y++  )
		{
			register int x = imdec->buffer.width;
			CARD32 *r = imdec->buffer.red;
			CARD32 *g = imdec->buffer.green;
			CARD32 *b = imdec->buffer.blue ;
			long h, s, v ;
			imdec->decode_image_scanline( imdec );
			while( --x >= 0 )
			{
				if( (h = rgb2hue( r[x], g[x], b[x] )) != 0 )
				{
#ifdef DEBUG_HSV_ADJUSTMENT
					fprintf( stderr, "IN  %d: rgb = #%4.4lX.%4.4lX.%4.4lX hue = %ld(%d)        range is (%ld - %ld, %ld - %ld), dh = %d\n", __LINE__, r[x], g[x], b[x], h, ((h>>8)*360)>>8, from_hue1, to_hue1, from_hue2, to_hue2, hue_offset );
#endif

					if( affected_radius >= 180 ||
						(h >= (int)from_hue1 && h <= (int)to_hue1 ) ||
						(h >= (int)from_hue2 && h <= (int)to_hue2 ) )

					{
						s = rgb2saturation( r[x], g[x], b[x] ) + saturation_offset;
						v = rgb2value( r[x], g[x], b[x] )+value_offset;
						h += hue_offset ;
						if( h > MAX_HUE16 )
							h -= MAX_HUE16 ;
						else if( h == 0 )
							h =  MIN_HUE16 ;
						else if( h < 0 )
							h += MAX_HUE16 ;
						if( v < 0 ) v = 0 ;
						else if( v > 0x00FFFF ) v = 0x00FFFF ;

						if( s < 0 ) s = 0 ;
						else if( s > 0x00FFFF ) s = 0x00FFFF ;

						hsv2rgb ( (CARD32)h, (CARD32)s, (CARD32)v, &r[x], &g[x], &b[x]);

#ifdef DEBUG_HSV_ADJUSTMENT
						fprintf( stderr, "OUT %d: rgb = #%4.4lX.%4.4lX.%4.4lX hue = %ld(%ld)     sat = %ld val = %ld\n", __LINE__, r[x], g[x], b[x], h, ((h>>8)*360)>>8, s, v );
#endif
					}
				}else if( do_greyscale ) 
				{
					int tmp = (int)r[x] + value_offset ; 
					g[x] = b[x] = r[x] = (tmp < 0)?0:((tmp>0x00FFFF)?0x00FFff:tmp);
				}
			}
			imdec->buffer.flags = 0xFFFFFFFF ;
			imout->output_image_scanline( imout, &(imdec->buffer), 1);
		}
		stop_image_output( &imout );
	}
	stop_image_decoding( &imdec );

	SHOW_TIME("", started);
	return dst;
}
