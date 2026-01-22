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
static void 
slice_scanline( ASScanline *dst, ASScanline *src, int start_x, int end_x, ASScanline *middle )
{
	CARD32 *sa = src->alpha, *da = dst->alpha ;
	CARD32 *sr = src->red, *dr = dst->red ;
	CARD32 *sg = src->green, *dg = dst->green ;
	CARD32 *sb = src->blue, *db = dst->blue ;
	int max_x = min( start_x, (int)dst->width);
	int tail = (int)src->width - end_x ; 
	int tiling_step = end_x - start_x ;
	int x1, x2, max_x2 ;

	LOCAL_DEBUG_OUT( "start_x = %d, end_x = %d, tail = %d, tiling_step = %d, max_x = %d", start_x, end_x, tail, tiling_step, max_x );
	for( x1 = 0 ; x1 < max_x ; ++x1 ) 
	{
		da[x1] = sa[x1] ; 
		dr[x1] = sr[x1] ; 
		dg[x1] = sg[x1] ; 
		db[x1] = sb[x1] ;	  
	}
	if( x1 >= dst->width )
		return;
	/* middle portion */
	max_x2 = (int) dst->width - tail ; 
	max_x = min(end_x, max_x2);		
	if( middle ) 
	{	  
		CARD32 *ma = middle->alpha-x1 ;
		CARD32 *mr = middle->red-x1 ;
		CARD32 *mg = middle->green-x1 ;
		CARD32 *mb = middle->blue-x1 ;
		LOCAL_DEBUG_OUT( "middle->width = %d", middle->width );

		for( ; x1 < max_x2 ; ++x1 )
		{
			da[x1] = ma[x1] ; 
			dr[x1] = mr[x1] ; 
			dg[x1] = mg[x1] ; 
			db[x1] = mb[x1] ;	  
		}	 
		LOCAL_DEBUG_OUT( "%d: %8.8lX %8.8lX %8.8lX %8.8lX", x1-1, ma[x1-1], mr[x1-1], mg[x1-1], mb[x1-1] );
	}else
	{	
		for( ; x1 < max_x ; ++x1 )
		{
  			x2 = x1 ;
			for( x2 = x1 ; x2 < max_x2 ; x2 += tiling_step )
			{
				da[x2] = sa[x1] ; 
				dr[x2] = sr[x1] ; 
				dg[x2] = sg[x1] ; 
				db[x2] = sb[x1] ;	  
			}				  
		}	 
	}
	/* tail portion */
	x1 = src->width - tail ;
	x2 = max(max_x2,start_x) ; 
	max_x = src->width ;
	max_x2 = dst->width ;
	for( ; x1 < max_x && x2 < max_x2; ++x1, ++x2 )
	{
		da[x2] = sa[x1] ; 
		dr[x2] = sr[x1] ; 
		dg[x2] = sg[x1] ; 
		db[x2] = sb[x1] ;	  
	}
}	 


ASImage*
slice_asimage2( ASVisual *asv, ASImage *src,
			   int slice_x_start, int slice_x_end,
			   int slice_y_start, int slice_y_end,
			   int to_width,
			   int to_height,
			   Bool scale,
			   ASAltImFormats out_format,
			   unsigned int compression_out, int quality )
{
	ASImage *dst = NULL ;
	ASImageDecoder *imdec = NULL ;
	ASImageOutput  *imout = NULL ;
	START_TIME(started);

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

LOCAL_DEBUG_CALLER_OUT( "scale = %d, sx1 = %d, sx2 = %d, sy1 = %d, sy2 = %d, to_width = %d, to_height = %d", scale, slice_x_start, slice_x_end, slice_y_start, slice_y_end, to_width, to_height );
	if( src == NULL )
		return NULL;
	if( (imdec = start_image_decoding(asv, src, SCL_DO_ALL, 0, 0, src->width, 0, NULL)) == NULL )
		return NULL;
	if( slice_x_end == 0 && slice_x_start > 0 ) 
		slice_x_end = slice_x_start + 1 ;
	if( slice_y_end == 0 && slice_y_start > 0 ) 
		slice_y_end = slice_y_start + 1 ;
	if( slice_x_end > src->width ) 
		slice_x_end = src->width ;
	if( slice_y_end > src->height ) 
		slice_y_end = src->height ;
	if( slice_x_start > slice_x_end ) 
		slice_x_start = (slice_x_end > 0 ) ? slice_x_end-1 : 0 ;
	if( slice_y_start > slice_y_end ) 
		slice_y_start = (slice_y_end > 0 ) ? slice_y_end-1 : 0 ;

LOCAL_DEBUG_OUT( "sx1 = %d, sx2 = %d, sy1 = %d, sy2 = %d, to_width = %d, to_height = %d", slice_x_start, slice_x_end, slice_y_start, slice_y_end, to_width, to_height );
	dst = create_destination_image( to_width, to_height, out_format, compression_out, src->back_color);
	if((imout = start_image_output( asv, dst, out_format, 0, quality)) == NULL )
	{
        destroy_asimage( &dst );
    }else 
	{	
		int y1, y2 ;
		int max_y = min( slice_y_start, (int)dst->height);
		int tail = (int)src->height - slice_y_end ; 
		int max_y2 = (int) dst->height - tail ; 		
		ASScanline *out_buf = prepare_scanline( to_width, 0, NULL, asv->BGR_mode );

		out_buf->flags = 0xFFFFFFFF ;

		if( scale ) 
		{
			ASImageDecoder *imdec_scaled ;
			ASImage *tmp ;
			int x_middle = to_width - slice_x_start ; 
			int x_right = src->width - (slice_x_end+1) ;
			int y_middle = to_height - slice_y_start ;
			int y_bottom = src->height - (slice_y_end+1) ;
			x_middle = ( x_middle <= x_right  )? 0 : x_middle-x_right ;
			y_middle = ( y_middle <= y_bottom )? 0 : y_middle-y_bottom ;
			
			if( x_middle > 0 )
			{	
				tmp = scale_asimage2( asv, src, slice_x_start, 0, 
									   slice_x_end-slice_x_start, max_y, 
									   x_middle, max_y, ASA_ASImage, 0, quality );
				imdec_scaled = start_image_decoding(asv, tmp, SCL_DO_ALL, 0, 0, 0, 0, NULL) ;
				for( y1 = 0 ; y1 < max_y ; ++y1 ) 
				{
					imdec->decode_image_scanline( imdec );
					imdec_scaled->decode_image_scanline( imdec_scaled );
					slice_scanline( out_buf, &(imdec->buffer), slice_x_start, slice_x_end, &(imdec_scaled->buffer) );
					imout->output_image_scanline( imout, out_buf, 1);
				}	 
				stop_image_decoding( &imdec_scaled );
				destroy_asimage( &tmp );
			}else
			{	
				for( y1 = 0 ; y1 < max_y ; ++y1 ) 
				{
					imdec->decode_image_scanline( imdec );
					imout->output_image_scanline( imout, &(imdec->buffer), 1);
				}	 
			}
			/*************************************************************/
			/* middle portion */
			if( y_middle > 0 ) 
			{	
				ASImage *sides ;
				ASImageDecoder *imdec_sides ;
				sides = scale_asimage2( asv, src, 0, slice_y_start, 
								   		src->width, slice_y_end-slice_y_start,
								   		src->width, y_middle, ASA_ASImage, 0, quality );
				imdec_sides = start_image_decoding(asv, sides, SCL_DO_ALL, 0, 0, 0, 0, NULL) ;
/*				print_asimage( sides, 0, __FUNCTION__, __LINE__ ); */
				if( x_middle > 0 ) 
				{
					tmp = scale_asimage2( asv, sides, slice_x_start, 0, 
									   	slice_x_end-slice_x_start, y_middle, 
									   	x_middle, y_middle, ASA_ASImage, 0, quality );
/*					print_asimage( tmp, 0, __FUNCTION__, __LINE__ ); */

					imdec_scaled = start_image_decoding(asv, tmp, SCL_DO_ALL, 0, 0, 0, 0, NULL) ;
					for( y1 = 0 ; y1 < y_middle ; ++y1 ) 
					{
						imdec_sides->decode_image_scanline( imdec_sides );
						imdec_scaled->decode_image_scanline( imdec_scaled );
						slice_scanline( out_buf, &(imdec_sides->buffer), slice_x_start, slice_x_end, &(imdec_scaled->buffer) );
						imout->output_image_scanline( imout, out_buf, 1);
					}	 
					stop_image_decoding( &imdec_scaled );
					destroy_asimage( &tmp );
				
				}else
				{
					for( y1 = 0 ; y1 < y_middle ; ++y1 ) 
					{
						imdec_sides->decode_image_scanline( imdec_sides );
						imout->output_image_scanline( imout, &(imdec->buffer), 1);
					}	 
				}		 
				stop_image_decoding( &imdec_sides );
				destroy_asimage( &sides );
			}			
			/*************************************************************/
			/* bottom portion */

			y2 =  max(max_y2,(int)slice_y_start) ; 
			y1 = src->height - tail ;
			imout->next_line = y2 ; 
			imdec->next_line = y1 ;
			max_y = src->height ;
			if( y2 + max_y - y1 > dst->height ) 
				max_y = dst->height + y1 - y2 ;
			LOCAL_DEBUG_OUT( "y1 = %d, max_y = %d", y1, max_y );		   
			if( x_middle > 0 )
			{	
				tmp = scale_asimage2( asv, src, slice_x_start, y1, 
									   slice_x_end-slice_x_start, src->height-y1, 
									   x_middle, src->height-y1, ASA_ASImage, 0, quality );
				imdec_scaled = start_image_decoding(asv, tmp, SCL_DO_ALL, 0, 0, 0, 0, NULL) ;
				for( ; y1 < max_y ; ++y1 )
				{
					imdec->decode_image_scanline( imdec );
					imdec_scaled->decode_image_scanline( imdec_scaled );
					slice_scanline( out_buf, &(imdec->buffer), slice_x_start, slice_x_end, &(imdec_scaled->buffer) );
					imout->output_image_scanline( imout, out_buf, 1);
				}	 
				stop_image_decoding( &imdec_scaled );
				destroy_asimage( &tmp );
			}else
			{	
				for( ; y1 < max_y ; ++y1 )
				{
					imdec->decode_image_scanline( imdec );
					imout->output_image_scanline( imout, &(imdec->buffer), 1);
				}	 
			}
			
		}else	 /* tile middle portion */
		{                      
			imout->tiling_step = 0;
			LOCAL_DEBUG_OUT( "max_y = %d", max_y );
			for( y1 = 0 ; y1 < max_y ; ++y1 ) 
			{
				imdec->decode_image_scanline( imdec );
				slice_scanline( out_buf, &(imdec->buffer), slice_x_start, slice_x_end, NULL );
				imout->output_image_scanline( imout, out_buf, 1);
			}	 
			/* middle portion */
			imout->tiling_step = (int)slice_y_end - (int)slice_y_start;
			max_y = min(slice_y_end, max_y2);
			LOCAL_DEBUG_OUT( "y1 = %d, max_y = %d, tiling_step = %d", y1, max_y, imout->tiling_step );
			for( ; y1 < max_y ; ++y1 )
			{
				imdec->decode_image_scanline( imdec );
				slice_scanline( out_buf, &(imdec->buffer), slice_x_start, slice_x_end, NULL );
				imout->output_image_scanline( imout, out_buf, 1);
			}

			/* bottom portion */
			imout->tiling_step = 0;
			imout->next_line = y2 =  max(max_y2,(int)slice_y_start) ; 
			imdec->next_line = y1 = src->height - tail ;
			max_y = src->height ;
			if( y2 + max_y - y1 > dst->height ) 
				max_y = dst->height + y1 - y2 ;
			LOCAL_DEBUG_OUT( "y1 = %d, max_y = %d", y1, max_y );		   
			for( ; y1 < max_y ; ++y1 )
			{
				imdec->decode_image_scanline( imdec );
				slice_scanline( out_buf, &(imdec->buffer), slice_x_start, slice_x_end, NULL );
				imout->output_image_scanline( imout, out_buf, 1);
			}
		}
		free_scanline( out_buf, False );
		stop_image_output( &imout );
	}
	stop_image_decoding( &imdec );

	SHOW_TIME("", started);
	return dst;
}

ASImage*
slice_asimage( ASVisual *asv, ASImage *src,
			   int slice_x_start, int slice_x_end,
			   int slice_y_start, int slice_y_end,
			   int to_width, int to_height,
			   ASAltImFormats out_format,
			   unsigned int compression_out, int quality )
{
	
	return slice_asimage2( asv, src, slice_x_start, slice_x_end,
			   			   slice_y_start, slice_y_end, to_width,  to_height,
			   				False, out_format, compression_out, quality );
}
