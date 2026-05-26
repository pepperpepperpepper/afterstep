/***********************************************************************
 * Workspace/viewport AfterStep function handlers.
 ***********************************************************************/

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include "functions_internal.h"

static inline int
make_scroll_pos (int val, int unit, int curr, int max, int size)
{
	int pos = curr;
	if (val > -100000 && val < 100000) {
		pos += APPLY_VALUE_UNIT (size, val, unit);
		if (pos < 0)
			pos = 0;
		if (pos > max)
			pos = max;
	} else {
		pos += APPLY_VALUE_UNIT (size, val / 1000, unit);
		while (pos < 0)
			pos += max;
		while (pos > max)
			pos -= max;
	}
	return pos;
}

static inline int
make_edge_scroll (int curr_pos, int curr_view, int view_size, int max_view,
									int edge_scroll)
{
	int new_view = curr_view;
	edge_scroll = (edge_scroll * view_size) / 100;
	if (curr_pos >= new_view + view_size - 2) {
		while (curr_pos >= new_view + view_size - 2)
			new_view += edge_scroll;
		if (new_view > max_view)
			new_view = max_view;
	} else {
		while (curr_pos < new_view + 2)
			new_view -= edge_scroll;
		if (new_view < 0)
			new_view = 0;
	}
	return new_view;
}

void scroll_func_handler (FunctionData * data, ASEvent * event, int module)
{
#ifndef NO_VIRTUAL
	int x, y;
	register ScreenInfo *scr = event->scr;

	x = make_scroll_pos (data->func_val[0], data->unit_val[0], scr->Vx,
											 scr->VxMax, scr->MyDisplayWidth);
	y = make_scroll_pos (data->func_val[1], data->unit_val[1], scr->Vy,
											 scr->VyMax, scr->MyDisplayHeight);

	MoveViewport (x, y, True);
#endif
}

void movecursor_func_handler (FunctionData * data, ASEvent * event,
															int module)
{
	int curr_x, curr_y;
	int x, y;
	register ScreenInfo *scr = ASDefaultScr;

	ASQueryPointerRootXY (&curr_x, &curr_y);
	x = make_scroll_pos (data->func_val[0], data->unit_val[0],
											 scr->Vx + curr_x, scr->VxMax + scr->MyDisplayWidth,
											 scr->MyDisplayWidth);
	y = make_scroll_pos (data->func_val[1], data->unit_val[1],
											 scr->Vy + curr_y, scr->VyMax + scr->MyDisplayHeight,
											 scr->MyDisplayHeight);

#ifndef NO_VIRTUAL
	{
		int new_vx = 0, new_vy = 0;

		new_vx =
				make_edge_scroll (x, scr->Vx, scr->MyDisplayWidth, scr->VxMax,
													scr->Feel.EdgeScrollX);
		new_vy =
				make_edge_scroll (y, scr->Vy, scr->MyDisplayHeight, scr->VyMax,
													scr->Feel.EdgeScrollY);
		if (new_vx != scr->Vx || new_vy != scr->Vy)
			MoveViewport (new_vx, new_vy, True);
	}
#endif
	x -= scr->Vx;
	y -= scr->Vy;
	if (x != curr_x || y != curr_y)
		XWarpPointer (dpy, scr->Root, scr->Root, 0, 0, scr->MyDisplayWidth,
									scr->MyDisplayHeight, x, y);
}

void goto_page_func_handler (FunctionData * data, ASEvent * event,
														 int module)
{
#ifndef NO_VIRTUAL
	int newvx = data->func_val[0] * event->scr->MyDisplayWidth;
	int newvy = data->func_val[1] * event->scr->MyDisplayHeight;
	LOCAL_DEBUG_OUT ("val(%d,%d)->scr(%d,%d)->newv(%d,%d)",
									 (int)data->func_val[0], (int)data->func_val[1],
									 event->scr->MyDisplayWidth, event->scr->MyDisplayHeight,
									 newvx, newvy);
	MoveViewport (newvx, newvy, True);
#endif
}

void toggle_page_func_handler (FunctionData * data, ASEvent * event,
															 int module)
{
#ifndef NO_VIRTUAL
	if (get_flags (Scr.Feel.flags, DoHandlePageing))
		clear_flags (Scr.Feel.flags, DoHandlePageing);
	else
		set_flags (Scr.Feel.flags, DoHandlePageing);

	SendPacket (-1, M_TOGGLE_PAGING, 1,
							get_flags (Scr.Feel.flags, DoHandlePageing));
	check_screen_panframes (ASDefaultScr);
#endif
}

void desk_func_handler (FunctionData * data, ASEvent * event, int module)
{
	long new_desk;

	if (data->func_val[0] != 0)
		new_desk = Scr.CurrentDesk + data->func_val[0];
	else if (IsValidDesk (data->func_val[1]))
		new_desk = data->func_val[1];
	else
		return;
	ChangeDesks (new_desk);
}

void deskviewport_func_handler (FunctionData * data, ASEvent * event,
																int module)
{
	unsigned int new_desk1, new_desk2;
	int new_vx, new_vy, flags;
	int new_desk;

	if (data->text == NULL)
		return;
	if (parse_geometry
			(data->text, &new_vx, &new_vy, &new_desk1, &new_desk2,
			 &flags) == data->text)
		return;

	if (!get_flags (flags, XValue))
		new_vx = Scr.Vx;

	if (!get_flags (flags, YValue))
		new_vy = Scr.Vy;

	if (get_flags (flags, WidthValue))
		new_desk = new_desk1;
	else if (get_flags (flags, HeightValue))
		new_desk = new_desk2;
	else
		new_desk = Scr.CurrentDesk;

	ChangeDeskAndViewport (new_desk, new_vx, new_vy, False);
}
