#!/usr/bin/env bash
set -euo pipefail
mkdir -p /tank/pistorm/ipc
OUT_TAG="${1:-$(date +%Y%m%d_%H%M%S)}"
LOG="/tank/pistorm/ipc/markers_${OUT_TAG}.log"
exec socat TCP-LISTEN:9009,reuseaddr,fork \
  EXEC:"tee -a '$LOG'"