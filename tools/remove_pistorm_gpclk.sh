#!/usr/bin/env bash
set -euo pipefail

# Script to remove PiStorm GPCLK0 overlay and kernel module for Pi5

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GPCLK_DIR="$REPO_ROOT/pi5/gpclk0"

echo "[remove-pistorm-gpclk] Removing PiStorm GPCLK0 setup..."

# Remove all possible overlay instances by name
sudo dtoverlay -r pistorm-gpclk0-pll5ff 2>/dev/null || true
sudo dtoverlay -r pistorm-gpclk0-pll5 2>/dev/null || true
sudo dtoverlay -r pistorm-gpclk0 2>/dev/null || true

# Also try to remove by ID in case they were loaded with numeric prefixes
ids="$(sudo dtoverlay -l 2>/dev/null | sed -n 's/^\([0-9]\+\):.*pistorm-gpclk0.*$/\1/p' || true)"
if [[ -n "${ids:-}" ]]; then
  while read -r id; do
    [[ -n "$id" ]] || continue
    echo "[remove-pistorm-gpclk] Removing overlay ID: $id"
    sudo dtoverlay -r "$id" 2>/dev/null || true
  done <<<"$ids"
fi

# Remove the kernel module
sudo rmmod pistorm_gpclk0 2>/dev/null || true

echo "[remove-pistorm-gpclk] Cleanup complete."

# Verify removal
echo ""
echo "[remove-pistorm-gpclk] Current overlay list:"
sudo dtoverlay -l 2>/dev/null || echo "(no overlays loaded)"

echo ""
echo "[remove-pistorm-gpclk] Kernel module status:"
if lsmod | grep pistorm_gpclk0 >/dev/null 2>&1; then
  echo "pistorm_gpclk0 module still loaded"
else
  echo "pistorm_gpclk0 module successfully removed"
fi

# Check GPIO4 status
echo ""
echo "[remove-pistorm-gpclk] GPIO4 status:"
sudo pinctrl get 4 2>/dev/null || echo "pinctrl not available"