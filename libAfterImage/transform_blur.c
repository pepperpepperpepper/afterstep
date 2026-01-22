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
 * Gaussian blur code.
 **********************************************************************/

#undef PI
#define PI 3.141592526

#define GAUSS_COEFF_TYPE int
/* static void calc_gauss_double(double radius, double* gauss); */
static void calc_gauss_int(int radius, GAUSS_COEFF_TYPE* gauss, GAUSS_COEFF_TYPE* gauss_sums);

#define gauss_data_t CARD32
#define gauss_var_t int

static inline void
gauss_component_int(gauss_data_t *s1, gauss_data_t *d1, int radius, GAUSS_COEFF_TYPE* gauss, GAUSS_COEFF_TYPE* gauss_sums, int len)
{
#define DEFINE_GAUS_TMP_VAR		CARD32 *xs1 = &s1[x]; CARD32 v1 = xs1[0]*gauss[0]
	if( len < radius + radius )
	{
		int x = 0, j;
		while( x < len )
		{
			int tail = len - 1 - x;
			int gauss_sum = gauss[0];
			DEFINE_GAUS_TMP_VAR;
			for (j = 1 ; j <= x ; ++j)
			{
				v1 += xs1[-j]*gauss[j];
				gauss_sum += gauss[j];
			}
			for (j = 1 ; j <= tail ; ++j)
			{
				v1 += xs1[j]*gauss[j];
				gauss_sum += gauss[j];
			}
			d1[x] = (v1<<10)/gauss_sum;
			++x;
		}
		return;
	}

#define MIDDLE_STRETCH_GAUSS(j_check)	\
	do{ for( j = 1 ; j j_check ; ++j ) v1 += (xs1[-j]*gauss[j]+xs1[j]*gauss[j]); }while(0)

	/* left stretch [0, r-2] */
	{
		int x = 0 ;
		for( ; x < radius-1 ; ++x )
		{
			int j ;
			gauss_data_t *xs1 = &s1[x]; 
			gauss_var_t v1 = xs1[0]*gauss[0];
			for( j = 1 ; j <= x ; ++j )
				v1 += (xs1[-j]*gauss[j]+xs1[j]*gauss[j]);

			for( ; j < radius ; ++j ) 
				v1 += xs1[j]*gauss[j];
			d1[x] = (v1<<10)/gauss_sums[x];
		}	
	}

	/* middle stretch : [r-1, l-r] */
	if (radius-1 == len - radius)
	{
		gauss_data_t *xs1 = &s1[radius-1]; 
		gauss_var_t v1 = xs1[0]*gauss[0];
		int j = 1;
		for( ; j < radius ; ++j ) 
			v1 += (xs1[-j]*gauss[j]+xs1[j]*gauss[j]);
		d1[radius] = v1 ;
	}else
	{
		int x = radius;
		for(; x <= len - radius + 1; x+=3)
		{
			gauss_data_t *xs1 = &s1[x]; 
			gauss_var_t v1 = xs1[-1]*gauss[0];
			gauss_var_t v2 = xs1[0]*gauss[0];
			gauss_var_t v3 = xs1[1]*gauss[0];
			int j = 1;
			for( ; j < radius ; ++j ) 
			{
				int g = gauss[j];
				v1 += xs1[-j-1]*g+xs1[j-1]*g;
				v2 += xs1[-j]*g+xs1[j]*g;
				v3 += xs1[-j+1]*g+xs1[j+1]*g;
			}
			d1[x-1] = v1 ; 
			d1[x] = v2 ;
			d1[x+1] = v3 ;
		}
	}
	{
		int x = 0;
		gauss_data_t *td1 = &d1[len-1];
		for( ; x < radius-1; ++x )
		{
			int j;
			gauss_data_t *xs1 = &s1[len-1-x]; 
			gauss_var_t v1 = xs1[0]*gauss[0];
			for( j = 1 ; j <= x ; ++j ) 
				v1 += (xs1[-j]*gauss[j]+xs1[j]*gauss[j]);			
				
			for( ; j <radius ; ++j )
				v1 += xs1[-j]*gauss[j];
			td1[-x] = (v1<<10)/gauss_sums[x];
		}
	}
#undef 	MIDDLE_STRETCH_GAUSS
#undef DEFINE_GAUS_TMP_VAR
}

/*#define 	USE_PARALLEL_OPTIMIZATION */

#ifdef USE_PARALLEL_OPTIMIZATION
/* this ain't worth a crap it seems. The code below seems to perform 20% slower then 
   plain and simple one component at a time 
 */
static inline void
gauss_component_int2(CARD32 *s1, CARD32 *d1, CARD32 *s2, CARD32 *d2, int radius, GAUSS_COEFF_TYPE* gauss, GAUSS_COEFF_TYPE* gauss_sums, int len)
{
#define MIDDLE_STRETCH_GAUSS do{GAUSS_COEFF_TYPE g = gauss[j]; \
								v1 += (xs1[-j]+xs1[j])*g; \
								v2 += (xs2[-j]+xs2[j])*g; }while(0)

	int x, j;
	int tail = radius;
	GAUSS_COEFF_TYPE g0 = gauss[0];
	for( x = 0 ; x < radius ; ++x )
	{
		register CARD32 *xs1 = &s1[x];
		register CARD32 *xs2 = &s2[x];
		register CARD32 v1 = s1[x]*g0;
		register CARD32 v2 = s2[x]*g0;
		for( j = 1 ; j <= x ; ++j )
			MIDDLE_STRETCH_GAUSS;
		for( ; j < radius ; ++j ) 
		{
			GAUSS_COEFF_TYPE g = gauss[j];
			CARD32 m1 = xs1[j]*g;
			CARD32 m2 = xs2[j]*g;
			v1 += m1;
			v2 += m2;
		}
		v1 = v1<<10;
		v2 = v2<<10;
		{
			GAUSS_COEFF_TYPE gs = gauss_sums[x];
			d1[x] = v1/gs;
			d2[x] = v2/gs;
		}
	}	
	while( x <= len-radius )
	{
		register CARD32 *xs1 = &s1[x];
		register CARD32 *xs2 = &s2[x];
		register CARD32 v1 = s1[x]*g0;
		register CARD32 v2 = s2[x]*g0;
		for( j = 1 ; j < radius ; ++j ) 
			MIDDLE_STRETCH_GAUSS;
		d1[x] = v1 ;
		d2[x] = v2 ;
		++x;
	}
	while( --tail > 0 )/*x < len*/
	{
		register CARD32 *xs1 = &s1[x];
		register CARD32 *xs2 = &s2[x];
		register CARD32 v1 = xs1[0]*g0;
		register CARD32 v2 = xs2[0]*g0;
		for( j = 1 ; j < tail ; ++j ) 
			MIDDLE_STRETCH_GAUSS;
		for( ; j <radius ; ++j )
		{
			GAUSS_COEFF_TYPE g = gauss[j];
			CARD32 m1 = xs1[-j]*g;
			CARD32 m2 = xs2[-j]*g;
			v1 += m1;
			v2 += m2;
		}
		v1 = v1<<10;
		v2 = v2<<10;
		{
			GAUSS_COEFF_TYPE gs = gauss_sums[tail];
			d1[x] = v1/gs;
			d2[x] = v2/gs;
		}
		++x;
	}
#undef 	MIDDLE_STRETCH_GAUSS
}
#endif

static inline void
load_gauss_scanline(ASScanline *result, ASImageDecoder *imdec, int horz, GAUSS_COEFF_TYPE *sgauss, GAUSS_COEFF_TYPE *sgauss_sums, ASFlagType filter )
{
    ASFlagType lf; 
	int x, chan;
#ifdef USE_PARALLEL_OPTIMIZATION
	int todo_count = 0;
	int todo[IC_NUM_CHANNELS] = {-1,-1,-1,-1};
#endif
	imdec->decode_image_scanline(imdec);
	lf = imdec->buffer.flags&filter ;
	result->flags = imdec->buffer.flags;
	result->back_color = imdec->buffer.back_color;

	for( chan = 0 ; chan < IC_NUM_CHANNELS ; ++chan )
	{
		CARD32 *res_chan = result->channels[chan];
		CARD32 *src_chan = imdec->buffer.channels[chan];
		if( get_flags(lf, 0x01<<chan) )
		{
			if( horz == 1 ) 
			{
				for( x =  0 ; x < result->width ; ++x ) 
					res_chan[x] = src_chan[x]<<10 ;
			}else
			{
#ifdef USE_PARALLEL_OPTIMIZATION
				todo[todo_count++] = chan;
#else			
				gauss_component_int(src_chan, res_chan, horz, sgauss, sgauss_sums, result->width);
#endif
			}
	    }else if( get_flags( result->flags, 0x01<<chan ) )
	        copy_component( src_chan, res_chan, 0, result->width);
		else if( get_flags( filter, 0x01<<chan ) )
		{
			CARD32 fill = (CARD32)ARGB32_RED8(imdec->buffer.back_color)<<10;
			for( x =  0 ; x < result->width ; ++x ) res_chan[x] = fill ;
		}
	}

#ifdef USE_PARALLEL_OPTIMIZATION
	switch( 4 - todo_count )
	{
		case 0 : /* todo_count == 4 */
			gauss_component_int2(imdec->buffer.channels[todo[2]], result->channels[todo[2]],
								 imdec->buffer.channels[todo[3]], result->channels[todo[3]],
								 horz, sgauss, sgauss_sums, result->width);
		case 2 : /* todo_count == 2 */
			gauss_component_int2(imdec->buffer.channels[todo[0]], result->channels[todo[0]], 
								 imdec->buffer.channels[todo[1]], result->channels[todo[1]],
								 horz, sgauss, sgauss_sums, result->width); break ;
		case 1 : /* todo_count == 3 */
			gauss_component_int2(imdec->buffer.channels[todo[1]], result->channels[todo[1]],
								 imdec->buffer.channels[todo[2]], result->channels[todo[2]],
								 horz, sgauss, sgauss_sums, result->width);
		case 3 : /* todo_count == 1 */
			gauss_component_int( imdec->buffer.channels[todo[0]], 
								 result->channels[todo[0]], 
								 horz, sgauss, sgauss_sums, result->width); break ;
	}
#endif
}


ASImage* blur_asimage_gauss(ASVisual* asv, ASImage* src, double dhorz, double dvert,
                            ASFlagType filter,
							ASAltImFormats out_format, unsigned int compression_out, int quality)
{
	ASImage *dst = NULL;
	ASImageOutput *imout;
	ASImageDecoder *imdec;
	int y, x, chan;
	int horz = (int)dhorz;
	int vert = (int)dvert;
	int width, height ; 
#define PRINT_BACKGROUND_OP_TIME do{}while(0)                                          

	if (!src) return NULL;

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	width = src->width ;
	height = src->height ;
	dst = create_destination_image( width, height, out_format, compression_out, src->back_color);

	imout = start_image_output(asv, dst, out_format, 0, quality);
    if (!imout)
    {
        destroy_asimage( &dst );
		return NULL;
	}

	imdec = start_image_decoding(asv, src, SCL_DO_ALL, 0, 0, src->width, src->height, NULL);
	if (!imdec) 
	{
		stop_image_output(&imout);
        destroy_asimage( &dst );
		return NULL;
	}
	
	if( horz > (width-1)/2  ) horz = (width==1 )?1:(width-1)/2 ;
	if( vert > (height-1)/2 ) vert = (height==1)?1:(height-1)/2 ;
	if (horz > 128) 
		horz = 128;
	else if (horz < 1) 
		horz = 1;
	if( vert > 128 )
		vert = 128 ;
	else if( vert < 1 ) 
		vert = 1 ;

	if( vert == 1 && horz == 1 ) 
	{
	    for (y = 0 ; y < dst->height ; y++)
		{
			imdec->decode_image_scanline(imdec);
	        imout->output_image_scanline(imout, &(imdec->buffer), 1);
		}
	}else
	{
		ASScanline result;
		GAUSS_COEFF_TYPE *horz_gauss = NULL;
		GAUSS_COEFF_TYPE *horz_gauss_sums = NULL;

		if( horz > 1 )
		{
			PRINT_BACKGROUND_OP_TIME;
			horz_gauss = safecalloc(horz+1, sizeof(GAUSS_COEFF_TYPE));
			horz_gauss_sums = safecalloc(horz+1, sizeof(GAUSS_COEFF_TYPE));
			calc_gauss_int(horz, horz_gauss, horz_gauss_sums);
			PRINT_BACKGROUND_OP_TIME;
		}
		prepare_scanline(width, 0, &result, asv->BGR_mode);
		if( vert == 1 ) 
		{
		    for (y = 0 ; y < height ; y++)
		    {
				load_gauss_scanline(&result, imdec, horz, horz_gauss, horz_gauss_sums, filter );
				for( chan = 0 ; chan < IC_NUM_CHANNELS ; ++chan )
					if( get_flags( filter, 0x01<<chan ) )
					{
						CARD32 *res_chan = result.channels[chan];
						for( x = 0 ; x < width ; ++x ) 
							res_chan[x] = (res_chan[x]&0x03Fc0000)?255:res_chan[x]>>10;
					}
		        imout->output_image_scanline(imout, &result, 1);
			}
		}else
		{ /* new code : */
			GAUSS_COEFF_TYPE *vert_gauss = safecalloc(vert+1, sizeof(GAUSS_COEFF_TYPE));
			GAUSS_COEFF_TYPE *vert_gauss_sums = safecalloc(vert+1, sizeof(GAUSS_COEFF_TYPE));
			int lines_count = vert*2-1;
			int first_line = 0, last_line = lines_count-1;
			ASScanline *lines_mem = safecalloc( lines_count, sizeof(ASScanline));
			ASScanline **lines = safecalloc( dst->height+1, sizeof(ASScanline*));

			/* init */
			calc_gauss_int(vert, vert_gauss, vert_gauss_sums);
			PRINT_BACKGROUND_OP_TIME;

			for( y = 0 ; y < lines_count ; ++y ) 
			{
				lines[y] = &lines_mem[y] ;
				prepare_scanline(width, 0, lines[y], asv->BGR_mode);
				load_gauss_scanline(lines[y], imdec, horz, horz_gauss, horz_gauss_sums, filter );
			}

			PRINT_BACKGROUND_OP_TIME;
			result.flags = 0xFFFFFFFF;
			/* top band  [0, vert-2] */
    		for (y = 0 ; y < vert-1 ; y++)
    		{
				for( chan = 0 ; chan < IC_NUM_CHANNELS ; ++chan )
				{
					CARD32 *res_chan = result.channels[chan];
					if( !get_flags(filter, 0x01<<chan) )
		        		copy_component( lines[y]->channels[chan], res_chan, 0, width);
					else
					{	
						register ASScanline **ysrc = &lines[y];
						int j = 0;
						GAUSS_COEFF_TYPE g = vert_gauss[0];
						CARD32 *src_chan1 = ysrc[0]->channels[chan];
						for( x = 0 ; x < width ; ++x ) 
							res_chan[x] = src_chan1[x]*g;
						while( ++j <= y )
						{
							CARD32 *src_chan2 = ysrc[j]->channels[chan];
							g = vert_gauss[j];
							src_chan1 = ysrc[-j]->channels[chan];
							for( x = 0 ; x < width ; ++x ) 
								res_chan[x] += (src_chan1[x]+src_chan2[x])*g;
						}	
						for( ; j < vert ; ++j ) 
						{
							g = vert_gauss[j];
							src_chan1 = ysrc[j]->channels[chan];
							for( x = 0 ; x < width ; ++x ) 
								res_chan[x] += src_chan1[x]*g;
						}
						g = vert_gauss_sums[y];
						for( x = 0 ; x < width ; ++x ) 
						{
							gauss_var_t v = res_chan[x]/g;
							res_chan[x] = (v&0x03Fc0000)?255:v>>10;
						}
					}
				}
        		imout->output_image_scanline(imout, &result, 1);
			}
			PRINT_BACKGROUND_OP_TIME;
			/* middle band [vert-1, height-vert] */
			for( ; y <= height - vert; ++y)
			{
				for( chan = 0 ; chan < IC_NUM_CHANNELS ; ++chan )
				{
					CARD32 *res_chan = result.channels[chan];
					if( !get_flags(filter, 0x01<<chan) )
		        		copy_component( lines[y]->channels[chan], res_chan, 0, result.width);
					else
					{	
						register ASScanline **ysrc = &lines[y];
/* surprisingly, having x loops inside y loop yields 30% to 80% better performance */
						int j = 0;
						CARD32 *src_chan1 = ysrc[0]->channels[chan];
						memset( res_chan, 0x00, width*4 );
						while( ++j < vert ) 
						{
							CARD32 *src_chan2 = ysrc[j]->channels[chan];
							GAUSS_COEFF_TYPE g = vert_gauss[j];
							src_chan1 = ysrc[-j]->channels[chan];
							switch( g ) 
							{
								case 1 :
									for( x = 0 ; x < width ; ++x ) 
										res_chan[x] += src_chan1[x]+src_chan2[x];
									break;
								case 2 :
									for( x = 0 ; x < width ; ++x ) 
										res_chan[x] += (src_chan1[x]+src_chan2[x])<<1;
									break;
								case 4 :
									for( x = 0 ; x < width ; ++x ) 
										res_chan[x] += (src_chan1[x]+src_chan2[x])<<2;
									break;
								case 8 :
									for( x = 0 ; x < width ; ++x ) 
										res_chan[x] += (src_chan1[x]+src_chan2[x])<<3;
									break;
								case 16 :
									for( x = 0 ; x < width ; ++x ) 
										res_chan[x] += (src_chan1[x]+src_chan2[x])<<4;
									break;
								case 32 :
									for( x = 0 ; x < width ; ++x ) 
										res_chan[x] += (src_chan1[x]+src_chan2[x])<<5;
									break;
								default : 									
									for( x = 0 ; x < width ; ++x ) 
										res_chan[x] += (src_chan1[x]+src_chan2[x])*g;
							}
						}
 						src_chan1 = ysrc[0]->channels[chan];
						for( x = 0 ; x < width ; ++x ) 
						{
							gauss_var_t v = src_chan1[x]*vert_gauss[0] + res_chan[x];
							res_chan[x] = (v&0xF0000000)?255:v>>20;
						} 
					}
				}

        		imout->output_image_scanline(imout, &result, 1);
				++last_line;
				/* fprintf( stderr, "last_line = %d, first_line = %d, height = %d, vert = %d, y = %d\n", last_line, first_line, dst->height, vert, y ); */
				lines[last_line] = lines[first_line] ; 
				++first_line;
				load_gauss_scanline(lines[last_line], imdec, horz, horz_gauss, horz_gauss_sums, filter );
			}
			PRINT_BACKGROUND_OP_TIME;
			/* bottom band */
			for( ; y < height; ++y)
			{
				int tail = height - y ; 
				for( chan = 0 ; chan < IC_NUM_CHANNELS ; ++chan )
				{
					CARD32 *res_chan = result.channels[chan];
					if( !get_flags(filter, 0x01<<chan) )
		        		copy_component( lines[y]->channels[chan], res_chan, 0, result.width);
					else
					{	
						register ASScanline **ysrc = &lines[y];
						int j = 0;
						GAUSS_COEFF_TYPE g ;
						CARD32 *src_chan1 = ysrc[0]->channels[chan];
						for( x = 0 ; x < width ; ++x ) 
							res_chan[x] = src_chan1[x]*vert_gauss[0];
						for( j = 1 ; j < tail ; ++j ) 
						{
							CARD32 *src_chan2 = ysrc[j]->channels[chan];
							g = vert_gauss[j];
							src_chan1 = ysrc[-j]->channels[chan];
							for( x = 0 ; x < width ; ++x ) 
								res_chan[x] += (src_chan1[x]+src_chan2[x])*g;
						}
						for( ; j < vert ; ++j )
						{
							g = vert_gauss[j];
							src_chan1 = ysrc[-j]->channels[chan];
							for( x = 0 ; x < width ; ++x ) 
								res_chan[x] += src_chan1[x]*g;
						}
						g = vert_gauss_sums[tail];
						for( x = 0 ; x < width ; ++x ) 
						{
							gauss_var_t v = res_chan[x]/g;
							res_chan[x] = (v&0x03Fc0000)?255:v>>10;
						}
					}
				}

        		imout->output_image_scanline(imout, &result, 1);
			}
			/* cleanup */
			for( y = 0 ; y < lines_count ; ++y ) 
				free_scanline(&lines_mem[y], True);
			free( lines_mem );
			free( lines );
			free(vert_gauss_sums);
			free(vert_gauss);

		}
		free_scanline(&result, True);
		if( horz_gauss_sums )
			free(horz_gauss_sums);
		if( horz_gauss )
			free(horz_gauss);
	}
PRINT_BACKGROUND_OP_TIME;

	stop_image_decoding(&imdec);
	stop_image_output(&imout);

	return dst;
}

/* even though lookup tables take space - using those speeds kernel calculations tenfold */
static const double standard_deviations[128] = 
{
		 0.0,       0.300387,  0.600773,  0.901160,  1.201547,  1.501933,  1.852320,  2.202706,  2.553093,  2.903480,  3.253866,  3.604253,  3.954640,  4.355026,  4.705413,  5.105799, 
		 5.456186,  5.856573,  6.256959,  6.657346,  7.057733,  7.458119,  7.858506,  8.258892,  8.659279,  9.059666,  9.510052,  9.910439, 10.360826, 10.761212, 11.211599, 11.611986, 
		12.062372, 12.512759, 12.963145, 13.413532, 13.863919, 14.314305, 14.764692, 15.215079, 15.665465, 16.115852, 16.566238, 17.066625, 17.517012, 18.017398, 18.467785, 18.968172, 
		19.418558, 19.918945, 20.419332, 20.869718, 21.370105, 21.870491, 22.370878, 22.871265, 23.371651, 23.872038, 24.372425, 24.872811, 25.373198, 25.923584, 26.423971, 26.924358, 
		27.474744, 27.975131, 28.525518, 29.025904, 29.576291, 30.126677, 30.627064, 31.177451, 31.727837, 32.278224, 32.828611, 33.378997, 33.929384, 34.479771, 35.030157, 35.580544, 
		36.130930, 36.731317, 37.281704, 37.832090, 38.432477, 38.982864, 39.583250, 40.133637, 40.734023, 41.334410, 41.884797, 42.485183, 43.085570, 43.685957, 44.286343, 44.886730, 
		45.487117, 46.087503, 46.687890, 47.288276, 47.938663, 48.539050, 49.139436, 49.789823, 50.390210, 50.990596, 51.640983, 52.291369, 52.891756, 53.542143, 54.192529, 54.842916, 
		55.443303, 56.093689, 56.744076, 57.394462, 58.044849, 58.745236, 59.395622, 60.046009, 60.696396, 61.396782, 62.047169, 62.747556, 63.397942, 64.098329, 64.748715, 65.449102
	
};

static const double descr_approxim_mult[128] = 
{
		 0.0,             576.033927, 1539.585724, 2313.193545, 3084.478025, 3855.885078, 4756.332754, 5657.242476, 6558.536133, 7460.139309, 8361.990613, 9264.041672, 10166.254856, 11199.615571, 12102.233350, 13136.515398, 
		 14039.393687,  15074.393173, 16109.866931, 17145.763345, 18182.036948, 19218.647831, 20255.561010, 21292.745815, 22330.175327, 23367.825876, 24540.507339, 25578.741286, 26752.587529, 27791.291872, 28966.144174, 30005.229117, 
		 31180.955186,  32357.252344, 33534.082488, 34711.410459, 35889.203827, 37067.432679, 38246.069415, 39425.088562, 40604.466591, 41784.181759, 42964.213952, 44284.538859, 45465.382595, 46787.130142, 47968.686878, 49291.724522, 
		 50473.909042,  51798.119528, 53123.060725, 54306.137507, 55632.091099, 56958.688068, 58285.899344, 59613.697438, 60942.056354, 62270.951500, 63600.359608, 64930.258655, 66260.627789, 67737.102560, 69068.620203, 70400.544942, 
		 71879.460632,  73212.395873, 74692.932606, 76026.792904, 77508.839552, 78991.791376, 80327.002820, 81811.308203, 83296.434414, 84782.353155, 86269.037314, 87756.460905, 89244.599028, 90733.427810, 92222.924365, 93713.066749, 
		 95203.833910,  96847.414084, 98339.659244, 99832.465294, 101479.012792, 102973.158567, 104621.682880, 106117.081106, 107767.473327, 109418.953577, 110916.202212, 112569.394820, 114223.592283, 115878.766626, 117534.890826, 119191.938777, 
		120849.885258, 122508.705901, 124168.377156, 125828.876263, 127648.916790, 129311.319925, 130974.481906, 132798.169283, 134463.087846, 136128.703019, 137955.784611, 139784.215161, 141452.370894, 143283.009733, 145114.908442, 146948.037106, 
		148619.357599, 150454.489089, 152290.771330, 154128.177628, 155966.682058, 157971.069246, 159812.039780, 161654.030102, 163497.017214, 165507.250512, 167352.489586, 169365.683736, 171213.071546, 173229.105499, 175078.544507, 177097.303447
	
};

static void calc_gauss_int(int radius, GAUSS_COEFF_TYPE* gauss, GAUSS_COEFF_TYPE* gauss_sums) 
{
	int i = radius;
	double dmult;
	double std_dev;
	if (i <= 1) 
	{
		gauss[0] = 1024;
		gauss_sums[0] = 1024;
		return;
	}
	/* after radius of 128 - gaussian degrades into something weird,
	   since our colors are only 8 bit */
	if (i > 128) i = 128; 
#if 1
	{
		double nn;
		GAUSS_COEFF_TYPE sum = 1024 ;
		std_dev = standard_deviations[i-1];
		dmult = descr_approxim_mult[i-1];
		nn = 2*std_dev * std_dev ;
		dmult /=nn*PI;
		gauss[0] = (GAUSS_COEFF_TYPE)(dmult + 0.5);
		while( i >= 1 )
		{
			gauss[i] = (GAUSS_COEFF_TYPE)(exp((double)-i * (double)i / nn)*dmult + 0.5);
			gauss_sums[i] = sum;
			sum -= gauss[i];
			--i;
		}
		gauss_sums[0] = sum;
	}
#else 
	double g0, g_last, sum = 0.;
	double n, nn, nPI, nnPI;
	std_dev = (radius - 1) * 0.3003866304;
	do
	{
		sum = 0 ;
		n = std_dev * std_dev;
		nn = 2*n ;
		nPI = n*PI;
		nnPI = nn*PI;
		sum = g0 = 1.0 / nnPI ;
		for (i = 1 ; i < radius-1 ; i++) 
			sum += exp((double)-i * (double)i / nn)/nPI; 
		g_last = exp((double)-i * (double)i / nn)/nnPI; 
		sum += g_last*2.0 ; 
	
		dmult = 1024.0/sum;
		std_dev += 0.05 ;
	}while( g_last*dmult  < 1. );
	gauss[0] = g0 * dmult + 0.5; 
	gauss[(int)radius-1] = g_last * dmult + 0.5;
	dmult /=nnPI;
	for (i = 1 ; i < radius-1 ; i++)
		gauss[i] = exp((double)-i * (double)i / nn)*dmult + 0.5;

#endif	

#if 0
	{
		static int count = 0 ; 
		if( ++count == 16 ) 
		{
			fprintf( stderr, "\n		" );
			count = 0 ;
		}
			
		fprintf(stderr, "%lf, ", dmult*nnPI );			
	}
#endif
#if 0
	{
		int sum_da = 0 ;
		fprintf(stderr, "sum = %f, dmult = %f\n", sum, dmult );
		for (i = 0 ; i < radius ; i++)
		{
//			gauss[i] /= sum;
			sum_da += gauss[i]*2 ;
			fprintf(stderr, "discr_approx(%d) = %d\n", i, gauss[i]);
		}
		sum_da -= gauss[0];
	
		fprintf(stderr, "sum_da = %d\n", sum_da );			
	}
#endif	
}
