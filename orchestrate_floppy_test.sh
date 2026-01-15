#!/usr/bin/env bash
set -euo pipefail

OUT_TAG="${1:-floppy_test_$(date +%Y%m%d_%H%M%S)}"
CAP="captures/${OUT_TAG}.srzip"
LOG="ipc/${OUT_TAG}_markers.log"

mkdir -p captures ipc

# 1) marker listener (background) - listen on TCP port
socat TCP-LISTEN:9009,reuseaddr,fork EXEC:'tee -a /tank/pistorm/ipc/'${OUT_TAG}'_markers.log' &
LISTEN_PID=$!

# 2) start sigrok capture (background)
sudo sigrok-cli -d fx2lafw:conn=1.27 -c samplerate=24MHz --time 3000 \
  --channels D0,D1,D2,D3,D4,D5,D6,D7 \
  -O srzip -o "$CAP" &
CAP_PID=$!

# 3) marker: capture armed
sleep 2  # Give capture time to start
amiga 'echo "CAPTURE_ARM '"$OUT_TAG"' $(date +%s.%N)" | nc 172.16.0.2 9009' || true

# 4) stop emulator if running
amiga 'sudo pkill -f emulator 2>/dev/null || true; sleep 2'

# 5) run dumpdisk test with markers
amiga 'echo "START_DUMPDISK '"$OUT_TAG"' $(date +%s.%N)" | nc 172.16.0.2 9009; timeout 30s sudo /home/smalley/pistorm/build/dumpdisk --out /tmp/dump_test.raw --drive 0 --tracks 1 --sides 1; echo "END_DUMPDISK '"$OUT_TAG"' $(date +%s.%N)" | nc 172.16.0.2 9009' || true

# Wait for capture to complete
sleep 5
kill "$CAP_PID" 2>/dev/null || true
kill "$LISTEN_PID" 2>/dev/null || true

echo "Capture: $CAP"
echo "Markers: /tank/pistorm/ipc/${OUT_TAG}_markers.log"