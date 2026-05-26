#ifndef AFTERSTEP_CONFIGURE_INTERNAL_H
#define AFTERSTEP_CONFIGURE_INTERNAL_H

#include "asinternals.h"

#include "../../libAfterConf/afterconf.h"

/* Legacy parser/config state shared across split translation units. */
extern ASFeel TmpFeel;
extern MyLook TmpLook;

extern char *Stdfont;
extern char *Windowfont;
extern char *Iconfont;

extern char *WindowForeColor[BACK_STYLES];
extern char *WindowBackColor[BACK_STYLES];
extern char *WindowGradient[BACK_STYLES];
extern char *WindowPixmap[BACK_STYLES];

extern char *MenuForeColor[MENU_BACK_STYLES];
extern char *MenuBackColor[MENU_BACK_STYLES];
extern char *MenuGradient[MENU_BACK_STYLES];
extern char *MenuPixmap[MENU_BACK_STYLES];

extern char *IconBgColor;
extern char *IconTexColor;
extern char *IconPixmapFile;

extern char *TexTypes;
extern int TitleTextType;
extern int TitleTextY;
extern int IconTexType;

extern char *MenuPinOn;

extern MyStyleDefinition *MyStyleList;
extern MyFrameDefinition *MyFrameList;
extern MyFrameDefinition *LegacyFrameDef;

extern balloonConfig BalloonConfig;
extern balloonConfig MenuBalloonConfig;

extern char *MSWindowName[BACK_STYLES];
extern char *MSMenuName[MENU_BACK_STYLES];

/* Split-out helpers used by configure.c orchestration. */
int ParseConfigFile (const char *file, char **tline);
int MeltStartMenu (char *buf);

void merge_feel (ASFeel * to, ASFeel * from);
void merge_look (MyLook * to, MyLook * from);

void ApplyFeel (ASFeel * feel);
void FixLook (MyLook * look);
void fix_menu_pin_on (MyLook * look);

Bool redecorate_aswindow_iter_func (void *data, void *aux_data);
void advertise_tbar_props ();

#endif /* AFTERSTEP_CONFIGURE_INTERNAL_H */
