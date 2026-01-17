#!/usr/bin/env bash
set -euo pipefail

gcc -O2 -Wall -Wextra -I./ -I./include/uapi src/platforms/amiga/registers/piamisound.c src/gpio/ps_protocol_kmod.c -lm -o piamisound
