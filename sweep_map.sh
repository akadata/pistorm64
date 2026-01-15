#!/usr/bin/env bash
set -euo pipefail

echo "Setting DDRB=0xFF and PRB=0xFF..."
sudo ./build/regtool --force --write8 0xBFD300 0xFF
sudo ./build/regtool --force --write8 0xBFD100 0xFF
sleep 0.2

declare -A map

for bit in 7 6 5 4 3 2 1 0; do
val=$((0xFF ^ (1 << bit)))
printf "Toggling PRB bit %d low (PRB=0x%02X). Observe LA and enter channel (0-7): " "$bit" "$val"
sudo ./build/regtool --force --write8 0xBFD100 "$val"
read -r chan
map[$bit]="$chan"
sudo ./build/regtool --force --write8 0xBFD100 0xFF
sleep 0.1
done

echo "Mapping (PRB bit -> LA channel):"
for bit in 7 6 5 4 3 2 1 0; do
echo "  bit $bit -> ${map[$bit]}"
done
