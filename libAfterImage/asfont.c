/*
 * Copyright (c) 2001 Sasha Vasko <sasha at aftercode.net>
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
#undef LOCAL_DEBUG
#undef DO_CLOCKING

#ifdef _WIN32
#include "win32/config.h"
#else
#include "config.h"
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#ifdef DO_CLOCKING
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
#endif

#ifdef HAVE_FREETYPE
# ifdef HAVE_FT2BUILD_H
#  include <ft2build.h>
#  include FT_FREETYPE_H
# endif
# ifdef HAVE_FREETYPE_FREETYPE
#  include <freetype/freetype.h>
# else
#  include <freetype.h>
# endif
# if (FREETYPE_MAJOR == 2) && ((FREETYPE_MINOR == 0) || ((FREETYPE_MINOR == 1) && (FREETYPE_PATCH < 3)))
#  define FT_KERNING_DEFAULT ft_kerning_default
# endif
#endif

#define INCLUDE_ASFONT_PRIVATE

#ifdef _WIN32
# include "win32/afterbase.h"
#else
# include "afterbase.h"
#endif
#include "asfont.h"
#include "asfont_internal.h"
#include "asimage.h"
#include "asvisual.h"

#ifdef HAVE_XRENDER
#include <X11/extensions/Xrender.h>
#endif

#undef MAX_GLYPHS_PER_FONT


/*********************************************************************************/
/* TrueType and X11 font management functions :   								 */
/*********************************************************************************/

/*********************************************************************************/
/* construction destruction miscelanea:			   								 */
/*********************************************************************************/

void asfont_destroy (ASHashableValue value, void *data);

ASFontManager *
create_font_manager( Display *dpy, const char * font_path, ASFontManager *reusable_memory )
{
	ASFontManager *fontman = reusable_memory;
	if( fontman == NULL )
		fontman = safecalloc( 1, sizeof(ASFontManager));
	else
		memset( fontman, 0x00, sizeof(ASFontManager));

	fontman->dpy = dpy ;
	if( font_path )
		fontman->font_path = mystrdup( font_path );

#ifdef HAVE_FREETYPE
	if( !FT_Init_FreeType( &(fontman->ft_library)) )
		fontman->ft_ok = True ;
	else
		show_error( "Failed to initialize FreeType library - TrueType Fonts support will be disabled!");
LOCAL_DEBUG_OUT( "Freetype library is %p", fontman->ft_library );
#endif

	fontman->fonts_hash = create_ashash( 7, string_hash_value, string_compare, asfont_destroy );

	return fontman;
}

void
destroy_font_manager( ASFontManager *fontman, Bool reusable )
{
	if( fontman )
	{

        destroy_ashash( &(fontman->fonts_hash) );

#ifdef HAVE_FREETYPE
		FT_Done_FreeType( fontman->ft_library);
		fontman->ft_ok = False ;
#endif
		if( fontman->font_path )
			free( fontman->font_path );

		if( !reusable )
			free( fontman );
		else
			memset( fontman, 0x00, sizeof(ASFontManager));
	}
}

ASFont*
get_asfont( ASFontManager *fontman, const char *font_string, int face_no, int size, ASFontType type_and_flags )
{
	ASFont *font = NULL ;
	Bool freetype = False ;
	int type = type_and_flags&ASF_TypeMask ;
	if( face_no >= 100 )
		face_no = 0 ;
	if( size >= 1000 )
		size = 999 ;

	if( fontman && font_string )
	{
		ASHashData hdata = { 0 };
		if( get_hash_item( fontman->fonts_hash, AS_HASHABLE((char*)font_string), &hdata.vptr) != ASH_Success )
		{
			char *ff_name ;
			int len = strlen( font_string)+1 ;
			len += ((size>=100)?3:2)+1 ;
			len += ((face_no>=10)?2:1)+1 ;
			ff_name = safemalloc( len );
			sprintf( ff_name, "%s$%d$%d", font_string, size, face_no );
			if( get_hash_item( fontman->fonts_hash, AS_HASHABLE((char*)ff_name), &hdata.vptr) != ASH_Success )
			{	/* not loaded just yet - lets do it :*/
				if( type == ASF_Freetype || type == ASF_GuessWho )
					font = asfont_open_freetype_font_internal( fontman, font_string, face_no, size, (type == ASF_Freetype), get_flags(type_and_flags,~ASF_TypeMask));
				if( font == NULL && type != ASF_Freetype )
				{/* don't want to try and load font as X11 unless requested to do so */
					font = asfont_open_X11_font_internal( fontman, font_string, get_flags(type_and_flags,~ASF_TypeMask) );
				}else
					freetype = True ;
				if( font != NULL )
				{
					if( freetype )
					{
						font->name = ff_name ;
						ff_name = NULL ;
					}else
						font->name = mystrdup( font_string );
					add_hash_item( fontman->fonts_hash, AS_HASHABLE((char*)font->name), font);
				}
			}
			if( ff_name != NULL )
				free( ff_name );
		}

		if( font == NULL )
			font = hdata.vptr ;

		if( font )
			font->ref_count++ ;
	}
	return font;
}

ASFont*
dup_asfont( ASFont *font )
{
	if( font && font->fontman )
		font->ref_count++ ;
	else
		font = NULL;
	return font;
}

int
release_font( ASFont *font )
{
	int res = -1 ;
	if( font )
	{
		if( font->magic == MAGIC_ASFONT )
		{
			if( --(font->ref_count) < 0 )
			{
				ASFontManager *fontman = font->fontman ;
				if( fontman )
					remove_hash_item(fontman->fonts_hash, (ASHashableValue)(char*)font->name, NULL, True);
			}else
				res = font->ref_count ;
		}
	}
	return res ;
}

static inline void
free_glyph_data( register ASGlyph *asg )
{
    if( asg->pixmap )
        free( asg->pixmap );
/*fprintf( stderr, "\t\t%p\n", asg->pixmap );*/
    asg->pixmap = NULL ;
}

static void
destroy_glyph_range( ASGlyphRange **pgr )
{
	ASGlyphRange *gr = *pgr;
	if( gr )
	{
		*pgr = gr->above ;
        if( gr->below )
			gr->below->above = gr->above ;
        if( gr->above )
			gr->above->below = gr->below ;
        if( gr->glyphs )
		{
            int max_i = ((int)gr->max_char-(int)gr->min_char)+1 ;
            register int i = -1 ;
/*fprintf( stderr, " max_char = %d, min_char = %d, i = %d", gr->max_char, gr->min_char, max_i);*/
            while( ++i < max_i )
            {
/*fprintf( stderr, "%d >", i );*/
				free_glyph_data( &(gr->glyphs[i]) );
            }
            free( gr->glyphs );
			gr->glyphs = NULL ;
		}
		free( gr );
	}
}

static void
destroy_font( ASFont *font )
{
	if( font )
	{
#if defined(HAVE_XRENDER) && !defined(X_DISPLAY_MISSING)
		if( font->xrender_glyphset != 0 && font->fontman && font->fontman->dpy )
		{
			XRenderFreeGlyphSet( font->fontman->dpy, (GlyphSet)font->xrender_glyphset );
			font->xrender_glyphset = 0;
		}
#endif
#ifdef HAVE_FREETYPE
        if( font->type == ASF_Freetype && font->ft_face )
			FT_Done_Face(font->ft_face);
#endif
        if( font->name )
			free( font->name );
        while( font->codemap )
			destroy_glyph_range( &(font->codemap) );
        free_glyph_data( &(font->default_glyph) );
        if( font->locale_glyphs )
			destroy_ashash( &(font->locale_glyphs) );
        font->magic = 0 ;
		free( font );
	}
}

void
asglyph_destroy (ASHashableValue value, void *data)
{
	if( data )
	{
		free_glyph_data( (ASGlyph*)data );
		free( data );
	}
}

void
asfont_destroy (ASHashableValue value, void *data)
{
	if( data )
	{
	    char* cval = (char*)value ;
        if( ((ASMagic*)data)->magic == MAGIC_ASFONT )
        {
            if( cval == ((ASFont*)data)->name )
                cval = NULL ;          /* name is freed as part of destroy_font */
/*              fprintf( stderr,"freeing font \"%s\"...", (char*) value ); */
              destroy_font( (ASFont*)data );
/*              fprintf( stderr,"   done.\n"); */
        }
        if( cval )
            free( cval );
    }
}


static inline ASGlyph *get_unicode_glyph( const UNICODE_CHAR uc, ASFont *font )
{
	register ASGlyphRange *r;
	ASGlyph *asg = NULL ;
	ASHashData hdata = {0} ;
	for( r = font->codemap ; r != NULL ; r = r->above )
	{
LOCAL_DEBUG_OUT( "looking for glyph for char %lu (%p) if range (%ld,%ld)", uc, asg, r->min_char, r->max_char);

		if( r->max_char >= uc )
			if( r->min_char <= uc )
			{
				asg = &(r->glyphs[uc - r->min_char]);
LOCAL_DEBUG_OUT( "Found glyph for char %lu (%p)", uc, asg );
				if( asg->width > 0 && asg->pixmap != NULL )
					return asg;
				break;
			}
	}
	if( get_hash_item( font->locale_glyphs, AS_HASHABLE(uc), &hdata.vptr ) != ASH_Success )
	{
#ifdef HAVE_FREETYPE
		asg = asfont_freetype_load_locale_glyph( font, uc );
LOCAL_DEBUG_OUT( "glyph for char %lu  loaded as %p", uc, asg );
#endif
	}else
		asg = hdata.vptr ;
LOCAL_DEBUG_OUT( "%sFound glyph for char %lu ( %p )", asg?"":"Did not ", uc, asg );
	return asg?asg:&(font->default_glyph) ;
}


static inline ASGlyph *get_character_glyph( const unsigned char c, ASFont *font )
{
	return get_unicode_glyph( CHAR2UNICODE(c), font );
}

static UNICODE_CHAR
utf8_to_unicode ( const unsigned char *s )
{
	unsigned char c = s[0];

	if (c < 0x80)
	{
  		return (UNICODE_CHAR)c;
	} else if (c < 0xc2)
	{
  		return 0;
    } else if (c < 0xe0)
	{
	    if (!((s[1] ^ 0x80) < 0x40))
    		return 0;
	    return ((UNICODE_CHAR) (c & 0x1f) << 6)
  		       |(UNICODE_CHAR) (s[1] ^ 0x80);
    } else if (c < 0xf0)
	{
	    if (!((s[1] ^ 0x80) < 0x40 && (s[2] ^ 0x80) < 0x40
  		      && (c >= 0xe1 || s[1] >= 0xa0)))
	        return 0;
		return ((UNICODE_CHAR) (c & 0x0f) << 12)
        	 | ((UNICODE_CHAR) (s[1] ^ 0x80) << 6)
          	 |  (UNICODE_CHAR) (s[2] ^ 0x80);
	} else if (c < 0xf8 && sizeof(UNICODE_CHAR)*8 >= 32)
	{
	    if (!((s[1] ^ 0x80) < 0x40 && (s[2] ^ 0x80) < 0x40
  	        && (s[3] ^ 0x80) < 0x40
    	    && (c >= 0xf1 || s[1] >= 0x90)))
    		return 0;
	    return ((UNICODE_CHAR) (c & 0x07) << 18)
             | ((UNICODE_CHAR) (s[1] ^ 0x80) << 12)
	         | ((UNICODE_CHAR) (s[2] ^ 0x80) << 6)
  	         |  (UNICODE_CHAR) (s[3] ^ 0x80);
	} else if (c < 0xfc && sizeof(UNICODE_CHAR)*8 >= 32)
	{
	    if (!((s[1] ^ 0x80) < 0x40 && (s[2] ^ 0x80) < 0x40
  	        && (s[3] ^ 0x80) < 0x40 && (s[4] ^ 0x80) < 0x40
    	    && (c >= 0xf9 || s[1] >= 0x88)))
	        return 0;
		return ((UNICODE_CHAR) (c & 0x03) << 24)
             | ((UNICODE_CHAR) (s[1] ^ 0x80) << 18)
	         | ((UNICODE_CHAR) (s[2] ^ 0x80) << 12)
  	         | ((UNICODE_CHAR) (s[3] ^ 0x80) << 6)
    	     |  (UNICODE_CHAR) (s[4] ^ 0x80);
	} else if (c < 0xfe && sizeof(UNICODE_CHAR)*8 >= 32)
	{
	    if (!((s[1] ^ 0x80) < 0x40 && (s[2] ^ 0x80) < 0x40
  		    && (s[3] ^ 0x80) < 0x40 && (s[4] ^ 0x80) < 0x40
      	    && (s[5] ^ 0x80) < 0x40
        	&& (c >= 0xfd || s[1] >= 0x84)))
	  		return 0;
		return ((UNICODE_CHAR) (c & 0x01) << 30)
      	     | ((UNICODE_CHAR) (s[1] ^ 0x80) << 24)
        	 | ((UNICODE_CHAR) (s[2] ^ 0x80) << 18)
             | ((UNICODE_CHAR) (s[3] ^ 0x80) << 12)
	         | ((UNICODE_CHAR) (s[4] ^ 0x80) << 6)
  	    	 |  (UNICODE_CHAR) (s[5] ^ 0x80);
    }
    return 0;
}

static inline ASGlyph *get_utf8_glyph( const char *utf8, ASFont *font )
{
	UNICODE_CHAR uc = utf8_to_unicode ( (const unsigned char*)utf8 );
	LOCAL_DEBUG_OUT( "translated to Unicode 0x%lX(%ld), UTF8 size = %d", uc, uc, UTF8_CHAR_SIZE(utf8[0]) );
	return get_unicode_glyph( uc, font );
}

/*********************************************************************************/
/* actuall rendering code :						   								 */
/*********************************************************************************/

typedef struct ASGlyphMap
{
	int  height, width ;
	ASGlyph 	**glyphs;
	int 		  glyphs_num;
	short 		 *x_kerning ;
}ASGlyphMap;


static void
apply_text_3D_type( ASText3DType type,
                    int *width, int *height )
{
	switch( type )
	{
		case AST_Embossed   :
		case AST_Sunken     :
				(*width) += 2; (*height) += 2 ;
				break ;
		case AST_ShadeAbove :
		case AST_ShadeBelow :
				(*width)+=3; (*height)+=3 ;
				break ;
		case AST_SunkenThick :
		case AST_EmbossedThick :
				(*width)+=3; (*height)+=3 ;
				break ;
		case AST_OutlineAbove :
		case AST_OutlineBelow :
				(*width) += 1; (*height) += 1 ;
				break ;
		case AST_OutlineFull :
				(*width) += 2; (*height) += 2 ;
				break ;
		default  :
				break ;
	}
}

static unsigned int
goto_tab_stop( ASTextAttributes *attr, unsigned int space_size, unsigned int line_width )
{
	unsigned int tab_size = attr->tab_size*space_size ;
	unsigned int tab_stop = (((attr->origin + line_width)/tab_size)+1)*tab_size ;
	if( attr->tab_stops != NULL && attr->tab_stops_num > 0 ) 	
	{
		unsigned int i ;
		for( i = 0 ; i < attr->tab_stops_num ; ++i ) 
		{	
			if( attr->tab_stops[i] < line_width )
				continue;
			if( attr->tab_stops[i] < tab_stop ) 
				tab_stop = attr->tab_stops[i] ;
			break;
		}
	}
	return tab_stop;		
}

#ifdef HAVE_FREETYPE
#define GET_KERNING(var,prev_gid,this_gid)   \
	do{ if( (prev_gid) != 0 && font->type == ASF_Freetype && get_flags(font->flags, ASF_Monospaced|ASF_HasKerning) == ASF_HasKerning ) { \
		FT_Vector delta; \
		FT_Get_Kerning( font->ft_face, (prev_gid), (this_gid), FT_KERNING_DEFAULT, &delta );\
		(var) = (short)(delta.x >> 6); \
	}}while(0)
#else
#define GET_KERNING(var,prev_gid,this_gid)	do{(var)=0;}while(0)	  
#endif
/*		fprintf( stderr, "####### pair %d ,%d 	has kerning = %d\n", prev_gid,this_gid, var ); */


#define FILL_TEXT_GLYPH_MAP(name,type,getglyph,incr) \
static unsigned int \
name( const type *text, ASFont *font, ASGlyphMap *map, ASTextAttributes *attr, int space_size, unsigned int offset_3d_x ) \
{ \
	int w = 0, line_count = 0, line_width = 0; \
	int i = -1, g = 0 ; \
	ASGlyph *last_asg = NULL ; unsigned int last_gid = 0 ; \
	do \
	{ \
		++i ; \
		LOCAL_DEBUG_OUT("begin_i=%d, char_code = 0x%2.2X",i,text[i]); \
		if( text[i] == '\n' || g == map->glyphs_num-1 ) \
		{ \
			if( last_asg && last_asg->width+last_asg->lead > last_asg->step ) \
				line_width += last_asg->width+last_asg->lead - last_asg->step ; \
			last_asg = NULL; last_gid = 0 ; \
			if( line_width > w ) \
				w = line_width ; \
			line_width = 0 ; \
			++line_count ; \
			map->glyphs[g] = (g == map->glyphs_num-1)?GLYPH_EOT:GLYPH_EOL; \
			++g; \
		}else \
		{ \
			last_asg = NULL ; \
			if( text[i] == ' ' ) \
			{   last_gid = 0 ; \
				line_width += space_size ; \
				map->glyphs[g++] = GLYPH_SPACE; \
			}else if( text[i] == '\t' ) \
			{   last_gid = 0 ; \
				if( !get_flags(attr->rendition_flags, ASTA_UseTabStops) ) line_width += space_size*attr->tab_size ; \
				else line_width = goto_tab_stop( attr, space_size, line_width ); \
				map->glyphs[g++] = GLYPH_TAB; \
			}else \
			{ \
				last_asg = getglyph; \
				map->glyphs[g] = last_asg; \
				GET_KERNING(map->x_kerning[g],last_gid,last_asg->font_gid); \
				if( line_width < -last_asg->lead ) line_width -= (line_width+last_asg->lead);\
				line_width += last_asg->step+offset_3d_x+map->x_kerning[g]; \
				++g; last_gid = last_asg->font_gid ; \
				LOCAL_DEBUG_OUT("pre_i=%d",i); \
				incr; /* i+=CHAR_SIZE(text[i])-1; */ \
				LOCAL_DEBUG_OUT("post_i=%d",i); \
			} \
		} \
	}while( g < map->glyphs_num );  \
	map->width = MAX( w, 1 ); \
	return line_count ; \
}

#ifdef _MSC_VER
FILL_TEXT_GLYPH_MAP(fill_text_glyph_map_Char,char,get_character_glyph(text[i],font),1)
FILL_TEXT_GLYPH_MAP(fill_text_glyph_map_Unicode,UNICODE_CHAR,get_unicode_glyph(text[i],font),1)
#else
FILL_TEXT_GLYPH_MAP(fill_text_glyph_map_Char,char,get_character_glyph(text[i],font),/* */)
FILL_TEXT_GLYPH_MAP(fill_text_glyph_map_Unicode,UNICODE_CHAR,get_unicode_glyph(text[i],font),/* */)
#endif
FILL_TEXT_GLYPH_MAP(fill_text_glyph_map_UTF8,char,get_utf8_glyph(&text[i],font),i+=(UTF8_CHAR_SIZE(text[i])-1))

void
free_glyph_map( ASGlyphMap *map, Bool reusable )
{
    if( map )
    {
		if( map->glyphs )
	        free( map->glyphs );
		if( map->x_kerning )
	        free( map->x_kerning );
        if( !reusable )
            free( map );
    }
}

static int
get_text_length (ASCharType char_type, const char *text)
{
	register int count = 0;
	if( char_type == ASCT_Char )
	{
		register char *ptr = (char*)text ;
		while( ptr[count] != 0 )++count;
	}else if( char_type == ASCT_UTF8 )
	{
		register char *ptr = (char*)text ;
		while( *ptr != 0 ){	++count; ptr += UTF8_CHAR_SIZE(*ptr); }
	}else if( char_type == ASCT_Unicode )
	{
		register UNICODE_CHAR *uc_ptr = (UNICODE_CHAR*)text ;
		while( uc_ptr[count] != 0 )	++count;
	}
	return count;
}

ASGlyph**
get_text_glyph_list (const char *text, ASFont *font, ASCharType char_type, int length)
{
	ASGlyph** glyphs = NULL;
	int i = 0;
	
	if (text == NULL || font == NULL)
		return NULL;
	if (length <= 0)
		if ((length = get_text_length (char_type, text)) <= 0)
			return NULL;
	
	glyphs = safecalloc( length+1, sizeof(ASGlyph*));
	if (char_type == ASCT_Char)
	{
		register char *ptr = (char*)text;
		for (i = 0 ; i < length ; ++i)
			glyphs[i] = get_character_glyph (ptr[i], font);
	}else if (char_type == ASCT_UTF8)
	{
		register char *ptr = (char*)text;
		for (i = 0 ; i < length ; ++i)
		{
			glyphs[i] = get_utf8_glyph (ptr, font);
			ptr += UTF8_CHAR_SIZE(*ptr);
		}		
	}else if( char_type == ASCT_Unicode )
	{
		register UNICODE_CHAR *uc_ptr = (UNICODE_CHAR*)text ;
		for (i = 0 ; i < length ; ++i)
			glyphs[i] = get_unicode_glyph (uc_ptr[i], font);
	}
	
	return glyphs;			
}

static Bool
get_text_glyph_map (const char *text, ASFont *font, ASGlyphMap *map, ASTextAttributes *attr, int length  )
{
	unsigned int line_count = 0;
	int offset_3d_x = 0, offset_3d_y = 0 ;
	int space_size  = 0 ;

	apply_text_3D_type( attr->type, &offset_3d_x, &offset_3d_y );

	if( text == NULL || font == NULL || map == NULL)
		return False;
	
	offset_3d_x += font->spacing_x ;
	offset_3d_y += font->spacing_y ;
	
	space_size  = font->space_size ;
	if( !get_flags( font->flags, ASF_Monospaced) )
		space_size  = (space_size>>1)+1 ;
	space_size += offset_3d_x;

	map->glyphs_num = 1;
	if( length <= 0 ) 
		length = get_text_length (attr->char_type, text);

	map->glyphs_num = 1 + length ;

	map->glyphs = safecalloc( map->glyphs_num, sizeof(ASGlyph*));
	map->x_kerning = safecalloc( map->glyphs_num, sizeof(short));

	if( attr->char_type == ASCT_UTF8 )
		line_count = fill_text_glyph_map_UTF8( text, font, map, attr, space_size, offset_3d_x );
	else if( attr->char_type == ASCT_Unicode )
		line_count = fill_text_glyph_map_Unicode( (UNICODE_CHAR*)text, font, map, attr, space_size, offset_3d_x );
	else /* assuming attr->char_type == ASCT_Char by default */
		line_count = fill_text_glyph_map_Char( text, font, map, attr, space_size, offset_3d_x );
	
    map->height = line_count * (font->max_height+offset_3d_y) - font->spacing_y;

	if( map->height <= 0 )
		map->height = 1 ;

	return True;
}

#define GET_TEXT_SIZE_LOOP(getglyph,incr,len) \
	do{ Bool terminated = True; ++i ;\
		if( len == 0 || i < len )	\
		{ 	terminated = ( text[i] == '\0' || text[i] == '\n' ); \
			if( x_positions ) x_positions[i] = line_width ; \
		} \
		if( terminated ) { \
			if( last_asg && last_asg->width+last_asg->lead > last_asg->step ) \
				line_width += last_asg->width+last_asg->lead - last_asg->step ; \
			last_asg = NULL; last_gid = 0 ; \
			if( line_width > w ) \
				w = line_width ; \
			line_width = 0 ; \
            ++line_count ; \
		}else { \
			last_asg = NULL ; \
			if( text[i] == ' ' ){ \
				line_width += space_size ; last_gid = 0 ;\
			}else if( text[i] == '\t' ){ last_gid = 0 ; \
				if( !get_flags(attr->rendition_flags, ASTA_UseTabStops) ) line_width += space_size*attr->tab_size ; \
				else line_width = goto_tab_stop( attr, space_size, line_width ); \
			}else{ int kerning = 0 ;\
				last_asg = getglyph; \
				GET_KERNING(kerning,last_gid,last_asg->font_gid); \
				if( line_width < -last_asg->lead ) line_width -= (line_width+last_asg->lead);\
				line_width += last_asg->step+offset_3d_x +kerning ;  \
				last_gid = last_asg->font_gid ; \
				incr ; \
			} \
		} \
	}while( (len <= 0 || len > i) && text[i] != '\0' )

static Bool
get_text_size_internal( const char *src_text, ASFont *font, ASTextAttributes *attr, unsigned int *width, unsigned int *height, int length, int *x_positions )
{
    int w = 0, h = 0, line_count = 0;
	int line_width = 0;
    int i = -1;
	ASGlyph *last_asg = NULL ;
	int space_size = 0;
	int offset_3d_x = 0, offset_3d_y = 0 ;
	int last_gid = 0 ;


	apply_text_3D_type( attr->type, &offset_3d_x, &offset_3d_y );
	if( src_text == NULL || font == NULL )
		return False;
	
	offset_3d_x += font->spacing_x ;
	offset_3d_y += font->spacing_y ;

	space_size  = font->space_size ;
	if( !get_flags( font->flags, ASF_Monospaced) )
		space_size  = (space_size>>1)+1 ;
	space_size += offset_3d_x ;

	LOCAL_DEBUG_OUT( "char_type = %d", attr->char_type );
	if( attr->char_type == ASCT_Char )
	{
		char *text = (char*)&src_text[0] ;
#ifdef _MSC_VER
		GET_TEXT_SIZE_LOOP(get_character_glyph(text[i],font),1,length);
#else		
		GET_TEXT_SIZE_LOOP(get_character_glyph(text[i],font),/* */,length);
#endif
	}else if( attr->char_type == ASCT_UTF8 )
	{
		char *text = (char*)&src_text[0] ;
		int byte_length = 0 ;
		if( length > 0 )
		{
			int k ; 
			for( k = 0 ; k < length ; ++k )
			{
				if( text[byte_length] == '\0' ) 
					break;
				byte_length += UTF8_CHAR_SIZE(text[byte_length]);		   
			}	 
		}	 
		GET_TEXT_SIZE_LOOP(get_utf8_glyph(&text[i],font),i+=UTF8_CHAR_SIZE(text[i])-1,byte_length);
	}else if( attr->char_type == ASCT_Unicode )
	{
		UNICODE_CHAR *text = (UNICODE_CHAR*)&src_text[0] ;
#ifdef _MSC_VER
		GET_TEXT_SIZE_LOOP(get_unicode_glyph(text[i],font),1,length);
#else		   
		GET_TEXT_SIZE_LOOP(get_unicode_glyph(text[i],font),/* */,length);
#endif
	}

    h = line_count * (font->max_height+offset_3d_y) - font->spacing_y;

    if( w < 1 )
		w = 1 ;
	if( h < 1 )
		h = 1 ;
	if( width )
		*width = w;
	if( height )
		*height = h;
	return True ;
}

Bool
get_text_size( const char *src_text, ASFont *font, ASText3DType type, unsigned int *width, unsigned int *height )
{
	ASTextAttributes attr = {ASTA_VERSION_INTERNAL, 0, 0, ASCT_Char, 8, 0, NULL, 0 }; 
	attr.type = type ;
	if( IsUTF8Locale() ) 
		attr.char_type = ASCT_UTF8 ;
	return get_text_size_internal( (char*)src_text, font, &attr, width, height, 0/*autodetect length*/, NULL );
}

Bool
get_unicode_text_size( const UNICODE_CHAR *src_text, ASFont *font, ASText3DType type, unsigned int *width, unsigned int *height )
{
	ASTextAttributes attr = {ASTA_VERSION_INTERNAL, 0, 0, ASCT_Unicode, 8, 0, NULL, 0 }; 
	attr.type = type ;
	return get_text_size_internal( (char*)src_text, font, &attr, width, height, 0/*autodetect length*/, NULL );
}

Bool
get_utf8_text_size( const char *src_text, ASFont *font, ASText3DType type, unsigned int *width, unsigned int *height )
{
	ASTextAttributes attr = {ASTA_VERSION_INTERNAL, 0, 0, ASCT_UTF8, 8, 0, NULL, 0 }; 
	attr.type = type ;
	return get_text_size_internal( (char*)src_text, font, &attr, width, height, 0/*autodetect length*/, NULL );
}

Bool
get_fancy_text_size( const void *src_text, ASFont *font, ASTextAttributes *attr, unsigned int *width, unsigned int *height, int length, int *x_positions )
{
	ASTextAttributes internal_attr = {ASTA_VERSION_INTERNAL, 0, 0, ASCT_Char, 8, 0, NULL, 0 }; 
	if( attr != NULL ) 
	{	
		internal_attr = *attr;
		if( internal_attr.tab_size == 0 ) 
			internal_attr.tab_size = 8 ;
		internal_attr.version = ASTA_VERSION_INTERNAL ;
	}else
	{
		if( IsUTF8Locale() ) 
			internal_attr.char_type = ASCT_UTF8 ;
	}	 
	return get_text_size_internal( src_text, font, &internal_attr, width, height, length, x_positions );
}

inline static void
render_asglyph( CARD8 **scanlines, CARD8 *row,
                int start_x, int y, int width, int height,
				CARD32 ratio )
{
	int count = -1 ;
	int max_y = y + height ;
	register CARD32 data = 0;
	while( y < max_y )
	{
		register CARD8 *dst = scanlines[y]+start_x;
		register int x = -1;
		while( ++x < width )
		{
/*fprintf( stderr, "data = %X, count = %d, x = %d, y = %d\n", data, count, x, y );*/
			if( count < 0 )
			{
				data = *(row++);
				if( (data&0x80) != 0)
				{
					data = ((data&0x7F)<<1);
					if( data != 0 )
						++data;
				}else
				{
					count = data&0x3F ;
					data = ((data&0x40) != 0 )? 0xFF: 0x00;
				}
				if( ratio != 0xFF && data != 0 )
					data = ((data*ratio)>>8)+1 ;
			}
			if( data > dst[x] ) 
				dst[x] = (data > 255)? 0xFF:data ;
			--count;
		}
		++y;
	}
}

inline static void
render_asglyph_over( CARD8 **scanlines, CARD8 *row,
                int start_x, int y, int width, int height,
				CARD32 value )
{
	int count = -1 ;
	int max_y = y + height ;
	CARD32 anti_data = 0;
	register CARD32 data = 0;
	while( y < max_y )
	{
		register CARD8 *dst = scanlines[y]+start_x;
		register int x = -1;
		while( ++x < width )
		{
/*fprintf( stderr, "data = %X, count = %d, x = %d, y = %d\n", data, count, x, y );*/
			if( count < 0 )
			{
				data = *(row++);
				if( (data&0x80) != 0)
				{
					data = ((data&0x7F)<<1);
					if( data != 0 )
						++data;
				}else
				{
					count = data&0x3F ;
					data = ((data&0x40) != 0 )? 0xFF: 0x00;
				}
				anti_data = 256 - data ;
			}
			if( data >= 254 ) 
				dst[x] = value ;
			else
				dst[x] = ((CARD32)dst[x]*anti_data + value*data)>>8 ;
			--count;
		}
		++y;
	}
}



static ASImage *
draw_text_internal( const char *text, ASFont *font, ASTextAttributes *attr, int compression, int length )
{
	ASGlyphMap map ;
	CARD8 *memory, *rgb_memory = NULL;
	CARD8 **scanlines, **rgb_scanlines = NULL ;
	int i = 0, offset = 0, line_height, space_size, base_line;
	ASImage *im;
	int pen_x = 0, pen_y = 0;
	int offset_3d_x = 0, offset_3d_y = 0  ;
	CARD32 back_color = 0 ;
	CARD32 alpha_7 = 0x007F, alpha_9 = 0x009F, alpha_A = 0x00AF, alpha_C = 0x00CF, alpha_F = 0x00FF, alpha_E = 0x00EF;
	START_TIME(started);	   

	// Perform line breaks if a fixed width is specified
	// TODO: this is a quick and dirty fix and should work for now, but we really should fix it 
	// so we don't have to calculate text size so many times as well as make it UNICODE friendly
	// and remove mangling of the source text (Sasha): 
	if (attr->width)
	{
        unsigned int width, height; // SMA
        get_text_size(  text , font, attr->type, &width, &height ); 
        if ( (width > attr->width)  &&  (strchr(text, ' ')) )
        {
          char *tryPtr = strchr(text,' ');
          char *oldTryPtr = tryPtr;
          while (tryPtr)
            {        
               *tryPtr = 0;
               get_text_size(  text , font, attr->type, &width, &height ); 
               if (width > attr->width)
                   *oldTryPtr = '\n';
               
               *tryPtr = ' ';
               oldTryPtr = tryPtr;
               tryPtr = strchr(tryPtr + 1,' ');
            }
        }
	}	    

LOCAL_DEBUG_CALLER_OUT( "text = \"%s\", font = %p, compression = %d", text, font, compression );
	if( !get_text_glyph_map( text, font, &map, attr, length) )
		return NULL;
	
	if( map.width <= 0 ) 
		return NULL;

	apply_text_3D_type( attr->type, &offset_3d_x, &offset_3d_y );

	offset_3d_x += font->spacing_x ;
	offset_3d_y += font->spacing_y ;
	line_height = font->max_height+offset_3d_y ;

LOCAL_DEBUG_OUT( "text size = %dx%d pixels", map.width, map.height );
	im = create_asimage( map.width, map.height, compression );

	space_size  = font->space_size ;
	if( !get_flags( font->flags, ASF_Monospaced) )
		space_size  = (space_size>>1)+1 ;
	space_size += offset_3d_x;

	base_line = font->max_ascend;
LOCAL_DEBUG_OUT( "line_height is %d, space_size is %d, base_line is %d", line_height, space_size, base_line );
	scanlines = safemalloc( line_height*sizeof(CARD8*));
	memory = safecalloc( 1, line_height*map.width);
	for( i = 0 ; i < line_height ; ++i ) 
	{
		scanlines[i] = memory + offset;
		offset += map.width;
	}
	if( attr->type >= AST_OutlineAbove ) 
	{
		CARD32 fc = attr->fore_color ;
		offset = 0 ;
		rgb_scanlines = safemalloc( line_height*3*sizeof(CARD8*));
		rgb_memory = safecalloc( 1, line_height*map.width*3);
		for( i = 0 ; i < line_height*3 ; ++i ) 
		{
			rgb_scanlines[i] = rgb_memory + offset;
			offset += map.width;
		}
		if( (ARGB32_RED16(fc)*222+ARGB32_GREEN16(fc)*707+ARGB32_BLUE16(fc) *71)/1000 < 0x07FFF ) 
		{	
			back_color = 0xFF ;
			memset( rgb_memory, back_color, line_height*map.width*3 );
		}
	}	 
	if( ARGB32_ALPHA8(attr->fore_color) > 0 ) 
	{
		CARD32 a = ARGB32_ALPHA8(attr->fore_color);
		alpha_7 = (0x007F*a)>>8 ;
		alpha_9 = (0x009F*a)>>8 ;
		alpha_A = (0x00AF*a)>>8 ;
		alpha_C = (0x00CF*a)>>8 ;
		alpha_E	= (0x00EF*a)>>8 ;
		alpha_F = (0x00FF*a)>>8 ;
	}	 

	i = -1 ;
	if(get_flags(font->flags, ASF_RightToLeft))
		pen_x = map.width ;

	do
	{
		++i;
/*fprintf( stderr, "drawing character %d '%c'\n", i, text[i] );*/
		if( map.glyphs[i] == GLYPH_EOL || map.glyphs[i] == GLYPH_EOT )
		{
			int y;
			for( y = 0 ; y < line_height ; ++y )
			{
#if 1
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
				{				
					int x = 0;
					while( x < map.width )
						fprintf( stderr, "%2.2X ", scanlines[y][x++] );
					fprintf( stderr, "\n" );
				}
#endif
#endif
 				im->channels[IC_ALPHA][pen_y+y] = store_data( NULL, scanlines[y], map.width, ASStorage_RLEDiffCompress, 0);
				if( attr->type >= AST_OutlineAbove ) 
				{
	 				im->channels[IC_RED][pen_y+y] 	= store_data( NULL, rgb_scanlines[y], map.width, ASStorage_RLEDiffCompress, 0);
	 				im->channels[IC_GREEN][pen_y+y] = store_data( NULL, rgb_scanlines[y+line_height], map.width, ASStorage_RLEDiffCompress, 0);
	 				im->channels[IC_BLUE][pen_y+y]  = store_data( NULL, rgb_scanlines[y+line_height+line_height], map.width, ASStorage_RLEDiffCompress, 0);
				}	 
			}
			
			memset( memory, 0x00, line_height*map.width );
			if( attr->type >= AST_OutlineAbove ) 
				memset( rgb_memory, back_color, line_height*map.width*3 );
			
			pen_x = get_flags(font->flags, ASF_RightToLeft)? map.width : 0;
			pen_y += line_height;
			if( pen_y <0 )
				pen_y = 0 ;
		}else
		{
			if( map.glyphs[i] == GLYPH_SPACE || map.glyphs[i] == GLYPH_TAB )
			{
				if(map.glyphs[i] == GLYPH_TAB)
				{
					if( !get_flags(attr->rendition_flags, ASTA_UseTabStops) ) pen_x += space_size*attr->tab_size ;
					else pen_x = goto_tab_stop( attr, space_size, pen_x );
				}else if( get_flags(font->flags, ASF_RightToLeft) )
					pen_x -= space_size ;
				else
					pen_x += space_size ;
			}else
			{
				/* now comes the fun part : */
				ASGlyph *asg = map.glyphs[i] ;
				int y = base_line - asg->ascend;
				int start_x = 0, offset_x = 0;

				if( get_flags(font->flags, ASF_RightToLeft) )
					pen_x  -= asg->step+offset_3d_x +map.x_kerning[i];
				else
				{
					LOCAL_DEBUG_OUT( "char # %d : pen_x = %d, kerning = %d, lead = %d, width = %d, step = %d", i, pen_x, map.x_kerning[i], asg->lead, asg->width, asg->step );
					pen_x += map.x_kerning[i] ;
				}
				if( asg->lead > 0 )
					start_x = pen_x + asg->lead ;
				else
					start_x = pen_x + asg->lead ;
				if( start_x < 0 )
				{
					offset_x = -start_x ; 
					start_x = 0 ;
				}
				if( y < 0 )
					y = 0 ;

				switch( attr->type )
				{
					case AST_Plain :
						render_asglyph( scanlines, asg->pixmap, start_x, y, asg->width, asg->height, alpha_F );
					    break ;
					case AST_Embossed :
						render_asglyph( scanlines, asg->pixmap, start_x, y, asg->width, asg->height, alpha_F );
						render_asglyph( scanlines, asg->pixmap, start_x+2, y+2, asg->width, asg->height, alpha_9 );
						render_asglyph( scanlines, asg->pixmap, start_x+1, y+1, asg->width, asg->height, alpha_C );
 					    break ;
					case AST_Sunken :
						render_asglyph( scanlines, asg->pixmap, start_x, y, asg->width, asg->height, alpha_9 );
						render_asglyph( scanlines, asg->pixmap, start_x+2, y+2, asg->width, asg->height, alpha_F );
						render_asglyph( scanlines, asg->pixmap, start_x+1, y+1, asg->width, asg->height, alpha_C );
					    break ;
					case AST_ShadeAbove :
						render_asglyph( scanlines, asg->pixmap, start_x, y, asg->width, asg->height, alpha_7 );
						render_asglyph( scanlines, asg->pixmap, start_x+3, y+3, asg->width, asg->height, alpha_F );
					    break ;
					case AST_ShadeBelow :
						render_asglyph( scanlines, asg->pixmap, start_x+3, y+3, asg->width, asg->height, alpha_7 );
						render_asglyph( scanlines, asg->pixmap, start_x, y, asg->width, asg->height, alpha_F );
					    break ;
					case AST_EmbossedThick :
						render_asglyph( scanlines, asg->pixmap, start_x, y, asg->width, asg->height, alpha_F );
						render_asglyph( scanlines, asg->pixmap, start_x+1, y+1, asg->width, asg->height, alpha_E );
						render_asglyph( scanlines, asg->pixmap, start_x+3, y+3, asg->width, asg->height, alpha_7 );
						render_asglyph( scanlines, asg->pixmap, start_x+2, y+2, asg->width, asg->height, alpha_C );
 					    break ;
					case AST_SunkenThick :
						render_asglyph( scanlines, asg->pixmap, start_x, y, asg->width, asg->height, alpha_7 );
						render_asglyph( scanlines, asg->pixmap, start_x+1, y+1, asg->width, asg->height, alpha_A );
						render_asglyph( scanlines, asg->pixmap, start_x+3, y+3, asg->width, asg->height, alpha_F );
						render_asglyph( scanlines, asg->pixmap, start_x+2, y+2, asg->width, asg->height, alpha_C );
 					    break ;
					case AST_OutlineAbove :
						render_asglyph( scanlines, asg->pixmap, start_x, y, asg->width, asg->height, alpha_A );
						render_asglyph( scanlines, asg->pixmap, start_x+1, y+1, asg->width, asg->height, alpha_F );
						render_asglyph_over( rgb_scanlines, asg->pixmap, start_x+1, y+1, asg->width, asg->height, ARGB32_RED8(attr->fore_color) );
						render_asglyph_over( &rgb_scanlines[line_height], asg->pixmap, start_x+1, y+1, asg->width, asg->height, ARGB32_GREEN8(attr->fore_color) );
						render_asglyph_over( &rgb_scanlines[line_height*2], asg->pixmap, start_x+1, y+1, asg->width, asg->height, ARGB32_BLUE8(attr->fore_color) );
					    break ;
					case AST_OutlineBelow :
						render_asglyph( scanlines, asg->pixmap, start_x, y, asg->width, asg->height, alpha_F );
						render_asglyph( scanlines, asg->pixmap, start_x+1, y+1, asg->width, asg->height, alpha_A );
						render_asglyph_over( rgb_scanlines, asg->pixmap, start_x, y, asg->width, asg->height, ARGB32_RED8(attr->fore_color) );
						render_asglyph_over( &rgb_scanlines[line_height], asg->pixmap, start_x, y, asg->width, asg->height, ARGB32_GREEN8(attr->fore_color) );
						render_asglyph_over( &rgb_scanlines[line_height*2], asg->pixmap, start_x, y, asg->width, asg->height, ARGB32_BLUE8(attr->fore_color) );
					    break ;
					case AST_OutlineFull :
						render_asglyph( scanlines, asg->pixmap, start_x, y, asg->width, asg->height, alpha_A );
						render_asglyph( scanlines, asg->pixmap, start_x+1, y+1, asg->width, asg->height, alpha_F );
						render_asglyph( scanlines, asg->pixmap, start_x+2, y+2, asg->width, asg->height, alpha_A );
						render_asglyph_over( rgb_scanlines, asg->pixmap, start_x+1, y+1, asg->width, asg->height, ARGB32_RED8(attr->fore_color) );
						render_asglyph_over( &rgb_scanlines[line_height], asg->pixmap, start_x+1, y+1, asg->width, asg->height, ARGB32_GREEN8(attr->fore_color) );
						render_asglyph_over( &rgb_scanlines[line_height*2], asg->pixmap, start_x+1, y+1, asg->width, asg->height, ARGB32_BLUE8(attr->fore_color) );
					    break ;
				  default:
				        break ;
				}

				if( !get_flags(font->flags, ASF_RightToLeft) )
  					pen_x  += offset_x + asg->step+offset_3d_x;
			}
		}
	}while( map.glyphs[i] != GLYPH_EOT );
    free_glyph_map( &map, True );
	free( memory );
	free( scanlines );
	if( rgb_memory ) 
		free( rgb_memory );
	if( rgb_scanlines ) 
		free( rgb_scanlines );
	SHOW_TIME("", started);
	return im;
}

ASImage *
draw_text( const char *text, ASFont *font, ASText3DType type, int compression )
{
	ASTextAttributes attr = {ASTA_VERSION_INTERNAL, 0, 0, ASCT_Char, 8, 0, NULL, 0, ARGB32_White }; 
	attr.type = type ;
	if( IsUTF8Locale() ) 
		attr.char_type = ASCT_UTF8 ;
	return draw_text_internal( text, font, &attr, compression, 0/*autodetect length*/ );
}

ASImage *
draw_unicode_text( const UNICODE_CHAR *text, ASFont *font, ASText3DType type, int compression )
{
	ASTextAttributes attr = {ASTA_VERSION_INTERNAL, 0, 0, ASCT_Unicode, 8, 0, NULL, 0, ARGB32_White }; 
	attr.type = type ;
	return draw_text_internal( (const char*)text, font, &attr, compression, 0/*autodetect length*/ );
}

ASImage *
draw_utf8_text( const char *text, ASFont *font, ASText3DType type, int compression )
{
	ASTextAttributes attr = {ASTA_VERSION_INTERNAL, 0, 0, ASCT_UTF8, 8, 0, NULL, 0, ARGB32_White }; 
	attr.type = type ;
	return draw_text_internal( text, font, &attr, compression, 0/*autodetect length*/ );
}

ASImage *
draw_fancy_text( const void *text, ASFont *font, ASTextAttributes *attr, int compression, int length )
{
	ASTextAttributes internal_attr = {ASTA_VERSION_INTERNAL, 0, 0, ASCT_Char, 8, 0, NULL, 0, ARGB32_White }; 
	if( attr != NULL ) 
	{	
		internal_attr = *attr;
		if( internal_attr.tab_size == 0 ) 
			internal_attr.tab_size = 8 ;
		internal_attr.version = ASTA_VERSION_INTERNAL ;
	}else
	{
		if( IsUTF8Locale() ) 
			internal_attr.char_type = ASCT_UTF8 ;
	}  
	return draw_text_internal( text, font, &internal_attr, compression, length );
}

Bool get_asfont_glyph_spacing( ASFont* font, int *x, int *y )
{
	if( font )
	{
		if( x )
			*x = font->spacing_x ;
		if( y )
			*y = font->spacing_y ;
		return True ;
	}
	return False ;
}

Bool set_asfont_glyph_spacing( ASFont* font, int x, int y )
{
	if( font )
	{
		font->spacing_x = (x < 0 )? 0: x;
		font->spacing_y = (y < 0 )? 0: y;
		return True ;
	}
	return False ;
}

/* Misc functions : */
void print_asfont( FILE* stream, ASFont* font)
{
	if( font )
	{
		fprintf( stream, "font.type = %d\n", font->type       );
		fprintf( stream, "font.flags = 0x%lX\n", font->flags       );
#ifdef HAVE_FREETYPE
		fprintf( stream, "font.ft_face = %p\n", font->ft_face    );              /* free type font handle */
#endif
		fprintf( stream, "font.max_height = %d\n", font->max_height );
		fprintf( stream, "font.space_size = %d\n" , font->space_size );
		fprintf( stream, "font.spacing_x  = %d\n" , font->spacing_x );
		fprintf( stream, "font.spacing_y  = %d\n" , font->spacing_y );
		fprintf( stream, "font.max_ascend = %d\n", font->max_ascend );
		fprintf( stream, "font.max_descend = %d\n", font->max_descend );
	}
}

void print_asglyph( FILE* stream, ASFont* font, unsigned long c)
{
	if( font )
	{
		int i, k ;
		ASGlyph *asg = get_unicode_glyph( c, font );
		if( asg == NULL )
			return;

		fprintf( stream, "glyph[%lu].ASCII = %c\n", c, (char)c );
		fprintf( stream, "glyph[%lu].width = %d\n", c, asg->width  );
		fprintf( stream, "glyph[%lu].height = %d\n", c, asg->height  );
		fprintf( stream, "glyph[%lu].lead = %d\n", c, asg->lead  );
		fprintf( stream, "glyph[%lu].ascend = %d\n", c, asg->ascend);
		fprintf( stream, "glyph[%lu].descend = %d\n", c, asg->descend );
		k = 0 ;
		fprintf( stream, "glyph[%lu].pixmap = {", c);
#if 1
		for( i = 0 ; i < asg->height*asg->width ; i++ )
		{
			if( asg->pixmap[k]&0x80 )
			{
				fprintf( stream, "%2.2X ", ((asg->pixmap[k]&0x7F)<<1));
			}else
			{
				int count = asg->pixmap[k]&0x3F;
				if( asg->pixmap[k]&0x40 )
					fprintf( stream, "FF(%d times) ", count+1 );
				else
					fprintf( stream, "00(%d times) ", count+1 );
				i += count ;
			}
		    k++;
		}
#endif
		fprintf( stream, "}\nglyph[%lu].used_memory = %d\n", c, k );
	}
}


#ifndef HAVE_XRENDER
Bool afterimage_uses_xrender(){ return False;}
	
void
draw_text_xrender(  ASVisual *asv, const void *text, ASFont *font, ASTextAttributes *attr, int length,
					int xrender_op, unsigned long	xrender_src, unsigned long xrender_dst,
					int	xrender_xSrc,  int xrender_ySrc, int xrender_xDst, int xrender_yDst )
{}
#else
Bool afterimage_uses_xrender(){ return True;}

void
draw_text_xrender(  ASVisual *asv, const void *text, ASFont *font, ASTextAttributes *attr, int length,
					int xrender_op, unsigned long xrender_src, unsigned long xrender_dst,
					int	xrender_xSrc,  int xrender_ySrc, int xrender_xDst, int xrender_yDst )
{
	static unsigned long next_xrender_gid = 1;

	ASGlyphMap map;
	ASGlyph **missing = NULL;
	int missing_count = 0;
	int missing_cap = 0;
	size_t images_size = 0;
	int max_height = 0;
	int i;
	int offset_3d_x = 0, offset_3d_y = 0;
	int space_size = 0, line_height = 0;
	int pen_x = 0, pen_y = 0;
	XRenderPictFormat *mask_fmt = NULL;
	int render_event_base = 0, render_error_base = 0;

	(void)render_event_base;
	(void)render_error_base;

	if( asv == NULL || asv->dpy == NULL || text == NULL || font == NULL || attr == NULL )
		return;

	if( !XRenderQueryExtension( asv->dpy, &render_event_base, &render_error_base ) )
		return;

	mask_fmt = XRenderFindStandardFormat( asv->dpy, PictStandardA8 );
	if( mask_fmt == NULL )
		return;

	if( !get_text_glyph_map( text, font, &map, attr, length) )
		return;
	
	if( map.width == 0 ) 
	{
		free_glyph_map( &map, True );
		return;
	}
	/* xrender code starts here : */
	/* Step 1: we have to make sure we have a valid GlyphSet */
	if( font->xrender_glyphset == 0 ) 
		font->xrender_glyphset = XRenderCreateGlyphSet( asv->dpy, mask_fmt );
	if( font->xrender_glyphset == 0 )
	{
		free_glyph_map( &map, True );
		return;
	}

	/* Step 2: we have to make sure all the glyphs are in GlyphSet */
	for( i = 0 ; map.glyphs[i] != GLYPH_EOT ; ++i )
		if( map.glyphs[i] > MAX_SPECIAL_GLYPH && map.glyphs[i]->xrender_gid == 0 )
		{
			ASGlyph *asg = map.glyphs[i];
			int stride = (asg->width + 3) & ~3; /* ZPixmap alignment for depth=8 */

			asg->xrender_gid = next_xrender_gid++;
			if( next_xrender_gid == 0 )
				next_xrender_gid = 1;

			if( missing_count >= missing_cap )
			{
				missing_cap = (missing_cap == 0) ? 64 : (missing_cap * 2);
				missing = saferealloc( missing, sizeof(*missing) * missing_cap );
			}
			missing[missing_count++] = asg;

			images_size += (size_t)stride * (size_t)asg->height;
			if( asg->height > max_height )
				max_height = asg->height;
		}
	
	if( missing_count > 0 )
		{
			Glyph *gids = safecalloc( missing_count, sizeof(Glyph) );
			XGlyphInfo *glyphs = safecalloc( missing_count, sizeof(XGlyphInfo) );
			char *images = NULL;
			char *images_ptr = NULL;
			CARD8 **scanlines = safecalloc( MAX(max_height, 1), sizeof(CARD8*) );
			int gi;

			images = safecalloc( 1, (images_size > 0) ? images_size : 1 );
			images_ptr = images;

		for( gi = 0 ; gi < missing_count ; ++gi )
		{
			ASGlyph *asg = missing[gi];
			int y;
			int stride = (asg->width + 3) & ~3; /* ZPixmap alignment for depth=8 */

			gids[gi] = (Glyph)asg->xrender_gid;
			glyphs[gi].width = asg->width;
			glyphs[gi].height = asg->height;
			glyphs[gi].x = asg->lead;
			glyphs[gi].y = -asg->ascend;
			glyphs[gi].xOff = asg->step;
			glyphs[gi].yOff = 0;

			for( y = 0 ; y < asg->height ; ++y )
				scanlines[y] = (CARD8*)images_ptr + (size_t)y * (size_t)stride;

			if( images_ptr && asg->pixmap && asg->width > 0 && asg->height > 0 )
				render_asglyph( scanlines, asg->pixmap, 0, 0, asg->width, asg->height, 0xFF );

			images_ptr += (size_t)stride * (size_t)asg->height;
		}

		XRenderAddGlyphs( asv->dpy, (GlyphSet)font->xrender_glyphset, gids, glyphs, missing_count, images, (int)images_size );

		free( gids );
		free( glyphs );
			free( images );
			free( scanlines );
		}

	/* Step 3: actually rendering text  : */
	apply_text_3D_type( attr->type, &offset_3d_x, &offset_3d_y );
	offset_3d_x += font->spacing_x ;
	offset_3d_y += font->spacing_y ;

	line_height = font->max_height + offset_3d_y;

	space_size  = font->space_size ;
	if( !get_flags( font->flags, ASF_Monospaced) )
		space_size  = (space_size>>1)+1 ;
	space_size += offset_3d_x;

	for( i = 0 ; map.glyphs[i] != GLYPH_EOT ; ++i )
	{
		if( map.glyphs[i] == GLYPH_EOL )
		{
			pen_x = 0;
			pen_y += line_height;
			continue;
		}

		if( map.glyphs[i] == GLYPH_SPACE )
		{
			pen_x += space_size;
			continue;
		}

		if( map.glyphs[i] == GLYPH_TAB )
		{
			if( !get_flags( attr->rendition_flags, ASTA_UseTabStops ) )
				pen_x += space_size * attr->tab_size;
			else
				pen_x = goto_tab_stop( attr, space_size, pen_x );
			continue;
		}

		if( map.glyphs[i] > MAX_SPECIAL_GLYPH )
		{
			ASGlyph *asg = map.glyphs[i];
			unsigned int gid32;
			Picture src_pic = (Picture)xrender_src;
			Picture dst_pic = (Picture)xrender_dst;

			pen_x += map.x_kerning[i];

			gid32 = (unsigned int)asg->xrender_gid;
			if( gid32 != 0 )
				XRenderCompositeString32( asv->dpy, xrender_op, src_pic, dst_pic, mask_fmt,
										  (GlyphSet)font->xrender_glyphset,
										  xrender_xSrc, xrender_ySrc,
										  xrender_xDst + pen_x, xrender_yDst + pen_y,
										  &gid32, 1 );

			pen_x += asg->step + offset_3d_x;
		}
	}

	/* xrender code ends here : */
	free_glyph_map( &map, True );
	if( missing )
		free( missing );
}

#endif


/*********************************************************************************/
/* The end !!!! 																 */
/*********************************************************************************/
