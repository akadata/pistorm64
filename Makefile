# Makefile — PiStorm emulator
#
# Fixes applied:
#  - Prevent 0-byte/partial binaries on failed or interrupted link:
#      * .DELETE_ON_ERROR
#      * atomic link (link to $@.tmp then mv on success)
#  - Avoid linking headers as inputs (m68kops.h no longer appears in link line)
#  - Keep generated Musashi files ordered correctly
#  - Preserve the original "nice" MAINFILES style (MAINFILES += per-line) so
#    lines can be commented out easily.
#
# Usage examples:
#   make
#   make clean; make
#   make USE_ALSA=0
#   make USE_RAYLIB=0
#   make PLATFORM=ZEROW2_64

# Build defaults live in config.mk (override there or via make VAR=...)

# Default compilers (can still override with CC=... directly)
CC  ?= gcc
CXX ?= g++
AR  ?= ar

# Legacy shorthand: make C=clang or make C=gcc
ifeq ($(C),clang)
CC  := clang
CXX := clang++
endif

ifeq ($(C),gcc)
CC  := gcc
CXX := g++
endif


#EXTRA_CFLAGS ?= -g -O0
#EXTRA_M68K_CFLAGS ?= -g -O0
#EXTRA_LDFLAGS ?= -g -O0


# Tunables: edit here instead of hunting through rule bodies.
# WARNINGS   : compiler warnings; keep strict by default.
# OPT_LEVEL  : optimisation level (-Os/-O2/-O3). Can also set O=2,3,...
# USE_GOLD   : set to 1 to prefer gold linker (if installed).
# USE_RAYLIB : set to 0 to drop raylib/DRM deps and use a null RTG backend.
# USE_ALSA   : set to 0 to drop ALSA/ahi builds and -lasound.
# USE_PMMU   : set to 1 to enable Musashi PMMU support (experimental).
# USE_EC_FPU : set to 1 to force FPU on EC/020/LC/EC040 variants (for 68881/68882 emu).
# ARCH_FEATURES : optional AArch64 feature modifiers (e.g. +crc+simd+fp16+lse).
# CPUFLAGS   : per-platform tuning defaults below; override if needed.
# RAYLIB_*   : raylib include/lib paths; adjust for custom builds.
# USE_VC     : legacy (no longer required for pistorm-dev).
# USE_LTO    : set to 1 to enable link-time optimisation (-flto) on build and link.
# USE_NO_PLT : set to 1 to pass -fno-plt for direct calls (glibc-specific; default off).
# OMIT_FP    : set to 1 to omit frame pointers (-fomit-frame-pointer) for perf.
# USE_PIPE   : set to 1 to add -pipe to compile steps.
# M68K_WARN_SUPPRESS : extra warning suppressions for the generated Musashi core.
#
# Base warnings
VERBOSE ?= 0
WARNINGS_VERBOSE ?= -Wall -Wextra -pedantic
WARNINGS_QUIET   ?= -Wall
ifeq ($(VERBOSE),1)
WARNINGS ?= $(WARNINGS_VERBOSE)
else
WARNINGS ?= $(WARNINGS_QUIET)
endif

# Extra-aggressive warnings for emulator / non-Musashi code
ifeq ($(VERBOSE),1)
EMU_WARNINGS_EXTRA = \
  -Wformat=2 -Wwrite-strings -Wcast-qual -Wcast-align \
  -Wpointer-arith -Wstrict-overflow=5 -Wstrict-prototypes -Wmissing-prototypes \
  -Wswitch-enum -Wshadow \
  -Wconversion -Wsign-conversion \
  -Wundef -Wvla -Wredundant-decls
else
EMU_WARNINGS_EXTRA =
endif

EMU_WARNINGS  ?= $(WARNINGS) $(EMU_WARNINGS_EXTRA)

ifeq ($(findstring clang,$(CC)),)
OPT_LEVEL_DEFAULT ?= -O3 -ffast-math
else
OPT_LEVEL_DEFAULT ?= -O3 -ffast-math
endif

OPT_LEVEL ?= $(OPT_LEVEL_DEFAULT)

ifdef O
OPT_LEVEL := -O$(O)
endif

# Set USE_GOLD=1 to link with gold if available.
USE_GOLD   ?= 1

include config.mk

# Toggle RTG output backends: 1=raylib (default), 0=null stub.
USE_RAYLIB ?= 1
# Toggle ALSA-based audio (Pi AHI). If 0, drop pi_ahi and -lasound.
USE_ALSA   ?= 1

# Toggle PMMU emulation (68030/040). Default on; disable with USE_PMMU=0 if needed.
USE_PMMU   ?= 1

# Optional: build UAE/JIT objects (AArch64 JIT backend from Amiberry).
# This does not replace Musashi in the main emulator yet; it builds a standalone
# libuae.a for bring-up and integration work.
USE_UAE_JIT ?= 1

# Force FPU on EC/020/EC040/LC040 for 68881/68882 emulation (optional).
USE_EC_FPU ?= 0

ARCH_FEATURES ?=
# Toggle Pi host (/opt/vc) support for dev tools.
USE_VC     ?= 0
# Perf toggles
USE_LTO    ?= 1
USE_NO_PLT ?= 1 
OMIT_FP    ?= 1
USE_PIPE   ?= 1
# Keep loop transforms conservative while debugging JIT bring-up.
NO_UNROLL_FLAGS ?= -fno-unroll-loops

# Quiet noisy-but-benign warnings from the generated 68k core.
# Split into common + GCC-only; clang doesn't support every GCC flag.
M68K_WARN_SUPPRESS_COMMON = \
  -Wno-unused-variable \
  -Wno-unused-parameter

M68K_WARN_SUPPRESS_GCC = \
  -Wno-unused-but-set-variable

# Pick suppressions based on the actual compiler (CC), not C.
ifeq ($(findstring clang,$(CC)),clang)
  M68K_WARN_SUPPRESS ?= $(M68K_WARN_SUPPRESS_COMMON)
else
  M68K_WARN_SUPPRESS ?= $(M68K_WARN_SUPPRESS_COMMON) $(M68K_WARN_SUPPRESS_GCC)
endif

# Default CPU flags; overridden by PLATFORM selections below.
CPUFLAGS   ?= -march=armv8-a+crc -mtune=cortex-a53

# Raylib paths can be swapped if you use a custom build.
#RAYLIB_INC    ?= -I./src/raylib_drm #raylib
#RAYLIB_LIBDIR ?= -L./src/raylib_drm #raylib_drm

RAYLIB_DIR := $(CURDIR)/src/raylib_drm
RAYLIB_INC := -I$(RAYLIB_DIR)/src
RAYLIB_LIB := $(RAYLIB_DIR)/build/raylib/libraylib.a

PREFIX        ?= /opt/pistorm64
DESTDIR       ?=
INSTALL       ?= install
AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_VBCC ?= $(AMIGA_TOOLCHAIN)/vbcc
AMIGA_P96DEV ?= $(CURDIR)/amiga.dev/Picasso96Develop
AMIGA_AHI_INC ?= $(AMIGA_TOOLCHAIN)/src/m68k-amigaos-gcc/build-Linux-m68k-amigaos/vbcc_target_m68k-amigaos/targets/m68k-amigaos/include
AMIGA_HEADERS ?= $(CURDIR)/src/platforms/amiga/headers/include
AMIGA_SUBMAKE = $(MAKE) AMIGA_TOOLCHAIN=$(AMIGA_TOOLCHAIN) VBCC=$(AMIGA_VBCC) P96DEV=$(AMIGA_P96DEV) AHI_INC=$(AMIGA_AHI_INC) AMIGA_HEADERS=$(AMIGA_HEADERS)

PISTORM_GPCLK_SRC ?= 5
PISTORM_GPCLK_DIV ?= 6
PISTORM_KMOD_PARAMS ?= run_batch_enable=0 berr_reset_input=0 gpclk_src=$(PISTORM_GPCLK_SRC) gpclk_div=$(PISTORM_GPCLK_DIV)

PS_PROTOCOL_SRC := src/gpio/ps_protocol_kmod.c


MAINFILES =

MAINFILES += src/emulator.c
# TEST DOES FC EMIT 
MAINFILES += src/emulator_fc.c

MAINFILES += src/log.c
MAINFILES += src/memory_mapped.c


MAINFILES += src/config_file/config_file.c
MAINFILES += src/config_file/rominfo.c

MAINFILES += src/input/input.c
MAINFILES += $(PS_PROTOCOL_SRC)

MAINFILES += src/platforms/platforms.c
MAINFILES += src/z3bus_iface.c
MAINFILES += src/platforms/amiga/amiga_zorro.c
MAINFILES += src/platforms/amiga/zorro/z3bus_demo/z3bus_demo.c
MAINFILES += src/platforms/amiga/zorro/serial_echo/serial_echo.c
MAINFILES += src/platforms/amiga/zorro/z2_rng/z2_rng.c
MAINFILES += src/platforms/amiga/zorro/z2_pissa/z2_pissa.c
MAINFILES += src/host/crypto/pi_crypto_openssl.c

MAINFILES += src/platforms/amiga/amiga-autoconf.c
MAINFILES += src/platforms/amiga/amiga-platform.c
MAINFILES += src/platforms/amiga/amiga-registers.c
MAINFILES += src/platforms/amiga/amiga-interrupts.c

MAINFILES += src/platforms/mac68k/mac68k-platform.c

MAINFILES += src/platforms/dummy/dummy-platform.c
MAINFILES += src/platforms/dummy/dummy-registers.c

MAINFILES += src/platforms/amiga/Gayle.c
MAINFILES += src/platforms/amiga/hunk-reloc.c
MAINFILES += src/platforms/amiga/cdtv-dmac.c

MAINFILES += src/platforms/amiga/rtg/rtg.c
MAINFILES += src/platforms/amiga/rtg/rtg-output-raylib.c
MAINFILES += src/platforms/amiga/rtg/rtg-gfx.c

MAINFILES += src/platforms/amiga/piscsi/piscsi.c
MAINFILES += src/platforms/amiga/net/pi-net.c

MAINFILES += src/platforms/shared/rtc.c
MAINFILES += src/platforms/shared/common.c

# self-tests
MAINFILES += src/selftest.c


ifeq ($(USE_RAYLIB),0)
MAINFILES := $(filter-out src/platforms/amiga/rtg/rtg-output-raylib.c,$(MAINFILES))
MAINFILES += src/platforms/amiga/rtg/rtg-output-null.c
endif

ifeq ($(USE_ALSA),0)
MAINFILES := $(filter-out src/platforms/amiga/ahi/pi_ahi.c,$(MAINFILES))
MAINFILES += src/platforms/amiga/ahi/pi_ahi_stub.c
LDLIBS_ALSA :=
else
MAINFILES := $(filter-out src/platforms/amiga/ahi/pi_ahi_stub.c,$(MAINFILES))
MAINFILES += src/platforms/amiga/ahi/pi_ahi.c
LDLIBS_ALSA := -lasound
endif


# PiStorm-dev now uses sysfs and no longer depends on /opt/vc.
MAINFILES := $(filter-out src/platforms/amiga/pistorm-dev/pistorm-dev-stub.c,$(MAINFILES))
MAINFILES += src/platforms/amiga/pistorm-dev/pistorm-dev.c
VC_INC    :=
VC_LIBDIR :=
LDLIBS_VC :=

ifeq ($(USE_PMMU),1)
DEFINES += -DPISTORM_EXPERIMENT_PMMU
endif

ifeq ($(USE_EC_FPU),1)
DEFINES += -DPISTORM_ENABLE_020_FPU -DPISTORM_ENABLE_EC040_FPU
endif

MUSASHIFILES     = src/musashi/m68kcpu.c src/musashi/m68kdasm.c src/musashi/softfloat/softfloat.c src/musashi/softfloat/softfloat_fpsp.c
MUSASHIGENCFILES = src/musashi/m68kops.c
MUSASHIGENHFILES = src/musashi/m68kops.h
MUSASHIGENERATOR = m68kmake

EXE =
EXEPATH = ./

# Define the m68k related files separately to control build order
M68KFILES = $(MUSASHIFILES) $(MUSASHIGENCFILES)
.CFILES   = $(MAINFILES) $(M68KFILES)
.OFILES   = $(.CFILES:%.c=%.o) src/a314/a314.o

# UAE/JIT build (optional, AArch64 only)
UAE_SRCDIR   := src/uae
UAE_BUILDDIR := build/uae
UAE_INCLUDES := -Isrc -I$(UAE_SRCDIR) -I$(UAE_SRCDIR)/include -I$(UAE_SRCDIR)/include/uae \
	-I$(UAE_SRCDIR)/machdep -I$(UAE_SRCDIR)/jit
UAE_C_SRCS   :=
UAE_CXX_SRCS :=
UAE_OBJS     :=
UAE_TARGET   := $(UAE_BUILDDIR)/libuae.a
UAE_FPP_NATIVE_IN := $(UAE_SRCDIR)/fpp_native.cpp.in
UAE_FPP_NATIVE_CPP := $(UAE_SRCDIR)/fpp_native.cpp
UAE_LINK_FLAGS :=
UAE_JIT_NO_PIE ?= 1
UAE_PIE_FLAGS := $(if $(filter 1,$(UAE_JIT_NO_PIE)),-fno-pie,)
# UAE objects must include DEFINES (USE_UAE_JIT) but stay non-LTO/static-friendly.
UAE_OPT_LEVEL ?= -O2
UAE_WARN_SUPPRESS = -Wno-unused-variable -Wno-unused-parameter -Wno-unused-but-set-variable \
	-Wno-sign-compare -Wno-misleading-indentation -Wno-format -Wno-int-to-pointer-cast
UAE_EXTRA_CFLAGS ?=
UAE_CFLAGS   = $(EMU_WARNINGS) $(UAE_OPT_LEVEL) $(CPUFLAGS) $(DEFINES) $(UAE_INCLUDES) \
	$(UAE_PIE_FLAGS) $(PLT_FLAGS) $(FP_FLAGS) $(PIPE_FLAGS) $(NO_UNROLL_FLAGS) \
	$(UAE_EXTRA_CFLAGS) $(NO_LTO_FLAGS) -fPIC
UAE_CXXFLAGS = $(CXX_WARNINGS) $(UAE_OPT_LEVEL) $(CPUFLAGS) $(DEFINES) $(UAE_INCLUDES) \
	$(UAE_PIE_FLAGS) $(PLT_FLAGS) $(FP_FLAGS) $(PIPE_FLAGS) $(NO_UNROLL_FLAGS) $(UAE_EXTRA_CFLAGS) \
	$(NO_LTO_FLAGS) -fpermissive $(UAE_WARN_SUPPRESS) -fPIC
EXTRA_CXX_OBJS :=
EXTRA_LINK_DEPS :=

ifeq ($(USE_UAE_JIT),1)
UAE_C_SRCS   := $(shell find $(UAE_SRCDIR) -type f -name "*.c")
UAE_CXX_SRCS := $(shell find $(UAE_SRCDIR) -type f \( -name "*.cc" -o -name "*.cpp" -o -name "*.cxx" \))
UAE_JIT_SRCS := \
	$(UAE_SRCDIR)/jit/codegen_armA64.cpp \
	$(UAE_SRCDIR)/jit/compemu_midfunc_armA64.cpp \
	$(UAE_SRCDIR)/jit/compemu_midfunc_armA64_2.cpp \
	$(UAE_SRCDIR)/jit/codegen_x86.cpp \
	$(UAE_SRCDIR)/jit/compemu_midfunc_x86.cpp \
	$(UAE_SRCDIR)/jit/gencomp.cpp
UAE_CXX_SRCS := $(filter-out \
	$(UAE_SRCDIR)/uae_emulator.cc \
	$(UAE_SRCDIR)/pistorm_uae_bridge.cc \
	$(UAE_SRCDIR)/pistorm_uae_stubs.cc \
	$(UAE_SRCDIR)/fpp.cc \
	$(UAE_SRCDIR)/fpp_native.cc,$(UAE_CXX_SRCS))
UAE_CXX_SRCS := $(sort $(UAE_CXX_SRCS) $(UAE_JIT_SRCS) $(UAE_FPP_NATIVE_CPP))
UAE_OBJS := $(patsubst $(UAE_SRCDIR)/%.c,$(UAE_BUILDDIR)/%.o,$(UAE_C_SRCS))
UAE_OBJS += $(patsubst $(UAE_SRCDIR)/%.cc,$(UAE_BUILDDIR)/%.o,$(filter %.cc,$(UAE_CXX_SRCS)))
UAE_OBJS += $(patsubst $(UAE_SRCDIR)/%.cpp,$(UAE_BUILDDIR)/%.o,$(filter %.cpp,$(UAE_CXX_SRCS)))
UAE_OBJS += $(patsubst $(UAE_SRCDIR)/%.cxx,$(UAE_BUILDDIR)/%.o,$(filter %.cxx,$(UAE_CXX_SRCS)))
EXTRA_CXX_OBJS += src/uae/pistorm_uae_bridge.o src/uae/pistorm_uae_stubs.o
EXTRA_LINK_DEPS += $(UAE_TARGET)
UAE_LINK_FLAGS := -Wl,--whole-archive $(UAE_TARGET) -Wl,--no-whole-archive
DEFINES += -DUSE_UAE_JIT
endif

CC  ?= gcc
CXX ?= g++

DEFINES  += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1
# Allow command-line override of batching and rate limiting for performance tuning
PISTORM_USE_DIRECT_OPS ?= 0
DEFINES  += -DPISTORM_ENABLE_BATCH=$(PISTORM_ENABLE_BATCH) -DPISTORM_IPL_RATELIMIT_US=$(PISTORM_IPL_RATELIMIT_US) -DPISTORM_USE_DIRECT_OPS=$(PISTORM_USE_DIRECT_OPS)
DEFINES  += -DRTG_GFX_MEM=$(RTG_GFX_MEM) -DRTG_WIDTH=$(RTG_WIDTH) -DRTG_HEIGHT=$(RTG_HEIGHT)
LD_GOLD   = $(if $(filter 1,$(USE_GOLD)),-fuse-ld=gold,)
LTO_FLAGS = $(if $(filter 1,$(USE_LTO)),-flto=auto,)
NO_LTO_FLAGS = $(if $(filter 1,$(USE_LTO)),-fno-lto,)
PLT_FLAGS = $(if $(filter 1,$(USE_NO_PLT)),-fno-plt,)
FP_FLAGS  = $(if $(filter 1,$(OMIT_FP)),-fomit-frame-pointer,)
PIPE_FLAGS= $(if $(filter 1,$(USE_PIPE)),-pipe,)

# Platform-specific tuning and raylib variants.
ifeq ($(PLATFORM),PI4)
CPUFLAGS = -mcpu=cortex-a72 -mtune=cortex-a72 -march=armv8-a+crc -mfpu=neon-fp-armv8 -mfloat-abi=hard
RAYLIB_DIR := $(CURDIR)/src/raylib_drm
RAYLIB_INC := -I$(RAYLIB_DIR)/src
RAYLIB_LIB := $(RAYLIB_DIR)/build/raylib/libraylib.a
DEFINES      += -DRPI4_TEST
else ifeq ($(PLATFORM),PI4_64BIT)
CPUFLAGS = -mcpu=cortex-a72 -mtune=cortex-a72 -march=armv8-a+crc
RAYLIB_DIR := $(CURDIR)/src/raylib_drm
RAYLIB_INC := -I$(RAYLIB_DIR)/src
RAYLIB_LIB := $(RAYLIB_DIR)/build/raylib/libraylib.a
DEFINES      += -DRPI4_TEST

else ifeq ($(PLATFORM),PI4_64BIT_NATIVE)
CPUFLAGS = -mcpu=native -mtune=native -march=native
RAYLIB_DIR := $(CURDIR)/src/raylib_drm
RAYLIB_INC := -I$(RAYLIB_DIR)/src
RAYLIB_LIB := $(RAYLIB_DIR)/build/raylib/libraylib.a
DEFINES      += -DRPI4_TEST

else ifeq ($(PLATFORM),PI4_NATIVE)
CPUFLAGS = -march=native
RAYLIB_DIR := $(CURDIR)/src/raylib_drm
RAYLIB_INC := -I$(RAYLIB_DIR)/src
RAYLIB_LIB := $(RAYLIB_DIR)/build/raylib/libraylib.a
DEFINES      += -DRPI4_TEST
else ifeq ($(PLATFORM),PI4_64BIT_DEBUG)
CPUFLAGS = -mcpu=cortex-a72 -mtune=cortex-a72 -march=armv8-a+crc
RAYLIB_DIR := $(CURDIR)/src/raylib_drm
RAYLIB_INC := -I$(RAYLIB_DIR)/src
RAYLIB_LIB := $(RAYLIB_DIR)/build/raylib/libraylib.a
DEFINES      += -DRPI4_TEST
OPT_LEVEL := -O0 
EXTRA_CFLAGS += -fno-omit-frame-pointer
EXTRA_LDFLAGS += 
else ifeq ($(PLATFORM),PI3_BULLSEYE)
CPUFLAGS = -mcpu=cortex-a53 -mtune=cortex-a53 -march=armv8-a+crc
else ifeq ($(PLATFORM),PI_64BIT)
CPUFLAGS = -mcpu=cortex-a53 -mtune=cortex-a53 -march=armv8-a+crc
else ifeq ($(PLATFORM),ZEROW2_64)
CPUFLAGS = -mcpu=cortex-a53 -mtune=cortex-a53 -march=armv8-a+crc
else ifeq ($(PLATFORM),NATIVE) 
CPUFLAGS = -march=native
endif

# Optional manual overrides for CPU tuning.
ifdef MARCH
CPUFLAGS := $(filter-out -march=%,$(CPUFLAGS)) -march=$(MARCH)
endif
ifdef MCPU
CPUFLAGS := $(filter-out -mcpu=%,$(CPUFLAGS)) -mcpu=$(MCPU)
endif
ifdef MTUNE
CPUFLAGS := $(filter-out -mtune=%,$(CPUFLAGS)) -mtune=$(MTUNE)
endif

# Optional AArch64 feature modifiers (e.g. +crc+simd+fp16+lse). Leave blank to keep defaults.
ifneq ($(strip $(ARCH_FEATURES)),)
CPUFLAGS := $(patsubst -march=%,-march=%$(ARCH_FEATURES),$(CPUFLAGS))
CPUFLAGS := $(patsubst -mcpu=%,-mcpu=%$(ARCH_FEATURES),$(CPUFLAGS))
endif


RAYLIB_A := $(RAYLIB_LIB)
OPENSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
OPENSSL_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)

BASE_LIBS := -lm -ldl -lstdc++
ifneq ($(OPENSSL_LIBS),)
BASE_LIBS += $(OPENSSL_LIBS)
INCLUDES += $(OPENSSL_CFLAGS)
endif
RAYLIB_LIBS := $(RAYLIB_A) -lEGL -lGLESv2 -ldrm -lgbm
ifeq ($(USE_RAYLIB),0)
RAYLIB_LIBS :=
RAYLIB_INC    =
RAYLIB_LIBDIR =
endif

LIBS := $(BASE_LIBS)

INCLUDES  = -I. -Isrc -Isrc/musashi $(RAYLIB_INC) $(VC_INC)
LDSEARCH  = -L/usr/local/lib $(VC_LIBDIR) $(RAYLIB_LIBDIR)

ifeq ($(PISTORM_KMOD),1)
INCLUDES += -Iinclude -Iinclude/uapi
DEFINES  += -DPISTORM_KMOD
endif
ifeq ($(USE_UAE_JIT),1)
INCLUDES += $(UAE_INCLUDES)
endif

CXX_WARNINGS = $(filter-out -Wstrict-prototypes -Wmissing-prototypes,$(EMU_WARNINGS))
CFLAGS       = $(EMU_WARNINGS) $(OPT_LEVEL) $(CPUFLAGS) $(DEFINES) $(INCLUDES) $(ACFLAGS) $(LTO_FLAGS) $(PLT_FLAGS) $(FP_FLAGS) $(PIPE_FLAGS) $(NO_UNROLL_FLAGS) $(EXTRA_CFLAGS)
CXXFLAGS     = $(CXX_WARNINGS) $(OPT_LEVEL) $(CPUFLAGS) $(DEFINES) $(INCLUDES) $(LTO_FLAGS) $(PLT_FLAGS) $(FP_FLAGS) $(PIPE_FLAGS) $(NO_UNROLL_FLAGS) $(EXTRA_CFLAGS)
M68K_CFLAGS   = $(WARNINGS) $(OPT_LEVEL) $(CPUFLAGS) $(DEFINES) $(INCLUDES) $(ACFLAGS) $(LTO_FLAGS) $(PLT_FLAGS) $(FP_FLAGS) $(PIPE_FLAGS) $(NO_UNROLL_FLAGS) $(M68K_WARN_SUPPRESS) $(EXTRA_M68K_CFLAGS)
LDFLAGS      = $(WARNINGS) $(LD_GOLD) $(LDSEARCH) $(LTO_FLAGS) $(EXTRA_LDFLAGS)

LDLIBS   = $(RAYLIB_LIBS) $(LIBS) $(LDLIBS_VC) $(LDLIBS_ALSA)

TARGET = $(EXENAME)$(EXE)
INSTALL_DIR := $(DESTDIR)$(PREFIX)
CONFIG_FILES := default.cfg amiga.cfg mac68k.cfg test.cfg x68k.cfg
INSTALL_BINS := $(TARGET) buptest pistorm_truth_test #
UDEV_RULES := etc/udev/99-pistorm.rules
LIMITS_CONF := etc/security/limits.d/pistorm-rt.conf
MODULES_LOAD := etc/modules-load.d/pistorm.conf
MODPROBE_CONF := etc/modprobe.d/pistorm.conf
HELP_TARGETS = \
	"make"                             "Build emulator (kmod backend default)" \
	"make PISTORM_KMOD=0"             "Build emulator with legacy userspace GPIO" \
	"make clean"                      "Remove build artifacts" \
	"make amiga-net"                  "Build Amiga net driver (.device)" \
	"make amiga-piscsi"               "Build Amiga PiSCSI driver + bootrom" \
	"make amiga-rtg"                  "Build Amiga RTG driver (.card)" \
	"make amiga-ahi"                  "Build Amiga AHI driver (.audio)" \
	"make amiga-pissa"                "Build Amiga PISSA crypto tools" \
	"make amiga-pissl"                "Build Amiga PISSL TLS tools" \
	"make amiga-all"                  "Build all Amiga-side drivers" \
	"make amiga-clean"                "Clean Amiga-side driver build artifacts" \
	"make install [PREFIX=… DESTDIR=…]" "Install emulator, data/, configs, piscsi.rom, a314 files" \
	"make uninstall [PREFIX=… DESTDIR=…]" "Remove installed tree" \
	"make kernel_module"              "Build pistorm.ko + z3bus.ko (out-of-tree)" \
	"make kernel_module_pistorm"      "Build pistorm.ko only (out-of-tree)" \
	"make kernel_module_z3bus"        "Build z3bus.ko only (out-of-tree)" \
	"make kernel_install"             "Install pistorm.ko + z3bus.ko via kernel_module/Makefile (no build)" \
	"make kernel_install_build"       "Build + install pistorm.ko + z3bus.ko" \
	"make kernel_install_pistorm"     "Install pistorm.ko only via kernel_module/Makefile" \
	"make kernel_install_z3bus"       "Install z3bus.ko only via kernel_module/Makefile" \
	"make kernel_clean"               "Clean kernel module build outputs" \
	"make "            "Build interactive bus monitor" \
	"make full"         "Stop emulator, rebuild kmod+userland, install" \
	"make uae-jit"      "Build UAE AArch64 JIT objects (libuae.a)"

# Safety: never leave partial outputs
.DELETE_ON_ERROR:

DELETEFILES = $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(.OFILES) $(.OFILES:%.o=%.d) $(TARGET) buptest pistorm_truth_test pistorm_truth_test.d $(MUSASHIGENERATOR)$(EXE) \
	$(UAE_TARGET) $(UAE_OBJS) $(UAE_OBJS:%.o=%.d) $(EXTRA_CXX_OBJS) $(EXTRA_CXX_OBJS:%.o=%.d)
DELETEFILES += $(UAE_FPP_NATIVE_CPP)

all: $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(TARGET) buptest pistorm_truth_test 

clean:
	rm -f $(DELETEFILES) $(TARGET).tmp
	$(MAKE) kernel_clean
	rm -rf kernel_module/.tmp_versions
	find . \( -name '*.o' -o -name '*.tmp' \) -print0 | xargs -0 -r rm -f --

# Ensure generated m68k files are built before other files that depend on them
# Link is atomic: write to $@.tmp then move into place on success.
OBJS_LINK = $(filter %.o,$^)

$(TARGET): $(MUSASHIGENHFILES) $(MUSASHIGENCFILES:%.c=%.o) $(MAINFILES:%.c=%.o) $(MUSASHIFILES:%.c=%.o) src/a314/a314.o $(EXTRA_CXX_OBJS) $(EXTRA_LINK_DEPS)
	$(CC) $(LDFLAGS) -o $@.tmp $(OBJS_LINK) $(UAE_LINK_FLAGS) $(LDLIBS) && mv -f $@.tmp $@

uae-jit: $(UAE_TARGET)
ifeq ($(USE_UAE_JIT),1)
uae-jit: $(TARGET)
endif

$(UAE_TARGET): $(UAE_OBJS)
	@mkdir -p $(UAE_BUILDDIR)
	rm -f $@
	$(AR) rcs $@ $^

$(UAE_FPP_NATIVE_CPP): $(UAE_FPP_NATIVE_IN)
	@{ \
		printf '%s\n' '/* Auto-generated from fpp_native.cpp.in; do not edit. */'; \
		printf '%s\n' '' \
			'#include "sysconfig.h"' \
			'#include "sysdeps.h"' \
			'#include "options.h"' \
			'#include "memory.h"' \
			'#include "events.h"' \
			'#include "newcpu.h"' \
			'#include "fpp.h"' \
			'' \
			'#include "fpp_native.cpp.in"'; \
	} > $@

# Explicit rules to keep the generated 68k core quiet on unused-temp warnings.
src/musashi/m68kcpu.o: src/musashi/m68kcpu.c src/musashi/m68kops.h
	$(CC) -MMD -MP $(M68K_CFLAGS) $(NO_LTO_FLAGS) -c -o $@ $<

src/musashi/m68kops.o: src/musashi/m68kops.c src/musashi/m68kops.h
	$(CC) -MMD -MP $(M68K_CFLAGS) $(NO_LTO_FLAGS) -c -o $@ $<

src/musashi/m68kdasm.o: src/musashi/m68kdasm.c src/musashi/m68kops.h
	$(CC) -MMD -MP $(M68K_CFLAGS) $(NO_LTO_FLAGS) -c -o $@ $<

src/musashi/softfloat/softfloat.o: src/musashi/softfloat/softfloat.c
	$(CC) -MMD -MP $(CFLAGS) $(NO_LTO_FLAGS) -c -o $@ $<

src/musashi/softfloat/softfloat_fpsp.o: src/musashi/softfloat/softfloat_fpsp.c
	$(CC) -MMD -MP $(CFLAGS) $(NO_LTO_FLAGS) -c -o $@ $<

src/emulator.o: src/emulator.c src/musashi/m68kops.h
	$(CC) -MMD -MP $(CFLAGS) -c -o $@ $<

buptest: src/buptest/buptest.c $(PS_PROTOCOL_SRC) src/log.c
	@if [ -f src/buptest/buptest.c ]; then \
		$(CC) $(CFLAGS) -o $@ src/buptest/buptest.c $(PS_PROTOCOL_SRC) src/log.c; \
	else \
		echo "buptest skipped (src/buptest/buptest.c missing)"; \
	fi

pistorm_truth_test: tools/pistorm_truth_test.c include/uapi/linux/pistorm.h
	$(CC) -MMD -MP $(CFLAGS) -Iinclude -Iinclude/uapi -o $@ $<

src/a314/a314.o: src/a314/a314.cc src/a314/a314.h
	$(CXX) -MMD -MP -c -o src/a314/a314.o $(CXXFLAGS) $(NO_LTO_FLAGS) src/a314/a314.cc

src/uae/pistorm_uae_bridge.o: src/uae/pistorm_uae_bridge.cc src/uae/pistorm_uae_bridge.h
	$(CXX) -MMD -MP -c -o $@ $(CXXFLAGS) $(NO_LTO_FLAGS) $<

src/uae/pistorm_uae_stubs.o: src/uae/pistorm_uae_stubs.cc
	$(CXX) -MMD -MP -c -o $@ $(UAE_CXXFLAGS) $(NO_LTO_FLAGS) $<

$(UAE_BUILDDIR)/%.o: $(UAE_SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) -MMD -MP $(UAE_CFLAGS) -c -o $@ $<

$(UAE_BUILDDIR)/%.o: $(UAE_SRCDIR)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) -MMD -MP $(UAE_CXXFLAGS) -c -o $@ $<

$(UAE_BUILDDIR)/%.o: $(UAE_SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -MMD -MP $(UAE_CXXFLAGS) -c -o $@ $<

$(UAE_BUILDDIR)/%.o: $(UAE_SRCDIR)/%.cxx
	@mkdir -p $(dir $@)
	$(CXX) -MMD -MP $(UAE_CXXFLAGS) -c -o $@ $<

$(MUSASHIGENCFILES) $(MUSASHIGENHFILES): $(MUSASHIGENERATOR)$(EXE)
	cp $(MUSASHIGENERATOR)$(EXE) src/musashi/ && cd src/musashi && ./$(MUSASHIGENERATOR)$(EXE) && rm -f src/musashi/$(MUSASHIGENERATOR)$(EXE)

$(MUSASHIGENERATOR)$(EXE): src/musashi/$(MUSASHIGENERATOR).c
	$(CC) -MMD -MP  -o $(MUSASHIGENERATOR)$(EXE) src/musashi/$(MUSASHIGENERATOR).c

install: all
	$(INSTALL) -d $(INSTALL_DIR)
	for bin in $(INSTALL_BINS); do \
		[ -x $$bin ] && $(INSTALL) -m 755 $$bin $(INSTALL_DIR)/; \
	done
	for cfg in $(CONFIG_FILES); do \
		[ -f $$cfg ] && $(INSTALL) -m 644 $$cfg $(INSTALL_DIR)/; \
	done
	$(INSTALL) -d $(INSTALL_DIR)/src/platforms/amiga/piscsi
	$(INSTALL) -m 644 src/platforms/amiga/piscsi/piscsi.rom $(INSTALL_DIR)/src/platforms/amiga/piscsi/piscsi.rom
	$(INSTALL) -d $(INSTALL_DIR)/a314
	cp -a src/a314/files_pi/. $(INSTALL_DIR)/a314/
	cp -a data $(INSTALL_DIR)/
#	$(INSTALL) -d $(INSTALL_DIR)/data/a314-shared
#	@if [ -d $(INSTALL_DIR)/data/a314-shared ] && [ "$$(ls -A $(INSTALL_DIR)/data/a314-shared 2>/dev/null)" ]; then \
#		echo "Warning: $(INSTALL_DIR)/data/a314-shared is not empty; Python code must not be installed there."; \
#	fi
	$(INSTALL) -d $(INSTALL_DIR)/rtg
	$(INSTALL) -m 644 src/platforms/amiga/rtg/*.shader $(INSTALL_DIR)/rtg/
	[ -f pistorm.LICENSE ] && $(INSTALL) -m 644 pistorm.LICENSE $(INSTALL_DIR)/
	if [ -f $(UDEV_RULES) ]; then \
		$(INSTALL) -d /etc/udev/rules.d; \
		$(INSTALL) -m 644 $(UDEV_RULES) /etc/udev/rules.d/99-pistorm.rules; \
		udevadm control --reload >/dev/null 2>&1 || true; \
		udevadm trigger --subsystem-match=misc --attr-match=dev=10:262 >/dev/null 2>&1 || true; \
	fi
	if [ -f $(LIMITS_CONF) ]; then \
		$(INSTALL) -d /etc/security/limits.d; \
		$(INSTALL) -m 644 $(LIMITS_CONF) /etc/security/limits.d/pistorm-rt.conf; \
	fi
	if [ -f $(MODULES_LOAD) ]; then \
		$(INSTALL) -d /etc/modules-load.d; \
		$(INSTALL) -m 644 $(MODULES_LOAD) /etc/modules-load.d/pistorm.conf; \
	fi
	if [ -f $(MODPROBE_CONF) ]; then \
		$(INSTALL) -d /etc/modprobe.d; \
		$(INSTALL) -m 644 $(MODPROBE_CONF) /etc/modprobe.d/pistorm.conf; \
	fi

uninstall:
	rm -rf $(INSTALL_DIR)

kernel_module:
	@if [ "$$(id -u)" = "0" ]; then \
		echo "ERROR: build kernel_module as a normal user, not root."; \
		echo "       (root-owned .d files will break subsequent builds)"; \
		exit 1; \
	fi
	$(MAKE) -C kernel_module module

kernel_module_pistorm:
	@if [ "$$(id -u)" = "0" ]; then \
		echo "ERROR: build kernel_module as a normal user, not root."; \
		echo "       (root-owned .d files will break subsequent builds)"; \
		exit 1; \
	fi
	$(MAKE) -C kernel_module module_pistorm

kernel_module_z3bus:
	@if [ "$$(id -u)" = "0" ]; then \
		echo "ERROR: build kernel_module as a normal user, not root."; \
		echo "       (root-owned .d files will break subsequent builds)"; \
		exit 1; \
	fi
	$(MAKE) -C kernel_module module_z3bus

kernel_install:
	$(MAKE) -C kernel_module install

kernel_install_build: kernel_module
	$(MAKE) -C kernel_module install

kernel_install_pistorm: kernel_module_pistorm
	$(MAKE) -C kernel_module install_pistorm

kernel_install_z3bus: kernel_module_z3bus
	$(MAKE) -C kernel_module install_z3bus

kernel_clean:
	$(MAKE) -C kernel_module clean

amiga-net:
	$(AMIGA_SUBMAKE) -C src/platforms/amiga/net/net_driver_amiga

amiga-piscsi:
	$(AMIGA_SUBMAKE) -C src/platforms/amiga/piscsi/device_driver_amiga

amiga-rtg:
	$(AMIGA_SUBMAKE) -C src/platforms/amiga/rtg/rtg_driver_amiga

amiga-ahi:
	$(AMIGA_SUBMAKE) -C src/platforms/amiga/ahi/ahi_driver_amiga

amiga-pissa:
	$(AMIGA_SUBMAKE) -C amiga/pissa

amiga-pissl:
	$(AMIGA_SUBMAKE) -C amiga/pissl

amiga-all: amiga-net amiga-piscsi amiga-rtg amiga-pissa amiga-pissl

amiga-clean:
	$(AMIGA_SUBMAKE) -C src/platforms/amiga/net/net_driver_amiga clean
	$(AMIGA_SUBMAKE) -C src/platforms/amiga/piscsi/device_driver_amiga clean
	$(AMIGA_SUBMAKE) -C src/platforms/amiga/rtg/rtg_driver_amiga clean
	$(AMIGA_SUBMAKE) -C src/platforms/amiga/ahi/ahi_driver_amiga clean
	$(AMIGA_SUBMAKE) -C amiga/pissa clean
	$(AMIGA_SUBMAKE) -C amiga/pissl clean

full:
	@if [ "$$(id -u)" -eq 0 ]; then \
		echo "ERROR: run 'make full' as a normal user (it calls sudo internally)."; \
		exit 1; \
	fi
	-pkill -x emulator 2>/dev/null || true
	-sudo rmmod pistorm 2>/dev/null || true
	$(MAKE) clean
	$(MAKE) USE_UAE_JIT=1 uae-jit
	$(MAKE) USE_UAE_JIT=1 PISTORM_KMOD=$(PISTORM_KMOD)
	$(MAKE) kernel_module
	sudo $(MAKE) kernel_install
	sudo $(MAKE) USE_UAE_JIT=1 PISTORM_KMOD=$(PISTORM_KMOD) install
	# Copy boot configuration files
	#sudo cp -f boot/firmware/config.txt /boot/firmware/config.txt
	#sudo cp -f boot/firmware/cmdline.txt /boot/firmware/cmdline.txt
	# Copy system configuration files
	#sudo cp -f 10-hugepages.conf /etc/sysctl.d/10-hugepages.conf
	sudo cp -f etc/modules-load.d/pistorm.conf /etc/modules-load.d/pistorm.conf
	sudo cp -f etc/modules-load.d/z3bus.conf /etc/modules-load.d/z3bus.conf	
	sudo cp -f $(MODPROBE_CONF) /etc/modprobe.d/pistorm.conf
	sudo cp -f etc/security/limits.d/pistorm-rt.conf /etc/security/limits.d/pistorm-rt.conf
	sudo cp -f etc/udev/99-pistorm.rules /etc/udev/rules.d/99-pistorm.rules
	sudo cp -f etc/systemd/system/kernelpistorm64.service /etc/systemd/system/kernelpistorm64.service
	# Reload systemd configurations
	sudo systemctl daemon-reload
	sudo udevadm control --reload-rules && sudo udevadm trigger
	# Apply sysctl settings (continue even if hugepages not supported)
	sudo sysctl -p /etc/sysctl.d/10-hugepages.conf || echo "Note: Some hugepage settings may not be supported on this system"
	# Enable and start the emulator service
	sudo systemctl enable kernelpistorm64.service
	echo "Loading Kernel PiStorm64"
#	sudo modprobe pistorm run_batch_enable=1 berr_reset_input=1 gpclk_src=$(PISTORM_GPCLK_SRC) gpclk_div=$(PISTORM_GPCLK_DIV) 2>/dev/null || true
	sudo modprobe pistorm $(PISTORM_KMOD_PARAMS) 2>/dev/null || true

help:
	@printf "Available targets:\n"
	@printf "  %-32s %s\n" $(HELP_TARGETS)

-include $(.CFILES:%.c=%.d) $(MUSASHIGENCFILES:%.c=%.d) src/a314/a314.d src/musashi/$(MUSASHIGENERATOR).d pistorm_truth_test.d $(UAE_OBJS:%.o=%.d)

.PHONY: all clean buptest pistorm_truth_test  install uninstall kernel_module kernel_module_pistorm kernel_module_z3bus kernel_install kernel_install_pistorm kernel_install_z3bus kernel_clean amiga-net amiga-piscsi amiga-rtg amiga-ahi amiga-all amiga-clean
