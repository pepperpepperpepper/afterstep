# Build a small Wayland client PoC (AfterStep-on-Wayland “B” track scaffolding).
#
# Requirements:
#   - wayland-client (headers + library)
#   - wayland-scanner
#   - wayland-protocols (for xdg-shell XML)
#   - (vendored) wlr-layer-shell protocol XML (in this repo)
#
# Optional (enables PNG icons):
#   - libpng (via pkg-config `libpng`)
# Optional (enables font rendering via FreeType; name lookup via fontconfig):
#   - freetype2 (via pkg-config `freetype2`)
#   - fontconfig (via pkg-config `fontconfig`)
#
# Usage:
#   make
#   ./aswlpanel
#   make aswlcomp   # wlroots-based compositor (Track C scaffolding)

CC ?= cc
PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner

CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wformat=2 -Wshadow -Wpointer-arith

WAYLAND_CFLAGS := $(shell $(PKG_CONFIG) --cflags wayland-client 2>/dev/null)
WAYLAND_LIBS := $(shell $(PKG_CONFIG) --libs wayland-client 2>/dev/null)

WAYLAND_SERVER_LIBS := $(shell $(PKG_CONFIG) --libs wayland-server 2>/dev/null)
XKBCOMMON_LIBS := $(shell $(PKG_CONFIG) --libs xkbcommon 2>/dev/null)
XKBCOMMON_CFLAGS := $(shell $(PKG_CONFIG) --cflags xkbcommon 2>/dev/null)
LIBINPUT_CFLAGS := $(shell $(PKG_CONFIG) --cflags libinput 2>/dev/null)
LIBINPUT_LIBS := $(shell $(PKG_CONFIG) --libs libinput 2>/dev/null)

PNG_CFLAGS := $(shell $(PKG_CONFIG) --cflags libpng 2>/dev/null)
PNG_LIBS := $(shell $(PKG_CONFIG) --libs libpng 2>/dev/null)

JPEG_CFLAGS := $(shell $(PKG_CONFIG) --cflags libjpeg 2>/dev/null)
JPEG_LIBS := $(shell $(PKG_CONFIG) --libs libjpeg 2>/dev/null)

FREETYPE_CFLAGS := $(shell $(PKG_CONFIG) --cflags freetype2 2>/dev/null)
FREETYPE_LIBS := $(shell $(PKG_CONFIG) --libs freetype2 2>/dev/null)

FONTCONFIG_CFLAGS := $(shell $(PKG_CONFIG) --cflags fontconfig 2>/dev/null)
FONTCONFIG_LIBS := $(shell $(PKG_CONFIG) --libs fontconfig 2>/dev/null)

# Arch and some distros ship versioned wlroots pkg-config names (eg `wlroots-0.19`).
WLROOTS_PKG := $(shell for p in wlroots wlroots-0.19 wlroots-0.18 wlroots-0.17; do \
	$(PKG_CONFIG) --exists $$p 2>/dev/null && echo $$p && break; \
done)
WLROOTS_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(WLROOTS_PKG) 2>/dev/null)
WLROOTS_LIBS := $(shell $(PKG_CONFIG) --libs $(WLROOTS_PKG) 2>/dev/null)
PIXMAN_LIBS := $(shell $(PKG_CONFIG) --libs pixman-1 2>/dev/null)
XCB_ICCCM_LIBS := $(shell $(PKG_CONFIG) --libs xcb-icccm 2>/dev/null)

XDG_PKGDATADIR := $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols 2>/dev/null)
XDG_SHELL_XML := $(XDG_PKGDATADIR)/stable/xdg-shell/xdg-shell.xml

LAYER_SHELL_XML := protocols/wlr-layer-shell-unstable-v1.xml
CONTROL_XML := protocols/afterstep-control-v1.xml
OUTPUT_MGMT_XML := protocols/wlr-output-management-unstable-v1.xml
SESSION_LOCK_XML := $(XDG_PKGDATADIR)/staging/ext-session-lock/ext-session-lock-v1.xml
IDLE_INHIBIT_XML := $(XDG_PKGDATADIR)/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml
POINTER_CONSTRAINTS_XML := $(XDG_PKGDATADIR)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml

GEN_DIR := gen

XDG_HDR := $(GEN_DIR)/xdg-shell-client-protocol.h
XDG_SRC := $(GEN_DIR)/xdg-shell-protocol.c
XDG_SERVER_HDR := $(GEN_DIR)/xdg-shell-protocol.h

LAYER_HDR := $(GEN_DIR)/wlr-layer-shell-unstable-v1-client-protocol.h
LAYER_SRC := $(GEN_DIR)/wlr-layer-shell-unstable-v1-protocol.c
LAYER_SERVER_HDR := $(GEN_DIR)/wlr-layer-shell-unstable-v1-protocol.h

CONTROL_HDR := $(GEN_DIR)/afterstep-control-v1-client-protocol.h
CONTROL_SRC := $(GEN_DIR)/afterstep-control-v1-protocol.c
CONTROL_SERVER_HDR := $(GEN_DIR)/afterstep-control-v1-protocol.h

OUTPUT_MGMT_HDR := $(GEN_DIR)/wlr-output-management-unstable-v1-client-protocol.h
OUTPUT_MGMT_SRC := $(GEN_DIR)/wlr-output-management-unstable-v1-protocol.c
OUTPUT_MGMT_SERVER_HDR := $(GEN_DIR)/wlr-output-management-unstable-v1-protocol.h

SESSION_LOCK_HDR := $(GEN_DIR)/ext-session-lock-v1-client-protocol.h
SESSION_LOCK_SRC := $(GEN_DIR)/ext-session-lock-v1-protocol.c

IDLE_INHIBIT_HDR := $(GEN_DIR)/idle-inhibit-unstable-v1-client-protocol.h
IDLE_INHIBIT_SRC := $(GEN_DIR)/idle-inhibit-unstable-v1-protocol.c

POINTER_CONSTRAINTS_SERVER_HDR := $(GEN_DIR)/pointer-constraints-unstable-v1-protocol.h

ifeq ($(wildcard $(XDG_SHELL_XML)),)
$(error Missing xdg-shell XML. Install wayland-protocols (and make sure pkg-config can find it).)
endif

ifeq ($(wildcard $(LAYER_SHELL_XML)),)
$(error Missing layer-shell XML. Expected $(LAYER_SHELL_XML).)
endif

ifeq ($(wildcard $(CONTROL_XML)),)
$(error Missing afterstep-control XML. Expected $(CONTROL_XML).)
endif

ifeq ($(wildcard $(OUTPUT_MGMT_XML)),)
$(error Missing wlr-output-management XML. Expected $(OUTPUT_MGMT_XML).)
endif

ifeq ($(wildcard $(SESSION_LOCK_XML)),)
$(error Missing ext-session-lock XML. Install wayland-protocols (staging/ext-session-lock).)
endif

ifeq ($(wildcard $(IDLE_INHIBIT_XML)),)
$(error Missing idle-inhibit XML. Install wayland-protocols (unstable/idle-inhibit).)
endif

ifeq ($(wildcard $(POINTER_CONSTRAINTS_XML)),)
$(error Missing pointer-constraints XML. Install wayland-protocols (unstable/pointer-constraints).)
endif

PROTOCOL_SRCS := $(XDG_SRC)
PROTOCOL_HDRS := $(XDG_HDR)

PROTOCOL_SRCS += $(LAYER_SRC)
PROTOCOL_HDRS += $(LAYER_HDR)
CFLAGS += -DHAVE_WLR_LAYER_SHELL=1

PROTOCOL_SRCS += $(CONTROL_SRC)
PROTOCOL_HDRS += $(CONTROL_HDR)

PROTOCOL_SRCS += $(OUTPUT_MGMT_SRC)
PROTOCOL_HDRS += $(OUTPUT_MGMT_HDR)

PROTOCOL_SRCS += $(SESSION_LOCK_SRC)
PROTOCOL_HDRS += $(SESSION_LOCK_HDR)

PROTOCOL_SRCS += $(IDLE_INHIBIT_SRC)
PROTOCOL_HDRS += $(IDLE_INHIBIT_HDR)

EXTRA_LIBS :=
ifneq ($(strip $(PNG_LIBS)),)
CFLAGS += $(PNG_CFLAGS) -DHAVE_LIBPNG=1
EXTRA_LIBS += $(PNG_LIBS)
endif

ifneq ($(strip $(JPEG_LIBS)),)
CFLAGS += $(JPEG_CFLAGS) -DHAVE_LIBJPEG=1
EXTRA_LIBS += $(JPEG_LIBS)
endif

ifneq ($(strip $(FREETYPE_LIBS)),)
CFLAGS += $(FREETYPE_CFLAGS) -DHAVE_FREETYPE=1
EXTRA_LIBS += $(FREETYPE_LIBS)

ifneq ($(strip $(FONTCONFIG_LIBS)),)
CFLAGS += $(FONTCONFIG_CFLAGS) -DHAVE_FONTCONFIG=1
EXTRA_LIBS += $(FONTCONFIG_LIBS)
endif
endif

.PHONY: all clean run-nested smoke

MENU_CFLAGS :=
MENU_LIBS :=
ifneq ($(strip $(XKBCOMMON_LIBS)),)
MENU_CFLAGS += $(XKBCOMMON_CFLAGS) -DHAVE_XKBCOMMON=1
MENU_LIBS += $(XKBCOMMON_LIBS)
endif

# Use libAfterImage to render AfterStep's background XML scripts in `aswlbg`.
AFTERIMAGE_CONFIG := ../libAfterImage/afterimage-config
AFTERIMAGE_CFLAGS := -I../libAfterImage -I../libAfterBase
AFTERIMAGE_LIB := ../libAfterImage/libAfterImage.a
AFTERBASE_LIB := ../libAfterBase/libAfterBase.a
AFTERIMAGE_LIBS_EXTERNAL := $(shell sh $(AFTERIMAGE_CONFIG) --libs --external-only 2>/dev/null)

all: aswlpanel aswlmenu aswlctl aswlrandr aswlbg aswlbanner aswlwait aswllock aswlidle aswlprobe

aswlpanel: aswlpanel_main.o aswlpanel_wl.o aswlpanel_buffers.o aswlpanel_model.o aswlpanel_config.o aswlpanel_render.o aswlpanel_paint.o aswltheme_core.o aswltheme_colorscheme.o aswltheme_styles.o aswltheme_load.o aswlicon.o aswlicon_resolve.o aswlicon_png.o aswlicon_xml.o aswlicon_afterimage.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(PROTOCOL_SRCS) $(AFTERIMAGE_LIB)
		$(CC) $(CFLAGS) -o $@ aswlpanel_main.o aswlpanel_wl.o aswlpanel_buffers.o aswlpanel_model.o aswlpanel_config.o aswlpanel_render.o aswlpanel_paint.o aswltheme_core.o aswltheme_colorscheme.o aswltheme_styles.o aswltheme_load.o aswlicon.o aswlicon_resolve.o aswlicon_png.o aswlicon_xml.o aswlicon_afterimage.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS) $(AFTERIMAGE_LIB) $(AFTERBASE_LIB) $(AFTERIMAGE_LIBS_EXTERNAL) $(EXTRA_LIBS)

aswlpanel_main.o: aswlpanel_main.c aswlpanel_internal.h aswlicon.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -I$(GEN_DIR) -c -o $@ $<

aswlpanel_wl.o: aswlpanel_wl.c aswlpanel_internal.h aswlicon.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -I$(GEN_DIR) -c -o $@ $<

aswlpanel_buffers.o: aswlpanel_buffers.c aswlpanel_internal.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -I$(GEN_DIR) -c -o $@ $<

aswlpanel_model.o: aswlpanel_model.c aswlpanel_internal.h aswlicon.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
			$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -I$(GEN_DIR) -c -o $@ $<

aswlpanel_config.o: aswlpanel_config.c aswlpanel_internal.h aswlicon.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -I$(GEN_DIR) -c -o $@ $<

aswlpanel_render.o: aswlpanel_render.c aswlpanel_internal.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -I$(GEN_DIR) -c -o $@ $<

aswlpanel_paint.o: aswlpanel_paint.c aswlpanel_internal.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -I$(GEN_DIR) -c -o $@ $<

aswlmenu: aswlmenu_main.o aswlmenu_wl.o aswlmenu_model.o aswlmenu_config.o aswlmenu_desktop.o aswlmenu_render.o aswlmenu_input.o aswltheme_core.o aswltheme_colorscheme.o aswltheme_styles.o aswltheme_load.o aswlicon.o aswlicon_resolve.o aswlicon_png.o aswlicon_xml.o aswlicon_afterimage.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(PROTOCOL_SRCS) $(AFTERIMAGE_LIB)
			$(CC) $(CFLAGS) -o $@ aswlmenu_main.o aswlmenu_wl.o aswlmenu_model.o aswlmenu_config.o aswlmenu_desktop.o aswlmenu_render.o aswlmenu_input.o aswltheme_core.o aswltheme_colorscheme.o aswltheme_styles.o aswltheme_load.o aswlicon.o aswlicon_resolve.o aswlicon_png.o aswlicon_xml.o aswlicon_afterimage.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS) $(AFTERIMAGE_LIB) $(AFTERBASE_LIB) $(AFTERIMAGE_LIBS_EXTERNAL) $(EXTRA_LIBS) $(MENU_LIBS)

aswlmenu_main.o: aswlmenu_main.c aswlmenu_internal.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(MENU_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlmenu_wl.o: aswlmenu_wl.c aswlmenu_internal.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(MENU_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlmenu_model.o: aswlmenu_model.c aswlmenu_internal.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(MENU_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlmenu_config.o: aswlmenu_config.c aswlmenu_internal.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(MENU_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlmenu_desktop.o: aswlmenu_desktop.c aswlmenu_internal.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(MENU_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlmenu_render.o: aswlmenu_render.c aswlmenu_internal.h aswlicon.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(MENU_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlmenu_input.o: aswlmenu_input.c aswlmenu_internal.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(MENU_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlctl: aswlctl.o $(PROTOCOL_SRCS)
	$(CC) $(CFLAGS) -o $@ aswlctl.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS)

aswlctl.o: aswlctl.c $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlrandr: aswlrandr.o $(PROTOCOL_SRCS)
	$(CC) $(CFLAGS) -o $@ aswlrandr.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS)

aswlrandr.o: aswlrandr.c $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

$(AFTERBASE_LIB):
	$(MAKE) -C ../libAfterBase libAfterBase.a

$(AFTERIMAGE_LIB): $(AFTERBASE_LIB)
	$(MAKE) -C ../libAfterImage libAfterImage.a

aswlbg: aswlbg.o aswlbg_wl.o aswlbg_render.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(PROTOCOL_SRCS) $(AFTERIMAGE_LIB)
	$(CC) $(CFLAGS) -o $@ aswlbg.o aswlbg_wl.o aswlbg_render.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS) $(AFTERIMAGE_LIB) $(AFTERBASE_LIB) $(AFTERIMAGE_LIBS_EXTERNAL) $(EXTRA_LIBS)

aswlbg.o: aswlbg.c aswlbg_internal.h $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(AFTERIMAGE_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlbg_wl.o: aswlbg_wl.c aswlbg_internal.h $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(AFTERIMAGE_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlbg_render.o: aswlbg_render.c aswlbg_internal.h $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(AFTERIMAGE_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlbanner: aswlbanner.o $(PROTOCOL_SRCS) $(AFTERIMAGE_LIB)
	$(CC) $(CFLAGS) -o $@ aswlbanner.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS) $(AFTERIMAGE_LIB) $(AFTERBASE_LIB) $(AFTERIMAGE_LIBS_EXTERNAL) $(EXTRA_LIBS)

aswlbanner.o: aswlbanner.c $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(AFTERIMAGE_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlwait: aswlwait.o aswltheme_core.o aswltheme_colorscheme.o aswltheme_styles.o aswltheme_load.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(PROTOCOL_SRCS)
	$(CC) $(CFLAGS) -o $@ aswlwait.o aswltheme_core.o aswltheme_colorscheme.o aswltheme_styles.o aswltheme_load.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS) $(EXTRA_LIBS)

aswlwait.o: aswlwait.c aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswllock: aswllock.o aswllock_wl.o aswltheme_core.o aswltheme_colorscheme.o aswltheme_styles.o aswltheme_load.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(PROTOCOL_SRCS)
	$(CC) $(CFLAGS) -o $@ aswllock_wl.o aswllock.o aswltheme_core.o aswltheme_colorscheme.o aswltheme_styles.o aswltheme_load.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS) $(EXTRA_LIBS)

aswllock.o: aswllock.c aswllock_internal.h aswlfont.h aswltheme.h
aswllock_wl.o: aswllock_wl.c aswllock_internal.h aswlfont.h aswltheme.h $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlidle: aswlidle.o $(PROTOCOL_SRCS)
	$(CC) $(CFLAGS) -o $@ aswlidle.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS)

aswlidle.o: aswlidle.c $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlprobe: aswlprobe.o
	$(CC) $(CFLAGS) -o $@ aswlprobe.o $(WAYLAND_LIBS)

aswlprobe.o: aswlprobe.c
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) -c -o $@ $<

aswltheme_core.o: aswltheme_core.c aswltheme.h
	$(CC) $(CFLAGS) -c -o $@ $<

aswltheme_colorscheme.o: aswltheme_colorscheme.c aswltheme_internal.h aswltheme.h
	$(CC) $(CFLAGS) -c -o $@ $<

aswltheme_styles.o: aswltheme_styles.c aswltheme_internal.h aswltheme.h
	$(CC) $(CFLAGS) -c -o $@ $<

aswltheme_load.o: aswltheme_load.c aswltheme_internal.h aswltheme.h
	$(CC) $(CFLAGS) -c -o $@ $<

aswlicon.o: aswlicon.c aswlicon.h aswlicon_internal.h
	$(CC) $(CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -c -o $@ $<

aswlicon_resolve.o: aswlicon_resolve.c aswlicon_internal.h
	$(CC) $(CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -c -o $@ $<

aswlicon_png.o: aswlicon_png.c aswlicon_internal.h
	$(CC) $(CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -c -o $@ $<

aswlicon_xml.o: aswlicon_xml.c aswlicon_internal.h
	$(CC) $(CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -c -o $@ $<

aswlicon_afterimage.o: aswlicon_afterimage.c aswlicon_internal.h
	$(CC) $(CFLAGS) $(AFTERIMAGE_CFLAGS) -DHAVE_AFTERIMAGE=1 -c -o $@ $<

aswlfont.o: aswlfont.c aswlfont_internal.h aswlfont.h
	$(CC) $(CFLAGS) -c -o $@ $<

aswlfont_backend.o: aswlfont_backend.c aswlfont_internal.h aswlfont.h
	$(CC) $(CFLAGS) -c -o $@ $<

aswlfont_layout.o: aswlfont_layout.c aswlfont_internal.h aswlfont.h
	$(CC) $(CFLAGS) -c -o $@ $<

ifeq ($(strip $(WLROOTS_LIBS)),)
aswlcomp:
	@echo "Missing wlroots (pkg-config 'wlroots'). Install wlroots development files." 1>&2
	@exit 1
else
aswlcomp: aswlcomp_main.o aswlcomp_server.o aswlcomp_ime.o aswlcomp_decor.o aswlcomp_layer.o aswlcomp_output.o aswlcomp_state.o aswlcomp_control_lock.o aswlcomp_input.o aswlcomp_input_config.o aswlcomp_view.o aswlcomp_dock.o aswlcomp_toplevel.o aswlcomp_toplevel_protocols.o aswltheme_core.o aswltheme_colorscheme.o aswltheme_styles.o aswltheme_load.o aswlicon.o aswlicon_resolve.o aswlicon_png.o aswlicon_xml.o aswlicon_afterimage.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(CONTROL_SRC) $(AFTERIMAGE_LIB)
		$(CC) $(CFLAGS) -o $@ aswlcomp_main.o aswlcomp_server.o aswlcomp_ime.o aswlcomp_decor.o aswlcomp_layer.o aswlcomp_output.o aswlcomp_state.o aswlcomp_control_lock.o aswlcomp_input.o aswlcomp_input_config.o aswlcomp_view.o aswlcomp_dock.o aswlcomp_toplevel.o aswlcomp_toplevel_protocols.o aswltheme_core.o aswltheme_colorscheme.o aswltheme_styles.o aswltheme_load.o aswlicon.o aswlicon_resolve.o aswlicon_png.o aswlicon_xml.o aswlicon_afterimage.o aswlfont.o aswlfont_backend.o aswlfont_layout.o $(CONTROL_SRC) $(WLROOTS_LIBS) $(WAYLAND_SERVER_LIBS) $(XKBCOMMON_LIBS) $(PIXMAN_LIBS) $(LIBINPUT_LIBS) $(XCB_ICCCM_LIBS) $(AFTERIMAGE_LIB) $(AFTERBASE_LIB) $(AFTERIMAGE_LIBS_EXTERNAL) $(EXTRA_LIBS)

ASWLCOMP_PROTOCOL_HDRS := $(XDG_SERVER_HDR) $(LAYER_SERVER_HDR) $(CONTROL_SERVER_HDR) $(POINTER_CONSTRAINTS_SERVER_HDR)

aswlcomp_main.o: aswlcomp_main.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_server.o: aswlcomp_server.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_ime.o: aswlcomp_ime.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_decor.o: aswlcomp_decor.c aswlcomp_internal.h aswlicon.h aswlfont.h aswltheme.h
	$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_layer.o: aswlcomp_layer.c aswlcomp_internal.h aswlfont.h aswltheme.h
	$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_output.o: aswlcomp_output.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_state.o: aswlcomp_state.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_control_lock.o: aswlcomp_control_lock.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_input.o: aswlcomp_input.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_input_config.o: aswlcomp_input_config.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_view.o: aswlcomp_view.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_dock.o: aswlcomp_dock.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_toplevel.o: aswlcomp_toplevel.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<

aswlcomp_toplevel_protocols.o: aswlcomp_toplevel_protocols.c aswlcomp_internal.h aswlfont.h aswltheme.h $(ASWLCOMP_PROTOCOL_HDRS)
		$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) $(LIBINPUT_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<
endif

$(GEN_DIR):
	mkdir -p $@

$(XDG_HDR): $(XDG_SHELL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(XDG_SERVER_HDR): $(XDG_SHELL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) server-header $< $@

$(XDG_SRC): $(XDG_SHELL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(LAYER_HDR): $(LAYER_SHELL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(LAYER_SERVER_HDR): $(LAYER_SHELL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) server-header $< $@

$(LAYER_SRC): $(LAYER_SHELL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(CONTROL_HDR): $(CONTROL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(CONTROL_SERVER_HDR): $(CONTROL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) server-header $< $@

$(CONTROL_SRC): $(CONTROL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(OUTPUT_MGMT_HDR): $(OUTPUT_MGMT_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(OUTPUT_MGMT_SERVER_HDR): $(OUTPUT_MGMT_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) server-header $< $@

$(OUTPUT_MGMT_SRC): $(OUTPUT_MGMT_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(SESSION_LOCK_HDR): $(SESSION_LOCK_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(SESSION_LOCK_SRC): $(SESSION_LOCK_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(IDLE_INHIBIT_HDR): $(IDLE_INHIBIT_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(IDLE_INHIBIT_SRC): $(IDLE_INHIBIT_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(POINTER_CONSTRAINTS_SERVER_HDR): $(POINTER_CONSTRAINTS_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) server-header $< $@

clean:
			rm -rf $(GEN_DIR) aswlpanel aswlpanel.o aswlpanel_main.o aswlpanel_wl.o aswlpanel_buffers.o aswlpanel_model.o aswlpanel_config.o aswlpanel_render.o aswlpanel_paint.o aswlmenu aswlmenu_main.o aswlmenu_wl.o aswlmenu_model.o aswlmenu_config.o aswlmenu_desktop.o aswlmenu_render.o aswlmenu_input.o aswlctl aswlctl.o aswlrandr aswlrandr.o aswlbg aswlbg.o aswlbg_wl.o aswlbg_render.o aswlbanner aswlbanner.o aswlwait aswlwait.o aswllock aswllock.o aswllock_wl.o aswlidle aswlidle.o aswlprobe aswlprobe.o aswltheme.o aswltheme_core.o aswltheme_colorscheme.o aswltheme_styles.o aswltheme_load.o aswlicon.o aswlicon_resolve.o aswlicon_png.o aswlicon_xml.o aswlicon_afterimage.o aswlfont.o aswlfont_backend.o aswlfont_layout.o aswlcomp aswlcomp.o aswlcomp_main.o aswlcomp_server.o aswlcomp_ime.o aswlcomp_decor.o aswlcomp_layer.o aswlcomp_output.o aswlcomp_state.o aswlcomp_control_lock.o aswlcomp_input.o aswlcomp_input_config.o aswlcomp_view.o aswlcomp_dock.o aswlcomp_toplevel.o aswlcomp_toplevel_protocols.o

run-nested: aswlcomp aswlpanel aswlmenu
	./run-nested.sh

smoke:
	../tools/wayland-smoke.sh
