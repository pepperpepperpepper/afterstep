/***********************************************************************
 * Module-spawning/IPC AfterStep function handlers.
 ***********************************************************************/

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include "functions_internal.h"

void module_func_handler (FunctionData * data, ASEvent * event, int module)
{
	UngrabEm ();
	ExecModule (data->text, event->client ? event->client->w : None,
							event->context);
}

void killmodule_func_handler (FunctionData * data, ASEvent * event,
															int module)
{
	KillModuleByName (data->text);
}

void killallmodules_func_handler (FunctionData * data, ASEvent * event,
																	int module)
{
	KillAllModulesByName (data->text);
}

void restartmodule_func_handler (FunctionData * data, ASEvent * event,
																 int module)
{
	char *cmd_line = GetModuleCmdLineByName (data->text);
	KillModuleByName (data->text);

	UngrabEm ();
	ExecModule (cmd_line ? cmd_line : data->text,
							event->client ? event->client->w : None, event->context);
	if (cmd_line)
		free (cmd_line);
}

void modulelist_func_handler (FunctionData * data, ASEvent * event,
															int module)
{
	MenuData *md = (data->func == F_STOPMODULELIST) ?
			make_stop_module_menu (Scr.Feel.winlist_sort_order) :
			make_restart_module_menu (Scr.Feel.winlist_sort_order);
	if (md != NULL) {
		ASMenu *menu = run_menu_data (md);
		if (menu)
			menu->volitile_menu_data = md;
	}

}

static Bool send_aswindow_data_iter_func (void *data, void *aux_data)
{
	union {
		void *ptr;
		int id;
	} module_id;
	ASWindow *asw = (ASWindow *) data;

	module_id.ptr = aux_data;

	SendConfig (module_id.id, M_CONFIGURE_WINDOW, asw);
	/* always start with RES_CLASS to let module know if this is a DockApp or not */
	SendString (module_id.id, M_RES_CLASS, asw->w, asw->frame, asw,
							asw->hints->res_class, get_hint_name_encoding (asw->hints,
																													 asw->hints->
																													 res_class_idx));
	SendString (module_id.id, M_RES_NAME, asw->w, asw->frame, asw,
							asw->hints->res_name, get_hint_name_encoding (asw->hints,
																												 asw->hints->
																												 res_name_idx));
	SendString (module_id.id, M_ICON_NAME, asw->w, asw->frame, asw,
							asw->hints->icon_name, get_hint_name_encoding (asw->hints,
																													 asw->hints->
																													 icon_name_idx));
	SendString (module_id.id, M_WINDOW_NAME, asw->w, asw->frame, asw,
							asw->hints->names[0], get_hint_name_encoding (asw->hints,
																												 0));
	return True;
}

void send_window_list_func_handler (FunctionData * data, ASEvent * event,
																		int module)
{
	if (module >= 0) {
		union {
			void *ptr;
			int id;
		} module_id;
		module_id.id = module;
		SendPacket (module, M_TOGGLE_PAGING, 1, DoHandlePageing);
		SendPacket (module, M_NEW_DESKVIEWPORT, 3, Scr.Vx, Scr.Vy,
								Scr.CurrentDesk);
		iterate_asbidirlist (Scr.Windows->clients,
												 send_aswindow_data_iter_func, module_id.ptr, NULL,
												 False);
		SendPacket (module, M_END_WINDOWLIST, 0);
		if (IsValidDesk (Scr.CurrentDesk))
			send_stacking_order (Scr.CurrentDesk);
	}
}

void swallow_window_func_handler (FunctionData * data, ASEvent * event,
																	int module)
{
	if (event->client) {
		if (data->text)
			module = FindModuleByName (data->text);
		SendPacket (module, M_SWALLOW_WINDOW, 2, event->client->w,
								event->client->frame);
	}
}
