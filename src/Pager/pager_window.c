#include "pager_internal.h"

/********************************************************************/
/* showing our main window :                                        */
/********************************************************************/
Window make_pager_window ()
{
	Window w;
	XSizeHints shints;
	ExtendedWMHints extwm_hints;
	int x, y;
	int width = Config->geometry.width;
	int height = Config->geometry.height;
	XSetWindowAttributes attr;
	LOCAL_DEBUG_OUT ("configured geometry is %dx%d%+d%+d", width, height,
									 Config->geometry.x, Config->geometry.y);
	switch (Config->gravity) {
	case NorthEastGravity:
		x = Scr.MyDisplayWidth - width + Config->geometry.x;
		y = Config->geometry.y;
		break;
	case SouthEastGravity:
		x = Scr.MyDisplayWidth - width + Config->geometry.x;
		y = Scr.MyDisplayHeight - height + Config->geometry.y;
		break;
	case SouthWestGravity:
		x = Config->geometry.x;
		y = Scr.MyDisplayHeight - height + Config->geometry.y;
		break;
	case NorthWestGravity:
	default:
		x = Config->geometry.x;
		y = Config->geometry.y;
		break;
	}
	attr.event_mask =
			StructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
			PointerMotionMask;
	w = create_visual_window (Scr.asv, Scr.Root, x, y, width, height, 0,
														InputOutput, CWEventMask, &attr);
	set_client_names (w, MyName, MyName, AS_MODULE_CLASS, CLASS_PAGER);

	Scr.RootClipArea.x = x;
	Scr.RootClipArea.y = y;
	Scr.RootClipArea.width = width;
	Scr.RootClipArea.height = height;

	shints.flags = USSize | PMinSize | PResizeInc | PWinGravity;
	if (get_flags (Config->set_flags, PAGER_SET_GEOMETRY)
			|| get_flags (MyArgs.geometry.flags, XValue | YValue))
		shints.flags |= USPosition;
	else
		shints.flags |= PPosition;

	shints.min_width = Config->columns + Config->border_width;
	shints.min_height = Config->rows + Config->border_width;
	shints.width_inc = 1;
	shints.height_inc = 1;
	shints.win_gravity = Config->gravity;

	extwm_hints.pid = getpid ();
	extwm_hints.flags = EXTWM_PID | EXTWM_StateSet | EXTWM_TypeSet;
	extwm_hints.type_flags = EXTWM_TypeMenu | EXTWM_TypeASModule;
	extwm_hints.state_flags = EXTWM_StateSkipTaskbar | EXTWM_StateSkipPager;

	set_client_hints (w, NULL, &shints, AS_DoesWmDeleteWindow, &extwm_hints);
	set_client_cmd (w);

	/* showing window to let user see that we are doing something */
	XMapRaised (dpy, w);
	LOCAL_DEBUG_OUT ("mapping main window at %ux%u%+d%+d", width, height, x,
									 y);
	/* final cleanup */
	XFlush (dpy);
	sleep (1);										/* we have to give AS a chance to spot us */
	/* we will need to wait for PropertyNotify event indicating transition
	   into Withdrawn state, so selecting event mask: */
	XSelectInput (dpy, w, PropertyChangeMask | StructureNotifyMask);
	return w;
}
