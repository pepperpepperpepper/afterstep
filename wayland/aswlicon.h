#ifndef ASWL_ICON_H
#define ASWL_ICON_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Load an icon given an AfterStep-style icon spec (e.g. "normal/Document",
 * "logos/AfterStep", "Text.xpm"), an XDG icon name ("firefox"), or a filesystem
 * path. Returns newly allocated ARGB pixels that the caller must free().
 *
 * XDG icon lookup uses the preferred theme from $ASWL_ICON_THEME (if set) or
 * ~/.config/gtk-3.0/settings.ini, falling back to "hicolor"/"Adwaita" and
 * /usr/share/pixmaps.
 */
bool aswl_icon_load_argb(const char *spec, uint32_t **out_argb, int *out_w, int *out_h);

#endif
