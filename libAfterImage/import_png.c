/* This file contains code for unified image loading from many file formats */
/********************************************************************/
/* Copyright (c) 2001,2004 Sasha Vasko <sasha at aftercode.net>     */
/* Copyright (c) 2004 Maxim Nikulin <nikulin at gorodok.net>        */
/********************************************************************/
/*
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

#undef LOCAL_DEBUG
#undef DO_CLOCKING
#undef DEBUG_TRANSP_GIF

#ifdef _WIN32
#include "win32/config.h"
#else
#include "config.h"
#endif

#include <setjmp.h>

#ifdef HAVE_PNG
/* Include file for users of png library. */
# ifdef HAVE_BUILTIN_PNG
#  include "libpng/png.h"
# else
#  include <png.h>
# endif
#endif

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
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#include <string.h>
#include <ctype.h>

#ifdef const
#undef const
#endif

#ifdef _WIN32
# include "win32/afterbase.h"
#else
# include "afterbase.h"
#endif

#include "asimage.h"
#include "imencdec.h"
#include "scanline.h"
#include "ximage.h"
#include "import.h"
#include "import_internal.h"

#ifdef HAVE_PNG		/* PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG */
ASImage *
png2ASImage_int( void *data, png_rw_ptr read_fn, ASImageImportParams *params )
{

   double        image_gamma = DEFAULT_PNG_IMAGE_GAMMA;
	png_structp   png_ptr;
	png_infop     info_ptr;
	png_uint_32   width, height;
	int           bit_depth, color_type, interlace_type;
	int           intent;
	ASScanline    buf;
	CARD8         *upscaled_gray = NULL;
	Bool 	      do_alpha = False, grayscale = False ;
	png_bytep     *row_pointers, row;
	unsigned int  y;
	size_t		  row_bytes, offset ;
	static ASImage 	 *im = NULL ;
	int old_storage_block_size;
	START_TIME(started);

	/* Create and initialize the png_struct with the desired error handler
	 * functions.  If you want to use the default stderr and longjump method,
	 * you can supply NULL for the last three parameters.  We also supply the
	 * the compiler header file version, so that we know if the application
	 * was compiled with a compatible version of the library.  REQUIRED
	 */
	if((png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)) != NULL )
	{
		/* Allocate/initialize the memory for image information.  REQUIRED. */
		if( (info_ptr = png_create_info_struct (png_ptr)) != NULL )
		{
		  	/* Set error handling if you are using the setjmp/longjmp method (this is
			 * the normal method of doing things with libpng).  REQUIRED unless you
			 * set up your own error handlers in the png_create_read_struct() earlier.
			 */
			if ( !setjmp (png_jmpbuf(png_ptr)) )
			{
				ASFlagType rgb_flags = ASStorage_RLEDiffCompress|ASStorage_32Bit ;

	         if(read_fn == NULL )
	         {
		         png_init_io(png_ptr, (FILE*)data);
	         }else
	         {
	            png_set_read_fn(png_ptr, (void*)data, (png_rw_ptr) read_fn);
	         }

		    	png_read_info (png_ptr, info_ptr);
				png_get_IHDR (png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

/*fprintf( stderr, "bit_depth = %d, color_type = %d, width = %d, height = %d\n",
         bit_depth, color_type, width, height);
*/
				if (bit_depth < 8)
				{/* Extract multiple pixels with bit depths of 1, 2, and 4 from a single
				  * byte into separate bytes (useful for paletted and grayscale images).
				  */
					if( bit_depth == 1 )
					{
						set_flags( rgb_flags, ASStorage_Bitmap );
						png_set_packing (png_ptr);
					}else
					{
						/* even though 2 and 4 bit values get expanded into a whole bytes the
						   values don't get scaled accordingly !!!
						   WE will have to take care of it ourselves :
						*/
						upscaled_gray = safemalloc(width+8);
					}
				}else if (bit_depth == 16)
				{/* tell libpng to strip 16 bit/color files down to 8 bits/color */
					png_set_strip_16 (png_ptr);
				}

				/* Expand paletted colors into true RGB triplets */
				if (color_type == PNG_COLOR_TYPE_PALETTE)
				{
					png_set_expand (png_ptr);
					color_type = PNG_COLOR_TYPE_RGB;
				}

				/* Expand paletted or RGB images with transparency to full alpha channels
				 * so the data will be available as RGBA quartets.
		 		 */
   				if( color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY )
   				{
				   	if( png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
					{
						png_set_expand(png_ptr);
						color_type |= PNG_COLOR_MASK_ALPHA;
					}
   				}else
				{
					png_set_filler( png_ptr, 0xFF, PNG_FILLER_AFTER );
					color_type |= PNG_COLOR_MASK_ALPHA;
				}

/*				if( color_type == PNG_COLOR_TYPE_RGB )
					color_type = PNG_COLOR_TYPE_RGB_ALPHA ;
   				else
					color_type = PNG_COLOR_TYPE_GRAY_ALPHA ;
  */
				if (png_get_sRGB (png_ptr, info_ptr, &intent))
				{
                    png_set_gamma (png_ptr, params->gamma, DEFAULT_PNG_IMAGE_GAMMA);
				}else if (png_get_gAMA (png_ptr, info_ptr, &image_gamma) && bit_depth >= 8)
				{/* don't gamma-correct 1, 2, 4 bpp grays as we loose data this way */
					png_set_gamma (png_ptr, params->gamma, image_gamma);
				}else
				{
                    png_set_gamma (png_ptr, params->gamma, DEFAULT_PNG_IMAGE_GAMMA);
				}

				/* Optional call to gamma correct and add the background to the palette
				 * and update info structure.  REQUIRED if you are expecting libpng to
				 * update the palette for you (ie you selected such a transform above).
				 */

				png_read_update_info (png_ptr, info_ptr);

				png_get_IHDR (png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

				im = create_asimage( width, height, params->compression );
				do_alpha = ((color_type & PNG_COLOR_MASK_ALPHA) != 0 );
				grayscale = ( color_type == PNG_COLOR_TYPE_GRAY_ALPHA ||
				              color_type == PNG_COLOR_TYPE_GRAY) ;

/* fprintf( stderr, "do_alpha = %d, grayscale = %d, bit_depth = %d, color_type = %d, width = %d, height = %d\n",
         do_alpha, grayscale, bit_depth, color_type, width, height); */

				if( !do_alpha && grayscale )
					clear_flags( rgb_flags, ASStorage_32Bit );
				else
					prepare_scanline( im->width, 0, &buf, False );

				row_bytes = png_get_rowbytes (png_ptr, info_ptr);
				/* allocating big chunk of memory at once, to enable mmap
				 * that will release memory to system right after free() */
				row_pointers = safemalloc( height * sizeof( png_bytep ) + row_bytes * height );
				row = (png_bytep)(row_pointers + height) ;
				for (offset = 0, y = 0; y < height; y++, offset += row_bytes)
					row_pointers[y] = row + offset;

				/* The easiest way to read the image: */
				png_read_image (png_ptr, row_pointers);

				old_storage_block_size = set_asstorage_block_size( NULL, width*height*3/2 );
				for (y = 0; y < height; y++)
				{
					if( do_alpha || !grayscale )
					{
						raw2scanline( row_pointers[y], &buf, NULL, buf.width, grayscale, do_alpha );
						im->channels[IC_RED][y] = store_data( NULL, (CARD8*)buf.red, buf.width*4, rgb_flags, 0);
					}else
					{
						if ( bit_depth == 2 )
						{
							int i, pixel_i = -1;
							static CARD8  gray2bit_translation[4] = {0,85,170,255};
							for ( i = 0 ; i < row_bytes ; ++i )
							{
								CARD8 b = row_pointers[y][i];
								upscaled_gray[++pixel_i] = gray2bit_translation[b&0x03];
								upscaled_gray[++pixel_i] = gray2bit_translation[(b&0xC)>>2];
								upscaled_gray[++pixel_i] = gray2bit_translation[(b&0x30)>>4];
								upscaled_gray[++pixel_i] = gray2bit_translation[(b&0xC0)>>6];
							}
							im->channels[IC_RED][y] = store_data( NULL, upscaled_gray, width, rgb_flags, 0);
						}else if ( bit_depth == 4 )
						{
							int i, pixel_i = -1;
							static CARD8  gray4bit_translation[16] = {0,17,34,51,  68,85,102,119, 136,153,170,187, 204,221,238,255};
							for ( i = 0 ; i < row_bytes ; ++i )
							{
								CARD8 b = row_pointers[y][i];
								upscaled_gray[++pixel_i] = gray4bit_translation[b&0x0F];
								upscaled_gray[++pixel_i] = gray4bit_translation[(b&0xF0)>>4];
							}
							im->channels[IC_RED][y] = store_data( NULL, upscaled_gray, width, rgb_flags, 0);
						}else
							im->channels[IC_RED][y] = store_data( NULL, row_pointers[y], row_bytes, rgb_flags, 1);
					}

					if( grayscale )
					{
						im->channels[IC_GREEN][y] = dup_data( NULL, im->channels[IC_RED][y] );
						im->channels[IC_BLUE][y]  = dup_data( NULL, im->channels[IC_RED][y] );
					}else
					{
						im->channels[IC_GREEN][y] = store_data( NULL, (CARD8*)buf.green, buf.width*4, rgb_flags, 0);
						im->channels[IC_BLUE][y] = store_data( NULL, (CARD8*)buf.blue, buf.width*4, rgb_flags, 0);
					}

					if( do_alpha )
					{
						int has_zero = False, has_nozero = False ;
						register unsigned int i;
						for ( i = 0 ; i < buf.width ; ++i)
						{
							if( buf.alpha[i] != 0x00FF )
							{
								if( buf.alpha[i] == 0 )
									has_zero = True ;
								else
								{
									has_nozero = True ;
									break;
								}
							}
						}
						if( has_zero || has_nozero )
						{
							ASFlagType alpha_flags = ASStorage_32Bit|ASStorage_RLEDiffCompress ;
							if( !has_nozero )
								set_flags( alpha_flags, ASStorage_Bitmap );
							im->channels[IC_ALPHA][y] = store_data( NULL, (CARD8*)buf.alpha, buf.width*4, alpha_flags, 0);
						}
					}
				}
				set_asstorage_block_size( NULL, old_storage_block_size );
				if (upscaled_gray)
					free(upscaled_gray);
				free (row_pointers);
				if( do_alpha || !grayscale )
					free_scanline(&buf, True);
				/* read rest of file, and get additional chunks in info_ptr - REQUIRED */
				png_read_end (png_ptr, info_ptr);
		  	}
		}
		/* clean up after the read, and free any memory allocated - REQUIRED */
		png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp) NULL);
		if (info_ptr)
			free (info_ptr);
	}

#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
print_asimage( im, ASFLAGS_EVERYTHING, __FUNCTION__, __LINE__ );
#endif
	SHOW_TIME("image loading",started);
	return im ;
}


/****** VO ******/
typedef struct ASImPNGReadBuffer
{
	CARD8 *buffer ;

} ASImPNGReadBuffer;

static void asim_png_read_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
   ASImPNGReadBuffer *buf = (ASImPNGReadBuffer *)(png_get_io_ptr(png_ptr));
   memcpy(data, buf->buffer, length);
   buf->buffer += length;
}

ASImage *
PNGBuff2ASimage(CARD8 *buffer, ASImageImportParams *params)
{
   static ASImage *im = NULL;
   ASImPNGReadBuffer buf;
   buf.buffer = buffer;
   im = png2ASImage_int((void*)&buf,(png_rw_ptr)asim_png_read_data, params);
   return im;
}


ASImage *
png2ASImage( const char * path, ASImageImportParams *params )
{
   FILE *fp ;
	static ASImage *im = NULL ;

	if ((fp = open_image_file(path)) == NULL)
		return NULL;

   im = png2ASImage_int((void*)fp, NULL, params);

	fclose(fp);
	return im;
}
#else 			/* PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG */
ASImage *
png2ASImage( const char * path, ASImageImportParams *params )
{
	show_error( "unable to load file \"%s\" - PNG image format is not supported.\n", path );
	return NULL ;
}

ASImage *
PNGBuff2ASimage(CARD8 *buffer, ASImageImportParams *params)
{
   return NULL;
}

#endif 			/* PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG */
