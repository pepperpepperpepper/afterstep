#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

#include "afterimage.h"

extern Bool afterimage_uses_xrender(void);

extern void draw_text_xrender(ASVisual *asv, const void *text, ASFont *font,
                              ASTextAttributes *attr, int length, int xrender_op,
                              unsigned long xrender_src, unsigned long xrender_dst,
                              int xrender_xSrc, int xrender_ySrc, int xrender_xDst,
                              int xrender_yDst);

static void usage(const char *argv0)
{
	fprintf(stderr,
	        "Usage: %s [--font PATH] [--text TEXT] [--size N]\n"
	        "\n"
	        "Environment:\n"
	        "  AS_XRENDER_TEST_FONT  Default font path\n",
	        argv0);
}

int main(int argc, char **argv)
{
	const char *font_path = NULL;
	const char *text = "AfterStep XRender smoke";
	int size = 24;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--font") == 0 && i + 1 < argc) {
			font_path = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--text") == 0 && i + 1 < argc) {
			text = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
			size = atoi(argv[++i]);
			continue;
		}

		fprintf(stderr, "Unknown arg: %s\n", argv[i]);
		usage(argv[0]);
		return 2;
	}

	if (font_path == NULL) {
		font_path = getenv("AS_XRENDER_TEST_FONT");
	}
	if (font_path == NULL) {
		font_path = "libAfterImage/apps/test.ttf";
	}

	if (!afterimage_uses_xrender()) {
		fprintf(stderr, "libAfterImage was built without HAVE_XRENDER.\n");
		return 2;
	}

	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "XOpenDisplay failed (DISPLAY not set?).\n");
		return 2;
	}

	int render_event_base = 0, render_error_base = 0;
	if (!XRenderQueryExtension(dpy, &render_event_base, &render_error_base)) {
		fprintf(stderr, "X Render extension not available on this X server.\n");
		XCloseDisplay(dpy);
		return 2;
	}

	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);
	int depth = DefaultDepth(dpy, screen);

	ASVisual *asv = create_asvisual(dpy, screen, depth, NULL);
	if (asv == NULL) {
		fprintf(stderr, "create_asvisual failed.\n");
		XCloseDisplay(dpy);
		return 1;
	}

	struct ASFontManager *fontman = create_font_manager(dpy, NULL, NULL);
	if (fontman == NULL) {
		fprintf(stderr, "create_font_manager failed.\n");
		destroy_asvisual(asv, False);
		XCloseDisplay(dpy);
		return 1;
	}

	struct ASFont *font = get_asfont(fontman, font_path, 0, size, ASF_GuessWho);
	if (font == NULL) {
		fprintf(stderr, "Failed to load font: %s\n", font_path);
		destroy_font_manager(fontman, False);
		destroy_asvisual(asv, False);
		XCloseDisplay(dpy);
		return 1;
	}

	const int w = 640;
	const int h = 200;
	Pixmap pm = XCreatePixmap(dpy, root, (unsigned int)w, (unsigned int)h,
	                          (unsigned int)depth);
	if (pm == None) {
		fprintf(stderr, "XCreatePixmap failed.\n");
		release_font(font);
		destroy_font_manager(fontman, False);
		destroy_asvisual(asv, False);
		XCloseDisplay(dpy);
		return 1;
	}

	GC gc = XCreateGC(dpy, pm, 0, NULL);
	if (gc == None) {
		fprintf(stderr, "XCreateGC failed.\n");
		XFreePixmap(dpy, pm);
		release_font(font);
		destroy_font_manager(fontman, False);
		destroy_asvisual(asv, False);
		XCloseDisplay(dpy);
		return 1;
	}

	unsigned long black = BlackPixel(dpy, screen);
	XSetForeground(dpy, gc, black);
	XFillRectangle(dpy, pm, gc, 0, 0, (unsigned int)w, (unsigned int)h);

	XRenderPictFormat *dst_fmt =
	    XRenderFindVisualFormat(dpy, DefaultVisual(dpy, screen));
	if (dst_fmt == NULL) {
		fprintf(stderr, "XRenderFindVisualFormat failed.\n");
		XFreeGC(dpy, gc);
		XFreePixmap(dpy, pm);
		release_font(font);
		destroy_font_manager(fontman, False);
		destroy_asvisual(asv, False);
		XCloseDisplay(dpy);
		return 1;
	}

	Picture dst_pic = XRenderCreatePicture(dpy, pm, dst_fmt, 0, NULL);
	XRenderColor fg = {0xffff, 0xffff, 0xffff, 0xffff};
	Picture src_pic = XRenderCreateSolidFill(dpy, &fg);

	ASTextAttributes attr =
	    {ASTA_VERSION_INTERNAL, 0, AST_Plain, ASCT_Char, 8, 0, NULL, 0,
	     ARGB32_White, 0};
	draw_text_xrender(asv, text, font, &attr, 0, PictOpOver,
	                  (unsigned long)src_pic, (unsigned long)dst_pic, 0, 0, 20,
	                  60);
	XSync(dpy, False);

	XImage *img =
	    XGetImage(dpy, pm, 0, 0, (unsigned int)w, (unsigned int)h, AllPlanes,
	              ZPixmap);
	if (img == NULL) {
		fprintf(stderr, "XGetImage failed.\n");
		XRenderFreePicture(dpy, src_pic);
		XRenderFreePicture(dpy, dst_pic);
		XFreeGC(dpy, gc);
		XFreePixmap(dpy, pm);
		release_font(font);
		destroy_font_manager(fontman, False);
		destroy_asvisual(asv, False);
		XCloseDisplay(dpy);
		return 1;
	}

	int non_black = 0;
	for (int yy = 0; yy < h && non_black < 10; yy++) {
		for (int xx = 0; xx < w && non_black < 10; xx++) {
			unsigned long px = XGetPixel(img, xx, yy);
			if (px != black) {
				non_black++;
			}
		}
	}
	XDestroyImage(img);

	XRenderFreePicture(dpy, src_pic);
	XRenderFreePicture(dpy, dst_pic);
	XFreeGC(dpy, gc);
	XFreePixmap(dpy, pm);

	release_font(font);
	destroy_font_manager(fontman, False);
	destroy_asvisual(asv, False);
	XCloseDisplay(dpy);

	if (non_black == 0) {
		fprintf(stderr, "No pixels changed after XRender draw (text missing?).\n");
		return 1;
	}

	printf("OK\n");
	return 0;
}
