#!/usr/bin/env bash
set -euo pipefail

# Script to install PiStorm GPCLK0 overlay and kernel module for Pi5
# Usage: ./tools/install_pistorm_gpclk.sh [freq] [options]
# Default frequency is 200MHz, but can be overridden

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GPCLK_DIR="$REPO_ROOT/pi5/gpclk0"

usage() {
  cat <<'EOF'
Usage:
  ./tools/install_pistorm_gpclk.sh [freq] [--force-high] [--100mhz] [--50mhz] [--test-mode]

Examples:
  ./tools/install_pistorm_gpclk.sh              # 200MHz (default)
  ./tools/install_pistorm_gpclk.sh 100000000    # 100MHz
  ./tools/install_pistorm_gpclk.sh --100mhz     # 100MHz (shorthand)
  ./tools/install_pistorm_gpclk.sh --50mhz      # 50MHz (for testing)

Notes:
  - Builds overlay and kernel module if needed
  - Uses the pll5ff overlay which attempts to set up pll_sys/5 for ~200MHz
  - For frequencies > 50MHz, --force-high may be required on some systems
EOF
}

freq="200000000"
force_high=0
test_mode=0

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --100mhz)
      freq="100000000"
      shift
      ;;
    --50mhz)
      freq="50000000" 
      shift
      ;;
    --test-mode)
      test_mode=1
      shift
      ;;
    --force-high)
      force_high=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    [0-9]*)
      freq="$1"
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ! "$freq" =~ ^[0-9]+$ ]]; then
  echo "Invalid frequency: $freq" >&2
  exit 2
fi

if [[ "$force_high" -eq 0 && "$freq" -gt 50000000 ]]; then
  cat <<EOF >&2
Warning: Frequency $freq Hz > 50MHz may cause instability on some Pi5 systems.
Use --force-high to proceed anyway.
EOF
  exit 3
fi

echo "[install-pistorm-gpclk] Repository root: $REPO_ROOT"
echo "[install-pistorm-gpclk] GPCLK directory: $GPCLK_DIR"
echo "[install-pistorm-gpclk] Target frequency: $freq Hz"

# Build overlay if needed
echo "[install-pistorm-gpclk] Checking overlay..."
if [[ ! -f "$GPCLK_DIR/pistorm-gpclk0-pll5ff.dtbo" ]]; then
  echo "[install-pistorm-gpclk] Building overlay..."
  (cd "$GPCLK_DIR" && dtc -@ -I dts -O dtb -o pistorm-gpclk0-pll5ff.dtbo pistorm-gpclk0-pll5ff-overlay.dts)
fi

# Build kernel module if needed
echo "[install-pistorm-gpclk] Checking kernel module..."
if [[ ! -f "$GPCLK_DIR/pistorm_gpclk0.ko" ]]; then
  echo "[install-pistorm-gpclk] Building kernel module..."
  make -C "$GPCLK_DIR"
fi

# Remove any existing instances
echo "[install-pistorm-gpclk] Removing previous instances..."
sudo dtoverlay -r pistorm-gpclk0-pll5ff 2>/dev/null || true
sudo dtoverlay -r pistorm-gpclk0 2>/dev/null || true
sudo rmmod pistorm_gpclk0 2>/dev/null || true

# Load the new overlay and module
echo "[install-pistorm-gpclk] Loading overlay with freq=$freq..."
sudo dtoverlay -d "$GPCLK_DIR" pistorm-gpclk0-pll5ff "freq=$freq"

echo "[install-pistorm-gpclk] Loading kernel module..."
sudo insmod "$GPCLK_DIR/pistorm_gpclk0.ko"

# Verify the setup
echo ""
echo "[install-pistorm-gpclk] GPIO4 mux status:"
gpio4_mux="$(sudo pinctrl get 4 2>/dev/null || echo "pinctrl not available")"
echo "$gpio4_mux"

echo ""
echo "[install-pistorm-gpclk] Clock debug info (if available):"
sudo mount -t debugfs none /sys/kernel/debug 2>/dev/null || true
if [[ -d "/sys/kernel/debug/clk/clk_gp0" ]]; then
  echo -n "Parent: "
  cat /sys/kernel/debug/clk/clk_gp0/clk_parent 2>/dev/null || echo "N/A"
  echo -n "Rate: "
  cat /sys/kernel/debug/clk/clk_gp0/clk_rate 2>/dev/null || echo "N/A"
  echo -n "Enable count: "
  cat /sys/kernel/debug/clk/clk_gp0/clk_enable_count 2>/dev/null || echo "N/A"
else
  echo "clk_gp0 debugfs not available"
fi

# Test the clock if not in test mode
if [[ "$test_mode" -eq 0 ]]; then
  echo ""
  echo "[install-pistorm-gpclk] Testing clock output (no CPLD transactions)..."
  sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 "$REPO_ROOT/emulator" --gpclk-probe | head -n 3 || true
fi

echo ""
echo "[install-pistorm-gpclk] Installation complete!"
echo ""
echo "To run PiStorm with this clock setup:"
echo "  sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 PISTORM_TXN_TIMEOUT_US=200000 ./emulator --config basic.cfg"
echo ""
echo "To remove this setup:"
echo "  ./tools/remove_pistorm_gpclk.sh"