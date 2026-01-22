#ifndef PAGER_INTERNAL_H_HEADER_INCLUDED
#define PAGER_INTERNAL_H_HEADER_INCLUDED

/* Keep this header self-contained so new compilation units can share Pager.c's
 * internal state without pulling in Pager.c itself. */

/*#define DO_CLOCKING      */
#ifndef LOCAL_DEBUG
#define LOCAL_DEBUG
#endif
#ifndef EVENT_TRACE
#define EVENT_TRACE
#endif

#include "../../configure.h"
#include "../../libAfterStep/asapp.h"
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
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
#define IN_MODULE
#define MODULE_X_INTERFACE

#include "../../libAfterStep/afterstep.h"
#include "../../libAfterStep/screen.h"
#include "../../libAfterStep/module.h"
#include "../../libAfterStep/parser.h"
#include "../../libAfterStep/mystyle.h"
#include "../../libAfterStep/mystyle_property.h"
#include "../../libAfterStep/balloon.h"
#include "../../libAfterStep/aswindata.h"
#include "../../libAfterStep/decor.h"
#include "../../libAfterStep/event.h"
#include "../../libAfterStep/wmprops.h"
#include "../../libAfterStep/moveresize.h"
#include "../../libAfterStep/shape.h"

#include "../../libAfterConf/afterconf.h"

/* pager flags  - shared between PagerDEsk and PagerState */
#define ASP_DeskShaded          (0x01<<0)
#define ASP_UseRootBackground   (0x01<<1)
#define ASP_Shaped              (0x01<<2)
#define ASP_ShapeDirty          (0x01<<3)
#define ASP_ReceivingWindowList (0x01<<4)

#define CLIENT_EVENT_MASK   StructureNotifyMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|KeyPressMask|KeyReleaseMask|EnterWindowMask|LeaveWindowMask


typedef struct ASPagerDesk {

	ASFlagType flags;
	INT32 desk;										/* absolute value - no need to add start_desk */
	ASCanvas *desk_canvas;
	ASTBarData *title;
	ASTBarData *background;
	Window *separator_bars;				/* (rows-1)*(columns-1) */
	XRectangle *separator_bar_rects;
	unsigned int separator_bars_num;
	unsigned int title_width, title_height;

	ASWindowData **clients;
	unsigned int clients_num;

	ASImage *back;
} ASPagerDesk;

typedef struct ASPagerState {
	ASFlagType flags;

	ASCanvas *main_canvas;
	ASCanvas *icon_canvas;

	ASPagerDesk *desks;
	INT32 start_desk, desks_num;

	int page_rows, page_columns;
	/* x and y size of desktop */
	int desk_width, desk_height;	/* adjusted for the size of title */
	/* area of the main window used up by labels, borders and other garbadge : */
	int wasted_width, wasted_height;
	/* x and y size of virtual screen size inside desktop mini-window */
	int vscreen_width, vscreen_height;
	int aspect_x, aspect_y;

	int wait_as_response;

	ASCanvas *pressed_canvas;
	ASTBarData *pressed_bar;
	int pressed_context;
	ASPagerDesk *pressed_desk;
	int pressed_button;

	ASPagerDesk *focused_desk;
	ASPagerDesk *resize_desk;			/* desk on which we are currently resizing the window */

	Window selection_bars[4];
	XRectangle selection_bar_rects[4];

	ASTBarProps *tbar_props;

#define C_ShadeButton 		C_TButton0

	MyButton shade_button;
} ASPagerState;

extern ASPagerState PagerState;
extern ASHashTable *PagerClients;
extern PagerConfig *Config;
extern int Rows_override;
extern int Columns_override;

/* Internal Pager helpers shared across compilation units. */
Window make_pager_window ();
ASPagerDesk *get_pager_desk (INT32 desk);
void restack_desk_windows (ASPagerDesk * d);
void place_client (ASPagerDesk * d, ASWindowData * wd, Bool force_redraw,
									 Bool dont_update_shape);
ASWindowData *fetch_client (Window w);
void unregister_client (Window w);
void forget_desk_client (int desk, ASWindowData * wd);
void add_client (ASWindowData * wd);
void refresh_client (INT32 old_desk, ASWindowData * wd);
void change_desk_stacking (int desk, unsigned int clients_num,
													 send_data_type * clients);
void set_desktop_pixmap (int desk, Pixmap pmap);
void switch_deskviewport (int new_desk, int new_vx, int new_vy);
void place_selection ();
void request_background_image (ASPagerDesk * d);
Bool render_desk (ASPagerDesk * d, Bool force);
void set_client_look (ASWindowData * wd, Bool redraw);
void redecorate_pager_desks ();
void rearrange_pager_desks (Bool dont_resize_main);
void on_client_moveresize (ASWindowData * wd);
void on_pager_window_moveresize (void *client, Window w, int x, int y,
																 unsigned int width, unsigned int height);
void on_pager_pressure_changed (ASEvent * event);
void release_pressure ();
void on_scroll_viewport (ASEvent * event);
void update_pager_shape ();
void start_moveresize_client (ASWindowData * wd, Bool move, ASEvent * event);
void DeadPipe (int);
void CheckConfigSanity ();
void retrieve_pager_astbar_props ();

#endif /* PAGER_INTERNAL_H_HEADER_INCLUDED */
