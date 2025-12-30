#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  echo "Run as root (sudo)."
  exit 1
fi

GPIO_PIN="${PISTORM_CLK_GPIO:-4}"

PWMCHIP=""
for d in /sys/class/pwm/pwmchip*; do
  [[ -e "$d" ]] || continue
  if readlink -f "$d" | grep -q "pwm_pio@${GPIO_PIN}"; then
    PWMCHIP="$d"
    break
  fi
done

if [[ -z "${PWMCHIP}" ]]; then
  echo "No pwm-pio pwmchip found for gpio=${GPIO_PIN}; nothing to stop."
else
  cd "$PWMCHIP"
  if [[ -d pwm0 ]]; then
    echo 0 > pwm0/enable 2>/dev/null || true
    echo 0 > unexport 2>/dev/null || true
  fi
fi

if command -v dtoverlay >/dev/null 2>&1; then
  if dtoverlay -l 2>/dev/null | grep -q "pwm-pio"; then
    dtoverlay -r pwm-pio || true
  fi
fi

command -v pinctrl >/dev/null 2>&1 && pinctrl get "${GPIO_PIN}" || true
