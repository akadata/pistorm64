#!/usr/bin/env bash
set -euo pipefail

arg="${1:-}"                 # empty when no arg given
LOG="${LOG:-clang-build.log}" # allow override: LOG=... ./rebuildcount.sh
OUT="clang-diagnostics.log"

# Grab compiler diagnostics (now includes note/warning/error).
# This matches typical clang lines like:
#   <path>:<line>:<col>: warning: ...
#   <path>:<line>:<col>: error: ...
#   <path>:<line>:<col>: note: ...
rg -n ":\s*(fatal error|error|warning|note):" "$LOG" > "$OUT" || true

count() { rg -c "$1" "$OUT" 2>/dev/null || echo 0; }

# Totals by severity
TOTAL=$(wc -l < "$OUT" 2>/dev/null || echo 0)
NOTES=$(count ":\s*note:")
WARNINGS=$(count ":\s*warning:")
ERRORS=$(count ":\s*error:")
FATALS=$(count ":\s*fatal error:")

# Where they come from (directories)
MUSASHI=$(count "src/musashi/")
SOFTFLOAT=$(count "src/softfloat/")

# Hotspot files you named
M68KMAKE=$(count "m68kmake\.c")
M68KCPUH=$(count "m68kcpu\.h")
SOFTFLOATH=$(count "softfloat\.h")
M68KH=$(count "m68k\.h")
M68KC=$(count "m68k\.c")
M68KDASM=$(count "m68kdasm\.c")
M68KMMU=$(count "m68kmmu\.h")
M68KC=$(count "m68k\.c")
M68CONF=$(count "m68kconf\.h")
SOFTFLOATFPSP=$(count "softfloat_fpsp\.c")
SOFTFLOATSPECIALIZE=$(count "softfloat-specialize\.h")

# Existing per-file buckets (kept)
RTG=$(count "rtg\.c")
RTG_RAYLIB=$(count "rtg-output-raylib\.c")
RTG_GFX=$(count "rtg-gfx\.c")
PISCSI=$(count "piscsi\.c")
PISTORMDEV=$(count "pistorm-dev\.c")
PIAHI=$(count "pi_ahi\.c")

echo "- total diagnostics: $TOTAL"
echo "- notes: $NOTES"
echo "- warnings: $WARNINGS"
echo "- errors: $ERRORS"
echo "- fatal errors: $FATALS"
echo
echo "- src/musashi/: $MUSASHI"
echo "- src/softfloat/: $SOFTFLOAT"
echo
echo "- m68kmake.c: $M68KMAKE"
echo "- m68kcpu.h: $M68KCPUH"
echo "- m68k.h: $M68KH"
echo "- m68k.c: $M68KC"
echo "- softfloat.h: $SOFTFLOATH"
echo "- softfloat_fpsp.c: $SOFTFLOATFPSP"
echo "- softfloat-specialize.h: $SOFTFLOATSPECIALIZE"
echo "- m68kdasm.c: $M68KDASM"
echo "- m68kmmu.h: $M68KMMU"
echo "- m68kconf: $M68CONF"
echo

echo "- rtg.c: $RTG"
echo "- rtg-output-raylib.c: $RTG_RAYLIB"
echo "- rtg-gfx.c: $RTG_GFX"
echo "- piscsi.c: $PISCSI"
echo "- pistorm-dev.c: $PISTORMDEV"
echo "- pi_ahi.c: $PIAHI"

# Bottom-up output:
#   ./rebuildcount.sh 1                -> all diagnostics bottom-up
#   ./rebuildcount.sh warning:         -> only warnings bottom-up
#   ./rebuildcount.sh error:           -> only errors bottom-up
#   ./rebuildcount.sh src/musashi/     -> only musashi diagnostics bottom-up
#   ./rebuildcount.sh piscsi.c         -> only piscsi diagnostics bottom-up
if [[ "$arg" == "1" ]]; then
echo
echo "---- diagnostics (bottom-up, all) ----"
tac "$OUT" | sed -n '1,200p'
elif [[ -n "$arg" ]]; then
echo
echo "---- diagnostics (bottom-up, filter: $arg) ----"
tac "$OUT" | rg -n --fixed-strings "$arg" | sed -n '1,200p' || true
fi

