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

#ifdef HAVE_GIF
# ifdef HAVE_BUILTIN_UNGIF
#  include "libungif/gif_lib.h"
# else
#  include <gif_lib.h>
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

#ifdef _WIN32
# include "win32/afterbase.h"
#else
# include "afterbase.h"
#endif

#include "asimage.h"
#include "import.h"
#include "ungif.h"
#include "import_internal.h"

#ifdef HAVE_GIF		/* GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF */

int
gif_interlaced2y(int line /* 0 -- (height - 1) */, int height)
{
   	int passed_lines = 0;
   	int lines_in_current_pass;
   	/* pass 1 */
   	lines_in_current_pass = height / 8 + (height%8?1:0);
   	if (line < lines_in_current_pass)
    	return line * 8;

   	passed_lines = lines_in_current_pass;
   	/* pass 2 */
   	if (height > 4)
   	{
      	lines_in_current_pass = (height - 4) / 8 + ((height - 4)%8 ? 1 : 0);
      	if (line < lines_in_current_pass + passed_lines)
         	return 4 + 8*(line - passed_lines);
      	passed_lines += lines_in_current_pass;
   	}
   	/* pass 3 */
   	if (height > 2)
   	{
      	lines_in_current_pass = (height - 2) / 4 + ((height - 2)%4 ? 1 : 0);
      	if (line < lines_in_current_pass + passed_lines)
        	return 2 + 4*(line - passed_lines);
    	passed_lines += lines_in_current_pass;
   	}
	return 1 + 2*(line - passed_lines);
}


ASImage *
gif2ASImage( const char * path, ASImageImportParams *params )
{
	FILE			   *fp ;
	int					status = GIF_ERROR;
	GifFileType        *gif;
	ASImage 	 	   *im = NULL ;
	int  		transparent = -1 ;
	unsigned int  		y;
	unsigned int		width = 0, height = 0;
	ColorMapObject     *cmap = NULL ;

	START_TIME(started);

	params->return_animation_delay = 0 ;

	if ((fp = open_image_file(path)) == NULL)
		return NULL;
	if( (gif = open_gif_read(fp)) != NULL )
	{
		SavedImage	*sp = NULL ;
		int count = 0 ;

		status = get_gif_saved_images(gif, params->subimage, &sp, &count );
		if( status == GIF_OK && sp != NULL && count > 0 )
		{
			GifPixelType *row_pointer ;
#ifdef DEBUG_TRANSP_GIF
			fprintf( stderr, "Ext block = %p, count = %d\n", sp->ExtensionBlocks, sp->ExtensionBlockCount );
#endif
			if( sp->ExtensionBlocks )
				for ( y = 0; y < (unsigned int)sp->ExtensionBlockCount; y++)
				{
#ifdef DEBUG_TRANSP_GIF
					fprintf( stderr, "%d: func = %X, bytes[0] = 0x%X\n", y, sp->ExtensionBlocks[y].Function, sp->ExtensionBlocks[y].Bytes[0]);
#endif
					if( sp->ExtensionBlocks[y].Function == GRAPHICS_EXT_FUNC_CODE )
					{
						if( sp->ExtensionBlocks[y].Bytes[0]&0x01 )
						{
			   		 		transparent = ((unsigned int) sp->ExtensionBlocks[y].Bytes[GIF_GCE_TRANSPARENCY_BYTE])&0x00FF;
#ifdef DEBUG_TRANSP_GIF
							fprintf( stderr, "transp = %u\n", transparent );
#endif
						}
		   		 		params->return_animation_delay = (((unsigned int) sp->ExtensionBlocks[y].Bytes[GIF_GCE_DELAY_BYTE_LOW])&0x00FF) +
												   		((((unsigned int) sp->ExtensionBlocks[y].Bytes[GIF_GCE_DELAY_BYTE_HIGH])<<8)&0x00FF00);
					}else if(  sp->ExtensionBlocks[y].Function == APPLICATION_EXT_FUNC_CODE && sp->ExtensionBlocks[y].ByteCount == 11 ) /* application extension */
					{
						if( strncmp(&(sp->ExtensionBlocks[y].Bytes[0]), "NETSCAPE2.0", 11 ) == 0 )
						{
							++y ;
							if( y < (unsigned int)sp->ExtensionBlockCount && sp->ExtensionBlocks[y].ByteCount == 3 )
							{
				   		 		params->return_animation_repeats = (((unsigned int) sp->ExtensionBlocks[y].Bytes[GIF_NETSCAPE_REPEAT_BYTE_LOW])&0x00FF) +
														   		((((unsigned int) sp->ExtensionBlocks[y].Bytes[GIF_NETSCAPE_REPEAT_BYTE_HIGH])<<8)&0x00FF00);

#ifdef DEBUG_TRANSP_GIF
								fprintf( stderr, "animation_repeats = %d\n", params->return_animation_repeats );
#endif
							}
						}
					}
				}
			cmap = gif->SColorMap ;

			cmap = (sp->ImageDesc.ColorMap == NULL)?gif->SColorMap:sp->ImageDesc.ColorMap;
		    width = sp->ImageDesc.Width;
		    height = sp->ImageDesc.Height;

			if( cmap != NULL && (row_pointer = (unsigned char*)sp->RasterBits) != NULL &&
			    width < MAX_IMPORT_IMAGE_SIZE && height < MAX_IMPORT_IMAGE_SIZE )
			{
				int bg_color =   gif->SBackGroundColor ;
                int interlaced = sp->ImageDesc.Interlace;
                int image_y;
				CARD8 		 *r = NULL, *g = NULL, *b = NULL, *a = NULL ;
				int 	old_storage_block_size ;
				r = safemalloc( width );
				g = safemalloc( width );
				b = safemalloc( width );
				a = safemalloc( width );

				im = create_asimage( width, height, params->compression );
				old_storage_block_size = set_asstorage_block_size( NULL, im->width*im->height*3/2 );

				for (y = 0; y < height; ++y)
				{
					unsigned int x ;
					Bool do_alpha = False ;
                    image_y = interlaced ? gif_interlaced2y(y, height):y;
					for (x = 0; x < width; ++x)
					{
						int c = row_pointer[x];
      					if ( c == transparent)
						{
							c = bg_color ;
							do_alpha = True ;
							a[x] = 0 ;
						}else
							a[x] = 0x00FF ;

						r[x] = cmap->Colors[c].Red;
		        		g[x] = cmap->Colors[c].Green;
						b[x] = cmap->Colors[c].Blue;
	        		}
					row_pointer += x ;
					im->channels[IC_RED][image_y]  = store_data( NULL, r, width, ASStorage_RLEDiffCompress, 0);
				 	im->channels[IC_GREEN][image_y] = store_data( NULL, g, width, ASStorage_RLEDiffCompress, 0);
					im->channels[IC_BLUE][image_y]  = store_data( NULL, b, width, ASStorage_RLEDiffCompress, 0);
					if( do_alpha )
						im->channels[IC_ALPHA][image_y]  = store_data( NULL, a, im->width, ASStorage_RLEDiffCompress|ASStorage_Bitmap, 0);
				}
				set_asstorage_block_size( NULL, old_storage_block_size );
				free(a);
				free(b);
				free(g);
				free(r);
			}
			free_gif_saved_images( sp, count );
		}else if( status != GIF_OK )
			ASIM_PrintGifError();
		else if( params->subimage == -1 )
			show_error( "Image file \"%s\" does not have any valid image information.", path );
		else
			show_error( "Image file \"%s\" does not have subimage %d.", path, params->subimage );

		DGifCloseFile(gif);
		fclose( fp );
	}
	SHOW_TIME("image loading",started);
	return im ;
}
#else 			/* GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF */
ASImage *
gif2ASImage( const char * path, ASImageImportParams *params )
{
	show_error( "unable to load file \"%s\" - missing GIF image format libraries.\n", path );
	return NULL ;
}
#endif			/* GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF */

