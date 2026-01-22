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

#ifdef HAVE_LIBXPM
#ifdef HAVE_LIBXPM_X11
#include <X11/xpm.h>
#else
#include <xpm.h>
#endif
#endif

#ifdef _WIN32
#include "win32/afterbase.h"
#else
#include "afterbase.h"
#endif

#include "asimage.h"
#include "imencdec.h"
#include "scanline.h"
#include "ximage.h"
#include "xpm.h"
#include "import.h"

#ifdef HAVE_XPM      /* XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM */

#ifdef LOCAL_DEBUG
Bool print_component(CARD32 *, int, unsigned int);
#endif

static ASImage *
xpm_file2ASImage(ASXpmFile *xpm_file, unsigned int compression)
{
	ASImage *im = NULL;
	int line = 0;

	LOCAL_DEBUG_OUT("do_alpha is %d. im->height = %d, im->width = %d",
									xpm_file->do_alpha, xpm_file->height, xpm_file->width);
	if (build_xpm_colormap(xpm_file))
		if ((im = create_xpm_image(xpm_file, compression)) != NULL) {
			int bytes_count = im->width * 4;
			ASFlagType rgb_flags = ASStorage_RLEDiffCompress | ASStorage_32Bit;
			ASFlagType alpha_flags =
					ASStorage_RLEDiffCompress | ASStorage_32Bit;
			int old_storage_block_size =
					set_asstorage_block_size(NULL, xpm_file->width * xpm_file->height * 3 / 2);

			if (!xpm_file->full_alpha)
				alpha_flags |= ASStorage_Bitmap;
			for (line = 0; line < xpm_file->height; ++line) {
				if (!convert_xpm_scanline(xpm_file, line))
					break;
				im->channels[IC_RED][line] = store_data(
						NULL, (CARD8 *)xpm_file->scl.red, bytes_count, rgb_flags, 0);
				im->channels[IC_GREEN][line] = store_data(
						NULL, (CARD8 *)xpm_file->scl.green, bytes_count, rgb_flags, 0);
				im->channels[IC_BLUE][line] = store_data(
						NULL, (CARD8 *)xpm_file->scl.blue, bytes_count, rgb_flags, 0);
				if (xpm_file->do_alpha)
					im->channels[IC_ALPHA][line] =
							store_data(NULL, (CARD8 *)xpm_file->scl.alpha, bytes_count,
												 alpha_flags, 0);
#ifdef LOCAL_DEBUG
				printf("%d: \"%s\"\n", line, xpm_file->str_buf);
				print_component(xpm_file->scl.red, 0, xpm_file->width);
				print_component(xpm_file->scl.green, 0, xpm_file->width);
				print_component(xpm_file->scl.blue, 0, xpm_file->width);
#endif
			}
			set_asstorage_block_size(NULL, old_storage_block_size);
		}
	return im;
}

ASImage *
xpm2ASImage(const char *path, ASImageImportParams *params)
{
	ASXpmFile *xpm_file = NULL;
	ASImage *im = NULL;
	START_TIME(started);

	LOCAL_DEBUG_CALLER_OUT("(\"%s\", 0x%lX)", path, params->flags);
	if ((xpm_file = open_xpm_file(path)) == NULL) {
		show_error("cannot open image file \"%s\" for reading. Please check permissions.", path);
		return NULL;
	}

	im = xpm_file2ASImage(xpm_file, params->compression);
	close_xpm_file(&xpm_file);

	SHOW_TIME("image loading", started);
	return im;
}

ASXpmFile *open_xpm_data(const char **data);
ASXpmFile *open_xpm_raw_data(const char *data);

ASImage *
xpm_data2ASImage(const char **data, ASImageImportParams *params)
{
	ASXpmFile *xpm_file = NULL;
	ASImage *im = NULL;
	START_TIME(started);

	LOCAL_DEBUG_CALLER_OUT("(\"%s\", 0x%lX)", (char *)data, params->flags);
	if ((xpm_file = open_xpm_data(data)) == NULL) {
		show_error("cannot read XPM data.");
		return NULL;
	}

	im = xpm_file2ASImage(xpm_file, params->compression);
	close_xpm_file(&xpm_file);

	SHOW_TIME("image loading", started);
	return im;
}

ASImage *
xpmRawBuff2ASImage(const char *data, ASImageImportParams *params)
{
	ASXpmFile *xpm_file = NULL;
	ASImage *im = NULL;
	START_TIME(started);

	LOCAL_DEBUG_CALLER_OUT("(\"%s\", 0x%lX)", (char *)data, params->flags);
	if ((xpm_file = open_xpm_raw_data(data)) == NULL) {
		show_error("cannot read XPM data.");
		return NULL;
	}

	im = xpm_file2ASImage(xpm_file, params->compression);
	close_xpm_file(&xpm_file);

	SHOW_TIME("image loading", started);
	return im;
}

#else				/* XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM */

ASImage *
xpm2ASImage(const char *path, ASImageImportParams *params)
{
	show_error("unable to load file \"%s\" - XPM image format is not supported.\n", path);
	return NULL;
}

#endif				/* XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM */
