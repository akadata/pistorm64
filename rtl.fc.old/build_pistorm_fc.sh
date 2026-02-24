#!/usr/bin/env bash
# Build helper for PiStorm64 FC/BERR CPLD (EPM240).
# Usage: ./build_pistorm_fc.sh <project_name_without_ext>
# Env: QUARTUS_BIN points at Quartus bin dir (default /opt/intelFPGA/20.1/quartus/bin)
# Env: FREQ sets JTAG clock for cpf (-q), default 100KHz
set -euo pipefail

: "${QUARTUS_BIN:=/opt/intelFPGA/20.1/quartus/bin}"
PROJ="${1:?usage: $0 <project_name_without_ext>}"
FREQ="${FREQ:-100KHz}"

if ! command -v quartus_sh >/dev/null 2>&1 && [[ ! -x "$QUARTUS_BIN/quartus_sh" ]]; then
  for candidate in \
    "/opt/intelFPGA/20.1/quartus/bin" \
    "/opt/intelFPGA/20.1/quartus/bin64" \
    "/opt/intelFPGA_lite/20.1/quartus/bin" \
    "/opt/intelFPGA_lite/20.1/quartus/bin64" \
    "${QUARTUS_ROOT:-}/quartus/bin" \
    "${QUARTUS_ROOT:-}/quartus/bin64"; do
    if [[ -x "$candidate/quartus_sh" ]]; then
      QUARTUS_BIN="$candidate"
      break
    fi
  done
fi

if ! command -v quartus_sh >/dev/null 2>&1 && [[ ! -x "$QUARTUS_BIN/quartus_sh" ]]; then
  echo "Missing quartus_sh. Install Quartus Prime (Lite is fine), or set QUARTUS_BIN/QUARTUS_ROOT."
  exit 1
fi

if [[ ! -f "${PROJ}.qpf" ]]; then
  echo "Missing project file: ${PROJ}.qpf"
  exit 1
fi

echo "[1/3] Compile: $PROJ"
if command -v quartus_sh >/dev/null 2>&1; then
  quartus_sh --flow compile "$PROJ"
else
  "$QUARTUS_BIN/quartus_sh" --flow compile "$PROJ"
fi

POF="output_files/${PROJ}.pof"
SVF="${PROJ}.svf"

if [[ ! -f "$POF" ]]; then
  echo "Missing POF: $POF"
  exit 1
fi

echo "[2/3] Convert POF -> SVF: $SVF"
if command -v quartus_cpf >/dev/null 2>&1; then
  quartus_cpf -c -q "$FREQ" -g 3.3 -n p "$POF" "$SVF"
else
  "$QUARTUS_BIN/quartus_cpf" -c -q "$FREQ" -g 3.3 -n p "$POF" "$SVF"
fi

echo "[3/3] Done: $SVF"
ls -lh "$SVF"
