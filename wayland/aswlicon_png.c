#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "aswlicon_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_LIBPNG
#include <png.h>
#endif

#ifdef HAVE_LIBPNG
static void aswl_png_error(png_structp png, png_const_charp msg)
{
	(void)msg;
	png_longjmp(png, 1);
}

static void aswl_png_warning(png_structp png, png_const_charp msg)
{
	(void)png;
	(void)msg;
}

bool aswl_load_png_argb(const char *path, uint32_t **out_argb, int *out_w, int *out_h)
{
	if (out_argb != NULL)
		*out_argb = NULL;
	if (out_w != NULL)
		*out_w = 0;
	if (out_h != NULL)
		*out_h = 0;

	if (path == NULL || path[0] == '\0')
		return false;

	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return false;

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, aswl_png_error, aswl_png_warning);
	if (png == NULL) {
		fclose(fp);
		return false;
	}

	png_infop info = png_create_info_struct(png);
	if (info == NULL) {
		png_destroy_read_struct(&png, NULL, NULL);
		fclose(fp);
		return false;
	}

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_read_struct(&png, &info, NULL);
		fclose(fp);
		return false;
	}

	png_init_io(png, fp);
	png_read_info(png, info);

	png_uint_32 w = png_get_image_width(png, info);
	png_uint_32 h = png_get_image_height(png, info);
	int color_type = png_get_color_type(png, info);
	int bit_depth = png_get_bit_depth(png, info);

	if (w == 0 || h == 0) {
		png_destroy_read_struct(&png, &info, NULL);
		fclose(fp);
		return false;
	}
	if (w > 4096 || h > 4096) {
		png_destroy_read_struct(&png, &info, NULL);
		fclose(fp);
		return false;
	}

	if (bit_depth == 16)
		png_set_strip_16(png);
	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png);
	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);
	if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

	png_read_update_info(png, info);

	png_size_t rowbytes = png_get_rowbytes(png, info);
	if (rowbytes == 0 || rowbytes / 4 != w) {
		png_destroy_read_struct(&png, &info, NULL);
		fclose(fp);
		return false;
	}

	uint8_t *rgba = malloc((size_t)rowbytes * (size_t)h);
	png_bytep *rows = malloc(sizeof(*rows) * (size_t)h);
	uint32_t *argb = NULL;
	if (rgba == NULL || rows == NULL)
		goto fail;

	for (png_uint_32 y = 0; y < h; y++)
		rows[y] = (png_bytep)(rgba + (size_t)y * (size_t)rowbytes);

	png_read_image(png, rows);
	png_read_end(png, NULL);

	argb = malloc((size_t)w * (size_t)h * sizeof(*argb));
	if (argb == NULL)
		goto fail;

	for (png_uint_32 y = 0; y < h; y++) {
		const uint8_t *src = rgba + (size_t)y * (size_t)rowbytes;
		for (png_uint_32 x = 0; x < w; x++) {
			uint8_t r = src[x * 4 + 0];
			uint8_t g = src[x * 4 + 1];
			uint8_t b = src[x * 4 + 2];
			uint8_t a = src[x * 4 + 3];
			argb[y * (size_t)w + x] = ((uint32_t)a << 24) |
			                         ((uint32_t)r << 16) |
			                         ((uint32_t)g << 8) |
			                         (uint32_t)b;
		}
	}

	free(rgba);
	free(rows);
	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);

	if (out_argb != NULL)
		*out_argb = argb;
	else
		free(argb);
	if (out_w != NULL)
		*out_w = (int)w;
	if (out_h != NULL)
		*out_h = (int)h;
	return out_argb == NULL || *out_argb != NULL;

fail:
	free(argb);
	free(rgba);
	free(rows);
	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);
	return false;
}
#endif
