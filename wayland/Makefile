# Build a small Wayland client PoC (AfterStep-on-Wayland “B” track scaffolding).
#
# Requirements:
#   - wayland-client (headers + library)
#   - wayland-scanner
#   - wayland-protocols (for xdg-shell XML)
#
# Optional (enables layer-shell):
#   - wlr-protocols (for wlr-layer-shell XML)
#
# Usage:
#   make
#   ./aswlpanel

CC ?= cc
PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner

CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wformat=2 -Wshadow -Wpointer-arith

WAYLAND_CFLAGS := $(shell $(PKG_CONFIG) --cflags wayland-client 2>/dev/null)
WAYLAND_LIBS := $(shell $(PKG_CONFIG) --libs wayland-client 2>/dev/null)

XDG_PKGDATADIR := $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols 2>/dev/null)
XDG_SHELL_XML := $(XDG_PKGDATADIR)/stable/xdg-shell/xdg-shell.xml

WLR_PKGDATADIR := $(shell $(PKG_CONFIG) --variable=pkgdatadir wlr-protocols 2>/dev/null)
LAYER_SHELL_XML := $(WLR_PKGDATADIR)/unstable/wlr-layer-shell-unstable-v1.xml

GEN_DIR := gen

XDG_HDR := $(GEN_DIR)/xdg-shell-client-protocol.h
XDG_SRC := $(GEN_DIR)/xdg-shell-protocol.c

LAYER_HDR := $(GEN_DIR)/wlr-layer-shell-unstable-v1-client-protocol.h
LAYER_SRC := $(GEN_DIR)/wlr-layer-shell-unstable-v1-protocol.c

ifeq ($(wildcard $(XDG_SHELL_XML)),)
$(error Missing xdg-shell XML. Install wayland-protocols (and make sure pkg-config can find it).)
endif

PROTOCOL_SRCS := $(XDG_SRC)
PROTOCOL_HDRS := $(XDG_HDR)

ifneq ($(wildcard $(LAYER_SHELL_XML)),)
PROTOCOL_SRCS += $(LAYER_SRC)
PROTOCOL_HDRS += $(LAYER_HDR)
CFLAGS += -DHAVE_WLR_LAYER_SHELL=1
endif

.PHONY: all clean

all: aswlpanel

aswlpanel: aswlpanel.o $(PROTOCOL_SRCS)
	$(CC) $(CFLAGS) -o $@ aswlpanel.o $(PROTOCOL_SRCS) $(WAYLAND_LIBS)

aswlpanel.o: aswlpanel.c $(PROTOCOL_HDRS)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) -I$(GEN_DIR) -c -o $@ $<

$(GEN_DIR):
	mkdir -p $@

$(XDG_HDR): $(XDG_SHELL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(XDG_SRC): $(XDG_SHELL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(LAYER_HDR): $(LAYER_SHELL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(LAYER_SRC): $(LAYER_SHELL_XML) | $(GEN_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

clean:
	rm -rf $(GEN_DIR) aswlpanel aswlpanel.o
