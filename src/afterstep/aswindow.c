/*
 * Copyright (c) 2000 Sasha Vasko <sashav@sprintmail.com>
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

#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

/*
 * `aswindow.c` historically collected most ASWindow list/lifecycle/session logic.
 * It has been split into focused compilation units:
 *  - `window_list.c`: ASWindowList init/destroy, xref helpers, layers.
 *  - `window_session.c`: workspace save/close helpers.
 *  - `window_lookup.c`: bookmark/pattern window lookup helpers.
 *  - `window_lifecycle.c`: group/transient + enlist/delist helpers.
 *  - `window_focus.c`: focus/circulation helpers.
 *  - `window_stacking.c`: stacking/restack helpers.
 *  - `window_menus.c`: winlist menu generation helpers.
 */

