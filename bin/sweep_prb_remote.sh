#!/usr/bin/env bash
# Capture a PRB sweep while toggling CIABPRB values on the Pi.
# - Starts a 24 MHz FX2 capture for the given duration into capture_prb.srzip
# - SSHes to the Pi (default pi@localhost:9022) to write PRB values (default 255..0)
# - Requires sigrok-cli on this host and regtool built on the Pi.

set -euo pipefail

SSH_TARGET="${SSH_TARGET:-pi}"
SSH_PORT="${SSH_PORT:-9022}"
OUT="${OUT:-capture_prb.srzip}"
CAPTURE_TIME="${CAPTURE_TIME:-3s}"
SAMPLERATE="${SAMPLERATE:-24mhz}"
PRB_START="${PRB_START:-255}"
PRB_STOP="${PRB_STOP:-0}"
PRB_STEP="${PRB_STEP:--1}"
PRB_DELAY="${PRB_DELAY:-0.1}"  # seconds between writes

echo "Starting capture to $OUT at $SAMPLERATE for $CAPTURE_TIME..."
sigrok-cli -d fx2lafw --config samplerate="$SAMPLERATE" --time "$CAPTURE_TIME" -O srzip -o "$OUT" &
CAP_PID=$!
sleep 0.3

echo "Writing PRB values via SSH ${SSH_TARGET}:${SSH_PORT} (seq $PRB_START $PRB_STEP $PRB_STOP)..."
ssh -p "$SSH_PORT" "$SSH_TARGET" bash -s <<EOSSH
set -euo pipefail
sudo ./pistorm/build/regtool --force --write8 0xBFD300 0xFF
sudo ./pistorm/build/regtool --force --write8 0xBFD100 0xFF
for val in \$(seq "$PRB_START" "$PRB_STEP" "$PRB_STOP"); do
  sudo ./pistorm/build/regtool --force --write8 0xBFD100 "\$val"
  sleep "$PRB_DELAY"
done
EOSSH

wait "$CAP_PID"
echo "Capture complete: $OUT"
