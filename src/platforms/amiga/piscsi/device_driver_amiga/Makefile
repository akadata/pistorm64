AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_PATH := $(AMIGA_TOOLCHAIN)/bin:$(PATH)

M68K_GCC ?= m68k-amigaos-gcc
VASM ?= vasmm68k_mot
VBCC ?=
AMIGA_NDK_C_INCLUDE ?=
AMIGA_NDK_ASM_INCLUDE ?=

CPU ?= 040

ifeq ($(CPU),000)
DEVICE_TARGET = PiSCSI000.device
CPUFLAG = -m68000
endif
ifeq ($(CPU),010)
DEVICE_TARGET = PiSCSI010.device
CPUFLAG = -m68010
endif
ifeq ($(CPU),020)
DEVICE_TARGET = PiSCSI020.device
CPUFLAG = -m68020
endif
ifeq ($(CPU),030)
DEVICE_TARGET = PiSCSI030.device
CPUFLAG = -m68030
endif
ifeq ($(CPU),040)
DEVICE_TARGET = PiSCSI040.device
CPUFLAG = -m68040
endif
ifeq ($(CPU),060)
DEVICE_TARGET = PiSCSI060.device
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

ROM_TARGET ?= bootrom
ROM_SRC ?= bootrom.s

DEVICE_SRC ?= piscsi-amiga-2.c

DEVICE_CFLAGS ?= $(CPUFLAG) -O3 -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -nostdlib -nostartfiles
ifneq ($(strip $(AMIGA_NDK_C_INCLUDE)),)
DEVICE_CFLAGS += -I$(AMIGA_NDK_C_INCLUDE)
endif

all: $(ROM_TARGET) $(DEVICE_TARGET)

$(ROM_TARGET): $(ROM_SRC)
	PATH="$(AMIGA_PATH)" $(VASM) -m68000 -Fhunk $(VASM_INCLUDES) $< -o $@

$(DEVICE_TARGET): $(DEVICE_SRC)
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(DEVICE_CFLAGS) -o $@ $<

install:
	cp -a PiSCSI$(CPU).device /opt/pistorm64/data/a314-shared/pi-scsi.device
clean:
	rm -f $(ROM_TARGET) PiSCSI*.device pi-scsi.device pi-scsi000.device pi-scsi010.device pi-scsi020.device pi-scsi030.device pi-scsi040.device pi-scsi060.device


.PHONY: all clean
