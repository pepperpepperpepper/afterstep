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
#include "scanline.h"
#include "import.h"
#include "import_internal.h"

/***********************************************************************************/
/* PPM/PNM file format : 											   				   */
ASImage *
ppm2ASImage( const char * path, ASImageImportParams *params )
{
	ASImage *im = NULL ;
	/* More stuff */
	FILE         *infile;					   /* source file */
	ASScanline    buf;
	int y;
	unsigned int type = 0, width = 0, height = 0, colors = 0;
#define PPM_BUFFER_SIZE 71                     /* Sun says that no line should be longer then this */
	char buffer[PPM_BUFFER_SIZE];
	START_TIME(started);

	if ((infile = open_image_file(path)) == NULL)
		return NULL;

	if( fgets( &(buffer[0]), PPM_BUFFER_SIZE, infile ) )
	{
		if( buffer[0] == 'P' )
			switch( buffer[1] )
			{    /* we only support RAWBITS formats : */
					case '5' : 	type= 5 ; break ;
					case '6' : 	type= 6 ; break ;
					case '8' : 	type= 8 ; break ;
				default:
					show_error( "invalid or unsupported PPM/PNM file format in image file \"%s\"", path );
			}
		if( type > 0 )
		{
			while ( fgets( &(buffer[0]), PPM_BUFFER_SIZE, infile ) )
			{
				if( buffer[0] != '#' )
				{
					register int i = 0;
					if( width > 0 )
					{
						colors = atoi(&(buffer[i]));
						break;
					}
					width = atoi( &(buffer[i]) );
					while ( buffer[i] != '\0' && !isspace((int)buffer[i]) ) ++i;
					while ( isspace((int)buffer[i]) ) ++i;
					if( buffer[i] != '\0')
						height = atoi(&(buffer[i]));
				}
			}
		}
	}

	if( type > 0 && colors <= 255 &&
		width > 0 && width < MAX_IMPORT_IMAGE_SIZE &&
		height > 0 && height < MAX_IMPORT_IMAGE_SIZE )
	{
		CARD8 *data ;
		size_t row_size = width * ((type==6)?3:((type==8)?4:1));

		data = safemalloc( row_size );

		LOCAL_DEBUG_OUT("stored image size %dx%d", width,  height);
		im = create_asimage( width,  height, params->compression );
		prepare_scanline( im->width, 0, &buf, False );
		y = -1 ;
		/*cinfo.output_scanline*/
		while ( ++y < (int)height )
		{
			if( fread( data, sizeof (char), row_size, infile ) < row_size )
				break;

			raw2scanline( data, &buf, params->gamma_table, im->width, (type==5), (type==8));

			asimage_add_line (im, IC_RED,   buf.red  , y);
			asimage_add_line (im, IC_GREEN, buf.green, y);
			asimage_add_line (im, IC_BLUE,  buf.blue , y);
			if( type == 8 )
				asimage_add_line (im, IC_ALPHA,   buf.alpha  , y);
		}
		free_scanline(&buf, True);
		free( data );
	}
	fclose( infile );
	SHOW_TIME("image loading",started);
	return im ;
}
