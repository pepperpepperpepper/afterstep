/****************************************************************************
 * Copyright (c) 2000,2001,2003 Sasha Vasko <sasha at aftercode.net>
 * Copyright (c) 1999 Ethan Fisher <allanon@crystaltokyo.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * This has been completely rewritten and as the result relicensed under GPL.
 * For historic purposes we keep original creators here :
 *
 * This module is based on Twm, but has been SIGNIFICANTLY modified
 * by Rob Nation
 * by Bo Yang
 * by Frank Fejes
 ****************************************************************************/
/***********************************************************************
 * afterstep function execution code
 ***********************************************************************/

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include <limits.h>
#include <signal.h>
#include <unistd.h>

#include "../../libAfterStep/session.h"
#include "../../libAfterStep/moveresize.h"
#include "../../libAfterStep/mylook.h"
#include "../../libAfterStep/desktop_category.h"
#include "../../libAfterStep/wmprops.h"
#include "../../libAfterConf/afterconf.h"
#include "../../libAfterStep/kde.h"

#include "functions_internal.h"

static as_function_handler function_handlers[F_FUNCTIONS_NUM];

void ExecuteComplexFunction (ASEvent * event, char *name);
Bool desktop_category2complex_function (const char *name,
																				const char *category_name);

/* handlers initialization function : */
void SetupFunctionHandlers ()
{
	memset (&(function_handlers[0]), 0x00, sizeof (function_handlers));
	function_handlers[F_BEEP] = beep_func_handler;

	function_handlers[F_RESIZE] =
			function_handlers[F_MOVE] = moveresize_func_handler;

#ifndef NO_VIRTUAL
	function_handlers[F_SCROLL] = scroll_func_handler;
	function_handlers[F_MOVECURSOR] = movecursor_func_handler;
#endif

	function_handlers[F_RAISE] =
			function_handlers[F_LOWER] =
			function_handlers[F_RAISELOWER] = raiselower_func_handler;

	function_handlers[F_PUTONTOP] =
			function_handlers[F_PUTONBACK] =
			function_handlers[F_TOGGLELAYER] =
			function_handlers[F_SETLAYER] = setlayer_func_handler;

	function_handlers[F_CHANGE_WINDOWS_DESK] = change_desk_func_handler;

	function_handlers[F_MAXIMIZE] =
			function_handlers[F_FULLSCREEN] =
			function_handlers[F_SHADE] =
			function_handlers[F_STICK] = toggle_status_func_handler;

	function_handlers[F_ICONIFY] = iconify_func_handler;

	function_handlers[F_FOCUS] = focus_func_handler;
	function_handlers[F_CHANGEWINDOW_UP] =
			function_handlers[F_WARP_F] =
			function_handlers[F_CHANGEWINDOW_DOWN] =
			function_handlers[F_WARP_B] = warp_func_handler;

	function_handlers[F_PASTE_SELECTION] = paste_selection_func_handler;
	function_handlers[F_GOTO_BOOKMARK] = goto_bookmark_func_handler;
	function_handlers[F_BOOKMARK_WINDOW] = bookmark_window_func_handler;
	function_handlers[F_PIN_MENU] = pin_menu_func_handler;
	function_handlers[F_DESTROY] =
			function_handlers[F_DELETE] =
			function_handlers[F_CLOSE] = close_func_handler;
	function_handlers[F_RESTART] = restart_func_handler;

	function_handlers[F_DesktopEntry] = desktop_entry_func_handler;

	function_handlers[F_EXEC] =
			function_handlers[F_Swallow] =
			function_handlers[F_MaxSwallow] = exec_func_handler;

	function_handlers[F_ExecInDir] = exec_in_dir_func_handler;

	function_handlers[F_ExecInTerm] = exec_in_term_func_handler;

	function_handlers[F_ExecBrowser] =
			function_handlers[F_ExecEditor] = exec_tool_func_handler;

	function_handlers[F_CHANGE_BACKGROUND] = change_background_func_handler;
	function_handlers[F_CHANGE_BACKGROUND_FOREIGN] =
			change_back_foreign_func_handler;

	function_handlers[F_CHANGE_LOOK] =
			function_handlers[F_CHANGE_FEEL] =
			function_handlers[F_CHANGE_COLORSCHEME] =
			function_handlers[F_CHANGE_THEME_FILE] = change_config_func_handler;

	function_handlers[F_CHANGE_THEME] = change_theme_func_handler;

	function_handlers[F_INSTALL_LOOK] =
			function_handlers[F_INSTALL_FEEL] =
			function_handlers[F_INSTALL_BACKGROUND] =
			function_handlers[F_INSTALL_FONT] =
			function_handlers[F_INSTALL_ICON] =
			function_handlers[F_INSTALL_TILE] =
			function_handlers[F_INSTALL_THEME_FILE] =
			function_handlers[F_INSTALL_COLORSCHEME] = install_file_func_handler;



	function_handlers[F_SAVE_WORKSPACE] = save_workspace_func_handler;
	function_handlers[F_SIGNAL_RELOAD_GTK_RCFILE] =
			signal_reload_GTKRC_file_handler;
	function_handlers[F_KIPC_SEND_MESSAGE_ALL] =
			KIPC_send_message_all_handler;

	function_handlers[F_REFRESH] = refresh_func_handler;
#ifndef NO_VIRTUAL
	function_handlers[F_GOTO_PAGE] = goto_page_func_handler;
	function_handlers[F_TOGGLE_PAGE] = toggle_page_func_handler;
#endif
	function_handlers[F_GETHELP] = gethelp_func_handler;
	function_handlers[F_WAIT] = wait_func_handler;
	function_handlers[F_RAISE_IT] = raise_it_func_handler;
	function_handlers[F_DESK] = desk_func_handler;
	function_handlers[F_GOTO_DESKVIEWPORT] = deskviewport_func_handler;

	function_handlers[F_MODULE] =
			function_handlers[F_SwallowModule] =
			function_handlers[F_MaxSwallowModule] = module_func_handler;

	function_handlers[F_KILLMODULEBYNAME] = killmodule_func_handler;
	function_handlers[F_RESTARTMODULEBYNAME] = restartmodule_func_handler;
	function_handlers[F_KILLALLMODULESBYNAME] = killallmodules_func_handler;
	function_handlers[F_POPUP] = popup_func_handler;
	function_handlers[F_QUIT] = quit_func_handler;
#ifndef NO_WINDOWLIST
	function_handlers[F_WINDOWLIST] = windowlist_func_handler;
#endif													/* ! NO_WINDOWLIST */
	function_handlers[F_STOPMODULELIST] =
			function_handlers[F_RESTARTMODULELIST] = modulelist_func_handler;

	function_handlers[F_QUICKRESTART] = quickrestart_func_handler;
	function_handlers[F_SEND_WINDOW_LIST] = send_window_list_func_handler;
	function_handlers[F_SET] = set_func_handler;
	function_handlers[F_Test] = test_func_handler;
/*	function_handlers[F_Remap] = test_func_handler; */

	function_handlers[F_TAKE_WINDOWSHOT] =
			function_handlers[F_TAKE_FRAMESHOT] =
			function_handlers[F_TAKE_SCREENSHOT] = screenshot_func_handler;
	function_handlers[F_SWALLOW_WINDOW] = swallow_window_func_handler;
	function_handlers[F_SYSTEM_SHUTDOWN] = system_shutdown_func_handler;
	function_handlers[F_SUSPEND] = system_shutdown_func_handler;
	function_handlers[F_HIBERNATE] = system_shutdown_func_handler;
	function_handlers[F_LOGOUT] = quit_func_handler;
	function_handlers[F_QUIT_WM] = quit_wm_func_handler;
}

/* complex functions are stored in hash table ComplexFunctions */
ComplexFunction *get_complex_function (char *name)
{
	return find_complex_func (Scr.Feel.ComplexFunctions, name);
}

/* WE need to implement functions queue, so that ExecuteFunction
 * only places function to run into that queue, and queue gets processed
 * at a later time from the main event loop.
 * This is to prevent nasty recursions when functions are called from
 * functions, etc.
 */
typedef struct ASScheduledFunction {
	FunctionData fdata;
	int module;
	ASEvent event;
	/* we do not want to keep pointers to data structures here,
	 * since they may change by the time function is run : */
	Window client;
	Window canvas;

	Bool defered;
} ASScheduledFunction;

ASBiDirList *FunctionQueue = NULL;

void destroy_scheduled_function_handler (void *data)
{
	ASScheduledFunction *sf = data;
	if (sf) {
		free_func_data (&(sf->fdata));
		free (sf);
	}
}

#define destroy_scheduled_function(sf)  destroy_scheduled_function_handler((void*)sf)

static void DoExecuteFunction (ASScheduledFunction * sf);

/***********************************************************************
 *  Procedure:
 *  ExecuteFunction - schedule execution a afterstep built in function
 *  Inputs:
 *      data    - the function to execute
 *      event   - the event that caused the function
 *      module  - number of the module we received function request from
 ***********************************************************************/
void
ExecuteFunctionExt (FunctionData * data, ASEvent * event, int module,
										Bool defered)
{
	ASScheduledFunction *sf = NULL;

	if (data == NULL)
		return;
	if (FunctionQueue == NULL)
		FunctionQueue =
				create_asbidirlist (destroy_scheduled_function_handler);

	sf = safecalloc (1, sizeof (ASScheduledFunction));
	dup_func_data (&(sf->fdata), data);
	sf->module = module;

	sf->event.event_time = Scr.last_Timestamp;
	sf->event.scr = ASDefaultScr;
	sf->defered = defered;
	if (event) {
		sf->event.mask = event->mask;
		sf->event.eclass = event->eclass;
		sf->event.last_time = event->last_time;
		sf->event.typed_last_time = event->typed_last_time;
		sf->event.event_time = event->event_time;

		sf->event.w = event->w;
		sf->event.context = event->context;
		sf->event.x = event->x;
		/* may become freed when it comes to running the function - can't keep a pointer ! */
		if (event->client && event->client->magic == MAGIC_ASWINDOW)
			sf->client = event->client->w;
		if (event->widget)
			sf->canvas = event->widget->w;
	}
	/* we may end up deadlocked if we wait for modules who wait for window list : */
	if (data->func == F_SEND_WINDOW_LIST)
		DoExecuteFunction (sf);
	else
		append_bidirelem (FunctionQueue, sf);
}

void ExecuteFunctionForClient (FunctionData * data, Window client)
{
	ASEvent dummy;
	LOCAL_DEBUG_CALLER_OUT ("client_window(%lX)", client);
	if (data == NULL)
		return;
	memset (&dummy, 0x00, sizeof (dummy));
	dummy.client = window2ASWindow (client);
	dummy.x.type = ButtonRelease;
	dummy.x.xany.window = client;
	ExecuteFunctionExt (data, &dummy, -1, (client != None) /* deffered */ );
}


#undef ExecuteFunction
void ExecuteFunction (FunctionData * data, ASEvent * event, int module)
#ifdef TRACE_ExecuteFunction
#define ExecuteFunction(d,e,m) trace_ExecuteFunction(d,e,m,__FILE__,__LINE__)
#endif
{
	LOCAL_DEBUG_CALLER_OUT
			("event(%d(%s))->window(%lX)->client(%p(%s))->module(%d)",
			 event ? event->x.type : -1,
			 event ? event_type2name (event->x.type) : "n/a",
			 event ? (unsigned long)event->w : 0, event ? event->client : NULL,
			 event ? (event->
								client ? ASWIN_NAME (event->client) : "none") : "none",
			 module);
	if (data == NULL)
		return;
	ExecuteFunctionExt (data, event, module, False);
}

/***********************************************************************
 *  Procedure:
 *  DoExecuteFunction - execute an afterstep built in function
 ***********************************************************************/
Bool is_interactive_action (FunctionData * data)
{
	if (data->func == F_MOVE || data->func == F_RESIZE)
		return (data->func_val[0] == INVALID_POSITION
						&& data->func_val[1] == INVALID_POSITION);
	return False;
}

static void DoExecuteFunction (ASScheduledFunction * sf)
{
	FunctionData *data = &(sf->fdata);
	ASEvent *event = &(sf->event);
	register FunctionCode func = data->func;

#if !defined(LOCAL_DEBUG) || defined(NO_DEBUG_OUTPUT)
	if (get_output_threshold () >= OUTPUT_LEVEL_DEBUG)
#endif
	{
		print_func_data (__FILE__, "DoExecuteFunction", __LINE__, data);
	}

	if (sf->client != None) {
		ASWindow *asw = window2ASWindow (sf->client);
		if (asw == NULL) {					/* window had died by now - let go on with our lives */
			destroy_scheduled_function (sf);
			return;
		}

		event->client = asw;

		if (sf->canvas) {
			ASCanvas *canvas = NULL;
			if (sf->canvas == asw->client_canvas->w)
				canvas = asw->client_canvas;
			else if (sf->canvas == asw->frame_canvas->w)
				canvas = asw->frame_canvas;
			else if (asw->icon_canvas && sf->canvas == asw->icon_canvas->w)
				canvas = asw->icon_canvas;
			else if (asw->icon_title_canvas
							 && sf->canvas == asw->icon_title_canvas->w)
				canvas = asw->icon_title_canvas;
			else {
				int i = FRAME_SIDES;
				while (--i >= 0)
					if (asw->frame_sides[i] != NULL &&
							asw->frame_sides[i]->w == sf->canvas) {
						canvas = asw->frame_sides[i];
						break;
					}
			}
			event->widget = canvas;
		}
	}

	/* Defer Execution may wish to alter this value */
	LOCAL_DEBUG_OUT ("client = %p, IsWindowFunc() = %s", event->client,
									 IsWindowFunc (func) ? "Yes" : "No");
	if (IsWindowFunc (func)) {
		int do_defer = !(sf->defered), fin_event;

		if (event->x.type == ButtonPress)
			fin_event = ButtonRelease;
		else if (event->x.type == MotionNotify)
			fin_event =
					(event->x.xmotion.state & AllButtonMask) !=
					0 ? ButtonRelease : ButtonPress;
		else
			fin_event = ButtonPress;

		if (data->text != NULL && event->client == NULL && func != F_SWALLOW_WINDOW)	/* SWallowWindow has module name as its text ! */
			if (*(data->text) != '\0')
				if ((event->client = pattern2ASWindow (data->text)) != NULL) {
					event->w = get_window_frame (event->client);
					do_defer = False;
				}

		if (event->x.type == KeyPress || event->x.type == KeyRelease) {	/* keyboard events should never be deferred,
																																		 * and if no client is selected for window specific function - then it should be ignored */
			if (event->client == NULL)
				func = F_NOP;
			do_defer = False;
		}

		if (do_defer
				&& (fin_event != ButtonRelease || !is_interactive_action (data))) {
			int cursor = ASCUR_Move;
			if (func != F_RESIZE && func != F_MOVE)
				cursor = (func != F_DESTROY && func != F_DELETE
									&& func != F_CLOSE) ? ASCUR_Select : ASCUR_Destroy;

			if (DeferExecution (event, cursor, fin_event))
				func = F_NOP;
		}

		if (event->client == NULL)
			func = F_NOP;

	}

	if (function_handlers[func] || func == F_FUNCTION || func == F_CATEGORY) {
		char *complex_func_name = COMPLEX_FUNCTION_NAME (data);

		data->func = func;
		if (event->client) {
			if (get_flags (event->client->status->flags, AS_Fullscreen) &&
					(data->func == F_MOVE ||
					 data->func == F_RESIZE || data->func == F_MAXIMIZE)) {
				LOCAL_DEBUG_OUT
						("function \"%s\" is not allowed for the Fullscreen window",
						 COMPLEX_FUNCTION_NAME (data));
				func = data->func = F_BEEP;
			} else
					if (!check_allowed_function2 (data->func, event->client->hints))
			{
				LOCAL_DEBUG_OUT
						("function \"%s\" is not allowed for the specifyed window (mask 0x%lX)",
						 COMPLEX_FUNCTION_NAME (data),
						 ASWIN_FUNC_MASK (event->client));
				func = data->func = F_BEEP;
			}
		}

		if (get_flags (AfterStepState, ASS_WarpingMode) &&
				function_handlers[func] != warp_func_handler)
			EndWarping ();

		if (func == F_CATEGORY) {
			char *cat_name = data->text ? data->text : data->name;
			if (cat_name == NULL)
				func = F_BEEP;
			else {
				complex_func_name =
						safemalloc (sizeof ("CATEGORY()") + strlen (cat_name) + 1);
				sprintf (complex_func_name, "CATEGORY(%s)", cat_name);
				if (get_complex_function (complex_func_name) != NULL) {
					func = F_FUNCTION;
				} else {
					if (desktop_category2complex_function
							(complex_func_name, cat_name))
						func = F_FUNCTION;
					else
						func = F_BEEP;
				}
			}
		}

		if (func == F_FUNCTION)
			ExecuteComplexFunction (event, complex_func_name);
		else
			function_handlers[func] (data, event, sf->module);
		if (complex_func_name && complex_func_name != data->name
				&& complex_func_name != data->text)
			free (complex_func_name);
	}
	destroy_scheduled_function (sf);
}

Bool FunctionsPending ()
{
	return (FunctionQueue && FunctionQueue->head != NULL);
}

void ExecutePendingFunctions ()
{
	ASScheduledFunction *sf;
	if (FunctionQueue)
		while ((sf = extract_first_bidirelem (FunctionQueue)) != NULL)
			DoExecuteFunction (sf);
}

void DestroyPendingFunctionsQueue ()
{
	if (FunctionQueue)
		destroy_asbidirlist (&FunctionQueue);
}

/***********************************************************************
 *  Procedure:
 *	DeferExecution - defer the execution of a function to the
 *	    next button press if the context is ASC_Root
 *
 *  Inputs:
 *      eventp  - pointer to ASEvent to patch up
 *      cursor  - the cursor to display while waiting
 *      finish_event - ButtonRelease or ButtonPress; tells what kind of event to
 *                     terminate on.
 * Returns:
 *      True    - if we should defer execution of the function
 *      False   - if we can continue as planned
 ***********************************************************************/
int DeferExecution (ASEvent * event, int cursor, int finish_event)
{
	Bool res = False;
	LOCAL_DEBUG_CALLER_OUT
			("cursor %d, event %d, window 0x%lX, window_name \"%s\", finish event %d",
			 cursor, event ? event->x.type : -1,
			 event ? (unsigned long)event->w : 0,
			 event->client ? ASWIN_NAME (event->client) : "none", finish_event);

/*    if (event->context != C_ROOT && event->context != C_NO_CONTEXT)
	if ( finish_event == ButtonPress ||
		(finish_event == ButtonRelease && event->x.type != ButtonPress))
		return False;
*/
	if (!(res = !GrabEm (ASDefaultScr, Scr.Feel.cursors[cursor]))) {
		ASHintWindow *hint =
				create_ashint_window (ASDefaultScr, &(Scr.Look),
															"Please select target window");
		WaitEventLoop (event, finish_event, -1, hint);
		destroy_ashint_window (&hint);

		LOCAL_DEBUG_OUT ("window(%lX)->root(%lX)->subwindow(%lX)",
										 event->x.xbutton.window, event->x.xbutton.root,
										 event->x.xbutton.subwindow);
		if (event->client == NULL) {
			res = True;
			/* since we grabbed cursor we may get clicks over client windows as reported
			 * relative to the root window, in which case we have to check subwindow to
			 * see what client was clicked */
			if (event->x.xbutton.subwindow != event->w) {
				event->client = window2ASWindow (event->x.xbutton.subwindow);
				if (event->client != NULL) {
					res = False;
					event->w = event->x.xbutton.subwindow;
				}
			}
		}
		UngrabEm ();
	}
	if (res)
		XBell (dpy, event->scr->screen);

	LOCAL_DEBUG_OUT ("result %d, event %d, window 0x%lX, window_name \"%s\"",
									 res, event ? event->x.type : -1,
									 event ? (unsigned long)event->w : 0,
									 event->client ? ASWIN_NAME (event->client) : "none");

	return res;
}

/*****************************************************************************
 *  Executes complex function defined with Function/EndFunction construct :
 ****************************************************************************/
void ExecuteComplexFunction (ASEvent * event, char *name)
{
	ComplexFunction *func = NULL;
	int clicks = 0;
	register int i;
	Bool persist = False;
	Bool need_window = False;
	char c;
	static char clicks_upper[MAX_CLICKS_HANDLED + 1] =
			{ CLICKS_TRIGGERS_UPPER };
	static char clicks_lower[MAX_CLICKS_HANDLED + 1] =
			{ CLICKS_TRIGGERS_LOWER };
	LOCAL_DEBUG_CALLER_OUT
			("event %d, window 0x%lX, asw(%p), window_name \"%s\", function name \"%s\"",
			 event ? event->x.type : -1, event ? (unsigned long)event->w : 0,
			 event->client, event->client ? ASWIN_NAME (event->client) : "none",
			 name);


	if (name && (name[0] == IMMEDIATE || name[0] == IMMEDIATE_UPPER))
		if (name[1] == ':')
			if ((func = get_complex_function (&(name[2]))) == NULL)
				return;
	if (func == NULL)
		if ((func = get_complex_function (name)) == NULL)
			return;

	LOCAL_DEBUG_OUT
			("function data found  %p ...\n   proceeding to execution of immediate items...",
			 func);
	if (event->w == None && event->client != NULL)
		event->w = get_window_frame (event->client);
	/* first running all the Imediate actions : */
	for (i = 0; i < func->items_num; i++) {
		c = func->items[i].name ? *(func->items[i].name) : 'i';
		if (c == IMMEDIATE || c == IMMEDIATE_UPPER) {
			Bool skip = False;
			if (func->items[i].name &&
					IsExecFunc (func->items[i].func) &&
					func->items[i].name[1] == ':') {
				int k = i;
				while (++k < func->items_num)
					if (func->items[k].func == F_WAIT && func->items[k].name)
						if (strcmp (func->items[k].name, func->items[i].name) == 0) {
							skip = True;
							break;
						}
				if (skip) {
					/* since function names in complex functions have i or I as the first
					 * character to signify that they are immediate items - we allow semicolon
					 * to separate that special character from the actuall window's name for
					 * readability sake
					 */
					char *pattern_start = &(func->items[i].name[2]);
					if (complex_pattern2ASWindow (pattern_start) == NULL)
						skip = False;				/* window is not up yet - can't skip */
				}
			}
			if (!skip)
				ExecuteFunctionExt (&(func->items[i]), event, -1, True);
		} else {
			persist = True;
			if (IsWindowFunc (func->items[i].func))
				need_window = True;
		}
	}
	/* now lets count clicks : */
	LOCAL_DEBUG_OUT ("done with immediate items - persisting ?: %s",
									 persist ? "True" : "False");
	if (!persist)
		return;

	if (need_window && event->client == NULL) {
		if (DeferExecution (event, ASCUR_Select, ButtonPress)) {
/*            WaitForButtonsUpLoop (); */
			return;
		}
	}
	if (!GrabEm (ASDefaultScr, Scr.Feel.cursors[ASCUR_Select])) {
		show_warning
				("failed to grab pointer while executing complex function \"%s\"",
				 name);
		XBell (dpy, Scr.screen);
		return;
	}

	clicks = 0;
	while (IsClickLoop (event, ButtonReleaseMask, Scr.Feel.ClickTime)) {
		clicks++;
		if (!IsClickLoop (event, ButtonPressMask, Scr.Feel.ClickTime))
			break;
		clicks++;
	}
	if (clicks <= MAX_CLICKS_HANDLED) {
		/* some functions operate on button release instead of
		 * presses. These gets really weird for complex functions ... */
/*        if (event->x.type == ButtonPress)
		event->x.type = ButtonRelease;
 */
		/* first running all the Imediate actions : */
		for (i = 0; i < func->items_num; i++)
			if (func->items[i].name) {
				c = func->items[i].name[0];
				if (c == clicks_upper[clicks] || c == clicks_lower[clicks])
					ExecuteFunctionExt (&(func->items[i]), event, -1, True);
			}
	}
/*    WaitForButtonsUpLoop (); */
	UngrabEm ();
	LOCAL_DEBUG_OUT
			("at the end : event %d, window 0x%lX, asw(%p), window_name \"%s\", function name \"%s\"",
			 event ? event->x.type : -1, event ? (unsigned long)event->w : 0,
			 event->client, event->client ? ASWIN_NAME (event->client) : "none",
			 name);
}

void ExecuteBatch (ComplexFunction * batch)
{
	ASEvent event = { 0 };
	register int i;

	LOCAL_DEBUG_CALLER_OUT ("event %p", batch);

	if (batch) {
		for (i = 0; i < batch->items_num; i++) {
			int func = batch->items[i].func;
			if (IsWindowFunc (func) || function_handlers[func] == NULL)
				continue;

			function_handlers[func] (&(batch->items[i]), &event, -1);
		}
	}
}


Bool desktop_category2complex_function (const char *name,
																				const char *category_name)
{
	ASCategoryTree *ct = NULL;
	ASDesktopCategory *dc = NULL;
	ComplexFunction *func;
	char **entries;
	int i, entries_num;

	if ((dc = name2desktop_category (category_name, &ct)) == NULL)
		return False;

	entries_num = PVECTOR_USED (dc->entries);
	if (entries_num == 0)
		return False;
	entries = PVECTOR_HEAD (char *, dc->entries);

	if ((func =
			 new_complex_func (Scr.Feel.ComplexFunctions, (char *)name)) == NULL)
		return False;

	func->items_num = 0;
	func->items = safecalloc (entries_num, sizeof (FunctionData));
	for (i = 0; i < entries_num; ++i) {
		ASDesktopEntry *de;
		FunctionData *fdata = &(func->items[func->items_num]);
		if ((de = fetch_desktop_entry (ct, entries[i])) == NULL)
			continue;
		if (de->type != ASDE_TypeApplication)
			continue;

		++(func->items_num);
		init_func_data (fdata);
		fdata->name = mystrdup ("I");	/* for immediate execution */
		fdata->func = desktop_entry2function_code (de);
		if (de->clean_exec)
			fdata->text = mystrdup (de->clean_exec);
	}

	return True;
}
