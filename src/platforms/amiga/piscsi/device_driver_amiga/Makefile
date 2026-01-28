AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_PATH := $(AMIGA_TOOLCHAIN)/bin:$(PATH)

M68K_GCC ?= m68k-amigaos-gcc
VASM ?= vasmm68k_mot
VBCC ?=

CPU ?= 020

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
VASM_INCLUDES := -I$(AMIGA_TOOLCHAIN)/m68k-amigaos/ndk-include
ifneq ($(VBCC),)
VASM_INCLUDES += -I$(VBCC)/NDK39/include/include_i
endif
ifneq ($(AMIGA_HEADERS),)
VASM_INCLUDES += -I$(AMIGA_HEADERS)
endif

ROM_TARGET ?= bootrom
ROM_SRC ?= bootrom.s

DEVICE_SRC ?= piscsi-amiga-2.c

DEVICE_CFLAGS ?= $(CPUFLAG) -O2 -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -nostdlib -nostartfiles

all: $(ROM_TARGET) $(DEVICE_TARGET)

$(ROM_TARGET): $(ROM_SRC)
	PATH="$(AMIGA_PATH)" $(VASM) -m68000 -Fhunk $(VASM_INCLUDES) $< -o $@

$(DEVICE_TARGET): $(DEVICE_SRC)
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(DEVICE_CFLAGS) -o $@ $<

clean:
	rm -f $(ROM_TARGET) PiSCSI*.device pi-scsi.device pi-scsi000.device pi-scsi010.device pi-scsi020.device pi-scsi030.device pi-scsi040.device pi-scsi060.device

.PHONY: all clean
