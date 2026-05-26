#ifndef AFTERSTEP_FUNCTIONS_INTERNAL_H
#define AFTERSTEP_FUNCTIONS_INTERNAL_H

#include "asinternals.h"

Bool is_interactive_action (FunctionData * data);

/* list of available handlers : */
void beep_func_handler (FunctionData * data, ASEvent * event, int module);
void moveresize_func_handler (FunctionData * data, ASEvent * event,
															int module);
void scroll_func_handler (FunctionData * data, ASEvent * event,
													int module);
void movecursor_func_handler (FunctionData * data, ASEvent * event,
															int module);
void raiselower_func_handler (FunctionData * data, ASEvent * event,
															int module);
void setlayer_func_handler (FunctionData * data, ASEvent * event,
														int module);
void change_desk_func_handler (FunctionData * data, ASEvent * event,
															 int module);
void toggle_status_func_handler (FunctionData * data, ASEvent * event,
																 int module);
void iconify_func_handler (FunctionData * data, ASEvent * event,
													 int module);
void focus_func_handler (FunctionData * data, ASEvent * event, int module);
void paste_selection_func_handler (FunctionData * data, ASEvent * event,
																	 int module);
void warp_func_handler (FunctionData * data, ASEvent * event, int module);
void goto_bookmark_func_handler (FunctionData * data, ASEvent * event,
																 int module);
void bookmark_window_func_handler (FunctionData * data, ASEvent * event,
																	 int module);
void pin_menu_func_handler (FunctionData * data, ASEvent * event,
														int module);
void close_func_handler (FunctionData * data, ASEvent * event, int module);
void restart_func_handler (FunctionData * data, ASEvent * event,
													 int module);
void exec_func_handler (FunctionData * data, ASEvent * event, int module);
void exec_in_dir_func_handler (FunctionData * data, ASEvent * event,
															 int module);
void desktop_entry_func_handler (FunctionData * data, ASEvent * event,
																 int module);
void exec_in_term_func_handler (FunctionData * data, ASEvent * event,
																int module);
void exec_tool_func_handler (FunctionData * data, ASEvent * event,
														 int module);
void change_background_func_handler (FunctionData * data, ASEvent * event,
																		 int module);
void change_back_foreign_func_handler (FunctionData * data,
																			 ASEvent * event, int module);
void change_config_func_handler (FunctionData * data, ASEvent * event,
																 int module);
void change_theme_func_handler (FunctionData * data, ASEvent * event,
																int module);
void install_file_func_handler (FunctionData * data, ASEvent * event,
																int module);
void refresh_func_handler (FunctionData * data, ASEvent * event,
													 int module);
void goto_page_func_handler (FunctionData * data, ASEvent * event,
														 int module);
void toggle_page_func_handler (FunctionData * data, ASEvent * event,
															 int module);
void gethelp_func_handler (FunctionData * data, ASEvent * event,
													 int module);
void wait_func_handler (FunctionData * data, ASEvent * event, int module);
void raise_it_func_handler (FunctionData * data, ASEvent * event,
														int module);
void desk_func_handler (FunctionData * data, ASEvent * event, int module);
void deskviewport_func_handler (FunctionData * data, ASEvent * event,
																int module);
void module_func_handler (FunctionData * data, ASEvent * event,
													int module);
void killmodule_func_handler (FunctionData * data, ASEvent * event,
															int module);
void killallmodules_func_handler (FunctionData * data, ASEvent * event,
																	int module);
void restartmodule_func_handler (FunctionData * data, ASEvent * event,
																 int module);
void popup_func_handler (FunctionData * data, ASEvent * event, int module);
void quit_func_handler (FunctionData * data, ASEvent * event, int module);
void quit_wm_func_handler (FunctionData * data, ASEvent * event, int module);
void windowlist_func_handler (FunctionData * data, ASEvent * event,
															int module);
void quickrestart_func_handler (FunctionData * data, ASEvent * event,
																int module);
void send_window_list_func_handler (FunctionData * data, ASEvent * event,
																		int module);
void save_workspace_func_handler (FunctionData * data, ASEvent * event,
																	int module);
void signal_reload_GTKRC_file_handler (FunctionData * data,
																			 ASEvent * event, int module);
void KIPC_send_message_all_handler (FunctionData * data, ASEvent * event,
																		int module);
void set_func_handler (FunctionData * data, ASEvent * event, int module);
void test_func_handler (FunctionData * data, ASEvent * event, int module);
void screenshot_func_handler (FunctionData * data, ASEvent * event,
															int module);
void swallow_window_func_handler (FunctionData * data, ASEvent * event,
																	int module);
void system_shutdown_func_handler (FunctionData * data, ASEvent * event,
																	int module);
void modulelist_func_handler (FunctionData * data, ASEvent * event,
															int module);

#endif /* AFTERSTEP_FUNCTIONS_INTERNAL_H */
