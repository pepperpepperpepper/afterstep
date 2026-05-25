#include "aswlfont.h"
#include "aswltheme.h"

#include "aswllock_internal.h"

static uint32_t as_premul_argb(uint32_t argb)
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

static void as_buffer_fill_rect(struct as_buffer *buf, int x, int y, int w, int h, uint32_t argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	if (w <= 0 || h <= 0)
		return;

	int x1 = x;
	int y1 = y;
	int x2 = x + w;
	int y2 = y + h;

	if (x1 < 0)
		x1 = 0;
	if (y1 < 0)
		y1 = 0;
	if (x2 > buf->width)
		x2 = buf->width;
	if (y2 > buf->height)
		y2 = buf->height;
	if (x2 <= x1 || y2 <= y1)
		return;

	uint32_t premul = as_premul_argb(argb);
	for (int yy = y1; yy < y2; yy++) {
		uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)yy * (size_t)buf->stride);
		for (int xx = x1; xx < x2; xx++)
			row[xx] = premul;
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

static void as_buffer_fill_gradient(struct as_buffer *buf, const struct aswl_gradient *grad, uint32_t fallback_argb)
{
	if (buf == NULL || buf->data == NULL)
		return;

	if (!aswl_gradient_is_valid(grad)) {
		as_buffer_fill_rect(buf, 0, 0, buf->width, buf->height, fallback_argb);
		return;
	}

	for (int y = 0; y < buf->height; y++) {
		uint32_t *row = (uint32_t *)((uint8_t *)buf->data + (size_t)y * (size_t)buf->stride);
		for (int x = 0; x < buf->width; x++) {
			double t = aswl_gradient_t(grad->type, x, y, buf->width, buf->height);
			uint32_t c = aswl_gradient_sample(grad, t) | 0xFF000000u;
			row[x] = as_premul_argb(c);
		}
	}
}

static void draw_lock_ui(const struct aswl_theme *theme, struct aswl_font *font, struct as_buffer *buf, int scale)
{
	if (theme == NULL || font == NULL || buf == NULL || buf->data == NULL)
		return;

	uint32_t desk_bg = theme->desk_bg | 0xFF000000u;
	as_buffer_fill_gradient(buf, &theme->desk_gradient, desk_bg);

	if (scale <= 0)
		scale = 1;
	int border = 2 * scale;
	int pad = 12 * scale;
	int header_h = 32 * scale;

	int panel_w = 540 * scale;
	int panel_h = 140 * scale;
	if (panel_w > buf->width - 2 * pad)
		panel_w = buf->width - 2 * pad;
	if (panel_w < 200 * scale)
		panel_w = buf->width;
	if (panel_h > buf->height - 2 * pad)
		panel_h = buf->height - 2 * pad;
	if (panel_h < 80 * scale)
		panel_h = buf->height;

	int panel_x = (buf->width - panel_w) / 2;
	int panel_y = (buf->height - panel_h) / 2;

	uint32_t border_c = theme->menu_border | 0xFF000000u;
	uint32_t bg_c = theme->menu_bg | 0xFF000000u;
	uint32_t header_bg = theme->menu_header_bg | 0xFF000000u;
	uint32_t header_fg = theme->menu_header_fg | 0xFF000000u;
	uint32_t body_fg = theme->menu_item_fg | 0xFF000000u;

	/* Border */
	as_buffer_fill_rect(buf, panel_x, panel_y, panel_w, border, border_c);
	as_buffer_fill_rect(buf, panel_x, panel_y + panel_h - border, panel_w, border, border_c);
	as_buffer_fill_rect(buf, panel_x, panel_y, border, panel_h, border_c);
	as_buffer_fill_rect(buf, panel_x + panel_w - border, panel_y, border, panel_h, border_c);

	/* Body and header */
	as_buffer_fill_rect(buf, panel_x + border, panel_y + border, panel_w - 2 * border, panel_h - 2 * border, bg_c);
	as_buffer_fill_rect(buf, panel_x + border, panel_y + border, panel_w - 2 * border, header_h, header_bg);

	int text_x = panel_x + border + pad;
	int y_title = panel_y + border + (header_h - aswl_font_height(font)) / 2;
	int body_y = panel_y + border + header_h + pad;

	aswl_font_draw_text(font,
	                   (uint32_t *)buf->data,
	                   buf->width,
	                   buf->height,
	                   buf->stride / 4,
	                   text_x,
	                   y_title,
	                   "AfterStep Locked",
	                   panel_w - 2 * (border + pad),
	                   header_fg);

	const char *hint = "Press Enter to unlock (demo)";
	aswl_font_draw_text(font,
	                   (uint32_t *)buf->data,
	                   buf->width,
	                   buf->height,
	                   buf->stride / 4,
	                   text_x,
	                   body_y,
	                   hint,
	                   panel_w - 2 * (border + pad),
	                   body_fg);
}

void aswllock_draw_ui(const struct aswl_theme *theme, struct aswl_font *font, struct as_buffer *buf, int scale)
{
	draw_lock_ui(theme, font, buf, scale);
}
