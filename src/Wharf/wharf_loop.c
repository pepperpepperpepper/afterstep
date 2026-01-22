#undef DO_CLOCKING
#ifndef LOCAL_DEBUG
#define LOCAL_DEBUG
#endif
#ifndef EVENT_TRACE
#define EVENT_TRACE
#endif

#include "../../configure.h"
#include "../../libAfterStep/asapp.h"
#include "../../libAfterStep/event.h"
#include "../../libAfterStep/module.h"

void DispatchEvent (ASEvent * Event);
void process_message (send_data_type type, send_data_type * body);

void HandleEvents ()
{
	ASEvent event;
	Bool has_x_events = False;
	while (True) {
		while ((has_x_events = XPending (dpy))) {
			if (ASNextEvent (&(event.x), True)) {
				event.client = NULL;
				setup_asevent_from_xevent (&event);
				DispatchEvent (&event);
				timer_handle ();
			}
		}
		module_wait_pipes_input (process_message);
	}
}

void MapConfigureNotifyLoop ()
{
	ASEvent event;

	do {
		if (!ASCheckTypedEvent (MapNotify, &(event.x)))
			if (!ASCheckTypedEvent (ConfigureNotify, &(event.x)))
				return;

		event.client = NULL;
		setup_asevent_from_xevent (&event);
		DispatchEvent (&event);
		ASSync (False);
	} while (1);
}
