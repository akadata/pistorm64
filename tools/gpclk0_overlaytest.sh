#!/usr/bin/env bash
set -e

OVERLAY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../pi5/gpclk0" && pwd)"
DTBO="${OVERLAY_DIR}/pistorm-gpclk0-pll5ff.dtbo"
DTS="${OVERLAY_DIR}/pistorm-gpclk0-pll5ff-overlay.dts"

echo "[gpclk0] Building overlay..."
dtc -@ -I dts -O dtb -o "${DTBO}" "${DTS}"

echo "[gpclk0] Unloading old overlays..."
sudo dtoverlay -r pistorm-gpclk0 2>/dev/null || true
sudo dtoverlay -r pistorm-gpclk0-pll5 2>/dev/null || true
sudo dtoverlay -r pistorm-gpclk0-pll5ff 2>/dev/null || true

echo "[gpclk0] Loading pll5ff overlay..."
sudo dtoverlay -d "${OVERLAY_DIR}" pistorm-gpclk0-pll5ff

echo "[gpclk0] GPIO4 mux:"
sudo pinctrl get 4

echo "[gpclk0] Clock debugfs:"
sudo sh -c 'mount -t debugfs none /sys/kernel/debug 2>/dev/null || true; \
  cat /sys/kernel/debug/clk/clk_gp0/clk_parent \
      /sys/kernel/debug/clk/clk_gp0/clk_rate \
      /sys/kernel/debug/clk/clk_gp0/clk_enable_count'

echo "[gpclk0] Optional bus-probe:"
sudo env PISTORM_PROTOCOL=old PISTORM_OLD_NO_HANDSHAKE=1 \
  PISTORM_RP1_CLK_FUNCSEL=0 PISTORM_ENABLE_GPCLK=0 \
  PISTORM_TXN_TIMEOUT_US=200000 \
  "$(dirname "${BASH_SOURCE[0]}")/../emulator" --bus-probe | head -n 8
