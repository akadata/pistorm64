#!/usr/bin/env bash
# Sweep test assuming an external clock is driven into GPIO4.
# Pi does NOT drive GPIO4 (PISTORM_ENABLE_GPCLK=0); GPIO4 must be muxed to GPCLK0 (ALT0).

set -e

# Frequencies to test (informational only; external source must be set manually)
FREQS=(50000000 100000000 150000000 200000000)

export PISTORM_PROTOCOL=old
export PISTORM_OLD_NO_HANDSHAKE=1
export PISTORM_RP1_CLK_FUNCSEL=0
export PISTORM_ENABLE_GPCLK=0
export PISTORM_TXN_TIMEOUT_US=200000

echo "[ext-clock-sweep] Ensure GPIO4 is muxed to GPCLK0:"
sudo pinctrl set 4 a0
sudo pinctrl get 4

for f in "${FREQS[@]}"; do
  echo "=== external freq ≈ $f Hz (set your osc accordingly) ==="
  # Optional: tell debug scripts what we think we're driving
  export PISTORM_EXT_CLK_HINT=$f

  sudo env PISTORM_PROTOCOL=$PISTORM_PROTOCOL \
           PISTORM_OLD_NO_HANDSHAKE=$PISTORM_OLD_NO_HANDSHAKE \
           PISTORM_RP1_CLK_FUNCSEL=$PISTORM_RP1_CLK_FUNCSEL \
           PISTORM_ENABLE_GPCLK=$PISTORM_ENABLE_GPCLK \
           PISTORM_TXN_TIMEOUT_US=$PISTORM_TXN_TIMEOUT_US \
           ./emulator --bus-probe | head -n 8
  echo
  sleep 1
done
