#!/usr/bin/env bash
set -euo pipefail

OUT_TAG="${1:-run_$(date +%Y%m%d_%H%M%S)}"
CAP="captures/${OUT_TAG}.srzip"
LOG="ipc/${OUT_TAG}_markers.log"

mkdir -p captures ipc

# 1) marker listener (background)
socat -u TCP-LISTEN:9009,reuseaddr,fork \
  SYSTEM:"ts \"[%Y-%m-%d %H:%M:%S]\" | tee -a /tank/pistorm/ipc/${OUT_TAG}_markers.log" &
LISTEN_PID=$!

# 2) ensure tunnel exists (run on amiga; put it in tmux or background there)
amiga 'pgrep -f "ssh -N -R 9009:/tank/pistorm/ipc/amiga.sock homer" >/dev/null || (nohup ssh -N -R 9009:/tank/pistorm/ipc/amiga.sock homer >/tmp/ipc_tunnel.log 2>&1 &)' || true

# 3) start sigrok capture (background)
sudo sigrok-cli -d fx2lafw:conn=1.27 -c samplerate=24MHz --time 3000 \
  --channels D0,D1,D2,D3,D4,D5,D6,D7 \
  -O srzip -o "$CAP" &
CAP_PID=$!

# 4) marker: capture armed
amiga 'echo "CAPTURE_ARM '"$OUT_TAG"' $(date +%s.%N)" | socat - TCP:127.0.0.1:9009' || true

# 5) start emulator (replace with your known command)
amiga 'echo "EMU_START '"$OUT_TAG"' $(date +%s.%N)" | socat - TCP:127.0.0.1:9009; cd "$HOME/pistorm" && timeout 60s sudo ./emulator' || true

wait "$CAP_PID" || true
kill "$LISTEN_PID" 2>/dev/null || true

echo "Capture: $CAP"
echo "Markers: /tank/pistorm/ipc/${OUT_TAG}_markers.log"