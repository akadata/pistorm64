#!/usr/bin/env bash
set -euo pipefail

OUT_TAG="${1:-run_$(date +%Y%m%d_%H%M%S)}"
CAP="captures/${OUT_TAG}.srzip"
LOG="ipc/${OUT_TAG}_markers.log"

mkdir -p captures ipc

# 1) Start marker listener (background)
socat TCP-LISTEN:9009,reuseaddr,fork \
  EXEC:'tee -a /tank/pistorm/ipc/'${OUT_TAG}'_markers.log' &
LISTEN_PID=$!

# 2) Start sigrok capture (background)
sudo sigrok-cli -d fx2lafw:conn=1.27 -c samplerate=24MHz --time 3000 \
  --channels D0,D1,D2,D3,D4,D5,D6,D7 \
  -O srzip -o "$CAP" &
CAP_PID=$!

# 3) Send capture armed marker
sleep 2  # Give capture time to start
amiga 'echo "CAPTURE_ARM '"$OUT_TAG"' $(date +%s.%N)" | nc <homer_ip_address> 9009' || true

# 4) Run emulator or register tool
amiga 'echo "EMU_START '"$OUT_TAG"' $(date +%s.%N)" | nc <homer_ip_address> 9009; timeout 60s sudo ./emulator; echo "EMU_STOP '"$OUT_TAG"' $(date +%s.%N)" | nc <homer_ip_address> 9009' || true

# Wait for capture to complete
sleep 5
kill "$CAP_PID" 2>/dev/null || true
kill "$LISTEN_PID" 2>/dev/null || true

echo "Capture: $CAP"
echo "Markers: /tank/pistorm/ipc/${OUT_TAG}_markers.log"