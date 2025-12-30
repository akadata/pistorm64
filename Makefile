# Makefile - replacement
# Target: build PiStorm emulator cleanly on Alpine (musl) for Pi 5 64-bit
# Also keeps existing PI_64BIT / PI3_BULLSEYE / PI4 branches.
#
# Key musl fixes:
#  - Threading: use -pthread (compile + link)
#  - Keep libraries OUT of CFLAGS (compile flags)
#  - Suppress generated m68kops.c warning only for that object
#
# Usage:
#   make clean
#   make PLATFORM=PI5_ALPINE_64BIT
#
# Note: emulator.c must have thread entrypoints with signature:
#   void *cpu_task(void *arg) { (void)arg; ... return NULL; }
#   void *keyboard_task(void *arg) { (void)arg; ... return NULL; }

EXENAME          = emulator

WITH_RAYLIB      ?= 1
WITH_ALSA        ?= 1
DEBUG            ?= 0

DBGFLAGS         =
ifeq ($(DEBUG),1)
DBGFLAGS         = -g3 -ggdb -fno-omit-frame-pointer -fno-optimize-sibling-calls
OPT              = -O0 -pipe -fno-strict-aliasing -fno-plt $(DBGFLAGS)
else
OPT              = -O3 -pipe -fno-strict-aliasing -fno-plt
endif

RTG_OUTPUT_SRC   = platforms/amiga/rtg/rtg-output-raylib.c
ifeq ($(WITH_RAYLIB),0)
RTG_OUTPUT_SRC   = platforms/amiga/rtg/rtg-output-headless.c
endif

AHI_SRC          = platforms/amiga/ahi/pi_ahi.c
ifeq ($(WITH_ALSA),0)
AHI_SRC          = platforms/amiga/ahi/pi_ahi_stub.c
endif

MAINFILES        = emulator.c \
                  memory_mapped.c \
                  config_file/config_file.c \
                  config_file/rominfo.c \
                  input/input.c \
                  gpio/ps_protocol.c \
                  platforms/platforms.c \
                  platforms/amiga/amiga-autoconf.c \
                  platforms/amiga/amiga-platform.c \
                  platforms/amiga/amiga-registers.c \
                  platforms/amiga/amiga-interrupts.c \
                  platforms/mac68k/mac68k-platform.c \
                  platforms/dummy/dummy-platform.c \
                  platforms/dummy/dummy-registers.c \
                  platforms/amiga/Gayle.c \
                  platforms/amiga/hunk-reloc.c \
                  platforms/amiga/cdtv-dmac.c \
                  platforms/amiga/rtg/rtg.c \
                  $(RTG_OUTPUT_SRC) \
                  platforms/amiga/rtg/rtg-gfx.c \
                  platforms/amiga/piscsi/piscsi.c \
                  $(AHI_SRC) \
                  platforms/amiga/pistorm-dev/pistorm-dev.c \
                  platforms/amiga/net/pi-net.c \
                  platforms/shared/rtc.c \
                  platforms/shared/common.c

MUSASHIFILES     = m68kcpu.c m68kdasm.c softfloat/softfloat.c softfloat/softfloat_fpsp.c
MUSASHIGENCFILES = m68kops.c
MUSASHIGENHFILES = m68kops.h
MUSASHIGENERATOR = m68kmake

EXE              =
EXEPATH          = ./

# Build order control
M68KFILES        = $(MUSASHIFILES) $(MUSASHIGENCFILES)
CFILES           = $(MAINFILES) $(M68KFILES)
OFILES           = $(CFILES:%.c=%.o) a314/a314.o
DFILES           = $(OFILES:%.o=%.d) $(MUSASHIGENERATOR).d

CC               = gcc
CXX              = g++

# Common flags
PTHREAD          = -pthread
WARNINGS_COMMON  = -Wall -Wextra -Wpedantic -Wformat=2 -Wundef
DEFS_COMMON      = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1

# Defaults (overridden per PLATFORM)
WARNINGS         = $(WARNINGS_COMMON)
MARCH            = -march=native
MCPU             = -mcpu=native

# Include paths - default to legacy raylib path; platforms override as needed
INCLUDES         = -I. -I./raylib

# Link search paths
LIBDIRS          = -L/usr/local/lib -L/opt/vc/lib

# Libraries (link-time only)
LDLIBS           = -lm -ldl -lstdc++

# Graphics/audio stack for raylib DRM
RAYLIB_DIR       = ./raylib_drm
RAYLIB_LIBDIRS   = -L$(RAYLIB_DIR)
RAYLIB_LIBS      = -lraylib -lGLESv2 -lEGL -lgbm -ldrm -lasound
ifeq ($(WITH_RAYLIB),0)
RAYLIB_LIBDIRS   =
RAYLIB_LIBS      =
endif
ifeq ($(WITH_ALSA),0)
RAYLIB_LIBS      := $(filter-out -lasound,$(RAYLIB_LIBS))
endif

# Optional VideoCore libs (may not exist on Alpine; keep them only where needed)
VCHIQ_LIBS       = -lvcos -lvchiq_arm -lvchostif

# Final flags (set after PLATFORM selection)
CFLAGS           =
CPPFLAGS         =
LDFLAGS          =
LFLAGS           =

# -----------------------------------------------------------------------------
# PLATFORM selection
# -----------------------------------------------------------------------------

ifeq ($(PLATFORM),PI5_ALPINE_64BIT)

# Alpine/musl on Pi 5: avoid /opt/vc assumptions; prefer the system raylib (aarch64).
INCLUDES   = -I.
LIBDIRS    = -L/usr/local/lib

CFLAGS     = $(WARNINGS) $(INCLUDES) $(MARCH) $(MCPU) $(OPT) $(DEFS_COMMON) $(ACFLAGS) $(PTHREAD) -DHAVE_LIBGPIOD -DPISTORM_RP1=1
CPPFLAGS   =
LDFLAGS    = $(PTHREAD) -Wl,-O2 -Wl,--as-needed

# No vcos/vchiq by default on Alpine unless explicitly present
LFLAGS     = $(WARNINGS) $(LDFLAGS) $(LIBDIRS) $(RAYLIB_LIBS) -lgpiod $(LDLIBS)

else ifeq ($(PLATFORM),PI5_DEBIAN_64BIT)

# Debian/Raspberry Pi OS on Pi 5: same flags as Alpine for now; the important part is RP1 + libgpiod.
INCLUDES   = -I.
LIBDIRS    = -L/usr/local/lib

CFLAGS     = $(WARNINGS) $(INCLUDES) $(MARCH) $(MCPU) $(OPT) $(DEFS_COMMON) $(ACFLAGS) $(PTHREAD) -DHAVE_LIBGPIOD -DPISTORM_RP1=1
CPPFLAGS   =
LDFLAGS    = $(PTHREAD) -Wl,-O2 -Wl,--as-needed

LFLAGS     = $(WARNINGS) $(LDFLAGS) $(LIBDIRS) $(RAYLIB_LIBS) -lgpiod $(LDLIBS)

else ifeq ($(PLATFORM),PI_64BIT)

# Existing 64-bit Raspberry Pi Linux: raylib_drm + VideoCore libs
INCLUDES   = -I. -I$(RAYLIB_DIR)
LIBDIRS    = -L/usr/local/lib

CFLAGS     = $(WARNINGS) $(INCLUDES) $(MARCH) $(MCPU) $(OPT) $(DEFS_COMMON) $(ACFLAGS) $(PTHREAD)
LDFLAGS    = $(PTHREAD) -Wl,-O2 -Wl,--as-needed

LFLAGS     = $(WARNINGS) $(LDFLAGS) $(LIBDIRS) $(RAYLIB_LIBDIRS) $(RAYLIB_LIBS) $(VCHIQ_LIBS) $(LDLIBS)

else ifeq ($(PLATFORM),PI3_BULLSEYE)

# Legacy Pi3 Bullseye build (kept compatible with original structure)
INCLUDES   = -I. -I./raylib -I/opt/vc/include/
LIBDIRS    = -L/usr/local/lib -L/opt/vc/lib -L$(RAYLIB_DIR)

CFLAGS     = -Wall -Wextra -pedantic $(INCLUDES) -march=native -Os $(DEFS_COMMON) $(ACFLAGS)
LDFLAGS    =
LFLAGS     = -Wall -Wextra -pedantic $(LIBDIRS) $(RAYLIB_LIBS) $(VCHIQ_LIBS) $(LDLIBS)

else ifeq ($(PLATFORM),PI4)

INCLUDES   = -Wall -Wextra -pedantic -DRPI4_TEST -I. -I./raylib_pi4_test -I/opt/vc/include/
LIBDIRS    = -L/usr/local/lib -L/opt/vc/lib -L./raylib_pi4_test

CFLAGS     = -Wall -Wextra -pedantic -DRPI4_TEST -I. -I./raylib_pi4_test -I/opt/vc/include/ -march=native -Os $(DEFS_COMMON) $(ACFLAGS)
LDFLAGS    =
LFLAGS     = -Wall -Wextra -pedantic $(LIBDIRS) -lraylib -lGLESv2 -lEGL -lgbm -ldrm -lasound $(VCHIQ_LIBS) $(LDLIBS)

else

# Default: build using raylib (non-drm) include path; keep safe.
CFLAGS     = -Wall -Wextra -pedantic $(INCLUDES) -march=native -Os $(DEFS_COMMON) $(ACFLAGS)
LDFLAGS    =
LFLAGS     = -Wall -Wextra -pedantic $(LIBDIRS) $(RAYLIB_LIBS) $(VCHIQ_LIBS) $(LDLIBS)

endif

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------

TARGET = $(EXENAME)$(EXE)

DELETEFILES = $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(OFILES) $(DFILES) $(TARGET) $(MUSASHIGENERATOR)$(EXE)

all: $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(TARGET) buptest

clean:
	rm -f $(DELETEFILES)

# Ensure generated files are built before compiling dependents
$(TARGET): $(MUSASHIGENHFILES) $(MUSASHIGENCFILES:%.c=%.o) $(MAINFILES:%.c=%.o) $(MUSASHIFILES:%.c=%.o) a314/a314.o
	$(CC) -o $@ $^ $(LFLAGS)

# Dependency: files that include m68kops.h must wait for generation
emulator.o: m68kops.h
m68kcpu.o: m68kops.h
m68kdasm.o: m68kops.h
m68kops.o: m68kops.h

# Generated file: keep strict warnings elsewhere, silence only here.
# (The generator emits a NOP handler where the 'state' argument is unused.)
m68kops.o: CFLAGS += -Wno-unused-parameter

buptest: buptest.c gpio/ps_protocol.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(WARNINGS) $(LDFLAGS) $(LIBDIRS) -lgpiod $(LDLIBS)

a314/a314.o: a314/a314.cc a314/a314.h
	$(CXX) -MMD -MP -c -o $@ a314/a314.cc \
	$(OPT) $(MARCH) $(MCPU) \
	$(DEFS_COMMON) \
	-I. -I..

# -----------------------------------------------------------------------------
# Musashi generator and generated files
# -----------------------------------------------------------------------------

$(MUSASHIGENCFILES) $(MUSASHIGENHFILES): $(MUSASHIGENERATOR)$(EXE)
	$(EXEPATH)$(MUSASHIGENERATOR)$(EXE)

$(MUSASHIGENERATOR)$(EXE): $(MUSASHIGENERATOR).c
	$(CC) -o $(MUSASHIGENERATOR)$(EXE) $(MUSASHIGENERATOR).c

# -----------------------------------------------------------------------------
# Generic compile rules + dependency generation
# -----------------------------------------------------------------------------

%.o: %.c
	$(CC) -MMD -MP $(CFLAGS) -c -o $@ $<

-include $(DFILES)
