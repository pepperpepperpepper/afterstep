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

PNG_CFLAGS := $(shell $(PKG_CONFIG) --cflags libpng 2>/dev/null)
PNG_LIBS := $(shell $(PKG_CONFIG) --libs libpng 2>/dev/null)

# Arch and some distros ship versioned wlroots pkg-config names (eg `wlroots-0.19`).
WLROOTS_PKG := $(shell for p in wlroots wlroots-0.19 wlroots-0.18 wlroots-0.17; do \
	$(PKG_CONFIG) --exists $$p 2>/dev/null && echo $$p && break; \
done)
WLROOTS_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(WLROOTS_PKG) 2>/dev/null)
WLROOTS_LIBS := $(shell $(PKG_CONFIG) --libs $(WLROOTS_PKG) 2>/dev/null)

XDG_PKGDATADIR := $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols 2>/dev/null)
XDG_SHELL_XML := $(XDG_PKGDATADIR)/stable/xdg-shell/xdg-shell.xml

LAYER_SHELL_XML := protocols/wlr-layer-shell-unstable-v1.xml
CONTROL_XML := protocols/afterstep-control-v1.xml

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

ifeq ($(wildcard $(XDG_SHELL_XML)),)
$(error Missing xdg-shell XML. Install wayland-protocols (and make sure pkg-config can find it).)
endif

ifeq ($(wildcard $(LAYER_SHELL_XML)),)
$(error Missing layer-shell XML. Expected $(LAYER_SHELL_XML).)
endif

ifeq ($(wildcard $(CONTROL_XML)),)
$(error Missing afterstep-control XML. Expected $(CONTROL_XML).)
endif

PROTOCOL_SRCS := $(XDG_SRC)
PROTOCOL_HDRS := $(XDG_HDR)

PROTOCOL_SRCS += $(LAYER_SRC)
PROTOCOL_HDRS += $(LAYER_HDR)
CFLAGS += -DHAVE_WLR_LAYER_SHELL=1

PROTOCOL_SRCS += $(CONTROL_SRC)
PROTOCOL_HDRS += $(CONTROL_HDR)

EXTRA_LIBS :=
ifneq ($(strip $(PNG_LIBS)),)
CFLAGS += $(PNG_CFLAGS) -DHAVE_LIBPNG=1
EXTRA_LIBS += $(PNG_LIBS)
endif

.PHONY: all clean run-nested

MENU_CFLAGS :=
MENU_LIBS :=
ifneq ($(strip $(XKBCOMMON_LIBS)),)
MENU_CFLAGS += $(XKBCOMMON_CFLAGS) -DHAVE_XKBCOMMON=1
MENU_LIBS += $(XKBCOMMON_LIBS)
endif

all: aswlpanel aswlmenu

aswlpanel: aswlpanel.o aswltheme.o $(PROTOCOL_SRCS)
	$(CC) $(CFLAGS) -o $@ aswlpanel.o aswltheme.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS) $(EXTRA_LIBS)

aswlpanel.o: aswlpanel.c $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswlmenu: aswlmenu.o aswltheme.o $(PROTOCOL_SRCS)
	$(CC) $(CFLAGS) -o $@ aswlmenu.o aswltheme.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS) $(EXTRA_LIBS) $(MENU_LIBS)

aswlmenu.o: aswlmenu.c $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) $(MENU_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

aswltheme.o: aswltheme.c aswltheme.h
	$(CC) $(CFLAGS) -c -o $@ $<

ifeq ($(strip $(WLROOTS_LIBS)),)
aswlcomp:
	@echo "Missing wlroots (pkg-config 'wlroots'). Install wlroots development files." 1>&2
	@exit 1
else
aswlcomp: aswlcomp.o $(CONTROL_SRC)
	$(CC) $(CFLAGS) -o $@ aswlcomp.o $(CONTROL_SRC) $(WLROOTS_LIBS) $(WAYLAND_SERVER_LIBS) $(XKBCOMMON_LIBS)

ASWLCOMP_PROTOCOL_HDRS := $(XDG_SERVER_HDR) $(LAYER_SERVER_HDR) $(CONTROL_SERVER_HDR)

aswlcomp.o: aswlcomp.c $(ASWLCOMP_PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WLROOTS_CFLAGS) -I$(GEN_DIR) -DWLR_USE_UNSTABLE -DHAVE_WLROOTS=1 -c -o $@ $<
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

clean:
	rm -rf $(GEN_DIR) aswlpanel aswlpanel.o aswlmenu aswlmenu.o aswltheme.o aswlcomp aswlcomp.o

run-nested: aswlcomp aswlpanel aswlmenu
	./run-nested.sh
