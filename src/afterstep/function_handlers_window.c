/***********************************************************************
 * Window-related AfterStep function handlers.
 ***********************************************************************/

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include "../../libAfterStep/moveresize.h"

#include "functions_internal.h"

void moveresize_func_handler (FunctionData * data, ASEvent * event,
															int module)
{																/* gotta have a window */
	ASWindow *asw = event->client;
	if (asw == NULL)
		return;

	if (!is_interactive_action (data)) {
		int new_val1 = 0, new_val2 = 0;
		int x = asw->status->x;
		int y = asw->status->y;
		int width = asw->status->width;
		int height = asw->status->height;

		new_val1 =
				APPLY_VALUE_UNIT (Scr.MyDisplayWidth, data->func_val[0],
													data->unit_val[0]);
		LOCAL_DEBUG_OUT ("val1 = %d, unit1 = %d, new_val1 = %d",
										 (int)data->func_val[0], (int)data->unit_val[0],
										 new_val1);
		new_val2 =
				APPLY_VALUE_UNIT (Scr.MyDisplayHeight, data->func_val[1],
													data->unit_val[1]);
		if (data->func == F_MOVE) {
			if (data->func_val[0] != INVALID_POSITION) {
				x = new_val1;
				if (!ASWIN_GET_FLAGS (asw, AS_Sticky))
					x -= asw->status->viewport_x;
			}
			if (data->func_val[1] != INVALID_POSITION) {
				y = new_val2;
				if (!ASWIN_GET_FLAGS (asw, AS_Sticky))
					y -= asw->status->viewport_y;
			}
		} else {
			if (data->func_val[0] != INVALID_POSITION)
				width = new_val1;
			if (data->func_val[1] != INVALID_POSITION)
				height = new_val2;
		}
		moveresize_aswindow_wm (asw, x, y, width, height, False);

	} else {
		ASMoveResizeData *mvrdata;
		/*release_pressure(); */
		if (data->func == F_MOVE) {
			mvrdata = move_widget_interactively (Scr.RootCanvas,
																					 asw->frame_canvas,
																					 event,
																					 apply_aswindow_move,
																					 complete_aswindow_move);
		} else {
			int side = 0;
			register unsigned long context = (event->context & C_FRAME);

			if (ASWIN_GET_FLAGS (asw, AS_Shaded)) {
				XBell (dpy, Scr.screen);
				return;
			}

			while ((0x01 & context) == 0 && side <= FR_SE) {
				++side;
				context = context >> 1;
			}

			if (side > FR_SE) {
				int pointer_x = 0, pointer_y = 0;
				ASQueryPointerRootXY (&pointer_x, &pointer_y);
				if (pointer_x >
						asw->frame_canvas->root_x + asw->frame_canvas->width / 2) {
					if (pointer_y >
							asw->frame_canvas->root_y + asw->frame_canvas->height / 2)
						side = FR_SE;
					else
						side = FR_NE;
				} else if (pointer_y >
									 asw->frame_canvas->root_y +
									 asw->frame_canvas->height / 2)
					side = FR_SW;
				else
					side = FR_NW;
			}

			mvrdata = resize_widget_interactively (Scr.RootCanvas,
																						 asw->frame_canvas,
																						 event,
																						 apply_aswindow_moveresize,
																						 complete_aswindow_moveresize,
																						 side);
		}
		if (mvrdata) {
			mvrdata->move_only = (data->func == F_MOVE);
			setup_aswindow_moveresize (asw, mvrdata);
			ASWIN_SET_FLAGS (asw, AS_MoveresizeInProgress);
		}
	}
}

void iconify_func_handler (FunctionData * data, ASEvent * event,
													 int module)
{
	if (event->client) {

		LOCAL_DEBUG_CALLER_OUT
				("function %d (val0 = %d), event %d, window 0x%lX, window_name \"%s\", module %d",
				 (int)(data ? data->func : 0), (int)(data ? data->func_val[0] : 0),
				 event ? event->x.type : -1, event ? (unsigned long)event->w : 0,
				 event->client ? ASWIN_NAME (event->client) : "none", module);
		if (ASWIN_GET_FLAGS (event->client, AS_Iconic)) {
			if (data->func_val[0] <= 0)
				set_window_wm_state (event->client, False, False);
		} else if (data->func_val[0] >= 0)
			set_window_wm_state (event->client, True, False);
	}
}

void raiselower_func_handler (FunctionData * data, ASEvent * event,
															int module)
{
	if (event->client) {
		if (event->client->last_restack_time != CurrentTime &&
				event->event_time != CurrentTime &&
				event->client->last_restack_time >= event->event_time)
			return;

		restack_window (event->client, None, (data->func == F_RAISE) ? Above :
										((data->func == F_RAISELOWER) ? Opposite : Below));
	}
}

void raise_it_func_handler (FunctionData * data, ASEvent * event,
														int module)
{
	ASWindow *asw = window2ASWindow (data->func_val[1]);
	activate_aswindow (asw, True, True);
}

void setlayer_func_handler (FunctionData * data, ASEvent * event,
														int module)
{
	if (event->client) {
		register int func = data->func;
		int layer = 0;

		if (func == F_PUTONTOP)
			layer = AS_LayerTop;
		else if (func == F_PUTONBACK)
			layer = AS_LayerBack;
		else if (func == F_TOGGLELAYER) {
			layer = ASWIN_LAYER (event->client);
			if (data->func_val[0] == 0)
				layer += data->func_val[1];
			else
				layer += data->func_val[0];
		} else
			layer = data->func_val[0];
		change_aswindow_layer (event->client, layer);
	}
}

void change_desk_func_handler (FunctionData * data, ASEvent * event,
															 int module)
{
	if (event->client)
		change_aswindow_desktop (event->client, data->func_val[0], False);
}

void toggle_status_func_handler (FunctionData * data, ASEvent * event,
																 int module)
{
	ASFlagType toggle_flags = 0;
	if (data->func == F_STICK)
		toggle_flags = AS_Sticky;
	else if (data->func == F_MAXIMIZE) {
		if (data->func_val[0] > 0 || data->func_val[1] == 0)
			toggle_flags = AS_MaximizedX;
		if (data->func_val[1] > 0 || data->func_val[0] == 0)
			toggle_flags |= AS_MaximizedY;
		if (event->client) {
			event->client->maximize_ratio_x =
					(data->unit_val[0] ==
					 0) ? data->func_val[0] : (data->func_val[0] *
																		 data->unit_val[0] * 100) /
					Scr.MyDisplayWidth;
			event->client->maximize_ratio_y =
					(data->unit_val[1] ==
					 0) ? data->func_val[1] : (data->func_val[1] *
																		 data->unit_val[1] * 100) /
					Scr.MyDisplayHeight;
		}
	} else if (data->func == F_SHADE)
		toggle_flags = AS_Shaded;
	else if (data->func == F_FULLSCREEN)
		toggle_flags = AS_Fullscreen;
	else
		return;
	toggle_aswindow_status (event->client, toggle_flags);
}

void close_func_handler (FunctionData * data, ASEvent * event, int module)
{
	if (event->client) {
		Window w = event->client->w;

		LOCAL_DEBUG_OUT ("window(0x%lX)->protocols(0x%lX)", w,
										 event->client->hints->protocols);
		if (get_flags (event->client->hints->protocols, AS_DoesWmDeleteWindow)
				&& data->func != F_DESTROY) {
			LOCAL_DEBUG_OUT ("sending delete window request to 0x%lX", w);
			send_wm_protocol_request (w, _XA_WM_DELETE_WINDOW, CurrentTime);
		} else {
			if (event->client->internal != NULL
					|| validate_drawable (w, NULL, NULL) == None)
				Destroy (event->client, True);
			else if (data->func == F_DELETE)
				XBell (dpy, event->scr->screen);
			else
				XKillClient (dpy, w);
			XSync (dpy, 0);
		}
	}
}
