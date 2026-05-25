#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlicon.h"
#include "aswlicon_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool aswl_icon_load_argb_rec(const char *spec, int depth, uint32_t **out_argb, int *out_w, int *out_h)
{
	if (out_argb != NULL)
		*out_argb = NULL;
	if (out_w != NULL)
		*out_w = 0;
	if (out_h != NULL)
		*out_h = 0;

	if (spec == NULL || spec[0] == '\0')
		return false;

	if (depth > 8)
		return false;

	if (strchr(spec, ',') != NULL)
		return aswl_icon_load_composite_list(spec, depth, out_argb, out_w, out_h);

	char *path = aswl_resolve_icon_spec(spec);
	if (path == NULL)
		return false;

	bool ok = false;
	enum aswl_icon_kind kind = aswl_detect_icon_kind(path);

#ifdef HAVE_LIBPNG
	if (kind == ASWL_ICON_KIND_PNG)
		ok = aswl_load_png_argb(path, out_argb, out_w, out_h);
#endif

	if (!ok && kind == ASWL_ICON_KIND_XML) {
#ifdef HAVE_AFTERIMAGE
		ok = aswl_icon_load_xml_afterimage(path, out_argb, out_w, out_h);
#endif
		if (!ok)
			ok = aswl_icon_load_xml(path, depth, out_argb, out_w, out_h);
	}

#ifdef HAVE_AFTERIMAGE
	if (!ok && kind != ASWL_ICON_KIND_XML)
		ok = aswl_icon_load_file_afterimage(path, out_argb, out_w, out_h);
#endif

#ifdef HAVE_LIBPNG
	if (!ok)
		ok = aswl_load_png_argb(path, out_argb, out_w, out_h);
#endif

	free(path);
	return ok;
}

bool aswl_icon_load_argb(const char *spec, uint32_t **out_argb, int *out_w, int *out_h)
{
	return aswl_icon_load_argb_rec(spec, 0, out_argb, out_w, out_h);
}
