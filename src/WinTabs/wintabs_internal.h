#ifndef WINTABS_INTERNAL_H_HEADER_INCLUDED
#define WINTABS_INTERNAL_H_HEADER_INCLUDED

/* Keep this header self-contained so new compilation units can share WinTabs.c's
 * internal state without pulling in WinTabs.c itself. */

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

/* #include <X11/keysym.h> */
#include "../../libAfterImage/afterimage.h"

#include "../../libAfterStep/afterstep.h"
#include "../../libAfterStep/screen.h"
#include "../../libAfterStep/module.h"
#include "../../libAfterStep/mystyle.h"
#include "../../libAfterStep/mystyle_property.h"
#include "../../libAfterStep/parser.h"
#include "../../libAfterStep/clientprops.h"
#include "../../libAfterStep/wmprops.h"
#include "../../libAfterStep/decor.h"
#include "../../libAfterStep/aswindata.h"
#include "../../libAfterStep/balloon.h"
#include "../../libAfterStep/event.h"

#include "../../libAfterConf/afterconf.h"

/**********************************************************************/
/*  AfterStep specific global variables :                             */
/**********************************************************************/
/**********************************************************************/
/*  Gadget local variables :                                         */
/**********************************************************************/
struct ASWinTab;

typedef struct ASWinTabGroup
{
	Bool 	pattern_is_tail;
	int 	pattern_length;
	char*	pattern;
	INT32 	pattern_encoding ;
	Bool 	selected;

	Window  selected_client;
	int		seqno;

}ASWinTabGroup;

typedef struct ASWinTab
{
	char 			*name ;
	INT32 			 name_encoding ;
	Window 			 client ;

	ASTBarData 		*bar ;
	ASCanvas 		*client_canvas ;

	ASCanvas 		*frame_canvas ;

	Bool closed ;
	XRectangle 	swallow_location ;
	ASFlagType wm_protocols ;

	XSizeHints	hints ;

	time_t      last_selected ;

	ASWinTabGroup  *group;
	Bool 			group_owner;
	int 			group_seqno;

	int calculated_width;
}ASWinTab;

typedef struct {

#define ASWT_StateMapped	(0x01<<0)
#define ASWT_StateFocused	(0x01<<1)
#define ASWT_AllDesks		(0x01<<2)
#define ASWT_Transparent	 (0x01<<3)
#define ASWT_ShutDownInProgress	(0x01<<4)
#define ASWT_SkipTransients		(0x01<<5)
#define ASWT_StateShaded		(0x01<<6)
#define ASWT_StateSticky		(0x01<<7)
#define ASWT_Desktops 			(0x01<<8) /* requested at command line */
#define ASWT_WantTransparent 	(0x01<<16) /* requested at command line */


	ASFlagType flags ;

    Window main_window, tabs_window ;
	ASCanvas *main_canvas ;
	ASCanvas *tabs_canvas ;

	wild_reg_exp *pattern_wrexp ;
	wild_reg_exp *exclude_pattern_wrexp ;

	ASVector *tabs ;

#define BANNER_BUTTONS_IDX  	0
#define BANNER_LABEL_IDX  		1
	ASWinTab  banner ;

	int rows ;
	int row_height ;

    Window selected_client;

	int win_width, win_height ;

	ASTBarProps *tbar_props ;

	MyButton close_button ;
	MyButton menu_button ;
	MyButton unswallow_button ;

	CARD32      my_desktop ;

	ASHashTable *unswallowed_apps ;

	unsigned long 		border_color;
}ASWinTabsState ;

extern ASWinTabsState WinTabsState;

#define C_CloseButton 		C_TButton0
#define C_UnswallowButton 	C_TButton1
#define C_MenuButton 		C_TButton2

#define WINTABS_SWITCH_KEYCODE	  49	/* XKeysymToKeycode (dpy, XK_grave) */
#define WINTABS_SWITCH_MOD        Mod1Mask
#define WINTABS_TAB_EVENT_MASK    (ButtonReleaseMask | ButtonPressMask | \
	                               LeaveWindowMask   | EnterWindowMask | \
                                   StructureNotifyMask|PointerMotionMask| \
								   	KeyPressMask )

#define WINTABS_MESSAGE_MASK      (M_END_WINDOWLIST |M_DESTROY_WINDOW |M_SWALLOW_WINDOW| \
					   			   WINDOW_CONFIG_MASK|WINDOW_NAME_MASK|M_SHUTDOWN)

/**********************************************************************/
/* Our configuration options :                                        */
/**********************************************************************/
extern WinTabsConfig *Config;

extern char *pattern_override;
extern char *exclude_pattern_override;
extern char *title_override, *icon_title_override;
extern char *border_color_override;

void retrieve_wintabs_astbar_props();
void CheckConfigSanity(const char *pattern_override, const char *exclude_pattern_override,
					   const char *title_override, const char *icon_title_override);
void SetWinTabsLook();
void GetBaseOptions (const char *filename);
void GetOptions (const char *filename);

static inline int
find_tab_for_client (Window client)
{
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
    int i = PVECTOR_USED(WinTabsState.tabs) ;

	if (client)
	    while( --i >= 0 ) if (tabs[i].client == client) return i;
	return -1;
}

static inline int
find_tab_pressed ()
{
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
    int i = PVECTOR_USED(WinTabsState.tabs) ;

    while( --i >= 0 ) if (IsASTBarPressed(tabs[i].bar)) return i;
	return -1;
}

static inline int
find_group_owner (ASWinTabGroup *group)
{
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
    int i = PVECTOR_USED(WinTabsState.tabs) ;

	if (group)
	    while( --i >= 0 ) if (tabs[i].group == group && tabs[i].group_owner) return i;
	return -1;
}

static inline int
find_tab_for_group (ASWinTabGroup *group, int after_index)
{
    ASWinTab *tabs = PVECTOR_HEAD(ASWinTab,WinTabsState.tabs);
	int i = after_index;
    int max_i = PVECTOR_USED(WinTabsState.tabs) ;

	if (group)
	    while (++i < max_i) if (tabs[i].group == group && !tabs[i].group_owner) return i;
	return -1;
}

/* above function may also return : */
#define BANNER_TAB_INDEX -1
#define INVALID_TAB_INDEX -2

void HandleEvents();
void process_message (send_data_type type, send_data_type *body);
void DispatchEvent (ASEvent * Event);
void send_swallowed_configure_notify(ASWinTab *aswt);

Window make_wintabs_window();
Window make_tabs_window( Window parent );
Window make_frame_window( Window parent );
void do_swallow_window( ASWindowData *wd );
void check_swallow_window( ASWindowData *wd );
void rearrange_tabs( Bool dont_resize_window );
void render_tabs( Bool canvas_resized );
void on_destroy_notify(Window w);
void on_unmap_notify(Window w);
void select_tab( int tab );
void press_tab( int tab );
void set_tab_look( ASWinTab *aswt, Bool no_bevel );
void set_tab_title( ASWinTab *aswt );

void show_hint( Bool redraw );
void show_banner_buttons();

int find_tab_by_position( int root_x, int root_y );
void send_swallow_command();
void close_current_tab();
Bool unswallow_current_tab();
Bool unswallow_tab(int t);
void  update_focus();
Bool handle_tab_name_change( Window client );
void update_tabs_desktop();
void update_tabs_state();
void register_unswallowed_client( Window client );
Bool check_unswallowed_client( Window client );
void delete_tab( int index );
void set_frame_background( ASWinTab *aswt );

ASWinTab *add_tab( Window client, const char *name, INT32 encoding );

ASWinTabGroup *check_belong_to_group (ASWinTabGroup *group, const char *name, INT32 name_encoding);
int remove_from_group (ASWinTabGroup *group, int t);
void add_to_group (ASWinTabGroup *group, int t);
void check_create_new_group();
void check_tab_grouping (int t);
void fix_grouping_order();

void OnDisconnect( int nonsense );
void DeadPipe(int);

#endif
