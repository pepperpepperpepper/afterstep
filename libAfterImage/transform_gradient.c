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
make_component_gradient16( register CARD32 *data, CARD16 from, CARD16 to, CARD8 seed, int len )
{
	register int i ;
	long incr = (((long)to<<8)-((long)from<<8))/len ;

	if( incr == 0 )
		for( i = 0 ; i < len ; ++i )
			data[i] = from;
	else
	{
		long curr = from<<8;
		curr += ((long)(((CARD32)seed)<<8) > incr)?incr:((CARD32)seed)<<8 ;
		for( i = 0 ; i < len ; ++i )
		{/* we make calculations in 24bit per chan, then convert it back to 16 and
		  * carry over half of the quantization error onto the next pixel */
			data[i] = curr>>8;
			curr += ((curr&0x00FF)>>1)+incr ;
		}
	}
}
/* **********************************************************************************************/
/* drawing gradient on scanline :  															   */
/* **********************************************************************************************/
void
make_gradient_scanline( ASScanline *scl, ASGradient *grad, ASFlagType filter, ARGB32 seed )
{
	if( scl && grad && filter != 0 )
	{
		int offset = 0, step, i, max_i = grad->npoints - 1 ;
		ARGB32 last_color = ARGB32_Black ;
		int last_idx = 0;
		double last_offset = 0., *offsets = grad->offset ;
		int *used = safecalloc(max_i+1, sizeof(int));
		/* lets find the color of the very first point : */
		for( i = 0 ; i <= max_i ; ++i )
			if( offsets[i] <= 0. )
			{
				last_color = grad->color[i] ;
				last_idx = i ;
				used[i] = 1 ;
				break;
			}

		for( i = 0  ; i <= max_i ; i++ )
		{
			register int k ;
			int new_idx = -1 ;
			/* now lets find the next point  : */
			for( k = 0 ; k <= max_i ; ++k )
			{
				if( used[k]==0 && offsets[k] >= last_offset )
				{
					if( new_idx < 0 )
						new_idx = k ;
					else if( offsets[new_idx] > offsets[k] )
						new_idx = k ;
					else
					{
						register int d1 = new_idx-last_idx ;
						register int d2 = k - last_idx ;
						if( d1*d1 > d2*d2 )
							new_idx = k ;
					}
				}
			}
			if( new_idx < 0 )
				break;
			used[new_idx] = 1 ;
			step = (int)((grad->offset[new_idx] * (double)scl->width) - (double)offset) ;
/*			fprintf( stderr, __FUNCTION__":%d>last_offset = %f, last_color = %8.8X, new_idx = %d, max_i = %d, new_offset = %f, new_color = %8.8X, step = %d, offset = %d\n", __LINE__, last_offset, last_color, new_idx, max_i, offsets[new_idx], grad->color[new_idx], step, offset ); */
			if( step > (int)scl->width-offset )
				step = (int)scl->width-offset ;
			if( step > 0 )
			{
				int color ;
				for( color = 0 ; color < IC_NUM_CHANNELS ; ++color )
					if( get_flags( filter, 0x01<<color ) )
					{
						LOCAL_DEBUG_OUT("channel %d from #%4.4lX to #%4.4lX, ofset = %d, step = %d",
	 	 									color, ARGB32_CHAN8(last_color,color)<<8, ARGB32_CHAN8(grad->color[new_idx],color)<<8, offset, step );
						make_component_gradient16( scl->channels[color]+offset,
												   (CARD16)(ARGB32_CHAN8(last_color,color)<<8),
												   (CARD16)(ARGB32_CHAN8(grad->color[new_idx],color)<<8),
												   (CARD8)ARGB32_CHAN8(seed,color),
												   step);
					}
				offset += step ;
			}
			last_offset = offsets[new_idx];
			last_color = grad->color[new_idx];
			last_idx = new_idx ;
		}
		scl->flags = filter ;
		free( used );
	}
}
/* **************************************************************************************/
/* GRADIENT drawing : 																   */
/* **************************************************************************************/
static void
make_gradient_left2right( ASImageOutput *imout, ASScanline *dither_lines, int dither_lines_num, ASFlagType filter )
{
	int line ;

	imout->tiling_step = dither_lines_num;
	for( line = 0 ; line < dither_lines_num ; line++ )
		imout->output_image_scanline( imout, &(dither_lines[line]), 1);
}

static void
make_gradient_top2bottom( ASImageOutput *imout, ASScanline *dither_lines, int dither_lines_num, ASFlagType filter )
{
	int y, height = imout->im->height, width = imout->im->width ;
	int line ;
	ASScanline result;
	CARD32 chan_data[MAX_GRADIENT_DITHER_LINES] = {0,0,0,0};
LOCAL_DEBUG_CALLER_OUT( "width = %d, height = %d, filetr = 0x%lX, dither_count = %d\n", width, height, filter, dither_lines_num );
	prepare_scanline( width, QUANT_ERR_BITS, &result, imout->asv->BGR_mode );
	for( y = 0 ; y < height ; y++ )
	{
		int color ;

		result.flags = 0 ;
		result.back_color = ARGB32_DEFAULT_BACK_COLOR ;
		LOCAL_DEBUG_OUT( "line: %d", y );
		for( color = 0 ; color < IC_NUM_CHANNELS ; color++ )
			if( get_flags( filter, 0x01<<color ) )
			{
				Bool dithered = False ;
				for( line = 0 ; line < dither_lines_num ; line++ )
				{
					/* we want to do error diffusion here since in other places it only works
						* in horisontal direction : */
					CARD32 c = dither_lines[line].channels[color][y] ; 
					if( y+1 < height )
					{
						c += ((dither_lines[line].channels[color][y+1]&0xFF)>>1);
						if( (c&0xFFFF0000) != 0 )
							c = ( c&0x7F000000 )?0:0x0000FF00;
					}
					chan_data[line] = c ;

					if( chan_data[line] != chan_data[0] )
						dithered = True;
				}
				LOCAL_DEBUG_OUT( "channel: %d. Dithered ? %d", color, dithered );

				if( !dithered )
				{
					result.back_color = (result.back_color&(~MAKE_ARGB32_CHAN8(0xFF,color)))|
										MAKE_ARGB32_CHAN16(chan_data[0],color);
					LOCAL_DEBUG_OUT( "back_color = %8.8lX", result.back_color);
				}else
				{
					register CARD32  *dst = result.channels[color] ;
					for( line = 0 ; line  < dither_lines_num ; line++ )
					{
						register int x ;
						register CARD32 d = chan_data[line] ;
						for( x = line ; x < width ; x+=dither_lines_num )
						{
							dst[x] = d ;
						}
					}
					set_flags(result.flags, 0x01<<color);
				}
			}
		imout->output_image_scanline( imout, &result, 1);
	}
	free_scanline( &result, True );
}

static void
make_gradient_diag_width( ASImageOutput *imout, ASScanline *dither_lines, int dither_lines_num, ASFlagType filter, Bool from_bottom )
{
	int line = 0;
	/* using bresengham algorithm again to trigger horizontal shift : */
	short smaller = imout->im->height;
	short bigger  = imout->im->width;
	register int i = 0;
	int eps;
LOCAL_DEBUG_CALLER_OUT( "width = %d, height = %d, filetr = 0x%lX, dither_count = %d, dither width = %d\n", bigger, smaller, filter, dither_lines_num, dither_lines[0].width );

	if( from_bottom )
		toggle_image_output_direction( imout );
	eps = -(bigger>>1);
	for ( i = 0 ; i < bigger ; i++ )
	{
		eps += smaller;
			if( (eps * 2) >= bigger )
		{
			/* put scanline with the same x offset */
			dither_lines[line].offset_x = i ;
			imout->output_image_scanline( imout, &(dither_lines[line]), 1);
			if( ++line >= dither_lines_num )
				line = 0;
			eps -= bigger ;
		}
	}
}

static void
make_gradient_diag_height( ASImageOutput *imout, ASScanline *dither_lines, int dither_lines_num, ASFlagType filter, Bool from_bottom )
{
	int line = 0;
	unsigned short width = imout->im->width, height = imout->im->height ;
	/* using bresengham algorithm again to trigger horizontal shift : */
	unsigned short smaller = width;
	unsigned short bigger  = height;
	register int i = 0, k =0;
	int eps;
	ASScanline result;
	int *offsets ;

	prepare_scanline( width, QUANT_ERR_BITS, &result, imout->asv->BGR_mode );
	offsets = safemalloc( sizeof(int)*width );
	offsets[0] = 0 ;

	eps = -(bigger>>1);
	for ( i = 0 ; i < bigger ; i++ )
	{
		++offsets[k];
		eps += smaller;
			if( (eps * 2) >= bigger )
		{
			if( ++k >= width )
				break;
			offsets[k] = offsets[k-1] ; /* seeding the offset */
			eps -= bigger ;
		}
	}

	if( from_bottom )
		toggle_image_output_direction( imout );

	result.flags = (filter&SCL_DO_ALL);
	if( (filter&SCL_DO_ALL) == SCL_DO_ALL )
	{
		for( i = 0 ; i < height ; i++ )
		{
			for( k = 0 ; k < width ; k++ )
			{
				int offset = i+offsets[k] ;
				CARD32 **src_chan = &(dither_lines[line].channels[0]) ;
				result.alpha[k] = src_chan[IC_ALPHA][offset] ;
				result.red  [k] = src_chan[IC_RED]  [offset] ;
				result.green[k] = src_chan[IC_GREEN][offset] ;
				result.blue [k] = src_chan[IC_BLUE] [offset] ;
				if( ++line >= dither_lines_num )
					line = 0 ;
			}
			imout->output_image_scanline( imout, &result, 1);
		}
	}else
	{
		for( i = 0 ; i < height ; i++ )
		{
			for( k = 0 ; k < width ; k++ )
			{
				int offset = i+offsets[k] ;
				CARD32 **src_chan = &(dither_lines[line].channels[0]) ;
				if( get_flags(filter, SCL_DO_ALPHA) )
					result.alpha[k] = src_chan[IC_ALPHA][offset] ;
				if( get_flags(filter, SCL_DO_RED) )
					result.red[k]   = src_chan[IC_RED]  [offset] ;
				if( get_flags(filter, SCL_DO_GREEN) )
					result.green[k] = src_chan[IC_GREEN][offset] ;
				if( get_flags(filter, SCL_DO_BLUE) )
					result.blue[k]  = src_chan[IC_BLUE] [offset] ;
				if( ++line >= dither_lines_num )
					line = 0 ;
			}
			imout->output_image_scanline( imout, &result, 1);
		}
	}

	free( offsets );
	free_scanline( &result, True );
}

static ARGB32
get_best_grad_back_color( ASGradient *grad )
{
	ARGB32 back_color = 0 ;
	int chan ;
	for( chan = 0 ; chan < IC_NUM_CHANNELS ; ++chan )
	{
		CARD8 best = 0;
		unsigned int best_size = 0;
		register int i = grad->npoints;
		while( --i > 0 )
		{ /* very crude algorithm, detecting biggest spans of the same color :*/
			CARD8 c = ARGB32_CHAN8(grad->color[i], chan );
			unsigned int span = grad->color[i]*20000;
			if( c == ARGB32_CHAN8(grad->color[i-1], chan ) )
			{
				span -= grad->color[i-1]*2000;
				if( c == best )
					best_size += span ;
				else if( span > best_size )
				{
					best_size = span ;
					best = c ;
				}
			}
		}
		back_color |= MAKE_ARGB32_CHAN8(best,chan);
	}
	return back_color;
}

ASImage*
make_gradient( ASVisual *asv, ASGradient *grad,
               int width, int height, ASFlagType filter,
  			   ASAltImFormats out_format, unsigned int compression_out, int quality  )
{
	ASImage *im = NULL ;
	ASImageOutput *imout;
	int line_len = width;
	START_TIME(started);
LOCAL_DEBUG_CALLER_OUT( "type = 0x%X, width=%d, height = %d, filter = 0x%lX", grad->type, width, height, filter );
	if( grad == NULL )
		return NULL;

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	if( width == 0 )
		width = 2;
 	if( height == 0 )
		height = 2;

	im = create_destination_image( width, height, out_format, compression_out, get_best_grad_back_color( grad ) );

	if( get_flags(grad->type,GRADIENT_TYPE_ORIENTATION) )
		line_len = height ;
	if( get_flags(grad->type,GRADIENT_TYPE_DIAG) )
		line_len = MAX(width,height)<<1 ;
	if((imout = start_image_output( asv, im, out_format, QUANT_ERR_BITS, quality)) == NULL )
	{
        destroy_asimage( &im );
    }else
	{
		int dither_lines = MIN(imout->quality+1, MAX_GRADIENT_DITHER_LINES) ;
		ASScanline *lines;
		int line;
		static ARGB32 dither_seeds[MAX_GRADIENT_DITHER_LINES] = { 0, 0xFFFFFFFF, 0x7F0F7F0F, 0x0F7F0F7F };

		if( dither_lines > (int)im->height || dither_lines > (int)im->width )
			dither_lines = MIN(im->height, im->width) ;

		lines = safecalloc( dither_lines, sizeof(ASScanline));
		for( line = 0 ; line < dither_lines ; line++ )
		{
			prepare_scanline( line_len, QUANT_ERR_BITS, &(lines[line]), asv->BGR_mode );
			make_gradient_scanline( &(lines[line]), grad, filter, dither_seeds[line] );
		}
		switch( get_flags(grad->type,GRADIENT_TYPE_MASK) )
		{
			case GRADIENT_Left2Right :
				make_gradient_left2right( imout, lines, dither_lines, filter );
  	    		break ;
			case GRADIENT_Top2Bottom :
				make_gradient_top2bottom( imout, lines, dither_lines, filter );
				break ;
			case GRADIENT_TopLeft2BottomRight :
			case GRADIENT_BottomLeft2TopRight :
				if( width >= height )
					make_gradient_diag_width( imout, lines, dither_lines, filter,
											 (grad->type==GRADIENT_BottomLeft2TopRight));
				else
					make_gradient_diag_height( imout, lines, dither_lines, filter,
											  (grad->type==GRADIENT_BottomLeft2TopRight));
				break ;
			default:
				break;
		}
		stop_image_output( &imout );
		for( line = 0 ; line < dither_lines ; line++ )
			free_scanline( &(lines[line]), True );
		free( lines );
	}
	SHOW_TIME("", started);
	return im;
}
