AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_PATH := $(AMIGA_TOOLCHAIN)/bin:$(PATH)

M68K_GCC ?= m68k-amigaos-gcc
P96DEV ?=

CPU ?= 020

ifeq ($(CPU),020)
    TARGET = PiGFX020.card
    CPUFLAG = -m68020
endif
ifeq ($(CPU),030)
    TARGET = PiGFX030.card
    CPUFLAG = -m68030
endif
ifeq ($(CPU),040)
    TARGET = PiGFX040.card
    CPUFLAG = -m68040
endif
ifeq ($(CPU),060)
    TARGET = PiGFX060.card
    CPUFLAG = -m68060
endif


SRC ?= pigfx-2.c

CFLAGS ?= $(CPUFLAG) -O3 -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -nostartfiles
ifneq ($(P96DEV),)
CFLAGS += -I$(P96DEV)/Include
endif
ifneq ($(CHIP_RAM_SIZE),)
CFLAGS += -DPIGFX_CHIP_RAM_FALLBACK=$(CHIP_RAM_SIZE)
endif
PIGFX_DEBUG ?= 0
CFLAGS += -DPIGFX_DEBUG=$(PIGFX_DEBUG)
LDFLAGS ?= -noixemul -lamiga

all: $(TARGET)

$(TARGET): $(SRC)
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f PiGFX*.card

.PHONY: all clean
