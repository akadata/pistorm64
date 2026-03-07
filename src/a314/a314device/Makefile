# A314 Amiga-side device build
#
# Usage:
#   make
#   make TOOLCHAIN=vbcc
#   make install INSTALL_DIR=/opt/pistorm64/data/a314-shared

# IMPORTANT:
# a314.device uses Amiga register-based entry points (__reg).
# The vbcc toolchain preserves this ABI. GCC builds are experimental and may
# produce binaries that run but crash under real load.
TOOLCHAIN ?= vbcc

OUT_DIR ?= ../software-amiga
TARGET ?= $(OUT_DIR)/a314.device
AMIGA_BIN ?= /opt/amiga/bin

INSTALL_DIR ?=
INSTALL_TARGET ?= $(INSTALL_DIR)/a314.device

ifneq ($(wildcard $(AMIGA_BIN)),)
export PATH := $(AMIGA_BIN):$(PATH)
endif

ifeq ($(TOOLCHAIN),vbcc)

VC ?= vc
VBCC_LDLIBS ?= -lamiga
VBCC_SRCS := romtag.asm a314driver.c device.c startup.c fix_mem_region.c sockets.c int_server.asm

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(VBCC_SRCS)
	@mkdir -p $(OUT_DIR)
	$(VC) $(VBCC_SRCS) -O3 -nostdlib $(VBCC_LDLIBS) -o $@

clean:
	@rm -f $(TARGET)

install: $(TARGET)
ifndef INSTALL_DIR
	$(error INSTALL_DIR is not set. Example: make install INSTALL_DIR=/opt/pistorm64/data/a314-shared)
endif
	cp $(TARGET) $(INSTALL_TARGET)

else

CROSS ?= m68k-amigaos-
CC = $(CROSS)gcc
AS = vasmm68k_mot

CFLAGS = -m68020 -O3 -Wall -Wextra
ASFLAGS = -Fhunk -quiet
LDFLAGS = -m68020 -nostartfiles
LDLIBS = -lamiga

# GCC does not understand vbcc-style __reg("a0") annotations used in these sources.
CFLAGS += -D'__reg(x)='

$(warning Building a314.device with TOOLCHAIN=gcc is experimental and may be ABI-unsafe on AmigaOS)

C_SRCS := a314driver.c device.c startup.c fix_mem_region.c sockets.c
ASM_SRCS := romtag.asm int_server.asm
OBJS := $(C_SRCS:.c=.o) $(ASM_SRCS:.asm=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(OUT_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

clean:
	@rm -f $(OBJS) $(TARGET)

install: $(TARGET)
ifndef INSTALL_DIR
	$(error INSTALL_DIR is not set. Example: make install INSTALL_DIR=/opt/pistorm64/data/a314-shared)
endif
	cp $(TARGET) $(INSTALL_TARGET)

endif
