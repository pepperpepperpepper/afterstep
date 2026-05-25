#ifndef ASWLICON_INTERNAL_H
#define ASWLICON_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum aswl_icon_kind {
	ASWL_ICON_KIND_UNKNOWN = 0,
	ASWL_ICON_KIND_PNG,
	ASWL_ICON_KIND_XPM,
	ASWL_ICON_KIND_XML,
};

struct aswl_icon_layer {
	char *src;
	int x;
	int y;
	int w;
	int h;
};

struct aswl_scale_ctx {
	int x;
	int y;
	int w;
	int h;
};

bool aswl_is_file_readable(const char *path);
char *aswl_expand_tilde(const char *path);

void aswl_rstrip_ws(char *s);
char *aswl_lstrip_ws(char *s);

enum aswl_icon_kind aswl_detect_icon_kind(const char *path);
char *aswl_resolve_icon_spec(const char *spec);

bool aswl_read_file(const char *path, char **out, size_t *out_len);

bool aswl_icon_load_composite_list(const char *spec, int depth, uint32_t **out_argb, int *out_w, int *out_h);
bool aswl_icon_load_xml(const char *xml_path, int depth, uint32_t **out_argb, int *out_w, int *out_h);

bool aswl_icon_load_argb_rec(const char *spec, int depth, uint32_t **out_argb, int *out_w, int *out_h);

#ifdef HAVE_LIBPNG
bool aswl_load_png_argb(const char *path, uint32_t **out_argb, int *out_w, int *out_h);
#endif

#ifdef HAVE_AFTERIMAGE
bool aswl_icon_load_xml_afterimage(const char *xml_path, uint32_t **out_argb, int *out_w, int *out_h);
bool aswl_icon_load_file_afterimage(const char *path, uint32_t **out_argb, int *out_w, int *out_h);
#endif

#endif
