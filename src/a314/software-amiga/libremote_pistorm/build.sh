#!/usr/bin/env sh
set -e

PATH=/opt/amiga/bin:$PATH

python3 gen_stubs.py bsdsocket
vasmm68k_mot -Fhunk -quiet romtag.asm -o romtag.o
m68k-amigaos-gcc -m68000 -O2 -c library.c -o library.o
m68k-amigaos-gcc -m68000 -nostartfiles -o ../bsdsocket.library romtag.o library.o -lamiga
