include ../../../../../config.mk

RTG_CFLAGS += -DRTG_GFX_MEM=$(RTG_GFX_MEM)

AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_PATH := $(AMIGA_TOOLCHAIN)/bin:$(PATH)

M68K_GCC ?= m68k-amigaos-gcc
P96DEV ?= ../../../../../amiga.dev/Picasso96Develop

CPU ?= 020
TARGET = pigfx.card
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


SRC ?= pigfx-2.c

//CFLAGS ?= $(CPUFLAG) -O3 -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -nostartfiles
CFLAGS ?= $(CPUFLAG) -O3 -noixemul -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -nostartfiles -lamiga -DIRTG

ifneq ($(P96DEV),)
CFLAGS += -I$(P96DEV)/Include
endif
ifneq ($(RTG_GFX_MEM),)
CFLAGS +=   -DRTG_GFX_MEM=$(RTG_GFX_MEM)
endif
PIGFX_DEBUG ?= 0
CFLAGS += -DPIGFX_DEBUG=$(PIGFX_DEBUG)
LDFLAGS ?= -noixemul -lamiga

all: $(TARGET)

# Always rebuild the card target for this CPU config
$(TARGET): $(SRC)
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f pigfx.card

install:
	sudo cp pigfx.card /opt/pistorm64/data/a314-shared/pigfx.card
	sudo cp PiGFX.info /opt/pistorm64/data/a314-shared/

FORCE:

.PHONY: all clean install FORCE
