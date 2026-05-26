/*
 * Copyright (C) 2003 Sasha Vasko
 * Copyright (C) 1996 Frank Fejes
 * Copyright (C) 1996 Alfredo Kojima
 * Copyright (C) 1995 Bo Yang
 * Copyright (C) 1993 Robert Nation
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

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"
#include "../../libAfterConf/afterconf.h"

#include "decorations_internal.h"

/* this gets called when Look changes or hints changes : */
ASCanvas *check_side_canvas (ASWindow * asw, FrameSide side,
																		Bool required)
{
	ASCanvas *canvas = asw->frame_sides[side];
	Window w;
	LOCAL_DEBUG_CALLER_OUT ("asw = %p, side = %d, required = %d", asw, side,
													required);
	if (required) {
		if (canvas == NULL) {				/* create canvas here */
			unsigned long valuemask;
			XSetWindowAttributes attributes;

			valuemask = CWBorderPixel | CWEventMask;
			attributes.border_pixel = Scr.asv->black_pixel;
			if (Scr.Feel.flags & BackingStore) {
				valuemask |= CWBackingStore;
				attributes.backing_store = WhenMapped;
			}
			attributes.event_mask = AS_CANVAS_EVENT_MASK;

			w = create_visual_window (Scr.asv, asw->frame,
																0, 0, 1, 1, 0, InputOutput,
																valuemask, &attributes);
			register_aswindow (w, asw);
			canvas = create_ascanvas (w);
			LOCAL_DEBUG_OUT
					("++CREAT Client(%lx(%s))->side(%d)->canvas(%p)->window(%lx)",
					 asw->w, ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname", side,
					 canvas, canvas->w);
		} else
			invalidate_canvas_config (canvas);
	} else if (canvas != NULL) {	/* destroy canvas here */
		w = canvas->w;
		LOCAL_DEBUG_OUT
				("--DESTR Client(%lx(%s))->side(%d)->canvas(%p)->window(%lx)",
				 asw->w, ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname", side,
				 canvas, canvas->w);
		destroy_ascanvas (&canvas);
		destroy_registered_window (w);
	}

	return (asw->frame_sides[side] = canvas);
}

/* creating/destroying our main frame window : */
ASCanvas *check_frame_canvas (ASWindow * asw, Bool required)
{
	ASCanvas *canvas = asw->frame_canvas;
	Window w;

	if (required) {
		if (canvas == NULL) {				/* create canvas here */
			unsigned long valuemask;
			XSetWindowAttributes attributes;
			int bw = 0;

			/* create windows */
			valuemask = CWBorderPixel | CWCursor | CWEventMask;
			if (Scr.asv->visual_info.visual == DefaultVisual (dpy, Scr.screen)) {
				/* only if root has same depth and visual as us! */
				attributes.background_pixmap = ParentRelative;
				valuemask |= CWBackPixmap;
			}
			attributes.border_pixel = Scr.asv->black_pixel;
			attributes.cursor = Scr.Feel.cursors[ASCUR_Default];
			attributes.event_mask = AS_FRAME_EVENT_MASK;

			if (get_flags (Scr.Feel.flags, SaveUnders)) {
				valuemask |= CWSaveUnder;
				attributes.save_under = True;
			}
			if (asw->hints && get_flags (asw->hints->flags, AS_Border))
				bw = asw->hints->border_width;
			asw->status->frame_border_width = bw;
			w = create_visual_window (Scr.asv,
																(ASWIN_DESK (asw) ==
																 Scr.CurrentDesk) ? Scr.Root : Scr.
																ServiceWin, asw->status->x, asw->status->y,
																asw->status->width, asw->status->height,
																bw, InputOutput, valuemask, &attributes);
			XLowerWindow (dpy, w);
			asw->frame = w;
			register_aswindow (w, asw);
			canvas = create_ascanvas_container (w);
			LOCAL_DEBUG_OUT
					("++CREAT Client(%lx(%s))->FRAME->canvas(%p)->window(%lx)->parent(%lx)",
					 asw->w, ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname", canvas,
					 canvas->w,
					 (ASWIN_DESK (asw) ==
						Scr.CurrentDesk) ? Scr.Root : Scr.ServiceWin);
		} else
			invalidate_canvas_config (canvas);
	} else if (canvas != NULL) {	/* destroy canvas here */
		w = canvas->w;
		LOCAL_DEBUG_OUT
				("--DESTR Client(%lx(%s))->FRAME->canvas(%p)->window(%lx)", asw->w,
				 ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname", canvas,
				 canvas->w);
		destroy_ascanvas (&canvas);
		destroy_registered_window (w);
	}

	return (asw->frame_canvas = canvas);
}

/* creating/destroying container canvas for our client's window : */
ASCanvas *check_client_canvas (ASWindow * asw, Bool required)
{
	ASCanvas *canvas = asw->client_canvas;
	Window w;

	if (required) {
		if (canvas == NULL) {				/* create canvas here */
			unsigned long valuemask;
			XSetWindowAttributes attributes;

			if (asw->frame == None || (w = asw->w) == None)
				return NULL;

			attributes.event_mask = AS_CLIENT_EVENT_MASK;
			if (asw->internal) {
				XWindowAttributes internal_attr;
				XGetWindowAttributes (dpy, w, &internal_attr);
				attributes.event_mask |= internal_attr.your_event_mask;
			}
			quietly_reparent_window (w, asw->frame, 0, 0, attributes.event_mask);

			valuemask = (CWEventMask | CWDontPropagate);
			attributes.do_not_propagate_mask =
					ButtonPressMask | ButtonReleaseMask;

			if (get_flags (Scr.Feel.flags, AppsBackingStore)) {
				valuemask |= CWBackingStore;
				attributes.backing_store = WhenMapped;
			}
			XChangeWindowAttributes (dpy, w, valuemask, &attributes);
			if (asw->internal == NULL)
				XAddToSaveSet (dpy, w);

			register_aswindow (w, asw);
			canvas = create_ascanvas_container (w);
			if (ASWIN_GET_FLAGS (asw, AS_Shaped))
				refresh_container_shape (canvas);
			LOCAL_DEBUG_OUT
					("++CREAT Client(%lx(%s))->CLIENT->canvas(%p)->window(%lx)",
					 asw->w, ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname", canvas,
					 canvas->w);
		}
		/*else    invalidate_canvas_config( canvas ); */
	} else if (canvas != NULL) {	/* destroy canvas here */
		XWindowChanges xwc;					/* our withdrawn geometry */
		ASStatusHints withdrawn_status = { 0 };
		register int i = 0;

		w = canvas->w;

		/* calculating the withdrawn location */
		if (asw->status)
			withdrawn_status = *(asw->status);

		xwc.border_width = withdrawn_status.border_width;

		for (i = 0; i < FRAME_SIDES; ++i)
			withdrawn_status.frame_size[i] = 0;
		clear_flags (withdrawn_status.flags, AS_Shaded | AS_Iconic);
		anchor2status (&withdrawn_status, asw->hints, &(asw->anchor));
		xwc.x = withdrawn_status.x;
		xwc.y = withdrawn_status.y;
		xwc.width = withdrawn_status.width;
		xwc.height = withdrawn_status.height;

		LOCAL_DEBUG_OUT
				("--DESTR Client(%lx(%s))->CLIENT->canvas(%p)->window(%lx)",
				 asw->w, ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname", canvas,
				 canvas->w);
		destroy_ascanvas (&canvas);

		/*
		 * Prevent the receipt of an UnmapNotify in case we are simply restarting,
		 * since that would cause a transition to the Withdrawn state.
		 */
#if 1
		if ((asw->status == NULL || !get_flags (asw->status->flags, AS_Dead))
				&& get_parent_window (w) == asw->frame) {
			LOCAL_DEBUG_OUT ("reparenting client window %lX", w);
			if (get_flags (AfterStepState, ASS_Shutdown))
				quietly_reparent_window (w, Scr.Root, xwc.x, xwc.y,
																 AS_CLIENT_EVENT_MASK);
			else
				XReparentWindow (dpy, w, Scr.Root, xwc.x, xwc.y);
			/* WE have to restore window's withdrawn location now. */
			XConfigureWindow (dpy, w,
												CWX | CWY | CWWidth | CWHeight | CWBorderWidth,
												&xwc);
		}
		XSync (dpy, 0);
#endif
	}

	asw->client_canvas = canvas;
	return canvas;
}

/* creating/destroying our icon window : */
ASCanvas *check_icon_canvas (ASWindow * asw, Bool required)
{
	ASCanvas *canvas = asw->icon_canvas;
	Window w;

	if (required) {
		if (canvas == NULL) {				/* create canvas here */
			unsigned long valuemask;
			XSetWindowAttributes attributes;

			valuemask = CWBorderPixel | CWCursor | CWEventMask;
			attributes.border_pixel = Scr.asv->black_pixel;
			attributes.cursor = Scr.Feel.cursors[ASCUR_Default];

			if ((get_flags (asw->hints->client_icon_flags, AS_ClientIcon | AS_ClientIconPixmap) != AS_ClientIcon) || asw->hints == NULL || asw->hints->icon.window == None || !get_flags (Scr.Feel.flags, KeepIconWindows)) {	/* create windows */
				attributes.event_mask = AS_ICON_TITLE_EVENT_MASK;
				w = create_visual_window (Scr.asv,
																	(ASWIN_DESK (asw) ==
																	 Scr.CurrentDesk) ? Scr.Root : Scr.
																	ServiceWin, -9000, -9000, 1, 1, 0,
																	InputOutput, valuemask, &attributes);
				canvas = create_ascanvas (w);
			} else {									/* reuse client's provided window */
				attributes.event_mask = AS_ICON_EVENT_MASK;
				w = asw->hints->icon.window;
				XChangeWindowAttributes (dpy, w, valuemask, &attributes);
				canvas = create_ascanvas_container (w);
			}
			LOCAL_DEBUG_OUT
					("++CREAT Client(%lx(%s))->ICON->canvas(%p)->window(%lx)",
					 asw->w, ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname", canvas,
					 canvas->w);
			register_aswindow (w, asw);
		} else
			invalidate_canvas_config (canvas);
	} else if (canvas != NULL) {	/* destroy canvas here */
		w = canvas->w;
		LOCAL_DEBUG_OUT
				("--DESTR Client(%lx(%s))->ICON->canvas(%p)->window(%lx)", asw->w,
				 ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname", canvas,
				 canvas->w);
		if (asw->icon_title_canvas == canvas)
			asw->icon_title_canvas = NULL;
		destroy_ascanvas (&canvas);
		if (asw->hints && asw->hints->icon.window == w)
			unregister_aswindow (w);
		else
			destroy_registered_window (w);
	}
	asw->icon_canvas = canvas;

	return canvas;
}

/* creating/destroying our icon title window : */
ASCanvas *check_icon_title_canvas (ASWindow * asw, Bool required,
																					Bool reuse_icon_canvas)
{
	ASCanvas *canvas = asw->icon_title_canvas;
	Window w;

	if (!required) {
		if (canvas && canvas != asw->icon_canvas) {
			w = canvas->w;
			LOCAL_DEBUG_OUT
					("--DESTR Client(%lx(%s))->ICONT->canvas(%p)->window(%lx)",
					 asw->w, ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname", canvas,
					 canvas->w);
			destroy_ascanvas (&canvas);
			destroy_registered_window (w);
		}
	} else {											/* if( required ) */

		if (reuse_icon_canvas)
			canvas = asw->icon_canvas;
		else if (canvas == NULL) {	/* create canvas here */
			unsigned long valuemask;
			XSetWindowAttributes attributes;

			valuemask = CWBorderPixel | CWCursor | CWEventMask;
			attributes.border_pixel = Scr.asv->black_pixel;
			attributes.cursor = Scr.Feel.cursors[ASCUR_Default];
			attributes.event_mask = AS_ICON_TITLE_EVENT_MASK;
			/* create windows */
			w = create_visual_window (Scr.asv,
																(ASWIN_DESK (asw) ==
																 Scr.CurrentDesk) ? Scr.Root : Scr.
																ServiceWin, -9010, -9010, 1, 1, 0,
																InputOutput, valuemask, &attributes);

			register_aswindow (w, asw);
			canvas = create_ascanvas (w);
			LOCAL_DEBUG_OUT
					("++CREAT Client(%lx(%s))->ICONT->canvas(%p)->window(%lx)",
					 asw->w, ASWIN_NAME (asw) ? ASWIN_NAME (asw) : "noname", canvas,
					 canvas->w);
		} else
			invalidate_canvas_config (canvas);
	}

	asw->icon_title_canvas = canvas;

	return canvas;
}

/* pointers should point to valid values preset to 0, im.width, 0, im.height */
void
geometry2slicing (ASGeometry g, int *pxs, int *pxe, int *pys, int *pye)
{
	if (get_flags (g.flags, XValue))
		*pxs = get_flags (g.flags, XNegative) ? *pxe + g.x : g.x;
	if (get_flags (g.flags, YValue))
		*pys = get_flags (g.flags, YNegative) ? *pye + g.y : g.y;

	if (get_flags (g.flags, WidthValue))
		*pxe = *pxs + g.width;
	if (get_flags (g.flags, HeightValue))
		*pye = *pys + g.height;
}


ASTBarData *check_tbar (ASTBarData ** tbar, Bool required,
															 const char *mystyle_name, ASImage * img,
															 unsigned short back_w,
															 unsigned short back_h, int flip,
															 ASFlagType align, ASFlagType fbevel,
															 ASFlagType ubevel, unsigned char fcm,
															 unsigned char ucm, int context,
															 ASGeometry * slicing)
{
	if (required) {
		if (*tbar == NULL) {
			*tbar = create_astbar ();
			LOCAL_DEBUG_OUT ("++CREAT tbar(%p)->context(%s)", *tbar,
											 context2text (context));
		} else
			delete_astbar_tile (*tbar, -1);

		set_astbar_flip (*tbar, flip);

		invalidate_astbar_style (*tbar, -1);
		set_astbar_style (*tbar, BAR_STATE_FOCUSED, mystyle_name);
		set_astbar_style (*tbar, BAR_STATE_UNFOCUSED, "default");
		if (img) {
			LOCAL_DEBUG_OUT ("adding bar icon %p %ux%u", img, img->width,
											 img->height);
			if (slicing && slicing->flags) {
				int xs = 0, xe = 0, ys = img->width, ye = img->height;
				geometry2slicing (*slicing, &xs, &xe, &ys, &ye);
				add_astbar_image (*tbar, 0, 0, flip, align, img, xs, xe, ys, ye);
			} else
				add_astbar_icon (*tbar, 0, 0, flip, align, img);
			if (back_w == 0)
				back_w =
						get_flags (flip, FLIP_VERTICAL) ? img->height : img->width;
			if (back_h == 0)
				back_h =
						get_flags (flip, FLIP_VERTICAL) ? img->width : img->height;
		}

		set_astbar_hilite (*tbar, BAR_STATE_FOCUSED, fbevel);
		set_astbar_hilite (*tbar, BAR_STATE_UNFOCUSED, ubevel);
		set_astbar_composition_method (*tbar, BAR_STATE_FOCUSED, fcm);
		set_astbar_composition_method (*tbar, BAR_STATE_UNFOCUSED, ucm);
		set_astbar_size (*tbar, (back_w == 0) ? 1 : back_w,
										 (back_h == 0) ? 1 : back_h);
		(*tbar)->context = context;
	} else if (*tbar) {
		destroy_astbar (tbar);
	}
	return *tbar;
}

/******************************************************************************/
/* now externally available interfaces to the above functions :               */
/******************************************************************************/
void invalidate_window_icon (ASWindow * asw)
{
	if (asw)
		check_icon_canvas (asw, False);
}

void invalidate_window_mystyles (ASWindow * asw)
{
	int i;

	for (i = 0; i < FRAME_PARTS; ++i)
		if (asw->frame_bars[i])
			invalidate_astbar_style (asw->frame_bars[i], -1);
	if (asw->tbar)
		invalidate_astbar_style (asw->tbar, -1);
	if (asw->icon_button)
		invalidate_astbar_style (asw->icon_button, -1);
	if (asw->icon_title)
		invalidate_astbar_style (asw->icon_title, -1);
}
