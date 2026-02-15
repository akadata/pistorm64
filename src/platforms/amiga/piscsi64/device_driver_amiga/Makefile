AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_PATH := $(AMIGA_TOOLCHAIN)/bin:$(PATH)

M68K_GCC ?= m68k-amigaos-gcc
VASM ?= vasmm68k_mot
HOST_CC ?= cc
VBCC ?=
AMIGA_NDK_C_INCLUDE ?=
AMIGA_NDK_ASM_INCLUDE ?=

CPU ?= 030

ifeq ($(CPU),000)
DEVICE_TARGET = pi-scsi64.device
CPUFLAG = -m68000
endif
ifeq ($(CPU),010)
DEVICE_TARGET = pi-scsi64.device
CPUFLAG = -m68010
endif
ifeq ($(CPU),020)
DEVICE_TARGET = pi-scsi64.device
CPUFLAG = -m68020
endif
ifeq ($(CPU),030)
DEVICE_TARGET = pi-scsi64.device
CPUFLAG = -m68030
endif
ifeq ($(CPU),040)
DEVICE_TARGET = pi-scsi64.device
CPUFLAG = -m68040
endif
ifeq ($(CPU),060)
DEVICE_TARGET = pi-scsi64.device
CPUFLAG = -m68060
endif
ifeq ($(strip $(AMIGA_NDK_C_INCLUDE)),)
ifneq ($(wildcard $(AMIGA_TOOLCHAIN)/m68k-amigaos/ndk-include),)
AMIGA_NDK_C_INCLUDE := $(AMIGA_TOOLCHAIN)/m68k-amigaos/ndk-include
else ifneq ($(wildcard $(AMIGA_TOOLCHAIN)/ndk/Include_H),)
AMIGA_NDK_C_INCLUDE := $(AMIGA_TOOLCHAIN)/ndk/Include_H
else ifneq ($(wildcard $(AMIGA_TOOLCHAIN)/ndk/include_h),)
AMIGA_NDK_C_INCLUDE := $(AMIGA_TOOLCHAIN)/ndk/include_h
endif
endif

ifeq ($(strip $(AMIGA_NDK_ASM_INCLUDE)),)
ifneq ($(wildcard $(AMIGA_TOOLCHAIN)/m68k-amigaos/ndk-include),)
AMIGA_NDK_ASM_INCLUDE := $(AMIGA_TOOLCHAIN)/m68k-amigaos/ndk-include
else ifneq ($(wildcard $(AMIGA_TOOLCHAIN)/ndk/Include_I),)
AMIGA_NDK_ASM_INCLUDE := $(AMIGA_TOOLCHAIN)/ndk/Include_I
else ifneq ($(wildcard $(AMIGA_TOOLCHAIN)/ndk/include_i),)
AMIGA_NDK_ASM_INCLUDE := $(AMIGA_TOOLCHAIN)/ndk/include_i
endif
endif

VASM_INCLUDES :=
ifneq ($(strip $(AMIGA_NDK_ASM_INCLUDE)),)
VASM_INCLUDES += -I$(AMIGA_NDK_ASM_INCLUDE)
endif
ifneq ($(VBCC),)
VASM_INCLUDES += -I$(VBCC)/NDK39/include/include_i
endif
ifneq ($(AMIGA_HEADERS),)
VASM_INCLUDES += -I$(AMIGA_HEADERS)
endif

ROM_TARGET ?= bootrom64
ROM_SRC ?= bootrom64.s
ROM_IMAGE ?= ../piscsi64.rom
MAKEROM ?= makerom

DEVICE_SRC ?= piscsi64-amiga.c

DEVICE_CFLAGS ?= $(CPUFLAG) -O3 -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -nostdlib -nostartfiles
ifneq ($(strip $(AMIGA_NDK_C_INCLUDE)),)
DEVICE_CFLAGS += -I$(AMIGA_NDK_C_INCLUDE)
endif

all: $(ROM_TARGET) $(DEVICE_TARGET) $(MAKEROM) $(ROM_IMAGE)

$(ROM_TARGET): $(ROM_SRC)
	PATH="$(AMIGA_PATH)" $(VASM) -m68000 -Fhunk $(VASM_INCLUDES) $< -o $@

$(DEVICE_TARGET): $(DEVICE_SRC)
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(DEVICE_CFLAGS) -o $@ $<

$(MAKEROM): makerom.c
	$(HOST_CC) -O2 -Wall -Wextra -o $@ $<

$(ROM_IMAGE): $(ROM_TARGET) $(DEVICE_TARGET) $(MAKEROM)
	./$(MAKEROM) $(DEVICE_TARGET)

install:
	sudo cp -a pi-scsi64.device /opt/pistorm64/data/a314-shared/pi-scsi64.device
	sudo cp -a $(ROM_IMAGE) /opt/pistorm64/src/platforms/amiga/piscsi64/piscsi64.rom
	sudo chown pistorm:pistorm /opt/pistorm64/data/a314-shared/* /opt/pistorm64/src/platforms/amiga/piscsi64/piscsi64.rom
clean:
	rm -f $(ROM_TARGET) $(MAKEROM) $(ROM_IMAGE) pi-scsi64.device

.PHONY: all clean
