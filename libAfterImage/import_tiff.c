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

#ifndef NO_DEBUG_OUTPUT
#define DEBUG_TIFF
#endif

#undef LOCAL_DEBUG
#undef DO_CLOCKING
#undef DEBUG_TRANSP_GIF

#ifdef _WIN32
#include "win32/config.h"
#else
#include "config.h"
#endif

#ifdef HAVE_TIFF
#include <tiff.h>
#include <tiffio.h>
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

#ifdef _WIN32
# include "win32/afterbase.h"
#else
# include "afterbase.h"
#endif

#include "asimage.h"
#include "imencdec.h"
#include "scanline.h"
#include "import.h"

#ifdef HAVE_TIFF/* TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF */


ASImage *
tiff2ASImage( const char * path, ASImageImportParams *params )
{
	TIFF 		 *tif ;

	static ASImage 	 *im = NULL ;
	CARD32 *data;
	int data_size;
	CARD32 width = 1, height = 1;
	CARD16 depth = 4 ;
	CARD16 bits = 0 ;
	CARD32 rows_per_strip =0 ;
	CARD32 tile_width = 0, tile_length = 0 ;
	CARD32 planar_config = 0 ;
	CARD16 photo = 0;
	START_TIME(started);

	if ((tif = TIFFOpen(path,"r")) == NULL)
	{
		show_error("cannot open image file \"%s\" for reading. Please check permissions.", path);
		return NULL;
	}

#ifdef DEBUG_TIFF
	{;}
#endif
	if( params->subimage > 0 )
		if( !TIFFSetDirectory(tif, params->subimage))
		{
			TIFFClose(tif);
			show_error("Image file \"%s\" does not contain subimage %d.", path, params->subimage);
			return NULL ;
		}

	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
	if( !TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &depth) )
		depth = 3 ;
	if( !TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bits) )
		bits = 8 ;
	if( !TIFFGetField(tif, TIFFTAG_ROWSPERSTRIP, &rows_per_strip ) )
		rows_per_strip = height ;
	if( !TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photo) )
		photo = 0 ;

#ifndef PHOTOMETRIC_CFA
#define PHOTOMETRIC_CFA 32803
#endif

	TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &planar_config);

	if( TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width) ||
		TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_length) )
	{
		show_error( "Tiled TIFF image format is not supported yet." );
		TIFFClose(tif);
		return NULL;
	}


	if( rows_per_strip == 0 || rows_per_strip > height )
		rows_per_strip = height ;
	if( depth <= 0 )
		depth = 4 ;
	if( depth <= 2 && get_flags( photo, PHOTOMETRIC_RGB) )
		depth += 2 ;
	LOCAL_DEBUG_OUT ("size = %ldx%ld, depth = %d, bits = %d, rps = %ld, photo = %d, tile_size = %dx%d, config = %d",
					 width, height, depth, bits, rows_per_strip, photo, tile_width, tile_length, planar_config);
	if( width < MAX_IMPORT_IMAGE_SIZE && height < MAX_IMPORT_IMAGE_SIZE )
	{
		data_size = width*rows_per_strip*sizeof(CARD32);
		data = (CARD32*) _TIFFmalloc(data_size);
		if (data != NULL)
		{
			CARD8 		 *r = NULL, *g = NULL, *b = NULL, *a = NULL ;
			ASFlagType store_flags = ASStorage_RLEDiffCompress	;
			int first_row = 0 ;
			int old_storage_block_size;
			if( bits == 1 )
				set_flags( store_flags, ASStorage_Bitmap );

			im = create_asimage( width, height, params->compression );
			old_storage_block_size = set_asstorage_block_size( NULL, im->width*im->height*3/2 );

			if( depth == 2 || depth == 4 )
				a = safemalloc( width );
			r = safemalloc( width );
			if( depth > 2 )
			{
				g = safemalloc( width );
				b = safemalloc( width );
			}
			if (photo == PHOTOMETRIC_CFA)
			{/* need alternative - more complicated method */
				Bool success = False;

				ASIMStrip *strip = create_asim_strip(10, im->width, 8, True);
				ASImageOutput *imout = start_image_output( NULL, im, ASA_ASImage, 8, ASIMAGE_QUALITY_DEFAULT);

				LOCAL_DEBUG_OUT( "custom CFA TIFF reading...");

				if (strip && imout)
				{
					int cfa_type = 0;
					ASIMStripLoader line_loaders[2][2] =
						{	{decode_RG_12_be, decode_GB_12_be},
	 						{decode_BG_12_be, decode_GR_12_be}
						};
					int line_loaders_num[2] = {2, 2};

					int bytes_per_row = (bits * width + 7)/8;
					int loaded_data_size = 0;

					if ( 1/* striped image */)
					{
						int strip_no;
						uint32* bc;
						TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &bc);
						int all_strip_size = 0;
						for (strip_no = 0; strip_no < TIFFNumberOfStrips(tif); ++strip_no)
							all_strip_size += bc[strip_no];
						/* create one large buffer for the image data : */
						if (data_size < all_strip_size)
						{
							data_size = all_strip_size;
							_TIFFfree(data);
							data = _TIFFmalloc(data_size);
						}

						if (planar_config == PLANARCONFIG_CONTIG)
						{
							for (strip_no = 0; strip_no < TIFFNumberOfStrips(tif); strip_no++)
							{
								int bytes_in;
								if (bits == 12) /* can't use libTIFF's function - it can't handle 12bit data ! */
								{
									/* PENTAX cameras claim that data is compressed as runlength packbits -
									   it is not in fact run-length, which confuses libTIFF
									 */
									bytes_in = TIFFReadRawStrip(tif, strip_no, data+loaded_data_size, data_size-loaded_data_size);
								}else
									bytes_in = TIFFReadEncodedStrip(tif, strip_no, data+loaded_data_size, data_size-loaded_data_size);

LOCAL_DEBUG_OUT( "strip size = %d, bytes_in = %d, bytes_per_row = %d", bc[strip_no], bytes_in, bytes_per_row);
								if (bytes_in >= 0)
									loaded_data_size += bytes_in;
								else
								{
									LOCAL_DEBUG_OUT( "failed reading strip %d", strip_no);
								}
							}
						} else if (planar_config == PLANARCONFIG_SEPARATE)
						{
							/* TODO: do something with split channels */
						}
					}else
					{
						/* TODO: implement support for tiled images */
					}

					if (loaded_data_size > 0)
					{
						int offset;
						int data_row = 0;
						do
						{
							offset = data_row * bytes_per_row;
							int loaded_rows = load_asim_strip (strip, (CARD8*)data + offset, loaded_data_size-offset,
																data_row, bytes_per_row,
																line_loaders[cfa_type], line_loaders_num[cfa_type]);

							if (loaded_rows == 0)
							{ /* need to write out some rows to free up space */
								interpolate_asim_strip_custom_rggb2 (strip, SCL_DO_RED|SCL_DO_GREEN|SCL_DO_BLUE, False);
								imout->output_image_scanline( imout, strip->lines[0], 1);

								advance_asim_strip (strip);

							}
							data_row += loaded_rows;
						}while (offset < loaded_data_size);
						success = True;
					}
				}
				destroy_asim_strip (&strip);
				stop_image_output( &imout );

				if (!success)
					destroy_asimage (&im);
			}else
			{
				TIFFReadRGBAStrip(tif, first_row, (void*)data);
				do
				{
					register CARD32 *row = data ;
					int y = first_row + rows_per_strip ;
					if( y > height )
						y = height ;
					while( --y >= first_row )
					{
						int x ;
						for( x = 0 ; x < width ; ++x )
						{
							CARD32 c = row[x] ;
							if( depth == 4 || depth == 2 )
								a[x] = TIFFGetA(c);
							r[x]   = TIFFGetR(c);
							if( depth > 2 )
							{
								g[x] = TIFFGetG(c);
								b[x]  = TIFFGetB(c);
							}
						}
						im->channels[IC_RED][y]  = store_data( NULL, r, width, store_flags, 0);
						if( depth > 2 )
						{
					 		im->channels[IC_GREEN][y] = store_data( NULL, g, width, store_flags, 0);
							im->channels[IC_BLUE][y]  = store_data( NULL, b, width, store_flags, 0);
						}else
						{
					 		im->channels[IC_GREEN][y] = dup_data( NULL, im->channels[IC_RED][y]);
							im->channels[IC_BLUE][y]  = dup_data( NULL, im->channels[IC_RED][y]);
						}

						if( depth == 4 || depth == 2 )
							im->channels[IC_ALPHA][y]  = store_data( NULL, a, width, store_flags, 0);
						row += width ;
					}
					/* move onto the next strip now : */
					do
					{
						first_row += rows_per_strip ;
					}while (first_row < height && !TIFFReadRGBAStrip(tif, first_row, (void*)data));

				}while (first_row < height);
		    }
			set_asstorage_block_size( NULL, old_storage_block_size );

			if( b ) free( b );
			if( g ) free( g );
			if( r ) free( r );
			if( a ) free( a );
			_TIFFfree(data);
		}
	}
	/* close the file */
	TIFFClose(tif);
	SHOW_TIME("image loading",started);

	return im ;
}
#else 			/* TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF */

ASImage *
tiff2ASImage( const char * path, ASImageImportParams *params )
{
	show_error( "unable to load file \"%s\" - missing TIFF image format libraries.\n", path );
	return NULL ;
}
#endif			/* TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF */

