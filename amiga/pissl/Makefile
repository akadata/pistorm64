# SPDX-License-Identifier: MIT

AMIGA_PREFIX ?= /opt/amiga
CROSS ?= $(AMIGA_PREFIX)/bin/m68k-amigaos-

CC      := $(CROSS)gcc
AR      := $(CROSS)ar
RANLIB  := $(CROSS)ranlib

CFLAGS_COMMON := -O2 -Wall -Wextra -noixemul -Iinclude
CFLAGS_020    := $(CFLAGS_COMMON) -m68020
CFLAGS_030    := $(CFLAGS_COMMON) -m68030
CFLAGS_040    := $(CFLAGS_COMMON) -m68040

LIB      := lib/libpissl.a
LIB_OBJS := lib/pissl.o

TOOL_020 := C/pissl020
TOOL_030 := C/pissl030
TOOL_040 := C/pissl040
TOOL     := C/pissl

TOOL_OBJS_020 := C/pissl020.o
TOOL_OBJS_030 := C/pissl030.o
TOOL_OBJS_040 := C/pissl040.o

all: $(LIB) $(TOOL) $(TOOL_020) $(TOOL_030) $(TOOL_040)

lib/pissl.o: lib/pissl.c include/pissl.h
	$(CC) $(CFLAGS_020) -c -o $@ $<

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

C/pissl020.o: C/pissl.c include/pissl.h
	$(CC) $(CFLAGS_020) -c -o $@ $<

C/pissl030.o: C/pissl.c include/pissl.h
	$(CC) $(CFLAGS_030) -c -o $@ $<

C/pissl040.o: C/pissl.c include/pissl.h
	$(CC) $(CFLAGS_040) -c -o $@ $<

$(TOOL_020): $(TOOL_OBJS_020) $(LIB)
	$(CC) $(CFLAGS_020) -o $@ $(TOOL_OBJS_020) -Llib -lpissl

$(TOOL_030): $(TOOL_OBJS_030) $(LIB)
	$(CC) $(CFLAGS_030) -o $@ $(TOOL_OBJS_030) -Llib -lpissl

$(TOOL_040): $(TOOL_OBJS_040) $(LIB)
	$(CC) $(CFLAGS_040) -o $@ $(TOOL_OBJS_040) -Llib -lpissl

$(TOOL): $(TOOL_030)
	cp $(TOOL_030) $(TOOL)

install: all
	mkdir -p $(AMIGA_PREFIX)/include $(AMIGA_PREFIX)/lib $(AMIGA_PREFIX)/C
	cp include/pissl.h $(AMIGA_PREFIX)/include/
	cp lib/libpissl.a $(AMIGA_PREFIX)/lib/
	cp C/pissl $(AMIGA_PREFIX)/C/
	cp C/pissl020 $(AMIGA_PREFIX)/C/
	cp C/pissl030 $(AMIGA_PREFIX)/C/
	cp C/pissl040 $(AMIGA_PREFIX)/C/

clean:
	rm -f lib/*.o C/*.o $(LIB) $(TOOL) $(TOOL_020) $(TOOL_030) $(TOOL_040)

.PHONY: all install clean
