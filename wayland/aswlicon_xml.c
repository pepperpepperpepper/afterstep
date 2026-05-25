#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlicon_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool aswl_read_file(const char *path, char **out, size_t *out_len)
{
	if (out != NULL)
		*out = NULL;
	if (out_len != NULL)
		*out_len = 0;
	if (path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return false;

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return false;
	}
	long n = ftell(fp);
	if (n < 0 || n > 8 * 1024 * 1024) {
		fclose(fp);
		return false;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return false;
	}

	char *buf = malloc((size_t)n + 1);
	if (buf == NULL) {
		fclose(fp);
		return false;
	}
	size_t got = fread(buf, 1, (size_t)n, fp);
	fclose(fp);
	if (got != (size_t)n) {
		free(buf);
		return false;
	}
	buf[n] = '\0';

	if (out != NULL)
		*out = buf;
	else
		free(buf);
	if (out_len != NULL)
		*out_len = (size_t)n;
	return out == NULL || *out != NULL;
}

static bool aswl_xml_attr_strdup(const char *tag, const char *attr, char **out_val)
{
	if (out_val != NULL)
		*out_val = NULL;
	if (tag == NULL || attr == NULL || out_val == NULL)
		return false;

	const char *end = strchr(tag, '>');
	if (end == NULL)
		end = tag + strlen(tag);

	const char *p = tag;
	while (p < end) {
		while (p < end && (isspace((unsigned char)*p) || *p == '<' || *p == '/'))
			p++;
		const char *key = p;
		while (p < end && (isalnum((unsigned char)*p) || *p == '_' || *p == '-'))
			p++;
		size_t keylen = (size_t)(p - key);
		if (keylen == 0) {
			p++;
			continue;
		}

		while (p < end && isspace((unsigned char)*p))
			p++;
		if (p >= end || *p != '=') {
			while (p < end && !isspace((unsigned char)*p) && *p != '>')
				p++;
			continue;
		}

		p++; /* '=' */
		while (p < end && isspace((unsigned char)*p))
			p++;
		if (p >= end)
			break;

		char quote = 0;
		if (*p == '"' || *p == '\'')
			quote = *p++;
		const char *val = p;
		if (quote != 0) {
			while (p < end && *p != quote)
				p++;
		} else {
			while (p < end && !isspace((unsigned char)*p) && *p != '>')
				p++;
		}
		size_t vallen = (size_t)(p - val);
		if (quote != 0 && p < end && *p == quote)
			p++;

		if (strlen(attr) == keylen && strncmp(key, attr, keylen) == 0) {
			*out_val = strndup(val, vallen);
			return *out_val != NULL;
		}
	}

	return false;
}

static bool aswl_xml_attr_int(const char *tag, const char *attr, int *out)
{
	if (out != NULL)
		*out = 0;
	char *s = NULL;
	if (!aswl_xml_attr_strdup(tag, attr, &s))
		return false;

	char *end = NULL;
	errno = 0;
	long v = strtol(s, &end, 10);
	while (end != NULL && isspace((unsigned char)*end))
		end++;
	bool ok = (errno == 0 && end != NULL && end != s && *end == '\0');
	free(s);
	if (!ok)
		return false;
	if (out != NULL)
		*out = (int)v;
	return true;
}

static bool aswl_str_starts_with_tag(const char *p, const char *tag)
{
	if (p == NULL || tag == NULL)
		return false;
	size_t n = strlen(tag);
	if (strncmp(p, tag, n) != 0)
		return false;
	char c = p[n];
	return c == '\0' || c == '>' || c == '/' || isspace((unsigned char)c);
}

static uint32_t aswl_blend_over(uint32_t dst, uint32_t src)
{
	uint32_t sa = (src >> 24) & 0xFFu;
	if (sa == 0)
		return dst;
	if (sa == 255)
		return src;

	uint32_t da = (dst >> 24) & 0xFFu;
	uint32_t inv = 255u - sa;

	uint32_t out_a = sa + (da * inv + 127u) / 255u;
	if (out_a == 0)
		return 0;

	uint32_t sr = (src >> 16) & 0xFFu;
	uint32_t sg = (src >> 8) & 0xFFu;
	uint32_t sb = src & 0xFFu;

	uint32_t dr = (dst >> 16) & 0xFFu;
	uint32_t dg = (dst >> 8) & 0xFFu;
	uint32_t db = dst & 0xFFu;

	uint32_t out_prem_r = sr * sa + ((dr * da) * inv + 127u) / 255u;
	uint32_t out_prem_g = sg * sa + ((dg * da) * inv + 127u) / 255u;
	uint32_t out_prem_b = sb * sa + ((db * da) * inv + 127u) / 255u;

	uint32_t out_r = (out_prem_r + out_a / 2u) / out_a;
	uint32_t out_g = (out_prem_g + out_a / 2u) / out_a;
	uint32_t out_b = (out_prem_b + out_a / 2u) / out_a;

	if (out_r > 255)
		out_r = 255;
	if (out_g > 255)
		out_g = 255;
	if (out_b > 255)
		out_b = 255;

	return (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

bool aswl_icon_load_composite_list(const char *spec, int depth, uint32_t **out_argb, int *out_w, int *out_h)
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

	char *dup = strdup(spec);
	if (dup == NULL)
		return false;

	struct aswl_composite_layer {
		uint32_t *pix;
		int w;
		int h;
	};

	struct aswl_composite_layer *layers = NULL;
	size_t count = 0;
	size_t cap = 0;
	bool ok = true;

	char *p = dup;
	while (p != NULL && *p != '\0') {
		char *comma = strchr(p, ',');
		if (comma != NULL)
			*comma = '\0';

		char *tok = aswl_lstrip_ws(p);
		aswl_rstrip_ws(tok);

		if (tok[0] != '\0' && strcmp(tok, "-") != 0) {
			uint32_t *pix = NULL;
			int w = 0;
			int h = 0;
			if (aswl_icon_load_argb_rec(tok, depth + 1, &pix, &w, &h) && pix != NULL && w > 0 && h > 0) {
				if (count == cap) {
					size_t ncap = cap == 0 ? 4 : cap * 2;
					struct aswl_composite_layer *nlayers = realloc(layers, ncap * sizeof(*nlayers));
					if (nlayers == NULL) {
						free(pix);
						ok = false;
						break;
					}
					layers = nlayers;
					cap = ncap;
				}

				layers[count] = (struct aswl_composite_layer){
					.pix = pix,
					.w = w,
					.h = h,
				};
				count++;
			} else {
				free(pix);
			}
		}

		p = comma != NULL ? (comma + 1) : NULL;
	}

	free(dup);

	if (!ok || count == 0 || layers == NULL) {
		if (layers != NULL) {
			for (size_t i = 0; i < count; i++)
				free(layers[i].pix);
		}
		free(layers);
		return false;
	}

	int cw = 0;
	int ch = 0;
	for (size_t i = 0; i < count; i++) {
		if (layers[i].w > cw)
			cw = layers[i].w;
		if (layers[i].h > ch)
			ch = layers[i].h;
	}

	if (cw <= 0 || ch <= 0 || cw > 4096 || ch > 4096) {
		for (size_t i = 0; i < count; i++)
			free(layers[i].pix);
		free(layers);
		return false;
	}

	uint32_t *canvas = calloc((size_t)cw * (size_t)ch, sizeof(*canvas));
	if (canvas == NULL) {
		for (size_t i = 0; i < count; i++)
			free(layers[i].pix);
		free(layers);
		return false;
	}

	for (size_t i = 0; i < count; i++) {
		int ox = (cw - layers[i].w) / 2;
		int oy = (ch - layers[i].h) / 2;

		for (int y = 0; y < layers[i].h; y++) {
			int dy = oy + y;
			if (dy < 0 || dy >= ch)
				continue;
			for (int x = 0; x < layers[i].w; x++) {
				int dx = ox + x;
				if (dx < 0 || dx >= cw)
					continue;
				size_t di = (size_t)dy * (size_t)cw + (size_t)dx;
				size_t si = (size_t)y * (size_t)layers[i].w + (size_t)x;
				canvas[di] = aswl_blend_over(canvas[di], layers[i].pix[si]);
			}
		}
	}

	for (size_t i = 0; i < count; i++)
		free(layers[i].pix);
	free(layers);

	if (out_argb != NULL)
		*out_argb = canvas;
	else
		free(canvas);
	if (out_w != NULL)
		*out_w = cw;
	if (out_h != NULL)
		*out_h = ch;
	return out_argb == NULL || *out_argb != NULL;
}

bool aswl_icon_load_xml(const char *xml_path, int depth, uint32_t **out_argb, int *out_w, int *out_h)
{
	if (out_argb != NULL)
		*out_argb = NULL;
	if (out_w != NULL)
		*out_w = 0;
	if (out_h != NULL)
		*out_h = 0;

	if (xml_path == NULL || xml_path[0] == '\0')
		return false;

	char *text = NULL;
	size_t text_len = 0;
	if (!aswl_read_file(xml_path, &text, &text_len))
		return false;

	const char *p = text;
	while (*p != '\0' && isspace((unsigned char)*p))
		p++;
	if (*p != '<') {
		free(text);
		return false;
	}

	struct aswl_icon_layer *layers = NULL;
	size_t layer_count = 0;
	size_t layer_cap = 0;
	uint32_t **pix = NULL;
	int *sw = NULL;
	int *sh = NULL;
	int *dw = NULL;
	int *dh = NULL;
	uint32_t *canvas = NULL;

	struct aswl_scale_ctx stack[16];
	size_t stack_len = 0;

	const char *cur = text;
	while ((cur = strchr(cur, '<')) != NULL) {
		if (aswl_str_starts_with_tag(cur, "</scale")) {
			if (stack_len > 0)
				stack_len--;
			cur++;
			continue;
		}

		if (aswl_str_starts_with_tag(cur, "<scale")) {
			if (stack_len < sizeof(stack) / sizeof(stack[0])) {
				struct aswl_scale_ctx ctx = { 0 };
				ctx.w = -1;
				ctx.h = -1;
				(void)aswl_xml_attr_int(cur, "x", &ctx.x);
				(void)aswl_xml_attr_int(cur, "y", &ctx.y);
				(void)aswl_xml_attr_int(cur, "width", &ctx.w);
				(void)aswl_xml_attr_int(cur, "height", &ctx.h);
				stack[stack_len++] = ctx;
			}
			cur++;
			continue;
		}

		if (aswl_str_starts_with_tag(cur, "<img")) {
			char *src = NULL;
			if (aswl_xml_attr_strdup(cur, "src", &src) && src != NULL && src[0] != '\0') {
				int x = 0;
				int y = 0;
				int w = -1;
				int h = -1;
				(void)aswl_xml_attr_int(cur, "x", &x);
				(void)aswl_xml_attr_int(cur, "y", &y);
				(void)aswl_xml_attr_int(cur, "width", &w);
				(void)aswl_xml_attr_int(cur, "height", &h);

				for (size_t i = 0; i < stack_len; i++) {
					x += stack[i].x;
					y += stack[i].y;
				}
				if (stack_len > 0) {
					int scale_w = stack[stack_len - 1].w;
					int scale_h = stack[stack_len - 1].h;
					if (w <= 0 && scale_w > 0)
						w = scale_w;
					if (h <= 0 && scale_h > 0)
						h = scale_h;
				}

				if (layer_count == layer_cap) {
					size_t next = layer_cap == 0 ? 8 : layer_cap * 2;
					struct aswl_icon_layer *tmp = realloc(layers, next * sizeof(*layers));
					if (tmp == NULL) {
						free(src);
						goto fail;
					}
					layers = tmp;
					layer_cap = next;
				}

				layers[layer_count++] = (struct aswl_icon_layer){
					.src = src,
					.x = x,
					.y = y,
					.w = w,
					.h = h,
				};
			} else {
				free(src);
			}
			cur++;
			continue;
		}

		cur++;
	}

	if (layer_count == 0)
		goto fail;

	/* Load layers and compute bounds. */
	pix = calloc(layer_count, sizeof(*pix));
	sw = calloc(layer_count, sizeof(*sw));
	sh = calloc(layer_count, sizeof(*sh));
	dw = calloc(layer_count, sizeof(*dw));
	dh = calloc(layer_count, sizeof(*dh));
	if (pix == NULL || sw == NULL || sh == NULL || dw == NULL || dh == NULL)
		goto fail;

	int min_x = 0;
	int min_y = 0;
	int max_x = 0;
	int max_y = 0;
	bool any_loaded = false;

	for (size_t i = 0; i < layer_count; i++) {
		if (!aswl_icon_load_argb_rec(layers[i].src, depth + 1, &pix[i], &sw[i], &sh[i]))
			continue;
		if (sw[i] <= 0 || sh[i] <= 0)
			continue;

		dw[i] = layers[i].w > 0 ? layers[i].w : sw[i];
		dh[i] = layers[i].h > 0 ? layers[i].h : sh[i];
		if (dw[i] <= 0)
			dw[i] = sw[i];
		if (dh[i] <= 0)
			dh[i] = sh[i];
		if (dw[i] <= 0 || dh[i] <= 0)
			continue;

		int lx = layers[i].x;
		int ly = layers[i].y;
		int rx = lx + dw[i];
		int by = ly + dh[i];
		if (!any_loaded) {
			min_x = lx;
			min_y = ly;
			max_x = rx;
			max_y = by;
			any_loaded = true;
		} else {
			if (lx < min_x)
				min_x = lx;
			if (ly < min_y)
				min_y = ly;
			if (rx > max_x)
				max_x = rx;
			if (by > max_y)
				max_y = by;
		}
	}

	if (!any_loaded)
		goto fail;

	int canvas_w = max_x - min_x;
	int canvas_h = max_y - min_y;
	if (canvas_w <= 0 || canvas_h <= 0 || canvas_w > 2048 || canvas_h > 2048)
		goto fail;

	canvas = calloc((size_t)canvas_w * (size_t)canvas_h, sizeof(*canvas));
	if (canvas == NULL)
		goto fail;

	for (size_t i = 0; i < layer_count; i++) {
		if (pix[i] == NULL || sw[i] <= 0 || sh[i] <= 0)
			continue;

		int lx = layers[i].x - min_x;
		int ly = layers[i].y - min_y;

		for (int y = 0; y < dh[i]; y++) {
			int dy = ly + y;
			if (dy < 0 || dy >= canvas_h)
				continue;
			int sy = (int)((int64_t)y * sh[i] / dh[i]);
			if (sy < 0)
				sy = 0;
			if (sy >= sh[i])
				sy = sh[i] - 1;
			for (int x = 0; x < dw[i]; x++) {
				int dx = lx + x;
				if (dx < 0 || dx >= canvas_w)
					continue;
				int sx = (int)((int64_t)x * sw[i] / dw[i]);
				if (sx < 0)
					sx = 0;
				if (sx >= sw[i])
					sx = sw[i] - 1;
				size_t dpos = (size_t)dy * (size_t)canvas_w + (size_t)dx;
				size_t spos = (size_t)sy * (size_t)sw[i] + (size_t)sx;
				canvas[dpos] = aswl_blend_over(canvas[dpos], pix[i][spos]);
			}
		}
	}

	for (size_t i = 0; i < layer_count; i++)
		free(pix[i]);
	free(pix);
	pix = NULL;
	free(sw);
	sw = NULL;
	free(sh);
	sh = NULL;
	free(dw);
	dw = NULL;
	free(dh);
	dh = NULL;

	for (size_t i = 0; i < layer_count; i++)
		free(layers[i].src);
	free(layers);
	free(text);

	if (out_argb != NULL)
		*out_argb = canvas;
	else
		free(canvas);
	canvas = NULL;
	if (out_w != NULL)
		*out_w = canvas_w;
	if (out_h != NULL)
		*out_h = canvas_h;
	return out_argb == NULL || *out_argb != NULL;

fail:
	if (canvas != NULL)
		free(canvas);
	if (pix != NULL) {
		for (size_t i = 0; i < layer_count; i++)
			free(pix[i]);
		free(pix);
	}
	free(sw);
	free(sh);
	free(dw);
	free(dh);
	if (layers != NULL) {
		for (size_t i = 0; i < layer_count; i++)
			free(layers[i].src);
		free(layers);
	}
	free(text);
	return false;
}
