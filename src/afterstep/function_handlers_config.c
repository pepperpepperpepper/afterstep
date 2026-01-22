/***********************************************************************
 * Configuration-related AfterStep function handlers.
 ***********************************************************************/

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include "../../libAfterStep/session.h"
#include "../../libAfterStep/moveresize.h"
#include "../../libAfterStep/mylook.h"
#include "../../libAfterStep/desktop_category.h"
#include "../../libAfterStep/wmprops.h"
#include "../../libAfterConf/afterconf.h"
#include "../../libAfterStep/kde.h"

#include <time.h>

static int _as_config_change_recursion = 0;
static int _as_config_change_count = 0;
static int _as_background_change_count = 0;

void commit_config_change (int func)
{
	if (_as_config_change_recursion <= 1) {
		if (_as_background_change_count > 0) {
			MyBackground *new_back =
					get_desk_back_or_default (Scr.CurrentDesk, False);
			/* we want to display selected background even if that was disabled in look,
			   this may cause problems, since when user loads AfterStep the next time - he/she 
			   will see what was configured in look again, and not what was selected from 
			   the menu ! */
			destroy_string (&(new_back->data));
			new_back->type = MB_BackImage;
			if (new_back->loaded_im_name) {
				free (new_back->loaded_im_name);
				new_back->loaded_im_name = NULL;
			}
			change_desktop_background (Scr.CurrentDesk);
			SendPacket (-1, M_NEW_BACKGROUND, 1, 1);
			_as_background_change_count = 0;
		}
		if (_as_config_change_count > 0) {
			if (func == F_CHANGE_THEME)
				QuickRestart ("theme");
			else if (func == F_CHANGE_COLORSCHEME)
				QuickRestart ("look");
			else
				QuickRestart ((func == F_CHANGE_LOOK) ? "look" : "feel");
			_as_config_change_count = 0;
		}
	}
}

static void change_background_internal (FunctionData * data,
																				Bool autoscale)
{
	char tmpfile[256], *realfilename;
	Bool success = False;

	++_as_config_change_recursion;
	XGrabPointer (dpy, Scr.Root, True, ButtonPressMask | ButtonReleaseMask,
								GrabModeAsync, GrabModeAsync, Scr.Root,
								Scr.Feel.cursors[ASCUR_Wait], CurrentTime);
	XSync (dpy, 0);

	if (Scr.screen == 0)
		sprintf (tmpfile, BACK_FILE, Scr.CurrentDesk);
	else
		sprintf (tmpfile, BACK_FILE ".scr%ld", Scr.CurrentDesk, Scr.screen);

	realfilename = make_session_data_file (Session, False, 0, tmpfile, NULL);
	cover_desktop ();
	if (autoscale) {
		ASImage *src_im =
				get_asimage (Scr.image_manager, data->text, 0xFFFFFFFF, 0);
		if (src_im) {
			if (src_im->width >= 600 && src_im->height >= 600) {
				int clip_width = src_im->width;
				int clip_height = src_im->height;
				ASImage *tiled = src_im;

				if (clip_width * Scr.MyDisplayHeight >
						((clip_height * 5) / 4) * Scr.MyDisplayWidth)
					clip_width =
							(((clip_height * 5) / 4) * Scr.MyDisplayWidth) /
							Scr.MyDisplayHeight;
				else if (((clip_width * 5) / 4) * Scr.MyDisplayHeight <
								 clip_height * Scr.MyDisplayWidth)
					clip_height =
							(((clip_width * 5) / 4) * Scr.MyDisplayHeight) /
							Scr.MyDisplayWidth;
				if (clip_width != src_im->width || clip_height != src_im->height) {
					display_progress (True, "Croping background to %dx%d ...",
														clip_width, clip_height);
					LOCAL_DEBUG_OUT ("Croping background to %dx%d ...", clip_width,
													 clip_height);
					tiled =
							tile_asimage (Scr.asv, src_im,
														(src_im->width - clip_width) / 2,
														(src_im->height - clip_height) / 2, clip_width,
														clip_height, TINT_LEAVE_SAME, ASA_ASImage, 0,
														ASIMAGE_QUALITY_DEFAULT);
				}
				if (tiled) {
					ASImage *scaled = tiled;
					if (tiled->width != Scr.MyDisplayWidth
							|| tiled->height != Scr.MyDisplayHeight) {
						display_progress (True, "Scaling background to %dx%d ...",
															Scr.MyDisplayWidth, Scr.MyDisplayHeight);
						LOCAL_DEBUG_OUT ("Scaling background to %dx%d ...",
														 Scr.MyDisplayWidth, Scr.MyDisplayHeight);
						scaled =
								scale_asimage (Scr.asv, tiled, Scr.MyDisplayWidth,
															 Scr.MyDisplayHeight, ASA_ASImage, 0,
															 ASIMAGE_QUALITY_DEFAULT);
					}
					if (scaled && scaled != src_im) {
						display_progress (True,
															"Saving transformed background into \"%s\" ...",
															realfilename);
						LOCAL_DEBUG_OUT
								("Saving transformed background into \"%s\" ...",
								 realfilename);
						success =
								save_asimage_to_file (realfilename, scaled, "png", "9",
																			NULL, 0, True);
					}
					if (scaled != tiled)
						destroy_asimage (&scaled);
					if (tiled != src_im)
						destroy_asimage (&tiled);
				}
			}
			safe_asimage_destroy (src_im);
		}
	}

	if (!success) {								/* just tile as usuall */
		display_progress (True,
											"Copying selected background \"%s\" into \"%s\" ...",
											data->text, realfilename);
		LOCAL_DEBUG_OUT ("Copying selected background \"%s\" into \"%s\" ...",
										 data->text, realfilename);
		success = (CopyFile (data->text, realfilename) == 0);
	}

	if (success) {
		++_as_background_change_count;
		if (Scr.CurrentDesk == 0)
			update_default_session (Session, F_CHANGE_BACKGROUND);
		change_desk_session (Session, Scr.CurrentDesk, realfilename,
												 F_CHANGE_BACKGROUND);
	}
	free (realfilename);

	commit_config_change (F_CHANGE_BACKGROUND);
	remove_desktop_cover ();

	XUngrabPointer (dpy, CurrentTime);
	XSync (dpy, 0);
	--_as_config_change_recursion;
}

typedef struct {
	char *url;
	char *cachedName;
	time_t downloadStart;
	FunctionData fdata;
	int pid;
} WebBackgroundDownloadAuxData;

static WebBackgroundDownloadAuxData webBackgroundDownloadAuxData =
		{ NULL, NULL, 0, {0}, 0 };

static void webBackgroundDownloadHandler (void *data)
{
	WebBackgroundDownloadAuxData *wb = (WebBackgroundDownloadAuxData *) data;
	int s1, s2;
	Bool downloadComplete =
			check_download_complete (wb->pid, wb->cachedName, &s1, &s2);
	if (downloadComplete) {
		if (s1 == 0 || s1 != s2) {
			downloadComplete = False;
			show_warning
					("Failed to download \"%s\", see \"%s.log\" for details",
					 wb->url, wb->cachedName);
			return;										/* download failed */
		}
		asdbus_Notify ("Image download complete", wb->url, -1);
	}

	if (downloadComplete) {
		change_background_internal (&(wb->fdata), True);
	} else
		timer_new (300, &webBackgroundDownloadHandler, data);
}

void change_background_func_handler (FunctionData * data, ASEvent * event,
																		 int module)
{
	timer_remove_by_data (&webBackgroundDownloadAuxData);
	change_background_internal (data, False);
}

void change_back_foreign_func_handler (FunctionData * data,
																			 ASEvent * event, int module)
{
	timer_remove_by_data (&webBackgroundDownloadAuxData);

	if (data->text != NULL && is_web_background (data)) {
		char *cachedFileName =
				make_session_webcache_file (Session, data->text);

		set_string (&(webBackgroundDownloadAuxData.url),
								mystrdup (data->text));
		set_string (&(webBackgroundDownloadAuxData.cachedName),
								cachedFileName);
		webBackgroundDownloadAuxData.downloadStart = time (NULL);
		webBackgroundDownloadAuxData.fdata = *data;
		webBackgroundDownloadAuxData.fdata.text = cachedFileName;

		if (CheckFile (cachedFileName) == 0)
			change_background_internal (&(webBackgroundDownloadAuxData.fdata),
																	True);
		else if ((webBackgroundDownloadAuxData.pid =
							spawn_download (webBackgroundDownloadAuxData.url,
															cachedFileName)) != 0) {
			asdbus_Notify ("Image download started",
										 webBackgroundDownloadAuxData.url, -1);
			timer_new (300, &webBackgroundDownloadHandler,
								 (void *)&webBackgroundDownloadAuxData);
		}
	} else
		change_background_internal (data, True);
}

void change_theme_func_handler (FunctionData * data, ASEvent * event,
																int module)
{
	++_as_config_change_recursion;
	if (install_theme_file (data->text))
		++_as_config_change_count;

	/* theme installation may trigger recursive look and feel changes - we
	 * don't want those to cause any restarts or config reloads.
	 */
	commit_config_change (data->func);

	--_as_config_change_recursion;
}

void change_config_func_handler (FunctionData * data, ASEvent * event,
																 int module)
{

	char *file_template;
	char tmpfile[256], *realfilename = NULL;
	int desk = 0;

	++_as_config_change_recursion;
#ifdef DIFFERENTLOOKNFEELFOREACHDESKTOP
	desk = Scr.CurrentDesk;
#endif
	if (Scr.screen == 0) {
		switch (data->func) {
		case F_CHANGE_LOOK:
			file_template = LOOK_FILE;
			break;
		case F_CHANGE_FEEL:
			file_template = FEEL_FILE;
			break;
		case F_CHANGE_COLORSCHEME:
			file_template = COLORSCHEME_FILE;
			break;
		default:
			file_template = THEME_FILE;
			break;
		}
		sprintf (tmpfile, file_template, desk);
	} else {
		switch (data->func) {
		case F_CHANGE_LOOK:
			file_template = LOOK_FILE ".scr%ld";
			break;
		case F_CHANGE_FEEL:
			file_template = FEEL_FILE ".scr%ld";
			break;
		case F_CHANGE_COLORSCHEME:
			file_template = COLORSCHEME_FILE ".scr%ld";
			break;
		default:
			file_template = THEME_FILE ".scr%ld";
			break;
		}
		sprintf (tmpfile, file_template, desk, Scr.screen);
	}

	realfilename = make_session_data_file (Session, False, 0, tmpfile, NULL);

	cover_desktop ();
	display_progress (True,
										"Copying selected config file \"%s\" into \"%s\" ...",
										data->text, realfilename);

	if (CopyFile (data->text, realfilename) == 0) {
		++_as_config_change_count;
		if (Scr.CurrentDesk == 0)
			update_default_session (Session, data->func);
		change_desk_session (Session, Scr.CurrentDesk, realfilename,
												 data->func);
	}
	free (realfilename);

	/* theme installation may trigger recursive look and feel changes - we
	 * don't want those to cause any restarts or config reloads.
	 */
	commit_config_change (data->func);
	remove_desktop_cover ();

	--_as_config_change_recursion;
}

void install_file_func_handler (FunctionData * data, ASEvent * event,
																int module)
{
	char *file = NULL;
	char *realfilename = NULL;
	Bool desktop_resource = False;
	char *dir_name = NULL;

	switch (data->func) {
	case F_INSTALL_LOOK:
		dir_name = as_look_dir_name;
		break;
	case F_INSTALL_FEEL:
		dir_name = as_feel_dir_name;
		break;
	case F_INSTALL_BACKGROUND:
		dir_name = as_background_dir_name;
		break;
	case F_INSTALL_FONT:
		dir_name = as_font_dir_name;
		desktop_resource = True;
		break;
	case F_INSTALL_ICON:
		dir_name = as_icon_dir_name;
		desktop_resource = True;
		break;
	case F_INSTALL_TILE:
		dir_name = as_tile_dir_name;
		desktop_resource = True;
		break;
	case F_INSTALL_THEME_FILE:
		dir_name = as_theme_file_dir_name;
		break;
	case F_INSTALL_COLORSCHEME:
		dir_name = as_colorscheme_dir_name;
		break;
	}
	if (dir_name != NULL) {
		parse_file_name (data->text, NULL, &file);

		cover_desktop ();
		display_progress (True, "Installing file \"%s\" into \"%s\" ...",
											data->text, dir_name);

		if (desktop_resource) {
			realfilename =
					make_session_data_file (Session, False, 0, DESKTOP_DIR, NULL);
			CheckOrCreate (realfilename);
			free (realfilename);
		}

		realfilename =
				make_session_data_file (Session, False, 0, dir_name, NULL);
		CheckOrCreate (realfilename);
		free (realfilename);

		realfilename =
				make_session_data_file (Session, False, 0, dir_name, file, NULL);
		CopyFile (data->text, realfilename);
		free (realfilename);
		free (file);
		remove_desktop_cover ();
	}
}

void install_feel_func_handler (FunctionData * data, ASEvent * event,
																int module)
{

}

void install_background_func_handler (FunctionData * data, ASEvent * event,
																			int module)
{

}

void install_font_func_handler (FunctionData * data, ASEvent * event,
																int module)
{

}

void install_icon_func_handler (FunctionData * data, ASEvent * event,
																int module)
{

}

void install_tile_func_handler (FunctionData * data, ASEvent * event,
																int module)
{

}

void install_theme_file_func_handler (FunctionData * data, ASEvent * event,
																			int module)
{

}

void save_workspace_func_handler (FunctionData * data, ASEvent * event,
																	int module)
{
	save_aswindow_list (Scr.Windows, data->text ? data->text : NULL, False);
}

Bool send_client_message_iter_func (void *data, void *aux_data)
{
	XClientMessageEvent *ev = (XClientMessageEvent *) aux_data;
	ASWindow *asw = (ASWindow *) data;

	ev->window = asw->w;
	XSendEvent (dpy, asw->w, False, 0, (XEvent *) ev);

	return True;
}

void signal_reload_GTKRC_file_handler (FunctionData * data,
																			 ASEvent * event, int module)
{
	XClientMessageEvent ev;

	memset (&ev, 0x00, sizeof (ev));
	ev.type = ClientMessage;
	ev.display = dpy;
	ev.message_type = _GTK_READ_RCFILES;
	ev.format = 8;

	iterate_asbidirlist (Scr.Windows->clients, send_client_message_iter_func,
											 &ev, NULL, False);
}

Bool send_kipc_client_message_iter_func (void *data, void *aux_data)
{
	XClientMessageEvent *ev = (XClientMessageEvent *) aux_data;
	ASWindow *asw = (ASWindow *) data;

	/* KDE not always sets this flag ??? */
	if (get_flags (asw->hints->protocols, AS_DoesKIPC)) {
		ev->window = asw->w;
		XSendEvent (dpy, asw->w, False, 0, (XEvent *) ev);
	}

	return True;
}

void KIPC_sendMessageAll (KIPC_Message msg, int data)
{
	XClientMessageEvent ev;

	memset (&ev, 0x00, sizeof (ev));
	ev.type = ClientMessage;
	ev.display = dpy;
	ev.message_type = _KIPC_COMM_ATOM;
	ev.format = 32;
	ev.data.l[0] = msg;
	ev.data.l[1] = data;

	iterate_asbidirlist (Scr.Windows->clients, send_client_message_iter_func,
											 &ev, NULL, False);
	/*  iterate_asbidirlist( Scr.Windows->clients, send_kipc_client_message_iter_func, &ev, NULL, False ); */
}

void KIPC_send_message_all_handler (FunctionData * data, ASEvent * event,
																		int module)
{
	KIPC_sendMessageAll ((KIPC_Message) data->func_val[0],
											 data->func_val[1]);

}

void quickrestart_func_handler (FunctionData * data, ASEvent * event,
																int module)
{
	QuickRestart (data->text);
}

void signal_reload_gtkrc_file ()
{
	FunctionData gtkrc_signal_func;
	ASEvent dummy_event = { 0 };

	init_func_data (&gtkrc_signal_func);
	gtkrc_signal_func.func = F_SIGNAL_RELOAD_GTK_RCFILE;

	ExecuteFunction (&gtkrc_signal_func, &dummy_event, -1);
}

void signal_kde_palette_changed ()
{
	FunctionData kde_signal_func;
	ASEvent dummy_event = { 0 };

	init_func_data (&kde_signal_func);
	kde_signal_func.func = F_KIPC_SEND_MESSAGE_ALL;
	kde_signal_func.func_val[0] = KIPC_PaletteChanged;
	kde_signal_func.func_val[1] = 0;

	ExecuteFunction (&kde_signal_func, &dummy_event, -1);
}
