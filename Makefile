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

PLATFORM=PI4_64BIT

# Enable batching (set to 1) and/or IPL rate limiting (set to interval in us) 100us seems best in tests
PISTORM_ENABLE_BATCH=1
PISTORM_IPL_RATELIMIT_US=100  



PISTORM_KMOD ?= 1
EXTRA_CFLAGS ?=
EXTRA_M68K_CFLAGS ?= -O3 -ffast-math
EXTRA_LDFLAGS ?=


ifeq ($(C),clang)
CC := clang
CXX := clang++
endif

ifeq ($(C),gcc)
CC := gcc
CXX := g++
endif


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
WARNINGS   ?= -Wall -Wextra -pedantic
EMU_WARNINGS ?= \
  -Wall -Wextra -Wpedantic \
  -Wformat=2 -Wwrite-strings -Wcast-qual -Wcast-align \
  -Wpointer-arith -Wstrict-overflow=5 -Wstrict-prototypes -Wmissing-prototypes \
  -Wswitch-enum -Wshadow \
  -Wconversion -Wsign-conversion \
  -Wundef -Wvla -Wredundant-decls

OPT_LEVEL  ?= -O3 -ffast-math

ifdef O
OPT_LEVEL := -O$(O)
endif

# Set USE_GOLD=1 to link with gold if available.
USE_GOLD   ?= 0

# Toggle RTG output backends: 1=raylib (default), 0=null stub.
USE_RAYLIB ?= 1

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
PISTORM_KMOD ?= 1

# Quiet noisy-but-benign warnings from the generated 68k core.
M68K_WARN_SUPPRESS ?= \
  -Wno-unused-variable \
  -Wno-unused-parameter \
  -Wno-unused-but-set-variable

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

PS_PROTOCOL_SRC := src/gpio/ps_protocol.c
ifeq ($(PISTORM_KMOD),1)
PS_PROTOCOL_SRC := src/gpio/ps_protocol_kmod.c
endif


MAINFILES =

MAINFILES += src/emulator.c
MAINFILES += src/log.c
MAINFILES += src/memory_mapped.c

MAINFILES += src/config_file/config_file.c
MAINFILES += src/config_file/rominfo.c

MAINFILES += src/input/input.c
MAINFILES += $(PS_PROTOCOL_SRC)
MAINFILES += src/gpio/rpi_peri.c

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

CC  ?= gcc
CXX ?= g++

DEFINES  += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1
# Allow command-line override of batching and rate limiting for performance tuning
PISTORM_ENABLE_BATCH ?= 0
PISTORM_IPL_RATELIMIT_US ?= 0
PISTORM_USE_DIRECT_OPS ?= 0
DEFINES  += -DPISTORM_ENABLE_BATCH=$(PISTORM_ENABLE_BATCH) -DPISTORM_IPL_RATELIMIT_US=$(PISTORM_IPL_RATELIMIT_US) -DPISTORM_USE_DIRECT_OPS=$(PISTORM_USE_DIRECT_OPS)
LD_GOLD   = $(if $(filter 1,$(USE_GOLD)),-fuse-ld=gold,)
LTO_FLAGS = $(if $(filter 1,$(USE_LTO)),-flto,)
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
BASE_LIBS := -lm -ldl -lstdc++
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

# Core flags
CFLAGS       = $(OPT_LEVEL) $(CPUFLAGS) $(DEFINES) $(INCLUDES) \
               $(ACFLAGS) $(LTO_FLAGS) $(PLT_FLAGS) $(FP_FLAGS) \
               $(PIPE_FLAGS) $(EXTRA_CFLAGS)

CXXFLAGS     = $(OPT_LEVEL) $(CPUFLAGS) $(DEFINES) $(INCLUDES) \
               $(LTO_FLAGS) $(PLT_FLAGS) $(FP_FLAGS) \
               $(PIPE_FLAGS) $(EXTRA_CFLAGS)

# Linker flags (warning flags via compiler driver, harmless)
LDFLAGS      = $(LD_GOLD) $(LDSEARCH) $(LTO_FLAGS) $(EXTRA_LDFLAGS)

# Apply “angry” warnings to normal code
CFLAGS      += $(EMU_WARNINGS)
CXXFLAGS    += $(EMU_WARNINGS)
LDFLAGS     += $(EMU_WARNINGS)

# Musashi gets base warnings + suppressions (and all the other CFLAGS)
M68K_CFLAGS  = $(WARNINGS) $(CFLAGS) $(M68K_WARN_SUPPRESS) $(EXTRA_M68K_CFLAGS)

LDLIBS   = $(RAYLIB_LIBS) $(LIBS) $(LDLIBS_VC) $(LDLIBS_ALSA)

TARGET = $(EXENAME)$(EXE)
INSTALL_DIR := $(DESTDIR)$(PREFIX)
CONFIG_FILES := default.cfg amiga.cfg mac68k.cfg test.cfg x68k.cfg
INSTALL_BINS := $(TARGET) buptest pistorm_truth_test #
UDEV_RULES := etc/udev/99-pistorm.rules
LIMITS_CONF := etc/security/limits.d/pistorm-rt.conf
MODULES_LOAD := etc/modules-load.d/pistorm.conf
HELP_TARGETS = \
	"make"                             "Build emulator (kmod backend default)" \
	"make PISTORM_KMOD=0"             "Build emulator with legacy userspace GPIO" \
	"make clean"                      "Remove build artifacts" \
	"make install [PREFIX=… DESTDIR=…]" "Install emulator, data/, configs, piscsi.rom, a314 files" \
	"make uninstall [PREFIX=… DESTDIR=…]" "Remove installed tree" \
	"make kernel_module"              "Build pistorm.ko (out-of-tree)" \
	"make kernel_install"             "Install pistorm.ko via kernel_module/Makefile" \
	"make kernel_clean"               "Clean kernel module build outputs" \
	"make "            "Build interactive bus monitor" \
	"make full"         "Stop emulator, rebuild kmod+userland, install"

# Safety: never leave partial outputs
.DELETE_ON_ERROR:

DELETEFILES = $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(.OFILES) $(.OFILES:%.o=%.d) $(TARGET) buptest  .d pistorm_truth_test pistorm_truth_test.d $(MUSASHIGENERATOR)$(EXE)

all: $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(TARGET) buptest pistorm_truth_test 

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

buptest:
	@if [ -f src/buptest/buptest.c ]; then \
		$(CC) $(CFLAGS) -o $@ src/buptest/buptest.c $(PS_PROTOCOL_SRC) src/gpio/rpi_peri.c; \
	else \
		echo "buptest skipped (src/buptest/buptest.c missing)"; \
	fi

pistorm_truth_test: tools/pistorm_truth_test.c include/uapi/linux/pistorm.h
	$(CC) -MMD -MP $(CFLAGS) -Iinclude -Iinclude/uapi -o $@ $<

: tools/.c include/uapi/linux/pistorm.h
	$(CC) -MMD -MP $(CFLAGS) -Iinclude -Iinclude/uapi -o $@ $<

src/a314/a314.o: src/a314/a314.cc src/a314/a314.h
	$(CXX) -MMD -MP -c -o src/a314/a314.o $(CXXFLAGS) src/a314/a314.cc

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
	$(INSTALL) -d $(INSTALL_DIR)/src/a314
	cp -a src/a314/files_pi $(INSTALL_DIR)/src/a314/
	cp -a data $(INSTALL_DIR)/
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

uninstall:
	rm -rf $(INSTALL_DIR)

kernel_module:
	$(MAKE) -C kernel_module module

kernel_install: kernel_module
	$(MAKE) -C kernel_module install

kernel_clean:
	$(MAKE) -C kernel_module clean

full:
	-pkill -x emulator 2>/dev/null || true
	-sudo rmmod pistorm 2>/dev/null || true
	$(MAKE) clean
	$(MAKE) PISTORM_KMOD=$(PISTORM_KMOD)
	$(MAKE) kernel_module
	sudo $(MAKE) kernel_install
	sudo $(MAKE) PISTORM_KMOD=$(PISTORM_KMOD) install
	# Copy boot configuration files
	#sudo cp -f boot/firmware/config.txt /boot/firmware/config.txt
	#sudo cp -f boot/firmware/cmdline.txt /boot/firmware/cmdline.txt
	# Copy system configuration files
	#sudo cp -f 10-hugepages.conf /etc/sysctl.d/10-hugepages.conf
	sudo cp -f etc/modules-load.d/pistorm.conf /etc/modules-load.d/pistorm.conf
	sudo cp -f etc/security/limits.d/pistorm-rt.conf /etc/security/limits.d/pistorm-rt.conf
	sudo cp -f etc/udev/99-pistorm.rules /etc/udev/rules.d/99-pistorm.rules
	sudo cp -f etc/systemd/system/kernelpistorm64.service /etc/systemd/system/kernelpistorm64.service
	# Reload systemd configurations
	sudo systemctl daemon-reload
	sudo udevadm control --reload-rules && sudo udevadm trigger
	# Apply sysctl settings (continue even if hugepages not supported)
	sudo sysctl -p /etc/sysctl.d/10-hugepages.conf || echo "Note: Some hugepage settings may not be supported on this system"
	# Enable and start the emulator service
	#sudo systemctl enable kernelpistorm64.service
	echo "Loading Kernel PiStorm64"
	sudo modprobe pistorm 2>/dev/null || true

help:
	@printf "Available targets:\n"
	@printf "  %-32s %s\n" $(HELP_TARGETS)

-include $(.CFILES:%.c=%.d) $(MUSASHIGENCFILES:%.c=%.d) src/a314/a314.d src/musashi/$(MUSASHIGENERATOR).d pistorm_truth_test.d .d

.PHONY: all clean buptest pistorm_truth_test  install uninstall kernel_module kernel_install kernel_clean
