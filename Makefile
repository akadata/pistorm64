EXENAME          = emulator

# Tunables: edit here instead of hunting through rule bodies.
# WARNINGS   : compiler warnings; keep strict by default.
# OPT_LEVEL  : optimisation level (-Os/-O2/-O3). Can also set O=2,3,...
# USE_GOLD   : set to 1 to prefer gold linker (if installed).
# USE_RAYLIB : set to 0 to drop raylib/DRM deps and use a null RTG backend.
# USE_ALSA   : set to 0 to drop ALSA/ahi builds and -lasound.
# USE_PMMU   : set to 1 to enable Musashi PMMU support (experimental).
# USE_EC_FPU : set to 1 to force FPU on EC/020/LC/EC040 variants (for 68881/68882 emu).
# CPUFLAGS   : per-platform tuning defaults below; override if needed.
# RAYLIB_*   : raylib include/lib paths; adjust for custom builds.
# USE_VC     : set to 0 to drop /opt/vc includes and Pi host support (vc_vchi_gencmd.h).
# M68K_WARN_SUPPRESS : extra warning suppressions for the generated Musashi core.
WARNINGS   ?= -Wall -Wextra -pedantic
OPT_LEVEL  ?= -O3
ifdef O
OPT_LEVEL := -O$(O)
endif
# Set USE_GOLD=1 to link with gold if available.
USE_GOLD   ?= 0
# Toggle RTG output backends: 1=raylib (default), 0=null stub.
USE_RAYLIB ?= 1
# Toggle ALSA-based audio (Pi AHI). If 0, drop pi_ahi and -lasound.
USE_ALSA   ?= 1
# Toggle PMMU emulation (68030/040). Default off to avoid regressions.
USE_PMMU   ?= 0
# Force FPU on EC/020/EC040/LC040 for 68881/68882 emulation (optional).
USE_EC_FPU ?= 0
# Toggle Pi host (/opt/vc) support for dev tools.
USE_VC     ?= 1
# Quiet noisy-but-benign warnings from the generated 68k core.
M68K_WARN_SUPPRESS ?= -Wno-unused-variable -Wno-unused-parameter -Wno-unused-but-set-variable
# Default CPU flags; overridden by PLATFORM selections below.
CPUFLAGS   ?= -march=native -mtune=native
# Raylib paths can be swapped if you use a custom build.
RAYLIB_INC    ?= -I./raylib
RAYLIB_LIBDIR ?= -L./raylib_drm

MAINFILES        = emulator.c \
	log.c \
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
	platforms/amiga/rtg/rtg-output-raylib.c \
	platforms/amiga/rtg/rtg-gfx.c \
	platforms/amiga/piscsi/piscsi.c \
	platforms/amiga/net/pi-net.c \
	platforms/shared/rtc.c \
	platforms/shared/common.c

ifeq ($(USE_RAYLIB),0)
	MAINFILES := $(filter-out platforms/amiga/rtg/rtg-output-raylib.c,$(MAINFILES))
	MAINFILES += platforms/amiga/rtg/rtg-output-null.c
endif

ifeq ($(USE_ALSA),0)
	MAINFILES := $(filter-out platforms/amiga/ahi/pi_ahi.c,$(MAINFILES))
	MAINFILES += platforms/amiga/ahi/pi_ahi_stub.c
	LDLIBS_ALSA :=
else
	LDLIBS_ALSA := -lasound
endif

ifeq ($(USE_VC),0)
	MAINFILES := $(filter-out platforms/amiga/pistorm-dev/pistorm-dev.c,$(MAINFILES))
	MAINFILES += platforms/amiga/pistorm-dev/pistorm-dev-stub.c
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

MUSASHIFILES     = m68kcpu.c m68kdasm.c softfloat/softfloat.c softfloat/softfloat_fpsp.c
MUSASHIGENCFILES = m68kops.c
MUSASHIGENHFILES = m68kops.h
MUSASHIGENERATOR = m68kmake

# EXE = .exe
# EXEPATH = .\\
EXE =
EXEPATH = ./

# Define the m68k related files separately to control build order
M68KFILES   = $(MUSASHIFILES) $(MUSASHIGENCFILES)
.CFILES   = $(MAINFILES) $(M68KFILES)
.OFILES   = $(.CFILES:%.c=%.o) a314/a314.o

CC        = gcc
CXX       = g++

DEFINES   = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1
LD_GOLD   = $(if $(filter 1,$(USE_GOLD)),-fuse-ld=gold,)

# Platform-specific tuning and raylib variants.
ifeq ($(PLATFORM),PI4)
	CPUFLAGS = -mcpu=cortex-a72 -mtune=cortex-a72 -march=armv8-a+crc -mfpu=neon-fp-armv8 -mfloat-abi=hard
	RAYLIB_INC    = -I./raylib_pi4_test
	RAYLIB_LIBDIR = -L./raylib_pi4_test
	DEFINES      += -DRPI4_TEST
else ifeq ($(PLATFORM),PI4_64BIT)
	CPUFLAGS = -mcpu=cortex-a72 -mtune=cortex-a72 -march=armv8-a+crc
	RAYLIB_INC    = -I./raylib_pi4_test
	RAYLIB_LIBDIR = -L./raylib_pi4_test
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

LDLIBS_RAYLIB = -lraylib -lGLESv2 -lEGL -lgbm -ldrm
ifeq ($(USE_RAYLIB),0)
	LDLIBS_RAYLIB =
	RAYLIB_INC    =
	RAYLIB_LIBDIR =
endif

INCLUDES  = -I. $(RAYLIB_INC) $(VC_INC)
LDSEARCH  = -L/usr/local/lib $(VC_LIBDIR) $(RAYLIB_LIBDIR)

CFLAGS   = $(WARNINGS) $(OPT_LEVEL) $(CPUFLAGS) $(DEFINES) $(INCLUDES) $(ACFLAGS)
M68K_CFLAGS = $(CFLAGS) $(M68K_WARN_SUPPRESS)
LDFLAGS  = $(WARNINGS) $(LD_GOLD) $(LDSEARCH)

LDLIBS   = $(LDLIBS_RAYLIB) $(LDLIBS_VC) $(LDLIBS_ALSA) -ldl -lstdc++ -lm -pthread

TARGET = $(EXENAME)$(EXE)

DELETEFILES = $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(.OFILES) $(.OFILES:%.o=%.d) $(TARGET) $(MUSASHIGENERATOR)$(EXE)


all: $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(TARGET) buptest

clean:
	rm -f $(DELETEFILES)

# Ensure generated m68k files are built before other files that depend on them
$(TARGET):  $(MUSASHIGENHFILES) $(MUSASHIGENCFILES:%.c=%.o) $(MAINFILES:%.c=%.o) $(MUSASHIFILES:%.c=%.o) a314/a314.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Explicit rules to keep the generated 68k core quiet on unused-temp warnings.
m68kcpu.o: m68kcpu.c m68kops.h
	$(CC) $(M68K_CFLAGS) -c -o $@ $<

m68kops.o: m68kops.c m68kops.h
	$(CC) $(M68K_CFLAGS) -c -o $@ $<

m68kdasm.o: m68kdasm.c m68kops.h
	$(CC) $(M68K_CFLAGS) -c -o $@ $<

emulator.o: emulator.c m68kops.h
	$(CC) $(M68K_CFLAGS) -c -o $@ $<

# Explicit dependency: any .o file that might need m68kops.h should depend on it
# Files that include m68kops.h (like emulator.c and m68kcpu.c) need to wait for it to be generated
emulator.o: m68kops.h
m68kcpu.o: m68kops.h
m68kdasm.o: m68kops.h
m68kops.o: m68kops.h

buptest: buptest.c gpio/ps_protocol.c
	$(CC) $(CFLAGS) -o $@ $^

a314/a314.o: a314/a314.cc a314/a314.h
	$(CXX) -MMD -MP -c -o a314/a314.o $(OPT_LEVEL) a314/a314.cc $(CPUFLAGS) $(DEFINES) -I. -I..

$(MUSASHIGENCFILES) $(MUSASHIGENHFILES): $(MUSASHIGENERATOR)$(EXE)
	$(EXEPATH)$(MUSASHIGENERATOR)$(EXE)

$(MUSASHIGENERATOR)$(EXE):  $(MUSASHIGENERATOR).c
	$(CC) -o  $(MUSASHIGENERATOR)$(EXE)  $(MUSASHIGENERATOR).c

-include $(.CFILES:%.c=%.d) $(MUSASHIGENCFILES:%.c=%.d) a314/a314.d $(MUSASHIGENERATOR).d
