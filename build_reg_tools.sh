#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$ROOT/build"
mkdir -p "$OUT"

tools=(
"src/platforms/amiga/registers/regtool.c"
"src/platforms/amiga/registers/ioharness.c"
"src/platforms/amiga/registers/dumpdisk.c"
"src/platforms/amiga/registers/motor_test.c"
)
LIBS="src/gpio/ps_protocol_kmod.c"

echo "Building register tools into $OUT..."
for src in "${tools[@]}"; do
  bin="$OUT/$(basename "${src%.c}")"
  echo "  cc $src -> $bin"
  cc -O2 -std=c11 -I"$ROOT" -I"$ROOT/include/uapi" -o "$bin" "$src" $LIBS
done
echo "Done."
