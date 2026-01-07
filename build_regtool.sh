#!/bin/sh
set -eu

rev="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
date_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

./tools/write_build_version.sh regtool

gcc -O2 -Wall -Wextra -I./ \
  -DBUILD_GIT_REV=\"${rev}\" -DBUILD_DATE=\"${date_utc}\" \
  platforms/amiga/registers/regtool.c gpio/ps_protocol.c gpio/rpi_peri.c -o regtool
