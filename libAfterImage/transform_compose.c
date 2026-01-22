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
tint_component_mod( register CARD32 *data, CARD16 ratio, int len )
{
	register int i ;
	if( ratio == 255 )
		for( i = 0 ; i < len ; ++i )
			data[i] = data[i]<<8;
	else if( ratio == 128 )
		for( i = 0 ; i < len ; ++i )
			data[i] = data[i]<<7;
	else if( ratio == 0 )
		for( i = 0 ; i < len ; ++i )
			data[i] = 0;
	else
		for( i = 0 ; i < len ; ++i )
			data[i] = data[i]*ratio;
}

static inline void
copytintpad_scanline( ASScanline *src, ASScanline *dst, int offset, ARGB32 tint )
{
	register int i ;
	CARD32 chan_tint[4], chan_fill[4] ;
	int color ;
	int copy_width = src->width, dst_offset = 0, src_offset = 0;

	if( offset+(int)src->width < 0 || offset > (int)dst->width )
		return;
	chan_tint[IC_RED]   = ARGB32_RED8  (tint)<<1;
	chan_tint[IC_GREEN] = ARGB32_GREEN8(tint)<<1;
	chan_tint[IC_BLUE]  = ARGB32_BLUE8 (tint)<<1;
	chan_tint[IC_ALPHA] = ARGB32_ALPHA8(tint)<<1;
	chan_fill[IC_RED]   = ARGB32_RED8  (dst->back_color)<<dst->shift;
	chan_fill[IC_GREEN] = ARGB32_GREEN8(dst->back_color)<<dst->shift;
	chan_fill[IC_BLUE]  = ARGB32_BLUE8 (dst->back_color)<<dst->shift;
	chan_fill[IC_ALPHA] = ARGB32_ALPHA8(dst->back_color)<<dst->shift;
	if( offset < 0 )
		src_offset = -offset ;
	else
		dst_offset = offset ;
	copy_width = MIN( src->width-src_offset, dst->width-dst_offset );

	dst->flags = src->flags ;
	for( color = 0 ; color < IC_NUM_CHANNELS ; ++color )
	{
		register CARD32 *psrc = src->channels[color]+src_offset;
		register CARD32 *pdst = dst->channels[color];
		int ratio = chan_tint[color];
/*	fprintf( stderr, "channel %d, tint is %d(%X), src_width = %d, src_offset = %d, dst_width = %d, dst_offset = %d psrc = %p, pdst = %p\n", color, ratio, ratio, src->width, src_offset, dst->width, dst_offset, psrc, pdst );
*/
		{
/*			register CARD32 fill = chan_fill[color]; */
			for( i = 0 ; i < dst_offset ; ++i )
				pdst[i] = 0;
			pdst += dst_offset ;
		}

		if( get_flags(src->flags, 0x01<<color) )
		{
			if( ratio >= 254 )
				for( i = 0 ; i < copy_width ; ++i )
					pdst[i] = psrc[i]<<8;
			else if( ratio == 128 )
				for( i = 0 ; i < copy_width ; ++i )
					pdst[i] = psrc[i]<<7;
			else if( ratio == 0 )
				for( i = 0 ; i < copy_width ; ++i )
					pdst[i] = 0;
			else
				for( i = 0 ; i < copy_width ; ++i )
					pdst[i] = psrc[i]*ratio;
		}else
		{
		    ratio = ratio*chan_fill[color];
			for( i = 0 ; i < copy_width ; ++i )
				pdst[i] = ratio;
			set_flags( dst->flags, (0x01<<color));
		}
		{
/*			register CARD32 fill = chan_fill[color]; */
			for( ; i < (int)dst->width-dst_offset ; ++i )
				pdst[i] = 0;
/*				print_component(pdst, 0, dst->width ); */
		}
	}
}
ASImage *
tile_asimage( ASVisual *asv, ASImage *src,
		      int offset_x, int offset_y,
			  int to_width,
			  int to_height,
			  ARGB32 tint,
			  ASAltImFormats out_format, unsigned int compression_out, int quality )
{
	ASImage *dst = NULL ;
	ASImageDecoder *imdec ;
	ASImageOutput  *imout ;
	START_TIME(started);

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

LOCAL_DEBUG_CALLER_OUT( "src = %p, offset_x = %d, offset_y = %d, to_width = %d, to_height = %d, tint = #%8.8lX", src, offset_x, offset_y, to_width, to_height, tint );
	if( src== NULL || (imdec = start_image_decoding(asv, src, SCL_DO_ALL, offset_x, offset_y, to_width, 0, NULL)) == NULL )
	{
		LOCAL_DEBUG_OUT( "failed to start image decoding%s", "");
		return NULL;
	}

	dst = create_destination_image( to_width, to_height, out_format, compression_out, src->back_color );

	if((imout = start_image_output( asv, dst, out_format, (tint!=0)?8:0, quality)) == NULL )
	{
		LOCAL_DEBUG_OUT( "failed to start image output%s", "");
        destroy_asimage( &dst );
    }else
	{
		int y, max_y = to_height;
LOCAL_DEBUG_OUT("tiling actually...%s", "");
		if( to_height > src->height )
		{
			imout->tiling_step = src->height ;
			max_y = src->height ;
		}
		if( tint != 0 )
		{
			for( y = 0 ; y < max_y ; y++  )
			{
				imdec->decode_image_scanline( imdec );
				tint_component_mod( imdec->buffer.red, (CARD16)(ARGB32_RED8(tint)<<1), to_width );
				tint_component_mod( imdec->buffer.green, (CARD16)(ARGB32_GREEN8(tint)<<1), to_width );
  				tint_component_mod( imdec->buffer.blue, (CARD16)(ARGB32_BLUE8(tint)<<1), to_width );
				tint_component_mod( imdec->buffer.alpha, (CARD16)(ARGB32_ALPHA8(tint)<<1), to_width );
				imout->output_image_scanline( imout, &(imdec->buffer), 1);
			}
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
merge_layers( ASVisual *asv,
				ASImageLayer *layers, int count,
			  	int dst_width,
			  	int dst_height,
			  	ASAltImFormats out_format, unsigned int compression_out, int quality )
{
	ASImage *dst = NULL ;
	ASImageDecoder **imdecs ;
	ASImageOutput  *imout ;
	ASImageLayer *pcurr = layers;
	int i ;
	ASScanline dst_line ;
	START_TIME(started);

LOCAL_DEBUG_CALLER_OUT( "dst_width = %d, dst_height = %d", dst_width, dst_height );
	
	dst = create_destination_image( dst_width, dst_height, out_format, compression_out, ARGB32_DEFAULT_BACK_COLOR );
	if( dst == NULL )
		return NULL;

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	prepare_scanline( dst_width, QUANT_ERR_BITS, &dst_line, asv->BGR_mode );
	dst_line.flags = SCL_DO_ALL ;

	imdecs = safecalloc( count+20, sizeof(ASImageDecoder*));

	for( i = 0 ; i < count ; i++ )
	{
		/* all laayers but first must have valid image or solid_color ! */
		if( (pcurr->im != NULL || pcurr->solid_color != 0 || i == 0) &&
			pcurr->dst_x < (int)dst_width && pcurr->dst_x+(int)pcurr->clip_width > 0 )
		{
			imdecs[i] = start_image_decoding(asv, pcurr->im, SCL_DO_ALL,
				                             pcurr->clip_x, pcurr->clip_y,
											 pcurr->clip_width, pcurr->clip_height,
											 pcurr->bevel);
			if( pcurr->bevel_width != 0 && pcurr->bevel_height != 0 )
				set_decoder_bevel_geom( imdecs[i],
				                        pcurr->bevel_x, pcurr->bevel_y,
										pcurr->bevel_width, pcurr->bevel_height );
  			if( pcurr->tint == 0 && i != 0 )
				set_decoder_shift( imdecs[i], 8 );
			if( pcurr->im == NULL )
				set_decoder_back_color( imdecs[i], pcurr->solid_color );
		}
		if( pcurr->next == pcurr )
			break;
		else
			pcurr = (pcurr->next!=NULL)?pcurr->next:pcurr+1 ;
	}
	if( i < count )
		count = i+1 ;

	if(imdecs[0] == NULL || (imout = start_image_output( asv, dst, out_format, QUANT_ERR_BITS, quality)) == NULL )
	{
		for( i = 0 ; i < count ; i++ )
			if( imdecs[i] )
				stop_image_decoding( &(imdecs[i]) );

        destroy_asimage( &dst );
		free_scanline( &dst_line, True );
    }else
	{
		int y, max_y = 0;
		int min_y = dst_height;
		int bg_tint = (layers[0].tint==0)?0x7F7F7F7F:layers[0].tint ;
		int bg_bottom = layers[0].dst_y+layers[0].clip_height+imdecs[0]->bevel_v_addon ;
LOCAL_DEBUG_OUT("blending actually...%s", "");
		pcurr = layers ;
		for( i = 0 ; i < count ; i++ )
		{
			if( imdecs[i] )
			{
				int layer_bottom = pcurr->dst_y+pcurr->clip_height ;
				if( pcurr->dst_y < min_y )
					min_y = pcurr->dst_y;
				layer_bottom += imdecs[i]->bevel_v_addon ;
				if( (int)layer_bottom > max_y )
					max_y = layer_bottom;
			}
			pcurr = (pcurr->next!=NULL)?pcurr->next:pcurr+1 ;
		}
		if( min_y < 0 )
			min_y = 0 ;
		else if( min_y >= (int)dst_height )
			min_y = dst_height ;
			
		if( max_y >= (int)dst_height )
			max_y = dst_height ;
		else
			imout->tiling_step = max_y ;

LOCAL_DEBUG_OUT( "min_y = %d, max_y = %d", min_y, max_y );
		dst_line.back_color = imdecs[0]->back_color ;
		dst_line.flags = 0 ;
		for( y = 0 ; y < min_y ; ++y  )
			imout->output_image_scanline( imout, &dst_line, 1);
		dst_line.flags = SCL_DO_ALL ;
		pcurr = layers ;
		for( i = 0 ; i < count ; ++i )
		{
			if( imdecs[i] && pcurr->dst_y < min_y  )
				imdecs[i]->next_line = min_y - pcurr->dst_y ;
			pcurr = (pcurr->next!=NULL)?pcurr->next:pcurr+1 ;
		}
		for( ; y < max_y ; ++y  )
		{
			if( layers[0].dst_y <= y && bg_bottom > y )
				imdecs[0]->decode_image_scanline( imdecs[0] );
			else
			{
				imdecs[0]->buffer.back_color = imdecs[0]->back_color ;
				imdecs[0]->buffer.flags = 0 ;
			}
			copytintpad_scanline( &(imdecs[0]->buffer), &dst_line, layers[0].dst_x, bg_tint );
			pcurr = layers[0].next?layers[0].next:&(layers[1]) ;
			for( i = 1 ; i < count ; i++ )
			{
				if( imdecs[i] && pcurr->dst_y <= y &&
					pcurr->dst_y+(int)pcurr->clip_height+(int)imdecs[i]->bevel_v_addon > y )
				{
					register ASScanline *b = &(imdecs[i]->buffer);
					CARD32 tint = pcurr->tint ;
					imdecs[i]->decode_image_scanline( imdecs[i] );
					if( tint != 0 )
					{
						tint_component_mod( b->red,   (CARD16)(ARGB32_RED8(tint)<<1),   b->width );
						tint_component_mod( b->green, (CARD16)(ARGB32_GREEN8(tint)<<1), b->width );
  					   	tint_component_mod( b->blue,  (CARD16)(ARGB32_BLUE8(tint)<<1),  b->width );
					  	tint_component_mod( b->alpha, (CARD16)(ARGB32_ALPHA8(tint)<<1), b->width );
					}
					pcurr->merge_scanlines( &dst_line, b, pcurr->dst_x );
				}
				pcurr = (pcurr->next!=NULL)?pcurr->next:pcurr+1 ;
			}
			imout->output_image_scanline( imout, &dst_line, 1);
		}
		dst_line.back_color = imdecs[0]->back_color ;
		dst_line.flags = 0 ;
		for( ; y < (int)dst_height ; y++  )
			imout->output_image_scanline( imout, &dst_line, 1);
		stop_image_output( &imout );
	}
	for( i = 0 ; i < count ; i++ )
		if( imdecs[i] != NULL )
		{
			stop_image_decoding( &(imdecs[i]) );
		}
	free( imdecs );
	free_scanline( &dst_line, True );
	SHOW_TIME("", started);
	return dst;
}
