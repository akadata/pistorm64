#!/usr/bin/env bash
set -euo pipefail

# Script to verify GPIO usage via RP1 Southbridge for PiStorm
# This checks if all PiStorm protocol pins are properly configured through RP1

echo "[rp1-gpio-check] Checking RP1 GPIO configuration for PiStorm..."

# Check if we can access the RP1 registers
if [[ ! -d "/sys/firmware/devicetree/base/soc/rp1" ]]; then
  echo "[rp1-gpio-check] WARNING: RP1 device tree node not found"
  echo "[rp1-gpio-check] This might indicate the Pi5 is not detected properly or device tree is incomplete"
else
  echo "[rp1-gpio-check] RP1 device tree node found"
fi

# Check if we can access the memory ranges
if [[ -f "/proc/iomem" ]]; then
  echo "[rp1-gpio-check] RP1 GPIO memory ranges:"
  grep -i gpio /proc/iomem | grep -i "rp1\|gpio" || echo "[rp1-gpio-check] No RP1 GPIO ranges found in iomem"
else
  echo "[rp1-gpio-check] /proc/iomem not accessible"
fi

# Check current GPIO pin states
echo ""
echo "[rp1-gpio-check] Current state of PiStorm protocol pins:"
echo "Pin | Name      | State"
echo "----|-----------|-------"

for pin in {0..23}; do
  case $pin in
    0) name="TXN_IN_PROGRESS" ;;
    1) name="IPL_ZERO" ;;
    2) name="A0" ;;
    3) name="A1" ;;
    4) name="CLK (GPCLK0)" ;;
    5) name="RESET" ;;
    6) name="RD" ;;
    7) name="WR" ;;
    *) name="D$((pin-8))" ;;
  esac
  
  if command -v pinctrl >/dev/null 2>&1; then
    state=$(sudo pinctrl get $pin 2>/dev/null || echo "N/A")
    printf "%3d | %-11s | %s\n" $pin "$name" "$state"
  else
    printf "%3d | %-11s | %s\n" $pin "$name" "pinctrl not available"
  fi
done

echo ""
echo "[rp1-gpio-check] Checking for PiStorm-specific overlays:"
sudo dtoverlay -l 2>/dev/null | grep -i pistorm || echo "No PiStorm overlays currently loaded"

echo ""
echo "[rp1-gpio-check] Checking kernel modules:"
if lsmod | grep pistorm_gpclk0 >/dev/null 2>&1; then
  echo "pistorm_gpclk0 module: LOADED"
else
  echo "pistorm_gpclk0 module: NOT LOADED"
fi

echo ""
echo "[rp1-gpio-check] To test PiStorm with proper RP1 GPIO usage:"
echo "  1. Load the GPCLK overlay: ./tools/install_pistorm_gpclk.sh --100mhz"
echo "  2. Run emulator with: sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 ./emulator --config basic.cfg"
echo ""
echo "[rp1-gpio-check] Key environment variables for Pi5/RP1:"
echo "  PISTORM_RP1=1                    (already set in build)"
echo "  PISTORM_ENABLE_GPCLK=0           (disable direct clock setup)"
echo "  PISTORM_RP1_LEAVE_CLK_PIN=1      (don't touch GPIO4 pinmux)"
echo "  PISTORM_RP1_CLK_FUNCSEL=0        (explicitly set GPIO4 to GPCLK0)"
echo "  PISTORM_TXN_TIMEOUT_US=200000    (increase timeout)"