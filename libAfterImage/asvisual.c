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
static int  get_shifts (unsigned long mask);
static int  get_bits (unsigned long mask);

void _XInitImageFuncPtrs(XImage*);

int
asvisual_empty_XErrorHandler (Display * dpy, XErrorEvent * event)
{
    return 0;
}
/***************************************************************************/
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
Status
debug_AllocColor( const char *file, int line, ASVisual *asv, Colormap cmap, XColor *pxcol )
{
	Status sret ;
	sret = XAllocColor( asv->dpy, cmap, pxcol );
	show_progress( " XAllocColor in %s:%d has %s -> cmap = %lX, pixel = %lu(%8.8lX), color = 0x%4.4X, 0x%4.4X, 0x%4.4X",
				   file, line, (sret==0)?"failed":"succeeded", (long)cmap, (unsigned long)(pxcol->pixel), (unsigned long)(pxcol->pixel), pxcol->red, pxcol->green, pxcol->blue );
	return sret;
}
#define ASV_ALLOC_COLOR(asv,cmap,pxcol)  debug_AllocColor(__FILE__, __LINE__, (asv),(cmap),(pxcol))
#else
#define ASV_ALLOC_COLOR(asv,cmap,pxcol)  XAllocColor((asv)->dpy,(cmap),(pxcol))
#endif

#else
#define ASV_ALLOC_COLOR(asv,cmap,pxcol)   0
#endif   /* ndef X_DISPLAY_MISSING */

/**********************************************************************/
/* returns the maximum number of true colors between a and b          */
long
ARGB32_manhattan_distance (long a, long b)
{
	register int d = (int)ARGB32_RED8(a)   - (int)ARGB32_RED8(b);
	register int t = (d < 0 )? -d : d ;

	d = (int)ARGB32_GREEN8(a) - (int)ARGB32_GREEN8(b);
	t += (d < 0)? -d : d ;
	d = (int)ARGB32_BLUE8(a)  - (int)ARGB32_BLUE8(b);
	return (t+((d < 0)? -d : d)) ;
}



/***************************************************************************
 * ASVisual :
 * encoding/decoding/querying/setup
 ***************************************************************************/
int get_bits_per_pixel(Display *dpy, int depth)
{
	if (depth <= 4)
	    return 4;
	if (depth <= 8)
	    return 8;
	if (depth <= 16)
	    return 16;
	return 32;
 }

/* ********************* ASVisual ************************************/
ASVisual *_set_default_asvisual( ASVisual *new_v );

#ifndef X_DISPLAY_MISSING
static XColor black_xcol = { 0, 0x0000, 0x0000, 0x0000, DoRed|DoGreen|DoBlue };
static XColor white_xcol = { 0, 0xFFFF, 0xFFFF, 0xFFFF, DoRed|DoGreen|DoBlue };

static void find_useable_visual( ASVisual *asv, Display *dpy, int screen,
	                             Window root, XVisualInfo *list, int nitems,
								 XSetWindowAttributes *attr )
{
	int k ;
	int (*oldXErrorHandler) (Display *, XErrorEvent *) =
						XSetErrorHandler (asvisual_empty_XErrorHandler);
	Colormap orig_cmap = attr->colormap ;

	for( k = 0  ; k < nitems ; k++ )
	{
		Window       w = None, wjunk;
		unsigned int width, height, ujunk ;
		int          junk;
		/* try and use default colormap when possible : */
		if( orig_cmap == None )
		{
  			if( list[k].visual == DefaultVisual( dpy, (screen) ) )
			{
				attr->colormap = DefaultColormap( dpy, screen );
				LOCAL_DEBUG_OUT( "Using Default colormap %lX", attr->colormap );
			}else
			{
				attr->colormap = XCreateColormap( dpy, root, list[k].visual, AllocNone);
				LOCAL_DEBUG_OUT( "DefaultVisual is %p, while ours is %p, so Created new colormap %lX", DefaultVisual( dpy, (screen) ), list[k].visual, attr->colormap );
			}
		}
		ASV_ALLOC_COLOR( asv, attr->colormap, &black_xcol );
		ASV_ALLOC_COLOR( asv, attr->colormap, &white_xcol );
		attr->border_pixel = black_xcol.pixel ;

/*fprintf( stderr, "checking out visual ID %d, class %d, depth = %d mask = %X,%X,%X\n", list[k].visualid, list[k].class, list[k].depth, list[k].red_mask, list[k].green_mask, list[k].blue_mask 	);*/
		w = XCreateWindow (dpy, root, -10, -10, 10, 10, 0, list[k].depth, CopyFromParent, list[k].visual, CWColormap|CWBorderPixel, attr );
		if( w != None && XGetGeometry (dpy, w, &wjunk, &junk, &junk, &width, &height, &ujunk, &ujunk))
		{
			/* don't really care what's in it since we do not use it anyways : */
			asv->visual_info = list[k] ;
			XDestroyWindow( dpy, w );
			asv->colormap = attr->colormap ;
			asv->own_colormap = (attr->colormap != DefaultColormap( dpy, screen ));
			asv->white_pixel = white_xcol.pixel ;
			asv->black_pixel = black_xcol.pixel ;
			break;
		}
		if( orig_cmap == None )
		{
			if( attr->colormap != DefaultColormap( dpy, screen ))
				XFreeColormap( dpy, attr->colormap );
			attr->colormap = None ;
		}
	}
	XSetErrorHandler(oldXErrorHandler);
}
#endif

/* Main procedure finding and querying the best visual */
Bool
query_screen_visual_id( ASVisual *asv, Display *dpy, int screen, Window root, int default_depth, VisualID visual_id, Colormap cmap )
{
#ifndef X_DISPLAY_MISSING
	int nitems = 0 ;
	/* first  - attempt locating 24bpp TrueColor or DirectColor RGB or BGR visuals as the best cases : */
	/* second - attempt locating 32bpp TrueColor or DirectColor RGB or BGR visuals as the next best cases : */
	/* third  - lesser but still capable 16bpp 565 RGB or BGR modes : */
	/* forth  - even more lesser 15bpp 555 RGB or BGR modes : */
	/* nothing nice has been found - use whatever X has to offer us as a default :( */
	int i ;

	XVisualInfo *list = NULL;
	XSetWindowAttributes attr ;
	static XVisualInfo templates[] =
		/* Visual, visualid, screen, depth, class      , red_mask, green_mask, blue_mask, colormap_size, bits_per_rgb */
		{{ NULL  , 0       , 0     , 24   , TrueColor  , 0xFF0000, 0x00FF00  , 0x0000FF , 0            , 0 },
		 { NULL  , 0       , 0     , 24   , TrueColor  , 0x0000FF, 0x00FF00  , 0xFF0000 , 0            , 0 },
		 { NULL  , 0       , 0     , 24   , TrueColor  , 0x0     , 0x0       , 0x0      , 0            , 0 },
		 { NULL  , 0       , 0     , 32   , TrueColor  , 0xFF0000, 0x00FF00  , 0x0000FF , 0            , 0 },
		 { NULL  , 0       , 0     , 32   , TrueColor  , 0x0000FF, 0x00FF00  , 0xFF0000 , 0            , 0 },
		 { NULL  , 0       , 0     , 32   , TrueColor  , 0x0     , 0x0       , 0x0      , 0            , 0 },
		 { NULL  , 0       , 0     , 16   , TrueColor  , 0xF800  , 0x07E0    , 0x001F   , 0            , 0 },
		 { NULL  , 0       , 0     , 16   , TrueColor  , 0x001F  , 0x07E0    , 0xF800   , 0            , 0 },
		 /* big endian or MBR_First modes : */
		 { NULL  , 0       , 0     , 16   , TrueColor  , 0x0     , 0xE007    , 0x0      , 0            , 0 },
		 /* some misrepresented modes that really are 15bpp : */
		 { NULL  , 0       , 0     , 16   , TrueColor  , 0x7C00  , 0x03E0    , 0x001F   , 0            , 0 },
		 { NULL  , 0       , 0     , 16   , TrueColor  , 0x001F  , 0x03E0    , 0x7C00   , 0            , 0 },
		 { NULL  , 0       , 0     , 16   , TrueColor  , 0x0     , 0xE003    , 0x0      , 0            , 0 },
		 { NULL  , 0       , 0     , 15   , TrueColor  , 0x7C00  , 0x03E0    , 0x001F   , 0            , 0 },
		 { NULL  , 0       , 0     , 15   , TrueColor  , 0x001F  , 0x03E0    , 0x7C00   , 0            , 0 },
		 { NULL  , 0       , 0     , 15   , TrueColor  , 0x0     , 0xE003    , 0x0      , 0            , 0 },
/* no suitable TrueColorMode found - now do the same thing to DirectColor :*/
		 { NULL  , 0       , 0     , 24   , DirectColor, 0xFF0000, 0x00FF00  , 0x0000FF , 0            , 0 },
		 { NULL  , 0       , 0     , 24   , DirectColor, 0x0000FF, 0x00FF00  , 0xFF0000 , 0            , 0 },
		 { NULL  , 0       , 0     , 24   , DirectColor, 0x0     , 0x0       , 0x0      , 0            , 0 },
		 { NULL  , 0       , 0     , 32   , DirectColor, 0xFF0000, 0x00FF00  , 0x0000FF , 0            , 0 },
		 { NULL  , 0       , 0     , 32   , DirectColor, 0x0000FF, 0x00FF00  , 0xFF0000 , 0            , 0 },
		 { NULL  , 0       , 0     , 32   , DirectColor, 0x0     , 0x0       , 0x0      , 0            , 0 },
		 { NULL  , 0       , 0     , 16   , DirectColor, 0xF800  , 0x07E0    , 0x001F   , 0            , 0 },
		 { NULL  , 0       , 0     , 16   , DirectColor, 0x001F  , 0x07E0    , 0xF800   , 0            , 0 },
		 { NULL  , 0       , 0     , 16   , DirectColor, 0x0     , 0xE007    , 0x0      , 0            , 0 },
		 { NULL  , 0       , 0     , 16   , DirectColor, 0x7C00  , 0x03E0    , 0x001F   , 0            , 0 },
		 { NULL  , 0       , 0     , 16   , DirectColor, 0x001F  , 0x03E0    , 0x7C00   , 0            , 0 },
		 { NULL  , 0       , 0     , 16   , DirectColor, 0x0     , 0xE003    , 0x0      , 0            , 0 },
		 { NULL  , 0       , 0     , 15   , DirectColor, 0x7C00  , 0x03E0    , 0x001F   , 0            , 0 },
		 { NULL  , 0       , 0     , 15   , DirectColor, 0x001F  , 0x03E0    , 0x7C00   , 0            , 0 },
		 { NULL  , 0       , 0     , 15   , DirectColor, 0x0     , 0xE003    , 0x0      , 0            , 0 },
		 { NULL  , 0       , 0     , 0    , 0          , 0       , 0         , 0        , 0            , 0 },
		} ;
#endif /*ifndef X_DISPLAY_MISSING */
	if( asv == NULL )
		return False ;
	memset( asv, 0x00, sizeof(ASVisual));

	asv->dpy = dpy ;

#ifndef X_DISPLAY_MISSING
	memset( &attr, 0x00, sizeof( attr ));
	attr.colormap = cmap ;

	if( visual_id == 0 )
	{
		for( i = 0 ; templates[i].depth != 0 ; i++ )
		{
			int mask = VisualScreenMask|VisualDepthMask|VisualClassMask ;

			templates[i].screen = screen ;
			if( templates[i].red_mask != 0 )
				mask |= VisualRedMaskMask;
			if( templates[i].green_mask != 0 )
				mask |= VisualGreenMaskMask ;
			if( templates[i].blue_mask != 0 )
				mask |= VisualBlueMaskMask ;

			if( (list = XGetVisualInfo( dpy, mask, &(templates[i]), &nitems ))!= NULL )
			{
				find_useable_visual( asv, dpy, screen, root, list, nitems, &attr );
				XFree( list );
				list = NULL ;
				if( asv->visual_info.visual != NULL )
					break;
			}
		}
	}else
	{
		templates[0].visualid = visual_id ;
		if( (list = XGetVisualInfo( dpy, VisualIDMask, &(templates[0]), &nitems )) != NULL )
		{
			find_useable_visual( asv, dpy, screen, root, list, nitems, &attr );
		 	XFree( list );
			list = NULL ;
		}
		if( asv->visual_info.visual == NULL )
			show_error( "Visual with requested ID of 0x%X is unusable - will try default instead.", visual_id );
	}

	if( asv->visual_info.visual == NULL )
	{  /* we ain't found any decent Visuals - that's some crappy screen you have! */
		register int vclass = 6 ;
		while( --vclass >= 0 )
			if( XMatchVisualInfo( dpy, screen, default_depth, vclass, &(asv->visual_info) ) )
				break;
		if( vclass < 0 )
			return False;
		/* try and use default colormap when possible : */
		if( asv->visual_info.visual == DefaultVisual( dpy, screen ) )
			attr.colormap = DefaultColormap( dpy, screen );
		else
			attr.colormap = XCreateColormap( dpy, root, asv->visual_info.visual, AllocNone);
		ASV_ALLOC_COLOR( asv, attr.colormap, &black_xcol );
		ASV_ALLOC_COLOR( asv, attr.colormap, &white_xcol );
		asv->colormap = attr.colormap ;
		asv->own_colormap = (attr.colormap != DefaultColormap( dpy, screen ));
		asv->white_pixel = white_xcol.pixel ;
		asv->black_pixel = black_xcol.pixel ;
	}
	if( get_output_threshold() >= OUTPUT_VERBOSE_THRESHOLD )
	{
		fprintf( stderr, "Selected visual 0x%lx: depth %d, class %d\n RGB masks: 0x%lX, 0x%lX, 0x%lX, Byte Ordering: %s\n",
				 (unsigned long)asv->visual_info.visualid,
				 asv->visual_info.depth,
				 asv->visual_info.class,
				 (unsigned long)asv->visual_info.red_mask,
				 (unsigned long)asv->visual_info.green_mask,
				 (unsigned long)asv->visual_info.blue_mask,
				 (ImageByteOrder(asv->dpy)==MSBFirst)?"MSBFirst":"LSBFirst" );
	}
#else
	asv->white_pixel = ARGB32_White ;
	asv->black_pixel = ARGB32_Black ;
#endif /*ifndef X_DISPLAY_MISSING */
	return True;
}

ASVisual *
create_asvisual_for_id( Display *dpy, int screen, int default_depth, VisualID visual_id, Colormap cmap, ASVisual *reusable_memory )
{
	ASVisual *asv = reusable_memory ;
#ifndef X_DISPLAY_MISSING
    Window root = dpy?RootWindow(dpy,screen):None;
#endif /*ifndef X_DISPLAY_MISSING */

	if( asv == NULL )
        asv = safecalloc( 1, sizeof(ASVisual) );
#ifndef X_DISPLAY_MISSING
    if( dpy )
    {
        if( query_screen_visual_id( asv, dpy, screen, root, default_depth, visual_id, cmap ) )
        {   /* found visual - now off to decide about color handling on it : */
            if( !setup_truecolor_visual( asv ) )
            {  /* well, we don't - lets try and preallocate as many colors as we can but up to
                * 1/4 of the colorspace or 12bpp colors, whichever is smaller */
                setup_pseudo_visual( asv );
                if( asv->as_colormap == NULL )
                    setup_as_colormap( asv );
            }
        }else
        {
            if( reusable_memory != asv )
                free( asv );
            asv = NULL ;
        }
    }
#endif /*ifndef X_DISPLAY_MISSING */
	_set_default_asvisual( asv );
	return asv;
}

ASVisual *
create_asvisual( Display *dpy, int screen, int default_depth, ASVisual *reusable_memory )
{
	VisualID visual_id = 0;
	char *id_env_var ;

	if( (id_env_var = getenv( ASVISUAL_ID_ENVVAR )) != NULL )
		visual_id = strtol(id_env_var,NULL,16);

	return create_asvisual_for_id( dpy, screen, default_depth, visual_id, None, reusable_memory );
}


void
destroy_asvisual( ASVisual *asv, Bool reusable )
{
	if( asv )
	{
		if( get_default_asvisual() == asv )
			_set_default_asvisual( NULL );
#ifndef X_DISPLAY_MISSING
	 	if( asv->own_colormap )
	 	{
	 		if( asv->colormap )
	 			XFreeColormap( asv->dpy, asv->colormap );
	 	}
	 	if( asv->as_colormap )
		{
	 		free( asv->as_colormap );
			if( asv->as_colormap_reverse.xref != NULL )
			{
				if( asv->as_colormap_type == ACM_12BPP )
					destroy_ashash( &(asv->as_colormap_reverse.hash) );
				else
					free( asv->as_colormap_reverse.xref );
			}
		}
#ifdef HAVE_GLX
		if( asv->glx_scratch_gc_direct )
			glXDestroyContext(asv->dpy, asv->glx_scratch_gc_direct );
		if( asv->glx_scratch_gc_indirect )
			glXDestroyContext(asv->dpy, asv->glx_scratch_gc_indirect );
#endif
		if( asv->scratch_window )
			XDestroyWindow( asv->dpy, asv->scratch_window );

#endif /*ifndef X_DISPLAY_MISSING */
		if( !reusable )
			free( asv );
	}
}

int as_colormap_type2size( int type )
{
	switch( type )
	{
		case ACM_3BPP :
			return 8 ;
		case ACM_6BPP :
			return 64 ;
		case ACM_12BPP :
			return 4096 ;
		default:
			return 0 ;
	}
}

Bool
visual2visual_prop( ASVisual *asv, size_t *size_ret,
								   unsigned long *version_ret,
								   unsigned long **data_ret )
{
	int cmap_size = 0 ;
	unsigned long *prop ;
	size_t size;

	if( asv == NULL || data_ret == NULL)
		return False;

	cmap_size = as_colormap_type2size( asv->as_colormap_type );

	if( cmap_size > 0 && asv->as_colormap == NULL )
		return False ;
	size = (1+1+2+1+cmap_size)*sizeof(unsigned long);
	prop = safemalloc( size ) ;
#ifndef X_DISPLAY_MISSING
	prop[0] = asv->visual_info.visualid ;
	prop[1] = asv->colormap ;
	prop[2] = asv->black_pixel ;
	prop[3] = asv->white_pixel ;
	prop[4] = asv->as_colormap_type ;
	if( cmap_size > 0 )
	{
		register int i;
		for( i = 0 ; i < cmap_size ; i++ )
			prop[i+5] = asv->as_colormap[i] ;
	}
	if( size_ret )
		*size_ret = size;
#endif /*ifndef X_DISPLAY_MISSING */
	if( version_ret )
		*version_ret = (1<<16)+0;                        /* version is 1.0 */
	*data_ret = prop ;
	return True;
}

Bool
visual_prop2visual( ASVisual *asv, Display *dpy, int screen,
								   size_t size,
								   unsigned long version,
								   unsigned long *data )
{
#ifndef X_DISPLAY_MISSING
	XVisualInfo templ, *list ;
	int nitems = 0 ;
	int cmap_size = 0 ;
#endif /*ifndef X_DISPLAY_MISSING */

	if( asv == NULL )
		return False;

	asv->dpy = dpy ;

	if( size < (1+1+2+1)*sizeof(unsigned long) ||
		(version&0x00FFFF) != 0 || (version>>16) != 1 || data == NULL )
		return False;

	if( data[0] == None || data[1] == None ) /* we MUST have valid colormap and visualID !!!*/
		return False;

#ifndef X_DISPLAY_MISSING
	templ.screen = screen ;
	templ.visualid = data[0] ;

	list = XGetVisualInfo( dpy, VisualScreenMask|VisualIDMask, &templ, &nitems );
	if( list == NULL || nitems == 0 )
		return False;   /* some very bad visual ID has been requested :( */

	asv->visual_info = *list ;
	XFree( list );

	if( asv->own_colormap && asv->colormap )
		XFreeColormap( dpy, asv->colormap );

	asv->colormap = data[1] ;
	asv->own_colormap = False ;
	asv->black_pixel = data[2] ;
	asv->white_pixel = data[3] ;
	asv->as_colormap_type = data[4];

	cmap_size = as_colormap_type2size( asv->as_colormap_type );

	if( cmap_size > 0 )
	{
		register int i ;
		if( asv->as_colormap )
			free( asv->as_colormap );
		asv->as_colormap = safemalloc( cmap_size );
		for( i = 0 ; i < cmap_size ; i++ )
			asv->as_colormap[i] = data[i+5];
	}else
		asv->as_colormap_type = ACM_None ;     /* just in case */
#else

#endif /*ifndef X_DISPLAY_MISSING */
	return True;
}

Bool
setup_truecolor_visual( ASVisual *asv )
{
#ifndef X_DISPLAY_MISSING
	XVisualInfo *vi = &(asv->visual_info) ;

	if( vi->class != TrueColor )
		return False;

#ifdef HAVE_GLX
	if( glXQueryExtension (asv->dpy, NULL, NULL))
	{
		int val = False;
		glXGetConfig(asv->dpy, vi, GLX_USE_GL, &val);
		if( val )
		{
			asv->glx_scratch_gc_indirect = glXCreateContext (asv->dpy, &(asv->visual_info), NULL, False);
			if( asv->glx_scratch_gc_indirect )
			{
				set_flags( asv->glx_support, ASGLX_Available );
				if( glXGetConfig(asv->dpy, vi, GLX_RGBA, &val) == 0 )
					if( val ) set_flags( asv->glx_support, ASGLX_RGBA );
				if( glXGetConfig(asv->dpy, vi, GLX_DOUBLEBUFFER, &val) == 0 )
					if( val ) set_flags( asv->glx_support, ASGLX_DoubleBuffer );
				if( glXGetConfig(asv->dpy, vi, GLX_DOUBLEBUFFER, &val) == 0 )
					if( val ) set_flags( asv->glx_support, ASGLX_DoubleBuffer );

				if( (asv->glx_scratch_gc_direct = glXCreateContext (asv->dpy, &(asv->visual_info), NULL, True)) != NULL )
					if( !glXIsDirect( asv->dpy, asv->glx_scratch_gc_direct ) )
					{
						glXDestroyContext(asv->dpy, asv->glx_scratch_gc_direct );
						asv->glx_scratch_gc_direct = NULL ;
					}
			}
		}
	}
#endif

	asv->BGR_mode = ((vi->red_mask&0x0010)!=0) ;
	asv->rshift = get_shifts (vi->red_mask);
	asv->gshift = get_shifts (vi->green_mask);
	asv->bshift = get_shifts (vi->blue_mask);
	asv->rbits = get_bits (vi->red_mask);
	asv->gbits = get_bits (vi->green_mask);
	asv->bbits = get_bits (vi->blue_mask);
	asv->true_depth = vi->depth ;
	asv->msb_first = (ImageByteOrder(asv->dpy)==MSBFirst);

	if( asv->true_depth == 16 && ((vi->red_mask|vi->blue_mask)&0x8000) == 0 )
		asv->true_depth = 15;
	/* setting up conversion handlers : */
	switch( asv->true_depth )
	{
		case 24 :
		case 32 :
			asv->color2pixel_func     = (asv->BGR_mode)?color2pixel32bgr:color2pixel32rgb ;
			asv->pixel2color_func     = (asv->BGR_mode)?pixel2color32bgr:pixel2color32rgb ;
			asv->ximage2scanline_func = ximage2scanline32 ;
			asv->scanline2ximage_func = scanline2ximage32 ;
		    break ;
/*		case 24 :
			scr->color2pixel_func     = (bgr_mode)?color2pixel24bgr:color2pixel24rgb ;
			scr->pixel2color_func     = (bgr_mode)?pixel2color24bgr:pixel2color24rgb ;
			scr->ximage2scanline_func = ximage2scanline24 ;
			scr->scanline2ximage_func = scanline2ximage24 ;
		    break ;
  */	case 16 :
			asv->color2pixel_func     = (asv->BGR_mode)?color2pixel16bgr:color2pixel16rgb ;
			asv->pixel2color_func     = (asv->BGR_mode)?pixel2color16bgr:pixel2color16rgb ;
			asv->ximage2scanline_func = ximage2scanline16 ;
			asv->scanline2ximage_func = scanline2ximage16 ;
		    break ;
		case 15 :
			asv->color2pixel_func     = (asv->BGR_mode)?color2pixel15bgr:color2pixel15rgb ;
			asv->pixel2color_func     = (asv->BGR_mode)?pixel2color15bgr:pixel2color15rgb ;
			asv->ximage2scanline_func = ximage2scanline15 ;
			asv->scanline2ximage_func = scanline2ximage15 ;
		    break ;
	}
#endif /*ifndef X_DISPLAY_MISSING */
	return (asv->ximage2scanline_func != NULL) ;
}

ARGB32 *
make_reverse_colormap( unsigned long *cmap, size_t size, int depth, unsigned short mask, unsigned short shift )
{
	unsigned int max_pixel = 0x01<<depth ;
	ARGB32 *rcmap = safecalloc( max_pixel, sizeof( ARGB32 ) );
	register int i ;

	for( i = 0 ; i < (int)size ; i++ )
		if( cmap[i] < max_pixel )
			rcmap[cmap[i]] = MAKE_ARGB32( 0xFF, (i>>(shift<<1))& mask, (i>>(shift))&mask, i&mask);
	return rcmap;
}

ASHashTable *
make_reverse_colorhash( unsigned long *cmap, size_t size, int depth, unsigned short mask, unsigned short shift )
{
	ASHashTable *hash = create_ashash( 0, NULL, NULL, NULL );
	register unsigned int i ;

	if( hash )
	{
		for( i = 0 ; i < size ; i++ )
			add_hash_item( hash, (ASHashableValue)cmap[i], (void*)((long)MAKE_ARGB32( 0xFF, (i>>(shift<<1))& mask, (i>>(shift))&mask, i&mask)) );
	}
	return hash;
}

void
setup_pseudo_visual( ASVisual *asv  )
{
#ifndef X_DISPLAY_MISSING
	XVisualInfo *vi = &(asv->visual_info) ;

	/* we need to allocate new usable list of colors based on available bpp */
	asv->true_depth = vi->depth ;
	if( asv->as_colormap == NULL )
	{
		if( asv->true_depth < 8 )
			asv->as_colormap_type = ACM_3BPP ;
		else if( asv->true_depth < 12 )
			asv->as_colormap_type = ACM_6BPP ;
		else
			asv->as_colormap_type = ACM_12BPP ;
	}
	/* then we need to set up hooks : */
	switch( asv->as_colormap_type )
	{
		case ACM_3BPP:
			asv->ximage2scanline_func = ximage2scanline_pseudo3bpp ;
			asv->scanline2ximage_func = scanline2ximage_pseudo3bpp ;
			asv->color2pixel_func = color2pixel_pseudo3bpp ;
		    break ;
		case ACM_6BPP:
			asv->ximage2scanline_func = ximage2scanline_pseudo6bpp ;
			asv->scanline2ximage_func = scanline2ximage_pseudo6bpp ;
			asv->color2pixel_func = color2pixel_pseudo6bpp ;
		    break ;
		default:
			asv->as_colormap_type = ACM_12BPP ;
		case ACM_12BPP:
			asv->ximage2scanline_func = ximage2scanline_pseudo12bpp ;
			asv->scanline2ximage_func = scanline2ximage_pseudo12bpp ;
			asv->color2pixel_func = color2pixel_pseudo12bpp ;
		    break ;
	}
	if( asv->as_colormap != NULL )
	{
		if( asv->as_colormap_type == ACM_3BPP || asv->as_colormap_type == ACM_6BPP )
		{
			unsigned short mask = 0x0003, shift = 2 ;
			if( asv->as_colormap_type==ACM_3BPP )
			{
				mask = 0x0001 ;
				shift = 1 ;
			}
			asv->as_colormap_reverse.xref = make_reverse_colormap( asv->as_colormap,
															  as_colormap_type2size( asv->as_colormap_type ),
															  asv->true_depth, mask, shift );
		}else if( asv->as_colormap_type == ACM_12BPP )
		{
			asv->as_colormap_reverse.hash = make_reverse_colorhash( asv->as_colormap,
															  as_colormap_type2size( asv->as_colormap_type ),
															  asv->true_depth, 0x000F, 4 );
		}
	}
#endif /*ifndef X_DISPLAY_MISSING */
}

#ifndef X_DISPLAY_MISSING
static unsigned long*
make_3bpp_colormap( ASVisual *asv )
{
	XColor colors_3bpp[8] =
	/* list of non-white, non-black colors in order of decreasing importance: */
	{   { 0, 0, 0xFFFF, 0, DoRed|DoGreen|DoBlue, 0},
		{ 0, 0xFFFF, 0, 0, DoRed|DoGreen|DoBlue, 0},
		{ 0, 0, 0, 0xFFFF, DoRed|DoGreen|DoBlue, 0},
	 	{ 0, 0xFFFF, 0xFFFF, 0, DoRed|DoGreen|DoBlue, 0},
	    { 0, 0, 0xFFFF, 0xFFFF, DoRed|DoGreen|DoBlue, 0},
	    { 0, 0xFFFF, 0, 0xFFFF, DoRed|DoGreen|DoBlue, 0}} ;
	unsigned long *cmap ;

	cmap = safemalloc( 8 * sizeof(unsigned long) );
	/* fail safe code - if any of the alloc fails - colormap entry will still have
	 * most suitable valid value ( black or white in 1bpp mode for example ) : */
	cmap[0] = cmap[1] = cmap[2] = cmap[3] = asv->black_pixel ;
	cmap[7] = cmap[6] = cmap[5] = cmap[4] = asv->white_pixel ;
	if( ASV_ALLOC_COLOR( asv, asv->colormap, &colors_3bpp[0] ))  /* pure green */
		cmap[0x02] = cmap[0x03] = cmap[0x06] = colors_3bpp[0].pixel ;
	if( ASV_ALLOC_COLOR( asv, asv->colormap, &colors_3bpp[1] ))  /* pure red */
		cmap[0x04] = cmap[0x05] = colors_3bpp[1].pixel ;
	if( ASV_ALLOC_COLOR( asv, asv->colormap, &colors_3bpp[2] ))  /* pure blue */
		cmap[0x01] = colors_3bpp[2].pixel ;
	if( ASV_ALLOC_COLOR( asv, asv->colormap, &colors_3bpp[3] ))  /* yellow */
		cmap[0x06] = colors_3bpp[3].pixel ;
	if( ASV_ALLOC_COLOR( asv, asv->colormap, &colors_3bpp[4] ))  /* cyan */
		cmap[0x03] = colors_3bpp[4].pixel ;
	if( ASV_ALLOC_COLOR( asv, asv->colormap, &colors_3bpp[5] ))  /* magenta */
		cmap[0x05] = colors_3bpp[5].pixel ;
	return cmap;
}

static unsigned long*
make_6bpp_colormap( ASVisual *asv, unsigned long *cmap_3bpp )
{
	unsigned short red, green, blue ;
	unsigned long *cmap = safemalloc( 0x0040*sizeof( unsigned long) );
	XColor xcol ;

	cmap[0] = asv->black_pixel ;

	xcol.flags = DoRed|DoGreen|DoBlue ;
	for( blue = 0 ; blue <= 0x0003 ; blue++ )
	{
		xcol.blue = (0xFFFF*blue)/3 ;
		for( red = 0 ; red <= 0x0003 ; red++ )
		{	                                /* red has highier priority then blue */
			xcol.red = (0xFFFF*red)/3 ;
/*			green = ( blue == 0 && red == 0 )?1:0 ; */
			for( green = 0 ; green <= 0x0003 ; green++ )
			{                                  /* green has highier priority then red */
				unsigned short index_3bpp = ((red&0x0002)<<1)|(green&0x0002)|((blue&0x0002)>>1);
				unsigned short index_6bpp = (red<<4)|(green<<2)|blue;
				xcol.green = (0xFFFF*green)/3 ;

				if( (red&0x0001) == ((red&0x0002)>>1) &&
					(green&0x0001) == ((green&0x0002)>>1) &&
					(blue&0x0001) == ((blue&0x0002)>>1) )
					cmap[index_6bpp] = cmap_3bpp[index_3bpp];
				else
				{
					if( ASV_ALLOC_COLOR( asv, asv->colormap, &xcol) != 0 )
						cmap[index_6bpp] = xcol.pixel ;
					else
						cmap[index_6bpp] = cmap_3bpp[index_3bpp] ;
				}
			}
		}
	}
	return cmap;
}

static unsigned long*
make_9bpp_colormap( ASVisual *asv, unsigned long *cmap_6bpp )
{
	unsigned long *cmap = safemalloc( 512*sizeof( unsigned long) );
	unsigned short red, green, blue ;
	XColor xcol ;

	cmap[0] = asv->black_pixel ;               /* just in case  */

	xcol.flags = DoRed|DoGreen|DoBlue ;
	for( blue = 0 ; blue <= 0x0007 ; blue++ )
	{
		xcol.blue = (0xFFFF*blue)/7 ;
		for( red = 0 ; red <= 0x0007 ; red++ )
		{	                                /* red has highier priority then blue */
			xcol.red = (0xFFFF*red)/7 ;
			for( green = 0 ; green <= 0x0007 ; green++ )
			{                                  /* green has highier priority then red */
				unsigned short index_6bpp = ((red&0x0006)<<3)|((green&0x0006)<<1)|((blue&0x0006)>>1);
				unsigned short index_9bpp = (red<<6)|(green<<3)|blue;
				xcol.green = (0xFFFF*green)/7 ;

				if( (red&0x0001) == ((red&0x0002)>>1) &&
					(green&0x0001) == ((green&0x0002)>>1) &&
					(blue&0x0001) == ((blue&0x0002)>>1) )
					cmap[index_9bpp] = cmap_6bpp[index_6bpp];
				else
				{
					if( ASV_ALLOC_COLOR( asv, asv->colormap, &xcol) != 0 )
						cmap[index_9bpp] = xcol.pixel ;
					else
						cmap[index_9bpp] = cmap_6bpp[index_6bpp] ;
				}
			}
		}
	}
	return cmap;
}

static unsigned long*
make_12bpp_colormap( ASVisual *asv, unsigned long *cmap_9bpp )
{
	unsigned long *cmap = safemalloc( 4096*sizeof( unsigned long) );
	unsigned short red, green, blue ;
	XColor xcol ;

	cmap[0] = asv->black_pixel ;               /* just in case  */

	xcol.flags = DoRed|DoGreen|DoBlue ;
	for( blue = 0 ; blue <= 0x000F ; blue++ )
	{
		xcol.blue = (0xFFFF*blue)/15 ;
		for( red = 0 ; red <= 0x000F ; red++ )
		{	                                /* red has highier priority then blue */
			xcol.red = (0xFFFF*red)/15 ;
			for( green = 0 ; green <= 0x000F ; green++ )
			{                                  /* green has highier priority then red */
				unsigned short index_9bpp = ((red&0x000E)<<5)|((green&0x000E)<<2)|((blue&0x000E)>>1);
				unsigned short index_12bpp = (red<<8)|(green<<4)|blue;
				xcol.green = (0xFFFF*green)/15 ;

				if( (red&0x0001) == ((red&0x0002)>>1) &&
					(green&0x0001) == ((green&0x0002)>>1) &&
					(blue&0x0001) == ((blue&0x0002)>>1) )
					cmap[index_12bpp] = cmap_9bpp[index_9bpp];
				else
				{
					if( ASV_ALLOC_COLOR( asv, asv->colormap, &xcol) != 0 )
						cmap[index_12bpp] = xcol.pixel ;
					else
						cmap[index_12bpp] = cmap_9bpp[index_9bpp] ;
				}
			}
		}
	}
	return cmap;
}
#endif /*ifndef X_DISPLAY_MISSING */

void
setup_as_colormap( ASVisual *asv )
{
#ifndef X_DISPLAY_MISSING
	unsigned long *cmap_lower, *cmap ;

	if( asv == NULL || asv->as_colormap != NULL )
		return ;

	cmap = make_3bpp_colormap( asv );
	if( asv->as_colormap_type == ACM_3BPP )
	{
		asv->as_colormap = cmap ;
		asv->as_colormap_reverse.xref = make_reverse_colormap( cmap, 8, asv->true_depth, 0x0001, 1 );
		return ;
	}
	cmap_lower = cmap ;
	cmap = make_6bpp_colormap( asv, cmap_lower );
	free( cmap_lower );
	if( asv->as_colormap_type == ACM_6BPP )
	{
		asv->as_colormap = cmap ;
		asv->as_colormap_reverse.xref = make_reverse_colormap( cmap, 64, asv->true_depth, 0x0003, 2 );
	}else
	{
		cmap_lower = cmap ;
		cmap = make_9bpp_colormap( asv, cmap_lower );
		free( cmap_lower );
		cmap_lower = cmap ;
		cmap = make_12bpp_colormap( asv, cmap_lower );
		free( cmap_lower );

		asv->as_colormap = cmap ;
		asv->as_colormap_reverse.hash = make_reverse_colorhash( cmap, 4096, asv->true_depth, 0x000F, 4 );
	}
#endif /*ifndef X_DISPLAY_MISSING */
}


/****************************************************************************/
/* Color manipulation functions :                                           */
/****************************************************************************/


/* misc function to calculate number of bits/shifts */
#ifndef X_DISPLAY_MISSING
static int
get_shifts (unsigned long mask)
{
	register int  i = 1;

	while (mask >> i)
		i++;

	return i - 1;							   /* can't be negative */
}

static int
get_bits (unsigned long mask)
{
	register int  i;

	for (i = 0; mask; mask >>= 1)
		if (mask & 1)
			i++;

	return i;								   /* can't be negative */
}
#endif

