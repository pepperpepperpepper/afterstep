/*
 * Copyright (C) 2000 Sasha Vasko <sasha at aftercode.net>
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

#include "../configure.h"

#define LOCAL_DEBUG
#include "asapp.h"
#include "afterstep.h"
#include "asdatabase.h"
#include "screen.h"
#include "functions.h"
#include "clientprops.h"
#include "hints.h"
#include "hints_private.h"

void
merge_kde_hints (ASHints * clean, ASRawHints * raw,
								 ASDatabaseRecord * db_rec, ASStatusHints * status,
								 ASFlagType what)
{
	register KDEHints *kh;

	if (raw == NULL)
		return;
	kh = &(raw->kde_hints);
	if (kh->flags == 0)
		return;

	if (get_flags (what, HINT_STARTUP) && status != NULL) {
		if (get_flags (kh->flags, KDE_SysTrayWindowFor))
			set_flags (status->flags, AS_StartsSticky);
	}

	if (get_flags (what, HINT_GENERAL)) {
		if (get_flags (kh->flags, KDE_DesktopWindow))
			set_flags (clean->protocols, AS_DoesKIPC);
		if (get_flags (kh->flags, KDE_SysTrayWindowFor)) {
			set_flags (clean->flags, AS_SkipWinList | AS_DontCirculate);
			clear_flags (clean->flags, AS_Handles | AS_Frame);
		}
	}
}

