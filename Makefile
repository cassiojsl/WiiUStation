#---------------------------------------------------------------------------------
# WiiUStation - standalone PlayStation emulator for the Wii U (Aroma)
# PCSX-ReARMed core (vendor/pcsx_rearmed, static libretro build) + our frontend.
#
#   git submodule update --init --recursive
#   make            -> WiiUStation.rpx + WiiUStation.wuhb
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)

APP_NAME	:= WiiUStation
APP_SHORTNAME	:= WiiUStation
APP_AUTHOR	:= WiiUStation (PCSX-ReARMed core)
APP_ICON	:= $(TOPDIR)/meta/icon.png
APP_TV_SPLASH	:= $(TOPDIR)/meta/splash_tv.png
APP_DRC_SPLASH	:= $(TOPDIR)/meta/splash_drc.png
APP_CONTENT	:= $(TOPDIR)/content

include $(DEVKITPRO)/wut/share/wut_rules

TARGET		:=	WiiUStation
BUILD		:=	build
SOURCES		:=	source
CORE_DIR	:=	$(TOPDIR)/vendor/pcsx_rearmed
CORE_LIB	:=	$(CORE_DIR)/pcsx_rearmed_libretro_wiiu.a
CORE_PATCHES	:=	$(sort $(wildcard $(TOPDIR)/patches/*.patch))

# Core build options:
#  - Lightrec dynarec (default for platform=wiiu), code buffer = codegen area
#  - gpulib + P.E.Op.S. soft renderer (big-endian safe; the official Wii/Wii U
#    builds use it too), optional threaded GPU and SPU (cores 2 and 0)
CORE_FLAGS	:=	platform=wiiu USE_ASYNC_GPU=1 USE_ASYNC_SPU=1 BUILTIN_GPU=peops
CORE_CC		:=	$(DEVKITPPC)/bin/powerpc-eabi-gcc -DWIIU -I$(DEVKITPRO)/wut/include \
			-Wno-error=int-conversion -Wno-error=incompatible-pointer-types \
			-Wno-error=implicit-function-declaration

# version shown in the app and compared with GitHub releases: the git tag
# (v1.2.0), or tag-commits-hash between releases
APP_VERSION	?=	$(shell git describe --tags --always 2>/dev/null || echo dev)
export APP_VERSION

CFLAGS		:=	-std=gnu11 -O2 -Wall -Wno-unused -ffunction-sections -fdata-sections \
			$(MACHDEP) -D__WIIU__ $(INCLUDE) -DAPP_VERSION=\"$(APP_VERSION)\"
CXXFLAGS	:=	$(CFLAGS)
LDFLAGS		=	$(ARCH) $(RPXSPECS) $(MACHDEP) -Wl,--gc-sections -Wl,-Map,$(notdir $*.map)

LIBS		:=	$(CORE_LIB) -lSDL2_image -lwebpdemux -lwebp -lsharpyuv -ljpeg \
			-lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lSDL2_ttf -lfreetype -lharfbuzz -lfreetype -lpng -lbz2 \
			-lbrotlidec -lbrotlicommon -lz -lSDL2 -lwut -lm
LIBDIRS		:=	$(PORTLIBS) $(WUT_ROOT)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES	:=	$(CFILES:.c=.o)
export LD	:=	$(CC)
export INCLUDE	:=	$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(PORTLIBS_PATH)/wiiu/include/SDL2 \
			-I$(CURDIR)/source -I$(CORE_DIR)/deps/libretro-common/include -I$(CORE_DIR)/deps/libchdr/include
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean core $(BUILD)

all: core $(BUILD)

# Apply our patches to the core once (stamp in the build dir), then build it.
core:
	@[ -f $(CORE_DIR)/Makefile.libretro ] || { echo "vendor/pcsx_rearmed missing: run git submodule update --init --recursive"; exit 1; }
	@for p in $(CORE_PATCHES); do \
		if git -C $(CORE_DIR) apply --reverse --check $$p 2>/dev/null; then :; \
		else echo "applying $$(basename $$p)"; git -C $(CORE_DIR) apply $$p || exit 1; fi; \
	done
	@$(MAKE) --no-print-directory -C $(CORE_DIR) -f Makefile.libretro $(CORE_FLAGS) CC="$(CORE_CC)"

$(BUILD): core
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).rpx $(TARGET).wuhb $(TARGET).elf
	@$(MAKE) --no-print-directory -C $(CORE_DIR) -f Makefile.libretro platform=wiiu clean

#---------------------------------------------------------------------------------
else
DEPENDS	:=	$(OFILES:.o=.d)

all	:	$(OUTPUT).wuhb
$(OUTPUT).wuhb	:	$(OUTPUT).rpx
$(OUTPUT).rpx	:	$(OUTPUT).elf
$(OUTPUT).elf	:	$(OFILES) $(CORE_LIB)

-include $(DEPENDS)
#---------------------------------------------------------------------------------
endif
