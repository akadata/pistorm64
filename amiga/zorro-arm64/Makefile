# SPDX-License-Identifier: MIT

AMIGA_PREFIX ?= /opt/amiga
CROSS ?= $(AMIGA_PREFIX)/bin/m68k-amigaos-
INSTALL_DIR ?= /opt/pistorm64/data/a314-shared/

CC := $(CROSS)gcc
ARM64_CC ?= aarch64-linux-gnu-gcc
COMMON_CFLAGS := -m68030 -O2 -Wall -Wextra \
	-I../../src/platforms/amiga/zorro/arm64_accel \
	-I../../amiga.dev/Picasso96Develop/Include
TOOL_CFLAGS := $(COMMON_CFLAGS) -noixemul
MODULE_CFLAGS := $(COMMON_CFLAGS) -fomit-frame-pointer -nostartfiles

TOOL := C/armshake
TOOL_OBJS := C/armshake.o C/armaccel_device.o
ARMRUN := C/armrun
ARMRUN_OBJS := C/armrun.o
DEVICE_IMAGE := C/armaccel.device
LIB_IMAGE := C/armaccel.library
DEVICE_OBJS := C/armaccel_device_module.o C/armaccel_device.o
LIB_OBJS := C/armaccel_library_module.o C/armaccel_library.o
ELF_BUILDER := arm64-payloads/build_julia_elf.sh
ABI_SMOKE_ELF_BUILDER := arm64-payloads/build_abi_smoke_elf.sh

all: $(TOOL) $(ARMRUN) $(DEVICE_IMAGE) $(LIB_IMAGE)

C/armshake.o: C/armshake.c
	$(CC) $(TOOL_CFLAGS) -c -o $@ $<

C/armrun.o: C/armrun.c C/armaccel_library.h
	$(CC) $(TOOL_CFLAGS) -c -o $@ $<

C/armaccel_device.o: C/armaccel_device.c C/armaccel_device.h C/armaccel_iocmds.h
	$(CC) $(COMMON_CFLAGS) -c -o $@ $<

C/armaccel_library.o: C/armaccel_library.c C/armaccel_library.h C/armaccel_iocmds.h
	$(CC) $(COMMON_CFLAGS) -c -o $@ $<

C/armaccel_device_module.o: C/armaccel_device_module.c C/armaccel_device.h C/armaccel_iocmds.h
	$(CC) $(MODULE_CFLAGS) -c -o $@ $<

C/armaccel_library_module.o: C/armaccel_library_module.c C/armaccel_library.h
	$(CC) $(MODULE_CFLAGS) -c -o $@ $<

$(TOOL): $(TOOL_OBJS)
	$(CC) $(TOOL_CFLAGS) -o $@ $(TOOL_OBJS)

$(ARMRUN): $(ARMRUN_OBJS)
	$(CC) $(TOOL_CFLAGS) -o $@ $(ARMRUN_OBJS)

$(DEVICE_IMAGE): $(DEVICE_OBJS)
	$(CC) $(MODULE_CFLAGS) -o $@ $(DEVICE_OBJS)

$(LIB_IMAGE): $(LIB_OBJS)
	$(CC) $(MODULE_CFLAGS) -o $@ $(LIB_OBJS)

install: all
	mkdir -p $(INSTALL_DIR)
	cp $(TOOL) $(ARMRUN) $(DEVICE_IMAGE) $(LIB_IMAGE) $(INSTALL_DIR)

elf-julia:
	CC=$(ARM64_CC) $(ELF_BUILDER)

install-julia-elf: elf-julia
	mkdir -p $(INSTALL_DIR)
	cp arm64-payloads/julia-fractal.elf $(INSTALL_DIR)

elf-abi-smoke:
	CC=$(ARM64_CC) $(ABI_SMOKE_ELF_BUILDER)

install-abi-smoke-elf: elf-abi-smoke
	mkdir -p $(INSTALL_DIR)
	cp arm64-payloads/abi-smoke.elf $(INSTALL_DIR)

clean:
	rm -f C/*.o $(TOOL) $(ARMRUN) $(DEVICE_IMAGE) $(LIB_IMAGE)

.PHONY: all install clean elf-julia install-julia-elf elf-abi-smoke install-abi-smoke-elf
