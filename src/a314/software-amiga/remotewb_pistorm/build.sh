#!/usr/bin/env sh
set -e

PATH=/opt/amiga/bin:$PATH

vasmm68k_mot -Fhunk -quiet vblank_server.asm -o vblank_server.o
m68k-amigaos-gcc -m68000 -O2 -c remotewb.c -o remotewb.o
m68k-amigaos-gcc -m68000 -o ../remotewb remotewb.o vblank_server.o -lamiga
