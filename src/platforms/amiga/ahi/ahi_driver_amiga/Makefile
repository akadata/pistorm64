AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_PATH := $(AMIGA_TOOLCHAIN)/bin:$(PATH)

M68K_GCC ?= m68k-amigaos-gcc
VASM ?= vasmm68k_mot
VBCC ?=
AHI_INC ?=
AHI_SDK ?=

CPU ?= 030
DEVICE_TARGET = pi-ahi.audio

ifeq ($(CPU),000)
CPUFLAG = -m68000
endif
ifeq ($(CPU),010)
CPUFLAG = -m68010
endif
ifeq ($(CPU),020)
CPUFLAG = -m68020
endif
ifeq ($(CPU),030)
CPUFLAG = -m68030
endif
ifeq ($(CPU),040)
CPUFLAG = -m68040
endif
ifeq ($(CPU),060)
CPUFLAG = -m68060
endif

VASM_INCLUDES :=
ifneq ($(VBCC),)
VASM_INCLUDES := -I$(VBCC)/NDK39/include/include_i
endif
ifneq ($(AMIGA_HEADERS),)
VASM_INCLUDES += -I$(AMIGA_HEADERS)
endif

VASM_CPUFLAG ?= -m68020
ifeq ($(CPU),000)
VASM_CPUFLAG = -m68000
endif
ifeq ($(CPU),010)
VASM_CPUFLAG = -m68010
endif
ifeq ($(CPU),020)
VASM_CPUFLAG = -m68020
endif
ifeq ($(CPU),030)
VASM_CPUFLAG = -m68030
endif
ifeq ($(CPU),040)
VASM_CPUFLAG = -m68040
endif
ifeq ($(CPU),060)
VASM_CPUFLAG = -m68060
endif

PREFS_TARGET ?= PI-AHI
PREFS_SRC ?= prefsfile.a

DEVICE_SRC ?= pi-ahi.c

DEVICE_CFLAGS ?= $(CPUFLAG) -O3 -Wall -Wextra -Wno-unused-parameter -nostartfiles
ifneq ($(AHI_SDK),)
DEVICE_CFLAGS += -I$(AHI_SDK)/Include
endif
ifneq ($(AHI_INC),)
DEVICE_CFLAGS += -I$(AHI_INC)
endif

ALL_TARGETS := $(DEVICE_TARGET)
ifneq ($(wildcard $(PREFS_SRC)),)
ALL_TARGETS += $(PREFS_TARGET)
else
$(info [ahi_driver] prefsfile.a not found; skipping $(PREFS_TARGET). Set PREFS_SRC or provide the file to build it.)
endif

all: $(ALL_TARGETS)

$(PREFS_TARGET): $(PREFS_SRC)
	PATH="$(AMIGA_PATH)" $(VASM) -quiet -phxass -Fhunk $(VASM_CPUFLAG) -o $@ $< $(VASM_INCLUDES)

$(DEVICE_TARGET): $(DEVICE_SRC)
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(DEVICE_CFLAGS) -o $@ $<

clean:
	rm -f $(PREFS_TARGET) pi-ahi.audio 

install:
	cp  pi-ahi.audio /opt/pistorm64/data/a314-shared/

.PHONY: all clean
