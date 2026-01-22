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

#ifdef HAVE_PNG
/* Include file for users of png library. */
# ifdef HAVE_BUILTIN_PNG
#  include "libpng/png.h"
# else
#  include <png.h>
# endif
#else
# include <setjmp.h>
# ifdef HAVE_JPEG
#   ifdef HAVE_UNISTD_H
#     include <unistd.h>
#   endif
#   include <stdio.h>
# endif
#endif
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
/* <setjmp.h> is used for the optional error recovery mechanism */

#ifdef const
#undef const
#endif
#ifdef _WIN32
# include "win32/afterbase.h"
#else
# include "afterbase.h"
#endif
#ifdef HAVE_GIF
# ifdef HAVE_BUILTIN_UNGIF
#  include "libungif/gif_lib.h"
# else
#  include <gif_lib.h>
# endif
#endif
#ifdef HAVE_TIFF
#include <tiff.h>
#include <tiffio.h>
#endif
#ifdef HAVE_SVG
#include <librsvg/rsvg.h>
#endif
#ifdef HAVE_LIBXPM
#ifdef HAVE_LIBXPM_X11
#include <X11/xpm.h>
#else
#include <xpm.h>
#endif
#endif

#include "asimage.h"
#include "imencdec.h"
#include "scanline.h"
#include "ximage.h"
#include "xcf.h"
#include "xpm.h"
#include "ungif.h"
#include "import.h"
#include "asimagexml.h"
#include "transform.h"


/***********************************************************************************/
/* High level interface : 														   */
static char *locate_image_file( const char *file, char **paths );
static ASImageFileTypes	check_image_type( const char *realfilename );

as_image_loader_func as_image_file_loaders[ASIT_Unknown] =
{
	xpm2ASImage ,
	xpm2ASImage ,
	xpm2ASImage ,
	png2ASImage ,
	jpeg2ASImage,
	xcf2ASImage ,
	ppm2ASImage ,
	ppm2ASImage ,
	bmp2ASImage ,
	ico2ASImage ,
	ico2ASImage ,
	gif2ASImage ,
	tiff2ASImage,
	xml2ASImage ,
	svg2ASImage ,
	NULL,
	tga2ASImage,
	NULL,
	NULL,
	NULL
};

const char *as_image_file_type_names[ASIT_Unknown+1] =
{
	"XPM" ,
	"Z-compressed XPM" ,
	"GZ-compressed XPM" ,
	"PNG" ,
	"JPEG",
	"GIMP Xcf" ,
	"PPM" ,
	"PNM" ,
	"MS Windows Bitmap" ,
	"MS Windows Icon" ,
	"MS Windows Cursor" ,
	"GIF" ,
	"TIFF",
	"AfterStep XML" ,
	"Scalable Vector Graphics (SVG)" ,
	"XBM",
	"Targa",
	"PCX",
	"HTML",
	"XML",
	"Unknown"
};

char *locate_image_file_in_path( const char *file, ASImageImportParams *iparams ) 
{
	int 		  filename_len ;
	char 		 *realfilename = NULL, *tmp = NULL ;
	register int i;
	ASImageImportParams dummy_iparams = {0};

	if( iparams == NULL )
		iparams = &dummy_iparams ;

	if( file )
	{
		filename_len = strlen(file);
#ifdef _WIN32
		for( i = 0 ; iparams->search_path[i] != NULL ; ++i ) 
			unix_path2dos_path( iparams->search_path[i] );
#endif

		/* first lets try to find file as it is */
		if( (realfilename = locate_image_file(file, iparams->search_path)) == NULL )
		{
  		tmp = safemalloc( filename_len+3+1);
			strcpy(tmp, file);
		}
		if( realfilename == NULL && !get_flags(iparams->flags, AS_IMPORT_SKIP_COMPRESSED))
		{ /* let's try and see if appending .gz will make any difference */
			strcpy(&(tmp[filename_len]), ".gz");
			realfilename = locate_image_file(tmp,iparams->search_path);
		}
		if( realfilename == NULL && !get_flags(iparams->flags, AS_IMPORT_SKIP_COMPRESSED))
		{ /* let's try and see if appending .Z will make any difference */
			strcpy(&(tmp[filename_len]), ".Z");
			realfilename = locate_image_file(tmp,iparams->search_path);
		}
		if( realfilename == NULL )
		{ /* let's try and see if we have subimage number appended */
			for( i = filename_len-1 ; i > 0; i-- )
				if( !isdigit( (int)tmp[i] ) )
					break;
			if( i < filename_len-1 && i > 0 )
				if( tmp[i] == '.' )                 /* we have possible subimage number */
				{
					iparams->subimage = atoi( &tmp[i+1] );
					tmp[i] = '\0';
					filename_len = i ;
					realfilename = locate_image_file(tmp,iparams->search_path);
					if( realfilename == NULL  && !get_flags(iparams->flags, AS_IMPORT_SKIP_COMPRESSED))
					{ /* let's try and see if appending .gz will make any difference */
						strcpy(&(tmp[filename_len]), ".gz");
						realfilename = locate_image_file(tmp,iparams->search_path);
					}
					if( realfilename == NULL  && !get_flags(iparams->flags, AS_IMPORT_SKIP_COMPRESSED))
					{ /* let's try and see if appending .Z will make any difference */
						strcpy(&(tmp[filename_len]), ".Z");
						realfilename = locate_image_file(tmp,iparams->search_path);
					}
				}
		}
		if( tmp != realfilename && tmp != NULL )
			free( tmp );
	}
	if( realfilename == file )
		realfilename = mystrdup(file);
	return realfilename ;
}
ASImage *
file2ASImage_extra( const char *file, ASImageImportParams *iparams )
{
	char *realfilename ;
	ASImage *im = NULL;
	ASImageImportParams dummy_iparams = {0};
	
	if( iparams == NULL )
		iparams = &dummy_iparams ;

	realfilename = locate_image_file_in_path( file, iparams ); 
	
	if( realfilename != NULL ) 
	{
		ASImageFileTypes file_type = check_image_type( realfilename );

		if( file_type == ASIT_Unknown )
			show_error( "Hmm, I don't seem to know anything about format of the image file \"%s\"\n.\tPlease check the manual", realfilename );
		else if( as_image_file_loaders[file_type] )
		{
			char *g_var = getenv( "SCREEN_GAMMA" );
			if( g_var != NULL )
				iparams->gamma = atof(g_var);
			im = as_image_file_loaders[file_type](realfilename, iparams);
		}else
			show_error( "Support for the format of image file \"%s\" has not been implemented yet.", realfilename );
		/* returned image must not be tracked by any ImageManager yet !!! */
		if( im != NULL && im->imageman != NULL ) 
		{
			if( im->ref_count == 1 ) 
			{
				forget_asimage( im );
			}else
			{
				ASImage *tmp = clone_asimage( im , 0xFFFFFFFF); 
				if( tmp ) 
				{
					release_asimage( im );
					im = tmp ;
				}
			}
		}

#ifndef NO_DEBUG_OUTPUT
		if( im != NULL ) 
			show_progress( "image loaded from \"%s\"", realfilename );
#endif
		free( realfilename );
	}else if (!get_flags(iparams->flags, AS_IMPORT_IGNORE_IF_MISSING))
		show_warning( "I'm terribly sorry, but image file \"%s\" is nowhere to be found.", file );
	else
		show_debug( __FILE__, __FUNCTION__, __LINE__, "Image file \"%s\" is nowhere to be found.", file );
	return im;
}

void init_asimage_import_params( ASImageImportParams *iparams ) 
{
	if( iparams ) 
	{
		iparams->flags = 0 ;
		iparams->width = 0 ;
		iparams->height = 0 ;
		iparams->filter = SCL_DO_ALL ;
		iparams->gamma = 0. ;
		iparams->gamma_table = NULL ;
		iparams->compression = 100 ;
		iparams->format = ASA_ASImage ;
		iparams->search_path = NULL;
		iparams->subimage = 0 ;
	}
}

static Bool 
check_compressed_file_type (const char *file)
{
	int len = strlen (file);
	if (len > 5 && file[len-5] == '.') {
		if (mystrcasecmp (&file[len-4], "jpeg") == 0 || mystrcasecmp (&file[len-4], "tiff") == 0)
			return True;
	}else if (len > 4 && file[len-4] == '.') {
		if (mystrcasecmp (&file[len-3], "jpg") == 0 
		    || mystrcasecmp (&file[len-3], "tif") == 0
		    || mystrcasecmp (&file[len-3], "png") == 0
		    || mystrcasecmp (&file[len-3], "svg") == 0)
			return True;
	}
	return False; 
}

ASImage *
file2ASImage( const char *file, ASFlagType what, double gamma, unsigned int compression, ... )
{
	int i ;
	char 		 *paths[MAX_SEARCH_PATHS+1] ;
	ASImageImportParams iparams ;
	va_list       ap;

	init_asimage_import_params( &iparams );
	iparams.gamma = gamma ;
	iparams.compression = compression ;
	iparams.search_path = &(paths[0]);
	
	if (check_compressed_file_type (file))
		set_flags(iparams.flags, AS_IMPORT_SKIP_COMPRESSED);

	va_start (ap, compression);
	for( i = 0 ; i < MAX_SEARCH_PATHS ; i++ )
	{	
		if( (paths[i] = va_arg(ap,char*)) == NULL )
			break;
	}		   
	paths[MAX_SEARCH_PATHS] = NULL ;
	va_end (ap);

	return file2ASImage_extra( file, &iparams );

}

Pixmap
file2pixmap(ASVisual *asv, Window root, const char *realfilename, Pixmap *mask_out)
{
	Pixmap trg = None;
#ifndef X_DISPLAY_MISSING
	Pixmap mask = None ;
	if( asv && realfilename )
	{
		double gamma = SCREEN_GAMMA;
		char  *gamma_str;
		ASImage *im = NULL;

		if ((gamma_str = getenv ("SCREEN_GAMMA")) != NULL)
		{
			gamma = atof (gamma_str);
			if (gamma == 0.0)
				gamma = SCREEN_GAMMA;
		}

		im = file2ASImage( realfilename, 0xFFFFFFFF, gamma, 0, NULL );

		if( im != NULL )
		{
			trg = asimage2pixmap( asv, root, im, NULL, False );
			if( mask_out )
				if( get_flags( get_asimage_chanmask(im), SCL_DO_ALPHA ) )
					mask = asimage2mask( asv, root, im, NULL, False );
			destroy_asimage( &im );
		}
	}
	if( mask_out )
	{
		if( *mask_out && asv )
			XFreePixmap( asv->dpy, *mask_out );
		*mask_out = mask ;
	}
#endif
	return trg ;
}

static ASImage *
load_image_from_path( const char *file, char **path, double gamma, int quiet)
{
	ASImageImportParams iparams ;

	init_asimage_import_params( &iparams );
	iparams.gamma = gamma ;
	iparams.search_path = path;
	if (quiet)
		set_flags(iparams.flags, AS_IMPORT_IGNORE_IF_MISSING);

	if (check_compressed_file_type (file))
		set_flags(iparams.flags, AS_IMPORT_SKIP_COMPRESSED);
		
	return file2ASImage_extra( file, &iparams );
}

ASImageFileTypes
get_asimage_file_type( ASImageManager* imageman, const char *file )
{
	ASImageFileTypes file_type = ASIT_Unknown ;
	if( file )
	{
		ASImageImportParams iparams ;
		char *realfilename ;
	
		init_asimage_import_params( &iparams );
		iparams.search_path = imageman?&(imageman->search_path[0]):NULL;

		if (check_compressed_file_type (file))
			set_flags(iparams.flags, AS_IMPORT_SKIP_COMPRESSED);

		realfilename = locate_image_file_in_path( file, &iparams ); 
	
		if( realfilename != NULL ) 
		{
			file_type = check_image_type( realfilename );
			free( realfilename );
		}
	}
	return file_type;
}


static ASImage *
get_asimage_int( ASImageManager* imageman, const char *file, ASFlagType what, unsigned int compression, int quiet, int path)
{
	ASImage *im = NULL ;
	if( imageman && file )
		if( (im = fetch_asimage(imageman, file )) == NULL )
		{
			if (path < 0 || path >=MAX_SEARCH_PATHS)
				im = load_image_from_path( file, &(imageman->search_path[0]), imageman->gamma, quiet);
			else {
				char *tmp_search_path[MAX_SEARCH_PATHS+1];				
				int i;
				for (i = 1 ; i < MAX_SEARCH_PATHS+1 ; ++i)
					tmp_search_path[i] = NULL;
				tmp_search_path[0] = imageman->search_path[path];
				im = load_image_from_path( file, &(tmp_search_path[0]), imageman->gamma, quiet);
			}
			
			if( im )
			{
				store_asimage( imageman, im, file );
				set_flags( im->flags, ASIM_NAME_IS_FILENAME );
			}
				
		}
	return im;
}

ASImage *
get_asimage( ASImageManager* imageman, const char *file, ASFlagType what, unsigned int compression )
{
	return get_asimage_int(imageman, file, what, compression, 0, -1);
}

ASImage *
get_asimage_quiet( ASImageManager* imageman, const char *file, ASFlagType what, unsigned int compression)
{
	return get_asimage_int(imageman, file, what, compression, 1, -1);
}

void 
calculate_proportions( int src_w, int src_h, int *pdst_w, int *pdst_h ) 
{
	int dst_w = pdst_w?*pdst_w:0 ; 
	int dst_h = pdst_h?*pdst_h:0 ; 
	
	if( src_w > 0 && src_w >= src_h && (dst_w > 0 || dst_h <= 0)) 
		dst_h = (src_h*dst_w)/src_w ; 
	else if( src_h > 0 ) 
		dst_w = (src_w*dst_h)/src_h ; 

	if( pdst_w ) *pdst_w = dst_w ; 	
	if( pdst_h ) *pdst_h = dst_h ; 
}

static char* thumbnail_dir = NULL;
/* exported */ void set_asimage_thumbnails_cache_dir(const char* p_thumbnail_dir)
{
	struct stat stbuf;
	if (thumbnail_dir && p_thumbnail_dir
		&& !strcmp(thumbnail_dir, p_thumbnail_dir))
		return;

	if (thumbnail_dir)
	{
		free(thumbnail_dir);
		thumbnail_dir = NULL;
	}

	if (p_thumbnail_dir
		&& stat(p_thumbnail_dir, &stbuf) == 0
		&& S_ISDIR(stbuf.st_mode))
	{
		thumbnail_dir = strdup(p_thumbnail_dir);
		DEBUG_OUT("set thumbnail dir to %s", thumbnail_dir);
	}
}

static char* get_thumbnail_dir()
{
	return thumbnail_dir;
}

static char* get_thumbnail_image_path(const char * file, ASImageImportParams *iparams, int * need_save)
{
	char *th_dir = get_thumbnail_dir();
	*need_save = 0;
	if (!th_dir || !*th_dir)
		return NULL;

	static char result[2048];
	char * realfilename = locate_image_file_in_path( file, iparams );
	if (!realfilename)
		return NULL;

	struct stat rbuf;
	if (stat(realfilename, &rbuf) == -1)
	{
		DEBUG_OUT("image file %s does not exist", realfilename);
		free(realfilename);
		return NULL;
	}

	if (snprintf(result, sizeof(result), "%s/%s-%dx%d.png", th_dir, realfilename,
							    iparams->height, iparams->width) >= sizeof(result))
	{
		DEBUG_OUT("thumbnail path too long for file %s", file);
		free(realfilename);
		return NULL;
	}

	char *tmp;
	for(tmp = result + strlen(th_dir) + 1; *tmp; ++tmp)
		if (*tmp == '/') *tmp = '_';

	struct stat thbuf;
	if (stat(result, &thbuf) == -1)
		thbuf.st_ctime = 0;

	*need_save = rbuf.st_ctime > thbuf.st_ctime;
	free(realfilename);
	return result;
}


void print_asimage_func (ASHashableValue value);
ASImage *
get_thumbnail_asimage( ASImageManager* imageman, const char *file, int thumb_width, int thumb_height, ASFlagType flags )
{
	ASImage *im = NULL ;
#define AS_THUMBNAIL_NAME_FORMAT2	"%ld_%s_%dx%dthumb%ld"
	size_t len = strlen(file);
	char *thumbnail_name = safemalloc( len+sizeof(AS_THUMBNAIL_NAME_FORMAT2)+80 );



	if (imageman && file)
	{
		sprintf( thumbnail_name, AS_THUMBNAIL_NAME_FORMAT2, len, file, thumb_width, thumb_height, (long) flags) ;
		im = fetch_asimage(imageman, thumbnail_name );
	}
	
	if( !im && imageman && file )
	{
		ASImage *original_im = query_asimage(imageman, file );

		if( thumb_width <= 0 && thumb_height <= 0 ) 
		{
			thumb_width = 48 ;
			thumb_height = 48 ;
		}

		if( get_flags(flags, AS_THUMBNAIL_PROPORTIONAL ) ) 
		{
			if( original_im != NULL )		
				calculate_proportions( original_im->width, original_im->height, &thumb_width, &thumb_height );
		}else
		{
			if( thumb_width == 0 ) 
				thumb_width = thumb_height ; 
			if( thumb_height == 0 ) 
				thumb_height = thumb_width ; 
		}

		if( thumb_width > 0 && thumb_height > 0 ) 
		{
			//im = fetch_asimage(imageman, thumbnail_name );
			if( im == NULL )
			{
				if( original_im != NULL ) /* simply scale it down to a thumbnail size */
				{
					if( (( (int)original_im->width > thumb_width || (int)original_im->height > thumb_height ) && !get_flags( flags, AS_THUMBNAIL_DONT_REDUCE ) ) ||
						(( (int)original_im->width < thumb_width || (int)original_im->height < thumb_height ) && !get_flags( flags, AS_THUMBNAIL_DONT_ENLARGE ) ) )
					{
						im = scale_asimage( NULL, original_im, thumb_width, thumb_height, ASA_ASImage, 100, ASIMAGE_QUALITY_FAST );
						if( im != NULL ) 
							store_asimage( imageman, im, thumbnail_name );
					}else
						im = dup_asimage( original_im );
				}
			}
		}
		
		if( im == NULL ) 	
		{
			ASImage *tmp = NULL; 
			ASImageImportParams iparams ;

			init_asimage_import_params( &iparams );
			iparams.gamma = imageman->gamma ;
			iparams.search_path = &(imageman->search_path[0]);
			
			iparams.width = thumb_width ; 
			iparams.height = thumb_height ; 
			if( !get_flags( flags, AS_THUMBNAIL_DONT_ENLARGE|AS_THUMBNAIL_DONT_REDUCE ) )
				iparams.flags |= AS_IMPORT_RESIZED|AS_IMPORT_SCALED_BOTH ; 
			
			if( get_flags( flags, AS_THUMBNAIL_DONT_ENLARGE ) )
				iparams.flags |= AS_IMPORT_FAST ; 

			int save_thumbnail = 0;
			char * thumbfile = get_thumbnail_image_path(file, &iparams, &save_thumbnail);
			if (thumbfile) thumbfile = strdup(thumbfile);
			if (!save_thumbnail && thumbfile)
				tmp = file2ASImage_extra( thumbfile, &iparams );
			LOCAL_DEBUG_OUT("Thumbnail path: %s --> %s, %d , %p", file, thumbfile, save_thumbnail, tmp);
			if (!tmp)
			{
				tmp = file2ASImage_extra( file, &iparams );
				save_thumbnail = 1;
			}
			if( tmp ) 
			{
				im = tmp ; 
				if( (int)tmp->width != thumb_width || (int)tmp->height != thumb_height ) 
				{
					if( get_flags(flags, AS_THUMBNAIL_PROPORTIONAL ) ) 
					{
						calculate_proportions( tmp->width, tmp->height, &thumb_width, &thumb_height );
						//sprintf( thumbnail_name, AS_THUMBNAIL_NAME_FORMAT, file, thumb_height );
						if( (im = query_asimage( imageman, thumbnail_name )) == NULL ) 
							im = tmp ; 
					}
					if( im == tmp )
					{
						if( (( (int)tmp->width > thumb_width || (int)tmp->height > thumb_height ) && !get_flags( flags, AS_THUMBNAIL_DONT_REDUCE ) ) ||
							(( (int)tmp->width < thumb_width || (int)tmp->height < thumb_height ) && !get_flags( flags, AS_THUMBNAIL_DONT_ENLARGE ) ) )
						{
							im = scale_asimage( NULL, tmp, thumb_width, thumb_height, ASA_ASImage, 100, ASIMAGE_QUALITY_FAST );
							if( im == NULL ) 
								im = tmp ;
						}
					}
				}			

				if( im != NULL )
				{
					if( im->imageman == NULL )
						store_asimage( imageman, im, thumbnail_name );
					else
						dup_asimage( im );
				}
				
				if( im != tmp ) 
					destroy_asimage( &tmp );
				if (save_thumbnail && thumbfile)
				{
					LOCAL_DEBUG_OUT("Saving thumbnail to file %s", thumbfile);
					save_asimage_to_file(thumbfile,
							     im,
							     "png",
							     NULL,
							     NULL,
							     0,
							     1
							     );
				}
			}

			if (thumbfile) free(thumbfile);	
		}
								 
	}
	if( thumbnail_name )
		free( thumbnail_name );
	return im;
}


Bool
reload_asimage_manager( ASImageManager *imman )
{
#if (HAVE_AFTERBASE_FLAG==1)
	if( imman != NULL ) 
	{
		ASHashIterator iter ;
		if( start_hash_iteration (imman->image_hash, &iter) )
		{
			do
			{
				ASImage *im = curr_hash_data( &iter );
/*fprintf( stderr, "im = %p. flags = 0x%lX\n", im, im->flags );		*/
				if( get_flags( im->flags, ASIM_NAME_IS_FILENAME ) )
				{
/*fprintf( stderr, "reloading image \"%s\" ...", im->name );*/
					ASImage *reloaded_im = load_image_from_path( im->name, &(imman->search_path[0]), imman->gamma, 0);
/*fprintf( stderr, "Done. reloaded_im = %p.\n", reloaded_im );*/					
					if( reloaded_im ) 
					{
						if( asimage_replace (im, reloaded_im) ) 
							free( reloaded_im );
						else
							destroy_asimage( &reloaded_im );
					}				
				}
			}while( next_hash_item( &iter ) );
			return True;		
		}
	}
#endif
	return False;
}


ASImageListEntry * 
ref_asimage_list_entry( ASImageListEntry *entry )
{
	if( entry ) 
	{
		if( IS_ASIMAGE_LIST_ENTRY(entry) )
			++(entry->ref_count);
		else
			entry = NULL ; 
	}
	return entry;
}
	 
ASImageListEntry *
unref_asimage_list_entry( ASImageListEntry *entry )
{
	if( entry ) 
	{	
		if( IS_ASIMAGE_LIST_ENTRY(entry) )
		{
			--(entry->ref_count);
			if( entry->ref_count  <= 0 )
			{
				ASImageListEntry *prev = entry->prev ; 
				ASImageListEntry *next = entry->next ; 
				if( !IS_ASIMAGE_LIST_ENTRY(prev) )
					prev = NULL ; 
				if( !IS_ASIMAGE_LIST_ENTRY(next) )
					next = NULL ; 
				if( prev ) 
					prev->next = next ; 
				if( next ) 
					next->prev = prev ; 

				if( entry->preview ) 
					safe_asimage_destroy( entry->preview );
				if( entry->name )
					free( entry->name );
				if( entry->fullfilename )
					free( entry->fullfilename );
				if( entry->buffer ) 
					destroy_asimage_list_entry_buffer( &(entry->buffer) );
				memset( entry, 0x00, sizeof(ASImageListEntry));
				free( entry );
				entry = NULL ; 
			}	 
		}else
			entry = NULL ;
	}
	return entry;
}	 

ASImageListEntry *
create_asimage_list_entry()
{
	ASImageListEntry *entry = safecalloc( 1, sizeof(ASImageListEntry));
	entry->ref_count = 1 ; 
	entry->magic = MAGIC_ASIMAGE_LIST_ENTRY ; 
	return entry;
}

void
destroy_asimage_list( ASImageListEntry **plist )
{
	if( plist )
	{		   
		ASImageListEntry *curr = *plist ;
		while( IS_ASIMAGE_LIST_ENTRY(curr) )
		{	
			ASImageListEntry *to_delete = curr ; 
			curr = curr->next ;
		 	unref_asimage_list_entry( to_delete );
		}
		*plist = NULL ;
	}
}

void destroy_asimage_list_entry_buffer( ASImageListEntryBuffer **pbuffer )
{
	if( pbuffer && *pbuffer ) 
	{		 
		if( (*pbuffer)->data ) 
			free( (*pbuffer)->data ) ;
		free( *pbuffer );
		*pbuffer = NULL ;
	}
}	 

struct ASImageListAuxData
{
	ASImageListEntry **pcurr;
	ASImageListEntry *last ;
	ASFlagType preview_type ;
	unsigned int preview_width ;
	unsigned int preview_height ;
	unsigned int preview_compression ;
	ASVisual *asv;
};

#ifndef _WIN32
Bool 
direntry2ASImageListEntry( const char *fname, const char *fullname, 
						   struct stat *stat_info, void *aux_data)
{
	struct ASImageListAuxData *data = (struct ASImageListAuxData*)aux_data;
	ASImageFileTypes file_type ;
	ASImageListEntry *curr ;
	   	
	if (S_ISDIR (stat_info->st_mode))
		return False;
	
	file_type = check_image_type( fullname );
	if( file_type != ASIT_Unknown && as_image_file_loaders[file_type] == NULL )
		file_type = ASIT_Unknown ;

	curr = create_asimage_list_entry();
	*(data->pcurr) = curr ; 
	if( data->last )
		data->last->next = curr ;
	curr->prev = data->last ;
	data->last = curr ;
	data->pcurr = &(data->last->next);

	curr->name = mystrdup( fname );
	curr->fullfilename = mystrdup(fullname);
	curr->type = file_type ;
   	curr->d_mode = stat_info->st_mode;
	curr->d_mtime = stat_info->st_mtime;
	curr->d_size  = stat_info->st_size;

	if( curr->type != ASIT_Unknown && data->preview_type != 0 )
	{
		ASImageImportParams iparams = {0} ;
		ASImage *im = as_image_file_loaders[file_type](fullname, &iparams);
		if( im )
		{
			int scale_width = im->width ;
			int scale_height = im->height ;
			int tile_width = im->width ;
			int tile_height = im->height ;

			if( data->preview_width > 0 )
			{
				if( get_flags( data->preview_type, SCALE_PREVIEW_H ) )
					scale_width = data->preview_width ;
				else
					tile_width = data->preview_width ;
			}
			if( data->preview_height > 0 )
			{
				if( get_flags( data->preview_type, SCALE_PREVIEW_V ) )
					scale_height = data->preview_height ;
				else
					tile_height = data->preview_height ;
			}
			if( scale_width != im->width || scale_height != im->height )
			{
				ASImage *tmp = scale_asimage( data->asv, im, scale_width, scale_height, ASA_ASImage, data->preview_compression, ASIMAGE_QUALITY_DEFAULT );
				if( tmp != NULL )
				{
					destroy_asimage( &im );
					im = tmp ;
				}
			}
			if( tile_width != im->width || tile_height != im->height )
			{
				ASImage *tmp = tile_asimage( data->asv, im, 0, 0, tile_width, tile_height, TINT_NONE, ASA_ASImage, data->preview_compression, ASIMAGE_QUALITY_DEFAULT );
				if( tmp != NULL )
				{
					destroy_asimage( &im );
					im = tmp ;
				}
			}
		}

		curr->preview = im ;
	}
	return True;
}
#endif

ASImageListEntry *
get_asimage_list( ASVisual *asv, const char *dir,
	              ASFlagType preview_type, double gamma,
				  unsigned int preview_width, unsigned int preview_height,
				  unsigned int preview_compression,
				  unsigned int *count_ret,
				  int (*select) (const char *) )
{
	ASImageListEntry *im_list = NULL ;
#ifndef _WIN32
	struct ASImageListAuxData aux_data ; 
	int count ; 
	
	aux_data.pcurr = &im_list;
	aux_data.last = NULL;
	aux_data.preview_type = preview_type;
	aux_data.preview_width = preview_width;
	aux_data.preview_height = preview_height;
	aux_data.preview_compression  = preview_compression;
	aux_data.asv = asv ; 
	
	
	if( asv == NULL || dir == NULL )
		return NULL ;

	count = my_scandir_ext ((char*)dir, select, direntry2ASImageListEntry, &aux_data);

	if( count_ret )
		*count_ret = count ;
#endif
	return im_list;
}

char *format_asimage_list_entry_details( ASImageListEntry *entry, Bool vertical )
{
	char *details_text ;

	if( entry ) 
	{	
		int type = (entry->type>ASIT_Unknown)?ASIT_Unknown:entry->type ; 
		details_text = safemalloc(128);
		if( entry->preview ) 
			sprintf( details_text, vertical?"File type: %s\nSize %dx%d":"File type: %s; Size %dx%d", as_image_file_type_names[type], entry->preview->width, entry->preview->height ); 	  
		else 
			sprintf( details_text, "File type: %s", as_image_file_type_names[type]);
	}else
		details_text = mystrdup("");		   
	return details_text;
}	 

Bool 
load_asimage_list_entry_data( ASImageListEntry *entry, size_t max_bytes )
{
	char * new_buffer ; 
	size_t new_buffer_size ;
	FILE *fp;
	Bool binary = False ; 
	if( entry == NULL ) 
		return False;
	if( entry->buffer == NULL ) 
		entry->buffer = safecalloc( 1, sizeof(ASImageListEntryBuffer) );
	if( (int)entry->buffer->size == entry->d_size || entry->buffer->size >= max_bytes )
		return True;
	new_buffer_size = min( max_bytes, (size_t)entry->d_size ); 
	new_buffer = malloc( new_buffer_size );
	if( new_buffer == NULL ) 
		return False ;
	if( entry->buffer->size > 0 ) 
	{	
		memcpy( new_buffer, entry->buffer->data, entry->buffer->size ) ;
		free( entry->buffer->data );
	}
	entry->buffer->data = new_buffer ; 
	/* TODO read new_buffer_size - entry->buffer_size bytes into the end of the buffer */
	fp = fopen(entry->fullfilename, "rb");
	if ( fp != NULL ) 
	{
		int len = new_buffer_size - entry->buffer->size ;
		if( entry->buffer->size > 0 ) 
			fseek( fp, entry->buffer->size, SEEK_SET );
		len = fread(entry->buffer->data, 1, len, fp);
		if( len > 0 ) 
			entry->buffer->size += len ;
		fclose(fp);
	}

	if( entry->type == ASIT_Unknown ) 
	{
		int i = entry->buffer->size ; 
		register char *ptr = entry->buffer->data ;
		while ( --i >= 0 )	
			if( !isprint(ptr[i]) && ptr[i] != '\n'&& ptr[i] != '\r'&& ptr[i] != '\t' )	
				break;
		binary = (i >= 0);				
	}else
		binary = (entry->type != ASIT_Xpm  && entry->type != ASIT_XMLScript &&
			  	  entry->type != ASIT_HTML && entry->type != ASIT_XML ); 
	if( binary ) 
		set_flags( entry->buffer->flags, ASILEB_Binary );
   	else
		clear_flags( entry->buffer->flags, ASILEB_Binary );
	 


	return True;
}

/***********************************************************************************/
/* Some helper functions :                                                         */

static char *
locate_image_file( const char *file, char **paths )
{
	char *realfilename = NULL;
	if( file != NULL )
	{
		realfilename = mystrdup( file );
#ifdef _WIN32
		unix_path2dos_path( realfilename );
#endif
		
		if( CheckFile( realfilename ) != 0 )
		{
			free( realfilename ) ;
			realfilename = NULL ;
			if(file[0] != '/' && paths != NULL )
			{	/* now lets try and find the file in any of the optional paths :*/
				register int i = 0;
				do
				{
					if( i > 0 ) 
					{	
						show_progress( "looking for image \"%s\" in path [%s]", file, paths[i] );
					}		
					realfilename = find_file( file, paths[i], R_OK );
				}while( realfilename == NULL && paths[i++] != NULL );
			}
		}
	}
	return realfilename;
}

FILE*
open_image_file( const char *path )
{
	FILE *fp = NULL;
	if ( path )
	{
		if ((fp = fopen (path, "rb")) == NULL)
			show_error("cannot open image file \"%s\" for reading. Please check permissions.", path);
	}else
		fp = stdin ;
	return fp ;
}

static ASImageFileTypes
check_image_type( const char *realfilename )
{
	ASImageFileTypes type = ASIT_Unknown ;
	int filename_len = strlen( realfilename );
	FILE *fp ;
#define FILE_HEADER_SIZE	512

	/* lets check if we have compressed xpm file : */
	if( filename_len > 5 && (mystrncasecmp( realfilename+filename_len-5, ".html", 5 ) == 0 || 
							 mystrncasecmp( realfilename+filename_len-4, ".htm", 4 ) == 0 ))
		type = ASIT_HTML;
	else if( filename_len > 7 && mystrncasecmp( realfilename+filename_len-7, ".xpm.gz", 7 ) == 0 )
		type = ASIT_GZCompressedXpm;
	else if( filename_len > 6 && mystrncasecmp( realfilename+filename_len-6, ".xpm.Z", 6 ) == 0 )
		type = ASIT_ZCompressedXpm ;
	else if( (fp = open_image_file( realfilename )) != NULL )
	{
		char head[FILE_HEADER_SIZE+1] ;
		int bytes_in = 0 ;
		memset(&head[0], 0x00, sizeof(head));
		bytes_in = fread( &(head[0]), sizeof(char), FILE_HEADER_SIZE, fp );
		DEBUG_OUT("%s: head[0]=0x%2.2X(%d),head[2]=0x%2.2X(%d)\n", realfilename+filename_len-4, head[0], head[0], head[2], head[2] );
/*		fprintf( stderr, " IMAGE FILE HEADER READS : [%s][%c%c%c%c%c%c%c%c][%s], bytes_in = %d\n", (char*)&(head[0]),
						head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7], strstr ((char *)&(head[0]), "XPM"),bytes_in );
 */
		if( bytes_in > 3 )
		{
			if( (CARD8)head[0] == 0xff && (CARD8)head[1] == 0xd8 && (CARD8)head[2] == 0xff)
				type = ASIT_Jpeg;
			else if (strstr ((char *)&(head[0]), "XPM") != NULL)
				type =  ASIT_Xpm;
			else if (head[1] == 'P' && head[2] == 'N' && head[3] == 'G')
				type = ASIT_Png;
			else if (head[0] == 'G' && head[1] == 'I' && head[2] == 'F')
				type = ASIT_Gif;
			else if (head[0] == head[1] && (head[0] == 'I' || head[0] == 'M'))
				type = ASIT_Tiff;
			else if (head[0] == 'P' && isdigit(head[1]))
				type = (head[1]!='5' && head[1]!='6')?ASIT_Pnm:ASIT_Ppm;
			else if (head[0] == 0xa && head[1] <= 5 && head[2] == 1)
				type = ASIT_Pcx;
			else if (head[0] == 'B' && head[1] == 'M')
				type = ASIT_Bmp;
			else if (head[0] == 0 && head[2] == 1 && mystrncasecmp(realfilename+filename_len-4, ".ICO", 4)==0 )
				type = ASIT_Ico;
			else if (head[0] == 0 && head[2] == 2 &&
						(mystrncasecmp(realfilename+filename_len-4, ".CUR", 4)==0 ||
						 mystrncasecmp(realfilename+filename_len-4, ".ICO", 4)==0) )
				type = ASIT_Cur;
		}
		if( type == ASIT_Unknown && bytes_in  > 6 )
		{
			if( mystrncasecmp( head, "<HTML>", 6 ) == 0 )
				type = ASIT_HTML;	
		}	 
		if( type == ASIT_Unknown && bytes_in  > 8 )
		{
			if( strncmp(&(head[0]), XCF_SIGNATURE, (size_t) XCF_SIGNATURE_LEN) == 0)
				type = ASIT_Xcf;
	   		else if (head[0] == 0 && head[1] == 0 &&
			    	 head[2] == 2 && head[3] == 0 && head[4] == 0 && head[5] == 0 && head[6] == 0 && head[7] == 0)
				type = ASIT_Targa;
			else if (strncmp (&(head[0]), "#define", (size_t) 7) == 0)
				type = ASIT_Xbm;
			else if( mystrncasecmp(realfilename+filename_len-4, ".SVG", 4)==0 )
				type = ASIT_SVG ;
			else
			{/* the nastiest check - for XML files : */
				int i ;

				type = ASIT_XMLScript ;
				for( i = 0 ; i < bytes_in ; ++i ) if( !isspace(head[i]) ) break;
				while( bytes_in > 0 && type == ASIT_XMLScript )
				{
					if( i >= bytes_in )
					{	
						bytes_in = fread( &(head[0]), sizeof(CARD8), FILE_HEADER_SIZE, fp );
						for( i = 0 ; i < bytes_in ; ++i ) if( !isspace(head[i]) ) break;
					}
					else if( head[i] != '<' )
						type = ASIT_Unknown ;
					else if( mystrncasecmp( &(head[i]), "<svg", 4 ) == 0 ) 
					{
						type = ASIT_SVG ;
					}else if( mystrncasecmp( &(head[i]), "<!DOCTYPE ", 10 ) == 0 ) 
					{	
						type = ASIT_XML ;
						for( i += 9 ; i < bytes_in ; ++i ) if( !isspace(head[i]) ) break;
						if( i < bytes_in ) 
						{
					 		if( mystrncasecmp( &(head[i]), "afterstep-image-xml", 19 ) == 0 ) 			
							{
								i += 19 ;	  
								type = ASIT_XMLScript ;
							}
						}	 
					}else
					{
						while( bytes_in > 0 && type == ASIT_XMLScript )
						{
							while( ++i < bytes_in )
								if( !isspace(head[i]) )
								{
									if( !isprint(head[i]) )
									{
										type = ASIT_Unknown ;
										break ;
									}else if( head[i] == '>' )
										break ;
								}

							if( i >= bytes_in )
							{	
								bytes_in = fread( &(head[0]), sizeof(CARD8), FILE_HEADER_SIZE, fp );
								i = 0 ; 
							}else
								break ;
						}
						break;
					}	
				}
			}
		}
		fclose( fp );
	}
	return type;
}


ASImageFileTypes
check_asimage_file_type( const char *realfilename )
{
	if( realfilename == NULL ) 
		return ASIT_Unknown;
	return check_image_type( realfilename );
}

/***********************************************************************************/
/* XPM loader moved to import_xpm.c */
/***********************************************************************************/

/* PNG loader moved to import_png.c */
/***********************************************************************************/


/***********************************************************************************/
#ifdef HAVE_JPEG     /* JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG */
/* JPEG loader moved to import_jpeg.c */
#else 			/* JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG */
/* JPEG loader moved to import_jpeg.c */
#endif 			/* JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG */
/***********************************************************************************/

/* XCF loader moved to import_xcf.c */

/* PPM loader moved to import_ppm.c */

/***********************************************************************************/
/* GIF loader moved to import_gif.c */

/* TIFF loader moved to import_tiff.c */


static ASImage *
load_xml2ASImage( ASImageManager *imman, const char *path, unsigned int compression, int width, int height )
{
	ASVisual fake_asv ;
	char *slash, *curr_path = NULL ;
	char *doc_str = NULL ;
	ASImage *im = NULL ;

	memset( &fake_asv, 0x00, sizeof(ASVisual) );
	if( (slash = strrchr( path, '/' )) != NULL )
		curr_path = mystrndup( path, slash-path );

	if((doc_str = load_file(path)) == NULL )
		show_error( "unable to load file \"%s\" file is either too big or is not readable.\n", path );
	else
	{
		im = compose_asimage_xml_at_size(&fake_asv, imman, NULL, doc_str, 0, 0, None, curr_path, width, height);
		free( doc_str );
	}

	if( curr_path )
		free( curr_path );
	return im ;
}


ASImage *
xml2ASImage( const char *path, ASImageImportParams *params )
{
	int width = -1, height = -1 ; 
	static ASImage 	 *im = NULL ;
	START_TIME(started);

 	if( get_flags( params->flags, AS_IMPORT_SCALED_H ) )
		width = (params->width <= 0)?((params->height<=0)?-1:params->height):params->width ;
	
 	if( get_flags( params->flags, AS_IMPORT_SCALED_V ) )
		height = (params->height <= 0)?((params->width <= 0)?-1:params->width):params->height ;
		
	im = load_xml2ASImage( NULL, path, params->compression, width, height );

	SHOW_TIME("image loading",started);
	return im ;
}

/* SVG loader moved to import_svg.c */


/* TGA loader moved to import_tga.c */
/*************************************************************************/
/* ARGB 																 */
/*************************************************************************/
ASImage *
convert_argb2ASImage( ASVisual *asv, int width, int height, ARGB32 *argb, CARD8 *gamma_table )
{
	ASImage *im = NULL ;
	ASImageOutput  *imout ;
	im = create_asimage( width, height, 100 );
	if((imout = start_image_output( NULL, im, ASA_ASImage, 0, ASIMAGE_QUALITY_DEFAULT)) == NULL )
	{
   		destroy_asimage( &im );
		return NULL;
	}else
	{	
		ASScanline    buf;
		int y ;
		int old_storage_block_size = set_asstorage_block_size( NULL, im->width*im->height*3 );

		prepare_scanline( im->width, 0, &buf, True );
		for( y = 0 ; y < height ; ++y ) 
		{	  
			int x ;
			for( x = 0 ; x < width ; ++x ) 
			{
				ARGB32 c = argb[x];
				buf.alpha[x] 	= ARGB32_ALPHA8(c);	
				buf.red[x] 	= ARGB32_RED8(c);	  
				buf.green[x] 	= ARGB32_GREEN8(c);	  
				buf.blue[x] 	= ARGB32_BLUE8(c);	  
			}	 
			argb += width ;			
			set_flags( buf.flags, SCL_DO_RED|SCL_DO_GREEN|SCL_DO_BLUE|SCL_DO_ALPHA );
			imout->output_image_scanline( imout, &buf, 1);
		}
		set_asstorage_block_size( NULL, old_storage_block_size );
		stop_image_output( &imout );
		free_scanline( &buf, True );
	}   
						
	return im ;	
}


ASImage *
argb2ASImage( const char *path, ASImageImportParams *params )
{
	ASVisual fake_asv ;
	long argb_data_len = -1; 
	char *argb_data = NULL ;
	ASImage *im = NULL ;

	memset( &fake_asv, 0x00, sizeof(ASVisual) );

	argb_data = load_binary_file(path, &argb_data_len);
	if(argb_data == NULL || argb_data_len < 8 )
		show_error( "unable to load file \"%s\" file is either too big or is not readable.\n", path );
	else
	{
		int width = ((CARD32*)argb_data)[0] ;
		int height = ((CARD32*)argb_data)[1] ;
		if( 2 + width*height > (int)(argb_data_len/sizeof(CARD32)))
		{
			show_error( "file \"%s\" is too small for specified image size of %dx%d.\n", path, width, height );
		}else
			im = convert_argb2ASImage( &fake_asv, width, height, (ARGB32*)argb_data+2, params->gamma_table );
	}
	if( argb_data ) 
		free( argb_data );
	
	return im ;
}
