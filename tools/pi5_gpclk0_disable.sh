#!/usr/bin/env bash
set -euo pipefail

echo "[pi5] Removing pistorm-gpclk0 overlay + module..."

# Remove any loaded instances (by id) to handle cases like '1_pistorm-gpclk0'.
ids="$(sudo dtoverlay -l 2>/dev/null | sed -n 's/^\([0-9]\+\):.*pistorm-gpclk0.*$/\1/p' || true)"
if [[ -n "${ids:-}" ]]; then
  while read -r id; do
    [[ -n "$id" ]] || continue
    sudo dtoverlay -r "$id" 2>/dev/null || true
  done <<<"$ids"
else
  sudo dtoverlay -r pistorm-gpclk0 2>/dev/null || true
fi

sudo rmmod pistorm_gpclk0 2>/dev/null || true
echo "[pi5] Done."
