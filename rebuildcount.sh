#!/usr/bin/env bash
set -euo pipefail

arg="${1:-}"          # empty when no arg given
LOG="clang-build.log"
OUT="core-warnings.log"

# Build core warnings log (exclude musashi + softfloat)
rg "warning:" "$LOG" | rg -v 'src/musashi/' | rg -v 'src/softfloat/' > "$OUT" || true

count() { rg -c "$1" "$OUT" 2>/dev/null || echo 0; }

TOTAL=$(count "warning:")
RTG=$(count "src/platforms/amiga/rtg/rtg\.c|(^|/)rtg\.c")
RTG_GFX=$(count "rtg-gfx\.c")
RTG_RAYLIB=$(count "rtg-output-raylib\.c")
PISCSI=$(count "piscsi\.c")
PISTORMDEV=$(count "pistorm-dev\.c")
PIAHI=$(count "pi_ahi\.c")

echo "- total core warnings: $TOTAL"
echo "- rtg.c: $RTG"
echo "- rtg-output-raylib.c: $RTG_RAYLIB"
echo "- rtg-gfx.c: $RTG_GFX"
echo "- piscsi.c: $PISCSI"
echo "- pistorm-dev.c: $PISTORMDEV"
echo "- pi_ahi.c: $PIAHI"

# Bottom-up output:
#   ./rebuildcount.sh 1         -> all warnings bottom-up
#   ./rebuildcount.sh piscsi.c  -> only piscsi.c warnings bottom-up
#   ./rebuildcount.sh rtg.c     -> only rtg.c warnings bottom-up
if [[ "$arg" == "1" ]]; then
  echo
  echo "---- core warnings (bottom-up, all) ----"
  tac "$OUT" | sed -n '1,200p'
elif [[ -n "$arg" ]]; then
  echo
  echo "---- core warnings (bottom-up, filter: $arg) ----"
  # match either the literal filename, or a path containing it
  tac "$OUT" | rg -n --fixed-strings "$arg" | sed -n '1,200p' || true
fi

