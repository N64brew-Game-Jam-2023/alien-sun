BUILD_DIR=build
SOURCE_DIR=src
include $(N64_INST)/include/n64.mk

DEBUG ?= 0

N64_CFLAGS := $(filter-out -std=gnu99 -O2,$(N64_CFLAGS)) \
              "-I$(abspath $(BUILD_DIR))" \
              "-I$(abspath $(SOURCE_DIR))" \
              -DLIBDRAGON_FAST_MATH "-I$(abspath subprojects/box2d/include)"

ifeq ($(DEBUG), 0)
  N64_CFLAGS += -Oz -finline-functions-called-once
endif
N64_CXXFLAGS := $(N64_CFLAGS)
N64_CFLAGS   += -std=gnu17
N64_CXXFLAGS += -std=gnu++20 -DB2_USER_SETTINGS

N64_MKASSET ?= $(N64_BINDIR)/mkasset

SCRIPT_GENASSETIDS = tools/genassetids.py
SCRIPT_TILETOOL = tools/tiletool.py
SCRIPT_MAPTOOL = tools/maptool.py
SCRIPT_DEPS = tools/util.py

AUDIOCONV_FLAGS ?= --wav-compress 1
MKSPRITE_FLAGS ?=
MKFONT_FLAGS ?=
MKMODEL_FLAGS ?=
MKASSET_FLAGS ?= -c 2
TILETOOL_FLAGS ?=
MAPTOOL_FLAGS ?=

SOURCES = actors.c cache.c enemy.cpp main.c map.c menu.c misc.cpp player.cpp \
          render.c script.c sound.c util.c world.cpp

SOURCES := $(addprefix $(SOURCE_DIR)/,$(SOURCES)) $(BUILD_DIR)/assets.c
OBJS := $(patsubst %.c,%.o,$(filter %.c,$(SOURCES))) \
        $(patsubst %.cpp,%.o,$(filter %.cpp,$(SOURCES)))
OBJS := $(OBJS:$(SOURCE_DIR)/%=$(BUILD_DIR)/%)

ASSETS_C = $(BUILD_DIR)/assets.c
ASSETS_H = $(ASSETS_C:%.c=%.h)

assets_wav = $(wildcard assets/*/*.wav)
assets_xm = $(wildcard assets/*/*.xm)
assets_png = $(wildcard assets/*/*.png)
assets_ttf = $(wildcard assets/*/*.ttf)
assets_gltf = $(wildcard assets/*/*.gltf)
assets_tsx = $(wildcard assets/actors/*.tsx) $(wildcard assets/bg/*.tsx) \
             $(wildcard assets/fx/*.tsx) $(wildcard assets/props/*.tsx) \
             $(wildcard assets/models/*.tsx)
assets_tmx = $(wildcard assets/*/*.tmx)
assets_conv = $(addprefix filesystem/,$(assets_wav:assets/%.wav=%.wav64)) \
              $(addprefix filesystem/,$(assets_xm:assets/%.xm=%.xm64)) \
              $(addprefix filesystem/,$(assets_png:assets/%.png=%.sprite)) \
              $(addprefix filesystem/,$(assets_ttf:assets/%.ttf=%.font64)) \
              $(addprefix filesystem/,$(assets_gltf:assets/%.gltf=%.model64)) \
              $(addprefix filesystem/,$(assets_tsx:assets/%.tsx=%.tiles)) \
              $(addprefix filesystem/,$(assets_tmx:assets/%.tmx=%.map))

ifeq ($(V), 1)
	AUDIOCONV_FLAGS += -v
	MKSPRITE_FLAGS += -v
	MKFONT_FLAGS += -v
	MKMODEL_FLAGS += -v
	MKASSET_FLAGS += -v
	TILETOOL_FLAGS += -v
	MAPTOOL_FLAGS += -v
endif

BOX2D_LIB = $(BUILD_DIR)/box2d/bin/libbox2d.a

all: aliensun.z64

filesystem/%.wav64: assets/%.wav
	@mkdir -p "$(dir $@)"
	@echo "    [AUDIO]  $@"
	@$(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o "$(dir $@)" "$<"

filesystem/%.xm64: assets/%.xm
	@mkdir -p "$(dir $@)"
	@echo "    [MUSIC]  $@"
	@$(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o "$(dir $@)" "$<"

filesystem/fg/water-t1.sprite: MKSPRITE_FLAGS += -f RGBA32
filesystem/fg/water-t2.sprite: MKSPRITE_FLAGS += -f RGBA32
filesystem/fg/water-t3.sprite: MKSPRITE_FLAGS += -f RGBA32
filesystem/fg/lava-3.sprite: MKSPRITE_FLAGS += -f RGBA32
filesystem/ui/aliensun.sprite: MKSPRITE_FLAGS += -f RGBA32

filesystem/%.sprite: assets/%.png
	@mkdir -p "$(dir $@)"
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o "$(dir $@)" "$<"

filesystem/fonts/STV5730A.font64:  MKFONT_FLAGS += -s 14
filesystem/fonts/pixeltype.font64: MKFONT_FLAGS += -s 16
filesystem/fonts/blocktopia.font64: MKFONT_FLAGS += -s 16

filesystem/%.font64: assets/%.ttf
	@mkdir -p "$(dir $@)"
	@echo "    [FONT]  $@"
	@$(N64_MKFONT) $(MKFONT_FLAGS) -o "$(dir $@)" "$<"

filesystem/%.model64: assets/%.gltf
	@mkdir -p "$(dir $@)"
	@echo "    [MODEL]  $@"
	@$(N64_MKMODEL) $(MKMODEL_FLAGS) -o "$(dir $@)" "$<"

filesystem/%.tiles: assets/%.tsx $(SCRIPT_TILETOOL) $(SCRIPT_DEPS)
	@mkdir -p "$(dir $@)"
	@echo "    [TILES]  $@"
	@$(SCRIPT_TILETOOL) $(TILETOOL_FLAGS) -o "$(dir $@)" "$<"
	@$(N64_MKASSET) $(MKASSET_FLAGS) -o "$(dir $@)" "$@"

filesystem/%.map: assets/%.tmx $(SCRIPT_MAPTOOL) $(ASSETS_C) src/actortypes.h src/scriptops.h $(SCRIPT_DEPS) tools/mapscriptparser.py
	@mkdir -p "$(dir $@)"
	@echo "    [MAP]    $@"
	@$(SCRIPT_MAPTOOL) $(MAPTOOL_FLAGS) -a $(ASSETS_C) -t src/actortypes.h -s src/scriptops.h -o "$(dir $@)" "$<"
	@$(N64_MKASSET) $(MKASSET_FLAGS) -o "$(dir $@)" "$@"

define genassetids
$(SCRIPT_GENASSETIDS) $(GENIDS_FLAGS) sfx $(patsubst %.wav,%.wav64,$(assets_wav:assets/%=%));
$(SCRIPT_GENASSETIDS) $(GENIDS_FLAGS) mus $(patsubst %.xm,%.xm64,$(assets_xm:assets/%=%));
$(SCRIPT_GENASSETIDS) $(GENIDS_FLAGS) gfx $(patsubst %.png,%.sprite,$(assets_png:assets/%=%));
$(SCRIPT_GENASSETIDS) $(GENIDS_FLAGS) model $(patsubst %.gltf,%.model64,$(assets_gltf:assets/%=%));
$(SCRIPT_GENASSETIDS) $(GENIDS_FLAGS) tileset $(patsubst %.tsx,%.tiles,$(assets_tsx:assets/%=%));
$(SCRIPT_GENASSETIDS) $(GENIDS_FLAGS) maps $(patsubst %.tmx,%.map,$(assets_tmx:assets/%=%));
endef

$(shell mkdir -p "$(BUILD_DIR)")

$(file > $(ASSETS_C).tmp,#include "assets.h")
GENIDS_FLAGS = $(ASSETS_C).tmp
$(shell $(genassetids))
ifneq ($(strip $(file < $(ASSETS_C))),$(strip $(file < $(ASSETS_C).tmp)))
  $(file > $(ASSETS_C),$(file < $(ASSETS_C).tmp))
  $(file > $(ASSETS_H),)
  GENIDS_FLAGS = $(ASSETS_H) -H
  $(shell $(genassetids))
endif
$(shell rm $(ASSETS_C).tmp)

BOX2D_CXXFLAGS := $(filter-out -Wall -Werror,$(N64_CXXFLAGS)) -include fmath.h

$(BOX2D_LIB):
	@cmake -B build/box2d subprojects/box2d --toolchain ../../n64-toolchain.cmake \
		-DBOX2D_BUILD_UNIT_TESTS=OFF -DBOX2D_BUILD_TESTBED=OFF -DBOX2D_USER_SETTINGS=ON \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_CXX_FLAGS_RELWITHDEBINFO="$(BOX2D_CXXFLAGS)"
	@cmake --build build/box2d -- -s

$(BUILD_DIR)/aliensun.dfs: $(assets_conv)
	@mkdir -p $(dir $@)
	@echo "    [DFS] $@"
	-@rm $(filter-out $(assets_conv),$(shell find filesystem -type f)) 2>/dev/null
	@$(N64_MKDFS) $@ filesystem > /dev/null

$(BUILD_DIR)/aliensun.elf: $(BOX2D_LIB) $(OBJS) $(N64_LIBDIR)/libdragon.a $(N64_LIBDIR)/libdragonsys.a $(N64_LIBDIR)/n64.ld
	@mkdir -p $(dir $@)
	@echo "    [LD] $@"
	EXTERNS_FILE="$(filter %.externs, $^)"; \
	if [ -z "$$EXTERNS_FILE" ]; then \
		$(CXX) -o $@ $(filter %.o, $^) $(BOX2D_LIB) -lc $(patsubst %,-Wl$(COMMA)%,$(LDFLAGS)) -Wl,-Map=$(BUILD_DIR)/$(notdir $(basename $@)).map; \
	else \
		$(CXX) -o $@ $(filter %.o, $^) $(BOX2D_LIB) -lc $(patsubst %,-Wl$(COMMA)%,$(LDFLAGS)) -Wl,-T"$$EXTERNS_FILE" -Wl,-Map=$(BUILD_DIR)/$(notdir $(basename $@)).map; \
	fi
	$(N64_SIZE) -G $@

aliensun.z64: N64_ROM_TITLE="Alien Sun"
aliensun.z64: N64_ROM_REGIONFREE=1
aliensun.z64: $(BUILD_DIR)/summer.dfs

clean:
	@echo "    [CLEAN]"
	@rm -rf $(BUILD_DIR) filesystem aliensun.z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean $(BOX2D_LIB)
