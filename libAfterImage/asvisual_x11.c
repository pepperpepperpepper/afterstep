/*
 * Copyright (c) 2001,2000,1999 Sasha Vasko <sasha at aftercode.net>
 *
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

#ifdef _WIN32
#include "win32/config.h"
#else
#include "config.h"
#endif

#undef LOCAL_DEBUG
#undef DEBUG_SL2XIMAGE
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef _WIN32
# include "win32/afterbase.h"
#else
# include "afterbase.h"
#endif
#include "asvisual.h"
#include "scanline.h"

#if defined(XSHMIMAGE) && !defined(X_DISPLAY_MISSING)
# include <sys/ipc.h>
# include <sys/shm.h>
# include <X11/extensions/XShm.h>
#else
# undef XSHMIMAGE
#endif

#if defined(HAVE_GLX) && !defined(X_DISPLAY_MISSING)
# include <GL/gl.h>
# include <GL/glx.h>
#else
# undef HAVE_GLX
#endif

#ifndef X_DISPLAY_MISSING
void _XInitImageFuncPtrs(XImage*);
#endif
/*********************************************************************/
/* handy utility functions for creation of windows/pixmaps/XImages : */
/*********************************************************************/
Window
create_visual_window( ASVisual *asv, Window parent,
					  int x, int y, unsigned int width, unsigned int height,
					  unsigned int border_width, unsigned int wclass,
 					  unsigned long mask, XSetWindowAttributes *attributes )
{
#ifndef X_DISPLAY_MISSING
	XSetWindowAttributes my_attr ;
	int depth = 0;

	if( asv == NULL || parent == None )
		return None ;
LOCAL_DEBUG_OUT( "Colormap %lX, parent %lX, %ux%u%+d%+d, bw = %d, class %d",
				  asv->colormap, parent, width, height, x, y, border_width,
				  wclass );
	if( attributes == NULL )
	{
		attributes = &my_attr ;
		memset( attributes, 0x00, sizeof(XSetWindowAttributes));
		mask = 0;
	}

	if( width < 1 )
		width = 1 ;
	if( height < 1 )
		height = 1 ;

	if( wclass == InputOnly )
	{
		border_width = 0 ;
		if( (mask&INPUTONLY_LEGAL_MASK) != mask )
			show_warning( " software BUG detected : illegal InputOnly window's mask 0x%lX - overriding", mask );
		mask &= INPUTONLY_LEGAL_MASK ;
	}else
	{
		depth = asv->visual_info.depth ;
		if( !get_flags(mask, CWColormap ) )
		{
			attributes->colormap = asv->colormap ;
			set_flags(mask, CWColormap );
		}
		if( !get_flags(mask, CWBorderPixmap ) )
		{
			attributes->border_pixmap = None ;
			set_flags(mask, CWBorderPixmap );
		}

		clear_flags(mask, CWBorderPixmap );
		if( !get_flags(mask, CWBorderPixel ) )
		{
			attributes->border_pixel = asv->black_pixel ;
			set_flags(mask, CWBorderPixel );
		}
		/* If the parent window and the new window have different bit
		** depths (such as on a Solaris box with 8bpp root window and
		** 24bpp child windows), ParentRelative will not work. */
		if ( get_flags(mask, CWBackPixmap) && attributes->background_pixmap == ParentRelative &&
			 asv->visual_info.visual != DefaultVisual( asv->dpy, DefaultScreen(asv->dpy) ))
		{
			clear_flags(mask, CWBackPixmap);
		}
	}
	LOCAL_DEBUG_OUT( "parent = %lX, mask = 0x%lX, VisualID = 0x%lX, Border Pixel = %ld, colormap = %lX",
					  parent, mask, asv->visual_info.visual->visualid, attributes->border_pixel, attributes->colormap );
	return XCreateWindow (asv->dpy, parent, x, y, width, height, border_width, depth,
						  wclass, asv->visual_info.visual,
	                      mask, attributes);
#else
	return None ;
#endif /*ifndef X_DISPLAY_MISSING */

}


GC
create_visual_gc( ASVisual *asv, Window root, unsigned long mask, XGCValues *gcvalues )
{
   	GC gc = NULL ;

#ifndef X_DISPLAY_MISSING
	if( asv )
	{
		XGCValues scratch_gcv ;
		if( asv->scratch_window == None )
			asv->scratch_window = create_visual_window( asv, root, -20, -20, 10, 10, 0, InputOutput, 0, NULL );
		if( asv->scratch_window != None )
			gc = XCreateGC( asv->dpy, asv->scratch_window, gcvalues?mask:0, gcvalues?gcvalues:&scratch_gcv );
	}
#endif
	return gc;
}

Pixmap
create_visual_pixmap( ASVisual *asv, Window root, unsigned int width, unsigned int height, unsigned int depth )
{
#ifndef X_DISPLAY_MISSING
	Pixmap p = None ;
	if( asv != NULL )
	{
		if( root == None )
			root = RootWindow(asv->dpy,DefaultScreen(asv->dpy));
		if( depth==0 )
			depth = asv->true_depth ;
		p = XCreatePixmap( asv->dpy, root, MAX(width,(unsigned)1), MAX(height,(unsigned)1), depth );
	}
	return p;
#else
	return None ;
#endif /*ifndef X_DISPLAY_MISSING */
}

void
destroy_visual_pixmap( ASVisual *asv, Pixmap *ppmap )
{
	if( asv && ppmap )
		if( *ppmap )
		{
#ifndef X_DISPLAY_MISSING
			XFreePixmap( asv->dpy, *ppmap );
			*ppmap = None ;
#endif
		}
}

#ifndef X_DISPLAY_MISSING
static int
quiet_xerror_handler (Display * dpy, XErrorEvent * error)
{
    return 0;
}

#endif

int
get_dpy_drawable_size (Display *drawable_dpy, Drawable d, unsigned int *ret_w, unsigned int *ret_h)
{
	int result = 0 ;
#ifndef X_DISPLAY_MISSING
	if( d != None && drawable_dpy != NULL )
	{
		Window        root;
		unsigned int  ujunk;
		int           junk;
		int           (*oldXErrorHandler) (Display *, XErrorEvent *) = XSetErrorHandler (quiet_xerror_handler);
		result = XGetGeometry (drawable_dpy, d, &root, &junk, &junk, ret_w, ret_h, &ujunk, &ujunk);
		XSetErrorHandler (oldXErrorHandler);
	}
#endif
	if ( result == 0)
	{
		*ret_w = 0;
		*ret_h = 0;
		return 0;
	}
	return 1;
}

Bool
get_dpy_window_position (Display *window_dpy, Window root, Window w, int *px, int *py, int *transparency_x, int *transparency_y)
{
	Bool result = False ;
	int x = 0, y = 0, transp_x = 0, transp_y = 0 ;
#ifndef X_DISPLAY_MISSING
	if( window_dpy != NULL && w != None )
	{
		Window wdumm;
		int rootHeight = XDisplayHeight(window_dpy, DefaultScreen(window_dpy) );
		int rootWidth = XDisplayWidth(window_dpy, DefaultScreen(window_dpy) );

		if( root == None )
			root = RootWindow(window_dpy,DefaultScreen(window_dpy));

		result = XTranslateCoordinates (window_dpy, w, root, 0, 0, &x, &y, &wdumm);
		if( result )
		{
			/* taking in to consideration virtual desktopping */
			result = (x < rootWidth && y < rootHeight );
			if( result )
			{
				unsigned int width = 0, height = 0;
				get_dpy_drawable_size (window_dpy, w, &width, &height);
				result = (x + width > 0 && y+height > 0) ;
			}

			for( transp_x = x ; transp_x < 0 ; transp_x += rootWidth );
			for( transp_y = y ; transp_y < 0 ; transp_y += rootHeight );
			while( transp_x > rootWidth ) transp_x -= rootWidth ;
			while( transp_y > rootHeight ) transp_y -= rootHeight ;
		}
	}
#endif
	if( px )
		*px = x;
	if( py )
		*py = y;
	if( transparency_x )
		*transparency_x = transp_x ;
	if( transparency_y )
		*transparency_y = transp_y ;
	return result;
}


#ifndef X_DISPLAY_MISSING
static unsigned char *scratch_ximage_data = NULL ;
static int scratch_use_count = 0 ;
static size_t scratch_ximage_allocated_size = 0;
#endif
static size_t scratch_ximage_max_size = ASSHM_SAVED_MAX*2;  /* maximum of 512 KBytes is default  */
static size_t scratch_ximage_normal_size = ASSHM_SAVED_MAX;  /* normal usage of scratch pool is 256 KBytes is default  */

int
set_scratch_ximage_max_size( int new_max_size )
{
	int tmp = scratch_ximage_max_size ;
	scratch_ximage_max_size = new_max_size ;
	return tmp;
}

int
set_scratch_ximage_normal_size( int new_normal_size )
{
	int tmp = scratch_ximage_normal_size ;
	scratch_ximage_normal_size = new_normal_size ;
	return tmp;
}

#ifndef X_DISPLAY_MISSING
static void*
get_scratch_data(size_t size)
{
	if( scratch_ximage_max_size < size || scratch_use_count > 0)
		return NULL;
	if( scratch_ximage_allocated_size < size )
	{
		scratch_ximage_allocated_size = size ;
		scratch_ximage_data = realloc( scratch_ximage_data, size );
	}

	++scratch_use_count;
	return scratch_ximage_data ;
}

static Bool
release_scratch_data( void *data )
{
	if( scratch_use_count == 0 || data != scratch_ximage_data )
		return False;
	--scratch_use_count ;
	if( scratch_use_count == 0 )
	{
		/* want to deallocate if too much is allocated ? */

	}
	return True;
}
#endif

#ifdef XSHMIMAGE

int	(*orig_XShmImage_destroy_image)(XImage *ximage) = NULL ;

typedef struct ASXShmImage
{
	XImage 			*ximage ;
	XShmSegmentInfo *segment ;
	int 			 ref_count ;
	Bool			 wait_completion_event ;
	unsigned int 	 size ;
  ASVisual *asv ;
}ASXShmImage;

typedef struct ASShmArea
{
	unsigned int 	 size ;
	char *shmaddr ;
	int shmid ;
	struct ASShmArea *next, *prev ;
}ASShmArea;

static ASHashTable	*xshmimage_segments = NULL ;
static ASHashTable	*xshmimage_images = NULL ;
/* attempt to reuse 256 Kb of shmem - no reason to reuse more than that,
 * since most XImages will be in range of 20K-100K */
static ASShmArea  *shm_available_mem_head = NULL ;
static int shm_available_mem_used = 0 ;

static Bool _as_use_shm_images = False ;

void really_destroy_shm_area( char *shmaddr, int shmid )
{
	shmdt (shmaddr);
	shmctl (shmid, IPC_RMID, 0);
	LOCAL_DEBUG_OUT("XSHMIMAGE> DESTROY_SHM : freeing shmid = %d, remaining in cache = %d bytes ", shmid, shm_available_mem_used );
}

void remove_shm_area( ASShmArea *area, Bool free_resources )
{
	if( area )
	{
		if( area == shm_available_mem_head )
			shm_available_mem_head = area->next ;
		if( area->next )
			area->next->prev = area->prev ;
		if( area->prev )
			area->prev->next = area->next ;
		shm_available_mem_used -= area->size ;
		if( free_resources )
			really_destroy_shm_area( area->shmaddr, area->shmid );
		else
		{
			LOCAL_DEBUG_OUT("XSHMIMAGE> REMOVE_SHM : reusing shmid = %d, size %d, remaining in cache = %d bytes ", area->shmid, area->size, shm_available_mem_used );
		}
		free( area );
	}

}

void flush_shm_cache( )
{
	if( xshmimage_images )
		destroy_ashash( &xshmimage_images );
	if( xshmimage_segments )
		destroy_ashash( &xshmimage_segments );
	while( shm_available_mem_head != NULL )
		remove_shm_area( shm_available_mem_head, True );
}

void save_shm_area( char *shmaddr, int shmid, int size )
{
	ASShmArea *area;

	if( shm_available_mem_used+size >= ASSHM_SAVED_MAX )
	{
	  	really_destroy_shm_area( shmaddr, shmid );
		return ;
	}

	shm_available_mem_used+=size ;
	area = safecalloc( 1, sizeof(ASShmArea) );

	area->shmaddr = shmaddr ;
	area->shmid = shmid ;
	area->size = size ;
	LOCAL_DEBUG_OUT("XSHMIMAGE> SAVE_SHM : saving shmid = %d, size %d, remaining in cache = %d bytes ", area->shmid, area->size, shm_available_mem_used );

	area->next = shm_available_mem_head ;
	if( shm_available_mem_head )
		shm_available_mem_head->prev = area ;
	shm_available_mem_head = area ;
}

char *get_shm_area( int size, int *shmid )
{
	ASShmArea *selected = NULL, *curr = shm_available_mem_head;

	while( curr != NULL )
	{
		if( curr->size >= size && curr->size < (size * 4)/3 )
		{
			if( selected == NULL )
				selected = curr ;
			else if( selected->size > curr->size )
				selected = curr ;
		}
		curr = curr->next ;
	}
	if( selected != NULL )
	{
		char *tmp = selected->shmaddr ;
		*shmid = selected->shmid ;
		remove_shm_area( selected, False );
		return tmp ;
	}

	*shmid = shmget (IPC_PRIVATE, size, IPC_CREAT|0666);
	return shmat (*shmid, 0, 0);
}

void
destroy_xshmimage_segment(ASHashableValue value, void *data)
{
	ASXShmImage *img_data = (ASXShmImage*)data ;
	if( img_data->segment != NULL )
	{
		LOCAL_DEBUG_OUT( "XSHMIMAGE> FREE_SEG : img_data = %p : segent to be freed: shminfo = %p ", img_data, img_data->segment );
		XShmDetach (img_data->asv->dpy, img_data->segment);
		save_shm_area( img_data->segment->shmaddr, img_data->segment->shmid, img_data->size );
		free( img_data->segment );
		img_data->segment = NULL ;
		if( img_data->ximage == NULL )
			free( img_data );
	}else
	{
		LOCAL_DEBUG_OUT( "XSHMIMAGE> FREE_SEG : img_data = %p : segment data is NULL already value = %ld!!", img_data, value );
	}
}

Bool destroy_xshm_segment( ShmSeg shmseg )
{
	if( xshmimage_segments )
	{
		if(remove_hash_item( xshmimage_segments, AS_HASHABLE(shmseg), NULL, True ) == ASH_Success)
		{
			LOCAL_DEBUG_OUT( "XSHMIMAGE> REMOVE_SEG : segment %ld removed from the hash successfully!", shmseg );
			return True ;
		}
		LOCAL_DEBUG_OUT( "XSHMIMAGE> ERROR : could not find segment %ld(0x%lX) in the hash!", shmseg, shmseg );
	}else
	{
		LOCAL_DEBUG_OUT( "XSHMIMAGE> ERROR : segments hash is %p!!", xshmimage_segments );
	}

	return False ;
}


void
destroy_xshmimage_image(ASHashableValue value, void *data)
{
	ASXShmImage *img_data = (ASXShmImage*)data ;
	if( img_data->ximage != NULL )
	{
		if( orig_XShmImage_destroy_image )
			orig_XShmImage_destroy_image( img_data->ximage );
		else
			XFree ((char *)img_data->ximage);
		LOCAL_DEBUG_OUT( "XSHMIMAGE> FREE_XIM : ximage freed: img_data = %p, xim = %p", img_data, img_data->ximage);
		img_data->ximage = NULL ;
		if( img_data->segment != NULL && !img_data->wait_completion_event )
		{
			if( destroy_xshm_segment( img_data->segment->shmseg ) )
				return ;
			img_data->segment = NULL ;
		}
		if( img_data->segment == NULL )
			free( img_data );
	}
}

Bool enable_shmem_images_for_visual (ASVisual *asv)
{
#ifndef DEBUG_ALLOCS
	if( asv && asv->dpy && XShmQueryExtension (asv->dpy) )
	{
		_as_use_shm_images = True ;
		if( xshmimage_segments == NULL )
			xshmimage_segments = create_ashash( 0, NULL, NULL, destroy_xshmimage_segment );
		if( xshmimage_images == NULL )
			xshmimage_images = create_ashash( 0, pointer_hash_value, NULL, destroy_xshmimage_image );
	}else
#endif
		_as_use_shm_images = False ;
	return _as_use_shm_images;
}

Bool enable_shmem_images ()
{
	return enable_shmem_images_for_visual (get_default_asvisual());
}


void disable_shmem_images()
{
	_as_use_shm_images = False ;
}

Bool
check_shmem_images_enabled()
{
	return _as_use_shm_images ;
}


int destroy_xshm_image( XImage *ximage )
{
	if( xshmimage_images )
	{
		if( remove_hash_item( xshmimage_images, AS_HASHABLE(ximage), NULL, True ) != ASH_Success )
		{
			if (ximage->data != NULL)
				free ((char *)ximage->data);
			if (ximage->obdata != NULL)
				free ((char *)ximage->obdata);
			XFree ((char *)ximage);
			LOCAL_DEBUG_OUT( "XSHMIMAGE> FREE_XIM : ximage freed: xim = %p", ximage);
		}
	}
	return 1;
}

unsigned long
ximage2shmseg( XImage *xim )
{
	void *vptr = NULL ;
	if( get_hash_item( xshmimage_images, AS_HASHABLE(xim), &vptr ) == ASH_Success )
	{
		ASXShmImage *data = (ASXShmImage *)vptr ;
		if( data->segment )
			return data->segment->shmseg;
	}
	return 0;
}

void registerXShmImage( ASVisual *asv, XImage *ximage, XShmSegmentInfo* shminfo )
{
	ASXShmImage *data = safecalloc( 1, sizeof(ASXShmImage));
	LOCAL_DEBUG_OUT( "XSHMIMAGE> CREATE_XIM : img_data = %p : image created: xiom = %p, shminfo = %p, segment = %d, data = %p", data, ximage, shminfo, shminfo->shmid, ximage->data );
  data->asv = asv ;
	data->ximage = ximage ;
	data->segment = shminfo ;
	data->size = ximage->bytes_per_line * ximage->height ;

	orig_XShmImage_destroy_image = ximage->f.destroy_image ;
	ximage->f.destroy_image = destroy_xshm_image ;

	add_hash_item( xshmimage_images, AS_HASHABLE(ximage), data );
	add_hash_item( xshmimage_segments, AS_HASHABLE(shminfo->shmseg), data );
}

void *
check_XImage_shared( XImage *xim )
{
	ASXShmImage *img_data = NULL ;
	if( _as_use_shm_images )
	{
		ASHashData hdata ;
		if(get_hash_item( xshmimage_images, AS_HASHABLE(xim), &hdata.vptr ) != ASH_Success)
			img_data = NULL ;
		else
			img_data = hdata.vptr ;
	}
	return img_data ;
}

Bool ASPutXImage( ASVisual *asv, Drawable d, GC gc, XImage *xim,
                  int src_x, int src_y, int dest_x, int dest_y,
				  unsigned int width, unsigned int height )
{
	ASXShmImage *img_data = NULL ;
	if( xim == NULL || asv == NULL )
		return False ;

	if( ( img_data = check_XImage_shared( xim )) != NULL )
	{
/*		LOCAL_DEBUG_OUT( "XSHMIMAGE> PUT_XIM : using shared memory Put = %p", xim ); */
		if( XShmPutImage( asv->dpy, d, gc, xim, src_x, src_y, dest_x, dest_y,width, height, True ) )
		{
			img_data->wait_completion_event = True ;
			return True ;
		}
	}
/*	LOCAL_DEBUG_OUT( "XSHMIMAGE> PUT_XIM : using normal Put = %p", xim ); */
	return XPutImage( asv->dpy, d, gc, xim, src_x, src_y, dest_x, dest_y,width, height );
}

XImage *ASGetXImage( ASVisual *asv, Drawable d,
                  int x, int y, unsigned int width, unsigned int height,
				  unsigned long plane_mask )
{
	XImage *xim = NULL ;

	if( asv == NULL || d == None )
		return NULL ;
	if( _as_use_shm_images && width*height > 4000)
	{
		unsigned int depth ;
		Window        root;
		unsigned int  ujunk;
		int           junk;
		if(XGetGeometry (asv->dpy, d, &root, &junk, &junk, &ujunk, &ujunk, &ujunk, &depth) == 0)
			return NULL ;

		xim = create_visual_ximage(asv,width,height,depth);
		XShmGetImage( asv->dpy, d, xim, x, y, plane_mask );

	}else
		xim = XGetImage( asv->dpy, d, x, y, width, height, plane_mask, ZPixmap );
	return xim ;
}
#else

Bool enable_shmem_images (){return False; }
void disable_shmem_images(){}
void *check_XImage_shared( XImage *xim ) {return NULL ; }

Bool ASPutXImage( ASVisual *asv, Drawable d, GC gc, XImage *xim,
                  int src_x, int src_y, int dest_x, int dest_y,
				  unsigned int width, unsigned int height )
{
#ifndef X_DISPLAY_MISSING
	if( xim == NULL || asv == NULL )
		return False ;
	return XPutImage( asv->dpy, d, gc, xim, src_x, src_y, dest_x, dest_y,width, height );
#else
	return False;
#endif
}

XImage * ASGetXImage( ASVisual *asv, Drawable d,
                  int x, int y, unsigned int width, unsigned int height,
				  unsigned long plane_mask )
{
#ifndef X_DISPLAY_MISSING
	if( asv == NULL || d == None )
		return NULL ;
	return XGetImage( asv->dpy, d, x, y, width, height, plane_mask, ZPixmap );
#else
	return NULL ;
#endif
}

#endif                                         /* XSHMIMAGE */

#ifndef X_DISPLAY_MISSING
int
My_XDestroyImage (XImage *ximage)
{
	if( !release_scratch_data(ximage->data) )
		if (ximage->data != NULL)
			free (ximage->data);
	if (ximage->obdata != NULL)
		free (ximage->obdata);
	XFree (ximage);
	return 1;
}
#endif /*ifndef X_DISPLAY_MISSING */


XImage*
create_visual_ximage( ASVisual *asv, unsigned int width, unsigned int height, unsigned int depth )
{
#ifndef X_DISPLAY_MISSING
	register XImage *ximage = NULL;
	unsigned long dsize;
	char         *data;
	int unit ;

	if( asv == NULL )
		return NULL;

	if( depth == 0 )
		unit = (asv->true_depth+7)&0x0038;
	else
		unit = (depth+7)&0x0038;
	if( unit == 24 )
		unit = 32 ;
#ifdef XSHMIMAGE
	if( _as_use_shm_images && width*height > 4000 )
	{
		XShmSegmentInfo *shminfo = safecalloc( 1, sizeof(XShmSegmentInfo));

		ximage = XShmCreateImage (asv->dpy, asv->visual_info.visual,
			                      (depth==0)?asv->visual_info.depth/*true_depth*/:depth,
								  ZPixmap, NULL, shminfo,
								  MAX(width,(unsigned int)1), MAX(height,(unsigned int)1));
		if( ximage == NULL )
			free( shminfo );
		else
		{
			shminfo->shmaddr = ximage->data = get_shm_area( ximage->bytes_per_line * ximage->height, &(shminfo->shmid) );
			if( shminfo->shmid == -1 )
			{
				static int shmem_failure_count = 0 ;
			    show_warning( "unable to allocate %d bytes of shared image memory", ximage->bytes_per_line * ximage->height ) ;
				if( ximage->bytes_per_line * ximage->height < 100000 || ++shmem_failure_count > 10 )
				{
					show_error( "too many shared memory failures - disabling" ) ;
					_as_use_shm_images = False ;
				}
				free( shminfo );
				shminfo = NULL ;
				XFree( ximage );
				ximage = NULL ;
			}else
			{
				shminfo->readOnly = False;
				XShmAttach (asv->dpy, shminfo);
				registerXShmImage( asv, ximage, shminfo );
			}
		}
	}
#endif
	if( ximage == NULL )
	{
		ximage = XCreateImage (asv->dpy, asv->visual_info.visual, (depth==0)?asv->visual_info.depth/*true_depth*/:depth, ZPixmap, 0, NULL, MAX(width,(unsigned int)1), MAX(height,(unsigned int)1),
						   	unit, 0);
		if (ximage != NULL)
		{
			_XInitImageFuncPtrs (ximage);
			ximage->obdata = NULL;
			ximage->f.destroy_image = My_XDestroyImage;
			dsize = ximage->bytes_per_line*ximage->height;
	    	if (((data = (char *)safemalloc (dsize)) == NULL) && (dsize > 0))
			{
				XFree ((char *)ximage);
				return (XImage *) NULL;
			}
			ximage->data = data;
		}
	}
	return ximage;
#else
	return NULL ;
#endif /*ifndef X_DISPLAY_MISSING */
}
/* this is the vehicle to use static allocated buffer for temporary XImages
 * in order to reduce XImage meory allocation overhead */
XImage*
create_visual_scratch_ximage( ASVisual *asv, unsigned int width, unsigned int height, unsigned int depth )
{
#ifndef X_DISPLAY_MISSING
	register XImage *ximage = NULL;
	char         *data;
	int unit ;

	if( asv == NULL )
		return NULL;

	if( depth == 0 )
		unit = (asv->true_depth+7)&0x0038;
	else
		unit = (depth+7)&0x0038;
	if( unit == 24 )
		unit = 32 ;

	/* for shared memory XImage we already do caching - no need for scratch ximage */
#ifdef XSHMIMAGE
	if( _as_use_shm_images )
		return create_visual_ximage( asv, width, height, depth );
#endif

	if( ximage == NULL )
	{
		ximage = XCreateImage (asv->dpy, asv->visual_info.visual,
			                   (depth==0)?asv->visual_info.depth/*true_depth*/:depth, ZPixmap,
							   0, NULL, MAX(width,(unsigned int)1), MAX(height,(unsigned int)1),
						   	   unit, 0);
		if (ximage != NULL)
		{
			data = get_scratch_data(ximage->bytes_per_line * ximage->height);
			if( data == NULL )
			{
				XFree ((char *)ximage);
				return create_visual_ximage( asv, width, height, depth );/* fall back */
			}
			_XInitImageFuncPtrs (ximage);
			ximage->obdata = NULL;
			ximage->f.destroy_image = My_XDestroyImage;
			ximage->data = data;
		}
	}
	return ximage;
#else
	return NULL ;
#endif /*ifndef X_DISPLAY_MISSING */
}
