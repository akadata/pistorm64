#!/usr/bin/env bash
set -euo pipefail

gcc -O2 -Wall -Wextra -I./ src/platforms/amiga/registers/piamisound.c src/gpio/ps_protocol.c src/gpio/rpi_peri.c -lm -o piamisound
