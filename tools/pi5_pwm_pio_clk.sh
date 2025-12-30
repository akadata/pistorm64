#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  echo "Run as root (sudo)."
  exit 1
fi

GPIO_PIN="${PISTORM_CLK_GPIO:-4}"
PERIOD_NS="${PISTORM_PWM_PIO_PERIOD_NS:-40}"
DUTY_NS="${PISTORM_PWM_PIO_DUTY_NS:-20}"

if ! command -v dtoverlay >/dev/null 2>&1; then
  echo "dtoverlay not found (install raspberrypi-utils)."
  exit 1
fi

if [[ ! -d /sys/class/pwm ]]; then
  echo "/sys/class/pwm not present; kernel PWM sysfs not available."
  exit 1
fi

if ! dtoverlay -l 2>/dev/null | grep -q "pwm-pio"; then
  dtoverlay "pwm-pio,gpio=${GPIO_PIN}"
fi

PWMCHIP=""
for d in /sys/class/pwm/pwmchip*; do
  [[ -e "$d" ]] || continue
  if readlink -f "$d" | grep -q "pwm_pio@${GPIO_PIN}"; then
    PWMCHIP="$d"
    break
  fi
done

if [[ -z "${PWMCHIP}" ]]; then
  echo "Failed to locate pwm-pio pwmchip for gpio=${GPIO_PIN}."
  echo "Check: ls -l /sys/class/pwm and dtoverlay -l"
  exit 1
fi

cd "$PWMCHIP"

if [[ ! -d pwm0 ]]; then
  echo 0 > export
fi

echo 0 > pwm0/enable 2>/dev/null || true
echo "${PERIOD_NS}" > pwm0/period
echo "${DUTY_NS}" > pwm0/duty_cycle
echo 1 > pwm0/enable

echo "pwm-pio clock enabled:"
echo "  gpio=${GPIO_PIN}"
echo "  pwmchip=${PWMCHIP}"
echo "  period_ns=$(cat pwm0/period)"
echo "  duty_ns=$(cat pwm0/duty_cycle)"
echo "  enable=$(cat pwm0/enable)"
echo
command -v pinctrl >/dev/null 2>&1 && pinctrl get "${GPIO_PIN}" || true
