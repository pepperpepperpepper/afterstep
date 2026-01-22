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

#ifdef HAVE_SVG
#include <librsvg/rsvg.h>
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
#include "import.h"

#ifdef HAVE_SVG/* SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG */
ASImage *
svg2ASImage( const char * path, ASImageImportParams *params )
{
   	static int gType_inited = 0;

   	ASImage *im = NULL;
   	GdkPixbuf *pixbuf;
	int channels ;
	Bool do_alpha ;
	int width = -1, height = -1 ;

	START_TIME(started);
#if 1
	/* Damn gtk mess... must init once atleast.. can we just init
	   several times or do we bork then? */
	if (gType_inited == 0)
	{
#ifndef GLIB_VERSION_2_36
	   g_type_init();
#endif
	   gType_inited = 1;
	}

 	if( get_flags( params->flags, AS_IMPORT_SCALED_H ) )
		width = (params->width <= 0)?((params->height<=0)?-1:params->height):params->width ;

 	if( get_flags( params->flags, AS_IMPORT_SCALED_V ) )
		height = (params->height <= 0)?((params->width <= 0)?-1:params->width):params->height ;

	if( (pixbuf = rsvg_pixbuf_from_file_at_size( path, width, height, NULL)) == NULL )
		return NULL ;

	channels = gdk_pixbuf_get_n_channels(pixbuf) ;
	do_alpha = gdk_pixbuf_get_has_alpha(pixbuf) ;
	if ( ((channels == 4 && do_alpha) ||(channels == 3 && !do_alpha)) &&
		gdk_pixbuf_get_bits_per_sample(pixbuf) == 8 )
	{
	   	int width, height;
		register CARD8 *row = gdk_pixbuf_get_pixels(pixbuf);
		int y;
		CARD8 		 *r = NULL, *g = NULL, *b = NULL, *a = NULL ;
		int old_storage_block_size;

		width = gdk_pixbuf_get_width(pixbuf);
		height = gdk_pixbuf_get_height(pixbuf);

		r = safemalloc( width );
		g = safemalloc( width );
		b = safemalloc( width );
		if( do_alpha )
			a = safemalloc( width );


		im = create_asimage(width, height, params->compression );
		old_storage_block_size = set_asstorage_block_size( NULL, im->width*im->height*3/2 );
		for (y = 0; y < height; ++y)
		{
			int x, i = 0 ;
			for( x = 0 ; x < width ; ++x )
			{
				r[x] = row[i++];
				g[x] = row[i++];
				b[x] = row[i++];
				if( do_alpha )
					a[x] = row[i++];
			}
			im->channels[IC_RED][y]  = store_data( NULL, r, width, ASStorage_RLEDiffCompress, 0);
		 	im->channels[IC_GREEN][y] = store_data( NULL, g, width, ASStorage_RLEDiffCompress, 0);
			im->channels[IC_BLUE][y]  = store_data( NULL, b, width, ASStorage_RLEDiffCompress, 0);

			if( do_alpha )
				for( x = 0 ; x < width ; ++x )
					if( a[x] != 0x00FF )
					{
						im->channels[IC_ALPHA][y]  = store_data( NULL, a, width, ASStorage_RLEDiffCompress, 0);
						break;
					}
			row += channels*width ;
		}
		set_asstorage_block_size( NULL, old_storage_block_size );
		free(r);
		free(g);
		free(b);
		if( a )
			free(a);
	}

	if (pixbuf)
		g_object_unref(pixbuf);
#endif
	SHOW_TIME("image loading",started);

	return im ;
}
#else 			/* SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG */

ASImage *
svg2ASImage( const char * path, ASImageImportParams *params )
{
	show_error( "unable to load file \"%s\" - missing SVG image format libraries.\n", path );
	return NULL ;
}
#endif			/* SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG SVG */
