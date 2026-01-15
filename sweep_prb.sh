#!/usr/bin/env bash
set -euo pipefail

# CIAB DDRB -> outputs, PRB -> all high
sudo ./build/regtool --force --write8 0xBFD300 0xFF
sudo ./build/regtool --force --write8 0xBFD100 0xFF
sleep 0.2

# Toggle each PRB bit low for 250ms, one at a time, then return high
for bit in 7 6 5 4 3 2 1 0; do
val=$((0xFF ^ (1 << bit)))
printf "PRB bit %d low (PRB=0x%02X)\n" "$bit" "$val"
sudo ./build/regtool --force --write8 0xBFD100 "$val"
sleep 0.25
sudo ./build/regtool --force --write8 0xBFD100 0xFF
sleep 0.10
done
