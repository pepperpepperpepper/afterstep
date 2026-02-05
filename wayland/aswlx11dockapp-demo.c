#define _POSIX_C_SOURCE 200809L

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000u));
}

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--size N] [--title TITLE] [--seconds N]\n", prog);
}

int main(int argc, char **argv)
{
	int size = 96;
	const char *title = "aswl dockapp demo";
	int seconds = 60;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
			size = atoi(argv[++i]);
			continue;
		}
		if (strcmp(argv[i], "--title") == 0 && i + 1 < argc) {
			title = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
			seconds = atoi(argv[++i]);
			continue;
		}
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			usage(argv[0]);
			return 0;
		}

		fprintf(stderr, "%s: unknown arg: %s\n", argv[0], argv[i]);
		usage(argv[0]);
		return 2;
	}

	if (size < 16)
		size = 16;
	if (size > 512)
		size = 512;
	if (seconds < 1)
		seconds = 1;

	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "%s: XOpenDisplay failed (DISPLAY?)\n", argv[0]);
		return 1;
	}

	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);

	XSetWindowAttributes attrs;
	memset(&attrs, 0, sizeof(attrs));
	attrs.background_pixel = BlackPixel(dpy, screen);
	attrs.border_pixel = BlackPixel(dpy, screen);
	attrs.event_mask = ExposureMask | StructureNotifyMask;

	Window win = XCreateWindow(dpy,
	                           root,
	                           0,
	                           0,
	                           (unsigned)size,
	                           (unsigned)size,
	                           0,
	                           CopyFromParent,
	                           InputOutput,
	                           CopyFromParent,
	                           CWBackPixel | CWBorderPixel | CWEventMask,
	                           &attrs);

	Atom atom_wm_name = XInternAtom(dpy, "WM_NAME", False);
	Atom atom_utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	XChangeProperty(dpy,
	                win,
	                atom_wm_name,
	                atom_utf8,
	                8,
	                PropModeReplace,
	                (const unsigned char *)title,
	                (int)strlen(title));

	/* Mark as a DOCK so aswlcomp treats it as a dockapp widget. */
	Atom atom_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	Atom atom_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	XChangeProperty(dpy,
	                win,
	                atom_type,
	                XA_ATOM,
	                32,
	                PropModeReplace,
	                (unsigned char *)&atom_dock,
	                1);

	GC gc = XCreateGC(dpy, win, 0, NULL);
	if (gc == 0) {
		fprintf(stderr, "%s: XCreateGC failed\n", argv[0]);
		XDestroyWindow(dpy, win);
		XCloseDisplay(dpy);
		return 1;
	}

	XMapWindow(dpy, win);
	XFlush(dpy);

	uint32_t start = now_ms();
	uint32_t end = start + (uint32_t)(seconds * 1000);
	bool saw_map = false;

	for (;;) {
		while (XPending(dpy) > 0) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == MapNotify)
				saw_map = true;
			if (ev.type == DestroyNotify) {
				XFreeGC(dpy, gc);
				XCloseDisplay(dpy);
				return 0;
			}
		}

		uint32_t now = now_ms();
		if (now >= end)
			break;

		if (saw_map) {
			/* Simple “logo” block. */
			XSetForeground(dpy, gc, 0x1E1E1E);
			XFillRectangle(dpy, win, gc, 0, 0, (unsigned)size, (unsigned)size);

			XSetForeground(dpy, gc, 0x00CC66);
			int pad = size / 6;
			XFillRectangle(dpy,
			               win,
			               gc,
			               pad,
			               pad,
			               (unsigned)(size - 2 * pad),
			               (unsigned)(size - 2 * pad));
			XFlush(dpy);
		}

		struct timespec req = {
			.tv_sec = 0,
			.tv_nsec = 50 * 1000 * 1000,
		};
		(void)nanosleep(&req, NULL);
	}

	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
