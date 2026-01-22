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
/* ********************************************************************************/
/* Vector -> ASImage functions :                                                  */
/* ********************************************************************************/
Bool
colorize_asimage_vector( ASVisual *asv, ASImage *im,
						 ASVectorPalette *palette,
						 ASAltImFormats out_format,
						 int quality )
{
	ASImageOutput  *imout = NULL ;
	ASScanline buf ;
	int x, y, curr_point, last_point ;
    register double *vector ;
	double *points ;
	double *multipliers[IC_NUM_CHANNELS] ;
	START_TIME(started);

	if( im == NULL || palette == NULL || out_format == ASA_Vector )
		return False;

	if( im->alt.vector == NULL )
		return False;
	vector = im->alt.vector ;

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	if((imout = start_image_output( asv, im, out_format, QUANT_ERR_BITS, quality)) == NULL )
		return False;
	/* as per ROOT ppl request double data goes from bottom to top,
	 * instead of from top to bottom : */
	if( !get_flags( im->flags, ASIM_VECTOR_TOP2BOTTOM) )
		toggle_image_output_direction(imout);

	prepare_scanline( im->width, QUANT_ERR_BITS, &buf, asv->BGR_mode );
	curr_point = palette->npoints/2 ;
	points = palette->points ;
	last_point = palette->npoints-1 ;
	buf.flags = 0 ;
	for( y = 0 ; y < IC_NUM_CHANNELS ; ++y )
	{
		if( palette->channels[y] )
		{
			multipliers[y] = safemalloc( last_point*sizeof(double));
			for( x = 0 ; x < last_point ; ++x )
			{
				if (points[x+1] == points[x])
      				multipliers[y][x] = 1;
				else
					multipliers[y][x] = (double)(palette->channels[y][x+1] - palette->channels[y][x])/
				                 	        (points[x+1]-points[x]);
/*				fprintf( stderr, "%e-%e/%e-%e=%e ", (double)palette->channels[y][x+1], (double)palette->channels[y][x],
				                 	        points[x+1], points[x], multipliers[y][x] );
 */
			}
/*			fputc( '\n', stderr ); */
			set_flags(buf.flags, (0x01<<y));
		}else
			multipliers[y] = NULL ;
	}
	for( y = 0 ; y < (int)im->height ; ++y )
	{
		for( x = 0 ; x < (int)im->width ;)
		{
			register int i = IC_NUM_CHANNELS ;
			double d ;

			if( points[curr_point] > vector[x] )
			{
				while( --curr_point >= 0 )
					if( points[curr_point] < vector[x] )
						break;
				if( curr_point < 0 )
					++curr_point ;
			}else
			{
				while( points[curr_point+1] < vector[x] )
					if( ++curr_point >= last_point )
					{
						curr_point = last_point-1 ;
						break;
					}
			}
			d = vector[x]-points[curr_point];

			while( --i >= 0 )
				if( multipliers[i] )
				{/* the following calculation is the most expensive part of the algorithm : */
					buf.channels[i][x] = (int)(d*multipliers[i][curr_point])+palette->channels[i][curr_point] ;
				}

			while( ++x < (int)im->width )
				if( vector[x] == vector[x-1] )
				{
					buf.red[x] = buf.red[x-1] ;
					buf.green[x] = buf.green[x-1] ;
					buf.blue[x] = buf.blue[x-1] ;
					buf.alpha[x] = buf.alpha[x-1] ;
				}else
					break;
		}

		imout->output_image_scanline( imout, &buf, 1);
		vector += im->width ;
	}
	for( y = 0 ; y < IC_NUM_CHANNELS ; ++y )
		if( multipliers[y] )
			free(multipliers[y]);

	stop_image_output( &imout );
	free_scanline( &buf, True );
	SHOW_TIME("", started);
	return True;
}

ASImage *
create_asimage_from_vector( ASVisual *asv, double *vector,
							int width, int height,
							ASVectorPalette *palette,
							ASAltImFormats out_format,
							unsigned int compression, int quality )
{
	ASImage *im = NULL;

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	if( vector != NULL )
	{
		im = create_destination_image( width, height, out_format, compression, ARGB32_DEFAULT_BACK_COLOR);

		if( im != NULL )
		{
			if( set_asimage_vector( im, vector ) )
				if( palette )
					colorize_asimage_vector( asv, im, palette, out_format, quality );
		}
	}
	return im ;
}
