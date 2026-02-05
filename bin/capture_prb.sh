#!/usr/bin/env bash
set -euo pipefail
sigrok-cli -d fx2lafw --config samplerate=24mhz --time 3s -O srzip -o capture.sr &
CAP=$!
sleep 0.3
ssh -p 9022 pi '
  sudo ./pistorm/build/regtool --force --write8 0xBFD300 0xFF
  sudo ./pistorm/build/regtool --force --write8 0xBFD100 0xFF
  for b in 7 6 5 4 3 2 1 0; do
    sudo ./pistorm/build/regtool --force --write8 0xBFD100 $((0xFF ^ (1 << b)))
    sleep 0.25
    sudo ./pistorm/build/regtool --force --write8 0xBFD100 0xFF
    sleep 0.1
  done
'
wait $CAP
