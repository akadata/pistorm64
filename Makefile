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

EXENAME          = emulator

PLATFORM=ZEROW2_64


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
# USE_VC     : set to 0 to drop /opt/vc includes and Pi host support (vc_vchi_gencmd.h).
# USE_LTO    : set to 1 to enable link-time optimisation (-flto) on build and link.
# USE_NO_PLT : set to 1 to pass -fno-plt for direct calls (glibc-specific; default off).
# OMIT_FP    : set to 1 to omit frame pointers (-fomit-frame-pointer) for perf.
# USE_PIPE   : set to 1 to add -pipe to compile steps.
# M68K_WARN_SUPPRESS : extra warning suppressions for the generated Musashi core.
WARNINGS   ?= -Wall -Wextra -pedantic
OPT_LEVEL  ?= -O3
ifdef O
OPT_LEVEL := -O$(O)
endif
# Set USE_GOLD=1 to link with gold if available.
USE_GOLD   ?= 0
# Toggle RTG output backends: 1=raylib (default), 0=null stub.
USE_RAYLIB ?= 0
# Toggle ALSA-based audio (Pi AHI). If 0, drop pi_ahi and -lasound.
USE_ALSA   ?= 1
# Toggle PMMU emulation (68030/040). Default on; disable with USE_PMMU=0 if needed.
USE_PMMU   ?= 1
# Force FPU on EC/020/EC040/LC040 for 68881/68882 emulation (optional).
USE_EC_FPU ?= 0

ARCH_FEATURES ?=
# Toggle Pi host (/opt/vc) support for dev tools.
USE_VC     ?= 0
# Perf toggles
USE_LTO    ?= 0
USE_NO_PLT ?= 1
OMIT_FP    ?= 1
USE_PIPE   ?= 1
# Quiet noisy-but-benign warnings from the generated 68k core.
M68K_WARN_SUPPRESS ?= -Wno-unused-variable -Wno-unused-parameter -Wno-unused-but-set-variable
# Default CPU flags; overridden by PLATFORM selections below.
CPUFLAGS   ?= -march=armv8-a+crc -mtune=cortex-a53
# Raylib paths can be swapped if you use a custom build.
RAYLIB_INC    ?= -I./src/raylib
RAYLIB_LIBDIR ?= -L./src/raylib_drm


MAINFILES =

MAINFILES += src/emulator.c
MAINFILES += src/log.c
MAINFILES += src/memory_mapped.c

MAINFILES += src/config_file/config_file.c
MAINFILES += src/config_file/rominfo.c

MAINFILES += src/input/input.c
MAINFILES += gpio/ps_protocol.c
MAINFILES += gpio/rpi_peri.c

MAINFILES += src/platforms/platforms.c

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


ifeq ($(USE_VC),0)
MAINFILES := $(filter-out src/platforms/amiga/pistorm-dev/pistorm-dev.c,$(MAINFILES))
MAINFILES += src/platforms/amiga/pistorm-dev/pistorm-dev-stub.c
VC_INC    :=
VC_LIBDIR :=
LDLIBS_VC :=
else
VC_INC    := -I/opt/vc/include/
VC_LIBDIR := -L/opt/vc/lib
LDLIBS_VC := -lvcos -lvchiq_arm -lvchostif
endif

ifeq ($(USE_PMMU),1)
DEFINES += -DPISTORM_EXPERIMENT_PMMU
endif

ifeq ($(USE_EC_FPU),1)
DEFINES += -DPISTORM_ENABLE_020_FPU -DPISTORM_ENABLE_EC040_FPU
endif

MUSASHIFILES     = src/musashi/m68kcpu.c src/musashi/m68kdasm.c src/softfloat/softfloat.c src/softfloat/softfloat_fpsp.c
MUSASHIGENCFILES = src/musashi/m68kops.c
MUSASHIGENHFILES = src/musashi/m68kops.h
MUSASHIGENERATOR = m68kmake

EXE =
EXEPATH = ./

# Define the m68k related files separately to control build order
M68KFILES = $(MUSASHIFILES) $(MUSASHIGENCFILES)
.CFILES   = $(MAINFILES) $(M68KFILES)
.OFILES   = $(.CFILES:%.c=%.o) src/a314/a314.o

CC  ?= gcc
CXX ?= g++

DEFINES  += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1
LD_GOLD   = $(if $(filter 1,$(USE_GOLD)),-fuse-ld=gold,)
LTO_FLAGS = $(if $(filter 1,$(USE_LTO)),-flto,)
PLT_FLAGS = $(if $(filter 1,$(USE_NO_PLT)),-fno-plt,)
FP_FLAGS  = $(if $(filter 1,$(OMIT_FP)),-fomit-frame-pointer,)
PIPE_FLAGS= $(if $(filter 1,$(USE_PIPE)),-pipe,)

# Platform-specific tuning and raylib variants.
ifeq ($(PLATFORM),PI4)
CPUFLAGS = -mcpu=cortex-a72 -mtune=cortex-a72 -march=armv8-a+crc -mfpu=neon-fp-armv8 -mfloat-abi=hard
RAYLIB_INC    = -I./src/raylib_pi4_test
RAYLIB_LIBDIR = -L./src/raylib_pi4_test
DEFINES      += -DRPI4_TEST
else ifeq ($(PLATFORM),PI4_64BIT)
CPUFLAGS = -mcpu=cortex-a72 -mtune=cortex-a72 -march=armv8-a+crc
RAYLIB_INC    = -I./src/raylib_pi4_test
RAYLIB_LIBDIR = -L./src/raylib_pi4_test
DEFINES      += -DRPI4_TEST
else ifeq ($(PLATFORM),PI3_BULLSEYE)
CPUFLAGS = -mcpu=cortex-a53 -mtune=cortex-a53 -march=armv8-a+crc
else ifeq ($(PLATFORM),PI_64BIT)
CPUFLAGS = -mcpu=cortex-a53 -mtune=cortex-a53 -march=armv8-a+crc
else ifeq ($(PLATFORM),ZEROW2_64)
CPUFLAGS = -mcpu=cortex-a53 -mtune=cortex-a53 -march=armv8-a+crc
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

LDLIBS_RAYLIB = -lraylib -lGLESv2 -lEGL -lgbm -ldrm
ifeq ($(USE_RAYLIB),0)
LDLIBS_RAYLIB =
RAYLIB_INC    =
RAYLIB_LIBDIR =
endif

INCLUDES  = -I. -Isrc -Isrc/musashi $(RAYLIB_INC) $(VC_INC)
LDSEARCH  = -L/usr/local/lib $(VC_LIBDIR) $(RAYLIB_LIBDIR)

CFLAGS       = $(WARNINGS) $(OPT_LEVEL) $(CPUFLAGS) $(DEFINES) $(INCLUDES) $(ACFLAGS) $(LTO_FLAGS) $(PLT_FLAGS) $(FP_FLAGS) $(PIPE_FLAGS)
M68K_CFLAGS   = $(CFLAGS) $(M68K_WARN_SUPPRESS)
LDFLAGS      = $(WARNINGS) $(LD_GOLD) $(LDSEARCH) $(LTO_FLAGS)

LDLIBS   = $(LDLIBS_RAYLIB) $(LDLIBS_VC) $(LDLIBS_ALSA) -ldl -lstdc++ -lm -pthread

TARGET = $(EXENAME)$(EXE)

# Safety: never leave partial outputs
.DELETE_ON_ERROR:

DELETEFILES = $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(.OFILES) $(.OFILES:%.o=%.d) $(TARGET) $(MUSASHIGENERATOR)$(EXE)

all: $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(TARGET) buptest

clean:
	rm -f $(DELETEFILES)

# Ensure generated m68k files are built before other files that depend on them
# Link is atomic: write to $@.tmp then move into place on success.
OBJS_LINK = $(filter %.o,$^)

$(TARGET): $(MUSASHIGENHFILES) $(MUSASHIGENCFILES:%.c=%.o) $(MAINFILES:%.c=%.o) $(MUSASHIFILES:%.c=%.o) src/a314/a314.o
	$(CC) $(LDFLAGS) -o $@.tmp $(OBJS_LINK) $(LDLIBS) && mv -f $@.tmp $@

# Explicit rules to keep the generated 68k core quiet on unused-temp warnings.
src/musashi/m68kcpu.o: src/musashi/m68kcpu.c src/musashi/m68kops.h
	$(CC) -MMD -MP $(M68K_CFLAGS) -c -o $@ $<

src/musashi/m68kops.o: src/musashi/m68kops.c src/musashi/m68kops.h
	$(CC) -MMD -MP $(M68K_CFLAGS) -c -o $@ $<

src/musashi/m68kdasm.o: src/musashi/m68kdasm.c src/musashi/m68kops.h
	$(CC) -MMD -MP $(M68K_CFLAGS) -c -o $@ $<

src/emulator.o: src/emulator.c src/musashi/m68kops.h
	$(CC) -MMD -MP $(M68K_CFLAGS) -c -o $@ $<

buptest: src/buptest/buptest.c gpio/ps_protocol.c gpio/rpi_peri.c
	$(CC) $(CFLAGS) -o $@ $^

src/a314/a314.o: src/a314/a314.cc src/a314/a314.h
	$(CXX) -MMD -MP -c -o src/a314/a314.o $(OPT_LEVEL) src/a314/a314.cc $(CPUFLAGS) $(DEFINES) -I. -Isrc -Isrc/musashi

$(MUSASHIGENCFILES) $(MUSASHIGENHFILES): $(MUSASHIGENERATOR)$(EXE)
	cp $(MUSASHIGENERATOR)$(EXE) src/musashi/ && cd src/musashi && ./$(MUSASHIGENERATOR)$(EXE) && rm -f src/musashi/$(MUSASHIGENERATOR)$(EXE)

$(MUSASHIGENERATOR)$(EXE): src/musashi/$(MUSASHIGENERATOR).c
	$(CC) -MMD -MP -o $(MUSASHIGENERATOR)$(EXE) src/musashi/$(MUSASHIGENERATOR).c

-include $(.CFILES:%.c=%.d) $(MUSASHIGENCFILES:%.c=%.d) src/a314/a314.d src/musashi/$(MUSASHIGENERATOR).d

