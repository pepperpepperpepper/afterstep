/***********************************************************************
 * Focus/warp AfterStep function handlers.
 ***********************************************************************/

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include "functions_internal.h"

void focus_func_handler (FunctionData * data, ASEvent * event, int module)
{
	activate_aswindow (event->client, True, False);
}

void warp_func_handler (FunctionData * data, ASEvent * event, int module)
{
	register ASWindow *t = NULL;

	if (data->text != NULL)
		if (*(data->text) != '\0')
			t = pattern2ASWindow (data->text);
	if (t == NULL)
		t = warp_aswindow_list (Scr.Windows,
														(data->func == F_CHANGEWINDOW_DOWN
														 || data->func == F_WARP_B));
	if (t != NULL) {
		event->client = t;
		event->w = get_window_frame (t);
		StartWarping (ASDefaultScr);
		warp_to_aswindow (t,
											(data->func == F_WARP_F || data->func == F_WARP_B));
	}
}

void goto_bookmark_func_handler (FunctionData * data, ASEvent * event,
																 int module)
{
	ASWindow *asw = bookmark2ASWindow (data->text);
	if (asw)
		activate_aswindow (asw, True, False);
}

void bookmark_window_func_handler (FunctionData * data, ASEvent * event,
																	 int module)
{
	bookmark_aswindow (event->client, data->text);
}
