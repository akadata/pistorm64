#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

# Canonical build
make full C=clang V=1 2>clang-build.log

# Filter warnings
rg "warning:" clang-build.log \
  | rg -v 'src/musashi/' \
  | rg -v 'src/softfloat/' \
  > core-warnings.log || true

TOTAL=$(rg "warning:" core-warnings.log 2>/dev/null | wc -l || echo 0)
RTG=$(rg "rtg-gfx.c" core-warnings.log 2>/dev/null | wc -l || echo 0)
PISCSI=$(rg "piscsi.c" core-warnings.log 2>/dev/null | wc -l || echo 0)
PIDEV=$(rg "pistorm-dev.c" core-warnings.log 2>/dev/null | wc -l || echo 0)
PIAHI=$(rg "pi_ahi.c" core-warnings.log 2>/dev/null | wc -l || echo 0)

printf 'total core warnings: %s\n' "$TOTAL"
printf 'rtg-gfx.c: %s\n' "$RTG"
printf 'piscsi.c: %s\n' "$PISCSI"
printf 'pistorm-dev.c: %s\n' "$PIDEV"
printf 'pi_ahi.c: %s\n' "$PIAHI"

