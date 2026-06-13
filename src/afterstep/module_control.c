/*
 * Copyright (c) 1999 Ethan Fischer <allanon@crystaltokyo.com>
 * Copyright (C) 1998 Guylhem Aznar
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

/***********************************************************************
 *
 * code for launching afterstep modules.
 *
 ***********************************************************************/

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include <signal.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>						/* for chmod() */
#include <sys/types.h>
#include <sys/un.h>							/* for struct sockaddr_un */

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <X11/keysym.h>

#include "../../libAfterStep/desktop_category.h"
#include "../../libAfterStep/module.h"
#include "../../libAfterStep/wmprops.h"
#include "module_internal.h"

/********************************************************************************/
/* public interfaces :                                                          */
/********************************************************************************/
int AcceptModuleConnection (int socket_fd)
{
	int fd;
	unsigned int len = sizeof (struct sockaddr_un);
	struct sockaddr_un name;

	fd = accept (socket_fd, (struct sockaddr *)&name, &len);

	if (fd < 0 && errno != EWOULDBLOCK)
		show_system_error ("error accepting connection");

	/* set non-blocking I/O mode */
	if (fd >= 0) {
		if (fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK) == -1) {
			show_system_error
					("unable to set non-blocking I/O for module socket");
			close (fd);
			fd = -1;
		}
	}

	/* mark as close-on-exec so other programs won't inherit the socket */
	if (fd >= 0) {
		if (fcntl (fd, F_SETFD, 1) == -1) {
			show_system_error ("unable to set close-on-exec for module socket");
			close (fd);
			fd = -1;
		}
	}

	if (fd >= 0 && Modules) {
		int channel = -1;

		/* Look for an available pipe slot */
		if (MODULES_NUM < Module_npipes - 1) {
			module_t new_module;
			memset (&new_module, 0x00, sizeof (module_t));
			/* add pipe to afterstep's active pipe list */
			new_module.name = mystrdup ("unknown module");
			new_module.fd = fd;
			new_module.active = 0;
			new_module.mask = MAX_MASK;
			new_module.output_queue = NULL;
			/* adding new module to the end of the list */
			LOCAL_DEBUG_OUT
					("adding new module:  total modules %d. list starts at %p",
					 MODULES_NUM, MODULES_LIST);
			channel = vector_insert_elem (Modules, &new_module, 1, NULL, False);
			LOCAL_DEBUG_OUT
					("added module # %d : total modules %d. list starts at %p",
					 channel, MODULES_NUM, MODULES_LIST);

		}
		if (channel < 0) {
			show_error ("too many modules!");
			close (fd);
			fd = -1;
		}
	}

	return fd;
}


void ShutdownModules (Bool dont_free_memory)
{
	if (Modules != NULL) {
		register int i = MODULES_NUM;
		register module_t *list = MODULES_LIST;
		LOCAL_DEBUG_OUT ("pid(%d),total modules %d. list starts at %p",
										 getpid (), MODULES_NUM, MODULES_LIST);
		while (--i >= 0)
			KillModule (&(list[i]), dont_free_memory);
		if (!dont_free_memory) {
			LOCAL_DEBUG_OUT ("pid(%d),destroy_asvector", getpid ());
			destroy_asvector (&Modules);
			LOCAL_DEBUG_OUT ("pid(%d),free_vector", getpid ());
			free_vector (&module_output_buffer);
			LOCAL_DEBUG_OUT ("pid(%d),modules are down", getpid ());
		}
		Modules = NULL;
	}
}

void SetupModules (void)
{
	if (Modules)
		ShutdownModules (False);

	Module_npipes = get_fd_width ();
	LOCAL_DEBUG_OUT ("max Module pipes = %d", Module_npipes);
	Modules = create_asvector (sizeof (module_t));
	module_setup_socket ();
}

void ExecModule (char *action, Window win, int context)
{
	char *module, *cmd, *args;

	if (action == NULL || Modules == NULL)
		return;
	args = parse_filename (action, &module);
	if (module == NULL)
		return;
	if ((cmd = find_file (module, Environment->module_path, X_OK)) == NULL) {
		show_error ("no such module %s in path %s\n", module,
								Environment->module_path);
		free (module);
		return;
	}
	free (module);

	args = stripcpy (args);
	spawn_child (cmd, -1, Scr.screen, NULL, win, context, True, True, args,
							 NULL);

	if (args)
		free (args);
	free (cmd);
}


void
HandleModuleInOut (unsigned int channel, Bool has_input, Bool has_output)
{
	int res = 0;

	if (Modules && channel < MODULES_NUM) {
		register module_t *module = &(MODULES_LIST[channel]);
		LOCAL_DEBUG_OUT ("module %d has %s input and %s output", channel,
										 has_input ? "" : "no", has_output ? "" : "no");
		if (has_input) {
			res = HandleModuleInput (module);
			if (res < 0)
				has_output = 0;
			else if (res > 0)					/* need to run command */
				RunCommand (module->ibuf.func, channel, module->ibuf.window);
		}
		/* module could be killed inside RunCoimmand ! */
		if (has_output && module->fd > 0)
			FlushQueue (module);
	}
}

void DeadPipe (int nonsense)
{
	signal (SIGPIPE, DeadPipe);
}


int FindModuleByName (char *name)
{
	int module = -1;
	if (Modules && name) {
		wild_reg_exp *wrexp = compile_wild_reg_exp (name);

		if (wrexp != NULL) {
			register int i = MODULES_NUM;
			register module_t *list = MODULES_LIST;

			while (--i >= 0)
				if (list[i].fd > 0) {
					LOCAL_DEBUG_OUT
							("checking if module %d \"%s\" matches regexp \"%s\"", i,
							 list[i].name, name);
					if (match_wild_reg_exp (list[i].name, wrexp) == 0) {
						module = i;
						break;
					}
				}
			destroy_wild_reg_exp (wrexp);
		}
	}
	return module;
}

char *GetModuleCmdLineByName (char *name)
{
	int module = FindModuleByName (name);
	if (module >= 0) {
		register module_t *list = MODULES_LIST;
		if (list[module].cmd_line)
			return mystrdup (list[module].cmd_line);
	}
	return NULL;
}

void KillModuleByName (char *name)
{
	int module = FindModuleByName (name);
	if (module >= 0) {
		register module_t *list = MODULES_LIST;
		KillModule (&(list[module]), False);
	}
}

void KillAllModulesByName (char *name)
{
	int module;
	while ((module = FindModuleByName (name)) >= 0) {
		register module_t *list = MODULES_LIST;
		KillModule (&(list[module]), False);
	}
}

int KillAllModules ()
{
	int count = 0;
	if (Modules != NULL) {
		register int i = MODULES_NUM;
		register module_t *list = MODULES_LIST;
		LOCAL_DEBUG_OUT ("pid(%d),total modules %d. list starts at %p",
										 getpid (), MODULES_NUM, MODULES_LIST);
		while (--i >= 0) {
			KillModule (&(list[i]), False);
			count++;
		}
	}
	return count;
}


void
SendPacket (int channel, send_data_type msg_type, send_data_type num_datum,
						...)
{
	va_list ap;
	register send_data_type *body;
	int i;

	flush_vector (&module_output_buffer);
	append_vector (&module_output_buffer,
								 make_msg_header (msg_type, num_datum), MSG_HEADER_SIZE);

	append_vector (&module_output_buffer, NULL, num_datum);
	body = VECTOR_TAIL (send_data_type, module_output_buffer);

	va_start (ap, num_datum);
	for (i = 0; i < num_datum; i++)
		*(body++) = va_arg (ap, send_data_type);

	VECTOR_USED (module_output_buffer) += num_datum;
	va_end (ap);

	LOCAL_DEBUG_OUT("Sending buffer , used = %d", VECTOR_USED (module_output_buffer));
	SendBuffer (channel);
	LOCAL_DEBUG_OUT("Done sending buffer , used = %d", VECTOR_USED (module_output_buffer));
}


void SendConfig (int module, send_data_type event_type, ASWindow * t)
{
	send_signed_data_type frame_x = 0, frame_y = 0, frame_width =
			0, frame_height = 0;
	send_ID_type icon_title_w = None, icon_pixmap_w = None;
	send_signed_data_type icon_x = 0, icon_y = 0, icon_width =
			0, icon_height = 0;
	send_signed_data_type pid = -1;
	union {
		ASWindow *asw;
		send_data_type id;
	} asw_id;

	if (t->frame_canvas) {
		frame_x = t->frame_canvas->root_x;
		frame_y = t->frame_canvas->root_y;
		if (!ASWIN_GET_FLAGS (t, AS_Sticky)) {
			frame_x += t->status->viewport_x;
			frame_y += t->status->viewport_y;
		}
		frame_width = t->frame_canvas->width + t->frame_canvas->bw * 2;
		frame_height = t->frame_canvas->height + t->frame_canvas->bw * 2;
	}

	if (t->icon_canvas)
		icon_pixmap_w = t->icon_canvas->w;
	if (t->icon_title_canvas && t->icon_title_canvas != t->icon_canvas)
		icon_title_w = t->icon_title_canvas->w;

	if (ASWIN_GET_FLAGS (t, AS_Iconic)) {
		ASCanvas *ic = t->icon_canvas ? t->icon_canvas : t->icon_title_canvas;
		if (ic != NULL) {
			icon_x = ic->root_x;
			icon_y = ic->root_y;
			icon_width = ic->width + ic->bw * 2;
			icon_height = ic->height + ic->bw * 2;
		}
	}

	if (ASWIN_HFLAGS (t, AS_PID))
		pid = t->hints->pid;

	asw_id.asw = t;
	SendPacket (module, event_type, 27,
							t->w, t->frame, asw_id.id,
							frame_x, frame_y, frame_width, frame_height,
							ASWIN_DESK (t), t->status->flags, t->hints->flags,
							t->hints->client_icon_flags, t->hints->base_width,
							t->hints->base_height, t->hints->width_inc,
							t->hints->height_inc, t->hints->min_width,
							t->hints->min_height, t->hints->max_width,
							t->hints->max_height, t->hints->gravity, icon_title_w,
							icon_pixmap_w, icon_x, icon_y, icon_width, icon_height, pid);
}

void
SendString (int channel, send_data_type msg_type,
						Window w, Window frame, ASWindow * asw_ptr,
						char *string, send_data_type encoding)
{
	send_data_type data[3];
	send_signed_data_type len = 0;
	union {
		ASWindow *asw;
		send_data_type id;
	} asw_id;

	if (string == NULL)
		return;
	len = strlen (string);

	flush_vector (&module_output_buffer);
	append_vector (&module_output_buffer,
								 make_msg_header (msg_type,
																	3 + 1 + (len >> 2) + 1 +
																	MSG_HEADER_SIZE), MSG_HEADER_SIZE);
	data[0] = w;
	data[1] = frame;
	asw_id.asw = asw_ptr;
	data[2] = asw_id.id;
	append_vector (&module_output_buffer, &(data[0]), 3);
	append_vector (&module_output_buffer, &encoding, 1);
	serialize_string (string, &module_output_buffer);
	SendBuffer (channel);
}

void SendVector (int channel, send_data_type msg_type, ASVector * vector)
{
	if (vector == NULL)
		return;

	flush_vector (&module_output_buffer);
	append_vector (&module_output_buffer,
								 make_msg_header (msg_type, VECTOR_USED (*vector)),
								 MSG_HEADER_SIZE);
	append_vector (&module_output_buffer,
								 VECTOR_HEAD (send_data_type, *vector),
								 VECTOR_USED (*vector));

	SendBuffer (channel);
}

void
SendStackingOrder (int channel, send_data_type msg_type,
									 send_data_type desk, ASVector * ids)
{
	send_data_type data[2];

	if (ids == NULL)
		return;

	flush_vector (&module_output_buffer);
	append_vector (&module_output_buffer,
								 make_msg_header (msg_type, VECTOR_USED (*ids) + 2),
								 MSG_HEADER_SIZE);
	data[0] = desk;
	data[1] = VECTOR_USED (*ids);
	append_vector (&module_output_buffer, &(data[0]), 2);
	append_vector (&module_output_buffer, VECTOR_HEAD (send_data_type, *ids),
								 VECTOR_USED (*ids));

	SendBuffer (channel);
}

static void
check_module_name_collision (unsigned int channel, const char *name,
														 Bool kill_new)
{
	register int i = MODULES_NUM;
	register module_t *list = MODULES_LIST;
	LOCAL_DEBUG_OUT
			("pid(%d),total modules %d. name = \"%s\", kill_new = %d", getpid (),
			 MODULES_NUM, name, kill_new);
	while (--i >= 0)
		if (i != channel) {
			LOCAL_DEBUG_OUT ("checking module %d, name = \"%s\"", i,
											 list[i].name ? list[i].name : "(null)");
			if (mystrcasecmp (list[i].name, name) == 0) {
				KillModule (&(list[kill_new ? channel : i]), False);
				break;
			}
		}
}


/* this will run command received from module */
void RunCommand (FunctionData * fdata, unsigned int channel, Window w)
{
	ASWindow *tmp_win;
	module_t *module;

	LOCAL_DEBUG_CALLER_OUT
			("fdata(%p)->func(%ld,MOD_FS=%d)->channel(%d)->w(%lX)->Modules(%p)",
			 fdata, fdata->func, F_MODULE_FUNC_START, channel, w, Modules);
/*fprintf( stderr,"Function parsed: [%s] [%s] [%d] [%d] [%c]\n",fdata.name,fdata.text,fdata.func_val[0], fdata.func_val[1] );
 */
	if (Modules == NULL || fdata == NULL || channel >= MODULES_NUM)
		return;
	if (!IsValidFunc (fdata->func))
		return;
	module = &(MODULES_LIST[channel]);
	switch (fdata->func) {
	case F_SET_MASK:
		module->mask = fdata->func_val[0];
		module->lock_on_send_mask = fdata->func_val[1];
		break;
	case F_SET_NAME:
		set_string (&(module->name), fdata->name);
		fdata->name = NULL;
		if (fdata->text) {
			set_string (&(module->cmd_line), fdata->text);
			fdata->text = NULL;
		}
		if (Environment->module_name_collision != ASE_AllowModuleNameCollision)
			check_module_name_collision (channel, module->name,
																	 (Environment->module_name_collision ==
																		ASE_KillNewModuleOnNameCollision));
		break;
	case F_UNLOCK:
		break;
	case F_SET_FLAGS:
		{
			int xorflag;
			Bool update = False;

			if ((tmp_win = window2ASWindow (w)) == NULL)
				break;
			xorflag = tmp_win->hints->flags ^ fdata->func_val[0];
			/*if (xorflag & STICKY)
			   Stick (tmp_win); */
			if (xorflag & AS_SkipWinList) {
				tmp_win->hints->flags ^= AS_SkipWinList;
				update = True;
			}
			if (xorflag & AS_AvoidCover) {
				tmp_win->hints->flags ^= AS_AvoidCover;
				update = True;
			}
			if (xorflag & AS_Transient) {
				tmp_win->hints->flags ^= AS_Transient;
				update = True;
			}
			if (xorflag & AS_DontCirculate) {
				tmp_win->hints->flags ^= AS_DontCirculate;
				update = True;
			}
			if (update)
				broadcast_config (M_CONFIGURE_WINDOW, tmp_win);
			break;
		}
	default:
		{
			ASEvent event;
			Bool defered = False;
			memset (&event, 0x00, sizeof (ASEvent));
			event.w = w;
			if ((event.client = window2ASWindow (w)) == NULL) {
				event.w = None;
				event.x.xbutton.x_root = 0;
				event.x.xbutton.y_root = 0;
			} else {
				event.x.xbutton.x_root = event.client->frame_canvas->root_x + 1;
				event.x.xbutton.y_root = event.client->frame_canvas->root_y + 1;
			}

			event.x.xany.type = ButtonRelease;
			event.x.xbutton.button = 1;
			event.x.xbutton.x = 0;
			event.x.xbutton.y = 0;
			event.x.xbutton.subwindow = None;
			event.context = C_FRAME;
			event.scr = ASDefaultScr;
			event.event_time = Scr.last_Timestamp;
			/* there must be no deffering on module commands ! */

			defered = (w != None || !IsWindowFunc (fdata->func));
			ExecuteFunctionExt (fdata, &event, channel, defered);
		}
	}
	free_func_data (fdata);
}

/*******************************************************************************/
/* usefull functions to simplify life in other places :                        */
/*******************************************************************************/
void broadcast_focus_change (ASWindow * asw, Bool focused)
{
	union {
		ASWindow *asw;
		send_data_type id;
	} asw_id;

	asw_id.asw = asw;
	if (asw)
		SendPacket (-1, M_FOCUS_CHANGE, 4, asw->w, asw->frame, asw_id.id,
								(send_data_type) focused);
}

void broadcast_window_name (ASWindow * asw)
{
	if (asw) {
		SendString (-1, M_WINDOW_NAME, asw->w, asw->frame,
								asw, ASWIN_NAME (asw), get_hint_name_encoding (asw->hints,
																															 0));
		SendString (-1, M_WINDOW_NAME_MATCHED, asw->w, asw->frame, asw,
								asw->hints->matched_name0,
								asw->hints->matched_name0_encoding);
	}
}

void broadcast_icon_name (ASWindow * asw)
{
	if (asw) {
		SendString (-1, M_ICON_NAME, asw->w, asw->frame,
								asw, ASWIN_ICON_NAME (asw),
								get_hint_name_encoding (asw->hints,
																				asw->hints->icon_name_idx));
	}
}

void broadcast_res_names (ASWindow * asw)
{
	if (asw) {
		SendString (-1, M_RES_CLASS, asw->w, asw->frame,
								asw, asw->hints->res_class,
								get_hint_name_encoding (asw->hints,
																				asw->hints->res_class_idx));
		SendString (-1, M_RES_NAME, asw->w, asw->frame, asw,
								asw->hints->res_name, get_hint_name_encoding (asw->hints,
																															asw->hints->
																															res_name_idx));
	}
}

void broadcast_status_change (int message, ASWindow * asw)
{
	union {
		ASWindow *asw;
		send_data_type id;
	} asw_id;

	asw_id.asw = asw;

	if (message == M_MAP)
		SendPacket (-1, M_MAP, 3, asw->w, asw->frame, asw_id.id);
}

void broadcast_config (send_data_type event_type, ASWindow * t)
{
	SendConfig (-1, event_type, t);
}

/********************************************************************************/
/* module list menus regeneration :                                             */
/********************************************************************************/

static inline void
module_t2func_data (FunctionCode func, module_t * module,
										FunctionData * fdata, char *scut)
{
	fdata->func = func;
	fdata->name = mystrdup (module->name);
	fdata->text = mystrdup (module->name);
	if (++(*scut) == ('9' + 1))
		(*scut) = 'A';							/* Next shortcut key */
	fdata->hotkey = (*scut);
}

MenuData *make_module_menu (FunctionCode func, const char *title,
														int sort_order)
{
	MenuData *md;
	MenuDataItem *mdi;
	FunctionData fdata;
	char scut = '0';							/* Current short cut key */
	module_t *modules;
	int i, max_i;
	MinipixmapData minipixmaps[MINIPIXMAP_TypesNum] = { {0}, {0} };

	if (Modules == NULL)
		return NULL;

	if ((md = create_menu_data ("@#%module_menu%#@")) == NULL)
		return NULL;

	modules = MODULES_LIST;
	max_i = MODULES_NUM;

	memset (&fdata, 0x00, sizeof (FunctionData));
	fdata.func = F_TITLE;
	fdata.name = mystrdup (title);
	add_menu_fdata_item (md, &fdata, NULL);

	if (sort_order == ASO_Alpha) {
		FunctionData **menuitems = safecalloc (sizeof (FunctionData *), max_i);

		for (i = 0; i < max_i; ++i) {
			menuitems[i] = safecalloc (1, sizeof (FunctionData));
			module_t2func_data (func, &(modules[i]), menuitems[i], &scut);
		}
		qsort (menuitems, i, sizeof (FunctionData *), compare_func_data_name);
		for (i = 0; i < max_i; ++i) {
			ASDesktopEntry *de;
			de = fetch_desktop_entry (AfterStepCategories, menuitems[i]->name);

			if (de)
				minipixmaps[MINIPIXMAP_Icon].filename = de->Icon;

			if ((mdi =
					 add_menu_fdata_item (md, menuitems[i],
																&(minipixmaps[0]))) != NULL) {
				set_flags (mdi->flags, MD_ScaleMinipixmapDown);
				if (de) {
					char *comment = NULL;
					if (dup_desktop_entry_Comment (de, &comment))
						set_flags (mdi->flags, MD_CommentIsUTF8);
					if (comment) {
						mdi->comment = interpret_ascii_string (comment);
						free (comment);
					}
				}
			}
			safefree (menuitems[i]);	/* scrubba-dub-dub */
		}
		safefree (menuitems);
	} else {											/* if( sort_order == ASO_Circulation || sort_order == ASO_Stacking ) */

		for (i = 0; i < max_i; ++i) {
			ASDesktopEntry *de;
			module_t2func_data (func, &(modules[i]), &fdata, &scut);
			de = fetch_desktop_entry (AfterStepCategories, fdata.name);
			if (de)
				minipixmaps[MINIPIXMAP_Icon].filename = de->Icon;
			if ((mdi = add_menu_fdata_item (md, &fdata, &(minipixmaps[0]))) != NULL)
				set_flags (mdi->flags, MD_ScaleMinipixmapDown);
		}
	}
	return md;
}
