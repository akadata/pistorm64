AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_PATH := $(AMIGA_TOOLCHAIN)/bin:$(PATH)

M68K_GCC ?= m68k-amigaos-gcc

CPU ?= 030
TARGET = net64.device

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

DEVICE_SRC ?= net64-amiga.c
DEVICE_TARGET ?= net64.device
DEVICE_CFLAGS ?= $(CPUFLAG) -O2 -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -nostartfiles
DEVICE_LDFLAGS ?= -lm -ldebug

TOOL_TARGET ?= net64info
TOOL_SRC ?= net64info.c
TOOL_CFLAGS ?= $(CPUFLAG) -O2 -Wall -Wextra -Wno-unused-parameter -noixemul
TOOL_LDFLAGS ?= -lamiga -L$(AMIGA_TOOLCHAIN)/m68k-amigaos/libnix/lib -lnix -lm
TOOL_ICON ?= net64info.info

all: $(DEVICE_TARGET) $(TOOL_TARGET)

$(DEVICE_TARGET): $(DEVICE_SRC)
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(DEVICE_CFLAGS) -o $@ $< $(DEVICE_LDFLAGS)

$(TOOL_TARGET): $(TOOL_SRC)
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(TOOL_CFLAGS) -o $@ $< $(TOOL_LDFLAGS)

install:
	cp -a $(DEVICE_TARGET) /opt/pistorm64/data/a314-shared/net64.device
	cp -a $(TOOL_TARGET) /opt/pistorm64/data/a314-shared/net64info
	cp -a $(TOOL_ICON) /opt/pistorm64/data/a314-shared/net64info.info

clean:
	rm -f net64-*.device net64.device $(TOOL_TARGET)

.PHONY: all install clean
