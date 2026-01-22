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
static inline void
reverse_component( register CARD32 *src, register CARD32 *dst, int *unused, int len )
{
	register int i = 0;
	src += len-1 ;
	do
	{
		dst[i] = src[-i];
	}while(++i < len );
}
/* ***************************************************************************/
/* Image flipping(rotation)													*/
/* ***************************************************************************/
ASImage *
flip_asimage( ASVisual *asv, ASImage *src,
		      int offset_x, int offset_y,
			  int to_width,
			  int to_height,
			  int flip,
			  ASAltImFormats out_format, unsigned int compression_out, int quality )
{
	ASImage *dst = NULL ;
	ASImageOutput  *imout ;
	ASFlagType filter = SCL_DO_ALL;
	START_TIME(started);

LOCAL_DEBUG_CALLER_OUT( "offset_x = %d, offset_y = %d, to_width = %d, to_height = %d", offset_x, offset_y, to_width, to_height );
	if( src == NULL )
		return NULL ;
	
	filter = get_asimage_chanmask(src);
	dst = create_destination_image( to_width, to_height, out_format, compression_out, src->back_color);

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	if((imout = start_image_output( asv, dst, out_format, 0, quality)) == NULL )
	{
        destroy_asimage( &dst );
    }else
	{
		ASImageDecoder *imdec ;
		ASScanline result ;
		int y;
LOCAL_DEBUG_OUT("flip-flopping actually...%s", "");
		prepare_scanline( to_width, 0, &result, asv->BGR_mode );
		if( (imdec = start_image_decoding(asv, src, filter, offset_x, offset_y,
		                                  get_flags( flip, FLIP_VERTICAL )?to_height:to_width,
										  get_flags( flip, FLIP_VERTICAL )?to_width:to_height, NULL)) != NULL )
		{
            if( get_flags( flip, FLIP_VERTICAL ) )
			{
				CARD32 *chan_data ;
				size_t  pos = 0;
				int x ;
				CARD32 *a = imdec->buffer.alpha ;
				CARD32 *r = imdec->buffer.red ;
				CARD32 *g = imdec->buffer.green ;
				CARD32 *b = imdec->buffer.blue;

				chan_data = safemalloc( to_width*to_height*sizeof(CARD32));
                result.back_color = src->back_color;
				result.flags = filter ;
/*				memset( a, 0x00, to_height*sizeof(CARD32));
				memset( r, 0x00, to_height*sizeof(CARD32));
				memset( g, 0x00, to_height*sizeof(CARD32));
				memset( b, 0x00, to_height*sizeof(CARD32));
  */			for( y = 0 ; y < (int)to_width ; y++ )
				{
					imdec->decode_image_scanline( imdec );
					for( x = 0; x < (int)to_height ; x++ )
					{
						chan_data[pos++] = MAKE_ARGB32( a[x],r[x],g[x],b[x] );
					}
				}

				if( get_flags( flip, FLIP_UPSIDEDOWN ) )
				{
					for( y = 0 ; y < (int)to_height ; ++y )
					{
						pos = y + (int)(to_width-1)*(to_height) ;
						for( x = 0 ; x < (int)to_width ; ++x )
						{
							result.alpha[x] = ARGB32_ALPHA8(chan_data[pos]);
							result.red  [x] = ARGB32_RED8(chan_data[pos]);
							result.green[x] = ARGB32_GREEN8(chan_data[pos]);
							result.blue [x] = ARGB32_BLUE8(chan_data[pos]);
							pos -= to_height ;
						}
						imout->output_image_scanline( imout, &result, 1);
					}
				}else
				{
					for( y = to_height-1 ; y >= 0 ; --y )
					{
						pos = y ;
						for( x = 0 ; x < (int)to_width ; ++x )
						{
							result.alpha[x] = ARGB32_ALPHA8(chan_data[pos]);
							result.red  [x] = ARGB32_RED8(chan_data[pos]);
							result.green[x] = ARGB32_GREEN8(chan_data[pos]);
							result.blue [x] = ARGB32_BLUE8(chan_data[pos]);
							pos += to_height ;
						}
						imout->output_image_scanline( imout, &result, 1);
					}
				}
				free( chan_data );
			}else
			{
				toggle_image_output_direction( imout );
/*                fprintf( stderr, __FUNCTION__":chanmask = 0x%lX", filter ); */
				for( y = 0 ; y < (int)to_height ; y++  )
				{
					imdec->decode_image_scanline( imdec );
                    result.flags = imdec->buffer.flags = imdec->buffer.flags & filter ;
                    result.back_color = imdec->buffer.back_color ;
                    SCANLINE_FUNC_FILTERED(reverse_component,imdec->buffer,result,0,to_width);
					imout->output_image_scanline( imout, &result, 1);
				}
			}
			stop_image_decoding( &imdec );
		}
		free_scanline( &result, True );
		stop_image_output( &imout );
	}
	SHOW_TIME("", started);
	return dst;
}

ASImage *
mirror_asimage( ASVisual *asv, ASImage *src,
		      int offset_x, int offset_y,
			  int to_width,
			  int to_height,
			  Bool vertical, ASAltImFormats out_format,
			  unsigned int compression_out, int quality )
{
	ASImage *dst = NULL ;
	ASImageOutput  *imout ;
	START_TIME(started);

	LOCAL_DEBUG_CALLER_OUT( "offset_x = %d, offset_y = %d, to_width = %d, to_height = %d", offset_x, offset_y, to_width, to_height );
	dst = create_destination_image( to_width, to_height, out_format, compression_out, src->back_color);

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	if((imout = start_image_output( asv, dst, out_format, 0, quality)) == NULL )
	{
        destroy_asimage( &dst );
    }else
	{
		ASImageDecoder *imdec ;
		ASScanline result ;
		int y;
		if( !vertical )
			prepare_scanline( to_width, 0, &result, asv->BGR_mode );
LOCAL_DEBUG_OUT("miroring actually...%s", "");
		if( (imdec = start_image_decoding(asv, src, SCL_DO_ALL, offset_x, offset_y,
		                                  to_width, to_height, NULL)) != NULL )
		{
			if( vertical )
			{
				toggle_image_output_direction( imout );
				for( y = 0 ; y < (int)to_height ; y++  )
				{
					imdec->decode_image_scanline( imdec );
					imout->output_image_scanline( imout, &(imdec->buffer), 1);
				}
			}else
			{
				for( y = 0 ; y < (int)to_height ; y++  )
				{
					imdec->decode_image_scanline( imdec );
					result.flags = imdec->buffer.flags ;
					result.back_color = imdec->buffer.back_color ;
					SCANLINE_FUNC(reverse_component,imdec->buffer,result,0,to_width);
					imout->output_image_scanline( imout, &result, 1);
				}
			}
			stop_image_decoding( &imdec );
		}
		if( !vertical )
			free_scanline( &result, True );
		stop_image_output( &imout );
	}
	SHOW_TIME("", started);
	return dst;
}

ASImage *
pad_asimage(  ASVisual *asv, ASImage *src,
		      int dst_x, int dst_y,
			  int to_width,
			  int to_height,
			  ARGB32 color,
			  ASAltImFormats out_format,
			  unsigned int compression_out, int quality )
{
	ASImage *dst = NULL ;
	ASImageOutput  *imout ;
	int clip_width, clip_height ;
	START_TIME(started);

LOCAL_DEBUG_CALLER_OUT( "dst_x = %d, dst_y = %d, to_width = %d, to_height = %d", dst_x, dst_y, to_width, to_height );
	if( src == NULL )
		return NULL ;

	if( to_width == src->width && to_height == src->height && dst_x == 0 && dst_y == 0 )
		return clone_asimage( src, SCL_DO_ALL );

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	dst = create_destination_image( to_width, to_height, out_format, compression_out, src->back_color);

	clip_width = src->width ;
	clip_height = src->height ;
	if( dst_x < 0 )
		clip_width = MIN( (int)to_width, dst_x+clip_width );
	else
		clip_width = MIN( (int)to_width-dst_x, clip_width );
    if( dst_y < 0 )
		clip_height = MIN( (int)to_height, dst_y+clip_height);
	else
		clip_height = MIN( (int)to_height-dst_y, clip_height);
	if( (clip_width <= 0 || clip_height <= 0) )
	{                              /* we are completely outside !!! */
		dst->back_color = color ;
		return dst ;
	}

	if((imout = start_image_output( asv, dst, out_format, 0, quality)) == NULL )
	{
        destroy_asimage( &dst );
    }else
	{
		ASImageDecoder *imdec = NULL;
		ASScanline result ;
		int y;
		int start_x = (dst_x < 0)? 0: dst_x;
		int start_y = (dst_y < 0)? 0: dst_y;

		if( (int)to_width != clip_width || clip_width != (int)src->width )
		{
			prepare_scanline( to_width, 0, &result, asv->BGR_mode );
			imdec = start_image_decoding(  asv, src, SCL_DO_ALL,
			                               (dst_x<0)? -dst_x:0,
										   (dst_y<0)? -dst_y:0,
		                                    clip_width, clip_height, NULL);
		}

		result.back_color = color ;
		result.flags = 0 ;
LOCAL_DEBUG_OUT( "filling %d lines with %8.8lX", start_y, color );
		for( y = 0 ; y < start_y ; y++  )
			imout->output_image_scanline( imout, &result, 1);

		if( imdec )
			result.back_color = imdec->buffer.back_color ;
		if( (int)to_width == clip_width )
		{
			if( imdec == NULL )
			{
LOCAL_DEBUG_OUT( "copiing %d lines", clip_height );
				copy_asimage_lines( dst, start_y, src, (dst_y < 0 )? -dst_y: 0, clip_height, SCL_DO_ALL );
				imout->next_line += clip_height ;
			}else
				for( y = 0 ; y < clip_height ; y++  )
				{
					imdec->decode_image_scanline( imdec );
					imout->output_image_scanline( imout, &(imdec->buffer), 1);
				}
		}else if( imdec )
		{
			for( y = 0 ; y < clip_height ; y++  )
			{
				int chan ;

				imdec->decode_image_scanline( imdec );
				result.flags = imdec->buffer.flags ;
				for( chan = 0 ; chan < IC_NUM_CHANNELS ; ++chan )
				{
	   				register CARD32 *chan_data = result.channels[chan] ;
	   				register CARD32 *src_chan_data = imdec->buffer.channels[chan]+((dst_x<0)? -dst_x : 0) ;
					CARD32 chan_val = ARGB32_CHAN8(color, chan);
					register int k = -1;
					for( k = 0 ; k < start_x ; ++k )
						chan_data[k] = chan_val ;
					chan_data += k ;
					for( k = 0 ; k < clip_width ; ++k )
						chan_data[k] = src_chan_data[k];
					chan_data += k ;
					k = to_width-(start_x+clip_width) ;
					while( --k >= 0 )
						chan_data[k] = chan_val ;
				}
				imout->output_image_scanline( imout, &result, 1);
			}
		}
		result.back_color = color ;
		result.flags = 0 ;
LOCAL_DEBUG_OUT( "filling %d lines with %8.8lX at the end", to_height-(start_y+clip_height), color );
		for( y = start_y+clip_height ; y < (int)to_height ; y++  )
			imout->output_image_scanline( imout, &result, 1);

		if( imdec )
		{
			stop_image_decoding( &imdec );
			free_scanline( &result, True );
		}
		stop_image_output( &imout );
	}
	SHOW_TIME("", started);
	return dst;
}


/**********************************************************************/

Bool fill_asimage( ASVisual *asv, ASImage *im,
               	   int x, int y, int width, int height,
				   ARGB32 color )
{
	ASImageOutput *imout;
	ASImageDecoder *imdec;
	START_TIME(started);

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	if( im == NULL )
		return False;
	if( x < 0 )
	{	width += x ; x = 0 ; }
	if( y < 0 )
	{	height += y ; y = 0 ; }

	if( width <= 0 || height <= 0 || x >= (int)im->width || y >= (int)im->height )
		return False;
	if( x+width > (int)im->width )
		width = (int)im->width-x ;
	if( y+height > (int)im->height )
		height = (int)im->height-y ;

	if((imout = start_image_output( asv, im, ASA_ASImage, 0, ASIMAGE_QUALITY_DEFAULT)) == NULL )
		return False ;
	else
	{
		int i ;
		imout->next_line = y ;
		if( x == 0 && width == (int)im->width )
		{
			ASScanline result ;
			result.flags = 0 ;
			result.back_color = color ;
			for( i = 0 ; i < height ; i++ )
				imout->output_image_scanline( imout, &result, 1);
		}else if ((imdec = start_image_decoding(asv, im, SCL_DO_ALL, 0, y, im->width, height, NULL)) != NULL )
		{
			CARD32 alpha = ARGB32_ALPHA8(color), red = ARGB32_RED8(color),
				   green = ARGB32_GREEN8(color), blue = ARGB32_BLUE8(color);
			CARD32 	*a = imdec->buffer.alpha + x ; 
			CARD32 	*r = imdec->buffer.red + x ;
			CARD32 	*g = imdec->buffer.green + x ;
			CARD32 	*b = imdec->buffer.blue + x  ;
			for( i = 0 ; i < height ; i++ )
			{
				register int k ;
				imdec->decode_image_scanline( imdec );
				for( k = 0 ; k < width ; ++k )
				{
					a[k] = alpha ;
					r[k] = red ;
					g[k] = green ;
					b[k] = blue ;
				}
				imout->output_image_scanline( imout, &(imdec->buffer), 1);
			}
			stop_image_decoding( &imdec );
		}
	}
	stop_image_output( &imout );
	SHOW_TIME("", started);
	return True;
}
