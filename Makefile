EXENAME          = emulator

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
	platforms/amiga/rtg/rtg-output-raylib.c \
	platforms/amiga/rtg/rtg-gfx.c \
	platforms/amiga/piscsi/piscsi.c \
	platforms/amiga/ahi/pi_ahi.c \
	platforms/amiga/pistorm-dev/pistorm-dev.c \
	platforms/amiga/net/pi-net.c \
	platforms/shared/rtc.c \
	platforms/shared/common.c

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
WARNINGS  = -Wall -Wextra -pedantic

# Default to 64-bit settings if no platform specified
CFLAGS    = $(WARNINGS) -I. -I./raylib -I/opt/vc/include/ -march=armv8-a -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1 -lstdc++ $(ACFLAGS)
LFLAGS    = $(WARNINGS) -L/usr/local/lib -L/opt/vc/lib -L./raylib_drm -lraylib -lGLESv2 -lEGL -lgbm -ldrm -ldl -lstdc++ -lvcos -lvchiq_arm -lvchostif -lasound

ifeq ($(PLATFORM),PI_64BIT)
	LFLAGS    = $(WARNINGS) -L/usr/local/lib -L/opt/vc/lib -L./raylib_drm -lraylib -lGLESv2 -lEGL -lgbm -ldrm -ldl -lstdc++ -lvcos -lvchiq_arm -lvchostif -lasound
	CFLAGS    = $(WARNINGS) -I. -I./raylib -I/opt/vc/include/ -march=armv8-a -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1 -lstdc++ $(ACFLAGS)
else ifeq ($(PLATFORM),PI3_BULLSEYE)
	LFLAGS    = $(WARNINGS) -L/usr/local/lib -L/opt/vc/lib -L./raylib_drm -lraylib -lGLESv2 -lEGL -lgbm -ldrm -ldl -lstdc++ -lvcos -lvchiq_arm -lvchostif -lasound
	CFLAGS    = $(WARNINGS) -I. -I./raylib -I/opt/vc/include/ -march=armv8-a -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1 -lstdc++ $(ACFLAGS)
else ifeq ($(PLATFORM),PI4)
	LFLAGS    = $(WARNINGS) -L/usr/local/lib -L/opt/vc/lib -L./raylib_pi4_test -lraylib -lGLESv2 -lEGL -lgbm -ldrm -ldl -lstdc++ -lvcos -lvchiq_arm -lvchostif -lasound
	CFLAGS    = $(WARNINGS) -DRPI4_TEST -I. -I./raylib_pi4_test -I/opt/vc/include/ -march=armv8-a -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1 -lstdc++ $(ACFLAGS)
endif

TARGET = $(EXENAME)$(EXE)

DELETEFILES = $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(.OFILES) $(.OFILES:%.o=%.d) $(TARGET) $(MUSASHIGENERATOR)$(EXE)


all: $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(TARGET) buptest

clean:
	rm -f $(DELETEFILES)

# Ensure generated m68k files are built before other files that depend on them
$(TARGET):  $(MUSASHIGENHFILES) $(MUSASHIGENCFILES:%.c=%.o) $(MAINFILES:%.c=%.o) $(MUSASHIFILES:%.c=%.o) a314/a314.o
	$(CC) -o $@ $^ -O3 -pthread $(LFLAGS) -lm -lstdc++

# Explicit dependency: any .o file that might need m68kops.h should depend on it
# Files that include m68kops.h (like emulator.c and m68kcpu.c) need to wait for it to be generated
emulator.o: m68kops.h
m68kcpu.o: m68kops.h
m68kdasm.o: m68kops.h
m68kops.o: m68kops.h

buptest: buptest.c gpio/ps_protocol.c
	$(CC) $^ -o $@ -I./ -march=armv8-a -O0

a314/a314.o: a314/a314.cc a314/a314.h
	$(CXX) -MMD -MP -c -o a314/a314.o -O3 a314/a314.cc -march=armv8-a -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -I. -I..

$(MUSASHIGENCFILES) $(MUSASHIGENHFILES): $(MUSASHIGENERATOR)$(EXE)
	$(EXEPATH)$(MUSASHIGENERATOR)$(EXE)

$(MUSASHIGENERATOR)$(EXE):  $(MUSASHIGENERATOR).c
	$(CC) -o  $(MUSASHIGENERATOR)$(EXE)  $(MUSASHIGENERATOR).c

-include $(.CFILES:%.c=%.d) $(MUSASHIGENCFILES:%.c=%.d) a314/a314.d $(MUSASHIGENERATOR).d
