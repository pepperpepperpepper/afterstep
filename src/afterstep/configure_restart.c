#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include "configure_internal.h"

void QuickRestart (char *what)
{
	unsigned long what_flags = 0;
	Bool update_background = False;

	if (what == NULL)
		return;

	if (strcasecmp (what, "all") == 0 || strcasecmp (what, "theme") == 0)
		what_flags = PARSE_EVERYTHING;
	else if (strcasecmp (what, "look&feel") == 0)
		what_flags = PARSE_LOOK_CONFIG | PARSE_FEEL_CONFIG;
	else if (strcasecmp (what, "startmenu") == 0
					 || strcasecmp (what, "feel") == 0)
		what_flags = PARSE_FEEL_CONFIG;
	else if (strcasecmp (what, "look") == 0)
		what_flags = PARSE_LOOK_CONFIG;
	else if (strcasecmp (what, "base") == 0)
		what_flags = PARSE_BASE_CONFIG;
	else if (strcasecmp (what, "database") == 0)
		what_flags = PARSE_DATABASE_CONFIG;
	else if (strcasecmp (what, "background") == 0)
		update_background = True;

	/* Force reinstall */
	if (what) {
		InstallRootColormap ();
		GrabEm (ASDefaultScr, Scr.Feel.cursors[ASCUR_Wait]);
		LoadASConfig (Scr.CurrentDesk, what_flags);
		UngrabEm ();
	}

	if (update_background)
		SendPacket (-1, M_NEW_BACKGROUND, 1, 1);
	SendPacket (-1, M_NEW_CONFIG, 1, what_flags);
}
