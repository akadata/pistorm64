# PiRTG64 Picasso96 RTG card – build script
# Copyright (c) 2026 AKADATA Limited
# Licensed under the MIT License – see LICENSE for details.
# Developed by AKADATA, with help and support from Codex.

include ../../../../../../config.mk

AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_PATH := $(AMIGA_TOOLCHAIN)/bin:$(PATH)

M68K_GCC ?= m68k-amigaos-gcc
P96DEV   ?= ../../../../../../amiga.dev/Picasso96Develop

CPU      ?= 030
TARGET   = pirtg64.card
SRC      ?= pirtg64.c

# CPU selection
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

# Optional debug level for the card
PIRTG64_DEBUG ?= 0

CFLAGS ?= $(CPUFLAG) -O3 -noixemul -Wall -Wextra -Wno-unused-parameter \
          -fomit-frame-pointer -nostartfiles -lamiga \
          -DPIRTG64_DEBUG=$(PIRTG64_DEBUG)

# Picasso96 includes
ifneq ($(P96DEV),)
CFLAGS += -I$(P96DEV)/Include
endif

# Optional: expose GFX memory size to the card, if the driver actually uses it
ifneq ($(RTG_GFX_MEM),)
CFLAGS += -DRTG_GFX_MEM=$(RTG_GFX_MEM)
endif

LDFLAGS ?= -noixemul -lamiga

all: $(TARGET)

$(TARGET): $(SRC)
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f pirtg64.card 

install:
	sudo cp pirtg64.card /opt/pistorm64/data/a314-shared/
	sudo cp PiRTG64.info /opt/pistorm64/data/a314-shared/

.PHONY: clean all  install
