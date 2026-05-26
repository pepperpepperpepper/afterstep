#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include <unistd.h>

#include "dirtree.h"

#include "../../libAfterStep/session.h"
#include "../../libAfterStep/mystyle_property.h"
#include "../../libAfterStep/wmprops.h"
#include "../../libAfterStep/desktop_category.h"
#include "../../libAfterStep/kde.h"

#include "../../libAfterConf/afterconf.h"

#include "configure_internal.h"

/* parsing handling functions for different data types : */

void SetInts (char *text, FILE * fd, char **arg1, int *arg2);
void SetInts2 (char *text, FILE * fd, char **arg1, int *arg2);
void SetFlag (char *text, FILE * fd, char **arg, int *another);
void SetLookFlag (char *text, FILE * fd, char **arg, int *another);
void SetFlag2 (char *text, FILE * fd, char **arg, int *var);
void SetLookFlag (char *text, FILE * fd, char **arg, int *junk);
void SetBox (char *text, FILE * fd, char **arg, int *junk);
void SetCursor (char *text, FILE * fd, char **arg, int *junk);
void SetCustomCursor (char *text, FILE * fd, char **arg, int *junk);
void SetButtonList (char *text, FILE * fd, char **arg1, int *arg2);
void SetTitleText (char *tline, FILE * fd, char **junk, int *junk2);
void SetTitleButton (char *tline, FILE * fd, char **junk, int *junk2);
void SetFramePart (char *text, FILE * fd, char **frame, int *id);
void SetModifier (char *text, FILE * fd, char **mod, int *junk2);
void SetTButtonOrder (char *text, FILE * fd, char **mod, int *junk2);

void assign_string (char *text, FILE * fd, char **arg, int *idx);
void assign_path (char *text, FILE * fd, char **arg, int *idx);
void assign_themable_path (char *text, FILE * fd, char **arg, int *idx);
void assign_pixmap (char *text, FILE * fd, char **arg, int *idx);
void assign_geometry (char *text, FILE * fd, char **geom, int *junk);
void obsolete (char *text, FILE * fd, char **arg, int *);

void deskback_parse (char *text, FILE * fd, char **junk, int *junk2);

/* main parsing function  : */
void match_string (struct config *table, char *text, char *error_msg,
									 FILE * fd);

/* menu loading code : */
int MeltStartMenu (char *buf);

/* scratch variable : */
static int dummy;

ASFeel TmpFeel;
MyLook TmpLook;

/*
 * Order is important here! if one keyword is the same as the first part of
 * another keyword, the shorter one must come first!
 */
struct config main_config[] = {
	/* feel options */
	{"StubbornIcons", SetFlag2, (char **)StubbornIcons, (int *)0},
	{"StubbornPlacement", SetFlag2, (char **)StubbornPlacement, (int *)0},
	{"StubbornIconPlacement", SetFlag2, (char **)StubbornIconPlacement,
	 (int *)0},
	{"StickyIcons", SetFlag2, (char **)StickyIcons, (int *)0},
	{"IconTitle", SetFlag2, (char **)IconTitle, (int *)0},
	{"KeepIconWindows", SetFlag2, (char **)KeepIconWindows, (int *)0},
	{"NoPPosition", SetFlag2, (char **)NoPPosition, (int *)0},
	{"CirculateSkipIcons", SetFlag2, (char **)CirculateSkipIcons, (int *)0},
	{"EdgeScroll", SetInts, (char **)&TmpFeel.EdgeScrollX,
	 &TmpFeel.EdgeScrollY},
	{"RandomPlacement", SetFlag2, (char **)FEEL_DEPRECATED_RandomPlacement,
	 (int *)&(TmpFeel.deprecated_flags)},
	{"SmartPlacement", SetFlag2, (char **)FEEL_DEPRECATED_SmartPlacement,
	 (int *)&(TmpFeel.deprecated_flags)},
	{"DontMoveOff", obsolete, (char **)NULL, (int *)0},
	{"DecorateTransients", SetFlag2, (char **)DecorateTransients, (int *)0},
	{"CenterOnCirculate", SetFlag2, (char **)CenterOnCirculate, (int *)0},
	{"AutoRaise", SetInts, (char **)&TmpFeel.AutoRaiseDelay, &dummy},
	{"ClickTime", SetInts, (char **)&TmpFeel.ClickTime, &dummy},
	{"OpaqueMove", SetInts, (char **)&TmpFeel.OpaqueMove, &dummy},
	{"OpaqueResize", SetInts, (char **)&TmpFeel.OpaqueResize, &dummy},
	{"XorValue", obsolete, (char **)NULL, &dummy},
	{"Mouse", ParseMouseEntry, (char **)1, (int *)0},
	{"Popup", ParsePopupEntry, (char **)1, (int *)0},
	{"Function", ParseFunctionEntry, (char **)1, (int *)0},
	{"Key", ParseKeyEntry, (char **)1, (int *)0},
	{"ClickToFocus", SetFlag, (char **)ClickToFocus, (int *)EatFocusClick},
	{"EatFocusClick", SetFlag2, (char **)EatFocusClick, (int *)0},
	{"ClickToRaise", SetButtonList, (char **)NULL, (int *)0},
	{"MenusHigh", obsolete, (char **)NULL, (int *)0},
	{"SloppyFocus", SetFlag2, (char **)SloppyFocus, (int *)0},
	{"PagingDefault", obsolete, (char **)NULL, NULL},
	{"EdgeResistance", SetInts, (char **)&TmpFeel.EdgeResistanceScroll,
	 &TmpFeel.EdgeResistanceMove},
	{"EdgeResistanceToDragging", SetInts,
	 (char **)&TmpFeel.EdgeResistanceDragScroll, NULL},
	{"BackingStore", SetFlag2, (char **)BackingStore, (int *)0},
	{"AppsBackingStore", SetFlag2, (char **)AppsBackingStore, (int *)0},
	{"SaveUnders", SetFlag2, (char **)SaveUnders, (int *)0},
	{"Xzap", SetInts, (char **)&TmpFeel.Xzap, (int *)&dummy},
	{"Yzap", SetInts, (char **)&TmpFeel.Yzap, (int *)&dummy},
	{"AutoReverse", SetInts, (char **)&TmpFeel.AutoReverse, (int *)&dummy},
	{"AutoTabThroughDesks", SetFlag2, (char **)AutoTabThroughDesks, NULL},
	{"MWMFunctionHints", obsolete, (char **)0, NULL},
	{"MWMDecorHints", obsolete, (char **)0, NULL},
	{"MWMHintOverride", obsolete, (char **)0, NULL},
	{"FollowTitleChanges", SetFlag2, (char **)FollowTitleChanges, (int *)0},
	{"PersistentMenus", SetFlag2, (char **)PersistentMenus, (int *)0},
	{"NoSnapKey", SetModifier, (char **)&(TmpFeel.no_snaping_mod), (int *)0},
	{"ScreenEdgeAttraction", SetInts, (char **)&TmpFeel.EdgeAttractionScreen,
	 &dummy},
	{"WindowEdgeAttraction", SetInts, (char **)&TmpFeel.EdgeAttractionWindow,
	 &dummy},
	{"DontRestoreFocus", SetFlag2, (char **)DontRestoreFocus, NULL},
	{"WindowBox", windowbox_parse, (char **)NULL, (int *)NULL},
	{"DefaultWindowBox", assign_string,
	 (char **)&(TmpFeel.default_window_box_name), (int *)0},
	{"RecentSubmenuItems", SetInts, (char **)&TmpFeel.recent_submenu_items,
	 (int *)&dummy},
	{"WinListSortOrder", SetInts, (char **)&TmpFeel.winlist_sort_order,
	 (int *)&dummy},
	{"WinListHideIcons", SetFlag2, (char **)WinListHideIcons, NULL},
	{"SuppressIcons", SetFlag2, (char **)SuppressIcons, NULL},
	{"WarpPointer", SetFlag2, (char **)WarpPointer, NULL},

	/* look options */
	/* obsolete stuff */
	{"Font", assign_string, &Stdfont, (int *)0},
	{"WindowFont", assign_string, &Windowfont, (int *)0},
	{"IconFont", assign_string, &Iconfont, (int *)0},
	{"MTitleForeColor", assign_string, &MenuForeColor[MENU_BACK_TITLE],
	 (int *)0},
	{"MTitleBackColor", assign_string, &MenuBackColor[MENU_BACK_TITLE],
	 (int *)0},
	{"MenuForeColor", assign_string, &MenuForeColor[MENU_BACK_ITEM],
	 (int *)0},
	{"MenuBackColor", assign_string, &MenuBackColor[MENU_BACK_ITEM],
	 (int *)0},
	{"MenuHiForeColor", assign_string, &MenuForeColor[MENU_BACK_HILITE],
	 (int *)0},
	{"MenuHiBackColor", assign_string, &MenuBackColor[MENU_BACK_HILITE],
	 (int *)0},
	{"MenuStippleColor", assign_string, &MenuForeColor[MENU_BACK_STIPPLE],
	 (int *)0},
	{"StdForeColor", assign_string, &WindowForeColor[BACK_UNFOCUSED],
	 (int *)0},
	{"StdBackColor", assign_string, &WindowBackColor[BACK_UNFOCUSED],
	 (int *)0},
	{"StickyForeColor", assign_string, &WindowForeColor[BACK_STICKY],
	 (int *)0},
	{"StickyBackColor", assign_string, &WindowBackColor[BACK_STICKY],
	 (int *)0},
	{"HiForeColor", assign_string, &WindowForeColor[BACK_FOCUSED], (int *)0},
	{"HiBackColor", assign_string, &WindowBackColor[BACK_FOCUSED], (int *)0},
	{"TextureTypes", assign_string, &TexTypes, (int *)0},
	{"TextureMaxColors", obsolete, NULL, (int *)0},
	{"TitleTextureColor", assign_string, &WindowGradient[BACK_FOCUSED], (int *)0},	/* title */
	{"UTitleTextureColor", assign_string, &WindowGradient[BACK_UNFOCUSED], (int *)0},	/* unfoc tit */
	{"STitleTextureColor", assign_string, &WindowGradient[BACK_STICKY], (int *)0},	/* stic tit */
	{"MTitleTextureColor", assign_string, &MenuGradient[MENU_BACK_TITLE], (int *)0},	/* menu title */
	{"MenuTextureColor", assign_string, &MenuGradient[MENU_BACK_ITEM], (int *)0},	/* menu items */
	{"MenuHiTextureColor", assign_string, &MenuGradient[MENU_BACK_HILITE], (int *)0},	/* sel items */
	{"MenuPixmap", assign_string, &MenuPixmap[MENU_BACK_ITEM], (int *)0},	/* menu entry */
	{"MenuHiPixmap", assign_string, &MenuPixmap[MENU_BACK_HILITE], (int *)0},	/* hil m entr */
	{"MTitlePixmap", assign_string, &MenuPixmap[MENU_BACK_TITLE], (int *)0},	/* menu title */
	{"TitlePixmap", assign_string, &WindowPixmap[BACK_FOCUSED], (int *)0},	/* foc tit */
	{"UTitlePixmap", assign_string, &WindowPixmap[BACK_UNFOCUSED], (int *)0},	/* unfoc tit */
	{"STitlePixmap", assign_string, &WindowPixmap[BACK_STICKY], (int *)0},	/* stick tit */
	{"MenuPinOff", obsolete, (char **)NULL, (int *)0},
	{"TexturedHandle", obsolete, (char **)NULL, (int *)0},
	{"TextGradientColor", obsolete, (char **)NULL, (int *)0},	/* title text */
	{"GradientText", obsolete, (char **)NULL, (int *)0},

	{"ButtonTextureType", SetInts, (char **)&IconTexType, (int *)&dummy},
	{"ButtonBgColor", assign_string, &IconBgColor, (int *)0},
	{"ButtonTextureColor", assign_string, &IconTexColor, (int *)0},
	{"ButtonMaxColors", obsolete, (char **)NULL, NULL},
	{"ButtonPixmap", assign_string, &IconPixmapFile, (int *)0},
	{"ButtonNoBorder", SetLookFlag, (char **)IconNoBorder, NULL},
	{"FrameNorth", SetFramePart, NULL, (int *)FR_N},
	{"FrameSouth", SetFramePart, NULL, (int *)FR_S},
	{"FrameEast", SetFramePart, NULL, (int *)FR_E},
	{"FrameWest", SetFramePart, NULL, (int *)FR_W},
	{"FrameNW", SetFramePart, NULL, (int *)FR_NW},
	{"FrameNE", SetFramePart, NULL, (int *)FR_NE},
	{"FrameSW", SetFramePart, NULL, (int *)FR_SW},
	{"FrameSE", SetFramePart, NULL, (int *)FR_SE},
	{"DecorateFrames", SetLookFlag, (char **)DecorateFrames, NULL},
	{"TitleButtonBalloonBorderWidth", obsolete, NULL, NULL},
	{"TitleButtonBalloonBorderColor", obsolete, NULL, NULL},
	{"TitleTextMode", SetTitleText, (char **)1, (int *)0},

	/* new stuff : */
	{"IconBox", SetBox, (char **)0, (int *)0},
	{"MyStyle", mystyle_parse, (char **)"afterstep", (int *)&MyStyleList},
	{"MyBackground", myback_parse, (char **)"asetroot", NULL},	/* pretending to be asteroot here */
	{"DeskBack", deskback_parse, NULL, NULL},
	{"*asetrootDeskBack", deskback_parse, NULL, NULL},	/* pretending to be asteroot here */
	{"MyFrame", myframe_parse, (char **)"afterstep", (int *)&MyFrameList},
	{"DefaultFrame", assign_string, (char **)&TmpLook.DefaultFrameName,
	 (int *)0},
	{"DontDrawBackground", SetLookFlag, (char **)DontDrawBackground, NULL},
	{"CursorFore", assign_string, &TmpLook.CursorFore, (int *)0},	/* foreground color to be used for coloring pointer's cursor */
	{"CursorBack", assign_string, &TmpLook.CursorBack, (int *)0},	/* background color to be used for coloring pointer's cursor */
	/* this two a really from the feel */
	{"CustomCursor", SetCustomCursor, (char **)0, (int *)0},
	{"Cursor", SetCursor, (char **)0, (int *)0},
	/***********************************/
	{"IconsGrowVertically", SetLookFlag, (char **)IconsGrowVertically,
	 (int *)0},
	{"MenuPinOn", assign_string, &MenuPinOn, (int *)0},	/* menu pin */
	{"MArrowPixmap", assign_pixmap, (char **)&TmpLook.MenuArrow, (int *)0},	/* menu arrow */
	{"TitlebarNoPush", SetLookFlag, (char **)TitlebarNoPush, NULL},
	{"TextureMenuItemsIndividually", SetLookFlag, (char **)TxtrMenuItmInd,
	 NULL},
	{"MenuMiniPixmaps", SetLookFlag, (char **)MenuMiniPixmaps, NULL},
	{"MenuShowUnavailable", SetLookFlag, (char **)MenuShowUnavailable, NULL},
	{"TitleTextAlign", SetInts, (char **)&TmpLook.TitleTextAlign, &dummy},
	{"TitleButtonSpacingLeft", SetInts,
	 (char **)&TmpLook.TitleButtonSpacing[0], &dummy},
	{"TitleButtonSpacingRight", SetInts,
	 (char **)&TmpLook.TitleButtonSpacing[1], &dummy},
	{"TitleButtonSpacing", SetInts2, (char **)&TmpLook.TitleButtonSpacing[0],
	 &TmpLook.TitleButtonSpacing[1]},
	{"TitleButtonXOffsetLeft", SetInts,
	 (char **)&TmpLook.TitleButtonXOffset[0], &dummy},
	{"TitleButtonXOffsetRight", SetInts,
	 (char **)&TmpLook.TitleButtonXOffset[1], &dummy},
	{"TitleButtonXOffset", SetInts2, (char **)&TmpLook.TitleButtonXOffset[0],
	 &TmpLook.TitleButtonXOffset[1]},
	{"TitleButtonYOffsetLeft", SetInts,
	 (char **)&TmpLook.TitleButtonYOffset[0], &dummy},
	{"TitleButtonYOffsetRight", SetInts,
	 (char **)&TmpLook.TitleButtonYOffset[1], &dummy},
	{"TitleButtonYOffset", SetInts2, (char **)&TmpLook.TitleButtonYOffset[0],
	 &TmpLook.TitleButtonYOffset[1]},
	{"TitleButtonStyle", SetInts, (char **)&TmpLook.TitleButtonStyle,
	 (int *)&dummy},
	{"TitleButtonOrder", SetTButtonOrder, NULL, NULL},
	{"ResizeMoveGeometry", assign_geometry,
	 (char **)&TmpLook.resize_move_geometry, (int *)0},
	{"StartMenuSortMode", SetInts, (char **)&TmpLook.StartMenuSortMode,
	 (int *)&dummy},
	{"DrawMenuBorders", SetInts, (char **)&TmpLook.DrawMenuBorders,
	 (int *)&dummy},
	{"ButtonSize", SetInts, (char **)&TmpLook.ButtonWidth,
	 (int *)&TmpLook.ButtonHeight},
	{"MinipixmapSize", SetInts, (char **)&TmpLook.minipixmap_width,
	 (int *)&TmpLook.minipixmap_height},
	{"SeparateButtonTitle", SetLookFlag, (char **)SeparateButtonTitle, NULL},
	{"RubberBand", SetInts, (char **)&TmpLook.RubberBand, &dummy},
	{"DefaultStyle", assign_string, (char **)&MSWindowName[BACK_DEFAULT],
	 (int *)0},
	{"FWindowStyle", assign_string, (char **)&MSWindowName[BACK_FOCUSED],
	 (int *)0},
	{"UWindowStyle", assign_string, (char **)&MSWindowName[BACK_UNFOCUSED],
	 (int *)0},
	{"SWindowStyle", assign_string, (char **)&MSWindowName[BACK_STICKY],
	 (int *)0},
	{"MenuItemStyle", assign_string, (char **)&MSMenuName[MENU_BACK_ITEM],
	 (int *)0},
	{"MenuTitleStyle", assign_string, (char **)&MSMenuName[MENU_BACK_TITLE],
	 (int *)0},
	{"MenuHiliteStyle", assign_string,
	 (char **)&MSMenuName[MENU_BACK_HILITE], (int *)0},
	{"MenuStippleStyle", assign_string,
	 (char **)&MSMenuName[MENU_BACK_STIPPLE], (int *)0},
	{"MenuSubItemStyle", assign_string,
	 (char **)&MSMenuName[MENU_BACK_SUBITEM], (int *)0},
	{"MenuHiTitleStyle", assign_string,
	 (char **)&MSMenuName[MENU_BACK_HITITLE], (int *)0},
	{"MenuItemCompositionMethod", SetInts, (char **)&TmpLook.menu_icm,
	 &dummy},
	{"MenuHiliteCompositionMethod", SetInts, (char **)&TmpLook.menu_hcm,
	 &dummy},
	{"MenuStippleCompositionMethod", SetInts, (char **)&TmpLook.menu_scm,
	 &dummy},
	{"MenuBalloonBorderHilite", bevel_parse, (char **)"afterstep",
	 (int *)&(MenuBalloonConfig.BorderHilite)},
	{"MenuBalloonXOffset", SetInts, (char **)&(MenuBalloonConfig.XOffset),
	 NULL},
	{"MenuBalloonYOffset", SetInts, (char **)&(MenuBalloonConfig.YOffset),
	 NULL},
	{"MenuBalloonDelay", SetInts, (char **)&(MenuBalloonConfig.Delay), NULL},
	{"MenuBalloonCloseDelay", SetInts,
	 (char **)&(MenuBalloonConfig.CloseDelay), NULL},
	{"MenuBalloonStyle", assign_string, &(MenuBalloonConfig.Style), NULL},
	{"MenuBalloonTextPaddingX", SetInts,
	 (char **)&(MenuBalloonConfig.TextPaddingX), NULL},
	{"MenuBalloonTextPaddingY", SetInts,
	 (char **)&(MenuBalloonConfig.TextPaddingY), NULL},
	{"MenuBalloons", SetFlag2, (char **)BALLOON_USED,
	 (int *)&(MenuBalloonConfig.set_flags)},
	{"ShadeAnimationSteps", SetInts, (char **)&TmpFeel.ShadeAnimationSteps,
	 (int *)&dummy},
	{"TitleButtonBalloonBorderHilite", bevel_parse, (char **)"afterstep",
	 (int *)&(BalloonConfig.BorderHilite)},
	{"TitleButtonBalloonXOffset", SetInts, (char **)&(BalloonConfig.XOffset),
	 NULL},
	{"TitleButtonBalloonYOffset", SetInts, (char **)&(BalloonConfig.YOffset),
	 NULL},
	{"TitleButtonBalloonDelay", SetInts, (char **)&(BalloonConfig.Delay),
	 NULL},
	{"TitleButtonBalloonCloseDelay", SetInts,
	 (char **)&(BalloonConfig.CloseDelay), NULL},
	{"TitleButtonBalloonStyle", assign_string, &(BalloonConfig.Style), NULL},
	{"TitleButtonBalloonTextPaddingX", SetInts,
	 (char **)&(BalloonConfig.TextPaddingX), NULL},
	{"TitleButtonBalloonTextPaddingY", SetInts,
	 (char **)&(BalloonConfig.TextPaddingY), NULL},
	{"TitleButtonBalloons", SetFlag2, (char **)BALLOON_USED,
	 (int *)&(BalloonConfig.set_flags)},
	{"TitleButton", SetTitleButton, (char **)1, (int *)0},
	{"KillBackgroundThreshold", SetInts,
	 (char **)&(TmpLook.KillBackgroundThreshold), NULL},
	{"DontAnimateBackground", SetFlag2, (char **)DontAnimateBackground,
	 NULL},
	{"CoverAnimationSteps", SetInts,
	 (char **)&(TmpFeel.desk_cover_animation_steps), NULL},
	{"CoverAnimationType", SetInts,
	 (char **)&(TmpFeel.desk_cover_animation_type), NULL},
	{"AnimateDeskChange", SetFlag2, (char **)AnimateDeskChange, NULL},
	{"DontCoverDesktop", SetFlag2, (char **)DontCoverDesktop, NULL},
	{"ButtonIconSpacing", SetInts, (char **)&TmpLook.ButtonIconSpacing,
	 &dummy},
	{"ButtonBevel", bevel_parse, (char **)"afterstep",
	 (int *)&(TmpLook.ButtonBevel)},
	{"ButtonAlign", align_parse, (char **)"afterstep",
	 (int *)&(TmpLook.ButtonAlign)},
	{"", 0, (char **)0, (int *)0}
};

#define PARSE_BUFFER_SIZE 	MAXLINELENGTH
char *orig_tline = NULL;

/* the following values must not be reset other then by main
   config reading routine
 */
int curr_conf_line = -1;
char *curr_conf_file = NULL;

void error_point ()
{
	fprintf (stderr, "AfterStep");
	if (curr_conf_file)
		fprintf (stderr, "(%s:%d)", curr_conf_file, curr_conf_line);
	fprintf (stderr, ":");
}

void tline_error (const char *err_text)
{
	error_point ();
	fprintf (stderr, "%s in [%s]\n", err_text, orig_tline);
}

/***************************************************************
 **************************************************************/
void obsolete (char *text, FILE * fd, char **arg, int *i)
{
	tline_error ("This option is obsolete. ");
}


int ParseConfigFile (const char *file, char **tline)
{
	FILE *fp = NULL;
	register char *ptr;

	/* memory management for parsing buffer */
	if (file == NULL)
		return -1;

	/* this should not happen, but still checking */
	if ((fp = fopen (file, "r")) == (FILE *) NULL) {
		show_error
				("can't open config file [%s] - skipping it for now.\nMost likely you have incorrect permissions on the AfterStep configuration dir.",
				 file);
		return -1;
	}

	if (*tline == NULL)
		*tline = safemalloc (MAXLINELENGTH + 1);

	curr_conf_file = (char *)file;
	curr_conf_line = 0;
	while (fgets (*tline, MAXLINELENGTH, fp)) {
		curr_conf_line++;
		/* prventing buffer overflow */
		*((*tline) + MAXLINELENGTH) = '\0';
		/* remove comments from the line */
		ptr = stripcomments (*tline);
		/* parsing the line */
		orig_tline = ptr;
		if (*ptr != '\0' && *ptr != '#')
			match_string (main_config, ptr, "error in config:", fp);
	}
	curr_conf_file = NULL;
	fclose (fp);
	return 1;
}

/*****************************************************************************
 *****************************************************************************
 * This routine is responsible for reading and parsing the config file
 ****************************************************************************
 ****************************************************************************/
/* MakeMenus - for those who can't remember LoadASConfig's real name        */

void assign_string (char *text, FILE * fd, char **arg, int *junk)
{
	if (*arg)
		free (*arg);
	*arg = stripcpy2 (text, 0);
}

void assign_path (char *text, FILE * fd, char **arg, int *junk)
{
	*arg = stripcpy (text);
	replaceEnvVar (arg);
}

void assign_geometry (char *text, FILE * fd, char **arg, int *junk)
{
	ASGeometry *geom = (ASGeometry *) arg;

	geom->x = geom->y = 0;
	geom->width = geom->height = 1;
	geom->flags = 0;
	parse_geometry (text, &(geom->x), &(geom->y), &(geom->width),
									&(geom->height), &(geom->flags));
}

/*****************************************************************************
 * Loads a pixmap to the assigned location
 ****************************************************************************/
void assign_pixmap (char *text, FILE * fd, char **arg, int *junk)
{
	char *fname = NULL;
	if (parse_filename (text, &fname) != text) {
		MyIcon **picon = (MyIcon **) arg;
		*picon = safecalloc (1, sizeof (icon_t));
		GetIconFromFile (fname, *picon, -1);
		free (fname);
	}
}

/****************************************************************************
 *  Read TitleText Controls
 ****************************************************************************/

void SetTitleText (char *tline, FILE * fd, char **junk, int *junk2)
{
	int ttype, y;

	sscanf (tline, "%d %d", &ttype, &y);
	TitleTextType = ttype;
	TitleTextY = y;
}

/****************************************************************************
 *
 *  Read Titlebar pixmap button
 *
 ****************************************************************************/

void SetTitleButton (char *tline, FILE * fd, char **junk, int *junk2)
{
	int num;
	char *files[2] = { NULL, NULL };
	int offset = 0;
	int n;

	if ((n = sscanf (tline, "%d", &num)) <= 0) {
		show_error
				("wrong number of parameters given with TitleButton in [%s]",
				 tline);
		return;
	}
	if (num < 0 || num >= TITLE_BUTTONS) {
		show_error ("invalid Titlebar button number: %d", num);
		return;
	}

	/* going the hard way to prevent buffer overruns */
	while (isspace (tline[offset]))
		offset++;
	while (isdigit (tline[offset]))
		offset++;
	while (isspace (tline[offset]))
		offset++;

	tline = parse_filename (&(tline[offset]), &(files[0]));
	offset = 0;
	while (isspace (tline[offset]))
		++offset;
	if (tline[offset] != '\0')
		parse_filename (&(tline[offset]), &(files[1]));

	if (!load_button (&(Scr.Look.buttons[num]), files, Scr.image_manager))
		show_error
				("Failed to load image files specified for TitleButton %d [%s]",
				 num, tline);
	if (files[0])
		free (files[0]);
	if (files[1])
		free (files[1]);
}

/*****************************************************************************
 *
 * Changes a cursor def.
 *
 ****************************************************************************/

void SetCursor (char *text, FILE * fd, char **arg, int *junk)
{
	int num, cursor_num, cursor_style;

	num = sscanf (text, "%d %d", &cursor_num, &cursor_style);
	if ((num != 2) || (cursor_num >= MAX_CURSORS) || (cursor_num < 0))
		show_warning ("bad Cursor number in [%s]", text);
	else {
		Cursor new_c = XCreateFontCursor (dpy, cursor_style);
		if (new_c) {
			if (Scr.Feel.cursors[cursor_num]
					&& Scr.Feel.cursors[cursor_num] !=
					Scr.standard_cursors[cursor_num])
				XFreeCursor (dpy, Scr.Feel.cursors[cursor_num]);
			Scr.Feel.cursors[cursor_num] = new_c;
			LOCAL_DEBUG_OUT ("New X Font cursor %lX created for cursor_num %d",
											 new_c, cursor_num);
		}
	}
}

void SetCustomCursor (char *text, FILE * fd, char **arg, int *junk)
{
	int num, cursor_num;
	char f_cursor[1024], f_mask[1024];
	Pixmap cursor = None, mask = None;
	unsigned int width, height;
	int x, y;
	XColor fore, back;
	char *path;
	Cursor new_c;

	num = sscanf (text, "%d %s %s", &cursor_num, f_cursor, f_mask);
	if ((num != 3) || (cursor_num >= MAX_CURSORS) || (cursor_num < 0)) {
		show_warning ("bad Cursor number in [%s]", text);
		return;
	}

	path = find_file (f_mask, Environment->cursor_path, R_OK);
	if (path) {
		XReadBitmapFile (dpy, Scr.Root, path, &width, &height, &mask, &x, &y);
		free (path);
	} else {
		show_warning ("Cursor mask requested in [%s] could not be found",
									text);
		return;
	}

	path = find_file (f_cursor, Environment->cursor_path, R_OK);
	if (path) {
		XReadBitmapFile (dpy, Scr.Root, path, &width, &height, &cursor, &x,
										 &y);
		free (path);
	} else {
		show_warning ("Cursor bitmap requested in [%s] could not be found",
									text);
		return;
	}

	fore.pixel = Scr.asv->black_pixel;
	back.pixel = Scr.asv->white_pixel;
	XQueryColor (dpy, Scr.asv->colormap, &fore);
	XQueryColor (dpy, Scr.asv->colormap, &back);

	if (cursor == None || mask == None) {
		show_warning
				("Cursor mask or bitmap requested in [%s] could not be loaded",
				 text);
		return;
	}

	new_c = XCreatePixmapCursor (dpy, cursor, mask, &fore, &back, x, y);
	if (new_c) {
		if (Scr.Feel.cursors[cursor_num]
				&& Scr.Feel.cursors[cursor_num] !=
				Scr.standard_cursors[cursor_num])
			XFreeCursor (dpy, Scr.Feel.cursors[cursor_num]);
		Scr.Feel.cursors[cursor_num] = new_c;
		LOCAL_DEBUG_OUT ("New Custom X cursor created for cursor_num %d",
										 cursor_num);
	}
	XFreePixmap (dpy, mask);
	XFreePixmap (dpy, cursor);
	ASSync (False);
	LOCAL_DEBUG_OUT ("mask %lX and cursor %lX freed", mask, cursor);
}

/*****************************************************************************
 *
 * Sets a boolean flag to true
 *
 ****************************************************************************/

void SetFlag (char *text, FILE * fd, char **arg, int *another)
{
	Scr.Feel.flags |= (unsigned long)arg;
	if (another) {
		long i = strtol (text, NULL, 0);
		if (i)
			Scr.Feel.flags |= (unsigned long)another;
	}
}

void SetLookFlag (char *text, FILE * fd, char **arg, int *another)
{
	unsigned long *flags = (unsigned long *)another;
	char *ptr;
	int val = strtol (text, &ptr, 0);

	if (flags == NULL)
		flags = &Scr.Look.flags;
	if (ptr != text && val == 0)
		*flags &= ~(unsigned long)arg;
	else
		*flags |= (unsigned long)arg;
}

void SetFlag2 (char *text, FILE * fd, char **arg, int *var)
{
	unsigned long *flags = (unsigned long *)var;
	char *ptr;
	int val = strtol (text, &ptr, 0);

	if (flags == NULL)
		flags = &Scr.Feel.flags;
	if (ptr != text && val == 0)
		*flags &= ~(unsigned long)arg;
	else
		*flags |= (unsigned long)arg;
}

/*****************************************************************************
 *
 * Reads in one or two integer values
 *
 ****************************************************************************/

void SetInts (char *text, FILE * fd, char **arg1, int *arg2)
{
	if (arg2 == NULL)
		sscanf (text, "%d", (int *)arg1);
	else
		sscanf (text, "%d%*c%d", (int *)arg1, (int *)arg2);
/*    LOCAL_DEBUG_OUT( "text=[%s], arg1=%p, Scr.Feel.Autoreverse = %p, res = %d", text, arg1, &(Scr.Feel.AutoReverse), *((int*)arg1) );*/
}

void SetInts2 (char *text, FILE * fd, char **arg1, int *arg2)
{
	sscanf (text, "%d", (int *)arg1);
	if (arg2)
		*arg2 = *(int *)arg1;
/*    LOCAL_DEBUG_OUT( "text=[%s], arg1=%p, Scr.Feel.Autoreverse = %p, res = %d", text, arg1, &(Scr.Feel.AutoReverse), *((int*)arg1) );*/
}

/*****************************************************************************
 *
 * Reads in a list of mouse button numbers
 *
 ****************************************************************************/

void SetButtonList (char *text, FILE * fd, char **arg1, int *arg2)
{
	int i, b;
	char *next;

	for (i = 0; i < MAX_MOUSE_BUTTONS; i++) {
		b = (int)strtol (text, &next, 0);
		if (next == text)
			break;
		text = next;
		if (*text == ',')
			text++;
		if ((b > 0) && (b <= MAX_MOUSE_BUTTONS))
			Scr.Feel.RaiseButtons |= 1 << b;
	}
	set_flags (Scr.Feel.flags, ClickToRaise);
}


/*****************************************************************************
 *
 * Reads Dimensions for an icon box from the config file
 *
 ****************************************************************************/

void SetBox (char *text, FILE * fd, char **arg, int *junk)
{
	int x1 = 0, y1 = 0, x2 = Scr.MyDisplayWidth, y2 = Scr.MyDisplayHeight;
	int num;

	/* not a standard X11 geometry string : */
	num = sscanf (text, "%d%d%d%d", &x1, &y1, &x2, &y2);

	/* check for negative locations */
	if (x1 < 0)
		x1 += Scr.MyDisplayWidth;
	if (y1 < 0)
		y1 += Scr.MyDisplayHeight;

	if (x2 < 0)
		x2 += Scr.MyDisplayWidth;
	if (y2 < 0)
		y2 += Scr.MyDisplayHeight;

	if (x1 >= x2 || y1 >= y2 ||
			x1 < 0 || x1 > Scr.MyDisplayWidth || x2 < 0
			|| x2 > Scr.MyDisplayWidth || y1 < 0 || y1 > Scr.MyDisplayHeight
			|| y2 < 0 || y2 > Scr.MyDisplayHeight) {
		show_error ("invalid IconBox '%s'", text);
	} else {
		int box_no = Scr.Look.configured_icon_areas_num;
		Scr.Look.configured_icon_areas =
				realloc (Scr.Look.configured_icon_areas,
								 (box_no + 1) * sizeof (ASGeometry));
		Scr.Look.configured_icon_areas[box_no].x = x1;
		Scr.Look.configured_icon_areas[box_no].y = y1;
		Scr.Look.configured_icon_areas[box_no].width = x2 - x1;
		Scr.Look.configured_icon_areas[box_no].height = y2 - y1;
		Scr.Look.configured_icon_areas[box_no].flags =
				XValue | YValue | WidthValue | HeightValue;
		if (x1 > Scr.MyDisplayWidth - x2)
			Scr.Look.configured_icon_areas[box_no].flags |= XNegative;
		if (y1 > Scr.MyDisplayHeight - y2)
			Scr.Look.configured_icon_areas[box_no].flags |= YNegative;
		++Scr.Look.configured_icon_areas_num;
	}
}

void SetFramePart (char *text, FILE * fd, char **frame, int *id)
{
	char *fname = NULL;
	if (parse_filename (text, &fname) != text) {
		union {
			int *ptr;
			int id;
		} ptr_id;
		ptr_id.ptr = id;
		if (LegacyFrameDef == NULL) {
			AddMyFrameDefinition (&LegacyFrameDef);
			LegacyFrameDef->name = mystrdup ("default");
		}
		show_warning
				("Frame* definitions are deprecated in look. Please use MyFrame ... ~MyFrame structures instead.%s",
				 "");
		set_string_value (&(LegacyFrameDef->parts[ptr_id.id]), fname,
											&(LegacyFrameDef->set_parts), (0x01 << ptr_id.id));
		set_flags (LegacyFrameDef->parts_mask, (0x01 << ptr_id.id));
	}
}

void SetModifier (char *text, FILE * fd, char **mod, int *junk2)
{
	int *pmod = (int *)mod;
	if (pmod)
		*pmod = parse_modifier (text);
}

void SetTButtonOrder (char *text, FILE * fd, char **unused1, int *unused2)
{
	unsigned int *xref = (unsigned int *)&(Scr.Look.button_xref[0]);
	unsigned int *rbtn = (unsigned int *)&(Scr.Look.button_first_right);
	if (xref && rbtn) {
		register int i = 0, btn = 0;
		*rbtn = TITLE_BUTTONS;
		while (!isspace (text[i]) && text[i] != '\0') {
			int context = C_NO_CONTEXT;
			switch (text[i]) {
			case '0':
				context = C_TButton0;
				break;
			case '1':
				context = C_TButton1;
				break;
			case '2':
				context = C_TButton2;
				break;
			case '3':
				context = C_TButton3;
				break;
			case '4':
				context = C_TButton4;
				break;
			case '5':
				context = C_TButton5;
				break;
			case '6':
				context = C_TButton6;
				break;
			case '7':
				context = C_TButton7;
				break;
			case '8':
				context = C_TButton8;
				break;
			case '9':
				context = C_TButton9;
				break;
			case 'T':
			case 't':
				context = C_TITLE;
				break;
			default:
				show_warning
						("invalid context specifier '%c' in TitleButtonOrder setting",
						 text[i]);
			}
			if (context == C_TITLE) {
				*rbtn = btn;
			} else if (context != C_NO_CONTEXT) {
				xref[btn] = context;

				if (++btn >= TITLE_BUTTONS)
					break;
			}
			++i;
		}
		while (btn < TITLE_BUTTONS) {
			xref[btn] = C_NO_CONTEXT;
			++btn;
		}
	}
}


/****************************************************************************
 *
 * These routines put together files from start directory
 *
 ***************************************************************************/

void dirtree_print_tree (dirtree_t * tree, int depth);

int MeltStartMenu (char *buf)
{
	char *as_start = NULL;
	dirtree_t *tree;

	switch (Scr.Look.StartMenuSortMode) {
	case SORTBYALPHA:
		dirtree_compar_list[0] = dirtree_compar_base_order;
		dirtree_compar_list[1] = dirtree_compar_order;
		dirtree_compar_list[2] = dirtree_compar_type;
		dirtree_compar_list[3] = dirtree_compar_alpha;
		dirtree_compar_list[4] = NULL;
		break;

	case SORTBYDATE:
		dirtree_compar_list[0] = dirtree_compar_base_order;
		dirtree_compar_list[1] = dirtree_compar_order;
		dirtree_compar_list[2] = dirtree_compar_type;
		dirtree_compar_list[3] = dirtree_compar_mtime;
		dirtree_compar_list[4] = NULL;
		break;

	default:
		dirtree_compar_list[0] = NULL;
		break;
	}

	/*
	 *    Here we test the existence of various
	 *    directories used for the generation.
	 */

	as_start = make_session_dir (Session, START_DIR, False);
	tree = dirtree_new_from_dir (as_start);
	show_progress ("MENU loaded from \"%s\" ...", as_start);
	free (as_start);

#ifdef FIXED_DIR
	{
		char *as_fixeddir = make_session_dir (Session, FIXED_DIR, False);

		if (CheckDir (as_fixeddir) == 0) {
			dirtree_t *fixed_tree = dirtree_new_from_dir (as_fixeddir);

			free (as_fixeddir);

			dirtree_move_children (tree, fixed_tree);
			dirtree_delete (fixed_tree);
			show_progress ("FIXED MENU loaded from \"%s\" ...", as_fixeddir);
		} else
			show_error ("unable to locate the fixed menu directory at \"%s\"",
									as_fixeddir);
		free (as_fixeddir);
	}
#endif													/* FIXED_DIR */

	dirtree_parse_include (tree);
	dirtree_remove_order (tree);
	dirtree_merge (tree);
	dirtree_sort (tree);
/*	dirtree_print_tree( tree, 0) ; */
	dirtree_set_id (tree, 0);
	/* make sure one copy of the root menu uses the name "0" */
	(*tree).flags &= ~DIRTREE_KEEPNAME;

	dirtree_make_menu2 (tree, buf, True);
	/* to keep backward compatibility, make a copy of the root menu with
	 * the name "start" */
	{
		if ((*tree).name != NULL)
			free ((*tree).name);
		(*tree).name = mystrdup ("start");
		(*tree).flags |= DIRTREE_KEEPNAME;
		dirtree_make_menu2 (tree, buf, False);
	}
	/* cleaning up cache of the searcher */
	is_executable_in_path (NULL);

	dirtree_delete (tree);
	return 0;
}

void deskback_parse (char *text, FILE * fd, char **junk, int *junk2)
{
	register int i = 0;
	int desk = atoi (text);
	char *data = NULL;
	MyDesktopConfig *dc = NULL;

	if (!IsValidDesk (desk)) {
		show_error ("invalid desktop number in: \"%s\"", text);
		return;
	}

	while (isdigit (text[i]))
		++i;
	if (i == 0 || !isspace (text[i])) {
		show_error ("missing desktop number in: \"%s\"", text);
		return;
	}

	while (isspace (text[i]))
		++i;
	if (text[i] == '#')
		return;

	data = stripcpy2 (&(text[i]), 0);
	if (data == NULL) {
		show_error
				("DeskBack option with no name of the relevant MyBackground: \"%s\"",
				 text);
		return;
	}
	LOCAL_DEBUG_OUT ("desk(%d)->data(\"%s\")->text(%s)", desk, data, text);
	dc = create_mydeskconfig (desk, data);
	add_deskconfig (&(Scr.Look), dc);
	free (data);
}

/****************************************************************************
 *
 * Matches text from config to a table of strings, calls routine
 * indicated in table.
 *
 ****************************************************************************/

void
match_string (struct config *table, char *text, char *error_msg, FILE * fd)
{
	register int i;
	table = find_config (table, text);
	if (table != NULL) {
		i = strlen (table->keyword);
		while (isspace (text[i]))
			++i;
		table->action (&(text[i]), fd, table->arg, table->arg2);
	} else
		tline_error (error_msg);
}
