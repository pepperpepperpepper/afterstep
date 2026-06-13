#ifndef ASWL_THEME_INTERNAL_H
#define ASWL_THEME_INTERNAL_H

#include "aswltheme.h"

char *aswl_trim(char *s);
char *aswl_dup_unquoted(const char *s);
bool aswl_parse_hex_color(const char *s, uint32_t *argb_out);

bool aswl_theme_is_file_readable(const char *path);

struct aswl_color_entry {
	char *name;
	uint32_t argb;
};

void aswl_free_colors(struct aswl_color_entry *colors, size_t count);
bool aswl_colors_lookup(const struct aswl_color_entry *colors, size_t count, const char *name, uint32_t *argb_out);
bool aswl_load_colorscheme(const char *path, struct aswl_color_entry **colors_out, size_t *count_out);

struct aswl_style {
	char *name;
	char *fore;
	char *back;
	char *font;
	int text_style; /* -1 when unset; otherwise 0..9 (AfterStep TextStyle) */
	int back_pixmap_type;
	char *back_pixmap;
	int back_grad_type;
	char **back_grad_colors;
	double *back_grad_offsets;
	size_t back_grad_count;
	char **inherits;
	size_t inherit_count;
};

struct aswl_look_directives {
	char *menu_item_style;
	char *menu_hilite_style;
	char *menu_title_style;
	char *menu_hititle_style;
	char *fwindow_style;
	char *uwindow_style;
	char *swindow_style;
};

void aswl_free_styles(struct aswl_style *styles, size_t count);
void aswl_free_look_directives(struct aswl_look_directives *d);
bool aswl_load_look(const char *path,
                    struct aswl_style **styles_out,
                    size_t *style_count_out,
                    struct aswl_look_directives *dirs_out);

bool aswl_resolve_style_color(struct aswl_style *styles,
                             size_t style_count,
                             const char *style_name,
                             bool want_fore,
                             const struct aswl_color_entry *colors,
                             size_t color_count,
                             uint32_t *argb_out);

bool aswl_resolve_style_font(struct aswl_style *styles, size_t style_count, const char *style_name, char **font_out);

bool aswl_resolve_style_text_style(struct aswl_style *styles, size_t style_count, const char *style_name, int *text_style_out);

bool aswl_resolve_style_back_pixmap_tint(struct aswl_style *styles,
                                         size_t style_count,
                                         const char *style_name,
                                         const struct aswl_color_entry *colors,
                                         size_t color_count,
                                         int *type_out,
                                         uint32_t *tint_out);

/* Resolves image-backed BackPixmap textures (127 scaled / 128 tiled). On
 * success *path_out is a malloc'd pixmap file path (caller frees). */
bool aswl_resolve_style_back_pixmap_path(struct aswl_style *styles,
                                         size_t style_count,
                                         const char *style_name,
                                         char **path_out,
                                         int *type_out);

bool aswl_resolve_style_gradient(struct aswl_style *styles,
                                 size_t style_count,
                                 const char *style_name,
                                 const struct aswl_color_entry *colors,
                                 size_t color_count,
                                 struct aswl_gradient *grad_out);

#endif
