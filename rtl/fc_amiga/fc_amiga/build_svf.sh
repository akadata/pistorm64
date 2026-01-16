#!/usr/bin/env bash
# Build helper for FC-enabled Amiga CPLD variants.
# Usage: ./build_svf.sh <project_name_without_ext> (e.g. pistorm_fc_amiga_EPM240)
# Env: QUARTUS_BIN may be set to point at Quartus bin dir (default /opt/intelFPGA_lite/20.1/quartus/bin)
# Env: FREQ sets JTAG clock for cpf (-q), default 100KHz
set -euo pipefail

: "${QUARTUS_BIN:=/opt/intelFPGA_lite/20.1/quartus/bin}"
PROJ="${1:?usage: $0 <project_name_without_ext>}"
FREQ="${FREQ:-100KHz}"

echo "[1/3] Compile: $PROJ"
"$QUARTUS_BIN/quartus_sh" --flow compile "$PROJ"

POF="output_files/${PROJ}.pof"
SVF="${PROJ}.svf"

if [[ ! -f "$POF" ]]; then
  echo "Missing POF: $POF"
  exit 1
fi

echo "[2/3] Convert POF -> SVF: $SVF"
"$QUARTUS_BIN/quartus_cpf" -c -q "$FREQ" -g 3.3 -n p "$POF" "$SVF"

echo "[3/3] Done: $SVF"
ls -lh "$SVF"
