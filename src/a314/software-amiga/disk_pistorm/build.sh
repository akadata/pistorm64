#!/usr/bin/env sh
set -e

PATH=/opt/amiga/bin:$PATH

vasmm68k_mot -Fhunk -quiet romtag.asm -o romtag.o
m68k-amigaos-gcc -m68000 -O2 -c device.c -o device.o
m68k-amigaos-gcc -m68000 -O2 -c debug.c -o debug.o
m68k-amigaos-gcc -m68000 -nostartfiles -o ../a314disk.device romtag.o device.o debug.o -lamiga
