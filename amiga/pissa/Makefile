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

LIB      := lib/libpissa.a
LIB_OBJS := lib/pissa.o

TOOL_020 := C/pissa020
TOOL_030 := C/pissa030
TOOL_040 := C/pissa040
TOOL     := C/pissa

TOOL_OBJS_020 := C/pissa020.o
TOOL_OBJS_030 := C/pissa030.o
TOOL_OBJS_040 := C/pissa040.o

all: $(LIB) $(TOOL) $(TOOL_020) $(TOOL_030) $(TOOL_040)

lib/pissa.o: lib/pissa.c include/pissa.h
	$(CC) $(CFLAGS_020) -c -o $@ $<

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

C/pissa020.o: C/pissa.c include/pissa.h
	$(CC) $(CFLAGS_020) -c -o $@ $<

C/pissa030.o: C/pissa.c include/pissa.h
	$(CC) $(CFLAGS_030) -c -o $@ $<

C/pissa040.o: C/pissa.c include/pissa.h
	$(CC) $(CFLAGS_040) -c -o $@ $<

$(TOOL_020): $(TOOL_OBJS_020) $(LIB)
	$(CC) $(CFLAGS_020) -o $@ $(TOOL_OBJS_020) -Llib -lpissa

$(TOOL_030): $(TOOL_OBJS_030) $(LIB)
	$(CC) $(CFLAGS_030) -o $@ $(TOOL_OBJS_030) -Llib -lpissa

$(TOOL_040): $(TOOL_OBJS_040) $(LIB)
	$(CC) $(CFLAGS_040) -o $@ $(TOOL_OBJS_040) -Llib -lpissa

$(TOOL): $(TOOL_030)
	cp $(TOOL_030) $(TOOL)

install: all
	mkdir -p $(AMIGA_PREFIX)/include $(AMIGA_PREFIX)/lib $(AMIGA_PREFIX)/C
	cp include/pissa.h $(AMIGA_PREFIX)/include/
	cp lib/libpissa.a $(AMIGA_PREFIX)/lib/
	cp C/pissa $(AMIGA_PREFIX)/C/
	cp C/pissa020 $(AMIGA_PREFIX)/C/
	cp C/pissa030 $(AMIGA_PREFIX)/C/
	cp C/pissa040 $(AMIGA_PREFIX)/C/

clean:
	rm -f lib/*.o C/*.o $(LIB) $(TOOL) $(TOOL_020) $(TOOL_030) $(TOOL_040)

.PHONY: all install clean
