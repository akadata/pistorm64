# SPDX-License-Identifier: MIT

AMIGA_PREFIX ?= /opt/amiga
CROSS ?= $(AMIGA_PREFIX)/bin/m68k-amigaos-

CC := $(CROSS)gcc
CFLAGS := -m68020 -O2 -Wall -Wextra -noixemul -I../../src/platforms/amiga/zorro/ppc_accel

TOOL := C/ppcshake
TOOL_OBJS := C/ppcshake.o
ELF_BUILDER := ppc-elf/build_remote.sh

all: $(TOOL)

C/ppcshake.o: C/ppcshake.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TOOL): $(TOOL_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TOOL_OBJS)

install: all
	cp C/ppcshake /opt/pistorm64/data/a314-shared/

elf-hello:
	$(ELF_BUILDER)

clean:
	rm -f C/*.o $(TOOL)

.PHONY: all install clean elf-hello
