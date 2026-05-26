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

/* old look auxilary variables : */
static MyFont StdFont = { NULL };	/* font structure */
static MyFont WindowFont = { NULL };	/* font structure for window titles */
static MyFont IconFont = { NULL };	/* for icon labels */

/*
 * the old-style look variables
 */
char *Stdfont = NULL;
char *Windowfont = NULL;
char *Iconfont = NULL;

char *WindowForeColor[BACK_STYLES] = { NULL };
char *WindowBackColor[BACK_STYLES] = { NULL };
char *WindowGradient[BACK_STYLES] = { NULL };
char *WindowPixmap[BACK_STYLES] = { NULL };
char *MenuForeColor[MENU_BACK_STYLES] = { NULL };
char *MenuBackColor[MENU_BACK_STYLES] = { NULL };
char *MenuGradient[MENU_BACK_STYLES] = { NULL };
char *MenuPixmap[MENU_BACK_STYLES] = { NULL };

char *IconBgColor = NULL;
char *IconTexColor = NULL;
char *IconPixmapFile = NULL;

char *TexTypes = NULL;
int TitleTextType = 0;
int TitleTextY = 0;
int IconTexType = TEXTURE_BUILTIN;

char *MenuPinOn = NULL;
static int MenuPinOnButton = -1;

MyStyleDefinition *MyStyleList = NULL;
MyFrameDefinition *MyFrameList = NULL;
MyFrameDefinition *LegacyFrameDef = NULL;

balloonConfig BalloonConfig =
		{ 0, 0, 0, 0, 0, 0, 0, 0, NULL, 0, 0 };
balloonConfig MenuBalloonConfig =
		{ 0, 0, 0, 0, 0, 0, 0, 0, NULL, 0, 0 };

char *MSWindowName[BACK_STYLES] = { NULL };
char *MSMenuName[MENU_BACK_STYLES] = { NULL };

/***************************************************************
 * get an icon
 **************************************************************/
void CheckImageManager ()
{
	if (Scr.image_manager == NULL)
		reload_screen_image_manager (&Scr, NULL);
}

Bool GetIconFromFile (char *file, MyIcon * icon, int max_colors)
{
	CheckImageManager ();
	memset (icon, 0x00, sizeof (icon_t));
	return load_icon (icon, file, Scr.image_manager);
}

ASImage *GetASImageFromFile (char *file)
{
	ASImage *im;
	CheckImageManager ();
	LOCAL_DEBUG_OUT ("loading image from file \"%s\"", file);
	im = get_asimage (Scr.image_manager, file, ASFLAGS_EVERYTHING, 100);
	if (im == NULL)
		show_error
				("failed to locate icon file \"%s\" in the IconPath and PixmapPath",
				 file);
	return im;
}

/*
 * Copies a string into a new, malloc'ed string
 * Strips all data before the second quote. and strips trailing spaces and
 * new lines
 */

char *stripcpy3 (const char *source, const Bool Warn)
{
	const char *orig_source = source;
	while ((*source != '"') && (*source != 0))
		source++;
	if (*source != 0)
		source++;
	while ((*source != '"') && (*source != 0))
		source++;
	if (*source == 0) {
		if (Warn)
			show_warning ("bad binding [%s]", orig_source);
		return 0;
	}
	source++;
	return stripcpy (source);
}

void assign_themable_path (char *text, FILE * fd, char **arg, int *junk)
{
	char *tmp = stripcpy (text);
	int tmp_len;
	char *as_theme_data = NULL;		/*make_session_dir(Session, ICON_DIR, False); */

	replaceEnvVar (&tmp);
	if (as_theme_data) {
		tmp_len = strlen (tmp);
		*arg = safemalloc (tmp_len + 1 + strlen (as_theme_data) + 1);
		strcpy (*arg, tmp);
		(*arg)[tmp_len] = ':';
		strcpy ((*arg) + tmp_len + 1, as_theme_data);
		free (tmp);
		free (as_theme_data);
	} else
		*arg = tmp;
}


/*
 * initialize the old-style look variables
 */

void init_old_look_variables (Bool free_resources)
{
	int i;
	if (free_resources) {
		/* the fonts */
		if (Stdfont != NULL)
			free (Stdfont);
		if (Windowfont != NULL)
			free (Windowfont);
		if (Iconfont != NULL)
			free (Iconfont);
		for (i = 0; i < BACK_STYLES; ++i) {
			if (WindowForeColor[i])
				free (WindowForeColor[i]);
			if (WindowBackColor[i])
				free (WindowBackColor[i]);
			if (WindowGradient[i])
				free (WindowGradient[i]);
			if (WindowPixmap[i])
				free (WindowPixmap[i]);
		}
		for (i = 0; i < MENU_BACK_STYLES; ++i) {
			if (MenuForeColor[i])
				free (MenuForeColor[i]);
			if (MenuBackColor[i])
				free (MenuBackColor[i]);
			if (MenuGradient[i])
				free (MenuGradient[i]);
			if (MenuPixmap[i])
				free (MenuPixmap[i]);
		}

		if (IconBgColor)
			free (IconBgColor);
		if (IconTexColor)
			free (IconTexColor);
		if (IconPixmapFile)
			free (IconPixmapFile);
		if (TexTypes)
			free (TexTypes);
	}

	/* the fonts */
	Stdfont = NULL;
	Windowfont = NULL;
	Iconfont = NULL;

	for (i = 0; i < BACK_STYLES; ++i) {
		WindowForeColor[i] = NULL;
		WindowBackColor[i] = NULL;
		WindowGradient[i] = NULL;
		WindowPixmap[i] = NULL;
	}
	for (i = 0; i < MENU_BACK_STYLES; ++i) {
		MenuForeColor[i] = NULL;
		MenuBackColor[i] = NULL;
		MenuGradient[i] = NULL;
		MenuPixmap[i] = NULL;
	}
	IconBgColor = NULL;
	IconTexColor = NULL;
	IconPixmapFile = NULL;
	/* miscellaneous stuff */
	TexTypes = NULL;
	TitleTextType = 0;
	TitleTextY = 0;
	IconTexType = TEXTURE_BUILTIN;

}


/*
 * merge the old variables into the new styles
 * the new styles have precedence
 */
void merge_old_look_variables (MyLook * look)
{
	char *button_style_names[BACK_STYLES] = { AS_ICON_TITLE_MYSTYLE,
		AS_ICON_TITLE_UNFOCUS_MYSTYLE,
		AS_ICON_TITLE_STICKY_MYSTYLE,
		NULL,
		"ButtonTitleDefault"
	};
	MyStyle *button_styles[BACK_STYLES];
	int i;

	for (i = 0; i < BACK_STYLES; ++i)
		button_styles[i] =
				mystyle_list_find (look->styles_list, button_style_names[i]);

	/* the fonts */
	if (Stdfont != NULL) {
		if (load_font (Stdfont, &StdFont) == False)
			exit (1);
		else
			for (i = MENU_BACK_ITEM; i < MENU_BACK_STYLES; ++i)
				mystyle_inherit_font (look->MSMenu[i], &StdFont);
	}
	if (Windowfont != NULL) {
		if (load_font (Windowfont, &WindowFont) == False)
			exit (1);
		for (i = 0; i < BACK_STYLES; ++i)
			mystyle_inherit_font (look->MSWindow[i], &WindowFont);
		mystyle_inherit_font (look->MSMenu[MENU_BACK_TITLE], &WindowFont);
	}
	if (Iconfont != NULL) {
		if (load_font (Iconfont, &IconFont) == False)
			exit (1);
		for (i = 0; i < BACK_STYLES; ++i)
			mystyle_inherit_font (button_styles[i], &IconFont);
	}
	/* the text type */
	if (TitleTextType != 0) {
		for (i = 0; i < BACK_STYLES; ++i)
			if (look->MSWindow[i])
				if (!get_flags (look->MSWindow[i]->set_flags, F_TEXTSTYLE)) {
					set_flags (look->MSWindow[i]->text_style, TitleTextType);
					set_flags (look->MSWindow[i]->user_flags, F_TEXTSTYLE);
					set_flags (look->MSWindow[i]->set_flags, F_TEXTSTYLE);
				}
	}
	/* the colors */
	/* for black and white - ignore user choices */
	/* for color - accept user choices */
	if (Scr.d_depth > 1) {
		int wtype[BACK_STYLES] = { 0 };
		int mtype[MENU_BACK_STYLES] = { 0 };

		for (i = 0; i < BACK_STYLES; ++i)
			wtype[i] = -1;
		for (i = 0; i < MENU_BACK_STYLES; ++i)
			mtype[i] = -1;

		if (TexTypes != NULL)
			sscanf (TexTypes, "%i %i %i %i %i %i", &wtype[BACK_FOCUSED],
							&wtype[BACK_UNFOCUSED], &wtype[BACK_STICKY],
							&mtype[MENU_BACK_TITLE], &mtype[MENU_BACK_ITEM],
							&mtype[MENU_BACK_HILITE]);

		if (IconTexType == TEXTURE_BUILTIN)
			IconTexType = -1;

		/* check for missing 1.4.5.x keywords */
		if (MenuForeColor[MENU_BACK_TITLE] == NULL
				&& WindowForeColor[BACK_FOCUSED] != NULL)
			MenuForeColor[MENU_BACK_TITLE] =
					mystrdup (WindowForeColor[BACK_FOCUSED]);
		if (MenuBackColor[MENU_BACK_TITLE] == NULL
				&& WindowBackColor[BACK_FOCUSED] != NULL)
			MenuBackColor[MENU_BACK_TITLE] =
					mystrdup (WindowBackColor[BACK_FOCUSED]);
		if (MenuForeColor[MENU_BACK_HILITE] == NULL
				&& WindowForeColor[BACK_FOCUSED] != NULL)
			MenuForeColor[MENU_BACK_HILITE] =
					mystrdup (WindowForeColor[BACK_FOCUSED]);
		if (MenuBackColor[MENU_BACK_HILITE] == NULL
				&& MenuBackColor[MENU_BACK_ITEM] != NULL) {
			mtype[MENU_BACK_HILITE] = mtype[MENU_BACK_ITEM];
			MenuBackColor[MENU_BACK_HILITE] =
					mystrdup (MenuBackColor[MENU_BACK_ITEM]);
			if (MenuGradient[MENU_BACK_HILITE] == NULL
					&& MenuGradient[MENU_BACK_ITEM] != NULL)
				MenuGradient[MENU_BACK_HILITE] =
						mystrdup (MenuGradient[MENU_BACK_ITEM]);
			if (MenuPixmap[MENU_BACK_HILITE] == NULL
					&& MenuPixmap[MENU_BACK_ITEM] != NULL)
				MenuPixmap[MENU_BACK_HILITE] =
						mystrdup (MenuPixmap[MENU_BACK_ITEM]);
		}
		for (i = 0; i < BACK_STYLES; ++i)
			mystyle_merge_colors (look->MSWindow[i], wtype[i],
														WindowForeColor[i], WindowBackColor[i],
														WindowGradient[i], WindowPixmap[i]);
		for (i = 0; i < MENU_BACK_STYLES; ++i)
			mystyle_merge_colors (look->MSMenu[i], mtype[i], MenuForeColor[i],
														MenuBackColor[i], MenuGradient[i],
														MenuPixmap[i]);

		{
			MyStyle *button_pixmap =
					mystyle_list_find (look->styles_list, AS_ICON_MYSTYLE);

			/* icon styles automagically inherit from window title styles */
			if (button_pixmap != NULL) {
				mystyle_merge_styles (look->MSWindow[BACK_FOCUSED], button_pixmap,
															0, 0);
				mystyle_merge_colors (button_pixmap, IconTexType, NULL,
															IconBgColor, IconTexColor, IconPixmapFile);
			}
		}
		for (i = 0; i < BACK_STYLES; ++i)
			if (button_styles[i] != NULL)
				mystyle_merge_styles (look->MSWindow[i], button_styles[i], 0, 0);
	}
	init_old_look_variables (True);	/* no longer need those strings !!!! */
}


/*
 * Initialize feel variables
 */
void InitFeel (ASFeel * feel, Bool free_resources)
{
	if (feel) {
		if (free_resources)
			destroy_asfeel (feel, True);
		feel->magic = MAGIC_ASFEEL;
		init_asfeel (feel);
	}
}


void ApplyFeel (ASFeel * feel)
{
	check_screen_panframes (ASDefaultScr);

}

/*
 * Initialize look variables
 */
void InitLook (MyLook * look, Bool free_resources)
{
	int i;
	/* actuall MyLook cleanup : */

	mylook_init (look, free_resources, LL_Everything);

	/* other related things : */
	if (free_resources) {
		/* icons */
		if (MenuPinOn != NULL)
			free (MenuPinOn);
		if (Scr.default_icon_box)
			destroy_asiconbox (&(Scr.default_icon_box));
		if (Scr.icon_boxes)
			destroy_ashash (&(Scr.icon_boxes));

		/* temporary old-style fonts : */
		unload_font (&StdFont);
		unload_font (&WindowFont);
		unload_font (&IconFont);
		DestroyMyStyleDefinitions (&MyStyleList);
		DestroyMyFrameDefinitions (&MyFrameList);
		if (LegacyFrameDef)
			DestroyMyFrameDefinitions (&LegacyFrameDef);
		if (BalloonConfig.Style)
			free (BalloonConfig.Style);
		for (i = 0; i < BACK_STYLES; ++i)
			if (MSWindowName[i])
				free (MSWindowName[i]);
		for (i = 0; i < MENU_BACK_STYLES; ++i)
			if (MSMenuName[i])
				free (MSMenuName[i]);
	}
	MenuPinOn = NULL;
	MenuPinOnButton = -1;

	Scr.default_icon_box = NULL;
	Scr.icon_boxes = NULL;

	/* temporary old-style fonts : */
	memset (&StdFont, 0x00, sizeof (MyFont));
	memset (&WindowFont, 0x00, sizeof (MyFont));
	memset (&IconFont, 0x00, sizeof (MyFont));

	MyStyleList = NULL;
	for (i = 0; i < BACK_STYLES; ++i)
		MSWindowName[i] = NULL;
	for (i = 0; i < MENU_BACK_STYLES; ++i)
		MSMenuName[i] = NULL;

	MyFrameList = NULL;
	LegacyFrameDef = NULL;
	memset (&BalloonConfig, 0x00, sizeof (BalloonConfig));
}


void make_styles (MyLook * look)
{
/* make sure the globals are defined */
	char *style_names[BACK_STYLES] =
			{ "FWindow", "UWindow", "SWindow", NULL, "default" };
	char *menu_style_names[MENU_BACK_STYLES] =
			{ "MenuTitle", "MenuItem", "MenuHilite", "MenuStipple",
"MenuSubItem", "MenuHiTitle" };
	int i;

	for (i = 0; i < BACK_STYLES; ++i)
		if (MSWindowName[i])
			look->MSWindow[i] =
					mystyle_find_or_get_from_file (look->styles_list,
																				 MSWindowName[i]);
	if (look->MSWindow[BACK_DEFAULT] == NULL)
		look->MSWindow[BACK_DEFAULT] =
				mystyle_find_or_get_from_file (look->styles_list, "default");

	/* this is the last resort : */
	if (look->MSWindow[BACK_DEFAULT] == NULL)
		look->MSWindow[BACK_DEFAULT] =
				mystyle_list_find_or_default (look->styles_list, "default");

	for (i = 0; i < BACK_STYLES; ++i)
		if (look->MSWindow[i] == NULL && style_names[i])
			look->MSWindow[i] =
					mystyle_list_new (look->styles_list, style_names[i]);

	for (i = 0; i < MENU_BACK_STYLES; ++i)
		if (MSMenuName[i])
			look->MSMenu[i] =
					mystyle_find_or_get_from_file (look->styles_list, MSMenuName[i]);

	for (i = 0; i < MENU_BACK_STYLES; ++i)
		if (look->MSMenu[i] == NULL) {
			if (i == MENU_BACK_SUBITEM)
				look->MSMenu[i] = look->MSMenu[MENU_BACK_ITEM];
			else if (i == MENU_BACK_HITITLE)
				look->MSMenu[i] = look->MSWindow[BACK_FOCUSED];
			else
				look->MSMenu[i] =
						mystyle_list_new (look->styles_list, menu_style_names[i]);
		}
	if (mystyle_find_or_get_from_file (look->styles_list, "ButtonPixmap") ==
			NULL)
		mystyle_list_new (look->styles_list, "ButtonPixmap");
	if (mystyle_find_or_get_from_file (look->styles_list, "ButtonTitleFocus")
			== NULL)
		mystyle_list_new (look->styles_list, "ButtonTitleFocus");
	if (mystyle_find_or_get_from_file
			(look->styles_list, "ButtonTitleSticky") == NULL)
		mystyle_list_new (look->styles_list, "ButtonTitleSticky");
	if (mystyle_find_or_get_from_file
			(look->styles_list, "ButtonTitleUnfocus") == NULL)
		mystyle_list_new (look->styles_list, "ButtonTitleUnfocus");
}

MyFrame *add_myframe_from_def (MyLook * look, MyFrameDefinition * fd,
															 ASFlagType default_title_align)
{
	ASHashTable *list = look->FramesList;
	MyFrame *frame;
	int i;

	frame = get_flags (fd->flags, MYFRAME_INHERIT_DEFAULTS) ?
			create_default_myframe (default_title_align) : create_myframe ();

	frame->name = mystrdup (fd->name);
	for (i = 0; i < fd->inheritance_num; ++i) {
		ASHashData hdata;
		if (get_hash_item
				(list, AS_HASHABLE (fd->inheritance_list[i]),
				 &hdata.vptr) == ASH_Success)
			inherit_myframe (frame, hdata.vptr);
	}
	frame->parts_mask =
			(frame->parts_mask & (~fd->set_parts)) | fd->parts_mask;
	LOCAL_DEBUG_OUT ("parts_mask == 0x%lX", frame->parts_mask);
	frame->set_parts |= fd->set_parts;
	for (i = 0; i < FRAME_PARTS; ++i) {
		if (fd->parts[i])
			set_string (&(frame->part_filenames[i]), mystrdup (fd->parts[i]));
		if (get_flags (fd->set_part_size, 0x01 << i)) {
			frame->part_width[i] = max (fd->part_width[i], 1);
			frame->part_length[i] = max (fd->part_length[i], 1);
		}
		if (IsPartFBevelSet (fd, i))
			frame->part_fbevel[i] = fd->part_fbevel[i];
		if (IsPartUBevelSet (fd, i))
			frame->part_ubevel[i] = fd->part_ubevel[i];
		if (IsPartSBevelSet (fd, i))
			frame->part_sbevel[i] = fd->part_sbevel[i];
		if (get_flags (fd->set_part_align, 0x01 << i))
			frame->part_align[i] = fd->part_align[i];
	}
	frame->set_part_size |= fd->set_part_size;
	frame->set_part_bevel |= fd->set_part_bevel;
	frame->set_part_align |= fd->set_part_align;

	for (i = 0; i < FRAME_PARTS; ++i)
		if (frame->part_filenames[i])
			if (!get_flags (frame->set_part_align, 0x01 << i)) {
				frame->part_align[i] = RESIZE_V | RESIZE_H;
				set_flags (frame->set_part_align, 0x01 << i);
			}

	for (i = 0; i < BACK_STYLES; ++i) {
		if (fd->title_styles[i]) {
			set_string (&(frame->title_style_names[i]),
									mystrdup (fd->title_styles[i]));
			/* force load the MyStyle in question */
			mystyle_find_or_get_from_file (look->styles_list,
																		 fd->title_styles[i]);
		}
		if (fd->frame_styles[i]) {
			set_string (&(frame->frame_style_names[i]),
									mystrdup (fd->frame_styles[i]));
			/* force load the MyStyle in question */
			mystyle_find_or_get_from_file (look->styles_list,
																		 fd->title_styles[i]);
		}
	}
	if (get_flags (fd->set_title_attr, MYFRAME_TitleFBevelSet))
		frame->title_fbevel = fd->title_fbevel;
	if (get_flags (fd->set_title_attr, MYFRAME_TitleUBevelSet))
		frame->title_ubevel = fd->title_ubevel;
	if (get_flags (fd->set_title_attr, MYFRAME_TitleSBevelSet))
		frame->title_sbevel = fd->title_sbevel;
	if (get_flags (fd->set_title_attr, MYFRAME_TitleAlignSet))
		frame->title_align = fd->title_align;
	if (get_flags (fd->set_title_attr, MYFRAME_LeftBtnAlignSet))
		frame->left_btn_align = fd->left_btn_align;
	if (get_flags (fd->set_title_attr, MYFRAME_RightBtnAlignSet))
		frame->right_btn_align = fd->right_btn_align;

	if (get_flags (fd->set_title_attr, MYFRAME_CondenseTitlebarSet))
		frame->condense_titlebar = fd->condense_titlebar;
	if (get_flags (fd->set_title_attr, MYFRAME_LeftTitlebarLayoutSet)) {
		frame->left_layout = fd->left_layout;
		LOCAL_DEBUG_OUT ("LeftTitlebarLayout = 0x%lX", fd->left_layout);
	}
	if (get_flags (fd->set_title_attr, MYFRAME_RightTitlebarLayoutSet))
		frame->right_layout = fd->right_layout;
	if (get_flags (fd->set_title_attr, MYFRAME_TitleFCMSet))
		frame->title_fcm = fd->title_fcm;
	if (get_flags (fd->set_title_attr, MYFRAME_TitleUCMSet))
		frame->title_ucm = fd->title_ucm;
	if (get_flags (fd->set_title_attr, MYFRAME_TitleSCMSet))
		frame->title_scm = fd->title_scm;

	if (get_flags (fd->set_title_attr, MYFRAME_TitleFHueSet))
		parse_hue (fd->title_fhue, &(frame->title_fhue));
	if (get_flags (fd->set_title_attr, MYFRAME_TitleUHueSet))
		parse_hue (fd->title_uhue, &(frame->title_uhue));
	if (get_flags (fd->set_title_attr, MYFRAME_TitleSHueSet))
		parse_hue (fd->title_shue, &(frame->title_shue));

	if (get_flags (fd->set_title_attr, MYFRAME_TitleFSatSet))
		frame->title_fsat = fd->title_fsat;
	if (get_flags (fd->set_title_attr, MYFRAME_TitleUSatSet))
		frame->title_usat = fd->title_usat;
	if (get_flags (fd->set_title_attr, MYFRAME_TitleSSatSet))
		frame->title_ssat = fd->title_ssat;

	if (get_flags (fd->set_title_attr, MYFRAME_TitleHSpacingSet))
		frame->title_h_spacing = fd->title_h_spacing;
	if (get_flags (fd->set_title_attr, MYFRAME_TitleVSpacingSet))
		frame->title_v_spacing = fd->title_v_spacing;


	for (i = 0; i < MYFRAME_TITLE_BACKS; ++i) {
		if (get_flags
				(fd->set_title_attr, MYFRAME_TitleBackAlignSet_Start << i))
			frame->title_backs_align[i] = fd->title_backs_align[i];

		if (fd->title_backs[i]) {
			set_string (&(frame->title_back_filenames[i]),
									mystrdup (fd->title_backs[i]));
			if (!get_flags
					(fd->set_title_attr, MYFRAME_TitleBackAlignSet_Start << i)) {
				frame->title_backs_align[i] = FIT_LABEL_WIDTH;
				set_flags (fd->set_title_attr,
									 MYFRAME_TitleBackAlignSet_Start << i);
			}
		}
	}
	frame->set_title_attr |= fd->set_title_attr;

	/* wee need to make sure that frame has such a
	 * neccessary attributes as title align and title bevel : */
	if (!get_flags (frame->set_title_attr, MYFRAME_TitleFBevelSet))
		frame->title_fbevel = DEFAULT_TBAR_HILITE;
	if (!get_flags (frame->set_title_attr, MYFRAME_TitleUBevelSet))
		frame->title_ubevel = DEFAULT_TBAR_HILITE;
	if (!get_flags (frame->set_title_attr, MYFRAME_TitleSBevelSet))
		frame->title_sbevel = DEFAULT_TBAR_HILITE;
	if (!get_flags (frame->set_title_attr, MYFRAME_TitleAlignSet))
		frame->title_align = default_title_align;

	if (!get_flags (frame->set_title_attr, MYFRAME_LeftBtnAlignSet))
		frame->left_btn_align = ALIGN_VCENTER;
	if (!get_flags (frame->set_title_attr, MYFRAME_RightBtnAlignSet))
		frame->right_btn_align = ALIGN_VCENTER;

	set_flags (frame->set_title_attr,
						 MYFRAME_TitleBevelSet | MYFRAME_TitleAlignSet);

	frame->set_flags = fd->set_flags;
	frame->flags = fd->flags;


	if (add_hash_item (list, AS_HASHABLE (frame->name), frame) !=
			ASH_Success) {
		LOCAL_DEBUG_OUT
				("failed to add frame with the name \"%s\", currently list holds %ld frames",
				 frame->name, list->items_num);
		destroy_myframe (&frame);
	} else {
		LOCAL_DEBUG_OUT ("added frame with the name \"%s\"", frame->name);
	}
	return frame;
}

void fix_menu_pin_on (MyLook * look)
{
	if (MenuPinOn != NULL) {
		if (MenuPinOnButton < 0 || MenuPinOnButton >= TITLE_BUTTONS) {
			register int i = TITLE_BUTTONS;

			while (--i >= 0) {
				if (look->buttons[i].unpressed.image == NULL
						&& look->buttons[i].pressed.image == NULL)
					break;
			}

			if (i >= 0) {
				if (GetIconFromFile
						(MenuPinOn, &(Scr.Look.buttons[i].unpressed), 0)) {
					int context = C_TButton0 << i;
					register int k;
					Scr.Look.buttons[i].width =
							Scr.Look.buttons[i].unpressed.image->width;
					Scr.Look.buttons[i].height =
							Scr.Look.buttons[i].unpressed.image->height;
					MenuPinOnButton = i;

					if (Scr.Look.buttons[i].context == C_NO_CONTEXT)
						Scr.Look.buttons[i].context = context;
					context = Scr.Look.buttons[i].context;
					for (k = 0; k < TITLE_BUTTONS; ++k)
						if (Scr.Look.button_xref[k] == context)
							break;
					if (k == TITLE_BUTTONS) {
						while (--k >= 0)
							if (Scr.Look.button_xref[k] == C_NO_CONTEXT) {
								Scr.Look.button_xref[k] = context;
								break;
							}
					}
					if (k >= 0 && k < TITLE_BUTTONS)
						Scr.Look.ordered_buttons[k] = &(Scr.Look.buttons[i]);
					else
						show_warning
								("there is no slot on the titlebar to place button %d into. Check yout TitleButtonOrder setting.",
								 i);
				} else
					MenuPinOnButton = -1;
			}
		}
		if (MenuPinOnButton >= 0) {
			static char binding[128];
			sprintf (binding, "1 %d A PinMenu\n", MenuPinOnButton);
			/* also need to add mouse binding for this one */
			ParseMouseEntry (binding, NULL, NULL, NULL);
		}
		show_warning
				("MenuPinOn setting is depreciated - instead add a Title button and bind PinMenu function to it.");
	}
}

void FixLook (MyLook * look)
{
	ASFlagType default_title_align = ALIGN_LEFT;
	int menu_font_size = 0;
	int i;
#ifdef LOCAL_DEBUG
	LOCAL_DEBUG_OUT ("syncing %s", "");
	ASSync (False);
#endif
	/* make sure all needed styles are created */
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	PrintMyStyleDefinitions (MyStyleList);
#endif
	LOCAL_DEBUG_OUT ("MyStyleList %p", MyStyleList);
	if (MyStyleList) {
		MyStyleDefinition *sd;
		for (sd = MyStyleList; sd != NULL; sd = sd->next) {
			LOCAL_DEBUG_OUT ("processing MyStyleDefinition %p", sd);
			mystyle_create_from_definition (look->styles_list, sd);
		}
		DestroyMyStyleDefinitions (&MyStyleList);
	}
	make_styles (look);
#ifdef LOCAL_DEBUG
	LOCAL_DEBUG_OUT ("syncing %s", "");
	ASSync (False);
#endif

	/* merge pre-1.5 compatibility keywords */
	merge_old_look_variables (look);
#ifdef LOCAL_DEBUG
	LOCAL_DEBUG_OUT ("syncing %s", "");
	ASSync (False);
#endif

	/* fill in remaining members with the default style */
	mystyle_list_fix_styles (look->styles_list);
#ifdef LOCAL_DEBUG
	LOCAL_DEBUG_OUT ("syncing %s", "");
	ASSync (False);
#endif

	mylook_set_font_size_var (look);

	for (i = 0; i < MENU_BACK_STYLES; ++i)
		if (look->MSMenu[i]) {
			int font_size = mystyle_get_font_height (look->MSMenu[i]);
			if (font_size > menu_font_size)
				menu_font_size = font_size;
		}
	asxml_var_insert (ASXMLVAR_MenuFontSize, menu_font_size);


	mystyle_list_set_property (Scr.wmprops, look->styles_list);
#ifdef LOCAL_DEBUG
	LOCAL_DEBUG_OUT ("syncing %s", "");
	ASSync (False);
#endif

	if (look->TitleTextAlign == JUSTIFY_RIGHT)
		default_title_align = ALIGN_RIGHT;
	else if (look->TitleTextAlign == JUSTIFY_CENTER)
		default_title_align = ALIGN_CENTER;

	if (look->DefaultFrameName == NULL)
		look->DefaultFrameName = mystrdup ("default");
	check_myframes_list (look);

	/* update frame geometries */
	if (get_flags (look->flags, DecorateFrames)) {
		MyFrameDefinition *fd;
		MyFrame *frame;
		/* TODO: need to load the list as well (if we have any ) */
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
		PrintMyFrameDefinitions (MyFrameList, 1);
#endif
		LOCAL_DEBUG_OUT ("MyFrameList %p", MyFrameList);
		for (fd = MyFrameList; fd != NULL; fd = fd->next) {
			LOCAL_DEBUG_OUT ("processing MyFrameDefinition %p", fd);
			if ((frame =
					 add_myframe_from_def (look, fd,
																 default_title_align | ALIGN_VCENTER)) !=
					NULL)
				myframe_load (frame, Scr.image_manager);
		}
		if (LegacyFrameDef) {
			LOCAL_DEBUG_OUT ("processing legacy MyFrameDefinition %p",
											 LegacyFrameDef);
			LegacyFrameDef->name = mystrdup (look->DefaultFrameName);
			if ((frame =
					 add_myframe_from_def (look, LegacyFrameDef,
																 default_title_align | ALIGN_VCENTER)) !=
					NULL)
				myframe_load (frame, Scr.image_manager);
		}
		DestroyMyFrameDefinitions (&MyFrameList);
		DestroyMyFrameDefinitions (&LegacyFrameDef);
		LOCAL_DEBUG_OUT ("DefaultFrameName is \"%s\".",
										 look->DefaultFrameName);
	}

	if (myframe_find (look->DefaultFrameName) == NULL) {
		MyFrame *dmf =
				create_default_myframe (default_title_align | ALIGN_VCENTER);
		dmf->name = mystrdup (look->DefaultFrameName);
		if (add_hash_item (look->FramesList, AS_HASHABLE (dmf->name), dmf) !=
				ASH_Success)
			destroy_myframe (&dmf);
	}
#ifdef LOCAL_DEBUG
	LOCAL_DEBUG_OUT ("syncing %s", "");
	ASSync (False);
#endif

	/* checking that all the buttons have assigned slots in the button xref : */
	for (i = 0; i < TITLE_BUTTONS; ++i) {
		if (Scr.Look.buttons[i].unpressed.image != NULL) {
			int context = C_TButton0 << i;
			register int k;
			if (Scr.Look.buttons[i].context == C_NO_CONTEXT)
				Scr.Look.buttons[i].context = context;
			context = Scr.Look.buttons[i].context;
			for (k = 0; k < TITLE_BUTTONS; ++k)
				if (Scr.Look.button_xref[k] == context)
					break;
			if (k == TITLE_BUTTONS) {
				while (--k >= 0)
					if (Scr.Look.button_xref[k] == C_NO_CONTEXT) {
						Scr.Look.button_xref[k] = context;
						break;
					}
			}
			if (k >= 0 && k < TITLE_BUTTONS)
				Scr.Look.ordered_buttons[k] = &(Scr.Look.buttons[i]);
			else
				show_warning
						("there is no slot on the titlebar to place button %d into. Check yout TitleButtonOrder setting.",
						 i);
		}
	}

	/* checking sanity of the move-resize window geometry : */
	if ((look->resize_move_geometry.flags & (HeightValue | WidthValue)) !=
			(HeightValue | WidthValue)) {
		unsigned int width = 0;
		unsigned int height = 0;
		mystyle_get_text_size (look->MSWindow[BACK_FOCUSED],
													 " +88888 x +88888 ", &width, &height);
		if (!get_flags (look->resize_move_geometry.flags, WidthValue))
			look->resize_move_geometry.width = width + SIZE_VINDENT * 2;

		if (!get_flags (look->resize_move_geometry.flags, HeightValue))
			look->resize_move_geometry.height = height + SIZE_VINDENT * 2;

		set_flags (look->resize_move_geometry.flags, HeightValue | WidthValue);
	}
	if (look->supported_hints == NULL) {
		look->supported_hints = create_hints_list ();
		enable_hints_support (look->supported_hints, HINTS_ICCCM);
		enable_hints_support (look->supported_hints, HINTS_Motif);
		enable_hints_support (look->supported_hints, HINTS_Gnome);
		enable_hints_support (look->supported_hints, HINTS_KDE);
		enable_hints_support (look->supported_hints, HINTS_ExtendedWM);
		enable_hints_support (look->supported_hints, HINTS_ASDatabase);
		enable_hints_support (look->supported_hints, HINTS_GroupLead);
		enable_hints_support (look->supported_hints, HINTS_Transient);
	}
	switch (look->TitleButtonStyle) {
	case 0:
		look->TitleButtonXOffset[0] = look->TitleButtonXOffset[1] = 3;
		look->TitleButtonYOffset[0] = look->TitleButtonYOffset[1] = 3;
		break;
	case 1:
		look->TitleButtonXOffset[0] = look->TitleButtonXOffset[1] = 1;
		look->TitleButtonYOffset[0] = look->TitleButtonYOffset[1] = 1;
		break;
	}

	/* now we need to go through all the deskconfigs and create generic
	 * MyBackground for those that alreadyu do not have one */
	if (look->desk_configs) {
		ASHashIterator it;
		if (start_hash_iteration (look->desk_configs, &it))
			do {
				MyDesktopConfig *dc = (MyDesktopConfig *) curr_hash_data (&it);
				MyBackground *myback = mylook_get_back (look, dc->back_name);
				LOCAL_DEBUG_OUT ("myback = %p, back_name = \"%s\"", myback,
												 dc->back_name ? dc->back_name : "<NULL>");
				if (myback == NULL && dc->back_name != NULL) {
					myback = create_myback (dc->back_name);
					myback->type = MB_BackImage;
					myback->data = mystrdup (dc->back_name);
					add_myback (look, myback);
				}
			} while (next_hash_item (&it));
	} else if (!get_flags (Scr.Look.flags, DontDrawBackground)) {
		MyDesktopConfig *dc;
		MyBackground *myback;
		char *buf = safemalloc (strlen (DEFAULT_BACK_NAME) + 1);

		sprintf (buf, DEFAULT_BACK_NAME, 0);
		dc = create_mydeskconfig (0, buf);
		free (buf);

		add_deskconfig (&(Scr.Look), dc);
		myback = mylook_get_back (look, dc->back_name);
		if (myback == NULL) {
			myback = create_myback (dc->back_name);
			myback->type = MB_BackImage;
			add_myback (look, myback);
		}
	}
#ifdef LOCAL_DEBUG
	LOCAL_DEBUG_OUT ("syncing %s", "");
	ASSync (False);
#endif
}


/*
 * Initialize database variables
 */

void InitDatabase (Bool free_resources)
{
	if (free_resources) {
		destroy_asdb (&Database);
		/* XResources : */
		destroy_user_database ();
	} else
		Database = NULL;
}

/*
 * Create/destroy window titlebar/buttons as necessary.
 */
