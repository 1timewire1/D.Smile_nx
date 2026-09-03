#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Switch homebrew port of D.Smile. Bring-up scope only: boot game.bin with
# graphics/logic/sound working, nothing else (no menu, no save states, no
# settings screen yet - see switch/README.md).
#
# The emulation core (../app/src/main/cpp/core) is pulled in unmodified - it
# has zero Android/JNI dependencies (already proven by test/host_test.cpp,
# which builds the same sources standalone on desktop). Everything under
# switch/source/ is this platform's own glue: EGL/GL setup, audrv audio,
# pad input - modeled on reference/Yokoi_Game_-_Watch_emulator_3ds-main's
# switch/ port, which is confirmed working on real hardware with the same
# devkitA64 + libnx + switch-glad + switch-mesa toolchain.
#---------------------------------------------------------------------------------
TARGET		:=	dsmile_switch
BUILD		:=	build
SOURCES		:=	source source/core source/render source/audio source/input source/third_party \
			../app/src/main/cpp/core
DATA		:=	data
INCLUDES	:=	source ../app/src/main/cpp/core

APP_TITLE		:=	D.Smile NX
APP_AUTHOR		:=	SenseiFromClubPenguin
APP_VERSION		:=	1.0.0

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# -O3 (not -O2) actually matches the core's own CMakeLists.txt now - a
# previous revision of this comment claimed that was already true when it
# wasn't. -flto=auto lets the compiler inline across translation units, which
# matters a lot here specifically: UnSP::Step() (unsp.cpp) calls
# Spg200::Read()/Write() (spg200.cpp) on every instruction fetch and operand
# access - the single hottest call in the whole emulator - and without LTO
# that's a real, non-inlinable function call every time purely because it
# lives in a different .cpp file, not for any correctness reason. Needed on
# both the compile flags (CFLAGS/CXXFLAGS) and the link flags (LDFLAGS)
# below - GCC's LTO backend runs at link time.
CFLAGS	:=	-g -Wall -O3 -flto=auto -ffunction-sections \
			$(ARCH) $(DEFINES)

# freetype2's headers live one level under $(PORTLIBS)/include (not picked up
# by the generic -I$(PORTLIBS)/include below), and pkg-config isn't usable
# from a plain (non-devkitPro-MSYS2) shell, so it's hardcoded here - same
# situation the Yokoi reference port's Makefile documents.
CFLAGS	+=	$(INCLUDE) -I$(PORTLIBS)/include/freetype2 -D__SWITCH__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++20

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -flto=auto -Wl,-Map,$(notdir $*.map)

# freetype/harfbuzz have a circular dependency (freetype's autohinter can use
# harfbuzz; harfbuzz uses freetype) - wrapped in --start-group/--end-group so
# the static linker can resolve both directions regardless of link order.
# FreeType (+ its harfbuzz/png/zlib/bz2 chain) is only for the ROM browser's
# text (switch_ui.cpp) - the in-game renderer doesn't need it.
LIBS	:= -lglad -lEGL -lglapi -ldrm_nouveau \
			-Wl,--start-group -lfreetype -lharfbuzz -lpng16 -lz -lbz2 -Wl,--end-group \
			-lnx

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all	:	$(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
else
$(OUTPUT).nro	:	$(OUTPUT).elf
endif

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
