/*
 * Copyright (c) 2002 Sasha Vasko <sasha@aftercode.net>
 * Copyright (c) 1998, 1999 Ethan Fischer <allanon@crystaltokyo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "../configure.h"

#undef LOCAL_DEBUG
#include "asapp.h"
#include "afterstep.h"
#include "parser.h"
#include "mystyle.h"
#include "screen.h"
#include "../libAfterImage/afterimage.h"

/* MyStyle merge/inherit, gradient/color parsing, bevel and text-image
 * helpers, split out of mystyle.c. */

/*
 * merges two styles, with the result in child
 * if override == False, will not override fields already set
 * if copy == True, copies instead of inheriting; this is important, because
 *   inherited members are deleted when their real parent is deleted; don't
 *   inherit if the parent style could go away before the child
 */
void mystyle_merge_font (MyStyle * style, MyFont * font, Bool override)
{
	if (override && get_flags (style->user_flags, F_FONT)) {
		unload_font (&style->font);
		clear_flags (style->user_flags, F_FONT);
		clear_flags (style->set_flags, F_FONT);
	}
	if (override || !get_flags (style->set_flags, F_FONT)) {
		set_string (&(style->font.name), mystrdup (font->name));
		style->font.as_font = dup_asfont (font->as_font);
		set_flags (style->user_flags, F_FONT);
		clear_flags (style->inherit_flags, F_FONT);
	}
}

void
mystyle_merge_styles (MyStyle * parent, MyStyle * child, Bool override,
											Bool copy)
{
	if (parent == NULL || child == NULL)
		return;
	if (parent->set_flags & F_FONT)
		mystyle_merge_font (child, &(parent->font), override);

	if (parent->set_flags & F_TEXTSTYLE) {
		if ((override == True) || !(child->set_flags & F_TEXTSTYLE)) {
			child->text_style = parent->text_style;

			if (copy == False) {
				child->user_flags &= ~F_TEXTSTYLE;
				child->inherit_flags |= F_TEXTSTYLE;
			} else {
				child->user_flags |= F_TEXTSTYLE;
				child->inherit_flags &= ~F_TEXTSTYLE;
			}
		}
	}
	if (parent->set_flags & F_FORECOLOR) {
		if ((override == True) || !(child->set_flags & F_FORECOLOR)) {
			if (override == True)
				child->texture_type = parent->texture_type;
			child->colors.fore = parent->colors.fore;
			if (copy == False) {
				child->user_flags &= ~F_FORECOLOR;
				child->inherit_flags |= F_FORECOLOR;
			} else {
				child->user_flags |= F_FORECOLOR;
				child->inherit_flags &= ~F_FORECOLOR;
			}
		}
	}
	if (parent->set_flags & F_BACKCOLOR) {
		if ((override == True) || !(child->set_flags & F_BACKCOLOR)) {
			child->colors.back = parent->colors.back;
			child->relief = parent->relief;
			if (copy == False) {
				child->user_flags &= ~F_BACKCOLOR;
				child->inherit_flags |= F_BACKCOLOR;
			} else {
				child->user_flags |= F_BACKCOLOR;
				child->inherit_flags &= ~F_BACKCOLOR;
			}
		}
	}
	if (parent->set_flags & F_SLICE) {
		if ((override == True) || !(child->set_flags & F_SLICE)) {
			child->slice_x_start = parent->slice_x_start;
			child->slice_x_end = parent->slice_x_end;
			child->slice_y_start = parent->slice_y_start;
			child->slice_y_end = parent->slice_y_end;
			if (copy == False) {
				child->user_flags &= ~F_SLICE;
				child->inherit_flags |= F_SLICE;
			} else {
				child->user_flags |= F_SLICE;
				child->inherit_flags &= ~F_SLICE;
			}
		}
	}
	if (parent->set_flags & F_BACKGRADIENT) {
		if ((override == True) || !(child->set_flags & F_BACKGRADIENT)) {
			if (override == True)
				child->texture_type = parent->texture_type;
			child->gradient = parent->gradient;
			if (copy == False) {
				child->user_flags &= ~F_BACKGRADIENT;
				child->inherit_flags |= F_BACKGRADIENT;
			} else {
				child->user_flags |= F_BACKGRADIENT;
				child->inherit_flags &= ~F_BACKGRADIENT;
			}
		}
	}
	if (parent->set_flags & F_BACKPIXMAP) {
		if ((override == True) && (child->user_flags & F_BACKPIXMAP)) {
			LOCAL_DEBUG_OUT ("calling mystyle_free_back_icon for style %p",
											 child);
			mystyle_free_back_icon (child);
		}
		if ((override == True) || !(child->set_flags & F_BACKPIXMAP)) {
			if (override == True)
				child->texture_type = parent->texture_type;
			if ((parent->texture_type == TEXTURE_TRANSPARENT ||
					 parent->texture_type == TEXTURE_TRANSPARENT_TWOWAY) &&
					(override == True ||
					 (child->texture_type != TEXTURE_TRANSPARENT
						&& child->texture_type != TEXTURE_TRANSPARENT_TWOWAY))) {
				child->tint = parent->tint;
			}
			if (!copy) {
				child->back_icon = parent->back_icon;
				clear_flags (child->user_flags, F_BACKPIXMAP | F_BACKTRANSPIXMAP);
				set_flags (child->inherit_flags, F_BACKPIXMAP);
				if (get_flags (parent->set_flags, F_BACKTRANSPIXMAP))
					set_flags (child->inherit_flags, F_BACKTRANSPIXMAP);
			} else {
				GC gc = create_visual_gc (ASDefaultVisual, ASDefaultRoot, 0, NULL);

				child->back_icon.pix =
						XCreatePixmap (dpy, ASDefaultRoot, parent->back_icon.width,
													 parent->back_icon.height,
													 ASDefaultVisual->visual_info.depth);
				XCopyArea (dpy, parent->back_icon.pix, child->back_icon.pix, gc, 0,
									 0, parent->back_icon.width, parent->back_icon.height, 0,
									 0);
				if (parent->back_icon.mask != None) {
					GC mgc = XCreateGC (dpy, parent->back_icon.mask, 0, NULL);

					child->back_icon.mask =
							XCreatePixmap (dpy, ASDefaultRoot, parent->back_icon.width,
														 parent->back_icon.height, 1);
					XCopyArea (dpy, parent->back_icon.mask, child->back_icon.mask,
										 mgc, 0, 0, parent->back_icon.width,
										 parent->back_icon.height, 0, 0);
					XFreeGC (dpy, mgc);
				}
				if (parent->back_icon.alpha != None) {
					GC mgc = XCreateGC (dpy, parent->back_icon.alpha, 0, NULL);

					child->back_icon.alpha =
							XCreatePixmap (dpy, ASDefaultRoot, parent->back_icon.width,
														 parent->back_icon.height, 8);
					XCopyArea (dpy, parent->back_icon.alpha, child->back_icon.alpha,
										 mgc, 0, 0, parent->back_icon.width,
										 parent->back_icon.height, 0, 0);
					XFreeGC (dpy, mgc);
				}
				if (parent->back_icon.image)
					child->back_icon.image = dup_asimage (parent->back_icon.image);
				else
					child->back_icon.image = 0;
				child->back_icon.width = parent->back_icon.width;
				child->back_icon.height = parent->back_icon.height;
				child->user_flags |=
						F_BACKPIXMAP | (parent->set_flags & F_BACKTRANSPIXMAP);
				child->inherit_flags &= ~(F_BACKPIXMAP | F_BACKTRANSPIXMAP);
				XFreeGC (dpy, gc);
			}
		}
	}
	if (parent->set_flags & F_DRAWTEXTBACKGROUND) {
		if ((override == True) || !(child->set_flags & F_DRAWTEXTBACKGROUND)) {
			child->flags &= ~F_DRAWTEXTBACKGROUND;
			child->flags |= parent->flags & F_DRAWTEXTBACKGROUND;
			if (copy == False) {
				child->user_flags &= ~F_DRAWTEXTBACKGROUND;
				child->inherit_flags |= F_DRAWTEXTBACKGROUND;
			} else {
				child->user_flags |= F_DRAWTEXTBACKGROUND;
				child->inherit_flags &= ~F_DRAWTEXTBACKGROUND;
			}
		}
	}
	if (parent->set_flags & F_OVERLAY) {
		if ((override == True) || !(child->set_flags & F_OVERLAY)) {
			child->flags &= ~F_OVERLAY;
			child->flags |= parent->flags & F_OVERLAY;
			child->overlay = parent->overlay;
			child->overlay_type = parent->overlay_type;
			if (copy == False) {
				child->user_flags &= ~F_OVERLAY;
				child->inherit_flags |= F_OVERLAY;
			} else {
				child->user_flags |= F_OVERLAY;
				child->inherit_flags &= ~F_OVERLAY;
			}
		}
	}
	child->set_flags = child->user_flags | child->inherit_flags;
}

/*
 * convert an old two-color gradient to a multi-point gradient
 */
int
mystyle_parse_old_gradient (int type, ARGB32 c1, ARGB32 c2,
														ASGradient * gradient)
{
	int cylindrical = 0;

	switch (type) {
	case TEXTURE_GRADIENT:
		type = TEXTURE_GRADIENT_TL2BR;
		break;
	case TEXTURE_HGRADIENT:
		type = TEXTURE_GRADIENT_T2B;
		break;
	case TEXTURE_HCGRADIENT:
		type = TEXTURE_GRADIENT_T2B;
		cylindrical = 1;
		break;
	case TEXTURE_VGRADIENT:
		type = TEXTURE_GRADIENT_L2R;
		break;
	case TEXTURE_VCGRADIENT:
		type = TEXTURE_GRADIENT_L2R;
		cylindrical = 1;
		break;
	default:
		break;
	}
	if (gradient) {
		gradient->npoints = 2 + cylindrical;
		gradient->color = NEW_ARRAY (ARGB32, gradient->npoints);
		gradient->offset = NEW_ARRAY (double, gradient->npoints);

		gradient->color[0] = c1;
		gradient->color[1] = c2;
		if (cylindrical)
			gradient->color[2] = c1;
		gradient->offset[0] = 0.0;
		if (cylindrical)
			gradient->offset[1] = 0.5;
		gradient->offset[gradient->npoints - 1] = 1.0;
		gradient->type = mystyle_translate_grad_type (type);
	}
	return type;
}

void
mystyle_merge_colors (MyStyle * style, int type, char *fore, char *back,
											char *gradient, char *pixmap)
{
	if (style == NULL)
		return;
	if ((fore != NULL) && !((*style).user_flags & F_FORECOLOR)) {
		if (parse_argb_color (fore, &((*style).colors.fore)) != fore)
			(*style).user_flags |= F_FORECOLOR;
	}
	if ((back != NULL) && !((*style).user_flags & F_BACKCOLOR)) {
		if (parse_argb_color (back, &((*style).colors.back)) != back) {
			(*style).relief.fore = GetHilite ((*style).colors.back);
			(*style).relief.back = GetShadow ((*style).colors.back);
			(*style).user_flags |= F_BACKCOLOR;
		}
	}
	if (type >= 0) {
		switch (type) {
		case TEXTURE_GRADIENT:
			style->texture_type = TEXTURE_GRADIENT_TL2BR;
			break;
		case TEXTURE_HGRADIENT:
			style->texture_type = TEXTURE_GRADIENT_L2R;
			break;
		case TEXTURE_HCGRADIENT:
			style->texture_type = TEXTURE_GRADIENT_L2R;
			break;
		case TEXTURE_VGRADIENT:
			style->texture_type = TEXTURE_GRADIENT_T2B;
			break;
		case TEXTURE_VCGRADIENT:
			style->texture_type = TEXTURE_GRADIENT_T2B;
			break;
		default:
			style->texture_type = type;
			break;
		}
	}
	if ((type > 0) && (type < TEXTURE_PIXMAP)
			&& !((*style).user_flags & F_BACKGRADIENT)) {
		if (gradient != NULL) {
			ARGB32 c1, c2 = 0;
			ASGradient grad;
			char *ptr;

			ptr = (char *)parse_argb_color (gradient, &c1);
			parse_argb_color (ptr, &c2);
			if (ptr != gradient
					&& (type =
							mystyle_parse_old_gradient (type, c1, c2, &grad)) >= 0) {
				if (style->user_flags & F_BACKGRADIENT) {
					free (style->gradient.color);
					free (style->gradient.offset);
				}
				style->gradient = grad;
				grad.type = mystyle_translate_grad_type (type);
				style->texture_type = type;
				style->user_flags |= F_BACKGRADIENT;
			} else
				show_error ("bad gradient definition in look file: %s", gradient);
		}
	} else if ((type == TEXTURE_PIXMAP)
						 && !get_flags (style->user_flags, F_BACKPIXMAP)) {
		if (pixmap != NULL) {
/* treat second parameter as an image filename : */
			if (load_icon
					(&(style->back_icon), pixmap, ASDefaultScr->image_manager)) {
				style->texture_type = type;
				set_flags (style->user_flags, F_BACKPIXMAP);
			} else
				show_error ("failed to load image file \"%s\" in MyStyle \"%s\".",
										pixmap, style->name);
		}
	}
	(*style).set_flags = (*style).user_flags | (*style).inherit_flags;
}

void mystyle_inherit_font (MyStyle * style, MyFont * font)
{
	/* NOTE: these should have inherit_flags set, so the font is only
	 *       unloaded once */
	if (style != NULL && !(style->set_flags & F_FONT)) {
		set_string (&(style->font.name), mystrdup (font->name));
		style->font.as_font = dup_asfont (font->as_font);
		clear_flags (style->inherit_flags, F_FONT);
		set_flags (style->user_flags, F_FONT);	/* to prevent confusion */
		style->set_flags = style->user_flags | style->inherit_flags;
	}
}


ASImageBevel *mystyle_make_bevel (MyStyle * style, ASImageBevel * bevel,
																	int hilite, Bool reverse)
{
	if (style && (hilite & HILITE_MASK) != 0 &&
			(hilite & (NO_HILITE_INLINE | NO_HILITE_OUTLINE)) !=
			(NO_HILITE_INLINE | NO_HILITE_OUTLINE)) {
		int extra_hilite = get_flags (hilite, EXTRA_HILITE) ? 2 : 0;

		if (bevel == NULL)
			bevel = safecalloc (1, sizeof (ASImageBevel));
		else
			memset (bevel, 0x00, sizeof (ASImageBevel));
		if (reverse != 0) {
			bevel->lo_color = style->relief.fore;
			bevel->lolo_color = GetHilite (style->relief.fore);
			bevel->hi_color = style->relief.back;
			bevel->hihi_color = GetShadow (style->relief.back);
		} else {
			bevel->hi_color = style->relief.fore;
			bevel->hihi_color = GetHilite (style->relief.fore);
			bevel->lo_color = style->relief.back;
			bevel->lolo_color = GetShadow (style->relief.back);
		}
		bevel->hilo_color = GetAverage (bevel->hi_color, bevel->lo_color);
#if 1
		if (!get_flags (hilite, NO_HILITE_OUTLINE)) {
			if (get_flags (hilite, NORMAL_HILITE)) {
				bevel->left_outline = bevel->top_outline = bevel->right_outline =
						bevel->bottom_outline = 1;
				bevel->left_inline = bevel->top_inline = bevel->right_inline =
						bevel->bottom_inline =
						extra_hilite + get_flags (hilite, NO_HILITE_INLINE) ? 0 : 1;
			} else {
#ifndef DONT_HILITE_PLAIN
				bevel->left_inline = bevel->top_inline = bevel->right_inline =
						bevel->bottom_inline = extra_hilite;
#endif
			}
		}
#endif
		if (get_flags (hilite, LEFT_HILITE)) {
			bevel->left_outline++;
			if (!get_flags (hilite, NO_HILITE_INLINE))
				bevel->left_inline++;
			if (get_flags (hilite, NO_HILITE_OUTLINE)) {
				bevel->left_outline++;
				bevel->left_inline += extra_hilite;
			}
		}
		if (get_flags (hilite, TOP_HILITE)) {
			bevel->top_outline++;
			if (!get_flags (hilite, NO_HILITE_INLINE))
				bevel->top_inline++;
			if (get_flags (hilite, NO_HILITE_OUTLINE))
				bevel->top_inline += extra_hilite;
		}
		if (get_flags (hilite, RIGHT_HILITE)) {
			bevel->right_outline++;
			if (!get_flags (hilite, NO_HILITE_INLINE))
				bevel->right_inline++;
			if (get_flags (hilite, NO_HILITE_OUTLINE))
				bevel->right_inline += extra_hilite;
		}
		if (get_flags (hilite, BOTTOM_HILITE)) {
			bevel->bottom_outline++;
			if (!get_flags (hilite, NO_HILITE_INLINE))
				bevel->bottom_inline++;
			if (get_flags (hilite, NO_HILITE_OUTLINE))
				bevel->bottom_inline += extra_hilite;
		}
/* experimental code */
#if 1
		if (!get_flags (hilite, NO_HILITE_INLINE)) {
			if (bevel->top_outline > 1) {
				bevel->top_inline += bevel->top_outline - 1;
				bevel->top_outline = 1;
			}
			if (bevel->left_outline > 1) {
				bevel->left_inline += bevel->left_outline - 1;
				bevel->left_outline = 1;
			}
			if (bevel->right_outline > 1) {
				bevel->right_inline += bevel->right_outline - 1;
				bevel->right_outline = 1;
			}
			if (bevel->bottom_outline > 1) {
				bevel->bottom_inline += bevel->bottom_outline - 1;
				bevel->bottom_outline = 1;
			}
		}
#endif
	} else if (bevel)
		memset (bevel, 0x00, sizeof (ASImageBevel));

	return bevel;
}

ASImage *mystyle_draw_text_image (MyStyle * style, const char *text,
																	unsigned long encoding)
{
	ASImage *im = NULL;

	if (style && text) {
		/* load fonts on demand only - no need to waste memory if ain't never gonna use it ! */
		if (style->font.as_font == NULL)
			load_font (NULL, &style->font);

		if (style->font.as_font) {
			ASTextAttributes attr =
					{ ASTA_VERSION_1, ASTA_UseTabStops, AST_Plain, ASCT_Char, 8, 0,
				NULL, 0, ARGB32_White
			};

			attr.type = style->text_style;
			attr.fore_color = style->colors.fore;

			switch (encoding) {
			case AS_Text_ASCII:
				attr.char_type = ASCT_Char;
				break;
			case AS_Text_UTF8:
				attr.char_type = ASCT_UTF8;
				break;
			case AS_Text_UNICODE:
				attr.char_type = ASCT_Unicode;
				break;
			}

			im = draw_fancy_text (text, style->font.as_font, &attr, 100, 0);

			LOCAL_DEBUG_OUT ("encoding is %ld, im is %p, back_color is %lX",
											 encoding, im, style->colors.fore);
			if (im) {
				im->back_color = style->colors.fore;
			}
		}
	}
	return im;
}

unsigned int mystyle_get_font_height (MyStyle * style)
{
	if (style) {
		if (style->font.as_font == NULL)
			load_font (NULL, &style->font);

		if (style->font.as_font)
			return style->font.as_font->max_height;
	}
	return 1;
}

void
mystyle_get_text_size (MyStyle * style, const char *text,
											 unsigned int *width, unsigned int *height)
{
	if (style && text) {
		if (style->font.as_font == NULL)
			load_font (NULL, &style->font);

		if (style->font.as_font)
			get_text_size (text, style->font.as_font, style->text_style, width,
										 height);
	}
}
