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
#include "xcf.h"
#include "import.h"
#include "import_internal.h"

/***********************************************************************************/
/* XCF - GIMP's native file format : 											   */

ASImage *
xcf2ASImage( const char * path, ASImageImportParams *params )
{
	ASImage *im = NULL ;
	/* More stuff */
	FILE         *infile;					   /* source file */
	XcfImage  *xcf_im;
	START_TIME(started);

	/* we want to open the input file before doing anything else,
	 * so that the setjmp() error recovery below can assume the file is open.
	 * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
	 * requires it in order to read binary files.
	 */
	if ((infile = open_image_file(path)) == NULL)
		return NULL;

	xcf_im = read_xcf_image( infile );
	fclose( infile );

	if( xcf_im == NULL )
		return NULL;

	LOCAL_DEBUG_OUT("stored image size %ldx%ld", xcf_im->width,  xcf_im->height);
#ifdef LOCAL_DEBUG
	print_xcf_image( xcf_im );
#endif
	{/* TODO : temporary workaround untill we implement layers merging */
		XcfLayer *layer = xcf_im->layers ;
		while ( layer )
		{
			if( layer->hierarchy )
				if( layer->hierarchy->image )
					if( layer->hierarchy->width == xcf_im->width &&
						layer->hierarchy->height == xcf_im->height )
					{
						im = layer->hierarchy->image ;
						layer->hierarchy->image = NULL ;
					}
			layer = layer->next ;
		}
	}
 	free_xcf_image(xcf_im);

	SHOW_TIME("image loading",started);
	return im ;
}
