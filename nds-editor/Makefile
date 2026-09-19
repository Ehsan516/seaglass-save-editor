#---------------------------------------------------------------------------------
# Seaglass Save Editor (DS/DSi) -- devkitARM ds_rules Makefile.
# Runs under TWiLight Menu++ (or any NDS/DSi homebrew loader) on original DS,
# DS Lite, DSi, and 3DS/2DS in DS mode.
#
# Shares the engine (seaglass_core.c/.h, g3_tables.h) directly with the 3DS
# build via ../3ds-editor/source -- do not fork a third copy of the engine;
# see CLAUDE.md's golden rule. main.c owns the DS platform; app.c/ui.c the UI.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules

# TOPDIR stays the project root even in the recursive sub-make (whose own
# CURDIR is build/) because it's exported and re-read with ?= there -- same
# trick as 3ds-editor/Makefile's APP_ICON fix. Without it, GAME_ICON below
# would resolve to build/icon.bmp during the actual link step and fail.
TOPDIR ?= $(CURDIR)

TARGET      := SeaglassSaveEditor
BUILD       := build
# Only source/ is globbed for .c files -- the 3DS folder is a header/engine-only
# dependency here (it also contains its own main.c, which must NOT be picked up).
SOURCES     := source
ENGINE_DIR  := ../3ds-editor/source
INCLUDES    := source $(ENGINE_DIR)

GAME_TITLE     := Seaglass Save Editor
GAME_SUBTITLE1 := Edit Emerald Seaglass saves
GAME_SUBTITLE2 := on DS/DSi via TWiLight Menu++
GAME_ICON      := $(TOPDIR)/icon.bmp

ARCH   := -march=armv5te -mtune=arm946e-s -mthumb
CFLAGS := -g -Wall -O2 -ffunction-sections -fdata-sections $(ARCH)
CFLAGS += $(INCLUDE) -DARM9 -D__NDS__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lmm9 -lfat -lnds9
LIBDIRS  := $(LIBNDS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export TOPDIR   := $(TOPDIR)
export OUTPUT   := $(CURDIR)/$(TARGET)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(CURDIR)/$(ENGINE_DIR)
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))) seaglass_core.c
export LD := $(CC)
export OFILES   := $(CFILES:.c=.o)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export GAME_TITLE GAME_SUBTITLE1 GAME_SUBTITLE2 GAME_ICON

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds $(TARGET).ds.gba

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).nds: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
