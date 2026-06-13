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

DECL_VECTOR (send_data_type, module_output_buffer);

static void DeleteQueueBuff (module_t * module);
static void AddToQueue (module_t * module, send_data_type * ptr, int size,
												int done);

int module_listen (const char *socket_name);


/* create a named UNIX socket, and start watching for connections */
Bool module_setup_socket ()
{
	char *tmp;
#ifdef __CYGWIN__
	{															/* there are problems under Windows as home dir could be anywhere  -
																 * so we just use /tmp since there will only be 1 user anyways :) */
		tmp = safemalloc (4 + 9 + 32 + 1);
		/*sprintf (tmp, "/tmp/connect.%s", display_string); */
		sprintf (tmp, "/tmp/as-connect.%ld", Scr.screen);
		fprintf (stderr, "using %s for intermodule communications.\n", tmp);
	}
#else
	{
		char *display = XDisplayString (dpy);
		char *tmpdir = getenv ("TMPDIR");
		static char *default_tmp_dir = "/tmp";
		if (tmpdir != NULL)
			if (CheckDir (tmpdir) < 0)
				tmpdir = NULL;

		if (tmpdir == NULL)
			tmpdir = default_tmp_dir;
		if (access (tmpdir, W_OK) != 0)
			if ((tmpdir = getenv ("HOME")) == NULL)
				return False;

		tmp = safemalloc (strlen (tmpdir) + 11 + 32 + strlen (display) + 1);
		sprintf (tmp, "%s/afterstep-%d.%s", tmpdir, getuid (), display);
		LOCAL_DEBUG_OUT ("using socket \"%s\" for intermodule communications",
										 tmp);
	}
#endif
	set_as_module_socket (Scr.wmprops, tmp);
	Module_fd = socket_listen (tmp);
	free (tmp);

	XSync (dpy, 0);

	return (Module_fd >= 0);
}

void KillModule (module_t * module, Bool dont_free_memory)
{
	LOCAL_DEBUG_OUT ("module %p ", module);
	LOCAL_DEBUG_OUT ("module name \"%s\"", module->name);
	if (module->fd > 0)
		close (module->fd);

	if (!dont_free_memory) {
		while (module->output_queue != NULL)
			DeleteQueueBuff (module);
		if (module->name != NULL)
			destroy_string (&(module->name));
		destroy_string (&(module->cmd_line));
		destroy_string (&(module->ibuf.text));

		if (module->ibuf.func != NULL) {
			free_func_data (module->ibuf.func);
			free (module->ibuf.func);
		}
		memset (module, 0x00, sizeof (module_t));
	} else {
		module->output_queue = NULL;
		module->name = NULL;
		module->ibuf.text = NULL;
		module->ibuf.func = NULL;
	}

	module->fd = -1;
	module->active = -1;
}


/*
 * ReadModuleInput Does actuall read from the module pipe.
 * returns :
 *  0, -1    - error or not enough data
 *  1        - SUCCESS
 */



static int
ReadModuleInput (module_t * module, size_t * offset, size_t size,
								 void *ptr)
{
	size_t done_this = module->ibuf.done - *offset;
	int n = size;

	if (done_this >= 0 && done_this < size) {
		ptr += done_this;
		n = read (module->fd, ptr, size - done_this);
		if (n > 0) {
			module->ibuf.done += n;
			if (module->ibuf.done < *offset + size)
				return 0;								/* No more data available */
		} else
			return (n == -1 && (errno == EINTR || errno == EAGAIN)) ? 0 : -1;
	}
	if (n > 0)
		*offset += n;
	return 1;											/* Success */
}

static void
CheckCmdTextSize (module_t * module, CARD32 * size, CARD32 * curr_len,
									char **text)
{
	/* max command length is 1024 */
	if (*size > 1024) {
		show_error ("command from module '%s' is too big (%d)", module->name,
								*size);
		*size = 1024;
	}

	/* need to be able to read in command */
	if (*curr_len < *size + 1) {
		*curr_len = *size + 1;
		*text = realloc (*text, *curr_len);
	}
}

/*
 * Higher level protocol handler
 *
 * Two possible protocols :
 * 1. Text command line :
 * <window>< 0<size<256 >< size bytes of text >
 * < continuation_indicator == F_FUNCTIONS_NUM >
 *
 * 2. Preprocessed AS Function data :
 * <window>< size==0 ><function_code>
 *  < 0<name_size<256 >[< name_size bytes of function name >|<nothing if size == 0>]
 *  < 0<text_size<256 >[< size bytes of text >|< nothing if size == 0>]
 *  < 2*sizeof(long) of func_val[] >< 2*sizeof(long) of unit_val >
 * < continuation_indicator == F_FUNCTIONS_NUM >
 *
 * Returns :
 *  -2    - bad module
 *  0, -1 - error or not enough data
 *  > 0   - command execution result
 */

int HandleModuleInput (module_t * module)
{
	size_t offset = 0;
	int res = 1;
	Bool invalid_func = False;
	register module_ibuf_t *ibuf;

	ibuf = &(module->ibuf);

	/* read a window id */
	res =
			ReadModuleInput (module, &offset, sizeof (ibuf->window),
											 &(ibuf->window));
	if (res > 0) {
		module->active = 1;
		res =
				ReadModuleInput (module, &offset, sizeof (ibuf->size),
												 &(ibuf->size));
	}

	LOCAL_DEBUG_OUT ("res(%d)->window(0x%X)->size(%d)", res, ibuf->window,
									 ibuf->size);
	if (res > 0) {
		if (ibuf->size > 0) {				/* Protocol 1 */
			LOCAL_DEBUG_OUT ("Incoming message in proto 1%s", "");
			CheckCmdTextSize (module, &(ibuf->size), &(ibuf->len),
												&(ibuf->text));
			res = ReadModuleInput (module, &offset, ibuf->size, ibuf->text);

			if (res > 0) {
				/* null-terminate the command line */
				ibuf->text[ibuf->size] = '\0';
				ibuf->func = String2Func (ibuf->text, ibuf->func, False);
				invalid_func = (ibuf->func == NULL);
			}
		} else {										/* Protocol 2 */

			/* for module->afterstep communications - 32 bit values are always used : */
			CARD32 curr_len;
			CARD32 tmp32, tmp32_val[2] = { 0, 0 }
			, tmp32_unit[2] = {
			0, 0};
			register FunctionData *pfunc = ibuf->func;

			LOCAL_DEBUG_OUT ("Incoming message in proto 2%s", "");
			if (pfunc == NULL) {
				pfunc = (FunctionData *) safecalloc (1, sizeof (FunctionData));
				init_func_data (pfunc);
				ibuf->func = pfunc;
			}

			res = ReadModuleInput (module, &offset, sizeof (CARD32), &tmp32);
			pfunc->func = tmp32;

			if (res > 0) {
				if (!IsValidFunc (pfunc->func)) {
					res = 0;
					ibuf->done = 0;
					invalid_func = True;
				} else
					res =
							ReadModuleInput (module, &offset, sizeof (ibuf->name_size),
															 &(ibuf->name_size));
			}
			if (res > 0 && ibuf->name_size > 0) {
				curr_len = (pfunc->name) ? strlen (pfunc->name) + 1 : 0;
				CheckCmdTextSize (module, &(ibuf->name_size), &curr_len,
													&(pfunc->name));
				res =
						ReadModuleInput (module, &offset, ibuf->name_size,
														 pfunc->name);
				pfunc->name[ibuf->name_size] = '\0';
			}
			LOCAL_DEBUG_OUT ("name_size = %d, pfunc->name = %p", ibuf->name_size,
											 pfunc->name);
			if (res > 0)
				res =
						ReadModuleInput (module, &offset, sizeof (ibuf->text_size),
														 &(ibuf->text_size));

			if (res > 0 && ibuf->text_size > 0) {
				curr_len = (pfunc->text) ? strlen (pfunc->text) + 1 : 0;
				CheckCmdTextSize (module, &(ibuf->text_size), &curr_len,
													&(pfunc->text));
				res =
						ReadModuleInput (module, &offset, ibuf->text_size,
														 pfunc->text);
				pfunc->text[ibuf->text_size] = '\0';
			}
			LOCAL_DEBUG_OUT ("text_size = %d, pfunc->text = %p", ibuf->text_size,
											 pfunc->text);
			if (res > 0) {
				res =
						ReadModuleInput (module, &offset, sizeof (tmp32_val),
														 &(tmp32_val[0]));
				if (res > 0) {
					pfunc->func_val[0] = tmp32_val[0];
					pfunc->func_val[1] = tmp32_val[1];
				}
			}


			if (res > 0) {
				res =
						ReadModuleInput (module, &offset, sizeof (tmp32_unit),
														 &(tmp32_unit[0]));
				if (res > 0) {
					pfunc->unit_val[0] = tmp32_unit[0];
					pfunc->unit_val[1] = tmp32_unit[1];
				}
			}

			if (res > 0 && IsValidFunc (pfunc->func))
				invalid_func = False;
		}
	}

	/* get continue command */
	if (res > 0) {
		res =
				ReadModuleInput (module, &offset, sizeof (ibuf->cont),
												 &(ibuf->cont));
		if (res > 0) {
			if (ibuf->cont != F_FUNCTIONS_NUM)
				if (ibuf->cont != 1)
					res = -1;
		} else
			ibuf->cont = 0;
	}

	if (res < 0)
		KillModule (module, False);
	else if (res > 0) {
		ibuf->done = 0;							/* done reading command */
		if (invalid_func)
			res = -1;
	}
	LOCAL_DEBUG_OUT ("result(%d)", res);
	PRINT_MEM_STATS (NULL);
	return res;
}

#define PREALLOCED_QUEUE_LEN		256
#define PREALLOCED_QUEUE_DATA_LEN	128
static unsigned char		_as_prealloced_queue_data[PREALLOCED_QUEUE_LEN][PREALLOCED_QUEUE_DATA_LEN];
static Bool _as_prealloced_init = False;
static struct queue_buff_struct		_as_prealloced_queue_elems[PREALLOCED_QUEUE_LEN];

static void
AddToQueue (module_t * module, send_data_type * ptr, int size, int done)
{
	register struct queue_buff_struct *new_elem, **tail;
	int i = 0;

	if (!_as_prealloced_init) {
		memset (&_as_prealloced_queue_elems[0], 0x00,
						sizeof (_as_prealloced_queue_elems));
		_as_prealloced_init = True;
	}
#if 1
	if (size < PREALLOCED_QUEUE_DATA_LEN)
		while (++i < PREALLOCED_QUEUE_LEN)
			if (_as_prealloced_queue_elems[i].prealloced_idx == 0)
				break;

	if (i > 0 && i < PREALLOCED_QUEUE_LEN) {
		new_elem = &_as_prealloced_queue_elems[i];
		new_elem->prealloced_idx = i;
		new_elem->data = &_as_prealloced_queue_data[i][0];
		new_elem->next = NULL;
	} else 
#endif	
	{
		new_elem = safecalloc (1, sizeof (struct queue_buff_struct));
		new_elem->data = safemalloc (size);
	}
	new_elem->size = size;
	new_elem->done = done;
	memcpy (new_elem->data, ptr, size);
	LOCAL_DEBUG_OUT ("que_buff %p: size = %d, done = %d, data = %p",
									 new_elem, size, done, new_elem->data);
	for (tail = &(module->output_queue); *tail; tail = &((*tail)->next)) {
		/*LOCAL_DEBUG_OUT ("*tail = %p, (*tail)->next = %p", *tail, ((*tail)->next))*/;
	}	
	*tail = new_elem;
}

static void DeleteQueueBuff (module_t * module)
{
	register struct queue_buff_struct *a = module->output_queue;
	if (a) {
		module->output_queue = a->next;
		LOCAL_DEBUG_OUT ("deleting buffer %p sent to module %p - next %p ", a,
										 module, a->next);
		if (a->prealloced_idx == 0) {
			free (a->data);
			free (a);
		} else
			a->prealloced_idx = 0;
	}
}

int FlushQueue (module_t * module)
{
	extern int errno;
	int fd;
	register struct queue_buff_struct *curr;
LOCAL_DEBUG_OUT ("module \"%s\", active= %d, out_queue = %p", module->name, module->active, module->output_queue);
	if (module->active <= 0)
		return -1;
	if (module->output_queue == NULL)
		return 1;

	fd = module->fd;
	while ((curr = module->output_queue) != NULL) {
		register unsigned char *dptr = curr->data;
		int bytes_written = 0;

		do {
			if ((bytes_written =
					 write (fd, &dptr[curr->done], curr->size - curr->done)) > 0)
				curr->done += bytes_written;
			LOCAL_DEBUG_OUT ("wrote %d bytes into the module %p pipe",
											 bytes_written, module);
		} while (curr->done < curr->size && bytes_written > 0);

		/* the write returns EWOULDBLOCK or EAGAIN if the pipe is full.
		 * (This is non-blocking I/O). SunOS returns EWOULDBLOCK, OSF/1
		 * returns EAGAIN under these conditions. Hopefully other OSes
		 * return one of these values too. Solaris 2 doesn't seem to have
		 * a man page for write(2) (!) */
		if (bytes_written < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
				return 0;
			} else {
				KillModule (module, False);
				return -1;
			}
		}
		DeleteQueueBuff (module);
	}
	return 1;
}

void FlushAllQueues ()
{
	fd_set out_fdset;
	int retval = -1;
	struct timeval tv;
	struct timeval *t = NULL;

	if (Modules == NULL)
		return;

	do {
		int max_fd = -1;
		register int i = MIN (MODULES_NUM, Module_npipes);
		register module_t *list = MODULES_LIST;

		FD_ZERO (&out_fdset);

		while (--i >= 0) {
			if (list[i].fd >= 0) {

				int res = 0;
				if (list[i].output_queue && (retval < 0 || FD_ISSET (list[i].fd, &out_fdset)))
					res = FlushQueue (&(list[i]));
				if (res >= 0 && list[i].output_queue != NULL) {
					FD_SET (list[i].fd, &out_fdset);
					if (max_fd < list[i].fd)
						max_fd = list[i].fd;
				}
			}
		}

		if (max_fd < 0)
			return;										/* no more output left */

		tv.tv_sec = 0;
		tv.tv_usec = 15000;
		t = &tv;
		retval =
				PORTABLE_SELECT (min (max_fd + 1, fd_width), NULL, &out_fdset,
												 NULL, t);
		if (retval <= 0)
			return;
	} while (1);
}




#include <sys/errno.h>
static inline int
PositiveWrite (unsigned int channel, send_data_type * ptr, int size)
{
	module_t *module = &(MODULES_LIST[channel]);
	register CARD32 mask = ptr[1];

	LOCAL_DEBUG_OUT ("module(%p)->name(\"%s\")->active(%d)->module_mask(0x%X)->mask(0x%X)",
									 module, module->name, module->active, module->mask, mask);
	if (module->active < 0 || !get_flags (module->mask, mask))
		return -1;

	AddToQueue (module, ptr, size, 0);
	LOCAL_DEBUG_OUT("lock_on_send_mask = %d,is_server_grabbed =%d", get_flags (module->lock_on_send_mask, mask), is_server_grabbed ());
	if (get_flags (module->lock_on_send_mask, mask) && !is_server_grabbed ()) {
		int res;
		int wait_count = 0;
		do {
			LOCAL_DEBUG_OUT ("Attempting to FlushQueue for module %d", module);
			if ((res = FlushQueue (module)) >= 0) {
				sleep_a_millisec (10);	/* give it some time to react */
				/* LOCAL_DEBUG_OUT ("HandleModuleInput for module %d", module);*/
				res = HandleModuleInput (module);
			}
			if (res > 0) {						/* need to run command */
				LOCAL_DEBUG_OUT
						("replay received while waiting for UNLOCK, func = %ld, F_UNLOCK = %d",
						 module->ibuf.func->func, F_UNLOCK);
				if (module->ibuf.func->func == F_UNLOCK)
					return size;
				RunCommand (module->ibuf.func, channel, module->ibuf.window);
			}
			sleep_a_millisec (10);		/* give it some time to react */
			++wait_count;
			/* module has no more then 20 seconds to unlock us */
		} while (res >= 0 && wait_count < 2000);
	}
	LOCAL_DEBUG_OUT ("all done %d", size);
	return size;
}

send_data_type *make_msg_header (send_data_type msg_type,
																				send_data_type size)
{
	static send_data_type msg_header[MSG_HEADER_SIZE];

	msg_header[0] = START_FLAG;
	msg_header[1] = msg_type;
	msg_header[2] = size + MSG_HEADER_SIZE;
	return &(msg_header[0]);
}

void SendBuffer (int channel)
{
	send_data_type *b = VECTOR_HEAD (send_data_type, module_output_buffer);
	send_data_type size_to_send;

	size_to_send = b[2];
	if (size_to_send > 0 && Modules) {
		/* lets make sure that we will not overrun the buffer : */
		realloc_vector (&module_output_buffer, size_to_send);
		b = VECTOR_HEAD (send_data_type, module_output_buffer);
		LOCAL_DEBUG_OUT ("sending %ld words to module # %d of %d",
										 size_to_send, channel, MODULES_NUM);
		if (channel >= 0 && channel < MODULES_NUM)
			PositiveWrite (channel, b, size_to_send * sizeof (send_data_type));
		else {
			register int i = MODULES_NUM;
			while (--i >= 0) {
				LOCAL_DEBUG_OUT ("sending to module %d ...", i);
				PositiveWrite (i, b, size_to_send * sizeof (send_data_type));
				LOCAL_DEBUG_OUT ("done sending to module %d ...", i);
			}
		}
	}
}


