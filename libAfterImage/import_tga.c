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
#include "import_internal.h"

/*************************************************************************/
/* Targa Image format - some stuff borrowed from the GIMP.
 *************************************************************************/
typedef struct ASTGAHeader
{
	CARD8 IDLength ;
	CARD8 ColorMapType;
#define TGA_NoImageData			0
#define TGA_ColormappedImage	1
#define TGA_TrueColorImage		2
#define TGA_BWImage				3
#define TGA_RLEColormappedImage		9
#define TGA_RLETrueColorImage		10
#define TGA_RLEBWImage				11
	CARD8 ImageType;
	struct
	{
		CARD16 FirstEntryIndex ;
		CARD16 ColorMapLength ;  /* number of entries */
		CARD8  ColorMapEntrySize ;  /* number of bits per entry */
	}ColormapSpec;
	struct
	{
		CARD16 XOrigin;
		CARD16 YOrigin;
		CARD16 Width;
		CARD16 Height;
		CARD8  Depth;
#define TGA_LeftToRight		(0x01<<4)
#define TGA_TopToBottom		(0x01<<5)
		CARD8  Descriptor;
	}ImageSpec;

}ASTGAHeader;

typedef struct ASTGAColorMap
{
	int bytes_per_entry;
	int bytes_total ;
	CARD8 *data ;
}ASTGAColorMap;

typedef struct ASTGAImageData
{
	int bytes_per_pixel;
	int image_size;
	int bytes_total ;
	CARD8 *data ;
}ASTGAImageData;

static Bool load_tga_colormapped(FILE *infile, ASTGAHeader *tga, ASTGAColorMap *cmap, ASScanline *buf, CARD8 *read_buf, CARD8 *gamma_table )
{

	return True;
}

static Bool load_tga_truecolor(FILE *infile, ASTGAHeader *tga, ASTGAColorMap *cmap, ASScanline *buf, CARD8 *read_buf, CARD8 *gamma_table )
{
	CARD32 *a = buf->alpha ;
	CARD32 *r = buf->red ;
	CARD32 *g = buf->green ;
	CARD32 *b = buf->blue ;
	int bpp = (tga->ImageSpec.Depth+7)/8;
	int bpl = buf->width*bpp;
	if( fread( read_buf, 1, bpl, infile ) != (unsigned int)bpl )
		return False;
	if( bpp == 3 )
	{
		unsigned int i;
		if( gamma_table )
			for( i = 0 ; i < buf->width ; ++i )
			{
				b[i] = gamma_table[*(read_buf++)];
				g[i] = gamma_table[*(read_buf++)];
				r[i] = gamma_table[*(read_buf++)];
			}
		else
			for( i = 0 ; i < buf->width ; ++i )
			{
				b[i] = *(read_buf++);
				g[i] = *(read_buf++);
				r[i] = *(read_buf++);
			}
		set_flags( buf->flags, SCL_DO_RED|SCL_DO_GREEN|SCL_DO_BLUE );
	}else if( bpp == 4 )
	{
		unsigned int i;
		for( i = 0 ; i < buf->width ; ++i )
		{
			b[i] = *(read_buf++);
			g[i] = *(read_buf++);
			r[i] = *(read_buf++);
			a[i] = *(read_buf++);
		}
		set_flags( buf->flags, SCL_DO_RED|SCL_DO_GREEN|SCL_DO_BLUE|SCL_DO_ALPHA );
	}

	return True;
}

static Bool load_tga_bw(FILE *infile, ASTGAHeader *tga, ASTGAColorMap *cmap, ASScanline *buf, CARD8 *read_buf, CARD8 *gamma_table )
{

	return True;
}

static Bool load_tga_rle_colormapped(FILE *infile, ASTGAHeader *tga, ASTGAColorMap *cmap, ASScanline *buf, CARD8 *read_buf, CARD8 *gamma_table )
{

	return True;
}

static Bool load_tga_rle_truecolor(FILE *infile, ASTGAHeader *tga, ASTGAColorMap *cmap, ASScanline *buf, CARD8 *read_buf, CARD8 *gamma_table )
{

	return True;
}

static Bool load_tga_rle_bw(FILE *infile, ASTGAHeader *tga, ASTGAColorMap *cmap, ASScanline *buf, CARD8 *read_buf, CARD8 *gamma_table )
{

	return True;
}



ASImage *
tga2ASImage( const char * path, ASImageImportParams *params )
{
	ASImage *im = NULL ;
	/* More stuff */
	FILE         *infile;					   /* source file */
	ASTGAHeader   tga;
	ASTGAColorMap *cmap = NULL ;
	int width = 1, height = 1;
	START_TIME(started);


	if ((infile = open_image_file(path)) == NULL)
		return NULL;
	if( fread( &tga, 1, 3, infile ) == 3 )
	if( fread( &tga.ColormapSpec, 1, 5, infile ) == 5 )
	if( fread( &tga.ImageSpec, 1, 10, infile ) == 10 )
	{
		Bool success = True ;
		Bool (*load_row_func)(FILE *infile, ASTGAHeader *tga, ASTGAColorMap *cmap, ASScanline *buf, CARD8 *read_buf, CARD8 *gamma_table );

		if( tga.IDLength > 0 )
			success = (fseek( infile, tga.IDLength, SEEK_CUR )==0);
		if( success && tga.ColorMapType != 0 )
		{
			cmap = safecalloc( 1, sizeof(ASTGAColorMap));
			cmap->bytes_per_entry = (tga.ColormapSpec.ColorMapEntrySize+7)/8;
			cmap->bytes_total = cmap->bytes_per_entry*tga.ColormapSpec.ColorMapLength;
			cmap->data = safemalloc( cmap->bytes_total);
			success = ( fread( cmap->data, 1, cmap->bytes_total, infile ) == (unsigned int)cmap->bytes_total );
		}else if( tga.ImageSpec.Depth != 24 && tga.ImageSpec.Depth != 32 )
			success = False ;

		if( success )
		{
			success = False;
			if( tga.ImageType != TGA_NoImageData )
			{
				width = tga.ImageSpec.Width ;
				height = tga.ImageSpec.Height ;
				if( width < MAX_IMPORT_IMAGE_SIZE && height < MAX_IMPORT_IMAGE_SIZE )
					success = True;
			}
		}
		switch( tga.ImageType )
		{
			case TGA_ColormappedImage	:load_row_func = load_tga_colormapped ; break ;
			case TGA_TrueColorImage		:load_row_func = load_tga_truecolor ; break ;
			case TGA_BWImage			:load_row_func = load_tga_bw ; break ;
			case TGA_RLEColormappedImage:load_row_func = load_tga_rle_colormapped ; break ;
			case TGA_RLETrueColorImage	:load_row_func = load_tga_rle_truecolor ; break ;
			case TGA_RLEBWImage			:load_row_func = load_tga_rle_bw ; break ;
			default:
				load_row_func = NULL ;
		}

		if( success && load_row_func != NULL )
		{
			ASImageOutput  *imout ;
			int old_storage_block_size;
			im = create_asimage( width, height, params->compression );
			old_storage_block_size = set_asstorage_block_size( NULL, im->width*im->height*3/2 );

			if((imout = start_image_output( NULL, im, ASA_ASImage, 0, ASIMAGE_QUALITY_DEFAULT)) == NULL )
			{
        		destroy_asimage( &im );
				success = False;
			}else
			{
				ASScanline    buf;
				int y ;
				CARD8 *read_buf = safemalloc( width*4*2 );
				prepare_scanline( im->width, 0, &buf, True );
				if( !get_flags( tga.ImageSpec.Descriptor, TGA_TopToBottom ) )
					toggle_image_output_direction( imout );
				for( y = 0 ; y < height ; ++y )
				{
					if( !load_row_func( infile, &tga, cmap, &buf, read_buf, params->gamma_table ) )
						break;
					imout->output_image_scanline( imout, &buf, 1);
				}
				stop_image_output( &imout );
				free_scanline( &buf, True );
				free( read_buf );
			}
			set_asstorage_block_size( NULL, old_storage_block_size );

		}
	}
	if( im == NULL )
		show_error( "invalid or unsupported TGA format in image file \"%s\"", path );

	if (cmap) free (cmap);
	fclose( infile );
	SHOW_TIME("image loading",started);
	return im ;
}
