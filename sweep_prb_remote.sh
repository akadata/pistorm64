#!/usr/bin/env bash
# Capture a PRB bit sweep while toggling CIABPRB bits on the Pi.
# - Starts a 24 MHz FX2 capture for ~3s into capture_prb.srzip
# - SSHes to the Pi (default pi@localhost:9022) to toggle PRB bits 7..0 low/high
# - Requires sigrok-cli on this host and regtool built on the Pi.

set -euo pipefail

SSH_TARGET="${SSH_TARGET:-pi}"
SSH_PORT="${SSH_PORT:-9022}"
OUT="${OUT:-capture_prb.srzip}"
CAPTURE_TIME="${CAPTURE_TIME:-3s}"
SAMPLERATE="${SAMPLERATE:-24mhz}"

echo "Starting capture to $OUT at $SAMPLERATE for $CAPTURE_TIME..."
sigrok-cli -d fx2lafw --config samplerate="$SAMPLERATE" --time "$CAPTURE_TIME" -O srzip -o "$OUT" &
CAP_PID=$!
sleep 0.3

echo "Toggling PRB bits via SSH ${SSH_TARGET}:${SSH_PORT}..."
ssh -p "$SSH_PORT" "$SSH_TARGET" bash -s <<'EOSSH'
set -euo pipefail
sudo ./pistorm/build/regtool --force --write8 0xBFD300 0xFF
sudo ./pistorm/build/regtool --force --write8 0xBFD100 0xFF
for b in 7 6 5 4 3 2 1 0; do
  val=$((0xFF ^ (1 << b)))
  sudo ./pistorm/build/regtool --force --write8 0xBFD100 "$val"
  sleep 0.25
  sudo ./pistorm/build/regtool --force --write8 0xBFD100 0xFF
  sleep 0.10
done
EOSSH

wait "$CAP_PID"
echo "Capture complete: $OUT"
