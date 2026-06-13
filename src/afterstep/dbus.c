/****************************************************************************
 * Copyright (c) 2008 Sasha Vasko <sasha at aftercode.net>
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
 */

#include "../../configure.h"
#define LOCAL_DEBUG

#include "asinternals.h"

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#undef HAVE_DBUS_CONTEXT

#if defined(HAVE_GIOLIB) && defined(HAVE_GSETTINGS)
# include <gio/gio.h>
# define GSM_MANAGER_SCHEMA        "org.gnome.SessionManager"
# define KEY_AUTOSAVE              "auto-save-session"
#endif

#ifdef HAVE_DBUS1
# include "dbus/dbus.h"

# ifndef TEST_AS_DBUS
#  define AFTERSTEP_APP_ID			            "afterstep"
# else
#  define AFTERSTEP_APP_ID			            "afterstep-test"
# endif

#undef ASDBUS_DISPATCH

#define AFTERSTEP_DBUS_SERVICE_NAME	      "org.afterstep." AFTERSTEP_APP_ID
#define AFTERSTEP_DBUS_INTERFACE			    "org.afterstep." AFTERSTEP_APP_ID
#define AFTERSTEP_DBUS_ROOT_PATH			    "/org/afterstep/" AFTERSTEP_APP_ID

#define SESSIONMANAGER_NAME				"org.gnome.SessionManager"
#define SESSIONMANAGER_PATH				"/org/gnome/SessionManager"
#define SESSIONMANAGER_INTERFACE	"org.gnome.SessionManager"
#define IFACE_SESSION_PRIVATE 		SESSIONMANAGER_INTERFACE ".ClientPrivate"

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      CK_PATH "/Manager"
#define CK_MANAGER_INTERFACE CK_NAME ".Manager"
#define CK_SEAT_INTERFACE    CK_NAME ".Seat"
#define CK_SESSION_INTERFACE CK_NAME ".Session"

#define UPOWER_NAME 			"org.freedesktop.UPower"
#define UPOWER_PATH 			"/org/freedesktop/UPower"
#define UPOWER_INTERFACE	"org.freedesktop.UPower"

#define KSMSERVER_NAME 			"org.kde.ksmserver"
#define KSMSERVER_PATH 			"/KSMServer"
#define KSMSERVER_INTERFACE	"org.kde.KSMServerInterface"

typedef enum  {
	KDE_ShutdownConfirmDefault = -1,
	KDE_ShutdownConfirmNo = 0,
	KDE_ShutdownConfirmYes = 1
}KDE_ShutdownConfirm;

typedef enum {
  KDE_ShutdownModeDefault = -1,
  KDE_ShutdownModeSchedule = 0,
  KDE_ShutdownModeTryNow = 1,
  KDE_ShutdownModeForceNow = 2,
  KDE_ShutdownModeInteractive = 3
}	KDE_ShutdownMode;

typedef enum {
  KDE_ShutdownTypeDefault = -1,
  KDE_ShutdownTypeNone = 0,
  KDE_ShutdownTypeReboot = 1,
 	KDE_ShutdownTypeHalt = 2,
  KDE_ShutdownTypeLogout = 3 /* unused - use None instead */
} KDE_ShutdownType;

typedef struct ASDBusOjectDescr {
	char *displayName;
	Bool systemBus;
	char *name;
	char *path;
	char *interface;
}ASDBusOjectDescr;

ASDBusOjectDescr dbusSessionManager = {"Session Manager", False, SESSIONMANAGER_NAME, SESSIONMANAGER_PATH, SESSIONMANAGER_INTERFACE };
ASDBusOjectDescr dbusUPower = {"Power Management Daemon", True, UPOWER_NAME, UPOWER_PATH, UPOWER_INTERFACE };

#define HAVE_DBUS_CONTEXT 1

typedef struct ASDBusContext {
	DBusConnection *system_conn;
	DBusConnection *session_conn;
	ASVector *watchFds;  // vector of ASDBusFds
	char *gnomeSessionPath;
	Bool sessionManagerCanShutdown;
	int kdeSessionVersion;
	ASBiDirList *dispatches;
} ASDBusContext;

ASDBusContext ASDBus = { NULL, NULL, NULL, NULL, False, 0, NULL };

static DBusHandlerResult asdbus_handle_message (DBusConnection *,
																								DBusMessage *, void *);

static DBusObjectPathVTable ASDBusMessagesVTable = {
	NULL, asdbus_handle_message,	/* handler function */
	NULL, NULL, NULL, NULL
};

/******************************************************************************/
/* internal stuff */
/******************************************************************************/
DBusHandlerResult
asdbus_handle_message (DBusConnection * conn, DBusMessage * msg,
											 void *data)
{
	Bool handled = False;

	show_progress ("Dbus message received from \"%s\", member \"%s\"",
								 dbus_message_get_interface (msg),
								 dbus_message_get_member (msg));

	if (dbus_message_is_signal
			(msg, "org.gnome.SessionManager", "SessionOver")) {
		dbus_message_unref (msg);
		handled = True;
		Done (False, NULL);
	}

	return handled ? DBUS_HANDLER_RESULT_HANDLED :
			DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

}

static DBusConnection *
_asdbus_get_session_connection()
{
	DBusError error;
	int res;
	DBusConnection *session_conn;
	dbus_error_init (&error);

	session_conn = dbus_bus_get (DBUS_BUS_SESSION, &error);

	if (dbus_error_is_set (&error)) {
		show_error ("Failed to connect to Session DBus: %s", error.message);
	} else {
		dbus_connection_set_exit_on_disconnect (session_conn, FALSE);
		res = dbus_bus_request_name (session_conn,
																 AFTERSTEP_DBUS_SERVICE_NAME,
																 DBUS_NAME_FLAG_REPLACE_EXISTING |
																 DBUS_NAME_FLAG_ALLOW_REPLACEMENT,
																 &error);
		if (dbus_error_is_set (&error)) {
			show_error ("Failed to request name from DBus: %s", error.message);
		} else if (res != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
			show_error ("Failed to request name from DBus - not a primary owner.");
		} else {
			dbus_connection_register_object_path (session_conn,
																						AFTERSTEP_DBUS_ROOT_PATH,
																						&ASDBusMessagesVTable, 0);
		}
	}
	if (dbus_error_is_set (&error))
		dbus_error_free (&error);

	return session_conn;
}

static DBusConnection *
_asdbus_get_system_connection()
{
	DBusError error;
	DBusConnection *sys_conn;

	dbus_error_init (&error);
	sys_conn = dbus_bus_get (DBUS_BUS_SYSTEM, &error);

	if (dbus_error_is_set (&error)) {
		show_error ("Failed to connect to System DBus: %s", error.message);
		dbus_error_free (&error);
	} else {
		dbus_connection_set_exit_on_disconnect (sys_conn, FALSE);
		show_progress ("Connected to System DBus.");
	}
	return sys_conn;
}

/******************************************************************************/
/* Watch functions */
/******************************************************************************/
static dbus_bool_t add_watch(DBusWatch *w, void *data)
{
    	if (!dbus_watch_get_enabled(w))
      		return TRUE;

	ASDBusFd *fd = safecalloc (1, sizeof(ASDBusFd));
	fd->fd =  dbus_watch_get_unix_fd(w);
	unsigned int flags = dbus_watch_get_flags(w);
	if (get_flags(flags, DBUS_WATCH_READABLE))
		fd->readable = True;
    /*short cond = EV_PERSIST;
    if (flags & DBUS_WATCH_READABLE)
        cond |= EV_READ;
    if (flags & DBUS_WATCH_WRITABLE)
        cond |= EV_WRITE; */

      // TODO add to the list of FDs
	dbus_watch_set_data(w, fd, NULL);
	if (ASDBus.watchFds == NULL)
		ASDBus.watchFds = create_asvector (sizeof(ASDBusFd*));

	append_vector(ASDBus.watchFds, &fd, 1);

	show_debug(__FILE__,__FUNCTION__,__LINE__,"added dbus watch fd=%d watch=%p readable =%d\n", fd->fd, w, fd->readable);
	return TRUE;
}

static void remove_watch(DBusWatch *w, void *data)
{
    ASDBusFd* fd = dbus_watch_get_data(w);

    vector_remove_elem (ASDBus.watchFds, &fd);
    dbus_watch_set_data(w, NULL, NULL);
    show_debug(__FILE__,__FUNCTION__,__LINE__,"removed dbus watch watch=%p\n", w);
}

static void toggle_watch(DBusWatch *w, void *data)
{
    show_debug(__FILE__,__FUNCTION__,__LINE__,"toggling dbus watch watch=%p\n", w);
    if (dbus_watch_get_enabled(w))
        add_watch(w, data);
    else
        remove_watch(w, data);
}
#ifdef ASDBUS_DISPATCH
typedef struct ASDBusDispatch {
	DBusConnection *connection;
	void *data;
}ASDBusDispatch;

static ASDBusDispatch *asdbus_create_dispatch(DBusConnection *connection, void *data){
    ASDBusDispatch *d = safecalloc (1, sizeof(ASDBusDispatch));
    d->data = data;
    d->connection = connection;
    return d;
}

static void queue_dispatch(DBusConnection *connection, DBusDispatchStatus new_status, void *data){
	if (new_status == DBUS_DISPATCH_DATA_REMAINS){
	        show_debug(__FILE__,__FUNCTION__,__LINE__,"ADDED dbus dispatch=%p\n", data);
		append_bidirelem (ASDBus.dispatches, asdbus_create_dispatch(connection, data));
	}
}
#endif
static void  asdbus_handle_timer (void *vdata) {
	show_debug(__FILE__,__FUNCTION__,__LINE__,"dbus_timeout_handle data=%p\n", vdata);
	dbus_timeout_handle (vdata);
}

static void asdbus_set_dbus_timer (struct timeval *expires, DBusTimeout *timeout) {
	int interval = dbus_timeout_get_interval(timeout);
	gettimeofday (expires, NULL);
	tv_add_ms(expires, interval);
	show_debug(__FILE__,__FUNCTION__,__LINE__,"time = %d, adding dbus timeout data=%p, interval = %d\n", time(NULL), timeout, interval);
	timer_new (interval, asdbus_handle_timer, timeout);
}

static dbus_bool_t add_timeout(DBusTimeout *timeout, void *data){
	/* add expiration data to timeout */
	struct timeval *expires = dbus_malloc(sizeof(struct timeval));
	if (!expires)
		return FALSE;
	dbus_timeout_set_data(timeout, expires, dbus_free);

	asdbus_set_dbus_timer (expires, timeout);
	return TRUE;
}

static void toggle_timeout(DBusTimeout *timeout, void *data){
	/* reset expiration data */
	struct timeval *expires = dbus_timeout_get_data(timeout);
	timer_remove_by_data (timeout);

	asdbus_set_dbus_timer (expires, timeout);
}

static void remove_timeout(DBusTimeout *timeout, void *data){
	show_debug(__FILE__,__FUNCTION__,__LINE__,"removing dbus timeout =%p\n", timeout);
	timer_remove_by_data (timeout);
}
#ifdef ASDBUS_DISPATCH

void asdbus_dispatch_destroy (void *data) {
    free (data);
}
#endif
static void _asdbus_add_match (DBusConnection *conn, const char* iface, const char* member) {
	char match[256];
	sprintf(match,	member?"type='signal',interface='%s',member='%s'":"type='signal',interface='%s'", iface, member);
    	DBusError error;
    	dbus_error_init(&error);
    	dbus_bus_add_match(conn, match, &error);
	show_debug(__FILE__,__FUNCTION__,__LINE__, "added match :[%s]", match);
    	if (dbus_error_is_set(&error)) {
      		show_error("dbus_bus_add_match() %s failed: %s\n",   member, error.message);
      		dbus_error_free(&error);
	}
}

/******************************************************************************/
/* External interfaces : */
/******************************************************************************/
Bool asdbus_init ()
{																/* return connection unix fd */
	char *tmp;
#ifdef ASDBUS_DISPATCH
	if (!ASDBus.dispatches)
		ASDBus.dispatches = create_asbidirlist(asdbus_dispatch_destroy);
#endif
	if (!ASDBus.session_conn) {
		ASDBus.session_conn = _asdbus_get_session_connection();
		if (!dbus_connection_set_watch_functions(ASDBus.session_conn, add_watch, remove_watch,  toggle_watch, ASDBus.session_conn, NULL)) {
		 	show_error("dbus_connection_set_watch_functions() failed");
		}
		_asdbus_add_match (ASDBus.session_conn,  SESSIONMANAGER_INTERFACE, NULL);
		//_asdbus_add_match (ASDBus.session_conn,  IFACE_SESSION_PRIVATE, "QueryEndSession");
		//_asdbus_add_match (ASDBus.session_conn,  IFACE_SESSION_PRIVATE, "EndSession");
		//_asdbus_add_match (ASDBus.session_conn,  IFACE_SESSION_PRIVATE, "Stop");
		dbus_connection_set_timeout_functions(ASDBus.session_conn, add_timeout, remove_timeout, toggle_timeout, NULL, NULL);
#ifdef ASDBUS_DISPATCH
		dbus_connection_set_dispatch_status_function(ASDBus.session_conn, queue_dispatch, NULL, NULL);
		queue_dispatch(ASDBus.session_conn, dbus_connection_get_dispatch_status(ASDBus.session_conn), NULL);
#endif
	}

	if (!ASDBus.system_conn){
		ASDBus.system_conn = _asdbus_get_system_connection();
		/*if (!dbus_connection_set_watch_functions(ASDBus.system_conn, add_watch, remove_watch,  toggle_watch, ASDBus.system_conn, NULL)) {
		 	show_error("dbus_connection_set_watch_functions() failed");
		}*/
	}

	/*if (ASDBus.session_conn && ASDBus.watchFds == NULL){
		//dbus_connection_get_unix_fd (ASDBus.session_conn, &(ASDBus.watch_fd));
		//dbus_whatch_get_unix_fd (ASDBus.session_conn, &(ASDBus.watch_fd));
	}*/

	if ((tmp = getenv ("KDE_SESSION_VERSION")) != NULL)
		ASDBus.kdeSessionVersion = atoi(tmp);

	return (ASDBus.session_conn != NULL);
}

ASVector* asdbus_getFds() {
	return ASDBus.watchFds;
}

void asdbus_handleDispatches (){
#ifdef ASDBUS_DISPATCH
	void *data;
	while ((data = extract_first_bidirelem (ASDBus.dispatches)) != NULL){
		ASDBusDispatch *d = (ASDBusDispatch*)data;
		while (dbus_connection_get_dispatch_status(d->data) == DBUS_DISPATCH_DATA_REMAINS){
			dbus_connection_dispatch(d->data);
			show_debug(__FILE__,__FUNCTION__,__LINE__,"dispatching dbus  data=%p\n", d->data);
		}
		free (d);
	}
#endif
}

void asdbus_shutdown ()
{
	if (ASDBus.session_conn)
		dbus_bus_release_name (ASDBus.session_conn,
													 AFTERSTEP_DBUS_SERVICE_NAME, NULL);

	if (ASDBus.session_conn || ASDBus.system_conn)
		dbus_shutdown ();
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
Bool get_gnome_autosave ()
{
	Bool autosave = False;
#ifdef HAVE_GIOLIB
	static Bool g_types_inited = False;
	if (!g_types_inited) {
#ifndef GLIB_VERSION_2_36
		g_type_init ();
#endif
		g_types_inited = True;
	}
	if (ASDBus.gnomeSessionPath) {
#if defined(HAVE_GSETTINGS)
		GSettings *gsm_settings = g_settings_new (GSM_MANAGER_SCHEMA);
		if (gsm_settings) {
			autosave = g_settings_get_boolean (gsm_settings, KEY_AUTOSAVE);
  		g_object_unref (gsm_settings);
		} else
			show_error (" Failed to get gnome-session Autosave settings");
#endif
	}
#endif
	return autosave;
}

/******************************************************************************/
void asdbus_EndSessionOk ();


void asdbus_process_messages (ASDBusFd* fd)
{
	//show_progress ("checking dbus messages for fd = %d", fd->fd);
#ifndef ASDBUS_DISPATCH
	while (ASDBus.session_conn) {
		DBusMessage *msg;
		const char *interface, *member;
		/* non blocking read of the next available message */
		dbus_connection_read_write (ASDBus.session_conn, 0);
		msg = dbus_connection_pop_message (ASDBus.session_conn);

		if (NULL == msg) {
			/* show_progress ("no more Dbus messages..."); */
			//show_progress("time(%ld):dbus message not received during the timeout - sleeping...", time (NULL));
			return;
		}
		interface = dbus_message_get_interface (msg);
		member = dbus_message_get_member (msg);
		show_debug(__FILE__,__FUNCTION__, __LINE__, "dbus msg iface = \"%s\", member = \"%s\"", interface?interface:"(nil)", member?member:"(nil)");
		if (interface == NULL || member == NULL) {
			show_progress ("time(%ld):dbus message cannot be parsed...",
										 time (NULL));
		} else {
			show_progress
					("time(%ld):dbus message received from \"%s\", member \"%s\"",
					 time (NULL), interface, member);
			if (strcmp (interface, IFACE_SESSION_PRIVATE) == 0) {
				if (strcmp (member, "QueryEndSession") == 0) {	/* must replay yes  within 10 seconds */
					asdbus_EndSessionOk ();
					asdbus_Notify ("Session is ending ...",
												 "Checking if it is safe to logout", 0);
					SaveSession (True);
				} else if (strcmp (member, "EndSession") == 0) {
					/*cover_desktop ();
					   display_progress (True, "Session is ending, please wait ..."); */
					asdbus_Notify ("Session is ending ...",
												 "Closing apps, please wait.", 0);
					dbus_connection_read_write (ASDBus.session_conn, 0);
					/* Yield to let other clients process Session Management requests */
					sleep_a_millisec (300);
					CloseSessionClients (False);
					/* we want to end to the very end */
				} else if (strcmp (member, "Stop") == 0) {
					asdbus_Notify ("Session is over.", "Bye-bye!", 0);
					dbus_connection_read_write (ASDBus.session_conn, 0);
					Done (False, NULL);
				}
			}
		}
#if 0
		if (dbus_message_is_method_call (msg, "test.method.Type", "Method"))
			reply_to_method_call (msg, conn);
#endif
		dbus_message_unref (msg);
	}
#else
	if (ASDBus.session_conn)
		do {
			dbus_connection_read_write_dispatch (ASDBus.session_conn, 0);
		} while (dbus_connection_get_dispatch_status (ASDBus.session_conn) ==
						 DBUS_DISPATCH_DATA_REMAINS);
#endif
}



/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
#else

int asdbus_init ()
{
	return -1;
}

void asdbus_shutdown ()
{
}

void asdbus_process_messages ()
{
};

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
#endif

