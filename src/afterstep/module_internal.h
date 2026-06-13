#ifndef AFTERSTEP_MODULE_INTERNAL_H
#define AFTERSTEP_MODULE_INTERNAL_H

/* Shared private symbols for the module.c <-> module_control.c split.
 * None of these are part of any public header.
 *
 * The low-level IPC engine (socket setup, queueing, packet I/O, the shared
 * output buffer and the message-header builder) stays in module.c; the
 * high-level spawn/control/broadcast/menu API moves to module_control.c,
 * which calls back into the engine through the declarations below. */

#include "asinternals.h"
#include "../../libAfterStep/module.h"	/* send_data_type, MSG_HEADER_SIZE */

/* Shared output buffer (defined, de-static'd, in module.c). */
extern ASVector module_output_buffer;

/* Low-level engine entry points (defined in module.c, used by module_control.c). */
Bool module_setup_socket (void);
void KillModule (module_t * module, Bool dont_free_memory);
int HandleModuleInput (module_t * module);
int FlushQueue (module_t * module);
void SendBuffer (int channel);

/* De-static'd message-header builder (defined in module.c). */
send_data_type *make_msg_header (send_data_type msg_type, send_data_type size);

#endif /* AFTERSTEP_MODULE_INTERNAL_H */
