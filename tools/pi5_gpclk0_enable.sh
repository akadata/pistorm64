#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gpdir="$repo_root/pi5/gpclk0"

dts="$gpdir/pistorm-gpclk0-overlay.dts"
dtbo="$gpdir/pistorm-gpclk0.dtbo"
ko="$gpdir/pistorm_gpclk0.ko"

usage() {
  cat <<'EOF'
Usage:
  ./tools/pi5_gpclk0_enable.sh [--freq <hz>] [--force-high] [--no-emulator-probe]

Notes:
  - Loads `pistorm-gpclk0` DT overlay + `pistorm_gpclk0.ko` module.
  - By default, refuses freq > 50MHz unless `--force-high` is used (higher rates have caused hard crashes on some Pi 5 setups).
EOF
}

freq_arg=""
force_high=0
emulator_probe=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --freq=*)
      freq_arg="${1#--freq=}"
      shift
      ;;
    --freq)
      freq_arg="${2:-}"
      shift 2
      ;;
    --force-high)
      force_high=1
      shift
      ;;
    --no-emulator-probe|--no-probe)
      emulator_probe=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown arg: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -n "${freq_arg:-}" && ! "$freq_arg" =~ ^[0-9]+$ ]]; then
  echo "Invalid --freq value: $freq_arg" >&2
  exit 2
fi
if [[ -n "${freq_arg:-}" && "$force_high" -eq 0 ]]; then
  # Empirically, switching the parent to achieve high rates (e.g. 200MHz) has caused hard crashes on some Pi 5 kernels.
  # Keep the default path conservative unless explicitly forced.
  if (( freq_arg > 50000000 )); then
    cat <<EOF >&2
Refusing to request GPCLK0 freq=$freq_arg Hz without --force-high.

On Pi 5 we can reliably validate GPCLK0 at low rates (e.g. 1MHz) and at xosc rates (<=50MHz).
Requesting >50MHz typically requires reparenting the clock (e.g. to pll_sys) and has caused hard crashes on this setup.

If you still want to try it:
  $0 --freq $freq_arg --force-high
EOF
    exit 3
  fi
fi

if ! command -v dtc >/dev/null 2>&1; then
  echo "dtc not found; install it first (Debian: sudo apt install device-tree-compiler)." >&2
  exit 1
fi

echo "[pi5] Building overlay if needed..."
if [[ ! -f "$dtbo" || "$dts" -nt "$dtbo" ]]; then
  (cd "$gpdir" && dtc -@ -I dts -O dtb -o "$(basename "$dtbo")" "$(basename "$dts")")
fi

echo "[pi5] Building kernel module if needed..."
if [[ ! -f "$ko" || "$gpdir/pistorm_gpclk0.c" -nt "$ko" ]]; then
  make -C "$gpdir"
fi

echo "[pi5] Unloading any previous instances..."
sudo dtoverlay -r pistorm-gpclk0 2>/dev/null || true
sudo rmmod pistorm_gpclk0 2>/dev/null || true

echo "[pi5] Loading overlay + module..."
if [[ -n "${freq_arg:-}" ]]; then
  sudo dtoverlay -d "$gpdir" pistorm-gpclk0 "freq=$freq_arg"
else
  sudo dtoverlay -d "$gpdir" pistorm-gpclk0
fi
sudo insmod "$ko"

echo "[pi5] GPIO4 mux:"
gpio4_mux="$(sudo pinctrl get 4 2>/dev/null || true)"
echo "${gpio4_mux:-"(pinctrl not available?)"}"
if [[ -n "${gpio4_mux:-}" && "${gpio4_mux}" != *"GPCLK0"* ]]; then
  cat <<'EOF'
[pi5] WARNING: GPIO4 is not currently muxed to GPCLK0.
  - If you ran `gpiomon`/`gpioset` on GPIO4, it can request the line as GPIO and override pinmux.
  - If another overlay/driver claimed GPIO4, remove it and re-run this script.
EOF
fi

echo "[pi5] Clock debug (if available):"
sudo mount -t debugfs none /sys/kernel/debug 2>/dev/null || true
if sudo test -d /sys/kernel/debug/clk/clk_gp0 2>/dev/null; then
  sudo sh -c 'echo -n "parent="; cat /sys/kernel/debug/clk/clk_gp0/clk_parent; echo -n "rate="; cat /sys/kernel/debug/clk/clk_gp0/clk_rate; echo -n "enable_count="; cat /sys/kernel/debug/clk/clk_gp0/clk_enable_count 2>/dev/null || true'
else
  echo "clk_gp0 debugfs not available (or not accessible)."
fi

if [[ "${emulator_probe}" -eq 1 ]]; then
  echo "[pi5] Probe transitions (no CPLD transactions):"
  sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 "$repo_root/emulator" --gpclk-probe | head -n 2 || true
fi

cat <<'EOF'
[pi5] Next:
  sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 PISTORM_TXN_TIMEOUT_US=200000 ./emulator --config basic.cfg
EOF
