#ifndef CLIENTPROPS_INTERNAL_H
#define CLIENTPROPS_INTERNAL_H

/* Shared across the clientprops.c family (clientprops.c / clientprops_read.c
 * / clientprops_write.c). None of these are part of the public
 * clientprops.h API. The AtomXref atom cross-reference tables and the
 * hint-handler hash are defined in clientprops.c and consumed by the hint
 * readers (clientprops_read.c) and the property setters (clientprops_write.c);
 * parent_hints_func is the pluggable parent-hint callback used and set by the
 * reading code. */

/* Internal atoms defined in clientprops.c but not exported via clientprops.h,
 * referenced by the hint readers / handler table in clientprops_read.c (and
 * the WM_NAME/ICON_NAME setters in clientprops_write.c). */
extern Atom _XA_WM_NAME;
extern Atom _XA_WM_ICON_NAME;
extern Atom _XA_WM_CLASS;
extern Atom _XA_WM_HINTS;
extern Atom _XA_WM_NORMAL_HINTS;
extern Atom _XA_WM_TRANSIENT_FOR;
extern Atom _XA_WM_COMMAND;
extern Atom _XA_WM_CLIENT_MACHINE;
extern Atom _XA_KDE_DESKTOP_WINDOW;
extern Atom _XA_KDE_NET_SYSTEM_TRAY_WINDOW_FOR;

extern AtomXref MainHints[];
extern AtomXref WM_Protocols[];
extern AtomXref EXTWM_WindowType[];
extern AtomXref EXTWM_Protocols[];
extern ASHashTable *hint_handlers;
extern get_parent_hints_func parent_hints_func;

#endif /* CLIENTPROPS_INTERNAL_H */
