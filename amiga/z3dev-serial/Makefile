# SPDX-License-Identifier: MIT

AMIGA_PREFIX ?= /opt/amiga
CROSS ?= $(AMIGA_PREFIX)/bin/m68k-amigaos-

CC      := $(CROSS)gcc
AR      := $(CROSS)ar
RANLIB  := $(CROSS)ranlib

CFLAGS  := -O2 -Wall -Wextra -Iinclude
LDFLAGS :=

LIB      := lib/libz3serial.a
LIB_OBJS := lib/z3serial.o

TOOL      := C/serialecho
TOOL_OBJS := C/serialecho.o

all: $(LIB) $(TOOL)

lib/z3serial.o: lib/z3serial.c include/z3serial.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

C/serialecho.o: C/serialecho.c include/z3serial.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TOOL): $(TOOL_OBJS) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(TOOL_OBJS) -Llib -lz3serial $(LDFLAGS)

install: all
	mkdir -p $(AMIGA_PREFIX)/include $(AMIGA_PREFIX)/lib $(AMIGA_PREFIX)/C
	cp include/z3serial.h $(AMIGA_PREFIX)/include/
	cp lib/libz3serial.a $(AMIGA_PREFIX)/lib/
	cp C/serialecho $(AMIGA_PREFIX)/C/

clean:
	rm -f lib/*.o C/*.o $(LIB) $(TOOL)

.PHONY: all install clean
