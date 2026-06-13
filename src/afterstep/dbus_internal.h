#ifndef DBUS_INTERNAL_H
#define DBUS_INTERNAL_H

/* Shared private environment for the dbus.c <-> dbus_session.c split.
 *
 * dbus_session.c (the session-manager / power / command API) includes this
 * AFTER asinternals.h to obtain the DBus macro/type/global environment that
 * originally lived inline at the top of dbus.c, inside the HAVE_DBUS1 guard.
 * dbus.c keeps that environment inline and does NOT include this header (so
 * the typedefs are not defined twice in one TU); it only drops `static` from
 * the three shared globals below so the session side can link against them.
 *
 * Relies on asinternals.h for Bool / ASVector / ASBiDirList. */

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

/* de-static'd globals, defined in dbus.c */
extern ASDBusOjectDescr dbusSessionManager;
extern ASDBusOjectDescr dbusUPower;
extern ASDBusContext ASDBus;

#endif				/* HAVE_DBUS1 */
#endif				/* DBUS_INTERNAL_H */
