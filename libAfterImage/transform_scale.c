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
/* ******************************************************************************/
/* below goes all kinds of funky stuff we can do with scanlines : 			   */
/* ******************************************************************************/
/* this will enlarge array based on count of items in dst per PAIR of src item with smoothing/scatter/dither */
/* the following formulas use linear approximation to calculate   */
/* color values for new pixels : 				  				  */
/* for scale factor of 2 we use this formula :    */
/* C = (-C1+3*C2+3*C3-C4)/4 					  */
/* or better :				 					  */
/* C = (-C1+5*C2+5*C3-C4)/8 					  */
#define INTERPOLATE_COLOR1(c) 			   	((c)<<QUANT_ERR_BITS)  /* nothing really to interpolate here */
#define INTERPOLATE_COLOR2(c1,c2,c3,c4)    	((((c2)<<2)+(c2)+((c3)<<2)+(c3)-(c1)-(c4))*(1<<(QUANT_ERR_BITS-3)))
#define INTERPOLATE_COLOR2_V(c1,c2,c3,c4)    	((((c2)<<2)+(c2)+((c3)<<2)+(c3)-(c1)-(c4))>>3)
/* for scale factor of 3 we use these formulas :  */
/* Ca = (-2C1+8*C2+5*C3-2C4)/9 		  			  */
/* Cb = (-2C1+5*C2+8*C3-2C4)/9 		  			  */
/* or better : 									  */
/* Ca = (-C1+5*C2+3*C3-C4)/6 		  			  */
/* Cb = (-C1+3*C2+5*C3-C4)/6 		  			  */
#define INTERPOLATE_A_COLOR3(c1,c2,c3,c4)  	(((((c2)<<2)+(c2)+((c3)<<1)+(c3)-(c1)-(c4))*(1<<QUANT_ERR_BITS))/6)
#define INTERPOLATE_B_COLOR3(c1,c2,c3,c4)  	(((((c2)<<1)+(c2)+((c3)<<2)+(c3)-(c1)-(c4))*(1<<QUANT_ERR_BITS))/6)
#define INTERPOLATE_A_COLOR3_V(c1,c2,c3,c4)  	((((c2)<<2)+(c2)+((c3)<<1)+(c3)-(c1)-(c4))/6)
#define INTERPOLATE_B_COLOR3_V(c1,c2,c3,c4)  	((((c2)<<1)+(c2)+((c3)<<2)+(c3)-(c1)-(c4))/6)
/* just a hypotesus, but it looks good for scale factors S > 3: */
/* Cn = (-C1+(2*(S-n)+1)*C2+(2*n+1)*C3-C4)/2S  	  			   */
/* or :
 * Cn = (-C1+(2*S+1)*C2+C3-C4+n*(2*C3-2*C2)/2S  			   */
/*       [ T                   [C2s]  [C3s]]   			       */
#define INTERPOLATION_Cs(c)	 		 	    ((c)<<1)
/*#define INTERPOLATION_TOTAL_START(c1,c2,c3,c4,S) 	(((S)<<1)*(c2)+((c3)<<1)+(c3)-c2-c1-c4)*/
#define INTERPOLATION_TOTAL_START(c1,c2,c3,c4,S) 	((((S)<<1)+1)*(c2)+(c3)-(c1)-(c4))
#define INTERPOLATION_TOTAL_STEP(c2,c3)  	((c3<<1)-(c2<<1))
#define INTERPOLATE_N_COLOR(T,S)		  	(((T)*(1<<(QUANT_ERR_BITS-1)))/(S))

#define AVERAGE_COLOR1(c) 					((c)<<QUANT_ERR_BITS)
#define AVERAGE_COLOR2(c1,c2)				(((c1)+(c2))<<(QUANT_ERR_BITS-1))
#define AVERAGE_COLORN(T,N)					(((T)<<QUANT_ERR_BITS)/N)

static inline void
enlarge_component12( register CARD32 *src, register CARD32 *dst, int *scales, int len )
{/* expected len >= 2  */
	register int i = 0, k = 0;
	register int c1 = src[0], c4;
	--len; --len ;
	while( i < len )
	{
		c4 = src[i+2];
		/* that's right we can do that PRIOR as we calculate nothing */
		dst[k] = INTERPOLATE_COLOR1(src[i]) ;
		if( scales[i] == 2 )
		{
			register int c2 = src[i], c3 = src[i+1] ;
			c3 = INTERPOLATE_COLOR2(c1,c2,c3,c4);
			dst[++k] = (c3&0xFF000000 )?0:c3;
		}
		c1 = src[i];
		++k;
		++i;
	}

	/* to avoid one more if() in loop we moved tail part out of the loop : */
	if( scales[i] == 1 )
		dst[k] = INTERPOLATE_COLOR1(src[i]);
	else
	{
		register int c2 = src[i], c3 = src[i+1] ;
		c2 = INTERPOLATE_COLOR2(c1,c2,c3,c3);
		dst[k] = (c2&0xFF000000 )?0:c2;
	}
	dst[k+1] = INTERPOLATE_COLOR1(src[i+1]);
}

static inline void
enlarge_component23( register CARD32 *src, register CARD32 *dst, int *scales, int len )
{/* expected len >= 2  */
	register int i = 0, k = 0;
	register int c1 = src[0], c4 = src[1];
	if( scales[0] == 1 )
	{/* special processing for first element - it can be 1 - others can only be 2 or 3 */
		dst[k] = INTERPOLATE_COLOR1(src[0]) ;
		++k;
		++i;
	}
	--len; --len ;
	while( i < len )
	{
		register int c2 = src[i], c3 = src[i+1] ;
		c4 = src[i+2];
		dst[k] = INTERPOLATE_COLOR1(c2) ;
		if( scales[i] == 2 )
		{
			c3 = INTERPOLATE_COLOR2(c1,c2,c3,c3);
			dst[++k] = (c3&0x7F000000 )?0:c3;
		}else
		{
			dst[++k] = INTERPOLATE_A_COLOR3(c1,c2,c3,c4);
			if( dst[k]&0x7F000000 )
				dst[k] = 0 ;
			c3 = INTERPOLATE_B_COLOR3(c1,c2,c3,c3);
			dst[++k] = (c3&0x7F000000 )?0:c3;
		}
		c1 = c2 ;
		++k;
		++i;
	}
	/* to avoid one more if() in loop we moved tail part out of the loop : */
	{
		register int c2 = src[i], c3 = src[i+1] ;
		dst[k] = INTERPOLATE_COLOR1(c2) ;
		if( scales[i] == 2 )
		{
			c2 = INTERPOLATE_COLOR2(c1,c2,c3,c3);
			dst[k+1] = (c2&0x7F000000 )?0:c2;
		}else
		{
			if( scales[i] == 1 )
				--k;
			else
			{
				dst[++k] = INTERPOLATE_A_COLOR3(c1,c2,c3,c3);
				if( dst[k]&0x7F000000 )
					dst[k] = 0 ;
				c2 = INTERPOLATE_B_COLOR3(c1,c2,c3,c3);
  				dst[k+1] = (c2&0x7F000000 )?0:c2;
			}
		}
	}
 	dst[k+2] = INTERPOLATE_COLOR1(src[i+1]) ;
}

/* this case is more complex since we cannot really hardcode coefficients
 * visible artifacts on smooth gradient-like images
 */
static inline void
enlarge_component( register CARD32 *src, register CARD32 *dst, int *scales, int len )
{/* we skip all checks as it is static function and we want to optimize it
  * as much as possible */
	int i = 0;
	int c1 = src[0];
	register int T ;
	--len ;
	if( len < 1 )
	{
		CARD32 c = INTERPOLATE_COLOR1(c1) ;
		for( i = 0 ; i < scales[0] ; ++i )
			dst[i] = c;
		return;
	}
	do
	{
		register short S = scales[i];
		register int step = INTERPOLATION_TOTAL_STEP(src[i],src[i+1]);

		if( i+1 == len )
			T = INTERPOLATION_TOTAL_START(c1,src[i],src[i+1],src[i+1],S);
		else
			T = INTERPOLATION_TOTAL_START(c1,src[i],src[i+1],src[i+2],S);

/*		LOCAL_DEBUG_OUT( "pixel %d, S = %d, step = %d", i, S, step );*/
		if( step )
		{
			register int n = 0 ;
			do
			{
				dst[n] = (T&0x7F000000)?0:INTERPOLATE_N_COLOR(T,S);
				if( ++n >= S ) break;
				T = (int)T + (int)step;
			}while(1);
			dst += n ;
		}else
		{
			register CARD32 c = (T&0x7F000000)?0:INTERPOLATE_N_COLOR(T,S);
			while(--S >= 0){	dst[S] = c;	}
			dst += scales[i] ;
		}
		c1 = src[i];
	}while(++i < len );
	*dst = INTERPOLATE_COLOR1(src[i]) ;
/*LOCAL_DEBUG_OUT( "%d pixels written", k );*/
}

static inline void
enlarge_component_dumb( register CARD32 *src, register CARD32 *dst, int *scales, int len )
{/* we skip all checks as it is static function and we want to optimize it
  * as much as possible */
	int i = 0, k = 0;
	do
	{
		register CARD32 c = INTERPOLATE_COLOR1(src[i]);
		int max_k = k+scales[i];
		do
		{
			dst[k] = c ;
		}while( ++k < max_k );
	}while( ++i < len );
}

/* this will shrink array based on count of items in src per one dst item with averaging */
static inline void
shrink_component( register CARD32 *src, register CARD32 *dst, int *scales, int len )
{/* we skip all checks as it is static function and we want to optimize it
  * as much as possible */
	register int i = -1, k = -1;
	while( ++k < len )
	{
		register int reps = scales[k] ;
		register int c1 = src[++i];
/*LOCAL_DEBUG_OUT( "pixel = %d, scale[k] = %d", k, reps );*/
		if( reps == 1 )
			dst[k] = AVERAGE_COLOR1(c1);
		else if( reps == 2 )
		{
			++i;
			dst[k] = AVERAGE_COLOR2(c1,src[i]);
		}else
		{
			reps += i-1;
			while( reps > i )
			{
				++i ;
				c1 += src[i];
			}
			{
				register short S = scales[k];
				dst[k] = AVERAGE_COLORN(c1,S);
			}
		}
	}
}
static inline void
shrink_component11( register CARD32 *src, register CARD32 *dst, int *scales, int len )
{
	register int i ;
	for( i = 0 ; i < len ; ++i )
		dst[i] = AVERAGE_COLOR1(src[i]);
}


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

static inline void
add_component( CARD32 *src, CARD32 *incr, int *scales, int len )
{
	len += len&0x01;
#ifdef HAVE_MMX   
#if 1
	if( asimage_use_mmx )
	{
		int i = 0;
		__m64  *vdst = (__m64*)&(src[0]);
		__m64  *vinc = (__m64*)&(incr[0]);
		len = len>>1;
		do{
			vdst[i] = _mm_add_pi32(vdst[i],vinc[i]);  /* paddd */
		}while( ++i  < len );
		_mm_empty();
	}else
#else
	if( asimage_use_mmx )
	{
		double *ddst = (double*)&(src[0]);
		double *dinc = (double*)&(incr[0]);
		len = len>>1;
		do{
			asm volatile
       		(
            	"movq %0, %%mm0  \n\t" /* load 8 bytes from src[i] into MM0 */
            	"paddd %1, %%mm0 \n\t" /* MM0=src[i]>>1              */
            	"movq %%mm0, %0  \n\t" /* store the result in dest */
				: "=m" (ddst[i])       /* %0 */
				:  "m"  (dinc[i])       /* %2 */
	        );
		}while( ++i < len );
	}else
#endif
#endif
	{
		register int c1, c2;
		int i = 0;
		do{
			c1 = (int)src[i] + (int)incr[i] ;
			c2 = (int)src[i+1] + (int)incr[i+1] ;
			src[i] = c1;
			src[i+1] = c2;
			i += 2 ;
		}while( i < len );
	}
}

#ifdef NEED_RBITSHIFT_FUNCS
static inline void
rbitshift_component( register CARD32 *src, register CARD32 *dst, int shift, int len )
{
	register int i ;
	for( i = 0 ; i < len ; ++i )
		dst[i] = src[i]>>shift;
}
#endif

static inline void
start_component_interpolation( CARD32 *c1, CARD32 *c2, CARD32 *c3, CARD32 *c4, register CARD32 *T, register CARD32 *step, int S, int len)
{
	register int i;
	for( i = 0 ; i < len ; i++ )
	{
		register int rc2 = c2[i], rc3 = c3[i] ;
		T[i] = INTERPOLATION_TOTAL_START(c1[i],rc2,rc3,c4[i],S)/(S<<1);
		step[i] = INTERPOLATION_TOTAL_STEP(rc2,rc3)/(S<<1);
	}
}

static void
component_interpolation_hardcoded( CARD32 *c1, CARD32 *c2, CARD32 *c3, CARD32 *c4, register CARD32 *T, CARD32 *unused, CARD16 kind, int len)
{
	register int i;
	if( kind == 1 )
	{
		for( i = 0 ; i < len ; i++ )
		{
			/* its seems that this simple formula is completely sufficient
			   and even better than more complicated one : */
			T[i] = (c2[i]+c3[i])>>1 ;
		}
	}else if( kind == 2 )
	{
		for( i = 0 ; i < len ; i++ )
		{
    		register int rc1 = c1[i], rc2 = c2[i], rc3 = c3[i] ;
			T[i] = INTERPOLATE_A_COLOR3_V(rc1,rc2,rc3,c4[i]);
		}
	}else
		for( i = 0 ; i < len ; i++ )
		{
    		register int rc1 = c1[i], rc2 = c2[i], rc3 = c3[i] ;
			T[i] = INTERPOLATE_B_COLOR3_V(rc1,rc2,rc3,c4[i]);
		}
}

#ifdef NEED_RBITSHIFT_FUNCS
static inline void
divide_component_mod( register CARD32 *data, CARD16 ratio, int len )
{
	register int i ;
	for( i = 0 ; i < len ; ++i )
		data[i] /= ratio;
}

static inline void
rbitshift_component_mod( register CARD32 *data, int bits, int len )
{
	register int i ;
	for( i = 0 ; i < len ; ++i )
		data[i] = data[i]>>bits;
}
#endif
/* **********************************************************************************************/
/* Scaling code ; 																			   */
/* **********************************************************************************************/
Bool
check_scale_parameters( ASImage *src, int src_width, int src_height, int *to_width, int *to_height )
{
	if( src == NULL )
		return False;

	if( *to_width == 0 )
		*to_width = src_width ;
	else if( *to_width < 2 )
		*to_width = 2 ;
	if( *to_height == 0 )
		*to_height = src_height ;
	else if( *to_height < 2 )
		*to_height = 2 ;
	return True;
}

int *
make_scales( int from_size, int to_size, int tail )
{
	int *scales ;
    int smaller = MIN(from_size,to_size);
    int bigger  = MAX(from_size,to_size);
	register int i = 0, k = 0;
	int eps;
    LOCAL_DEBUG_OUT( "from %d to %d tail %d", from_size, to_size, tail );
	scales = safecalloc( smaller+tail, sizeof(int));
	if( smaller <= 1 ) 
	{
		scales[0] = bigger ; 
		return scales;
	}
#if 1
	else if( smaller == bigger )
	{
		for ( i = 0 ; i < smaller ; i++ )
			scales[i] = 1 ; 
		return scales;	
	}
#endif
	if( from_size >= to_size )
		tail = 0 ;
	if( tail != 0 )
    {
        bigger-=tail ;
        if( (smaller-=tail) == 1 ) 
		{
			scales[0] = bigger ; 
			return scales;
		}	
    }else if( smaller == 2 ) 
	{
		scales[1] = bigger/2 ; 
		scales[0] = bigger - scales[1] ; 
		return scales ;
	}

    eps = -bigger/2;
    LOCAL_DEBUG_OUT( "smaller %d, bigger %d, eps %d", smaller, bigger, eps );
    /* now using Bresengham algoritm to fiill the scales :
	 * since scaling is merely transformation
	 * from 0:bigger space (x) to 0:smaller space(y)*/
	for ( i = 0 ; i < bigger ; i++ )
	{
		++scales[k];
		eps += smaller;
        LOCAL_DEBUG_OUT( "scales[%d] = %d, i = %d, k = %d, eps %d", k, scales[k], i, k, eps );
        if( eps+eps >= bigger )
		{
			++k ;
			eps -= bigger ;
		}
	}

	return scales;
}

/* *******************************************************************/
void
scale_image_down( ASImageDecoder *imdec, ASImageOutput *imout, int h_ratio, int *scales_h, int* scales_v)
{
	ASScanline dst_line, total ;
	int k = -1;
	int max_k 	 = imout->im->height,
		line_len = MIN(imout->im->width, imdec->out_width);

	prepare_scanline( imout->im->width, QUANT_ERR_BITS, &dst_line, imout->asv->BGR_mode );
	prepare_scanline( imout->im->width, QUANT_ERR_BITS, &total, imout->asv->BGR_mode );
	while( ++k < max_k )
	{
		int reps = scales_v[k] ;
		imdec->decode_image_scanline( imdec );
		total.flags = imdec->buffer.flags ;
		CHOOSE_SCANLINE_FUNC(h_ratio,imdec->buffer,total,scales_h,line_len);

		while( --reps > 0 )
		{
			imdec->decode_image_scanline( imdec );
			total.flags = imdec->buffer.flags ;
			CHOOSE_SCANLINE_FUNC(h_ratio,imdec->buffer,dst_line,scales_h,line_len);
			SCANLINE_FUNC(add_component,total,dst_line,NULL,total.width);
		}

		imout->output_image_scanline( imout, &total, scales_v[k] );
	}
	free_scanline(&dst_line, True);
	free_scanline(&total, True);
}

void
scale_image_up( ASImageDecoder *imdec, ASImageOutput *imout, int h_ratio, int *scales_h, int* scales_v)
{
	ASScanline src_lines[4], *c1, *c2, *c3, *c4 = NULL;
	int i = 0, max_i,
		line_len = MIN(imout->im->width, imdec->out_width),
		out_width = imout->im->width;
	ASScanline step ;

	prepare_scanline( out_width, 0, &(src_lines[0]), imout->asv->BGR_mode);
	prepare_scanline( out_width, 0, &(src_lines[1]), imout->asv->BGR_mode);
	prepare_scanline( out_width, 0, &(src_lines[2]), imout->asv->BGR_mode);
	prepare_scanline( out_width, 0, &(src_lines[3]), imout->asv->BGR_mode);
	prepare_scanline( out_width, QUANT_ERR_BITS, &step, imout->asv->BGR_mode );

/*	set_component(src_lines[0].red,0x00000000,0,out_width*3); */
	imdec->decode_image_scanline( imdec );
	src_lines[1].flags = imdec->buffer.flags ;
	CHOOSE_SCANLINE_FUNC(h_ratio,imdec->buffer,src_lines[1],scales_h,line_len);

	step.flags = src_lines[0].flags = src_lines[1].flags ;

	SCANLINE_FUNC(copy_component,src_lines[1],src_lines[0],0,out_width);

	imdec->decode_image_scanline( imdec );
	src_lines[2].flags = imdec->buffer.flags ;
	CHOOSE_SCANLINE_FUNC(h_ratio,imdec->buffer,src_lines[2],scales_h,line_len);

	i = 0 ;
	max_i = imdec->out_height-1 ;
	LOCAL_DEBUG_OUT( "i = %d, max_i = %d", i, max_i );
	do
	{
		int S = scales_v[i] ;
		c1 = &(src_lines[i&0x03]);
		c2 = &(src_lines[(i+1)&0x03]);
		c3 = &(src_lines[(i+2)&0x03]);
		c4 = &(src_lines[(i+3)&0x03]);

		if( i+1 < max_i )
		{
			imdec->decode_image_scanline( imdec );
			c4->flags = imdec->buffer.flags ;
			CHOOSE_SCANLINE_FUNC(h_ratio,imdec->buffer,*c4,scales_h,line_len);
		}
		/* now we'll prepare total and step : */
        if( S > 0 )
        {
            imout->output_image_scanline( imout, c2, 1);
            if( S > 1 )
            {
                if( S == 2 )
                {
                    SCANLINE_COMBINE(component_interpolation_hardcoded,*c1,*c2,*c3,*c4,*c1,*c1,1,out_width);
                    imout->output_image_scanline( imout, c1, 1);
                }else if( S == 3 )
                {
                    SCANLINE_COMBINE(component_interpolation_hardcoded,*c1,*c2,*c3,*c4,*c1,*c1,2,out_width);
                    imout->output_image_scanline( imout, c1, 1);
                    SCANLINE_COMBINE(component_interpolation_hardcoded,*c1,*c2,*c3,*c4,*c1,*c1,3,out_width);
                    imout->output_image_scanline( imout, c1, 1);
                }else
                {
                    SCANLINE_COMBINE(start_component_interpolation,*c1,*c2,*c3,*c4,*c1,step,S,out_width);
                    do
                    {
                        imout->output_image_scanline( imout, c1, 1);
                        if((--S)<=1)
                            break;
                        SCANLINE_FUNC(add_component,*c1,step,NULL,out_width );
                    }while(1);
                }
            }
        }
	}while( ++i < max_i );
    imout->output_image_scanline( imout, c3, 1);
	free_scanline(&step, True);
	free_scanline(&(src_lines[3]), True);
	free_scanline(&(src_lines[2]), True);
	free_scanline(&(src_lines[1]), True);
	free_scanline(&(src_lines[0]), True);
}

void
scale_image_up_dumb( ASImageDecoder *imdec, ASImageOutput *imout, int h_ratio, int *scales_h, int* scales_v)
{
	ASScanline src_line;
	int	line_len = MIN(imout->im->width, imdec->out_width);
	int	out_width = imout->im->width;
	int y = 0 ;

	prepare_scanline( out_width, QUANT_ERR_BITS, &src_line, imout->asv->BGR_mode );

	imout->tiling_step = 1 ;
	LOCAL_DEBUG_OUT( "imdec->next_line = %d, imdec->out_height = %d", imdec->next_line, imdec->out_height );
	while( y < (int)imdec->out_height )
	{
		imdec->decode_image_scanline( imdec );
		src_line.flags = imdec->buffer.flags ;
		CHOOSE_SCANLINE_FUNC(h_ratio,imdec->buffer,src_line,scales_h,line_len);
		imout->tiling_range = scales_v[y];
		LOCAL_DEBUG_OUT( "y = %d, tiling_range = %d", y, imout->tiling_range );
		imout->output_image_scanline( imout, &src_line, 1);
		imout->next_line += scales_v[y]-1;
		++y;
	}
	free_scanline(&src_line, True);
}


/* *****************************************************************************/
/* ASImage transformations : 												  */
/* *****************************************************************************/
ASImage *
scale_asimage( ASVisual *asv, ASImage *src, int to_width, int to_height,
			   ASAltImFormats out_format, unsigned int compression_out, int quality )
{
	ASImage *dst = NULL ;
	ASImageOutput  *imout ;
	ASImageDecoder *imdec;
	int h_ratio ;
	int *scales_h = NULL, *scales_v = NULL;
	START_TIME(started);
	
	if( asv == NULL ) 	asv = &__transform_fake_asv ;
	
	if( !check_scale_parameters(src,src->width, src->height,&to_width,&to_height) )
		return NULL;
	if( (imdec = start_image_decoding(asv, src, SCL_DO_ALL, 0, 0, 0, 0, NULL)) == NULL )
		return NULL;

	dst = create_destination_image( to_width, to_height, out_format, compression_out, src->back_color );

	if( to_width == src->width )
		h_ratio = 0;
	else if( to_width < src->width )
		h_ratio = 1;
	else
	{
		if ( quality == ASIMAGE_QUALITY_POOR )
			h_ratio = 1 ;
		else if( src->width > 1 )
		{
			h_ratio = (to_width/(src->width-1))+1;
			if( h_ratio*(src->width-1) < to_width )
				++h_ratio ;
		}else
			h_ratio = to_width ;
		++h_ratio ;
	}
	scales_h = make_scales( src->width, to_width, ( quality == ASIMAGE_QUALITY_POOR )?0:1 );
	scales_v = make_scales( src->height, to_height, ( quality == ASIMAGE_QUALITY_POOR  || src->height <= 3)?0:1 );
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	{
	  register int i ;
	  for( i = 0 ; i < MIN(src->width, to_width) ; i++ )
		fprintf( stderr, " %d", scales_h[i] );
	  fprintf( stderr, "\n" );
	  for( i = 0 ; i < MIN(src->height, to_height) ; i++ )
		fprintf( stderr, " %d", scales_v[i] );
	  fprintf( stderr, "\n" );
	}
#endif
	if((imout = start_image_output( asv, dst, out_format, QUANT_ERR_BITS, quality )) == NULL )
	{
        destroy_asimage( &dst );
	}else
	{
		if( to_height <= src->height ) 					   /* scaling down */
			scale_image_down( imdec, imout, h_ratio, scales_h, scales_v );
		else if( quality == ASIMAGE_QUALITY_POOR || src->height <= 3 ) 
			scale_image_up_dumb( imdec, imout, h_ratio, scales_h, scales_v );
		else
			scale_image_up( imdec, imout, h_ratio, scales_h, scales_v );
		stop_image_output( &imout );
	}
	free( scales_h );
	free( scales_v );
	stop_image_decoding( &imdec );
	SHOW_TIME("", started);
	return dst;
}

ASImage *
scale_asimage2( ASVisual *asv, ASImage *src, 
					int clip_x, int clip_y, 
					int clip_width, int clip_height, 
					int to_width, int to_height,
			   		ASAltImFormats out_format, unsigned int compression_out, int quality )
{
	ASImage *dst = NULL ;
	ASImageOutput  *imout ;
	ASImageDecoder *imdec;
	int h_ratio ;
	int *scales_h = NULL, *scales_v = NULL;
	START_TIME(started);

	if( src == NULL ) 
		return NULL;

	if( asv == NULL ) 	asv = &__transform_fake_asv ;

	if( clip_width == 0 )
		clip_width = src->width ;
	if( clip_height == 0 )
		clip_height = src->height ;
	if( !check_scale_parameters(src, clip_width, clip_height, &to_width, &to_height) )
		return NULL;
	if( (imdec = start_image_decoding(asv, src, SCL_DO_ALL, clip_x, clip_y, clip_width, clip_height, NULL)) == NULL )
		return NULL;

	dst = create_destination_image( to_width, to_height, out_format, compression_out, src->back_color );

	if( to_width == clip_width )
		h_ratio = 0;
	else if( to_width < clip_width )
		h_ratio = 1;
	else
	{
		if ( quality == ASIMAGE_QUALITY_POOR )
			h_ratio = 1 ;
		else if( clip_width > 1 )
		{
			h_ratio = (to_width/(clip_width-1))+1;
			if( h_ratio*(clip_width-1) < to_width )
				++h_ratio ;
		}else
			h_ratio = to_width ;
		++h_ratio ;
	}
	scales_h = make_scales( clip_width, to_width, ( quality == ASIMAGE_QUALITY_POOR )?0:1 );
	scales_v = make_scales( clip_height, to_height, ( quality == ASIMAGE_QUALITY_POOR  || clip_height <= 3)?0:1 );
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	{
	  register int i ;
	  for( i = 0 ; i < MIN(clip_width, to_width) ; i++ )
		fprintf( stderr, " %d", scales_h[i] );
	  fprintf( stderr, "\n" );
	  for( i = 0 ; i < MIN(clip_height, to_height) ; i++ )
		fprintf( stderr, " %d", scales_v[i] );
	  fprintf( stderr, "\n" );
	}
#endif
	if((imout = start_image_output( asv, dst, out_format, QUANT_ERR_BITS, quality )) == NULL )
	{
        destroy_asimage( &dst );
	}else
	{
		if( to_height <= clip_height ) 					   /* scaling down */
			scale_image_down( imdec, imout, h_ratio, scales_h, scales_v );
		else if( quality == ASIMAGE_QUALITY_POOR || clip_height <= 3 ) 
			scale_image_up_dumb( imdec, imout, h_ratio, scales_h, scales_v );
		else
			scale_image_up( imdec, imout, h_ratio, scales_h, scales_v );
		stop_image_output( &imout );
	}
	free( scales_h );
	free( scales_v );
	stop_image_decoding( &imdec );
	SHOW_TIME("", started);
	return dst;
}
