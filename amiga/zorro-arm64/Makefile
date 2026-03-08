# SPDX-License-Identifier: MIT

AMIGA_PREFIX ?= /opt/amiga
CROSS ?= $(AMIGA_PREFIX)/bin/m68k-amigaos-

CC := $(CROSS)gcc
CFLAGS := -m68030 -O2 -Wall -Wextra -noixemul \
	-I../../src/platforms/amiga/zorro/arm64_accel \
	-I../../amiga.dev/Picasso96Develop/Include

TOOL := C/armshake
TOOL_OBJS := C/armshake.o
ELF_BUILDER := arm64-payloads/build_julia_elf.sh

all: $(TOOL)

C/armshake.o: C/armshake.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TOOL): $(TOOL_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TOOL_OBJS)

install: all
	cp C/armshake /opt/pistorm64/data/a314-shared/

elf-julia:
	$(ELF_BUILDER)

install-julia-elf: elf-julia
	cp arm64-payloads/julia_arm64.elf /opt/pistorm64/data/a314-shared/

clean:
	rm -f C/*.o $(TOOL)

.PHONY: all install clean elf-julia install-julia-elf
