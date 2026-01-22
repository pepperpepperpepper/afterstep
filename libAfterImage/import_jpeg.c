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
#include <stddef.h>
#include <stdio.h>

#ifdef HAVE_JPEG
/* Include file for users of png library. */
# undef HAVE_STDLIB_H
# ifndef X_DISPLAY_MISSING
#  include <X11/Xmd.h>
# endif
# ifdef HAVE_BUILTIN_JPEG
#  include "libjpeg/jpeglib.h"
# else
#  include <jpeglib.h>
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
#include "import.h"
#include "import_internal.h"

static inline void
apply_gamma( register CARD8* raw, register CARD8 *gamma_table, unsigned int width )
{
	if( gamma_table )
	{
		register unsigned int i ;
		for( i = 0 ; i < width ; ++i )
			raw[i] = gamma_table[raw[i]] ;
	}
}

#ifdef HAVE_JPEG     /* JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG */
struct my_error_mgr
{
	struct jpeg_error_mgr pub;				   /* "public" fields */
	jmp_buf       setjmp_buffer;			   /* for return to caller */
};
typedef struct my_error_mgr *my_error_ptr;

METHODDEF (void)
my_error_exit (j_common_ptr cinfo)
{
	/* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
	my_error_ptr  myerr = (my_error_ptr) cinfo->err;
	/* Always display the message. */
	/* We could postpone this until after returning, if we chose. */
	(*cinfo->err->output_message) (cinfo);
	/* Return control to the setjmp point */
	longjmp (myerr->setjmp_buffer, 1);
}

ASImage *
jpeg2ASImage( const char * path, ASImageImportParams *params )
{
	ASImage *im ;
	int old_storage_block_size ;
	/* This struct contains the JPEG decompression parameters and pointers to
	 * working space (which is allocated as needed by the JPEG library).
	 */
	struct jpeg_decompress_struct cinfo;
	/* We use our private extension JPEG error handler.
	 * Note that this struct must live as long as the main JPEG parameter
	 * struct, to avoid dangling-pointer problems.
	 */
	struct my_error_mgr jerr;
	/* More stuff */
	FILE         *infile;					   /* source file */
	JSAMPARRAY    buffer;					   /* Output row buffer */
	ASScanline    buf;
	int y;
	START_TIME(started);
 /*	register int i ;*/

	/* we want to open the input file before doing anything else,
	 * so that the setjmp() error recovery below can assume the file is open.
	 * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
	 * requires it in order to read binary files.
	 */

	if ((infile = open_image_file(path)) == NULL)
		return NULL;

	/* Step 1: allocate and initialize JPEG decompression object */
	/* We set up the normal JPEG error routines, then override error_exit. */
	cinfo.err = jpeg_std_error (&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	/* Establish the setjmp return context for my_error_exit to use. */
	if (setjmp (jerr.setjmp_buffer))
	{
		/* If we get here, the JPEG code has signaled an error.
		   * We need to clean up the JPEG object, close the input file, and return.
		 */
		jpeg_destroy_decompress (&cinfo);
		fclose (infile);
		return NULL;
	}
	/* Now we can initialize the JPEG decompression object. */
	jpeg_create_decompress (&cinfo);
	/* Step 2: specify data source (eg, a file) */
	jpeg_stdio_src (&cinfo, infile);
	/* Step 3: read file parameters with jpeg_read_header() */
	(void)jpeg_read_header (&cinfo, TRUE);
	/* We can ignore the return value from jpeg_read_header since
	 *   (a) suspension is not possible with the stdio data source, and
	 *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
	 * See libjpeg.doc for more info.
	 */

	/* Step 4: set parameters for decompression */
	/* Adjust default decompression parameters */
	cinfo.quantize_colors = FALSE;		       /* we don't want no stinking colormaps ! */
	cinfo.output_gamma = params->gamma;

	if( get_flags( params->flags, AS_IMPORT_SCALED_BOTH ) == AS_IMPORT_SCALED_BOTH )
	{
		int w = params->width ;
		int h = params->height ;
		int ratio ;

		if( w == 0 )
		{
			if( h == 0 )
			{
				w = cinfo.image_width ;
				h = cinfo.image_height ;
			}else
				w = (cinfo.image_width * h)/cinfo.image_height ;
		}else if( h == 0 )
			h = (cinfo.image_height * w)/cinfo.image_width ;

		ratio = cinfo.image_height/h ;
		if( ratio > (int)cinfo.image_width/w )
			ratio = cinfo.image_width/w ;

		cinfo.scale_num = 1 ;
		/* only supported values are 1, 2, 4, and 8 */
		cinfo.scale_denom = 1 ;
		if( ratio >= 2 )
		{
			if( ratio >= 4 )
			{
				if( ratio >= 8 )
					cinfo.scale_denom = 8 ;
				else
					cinfo.scale_denom = 4 ;
			}else
				cinfo.scale_denom = 2 ;
		}
	}

	if( get_flags( params->flags, AS_IMPORT_FAST ) )
	{/* this does not really makes much of a difference */
		cinfo.do_fancy_upsampling = FALSE ;
		cinfo.do_block_smoothing = FALSE ;
		cinfo.dct_method = JDCT_IFAST ;
	}

	/* Step 5: Start decompressor */
	(void)jpeg_start_decompress (&cinfo);
	LOCAL_DEBUG_OUT("stored image size %dx%d", cinfo.output_width,  cinfo.output_height);

	im = create_asimage( cinfo.output_width,  cinfo.output_height, params->compression );

	if( cinfo.output_components != 1 )
		prepare_scanline( im->width, 0, &buf, False );

	/* Make a one-row-high sample array that will go away when done with image */
	buffer = cinfo.mem->alloc_sarray((j_common_ptr) &cinfo, JPOOL_IMAGE,
									cinfo.output_width * cinfo.output_components, 1);

	/* Step 6: while (scan lines remain to be read) */
	SHOW_TIME("loading initialization",started);
	y = -1 ;
	/*cinfo.output_scanline*/
/*	for( i = 0 ; i < im->width ; i++ )	fprintf( stderr, "%3.3d    ", i );
	fprintf( stderr, "\n");
 */
	old_storage_block_size = set_asstorage_block_size( NULL, im->width*im->height*3/2 );

 	while ( ++y < (int)cinfo.output_height )
	{
		/* jpeg_read_scanlines expects an array of pointers to scanlines.
		 * Here the array is only one element long, but you could ask for
		 * more than one scanline at a time if that's more convenient.
		 */
		(void)jpeg_read_scanlines (&cinfo, buffer, 1);
		if( cinfo.output_components==1 )
		{
			apply_gamma( (CARD8*)buffer[0], params->gamma_table, im->width );
			im->channels[IC_RED][y] = store_data( NULL, (CARD8*)buffer[0], im->width, ASStorage_RLEDiffCompress, 0);
			im->channels[IC_GREEN][y] = dup_data( NULL, im->channels[IC_RED][y] );
			im->channels[IC_BLUE][y]  = dup_data( NULL, im->channels[IC_RED][y] );
		}else
		{
			raw2scanline( (CARD8*)buffer[0], &buf, params->gamma_table, im->width, (cinfo.output_components==1), False);
			im->channels[IC_RED][y] = store_data( NULL, (CARD8*)buf.red, buf.width*4, ASStorage_32BitRLE, 0);
			im->channels[IC_GREEN][y] = store_data( NULL, (CARD8*)buf.green, buf.width*4, ASStorage_32BitRLE, 0);
			im->channels[IC_BLUE][y] = store_data( NULL, (CARD8*)buf.blue, buf.width*4, ASStorage_32BitRLE, 0);
		}
/*		fprintf( stderr, "src:");
		for( i = 0 ; i < im->width ; i++ )
			fprintf( stderr, "%2.2X%2.2X%2.2X ", buffer[0][i*3], buffer[0][i*3+1], buffer[0][i*3+2] );
		fprintf( stderr, "\ndst:");
		for( i = 0 ; i < im->width ; i++ )
			fprintf( stderr, "%2.2X%2.2X%2.2X ", buf.red[i], buf.green[i], buf.blue[i] );
		fprintf( stderr, "\n");
 */
	}
	set_asstorage_block_size( NULL, old_storage_block_size );
	if( cinfo.output_components != 1 )
		free_scanline(&buf, True);
	SHOW_TIME("read",started);

	/* Step 7: Finish decompression */
	/* we must abort the decompress if not all lines were read */
	if (cinfo.output_scanline < cinfo.output_height)
		jpeg_abort_decompress (&cinfo);
	else
		(void)jpeg_finish_decompress (&cinfo);
	/* We can ignore the return value since suspension is not possible
	 * with the stdio data source.
	 */
	/* Step 8: Release JPEG decompression object */
	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_decompress (&cinfo);
	/* After finish_decompress, we can close the input file.
	 * Here we postpone it until after no more JPEG errors are possible,
	 * so as to simplify the setjmp error logic above.  (Actually, I don't
	 * think that jpeg_destroy can do an error exit, but why assume anything...)
	 */
	fclose (infile);
	/* At this point you may want to check to see whether any corrupt-data
	 * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
	 */
	SHOW_TIME("image loading",started);
	LOCAL_DEBUG_OUT("done loading JPEG image \"%s\"", path);
	return im ;
}
#else 			/* JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG */
ASImage *
jpeg2ASImage( const char * path, ASImageImportParams *params )
{
	show_error( "unable to load file \"%s\" - JPEG image format is not supported.\n", path );
	return NULL ;
}

#endif 			/* JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG */
