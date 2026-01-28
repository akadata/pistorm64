AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_PATH := $(AMIGA_TOOLCHAIN)/bin:$(PATH)

M68K_GCC ?= m68k-amigaos-gcc

CPU ?= 020

ifeq ($(CPU),000)
TARGET = PiNet000.device
CPUFLAG = -m68000
endif
ifeq ($(CPU),010)
TARGET = PiNet010.device
CPUFLAG = -m68010
endif
ifeq ($(CPU),020)
TARGET = PiNet020.device
CPUFLAG = -m68020
endif
ifeq ($(CPU),030)
TARGET = PiNet030.device
CPUFLAG = -m68030
endif
ifeq ($(CPU),040)
TARGET = PiNet040.device
CPUFLAG = -m68040
endif
ifeq ($(CPU),060)
TARGET = PiNet060.device
CPUFLAG = -m68060
endif
SRC ?= pi-net-amiga-2.c

CFLAGS ?= $(CPUFLAG) -O2 -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -nostartfiles
LDFLAGS ?= -lm -ldebug

all: $(TARGET)

$(TARGET): $(SRC)
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f PiNet*.device pi-net.device pi-net000.device pi-net010.device pi-net020.device pi-net030.device pi-net040.device pi-net060.device

.PHONY: all clean
