#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xwayland.h>
#include "aswlicon.h"
#include "aswlcomp_internal.h"
struct aswl_pixbuf_buffer {
	struct wlr_buffer base;
	uint32_t *argb;
	size_t stride;
	uint32_t format;
};
void aswl_argb_to_premul_f(uint32_t argb, float out[static 4])
{
	float a = ((argb >> 24) & 0xFFu) / 255.0f;
	float r = ((argb >> 16) & 0xFFu) / 255.0f;
	float g = ((argb >> 8) & 0xFFu) / 255.0f;
	float b = (argb & 0xFFu) / 255.0f;

	out[0] = r * a;
	out[1] = g * a;
	out[2] = b * a;
	out[3] = a;
}
static void aswl_pixbuf_buffer_destroy(struct wlr_buffer *wlr_buffer)
{
	struct aswl_pixbuf_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	wlr_buffer_finish(&buf->base);
	free(buf->argb);
	free(buf);
}
static bool aswl_pixbuf_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
                                                     uint32_t flags,
                                                     void **data,
                                                     uint32_t *format,
                                                     size_t *stride)
{
	(void)flags;
	struct aswl_pixbuf_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	if (data != NULL)
		*data = buf->argb;
	if (format != NULL)
		*format = buf->format;
	if (stride != NULL)
		*stride = buf->stride;
	return true;
}
static void aswl_pixbuf_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer)
{
	(void)wlr_buffer;
}

static const struct wlr_buffer_impl aswl_pixbuf_buffer_impl = {
	.destroy = aswl_pixbuf_buffer_destroy,
	.begin_data_ptr_access = aswl_pixbuf_buffer_begin_data_ptr_access,
	.end_data_ptr_access = aswl_pixbuf_buffer_end_data_ptr_access,
};

static struct wlr_buffer *aswl_pixbuf_buffer_create(uint32_t *argb, int width, int height)
{
	if (argb == NULL || width <= 0 || height <= 0)
		return NULL;

	struct aswl_pixbuf_buffer *buf = calloc(1, sizeof(*buf));
	if (buf == NULL) {
		free(argb);
		return NULL;
	}

	buf->argb = argb;
	buf->stride = (size_t)width * 4u;
	buf->format = DRM_FORMAT_ARGB8888;

	wlr_buffer_init(&buf->base, &aswl_pixbuf_buffer_impl, width, height);
	return &buf->base;
}

static uint32_t aswl_premul_argb(uint32_t argb)
{
	uint32_t a = (argb >> 24) & 0xFFu;
	if (a == 0)
		return 0;
	if (a == 255u)
		return argb;

	uint32_t r = (argb >> 16) & 0xFFu;
	uint32_t g = (argb >> 8) & 0xFFu;
	uint32_t b = argb & 0xFFu;

	r = (r * a + 127u) / 255u;
	g = (g * a + 127u) / 255u;
	b = (b * a + 127u) / 255u;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static uint32_t aswl_unpremul_argb(uint32_t argb)
{
	uint32_t a = (argb >> 24) & 0xFFu;
	if (a == 0)
		return 0;
	if (a == 255u)
		return argb;

	uint32_t r = (argb >> 16) & 0xFFu;
	uint32_t g = (argb >> 8) & 0xFFu;
	uint32_t b = argb & 0xFFu;

	r = (r * 255u + a / 2u) / a;
	g = (g * 255u + a / 2u) / a;
	b = (b * 255u + a / 2u) / a;

	if (r > 255u)
		r = 255u;
	if (g > 255u)
		g = 255u;
	if (b > 255u)
		b = 255u;

	return (a << 24) | (r << 16) | (g << 8) | b;
}

static void aswl_blend_pixel_argb(uint32_t *dst, uint32_t src_argb)
{
	uint32_t sa = (src_argb >> 24) & 0xFFu;
	if (sa == 0)
		return;

	uint32_t src = aswl_premul_argb(src_argb);
	if (sa == 255u) {
		*dst = src;
		return;
	}

	uint32_t dst_argb = *dst;
	uint32_t da = (dst_argb >> 24) & 0xFFu;
	uint32_t inv = 255u - sa;

	uint32_t out_a = sa + (da * inv + 127u) / 255u;

	uint32_t dr = (dst_argb >> 16) & 0xFFu;
	uint32_t dg = (dst_argb >> 8) & 0xFFu;
	uint32_t db = dst_argb & 0xFFu;

	uint32_t sr = (src >> 16) & 0xFFu;
	uint32_t sg = (src >> 8) & 0xFFu;
	uint32_t sb = src & 0xFFu;

	uint32_t out_r = sr + (dr * inv + 127u) / 255u;
	uint32_t out_g = sg + (dg * inv + 127u) / 255u;
	uint32_t out_b = sb + (db * inv + 127u) / 255u;

	*dst = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

static uint32_t aswl_sample_image_bilinear_unpremul(const uint32_t *src_argb,
                                                    int sw,
                                                    int sh,
                                                    double gx,
                                                    double gy)
{
	if (src_argb == NULL || sw <= 0 || sh <= 0)
		return 0;

	if (gx < 0.0)
		gx = 0.0;
	if (gy < 0.0)
		gy = 0.0;

	double max_x = (double)(sw - 1);
	double max_y = (double)(sh - 1);
	if (gx > max_x)
		gx = max_x;
	if (gy > max_y)
		gy = max_y;

	int x0 = (int)gx;
	int y0 = (int)gy;
	int x1 = x0 + 1;
	int y1 = y0 + 1;
	if (x1 >= sw)
		x1 = sw - 1;
	if (y1 >= sh)
		y1 = sh - 1;

	double tx = gx - (double)x0;
	double ty = gy - (double)y0;
	if (tx < 0.0)
		tx = 0.0;
	if (ty < 0.0)
		ty = 0.0;
	if (tx > 1.0)
		tx = 1.0;
	if (ty > 1.0)
		ty = 1.0;

	uint32_t p00 = aswl_premul_argb(src_argb[(size_t)y0 * (size_t)sw + (size_t)x0]);
	uint32_t p10 = aswl_premul_argb(src_argb[(size_t)y0 * (size_t)sw + (size_t)x1]);
	uint32_t p01 = aswl_premul_argb(src_argb[(size_t)y1 * (size_t)sw + (size_t)x0]);
	uint32_t p11 = aswl_premul_argb(src_argb[(size_t)y1 * (size_t)sw + (size_t)x1]);

	double w00 = (1.0 - tx) * (1.0 - ty);
	double w10 = tx * (1.0 - ty);
	double w01 = (1.0 - tx) * ty;
	double w11 = tx * ty;

	double a = (double)((p00 >> 24) & 0xFFu) * w00 +
	           (double)((p10 >> 24) & 0xFFu) * w10 +
	           (double)((p01 >> 24) & 0xFFu) * w01 +
	           (double)((p11 >> 24) & 0xFFu) * w11;
	double r = (double)((p00 >> 16) & 0xFFu) * w00 +
	           (double)((p10 >> 16) & 0xFFu) * w10 +
	           (double)((p01 >> 16) & 0xFFu) * w01 +
	           (double)((p11 >> 16) & 0xFFu) * w11;
	double g = (double)((p00 >> 8) & 0xFFu) * w00 +
	           (double)((p10 >> 8) & 0xFFu) * w10 +
	           (double)((p01 >> 8) & 0xFFu) * w01 +
	           (double)((p11 >> 8) & 0xFFu) * w11;
	double b = (double)(p00 & 0xFFu) * w00 +
	           (double)(p10 & 0xFFu) * w10 +
	           (double)(p01 & 0xFFu) * w01 +
	           (double)(p11 & 0xFFu) * w11;

	uint32_t ia = (uint32_t)(a + 0.5);
	uint32_t ir = (uint32_t)(r + 0.5);
	uint32_t ig = (uint32_t)(g + 0.5);
	uint32_t ib = (uint32_t)(b + 0.5);
	if (ia > 255u)
		ia = 255u;
	if (ir > 255u)
		ir = 255u;
	if (ig > 255u)
		ig = 255u;
	if (ib > 255u)
		ib = 255u;

	uint32_t premul = (ia << 24) | (ir << 16) | (ig << 8) | ib;
	return aswl_unpremul_argb(premul);
}

static void aswl_blend_image_bilinear_argb(uint32_t *dst,
                                           int dst_w,
                                           int dst_h,
                                           int dx,
                                           int dy,
                                           int dw,
                                           int dh,
                                           const uint32_t *src,
                                           int sw,
                                           int sh)
{
	if (dst == NULL || src == NULL)
		return;
	if (dst_w <= 0 || dst_h <= 0 || sw <= 0 || sh <= 0)
		return;
	if (dw <= 0 || dh <= 0)
		return;

	int x0 = 0;
	int y0 = 0;
	int x1 = dw;
	int y1 = dh;
	if (dx < 0)
		x0 = -dx;
	if (dy < 0)
		y0 = -dy;
	if (dx + dw > dst_w)
		x1 = dst_w - dx;
	if (dy + dh > dst_h)
		y1 = dst_h - dy;
	if (x1 <= x0 || y1 <= y0)
		return;

	double sx_scale = 0.0;
	double sy_scale = 0.0;
	if (dw > 1 && sw > 1)
		sx_scale = (double)(sw - 1) / (double)(dw - 1);
	if (dh > 1 && sh > 1)
		sy_scale = (double)(sh - 1) / (double)(dh - 1);

	for (int y = y0; y < y1; y++) {
		double gy = (double)y * sy_scale;
		for (int x = x0; x < x1; x++) {
			double gx = (double)x * sx_scale;
			uint32_t c = aswl_sample_image_bilinear_unpremul(src, sw, sh, gx, gy);
			aswl_blend_pixel_argb(&dst[(size_t)(dy + y) * (size_t)dst_w + (size_t)(dx + x)], c);
		}
	}
}

static double aswl_gradient_t(int type, int x, int y, int w, int h)
{
	if (w <= 1)
		w = 1;
	if (h <= 1)
		h = 1;

	switch (type) {
	case 1:
		type = 6;
		break;
	case 2:
		type = 8;
		break;
	case 4:
		type = 9;
		break;
	default:
		break;
	}

	double fx = (double)x;
	double fy = (double)y;
	double fw = (double)(w - 1);
	double fh = (double)(h - 1);

	switch (type) {
	case 6:
		if (fw <= 0.0 && fh <= 0.0)
			return 0.0;
		if (fw <= 0.0)
			return fy / fh;
		if (fh <= 0.0)
			return fx / fw;
		return 0.5 * ((fx / fw) + (fy / fh));
	case 7:
		if (fw <= 0.0 && fh <= 0.0)
			return 0.0;
		if (fw <= 0.0)
			return (fh - fy) / fh;
		if (fh <= 0.0)
			return fx / fw;
		return 0.5 * ((fx / fw) + ((fh - fy) / fh));
	case 8:
		if (fh <= 0.0)
			return 0.0;
		return fy / fh;
	case 9:
		if (fw <= 0.0)
			return 0.0;
		return fx / fw;
	case 3:
	{
		double mid = fh / 2.0;
		if (mid <= 0.0)
			return 0.0;
		double d = (fy > mid) ? (fy - mid) : (mid - fy);
		double t = 1.0 - (d / mid);
		return t < 0.0 ? 0.0 : t;
	}
	case 5:
	{
		double mid = fw / 2.0;
		if (mid <= 0.0)
			return 0.0;
		double d = (fx > mid) ? (fx - mid) : (mid - fx);
		double t = 1.0 - (d / mid);
		return t < 0.0 ? 0.0 : t;
	}
	default:
		return 0.0;
	}
}

static uint32_t aswl_gradient_sample(const struct aswl_gradient *grad, double t)
{
	if (!aswl_gradient_is_valid(grad))
		return 0;

	if (t <= grad->offsets[0])
		return grad->colors[0];
	if (t >= grad->offsets[grad->count - 1])
		return grad->colors[grad->count - 1];

	for (size_t i = 0; i + 1 < grad->count; i++) {
		double a = grad->offsets[i];
		double b = grad->offsets[i + 1];
		if (t > b)
			continue;

		double span = b - a;
		if (span <= 0.0)
			return grad->colors[i + 1];

		double local = (t - a) / span;
		if (local < 0.0)
			local = 0.0;
		if (local > 1.0)
			local = 1.0;

		uint8_t tt = (uint8_t)(local * 255.0 + 0.5);
		return aswl_color_blend(grad->colors[i], grad->colors[i + 1], tt);
	}

	return grad->colors[grad->count - 1];
}

void view_get_current_size(struct aswl_view *view, int *width, int *height)
{
	int w = 0;
	int h = 0;

	if (view != NULL && view->xwayland_surface != NULL) {
		w = view->xwayland_surface->width;
		h = view->xwayland_surface->height;
	}

	if (view != NULL && view->xdg_surface != NULL) {
		/*
		 * The scene-graph xdg_surface helper positions the node at the top-left
		 * corner of the *window geometry*, so use geometry size as our content
		 * size when possible. This avoids "double framed" looking margins when
		 * the client surface includes extra padding/shadows around the geometry.
		 */
		if (view->xdg_surface->geometry.width > 0)
			w = view->xdg_surface->geometry.width;
		if (view->xdg_surface->geometry.height > 0)
			h = view->xdg_surface->geometry.height;

		if ((w <= 0 || h <= 0) && view->xdg_surface->surface != NULL) {
			w = view->xdg_surface->surface->current.width;
			h = view->xdg_surface->surface->current.height;
		}
	}

	if (width != NULL)
		*width = w;
	if (height != NULL)
		*height = h;
}

void view_get_deco_metrics(struct aswl_view *view, int *border_out, int *title_h_out)
{
	int border = 2;
	/* Classic AfterStep frames are slightly taller than a generic 24px titlebar.
	 * Matching the X11 baseline here is important because Xwayland clients that
	 * use pseudo-transparency (e.g. WinTabs/TermTabs) sample the root pixmap
	 * based on their on-screen position; even a few pixels of offset show up as
	 * large diffs in our screenshot parity checks. */
	int title_h = 28;

	const char *b_env = getenv("ASWLCOMP_DECOR_BORDER");
	if (b_env != NULL && b_env[0] != '\0') {
		char *end = NULL;
		long v = strtol(b_env, &end, 10);
		if (end != b_env && *end == '\0')
			border = (int)v;
	}

	const char *t_env = getenv("ASWLCOMP_DECOR_TITLE");
	if (t_env != NULL && t_env[0] != '\0') {
		char *end = NULL;
		long v = strtol(t_env, &end, 10);
		if (end != t_env && *end == '\0')
			title_h = (int)v;
	}

	border = clamp_int(border, 0, 64);
	title_h = clamp_int(title_h, 0, 128);

	if (view == NULL || view->is_dock) {
		border = 0;
		title_h = 0;
	}

	/* Suite popups/menus draw their own frame and should not get server-side decorations. */
	const char *app_id = view_app_id(view);
	if (app_id != NULL && strcmp(app_id, "afterstep.aswlmenu") == 0) {
		border = 0;
		title_h = 0;
	}
	/*
	 * AfterStep module windows are typically borderless and untitled per the
	 * default style database (Style "ASModule" ... NoTitle, BorderWidth 0,
	 * WindowListSkip, etc.). Match that so X11 modules (e.g. WinTabs/TermTabs)
	 * look like they do under X11 AfterStep.
	 */
	if (app_id != NULL && strcmp(app_id, "ASModule") == 0) {
		/*
		 * WinTabs/TermTabs is an ASModule-class client, but the default style DB
		 * includes a specific override for instance "WinTabs" enabling a normal
		 * titlebar. Under Xwayland we only expose WM_CLASS.class as app_id; use
		 * the instance to preserve the X11 look for TermTabs.
		 */
		const char *instance = NULL;
		if (view != NULL && view->xwayland_surface != NULL)
			instance = view->xwayland_surface->instance;
		if (instance == NULL || strcmp(instance, "WinTabs") != 0) {
			border = 0;
			title_h = 0;
		}
	}

	if (border_out != NULL)
		*border_out = border;
	if (title_h_out != NULL)
		*title_h_out = title_h;
}

void view_get_content_offset(struct aswl_view *view, int *ox, int *oy)
{
	int border = 0;
	int title_h = 0;
	view_get_deco_metrics(view, &border, &title_h);

	if (ox != NULL)
		*ox = border;
	if (oy != NULL)
		*oy = title_h;
}

void view_get_frame_size(struct aswl_view *view, int *width, int *height)
{
	int cw = 0;
	int ch = 0;
	view_get_current_size(view, &cw, &ch);
	if (cw < 1)
		cw = 1;
	if (ch < 1)
		ch = 1;

	int border = 0;
	int title_h = 0;
	view_get_deco_metrics(view, &border, &title_h);

	int fw = cw + 2 * border;
	int fh = ch + title_h + border;

	if (width != NULL)
		*width = fw;
	if (height != NULL)
		*height = fh;
}

bool view_create_frame_scene(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL)
		return false;
	if (view->scene_tree != NULL)
		return true;

	struct aswl_server *server = view->server;

	view->scene_tree = wlr_scene_tree_create(server->xdg_tree);
	if (view->scene_tree == NULL)
		return false;
	view->scene_tree->node.data = view;

	view->content_tree = wlr_scene_tree_create(view->scene_tree);
	if (view->content_tree == NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
		return false;
	}

	float border_color[4];
	aswl_argb_to_premul_f(server->theme.frame_border, border_color);

	view->deco_left = wlr_scene_rect_create(view->scene_tree, 1, 1, border_color);
	view->deco_right = wlr_scene_rect_create(view->scene_tree, 1, 1, border_color);
	view->deco_bottom = wlr_scene_rect_create(view->scene_tree, 1, 1, border_color);
	view->deco_titlebar = wlr_scene_buffer_create(view->scene_tree, NULL);
	if (view->deco_left == NULL || view->deco_right == NULL || view->deco_bottom == NULL || view->deco_titlebar == NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
		view->content_tree = NULL;
		view->deco_left = NULL;
		view->deco_right = NULL;
		view->deco_bottom = NULL;
		view->deco_titlebar = NULL;
		return false;
	}

	view_update_decorations(view);
	return true;
}

static void aswl_deco_icon_destroy(struct aswl_deco_icon *icon)
{
	if (icon == NULL)
		return;
	free(icon->argb);
	*icon = (struct aswl_deco_icon){ 0 };
}

static bool aswl_deco_icon_try_load(struct aswl_deco_icon *icon, const char *const specs[])
{
	if (icon == NULL)
		return false;
	if (icon->argb != NULL && icon->w > 0 && icon->h > 0)
		return true;
	if (specs == NULL)
		return false;

	for (size_t i = 0; specs[i] != NULL; i++) {
		uint32_t *argb = NULL;
		int w = 0;
		int h = 0;
		if (aswl_icon_load_argb(specs[i], &argb, &w, &h) && argb != NULL && w > 0 && h > 0) {
			free(icon->argb);
			icon->argb = argb;
			icon->w = w;
			icon->h = h;
			return true;
		}
		free(argb);
	}

	return false;
}

static void aswl_deco_assets_ensure(struct aswl_server *server)
{
	if (server == NULL || server->deco.loaded)
		return;

	/* Prefer look.DEFAULT title button assets; fall back to existing icons. */
	static const char *const kill_specs[] = { "default-kill-dark", "dots/abi-close", NULL };
	static const char *const shade_specs[] = { "default-shade-dark", NULL };
	static const char *const iconize_specs[] = { "default-iconize-dark", NULL };
	static const char *const menu_specs[] = { "default-menu-light", NULL };
	static const char *const switch_specs[] = { "default-switchwindow-light", NULL };
	static const char *const pin_specs[] = { "default-pin-light", NULL };

	(void)aswl_deco_icon_try_load(&server->deco.btn_kill, kill_specs);
	(void)aswl_deco_icon_try_load(&server->deco.btn_shade, shade_specs);
	(void)aswl_deco_icon_try_load(&server->deco.btn_iconize, iconize_specs);
	(void)aswl_deco_icon_try_load(&server->deco.btn_menu, menu_specs);
	(void)aswl_deco_icon_try_load(&server->deco.btn_switch, switch_specs);
	(void)aswl_deco_icon_try_load(&server->deco.btn_pin, pin_specs);

	server->deco.loaded = true;
}

void aswl_deco_assets_destroy(struct aswl_deco_assets *deco)
{
	if (deco == NULL)
		return;
	aswl_deco_icon_destroy(&deco->btn_switch);
	aswl_deco_icon_destroy(&deco->btn_menu);
	aswl_deco_icon_destroy(&deco->btn_pin);
	aswl_deco_icon_destroy(&deco->btn_iconize);
	aswl_deco_icon_destroy(&deco->btn_shade);
	aswl_deco_icon_destroy(&deco->btn_kill);
	deco->loaded = false;
}

/* Tile (128) or scale (127) an image-backed MyStyle BackPixmap into the
 * titlebar pixel buffer (contiguous frame_w x title_h, premultiplied ARGB).
 * Typical AfterStep titlebar pixmaps are opaque, so premultiply is a no-op
 * for them and correct for any with alpha. */
static bool view_fill_titlebar_backpixmap(uint32_t *pixels, int w, int h,
                                          const char *path, bool scaled)
{
	if (pixels == NULL || path == NULL || path[0] == '\0' || w <= 0 || h <= 0)
		return false;

	uint32_t *img = NULL;
	int iw = 0;
	int ih = 0;
	if (!aswl_icon_load_argb(path, &img, &iw, &ih) || img == NULL || iw <= 0 || ih <= 0) {
		free(img);
		return false;
	}

	for (int y = 0; y < h; y++) {
		int sy = scaled ? (int)((int64_t)y * ih / h) : (y % ih);
		if (sy < 0)
			sy = 0;
		else if (sy >= ih)
			sy = ih - 1;
		const uint32_t *srow = img + (size_t)sy * iw;
		uint32_t *drow = pixels + (size_t)y * w;
		for (int x = 0; x < w; x++) {
			int sx = scaled ? (int)((int64_t)x * iw / w) : (x % iw);
			if (sx < 0)
				sx = 0;
			else if (sx >= iw)
				sx = iw - 1;
			drow[x] = aswl_premul_argb(srow[sx]);
		}
	}
	free(img);
	return true;
}

static struct wlr_buffer *view_render_titlebar_buffer(struct aswl_view *view, int frame_w, int title_h)
{
	if (view == NULL || view->server == NULL || frame_w <= 0 || title_h <= 0)
		return NULL;

	struct aswl_server *server = view->server;

	bool focused = server->focused_view == view;
	uint32_t bg = focused ? server->theme.frame_active_bg : server->theme.frame_inactive_bg;
	uint32_t fg = focused ? server->theme.frame_active_fg : server->theme.frame_inactive_fg;

	uint32_t *pixels = malloc((size_t)frame_w * (size_t)title_h * sizeof(*pixels));
	if (pixels == NULL)
		return NULL;

	int bp_type = focused ? server->theme.frame_active_back_pixmap_type : server->theme.frame_inactive_back_pixmap_type;
	const char *bp_path = focused ? server->theme.frame_active_back_pixmap_path : server->theme.frame_inactive_back_pixmap_path;
	bool bp_filled = false;
	if ((bp_type == 128 || bp_type == 127) && bp_path != NULL)
		bp_filled = view_fill_titlebar_backpixmap(pixels, frame_w, title_h, bp_path, bp_type == 127);

	const struct aswl_gradient *grad = focused ? &server->theme.frame_active_gradient : &server->theme.frame_inactive_gradient;
	if (!bp_filled && aswl_gradient_is_valid(grad)) {
		for (int y = 0; y < title_h; y++) {
			for (int x = 0; x < frame_w; x++) {
				double t = aswl_gradient_t(grad->type, x, y, frame_w, title_h);
				uint32_t c = aswl_gradient_sample(grad, t);
				pixels[(size_t)y * (size_t)frame_w + (size_t)x] = aswl_premul_argb(c);
			}
		}
	} else if (!bp_filled) {
		/* Fallback: subtle vertical gradient. */
		uint32_t grad_top = aswl_color_lighten(bg, 42);
		uint32_t grad_bot = aswl_color_darken(bg, 52);
		for (int y = 0; y < title_h; y++) {
			uint8_t t = 0;
			if (title_h > 1)
				t = (uint8_t)((uint32_t)y * 255u / (uint32_t)(title_h - 1));
			uint32_t c = aswl_premul_argb(aswl_color_blend(grad_top, grad_bot, t));
			for (int x = 0; x < frame_w; x++)
				pixels[(size_t)y * (size_t)frame_w + (size_t)x] = c;
		}
	}

			/* AfterStep-style bevel (fixed colors derived from the base). */
			uint32_t relief_fore = aswl_color_hilite(bg);
			uint32_t relief_back = aswl_color_shadow(bg);

				uint32_t hi_color = relief_fore;
				uint32_t lo_color = relief_back;
				uint32_t hihi_color = aswl_color_hilite(relief_fore);
				uint32_t lolo_color = relief_back;
				uint32_t hilo_color = aswl_color_average(hi_color, lo_color);

			uint32_t hi_premul = aswl_premul_argb(hi_color);
			uint32_t lo_premul = aswl_premul_argb(lo_color);

			/* Top/bottom edges */
			for (int x = 0; x < frame_w; x++) {
				pixels[(size_t)x] = hi_premul;
				size_t pos = (size_t)(title_h - 1) * (size_t)frame_w + (size_t)x;
				pixels[pos] = lo_premul;
			}
			if (frame_w > 1 && title_h > 2) {
				for (int y = 1; y < title_h - 1; y++) {
					size_t lpos = (size_t)y * (size_t)frame_w;
					size_t rpos = lpos + (size_t)(frame_w - 1);
					pixels[lpos] = hi_premul;
					pixels[rpos] = lo_premul;
				}
			}
			if (frame_w > 1 && title_h > 1) {
				pixels[0] = aswl_premul_argb(hihi_color);
				pixels[(size_t)(frame_w - 1)] = aswl_premul_argb(hilo_color);
				size_t bl = (size_t)(title_h - 1) * (size_t)frame_w;
				pixels[bl] = aswl_premul_argb(hilo_color);
				pixels[bl + (size_t)(frame_w - 1)] = aswl_premul_argb(lolo_color);
			}

			aswl_deco_assets_ensure(server);

		int border = 0;
		view_get_deco_metrics(view, &border, NULL);

			/*
			 * Title buttons (AfterStep look.DEFAULT):
			 *   Left:  switchwindow, menu
			 *   Right: iconize, shade, kill
			 */
			/* Keep title buttons compact so small windows can still show the full title. */
			int btn_spacing = 2;
			int btn_outer_pad = 0;
		/*
		 * In classic AfterStep, TitleButtons are small (typically 16x16) and do
		 * not sit inside oversized bevel boxes. Use native asset sizing and keep
		 * a slightly larger hit-box for usability.
		 */
		int icon_box = clamp_int(title_h - 8, 10, 16);
		/* Match classic AfterStep: keep the effective button box at 16x16. */
		int hit_box = clamp_int(icon_box, 0, title_h);
		int btn_y = (title_h - hit_box) / 2;
		if (btn_y < 0)
			btn_y = 0;
		if (btn_y + hit_box > title_h)
			btn_y = title_h - hit_box;

		int left_x = border + btn_outer_pad;
		int right_x = frame_w - border - btn_outer_pad - hit_box;
		if (left_x < 0)
			left_x = 0;
		if (right_x < 0)
			right_x = 0;

		/* Right cluster. */
		int kill_x = right_x;
		int shade_x = kill_x - (hit_box + btn_spacing);
		int iconize_x = shade_x - (hit_box + btn_spacing);

		struct {
			int x;
			const struct aswl_deco_icon *icon;
			char fallback_glyph;
		} right_btns[] = {
			{ .x = iconize_x, .icon = &server->deco.btn_iconize, .fallback_glyph = '_' },
			{ .x = shade_x, .icon = &server->deco.btn_shade, .fallback_glyph = '^' },
			{ .x = kill_x, .icon = &server->deco.btn_kill, .fallback_glyph = 'X' },
		};

		for (size_t i = 0; i < sizeof(right_btns) / sizeof(right_btns[0]); i++) {
			int bx = right_btns[i].x;
			if (right_btns[i].icon->argb != NULL && right_btns[i].icon->w > 0 && right_btns[i].icon->h > 0) {
				int dw = right_btns[i].icon->w;
				int dh = right_btns[i].icon->h;
				if (icon_box > 0 && dw > 0 && dh > 0) {
					if (dw > icon_box || dh > icon_box) {
						double sx = (double)icon_box / (double)dw;
						double sy = (double)icon_box / (double)dh;
						double s = sx < sy ? sx : sy;
						dw = (int)((double)dw * s + 0.5);
						dh = (int)((double)dh * s + 0.5);
						if (dw < 1)
							dw = 1;
						if (dh < 1)
							dh = 1;
					}

					int px = bx + (hit_box - dw) / 2;
					int py = btn_y + (hit_box - dh) / 2;
					aswl_blend_image_bilinear_argb(pixels,
					                               frame_w,
					                               title_h,
					                               px,
					                               py,
					                               dw,
					                               dh,
					                               right_btns[i].icon->argb,
					                               right_btns[i].icon->w,
					                               right_btns[i].icon->h);
				}
			} else {
				int scale = clamp_int(icon_box / 10, 1, 3);
				int gw = aswl_font5x7_glyph_w(scale);
				int gh = aswl_font5x7_glyph_h(scale);
				int gx = bx + (hit_box - gw) / 2;
				int gy = btn_y + (hit_box - gh) / 2;
				aswl_font5x7_draw_glyph(pixels, frame_w, title_h, frame_w, gx, gy, right_btns[i].fallback_glyph, scale, fg);
			}
		}

		/* Hit-test the rightmost (kill) button for close. */
		view->deco_close_x = kill_x;
		view->deco_close_y = btn_y;
		view->deco_close_w = hit_box;
		view->deco_close_h = hit_box;

		/* Title text + left cluster layout. */
		const char *title = view_title(view);
		struct aswl_font *title_font = focused ? &server->deco_font : &server->deco_font_inactive;
		int text_style = focused ? server->theme.frame_active_text_style : server->theme.frame_inactive_text_style;
		int scale = clamp_int((title_h - 8) / 7, 1, 12);
		if (title_font->use_freetype && title_font->base_px > 0)
			scale = 1;
		(void)aswl_font_set_scale(title_font, scale);
		int text_h = aswl_font_height_styled(title_font, text_style);

			int title_w = 0;
			if (title != NULL && title[0] != '\0')
				title_w = aswl_font_text_width_styled(title_font, title, text_style);

			int left_btn_count = 2; /* switch, menu */

			/* Left cluster. */
			struct {
				const struct aswl_deco_icon *icon;
				char fallback_glyph;
			} left_specs[] = {
				{ .icon = &server->deco.btn_switch, .fallback_glyph = 'S' },
				{ .icon = &server->deco.btn_menu, .fallback_glyph = 'M' },
			};

			int drawn_left = 0;
			for (int i = 0; i < left_btn_count; i++) {
				int bx = left_x + drawn_left * (hit_box + btn_spacing);

				if (left_specs[i].icon->argb != NULL && left_specs[i].icon->w > 0 &&
				    left_specs[i].icon->h > 0) {
					int dw = left_specs[i].icon->w;
					int dh = left_specs[i].icon->h;
					if (icon_box > 0 && dw > 0 && dh > 0) {
						if (dw > icon_box || dh > icon_box) {
							double sx = (double)icon_box / (double)dw;
							double sy = (double)icon_box / (double)dh;
						double s = sx < sy ? sx : sy;
						dw = (int)((double)dw * s + 0.5);
						dh = (int)((double)dh * s + 0.5);
						if (dw < 1)
							dw = 1;
						if (dh < 1)
							dh = 1;
					}

					int px = bx + (hit_box - dw) / 2;
					int py = btn_y + (hit_box - dh) / 2;
						aswl_blend_image_bilinear_argb(pixels,
						                               frame_w,
						                               title_h,
						                               px,
						                               py,
						                               dw,
						                               dh,
						                               left_specs[i].icon->argb,
						                               left_specs[i].icon->w,
						                               left_specs[i].icon->h);
					}
				} else {
					int gscale = clamp_int(icon_box / 10, 1, 3);
					int gw = aswl_font5x7_glyph_w(gscale);
					int gh = aswl_font5x7_glyph_h(gscale);
					int gx = bx + (hit_box - gw) / 2;
					int gy = btn_y + (hit_box - gh) / 2;
					aswl_font5x7_draw_glyph(pixels, frame_w, title_h, frame_w, gx, gy, left_specs[i].fallback_glyph, gscale, fg);
				}

				drawn_left++;
			}

		int left_end = left_x;
		if (drawn_left > 0)
			left_end = left_x + drawn_left * hit_box + (drawn_left - 1) * btn_spacing;
		int right_start = iconize_x;
		int tx_pad = 4;
		int text_left = left_end + tx_pad;
		int text_right = right_start - tx_pad;
		int tw = text_right - text_left;
			if (tw > 0 && title != NULL && title[0] != '\0') {
				title_w = aswl_font_text_width_styled(title_font, title, text_style);
				int tx = text_left;
				if (title_w > 0 && title_w < tw)
					tx = text_right - title_w;
				int ty = (title_h - text_h) / 2;
				aswl_font_draw_text_styled(title_font, pixels, frame_w, title_h, frame_w, tx, ty, title, tw, fg, text_style);
			}

		return aswl_pixbuf_buffer_create(pixels, frame_w, title_h);
	}

void view_update_decorations(struct aswl_view *view)
{
	if (view == NULL || view->server == NULL || view->scene_tree == NULL)
		return;

	struct aswl_server *server = view->server;

	int border = 0;
	int title_h = 0;
	view_get_deco_metrics(view, &border, &title_h);

	view->deco_border = border;
	view->deco_title_height = title_h;

	if (view->content_tree != NULL)
		wlr_scene_node_set_position(&view->content_tree->node, border, title_h);

	int frame_w = 0;
	int frame_h = 0;
	view_get_frame_size(view, &frame_w, &frame_h);

	uint32_t border_base = server->theme.frame_border;
		uint32_t border_hilite = aswl_color_hilite(border_base);
		uint32_t border_shadow = aswl_color_shadow(border_base);
	float border_hilite_color[4];
	float border_shadow_color[4];
	aswl_argb_to_premul_f(border_hilite, border_hilite_color);
	aswl_argb_to_premul_f(border_shadow, border_shadow_color);

	if (view->deco_left != NULL) {
		wlr_scene_rect_set_color(view->deco_left, border_hilite_color);
		wlr_scene_rect_set_size(view->deco_left, border, frame_h - title_h);
		wlr_scene_node_set_position(&view->deco_left->node, 0, title_h);
		wlr_scene_node_set_enabled(&view->deco_left->node, border > 0);
		view->deco_left->node.data = view;
	}
	if (view->deco_right != NULL) {
		wlr_scene_rect_set_color(view->deco_right, border_shadow_color);
		wlr_scene_rect_set_size(view->deco_right, border, frame_h - title_h);
		wlr_scene_node_set_position(&view->deco_right->node, frame_w - border, title_h);
		wlr_scene_node_set_enabled(&view->deco_right->node, border > 0);
		view->deco_right->node.data = view;
	}
	if (view->deco_bottom != NULL) {
		wlr_scene_rect_set_color(view->deco_bottom, border_shadow_color);
		wlr_scene_rect_set_size(view->deco_bottom, frame_w, border);
		wlr_scene_node_set_position(&view->deco_bottom->node, 0, frame_h - border);
		wlr_scene_node_set_enabled(&view->deco_bottom->node, border > 0);
		view->deco_bottom->node.data = view;
	}

	if (view->deco_titlebar != NULL) {
		if (view->deco_titlebar_buf != NULL) {
			wlr_buffer_drop(view->deco_titlebar_buf);
			view->deco_titlebar_buf = NULL;
		}
		if (title_h > 0) {
			view->deco_titlebar_buf = view_render_titlebar_buffer(view, frame_w, title_h);
			wlr_scene_buffer_set_buffer(view->deco_titlebar, view->deco_titlebar_buf);
			wlr_scene_node_set_position(&view->deco_titlebar->node, 0, 0);
			wlr_scene_node_set_enabled(&view->deco_titlebar->node, true);
			view->deco_titlebar->node.data = view;
		} else {
			wlr_scene_buffer_set_buffer(view->deco_titlebar, NULL);
			wlr_scene_node_set_enabled(&view->deco_titlebar->node, false);
		}
	}
}

static void handle_xdg_deco_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_xdg_deco *xd = wl_container_of(listener, xd, destroy);
	if (xd == NULL)
		return;

	wl_list_remove(&xd->request_mode.link);
	wl_list_remove(&xd->destroy.link);
	free(xd);
}

static void handle_xdg_deco_request_mode(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_xdg_deco *xd = wl_container_of(listener, xd, request_mode);
	if (xd == NULL || xd->deco == NULL)
		return;

	/* Prefer server-side decorations so clients don't draw their own CSD frame. */
	wlr_xdg_toplevel_decoration_v1_set_mode(xd->deco, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void handle_new_xdg_decoration(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_xdg_decoration);
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	if (server == NULL || deco == NULL)
		return;

	struct aswl_xdg_deco *xd = calloc(1, sizeof(*xd));
	if (xd == NULL)
		return;

	xd->server = server;
	xd->deco = deco;
	deco->data = xd;

	xd->request_mode.notify = handle_xdg_deco_request_mode;
	wl_signal_add(&deco->events.request_mode, &xd->request_mode);

	xd->destroy.notify = handle_xdg_deco_destroy;
	wl_signal_add(&deco->events.destroy, &xd->destroy);

	/* Enforce server-side mode on creation. */
	wlr_xdg_toplevel_decoration_v1_set_mode(deco, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);

	struct wlr_xdg_surface *xdg_surface = deco->toplevel != NULL ? deco->toplevel->base : NULL;
	struct aswl_view *view = xdg_surface != NULL ? xdg_surface->data : NULL;
	fprintf(stderr, "aswlcomp: xdg-decoration: server-side for app_id=%s title=%s\n", view_app_id(view), view_title(view));
}
