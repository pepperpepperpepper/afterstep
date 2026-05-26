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

static int NormalX, NormalY;
static unsigned int NormalWidth, NormalHeight;
/* Mirror Note :
 *
 * For the purpose of sizing/placing left and right sides and corners - we employ somewhat
 * twisted logic - we mirror sides over lt2rb diagonal in case of
 * vertical title orientation. That allows us to apply simple x/y switching instead of complex
 * calculations. Note that we only do that for placement purposes. Contexts and images are
 * still taken from MyFrame parts as if it was rotated counterclockwise instead of mirrored.
 */

ASOrientation HorzOrientation = {
	{C_FrameN, C_FrameE, C_FrameS, C_FrameW, C_FrameNW, C_FrameNE, C_FrameSW,
	 C_FrameSE},
	{FR_N, FR_E, FR_S, FR_W, FR_NW, FR_NE, FR_SW, FR_SE},
/*      N     E     S     W     NW    NE    SW    SE    TITLE */
	{FR_N, FR_E, FR_S, FR_W, FR_N, FR_N, FR_S, FR_S, FR_N},
	FR_N,
	{FR_NW, FR_NE},
	{FR_NW, FR_NE},
	FR_S,
	{FR_SW, FR_SE},
	{FR_SW, FR_SE},
	FR_W, FR_E,
	FR_W, FR_E,
	MYFRAME_HOR_MASK,
	TBTN_ORDER_L2R, TBTN_ORDER_L2R,
	&NormalX, &NormalY, &NormalWidth, &NormalHeight,
	&NormalX, &NormalY, &NormalWidth, &NormalHeight,
	0,
	{0, 1, 2, 3, 4, 5, 6},
	{0, 0, 0, 0, 0, 0, 0}
};

ASOrientation VertOrientation = {
	{C_FrameW, C_FrameN, C_FrameE, C_FrameS, C_FrameSW, C_FrameNW, C_FrameSE,
	 C_FrameNE},
	{FR_W, FR_N, FR_E, FR_S, FR_SW, FR_NW, FR_SE, FR_NE},
/*      N     E     S     W     NW    NE    SW    SE    TITLE */
	{FR_N, FR_E, FR_S, FR_W, FR_W, FR_E, FR_W, FR_E, FR_W},
	FR_W,
	{FR_SW, FR_NW},
	{FR_NW, FR_SW},
	FR_E,
	{FR_SE, FR_NE},
	{FR_NE, FR_SE},
	FR_S, FR_N,
	FR_N, FR_S,
	MYFRAME_VERT_MASK,
	TBTN_ORDER_B2T, TBTN_ORDER_B2T,
	&NormalX, &NormalY, &NormalWidth, &NormalHeight,
	&NormalY, &NormalX, &NormalHeight, &NormalWidth,
	FLIP_VERTICAL,
	{0, 0, 0, 0, 0, 0, 0},
	{7, 6, 5, 4, 3, 2, 1}
};

ASOrientation *get_orientation_data (ASWindow * asw)
{
	if (asw && asw->magic == MAGIC_ASWINDOW && asw->hints)
		return ASWIN_HFLAGS (asw,
												 AS_VerticalTitle) ? &VertOrientation :
				&HorzOrientation;
	return &HorzOrientation;
}


/***********************************************************************
 *  grab_aswindow_buttons - grab needed buttons for all of the windows
 *  for specified client
 ***********************************************************************/
void grab_aswindow_buttons (ASWindow * asw, Bool focused)
{
	if (asw) {
		Bool do_focus_grab = (get_flags (Scr.Feel.flags, ClickToFocus));
		if (do_focus_grab) {
			if (focused)
				ungrab_focus_click (asw->frame);
			else
				grab_focus_click (asw->frame);
		} else
			ungrab_window_buttons (asw->frame);

		if (!ASWIN_GET_FLAGS (asw, AS_Dead)) {
			ungrab_window_buttons (asw->w);
			grab_window_buttons (asw->w, C_WINDOW);
		}

		if (asw->icon_canvas && !ASWIN_GET_FLAGS (asw, AS_Dead)
				&& validate_drawable (asw->icon_canvas->w, NULL, NULL) != None) {
			ungrab_window_buttons (asw->icon_canvas->w);
			if (do_focus_grab) {
				if (focused)
					ungrab_focus_click (asw->icon_canvas->w);
				else
					grab_focus_click (asw->icon_canvas->w);
			}
			grab_window_buttons (asw->icon_canvas->w, C_ICON);
		}

		if (asw->icon_title_canvas
				&& asw->icon_title_canvas != asw->icon_canvas) {
			ungrab_window_buttons (asw->icon_title_canvas->w);
			if (do_focus_grab) {
				if (focused)
					ungrab_focus_click (asw->icon_title_canvas->w);
				else
					grab_focus_click (asw->icon_title_canvas->w);
			}
			grab_window_buttons (asw->icon_title_canvas->w, C_ICON);

		}
	}
	XSync (dpy, False);
}

/***********************************************************************
 *  grab_aswindow_keys - grab needed keys for the window
 ***********************************************************************/
void grab_aswindow_keys (ASWindow * asw)
{
	if (AS_ASSERT (asw))
		return;

	ungrab_window_keys (asw->frame);
	grab_window_keys (asw->frame,
										(C_WINDOW | C_TITLE | C_TButtonAll | C_FRAME));

	if (asw->icon_canvas) {
		ungrab_window_keys (asw->icon_canvas->w);
		grab_window_keys (asw->icon_canvas->w, (C_ICON));
	}
	if (asw->icon_title_canvas && asw->icon_title_canvas != asw->icon_canvas) {
		ungrab_window_keys (asw->icon_title_canvas->w);
		grab_window_keys (asw->icon_title_canvas->w, (C_ICON));
	}
	XSync (dpy, False);
}


/* this should set up proper feel settings - grab keyboard and mouse buttons :
 * Note: must be called after redecorate_window, since some of windows may have
 * been created at that time.
 */
void grab_window_input (ASWindow * asw, Bool release_grab)
{
	if (asw) {
		if (release_grab) {
			ungrab_window_keys (asw->frame);
			ungrab_window_buttons (asw->frame);
			if (asw->icon_canvas) {
				ungrab_window_keys (asw->icon_canvas->w);
				ungrab_window_buttons (asw->icon_canvas->w);
			}
			if (asw->icon_title_canvas
					&& asw->icon_title_canvas != asw->icon_canvas) {
				ungrab_window_keys (asw->icon_title_canvas->w);
				ungrab_window_buttons (asw->icon_title_canvas->w);
			}
			XSync (dpy, False);
		} else {
			grab_aswindow_buttons (asw, (Scr.Windows->focused == asw));
			grab_aswindow_keys (asw);
		}
	}
}


/****************************************************************************
 *
 * Checks the function "function", and sees if it
 * is an allowed function for window t,  according to the motif way of life.
 * This routine is used to decide if we should refuse to perform a function.
 *
 ****************************************************************************/
int check_allowed_function2 (int func, ASHints * hints)
{
	if (func == F_NOP)
		return 0;

	if (hints) {
		int mask = function2mask (func);
		if (func == F_SHADE)
			return get_flags (hints->flags, AS_Titlebar);
		else if (mask != 0)
			return get_flags (hints->function_mask, mask);
	}
	return 1;
}

/****************************************************************************
 *
 * Checks the function described in menuItem mi, and sees if it
 * is an allowed function for window Tmp_Win,
 * according to the motif way of life.
 *
 * This routine is used to determine whether or not to grey out menu items.
 *
 ****************************************************************************/
int check_allowed_function (FunctionData * fdata, ASHints * hints)
{
	int func = fdata->func;
	int i;
	ComplexFunction *cfunc;

	if (func != F_FUNCTION && func != F_CATEGORY)
		return check_allowed_function2 (func, hints);

	if (func == F_FUNCTION) {
		if ((cfunc = get_complex_function (fdata->name)) == NULL)
			return 0;

		for (i = 0; i < cfunc->items_num; ++i)
			if (cfunc->items[i].func == F_FUNCTION
					|| cfunc->items[i].func == F_CATEGORY) {
				if (check_allowed_function (&(cfunc->items[i]), hints) == 0)
					return 0;
			} else if (check_allowed_function2 (cfunc->items[i].func, hints) ==
								 0)
				return 0;
	} else if (func == F_CATEGORY) {
		if (name2desktop_category
				(fdata->text ? fdata->text : fdata->name, NULL) == NULL)
			return 0;
	}
	return 1;
}


ASFlagType compile_titlebuttons_mask (ASHints * hints)
{
	ASFlagType disabled_mask = hints->disabled_buttons;
	ASFlagType enabled_mask = 0;
	int i;

	LOCAL_DEBUG_OUT ("disabled mask from hints is 0x%lX", disabled_mask);

	for (i = 0; i < TITLE_BUTTONS; i++) {
		if (Scr.Look.buttons[i].unpressed.image != NULL) {
			int context = Scr.Look.buttons[i].context;
			MouseButton *func;
			Bool at_least_one_enabled = False;

			enabled_mask |= context;
			for (func = Scr.Feel.MouseButtonRoot; func != NULL;
					 func = (*func).NextButton)
				if ((func->Context & context) != 0)
					if (check_allowed_function (func->fdata, hints)) {
						at_least_one_enabled = True;
						break;
					}
			if (!at_least_one_enabled)
				disabled_mask |= Scr.Look.buttons[i].context;
		}
	}
	LOCAL_DEBUG_OUT
			("disabled mask(0x%lX)->enabled_mask(0x%lX)->button_mask(0x%lX)",
			 disabled_mask, enabled_mask, enabled_mask & (~disabled_mask));


	return enabled_mask & (~disabled_mask);
}

void
estimate_titlebar_size (ASHints * hints, unsigned int *width_ret,
												unsigned int *height_ret)
{
	unsigned int width = 0, height = 0;
	if (hints) {
		ASTBarData *tbar = create_astbar ();
		ASFlagType btn_mask;

		btn_mask = compile_titlebuttons_mask (hints);
		tbar->h_spacing = DEFAULT_TBAR_HSPACING;
		tbar->v_spacing = DEFAULT_TBAR_VSPACING;
		/* left buttons : */
		add_astbar_btnblock (tbar, 0, 0, 0, NO_ALIGN,
												 &(Scr.Look.ordered_buttons[0]), btn_mask,
												 Scr.Look.button_first_right,
												 Scr.Look.TitleButtonXOffset[0],
												 Scr.Look.TitleButtonYOffset[0],
												 Scr.Look.TitleButtonSpacing[0], TBTN_ORDER_L2R);

		/* label */
		add_astbar_label (tbar, 1, 0, 0, ALIGN_LEFT, DEFAULT_TBAR_HSPACING,
											DEFAULT_TBAR_VSPACING, hints->names[0],
											hints->names_encoding[0]);
		/* right buttons : */
		add_astbar_btnblock (tbar, 2, 0, 0, NO_ALIGN,
												 &(Scr.Look.
													 ordered_buttons[Scr.Look.button_first_right]),
												 btn_mask,
												 TITLE_BUTTONS - Scr.Look.button_first_right,
												 Scr.Look.TitleButtonXOffset[1],
												 Scr.Look.TitleButtonYOffset[1],
												 Scr.Look.TitleButtonSpacing[1], TBTN_ORDER_R2L);

		set_astbar_style_ptr (tbar, BAR_STATE_UNFOCUSED,
													Scr.Look.MSMenu[MENU_BACK_TITLE]);
		set_astbar_style_ptr (tbar, BAR_STATE_FOCUSED,
													Scr.Look.MSMenu[MENU_BACK_HITITLE]);
		width = calculate_astbar_width (tbar);
		height = calculate_astbar_height (tbar);
		destroy_astbar (&tbar);
	}
	if (width_ret)
		*width_ret = width;
	if (height_ret)
		*height_ret = height;
}
