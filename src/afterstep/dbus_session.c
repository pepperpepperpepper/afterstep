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

#include "dbus_internal.h"
/*****************************************************************************/
/* Gnome Session Manager's dbus Protocol :

1) send messages to org.gnome.SessionManager :

Setenv ("IMAGE_PATH", image_path)
Setenv ("FONT_PATH", font_path)

RegisterClient ( app_id, client_startup_id, object_path (returned))

2) If Quit/Quit is selected, send message :
Logout (0);

3) If Quit/Shutdown is selected :
Shutdown ();

4) What to do if Quit/Restart? send UnregisterClient() ?

Dbus calls :

dbus_message_new_method_call ("org.gnome.SessionManager",
							  "/org/gnome/SessionManager",
							  "org.gnome.SessionManager",
							  name_of_the_message );

use dbus_message_append_args (msg, type1, arg1, ..., DBUS_TYPE_INVALID) if needed.
use dbus_message_set_no_replay (msg, NULL) if no return value expected.

use dbus__connection_send () to send it.

*/

static inline Bool set_sm_client_id (DBusMessageIter * iter,
																		 const char *sm_client_id)
{
	if (sm_client_id == NULL || sm_client_id[0] == '\0')
		return False;
	show_progress
			("Using \"%s\" as the client ID for registration with the Session Manager",
			 sm_client_id);
	dbus_message_iter_append_basic (iter, DBUS_TYPE_STRING, &sm_client_id);
	return True;
}

char *asdbus_RegisterSMClient (const char *sm_client_id)
{
	char *client_path = NULL;

#ifdef HAVE_DBUS_CONTEXT
	if (ASDBus.session_conn) {
		DBusMessage *message =
				dbus_message_new_method_call (SESSIONMANAGER_NAME,
																			SESSIONMANAGER_PATH,
																			SESSIONMANAGER_INTERFACE,
																			"RegisterClient");
		if (message) {
			DBusMessage *replay;
			DBusError error;
			char *app_id = AFTERSTEP_APP_ID;
			DBusMessageIter iter;

			dbus_message_iter_init_append (message, &iter);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &app_id);

			if (!set_sm_client_id (&iter, sm_client_id)) {
				if (!set_sm_client_id (&iter, getenv (SESSION_ID_ENVVAR))) {
					char *instance_id =
							safemalloc (sizeof (AFTERSTEP_APP_ID) + 1 + 32);
					sprintf (instance_id, "%s-%d", AFTERSTEP_APP_ID, getpid ());
					set_sm_client_id (&iter, instance_id);
					free (instance_id);
				}
			}

			dbus_error_init (&error);
			replay =
					dbus_connection_send_with_reply_and_block (ASDBus.session_conn,
																										 message, 20000, /* startup is busy time - better give gnome-session time to reply */
																										 &error);
			dbus_message_unref (message);

			if (!replay) {
				show_error
						("Failed to register as a client with the Gnome Session Manager. DBus error: %s",
						 dbus_error_is_set (&error) ? error.message : "unknown error");
				if (dbus_error_is_set (&error))
					dbus_error_free (&error);
			} else {									/* get the allocated session ClientID */
				char *ret_client_path;
				if (!dbus_message_get_args
						(replay, &error, DBUS_TYPE_OBJECT_PATH, &ret_client_path,
						 DBUS_TYPE_INVALID))
					show_error
							("Malformed registration replay from Gnome Session Manager. DBus error: %s",
							 dbus_error_is_set (&error) ? error.
							 message : "unknown error");
				else {
					client_path = mystrdup (ret_client_path);
					ASDBus.gnomeSessionPath = mystrdup (ret_client_path);
				}

				if (dbus_error_is_set (&error))
					dbus_error_free (&error);
				dbus_message_unref (replay);
			}
		}
	}
#endif
	return client_path;
}

void asdbus_UnregisterSMClient (const char *sm_client_path)
{
#ifdef HAVE_DBUS_CONTEXT
	if (ASDBus.session_conn && sm_client_path) {
		DBusMessage *message =
				dbus_message_new_method_call (SESSIONMANAGER_NAME,
																			SESSIONMANAGER_PATH,
																			SESSIONMANAGER_INTERFACE,
																			"UnregisterClient");
		if (message) {
			DBusMessage *replay;
			DBusError error;
			DBusMessageIter iter;

			dbus_message_iter_init_append (message, &iter);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_OBJECT_PATH,
																			&sm_client_path);
			dbus_error_init (&error);
			replay =
					dbus_connection_send_with_reply_and_block (ASDBus.session_conn,
																										 message, 200, &error);
			dbus_message_unref (message);

			if (!replay)
				show_error
						("Failed to unregister as a client with the Gnome Session Manager. DBus error: %s",
						 dbus_error_is_set (&error) ? error.message : "unknown error");
			else {										/* nothing is returned in replay to this message */
				show_progress ("Unregistered from Gnome Session Manager");
				dbus_message_unref (replay);
			}
			if (dbus_error_is_set (&error))
				dbus_error_free (&error);
		}
	}
#endif
}

void asdbus_EndSessionOk ()
{
		show_debug(__FILE__, __FUNCTION__, __LINE__, "dbus EndSessionOk");
#ifdef HAVE_DBUS_CONTEXT
	if (ASDBus.session_conn) {
		DBusMessage *message =
				dbus_message_new_method_call (SESSIONMANAGER_NAME,
																			ASDBus.gnomeSessionPath,	/*"/org/gnome/SessionManager", */
																			IFACE_SESSION_PRIVATE,
																			"EndSessionResponse");
		show_debug(__FILE__, __FUNCTION__, __LINE__, "dbus EndSessionResponse to iface = \"%s\", path = \"%s\", manager = \"%s\"", 
			    IFACE_SESSION_PRIVATE, ASDBus.gnomeSessionPath, SESSIONMANAGER_NAME);
		if (message) {
			DBusMessageIter iter;
			char *reason = "";
			dbus_bool_t ok = 1;
			dbus_uint32_t msg_serial;

			dbus_message_iter_init_append (message, &iter);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &ok);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &reason);
			dbus_message_set_no_reply (message, TRUE);
			if (!dbus_connection_send
					(ASDBus.session_conn, message, &msg_serial))
				show_error ("Failed to send EndSession indicator.");
			else
				show_progress ("Sent Ok to end session (time, %ld).", time (NULL));
			dbus_message_unref (message);
		}
	}
#endif
}


void *asdbus_SendSimpleCommandSync (ASDBusOjectDescr *descr, const char *command, int timeout)
{
	void *reply = NULL;

#ifdef HAVE_DBUS_CONTEXT
	DBusConnection *conn = descr->systemBus ? ASDBus.system_conn : ASDBus.session_conn;
	if (conn) {
		DBusMessage *message =
				dbus_message_new_method_call (descr->name,
																			descr->path,
																			descr->interface,
                         						  command);
		if (message) {
			DBusError error;

			dbus_error_init (&error);
			reply = dbus_connection_send_with_reply_and_block (conn, message, timeout, &error);
			dbus_message_unref (message);

			if (!reply) {
					show_error ("Request %s to %s failed: %s", command, descr->displayName,
				  						 dbus_error_is_set (&error) ? error.message : "unknown error");
			}
			if (dbus_error_is_set (&error))
				dbus_error_free (&error);
		}
	}
#endif
	return reply;
}

Bool asdbus_SendSimpleCommandSyncNoRep (ASDBusOjectDescr *descr, const char *command, int timeout)
{
  Bool res = False;
#ifdef HAVE_DBUS_CONTEXT
	DBusMessage *reply = asdbus_SendSimpleCommandSync (descr, command, -1);
	if (reply)
	{
		res = True;
    dbus_message_unref (reply);
	}
#endif
	return res;
}

static Bool asdbus_TryGetBoolProperty (ASDBusOjectDescr *descr, const char *property,
																			Bool *out_value)
{
#ifdef HAVE_DBUS_CONTEXT
	DBusConnection *conn;
	DBusMessage *message;
	DBusMessage *reply;
	DBusError error;
	DBusMessageIter iter;
	DBusMessageIter variant;
	const char *interface;
	const char *property_name;

	if (out_value)
		*out_value = False;
	if (descr == NULL || property == NULL || out_value == NULL)
		return False;

	conn = descr->systemBus ? ASDBus.system_conn : ASDBus.session_conn;
	if (conn == NULL)
		return False;

	message =
			dbus_message_new_method_call (descr->name, descr->path,
																		DBUS_INTERFACE_PROPERTIES, "Get");
	if (message == NULL)
		return False;

	interface = descr->interface;
	property_name = property;
	dbus_message_iter_init_append (message, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &interface);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &property_name);

	dbus_error_init (&error);
	reply = dbus_connection_send_with_reply_and_block (conn, message, -1, &error);
	dbus_message_unref (message);

	if (reply == NULL) {
		if (dbus_error_is_set (&error))
			dbus_error_free (&error);
		return False;
	}

	if (!dbus_message_iter_init (reply, &iter)
			|| dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_VARIANT) {
		dbus_message_unref (reply);
		if (dbus_error_is_set (&error))
			dbus_error_free (&error);
		return False;
	}

	dbus_message_iter_recurse (&iter, &variant);
	if (dbus_message_iter_get_arg_type (&variant) == DBUS_TYPE_BOOLEAN) {
		dbus_bool_t ok = 0;
		dbus_message_iter_get_basic (&variant, &ok);
		*out_value = ok ? True : False;
		dbus_message_unref (reply);
		if (dbus_error_is_set (&error))
			dbus_error_free (&error);
		return True;
	}

	dbus_message_unref (reply);
	if (dbus_error_is_set (&error))
		dbus_error_free (&error);
	return False;
#else
	(void)descr;
	(void)property;
	(void)out_value;
	return False;
#endif
}

Bool asdbus_GetIndicator (ASDBusOjectDescr *descr, const char *command)
{
  Bool res = False;
#ifdef HAVE_DBUS_CONTEXT
	DBusMessage *reply = asdbus_SendSimpleCommandSync (descr, command, -1);
	if (reply)
	{
		DBusMessageIter iter;
    dbus_bool_t ok = 0;

    dbus_message_iter_init (reply, &iter);
    dbus_message_iter_get_basic (&iter, &ok);
		res = ok;
    dbus_message_unref (reply);
	}
#endif
	return res;
}


static Bool asdbus_LogoutKDE (KDE_ShutdownConfirm confirm, KDE_ShutdownMode mode, KDE_ShutdownType type, int timeout)
{
	Bool requested = False;
#ifdef HAVE_DBUS_CONTEXT
	if (ASDBus.session_conn) {
		DBusMessage *message =
				dbus_message_new_method_call (KSMSERVER_NAME,
																			KSMSERVER_PATH,
																			KSMSERVER_INTERFACE,
																			"logout");
		if (message) {
			DBusMessageIter iter;
			dbus_uint32_t msg_serial;
			dbus_int32_t i32_confirm = confirm;
			dbus_int32_t i32_mode = mode;
			dbus_int32_t i32_type = type;

			dbus_message_iter_init_append (message, &iter);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_INT32, &i32_confirm);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_INT32, &i32_mode);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_INT32, &i32_type);

			dbus_message_set_no_reply (message, TRUE);
			if (!dbus_connection_send	(ASDBus.session_conn, message, &msg_serial))
				show_error ("Failed to send Logout request to KDE.");
			else {
				requested = True;
				show_progress ("Sent Ok to end session (time, %ld).", time (NULL));
			}
			dbus_message_unref (message);
		}
	}
#endif
	return requested;
}

static Bool asdbus_LogoutGNOME (int mode, int timeout)
{
	Bool requested = False;
#ifdef HAVE_DBUS_CONTEXT
	if (ASDBus.session_conn) {
		DBusMessage *message =
				dbus_message_new_method_call (SESSIONMANAGER_NAME,
																			SESSIONMANAGER_PATH,
																			SESSIONMANAGER_INTERFACE,
																			"Logout");
		if (message) {
			DBusMessage *replay;
			DBusError error;
			DBusMessageIter iter;
			dbus_uint32_t ui32_mode = mode;

			dbus_message_iter_init_append (message, &iter);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_UINT32, &ui32_mode);
			dbus_error_init (&error);
			replay =
					dbus_connection_send_with_reply_and_block (ASDBus.session_conn,
																										 message, timeout,
																										 &error);
			dbus_message_unref (message);

			if (!replay) {
				show_error
						("Failed to request Logout with the Gnome Session Manager. DBus error: %s",
						 dbus_error_is_set (&error) ? error.message : "unknown error");
			} else {										/* nothing is returned in replay to this message */
				dbus_message_unref (replay);
				requested = True;
			}
			if (dbus_error_is_set (&error))
				dbus_error_free (&error);
		}
	}
#endif
	return requested;
}

Bool asdbus_Logout (ASDbusLogoutMode mode, int timeout)
{
	Bool requested = False;
#ifdef HAVE_DBUS_CONTEXT
	if (ASDBus.session_conn) {
		if (ASDBus.gnomeSessionPath != NULL)
			requested = asdbus_LogoutGNOME (mode, timeout);
		else if (ASDBus.kdeSessionVersion >= 4) {
			KDE_ShutdownConfirm kdeConfirm = KDE_ShutdownConfirmDefault;
			KDE_ShutdownMode kdeMode = KDE_ShutdownModeDefault;
			if (mode == ASDBUS_Logout_Confirmed) {
				kdeConfirm = KDE_ShutdownConfirmNo;
				kdeMode = KDE_ShutdownModeTryNow;
			} else if (mode == ASDBUS_Logout_Force) {
				kdeConfirm = KDE_ShutdownConfirmNo;
				kdeMode = KDE_ShutdownModeForceNow;
			}
			requested = asdbus_LogoutKDE (kdeConfirm, kdeMode, KDE_ShutdownTypeNone, timeout);
		}
	}
#endif
	return requested;
}

Bool asdbus_GetCanLogout ()
{
	return (ASDBus.kdeSessionVersion >= 4 || ASDBus.gnomeSessionPath != NULL);
}

Bool asdbus_Shutdown (int timeout)
{
	Bool requested = False;
#ifdef HAVE_DBUS_CONTEXT
	if (ASDBus.session_conn) {
		if (ASDBus.gnomeSessionPath != NULL)
			requested = asdbus_SendSimpleCommandSyncNoRep (&dbusSessionManager, "Shutdown", timeout);
		else if (ASDBus.kdeSessionVersion >= 4)
			requested = asdbus_LogoutKDE (KDE_ShutdownConfirmDefault, KDE_ShutdownModeDefault, KDE_ShutdownTypeHalt, timeout);
	}
#endif
	return requested;
}

Bool asdbus_GetCanShutdown ()
{
	return (ASDBus.kdeSessionVersion >= 4
	        || (ASDBus.gnomeSessionPath != NULL && asdbus_GetIndicator (&dbusSessionManager, "CanShutdown")));
}

Bool asdbus_Suspend (int timeout)
{
	return asdbus_SendSimpleCommandSyncNoRep (&dbusUPower, "Suspend", timeout);
}

Bool asdbus_GetCanSuspend ()
{
	Bool res = False;
	if (asdbus_TryGetBoolProperty (&dbusUPower, "CanSuspend", &res))
		return res;
	return asdbus_GetIndicator (&dbusUPower, "SuspendAllowed");
}

Bool asdbus_Hibernate (int timeout)
{
	return asdbus_SendSimpleCommandSyncNoRep (&dbusUPower, "Hibernate", timeout);
}

Bool asdbus_GetCanHibernate ()
{
	Bool res = False;
	if (asdbus_TryGetBoolProperty (&dbusUPower, "CanHibernate", &res))
		return res;
	return asdbus_GetIndicator (&dbusUPower, "HibernateAllowed");
}

/*******************************************************************************
 * Freedesktop Console Kit
 *******************************************************************************/
char* asdbus_GetConsoleSessionId ()
{
  char     *session_id = NULL;
#ifdef HAVE_DBUS_CONTEXT
	if (ASDBus.system_conn) {
		DBusMessage *message = dbus_message_new_method_call (CK_NAME,
                        						                     CK_MANAGER_PATH,
                                      						       CK_MANAGER_INTERFACE,
                                            						 "GetCurrentSession");
    if (message) {
			DBusMessage *reply;
			DBusError error;
			DBusMessageIter iter;
      const char     *val = NULL;

  	  dbus_error_init (&error);
      reply = dbus_connection_send_with_reply_and_block (ASDBus.system_conn, message, -1, &error);
			dbus_message_unref (message);

      if (reply == NULL) {
				if (dbus_error_is_set (&error))
					show_error ("Unable to determine Console Kit Session: %s", error.message);
			} else {
        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &val);
				session_id = mystrdup (val);
        dbus_message_unref (reply);
			}
			if (dbus_error_is_set (&error))
				dbus_error_free (&error);
		}
	}
#endif
	return session_id;
}

char* asdbus_GetConsoleSessionType (const char *session_id)
{
  char     *session_type = NULL;
#ifdef HAVE_DBUS_CONTEXT
	if (ASDBus.system_conn && session_id) {
		DBusMessage *message = dbus_message_new_method_call (CK_NAME,
																													session_id,
																													CK_SESSION_INTERFACE,
																													"GetSessionType");
    if (message) {
			DBusMessage *reply;
			DBusError error;

			dbus_error_init (&error);
			reply = dbus_connection_send_with_reply_and_block (ASDBus.system_conn, message, -1, &error);
			dbus_message_unref (message);

      if (reply == NULL) {
				if (dbus_error_is_set (&error))
					show_error ("Unable to determine Console Kit Session Type: %s", error.message);
			} else {
				DBusMessageIter iter;
  	    const char     *val = NULL;
        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &val);
				session_type = mystrdup (val);
/*				show_progress ("sess_type returned = \"%s\", arg_type = \"%c\"", val, dbus_message_iter_get_arg_type (&iter)); */
        dbus_message_unref (reply);
			}
			if (dbus_error_is_set (&error))
				dbus_error_free (&error);
		}
	}
#endif
	return session_type;
}
/*******************************************************************************
 * Notifications
 *******************************************************************************/
void asdbus_Notify (const char *summary, const char *body, int timeout)
{
#ifdef HAVE_DBUS_CONTEXT
	if (ASDBus.session_conn) {
		DBusMessage *message = dbus_message_new_method_call  ("org.freedesktop.Notifications",
																													"/org/freedesktop/Notifications",
																													"org.freedesktop.Notifications",
																													"Notify");
		if (message) {
			const char *app_id = "AfterStep Window Manager";
			dbus_uint32_t replace_id = 0;
			const char *icon = "";
			DBusMessageIter iter;

			dbus_message_iter_init_append (message, &iter);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &app_id);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_UINT32,
																			&replace_id);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &icon);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &summary);
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &body);
			/* Must add the following even if we don't need it */
			{
				DBusMessageIter sub;
				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
																					DBUS_TYPE_STRING_AS_STRING,
																					&sub);
				dbus_message_iter_close_container (&iter, &sub);
				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY, "{sv}",
																					&sub);
				dbus_message_iter_close_container (&iter, &sub);
			}
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_INT32, &timeout);

			{
				DBusMessage *replay;
				DBusError error;

				dbus_error_init (&error);
				replay = dbus_connection_send_with_reply_and_block (ASDBus.session_conn,
																											 message, 200,
																											 &error);
				dbus_message_unref (message);

				if (!replay) {
					show_error ("Failed to send the notification. DBus error: %s",
											dbus_error_is_set (&error) ? error.message : "unknown error");
				} else {									/* get the allocated session ClientID */
					dbus_uint32_t req_id;
					if (!dbus_message_get_args(replay, &error, DBUS_TYPE_UINT32, &req_id, DBUS_TYPE_INVALID)) {
						show_error ("Malformed Notification replay. DBus error: %s",
												dbus_error_is_set (&error) ? error.message : "unknown error");
					}
#ifdef TEST_AS_DBUS
					else {
						show_progress ("Notification Request_id = %d", req_id);
					}
#endif
					dbus_message_unref (replay);
				}
				if (dbus_error_is_set (&error))
					dbus_error_free (&error);
			}
		}
	}
#endif
}


/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

#ifdef TEST_AS_DBUS
int ASDBus_fd = -1;
char *GnomeSessionClientID = NULL;

void asdbus_GetClients ()
{
#ifdef HAVE_DBUS_CONTEXT
	if (ASDBus.session_conn) {
		DBusMessageIter args;
		DBusPendingCall *pending;
		int current_type;

		DBusMessage *msg =
				dbus_message_new_method_call ("org.gnome.SessionManager",
																			"/org/gnome/SessionManager",
																			"org.gnome.SessionManager",
																			"GetClients");
		if (msg == NULL) {
			show_error ("Message Null");
			return;
		}
		// send message and get a handle for a reply
		if (!dbus_connection_send_with_reply (ASDBus.session_conn, msg, &pending, -1)) {	// -1 is default timeout
			show_error ("Out Of Memory!");
			return;
		}
		if (NULL == pending) {
			show_error ("Pending Call Null");
			return;
		}
		dbus_connection_flush (ASDBus.session_conn);

		// free message
		dbus_message_unref (msg);

		// block until we receive a reply
		dbus_pending_call_block (pending);

		// get the reply message
		msg = dbus_pending_call_steal_reply (pending);
		if (NULL == msg) {
			show_error ("Reply Null");
			return;
		}
		// free the pending message handle
		dbus_pending_call_unref (pending);

		// read the parameters
		if (!dbus_message_iter_init (msg, &args))
			show_error ("Message has no arguments!");
		while ((current_type =
						dbus_message_iter_get_arg_type (&args)) != DBUS_TYPE_INVALID) {
			show_progress ("current_type: %c", current_type, DBUS_TYPE_INVALID);
			if (current_type == DBUS_TYPE_ARRAY) {
				DBusMessageIter item;
				int item_type;
				dbus_message_iter_recurse (&args, &item);
				while ((item_type =
								dbus_message_iter_get_arg_type (&item)) !=
							 DBUS_TYPE_INVALID) {
					show_progress ("item_type: %c", item_type);
					if (item_type == DBUS_TYPE_OBJECT_PATH) {
						char *str = NULL;
						dbus_message_iter_get_basic (&item, &str);
						show_progress ("\t value = \"%s\"", str);
					}
					dbus_message_iter_next (&item);
				}
			} else if (current_type == DBUS_TYPE_STRING) {
				char *str = NULL;
				dbus_message_iter_get_basic (&args, &str);
				show_progress ("\t value = \"%s\"", str);
			}
			dbus_message_iter_next (&args);
		}

		// free reply and close connection
		dbus_message_unref (msg);
	}
#endif
}

void Done (Bool restart, char *cmd)
{
	exit (1);
}
void SaveSession (Bool force)
{
}

void CloseSessionClients (Bool only_modules)
{}

int main (int argc, char **argv)
{
	int logout_mode = -1;
	Bool shutdown_mode = False;
	char *console_session_id, *console_session_type;

	if (argc > 1) {
		if (strcmp (argv[1], "--logout") == 0) {
			if (argc > 2)
				logout_mode = atoi (argv[2]);
			else
				logout_mode = 0;
		}else if (strcmp (argv[1], "--shutdown") == 0)
			shutdown_mode = True;
	}
	InitMyApp ("test_asdbus", argc, argv, NULL, NULL, 0);

	ASDBus_fd = asdbus_init ();
	if (ASDBus_fd < 0) {
		show_error ("Failed to accure Session DBus connection.");
		return 0;
	}
	show_progress ("Successfuly accured Session DBus connection.");

	change_func_code ("Restart", F_NOP);
	if (!asdbus_GetCanShutdown())
		change_func_code ("SystemShutdown", F_NOP);

	console_session_id = asdbus_GetConsoleSessionId ();
	console_session_type = asdbus_GetConsoleSessionType (console_session_id);
	show_progress ("ConsoleKit session id is \"%s\", type = \"%s\"", console_session_id, console_session_type);
	show_progress ("CanLogout = %d", asdbus_GetCanLogout());
	show_progress ("CanShutdown = %d", asdbus_GetCanShutdown());
	show_progress ("CanSuspend = %d", asdbus_GetCanSuspend());
	show_progress ("CanHibernate = %d", asdbus_GetCanHibernate());

	asdbus_Notify ("TestNotification Summary", "Test notification body",
								 3000);

	GnomeSessionClientID = asdbus_RegisterSMClient (NULL);

	show_progress ("gnome-session Autosave set to %d", 	get_gnome_autosave ());
	if (GnomeSessionClientID != NULL)
		show_progress
				("Successfuly registered with GNOME Session Manager with Client Path \"%s\".",
				 GnomeSessionClientID);

	if (GnomeSessionClientID != NULL) {
		asdbus_GetClients ();
		asdbus_UnregisterSMClient (GnomeSessionClientID);
	}

	if (logout_mode >= 0 && logout_mode <= 2)
		asdbus_Logout (logout_mode, 100000);
	else if (shutdown_mode)
		asdbus_Shutdown (100000);


	asdbus_shutdown ();

	return 1;
}
#endif
