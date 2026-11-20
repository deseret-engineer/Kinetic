# kinetic-wall, Kinetic's wallpaper engine. Builds with what Omarchy ships:
# make, gcc and pkg-config (base-devel), Wayland, EGL/GLES, libpng, libjpeg,
# libwebp, and PipeWire for sound (left out if it is missing).
#
#   make                build build/kinetic-wall
#   make install        install it and the bundled wallpapers under PREFIX
#   make clean          remove build/
#   make AUDIO=0        build without PipeWire
#   make version        print the version (Kinetic's, from lib/kinetic.sh)

HERE    := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
PREFIX  ?= $(HOME)/.local
BUILD   ?= $(HERE)/build
CC      ?= cc
PKG_CONFIG ?= pkg-config

# One version for all of Kinetic, kept in lib/kinetic.sh
VERSION := $(shell sed -n 's/^KINETIC_VERSION=//p' $(HERE)/../../lib/kinetic.sh)
ifeq ($(VERSION),)
$(error could not read KINETIC_VERSION from lib/kinetic.sh)
endif

PKGS    := wayland-client wayland-egl egl glesv2 libpng libjpeg libwebp
AUDIO   ?= $(shell $(PKG_CONFIG) --exists libpipewire-0.3 && echo 1 || echo 0)
ifeq ($(AUDIO),1)
PKGS    += libpipewire-0.3
endif

PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS   := $(shell $(PKG_CONFIG) --libs $(PKGS))
WLP        := $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)
SCANNER    := $(shell $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner 2>/dev/null || command -v wayland-scanner)

# CFLAGS and LDFLAGS are yours (optimization and the like, as a distribution
# sets them); what the engine needs to build at all is in ENGINE_*.
CFLAGS  ?= -O3 -flto=auto
LDFLAGS ?= -flto=auto -Wl,--as-needed -Wl,-O1
ENGINE_CFLAGS := -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
                 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DLW_VERSION='"$(VERSION)"' \
                 -I$(BUILD)/protocols $(PKG_CFLAGS) -MMD -MP
ENGINE_LIBS   := $(PKG_LIBS) -lm

SRC := main util toml image gfx wallpaper engine wayland ipc offscreen overlay lens plugin motion \
       mod_theme mod_hyprland mod_power mod_shell json layers
ifeq ($(AUDIO),1)
SRC     += mod_audio
ENGINE_CFLAGS += -DLW_AUDIO=1
endif

PROTOCOLS := $(HERE)/protocols/wlr-layer-shell-unstable-v1.xml \
             $(WLP)/stable/xdg-shell/xdg-shell.xml \
             $(WLP)/stable/viewporter/viewporter.xml \
             $(WLP)/stable/tablet/tablet-v2.xml \
             $(WLP)/staging/fractional-scale/fractional-scale-v1.xml \
             $(WLP)/staging/cursor-shape/cursor-shape-v1.xml
PROTO_NAMES := $(basename $(notdir $(PROTOCOLS)))
PROTO_H     := $(PROTO_NAMES:%=$(BUILD)/protocols/%-protocol.h)
PROTO_C     := $(PROTO_NAMES:%=$(BUILD)/protocols/%-protocol.c)

OBJ := $(SRC:%=$(BUILD)/obj/%.o) $(PROTO_NAMES:%=$(BUILD)/obj/%-protocol.o)
BIN := $(BUILD)/kinetic-wall

.PHONY: all install clean version
all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(ENGINE_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(ENGINE_LIBS) $(LDLIBS)

# The sources include the generated headers, so they come first.
$(BUILD)/obj/%.o: $(HERE)/src/%.c $(PROTO_H) $(BUILD)/flags | $(BUILD)/obj
	$(CC) $(ENGINE_CFLAGS) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/obj/%-protocol.o: $(BUILD)/protocols/%-protocol.c $(BUILD)/flags | $(BUILD)/obj
	$(CC) $(ENGINE_CFLAGS) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# Each protocol by its file name, wherever it lives.
define protocol
$(BUILD)/protocols/$(basename $(notdir $(1)))-protocol.h: $(1) | $(BUILD)/protocols
	$(SCANNER) client-header $$< $$@
$(BUILD)/protocols/$(basename $(notdir $(1)))-protocol.c: $(1) | $(BUILD)/protocols
	$(SCANNER) private-code $$< $$@
endef
$(foreach p,$(PROTOCOLS),$(eval $(call protocol,$(p))))

# Rebuild everything when the version or the flags change: build/flags is
# rewritten (and so newer than every object) only when they do.
FLAGS := $(CC) $(ENGINE_CFLAGS) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(ENGINE_LIBS) $(LDLIBS)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(file < $(BUILD)/flags),$(FLAGS))
$(shell mkdir -p $(BUILD))
$(file > $(BUILD)/flags,$(FLAGS))
endif
endif

$(BUILD)/obj $(BUILD)/protocols:
	mkdir -p $@

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/kinetic-wall
	rm -rf $(DESTDIR)$(PREFIX)/share/kinetic/wallpapers
	mkdir -p $(DESTDIR)$(PREFIX)/share/kinetic
	cp -r $(HERE)/wallpapers $(DESTDIR)$(PREFIX)/share/kinetic/wallpapers

clean:
	rm -rf $(BUILD)

version:
	@echo $(VERSION)

-include $(OBJ:.o=.d)
