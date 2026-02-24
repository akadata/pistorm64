#!/usr/bin/env bash
# pistorm-gpclk-sweep.sh
#
# Sweep gpclk_src/gpclk_div by repeatedly unloading/loading the pistorm kmod,
# then read back what the driver reports (BUSY/EN/DIVI) from dmesg.
#
# Usage examples:
#   sudo ./pistorm-gpclk-sweep.sh --src 6 --div-min 3 --div-max 10
#   sudo ./pistorm-gpclk-sweep.sh --src 5 --div-min 3 --div-max 12 --batch 1 --berr 1
#
# Notes:
# - This measures only whether GPCLK comes up cleanly (en=1,busy=1) and what DIV is latched.
# - It does not validate bus stability under load. Once a "best" setting is found, run the emulator.

set -euo pipefail

SRC=6
DIV_MIN=3
DIV_MAX=12
BATCH=1
BERR=1
SLEEP_AFTER_LOAD=0.15
DMESG_TAIL=60

while [[ $# -gt 0 ]]; do
    case "$1" in
        --src) SRC="$2"; shift 2;;
        --div-min) DIV_MIN="$2"; shift 2;;
        --div-max) DIV_MAX="$2"; shift 2;;
        --batch) BATCH="$2"; shift 2;;
        --berr) BERR="$2"; shift 2;;
        --sleep) SLEEP_AFTER_LOAD="$2"; shift 2;;
        --tail) DMESG_TAIL="$2"; shift 2;;
        *)
            echo "Unknown arg: $1" >&2
            exit 2
            ;;
    esac
done

require_root() {
    if [[ "$(id -u)" -ne 0 ]]; then
        echo "Run as root (sudo)." >&2
        exit 1
    fi
}

mod_unload() {
    # rmmod returns non-zero when module not loaded; ignore.
    rmmod pistorm 2>/dev/null || true
}

mod_load() {
    local div="$1"
    modprobe pistorm \
        "run_batch_enable=${BATCH}" \
        "berr_reset_input=${BERR}" \
        "gpclk_src=${SRC}" \
        "gpclk_div=${div}"
}

last_report() {
    # Extract the most recent gpclk0 line.
    # Prefer the detailed ctl/div decode; fall back to configured line.
    dmesg | tail -n "${DMESG_TAIL}" | tac | awk '
        /pistorm: gpclk0 ctl=/ { print; exit }
        /pistorm: gpclk0 configured/ { print; exit }
    '
}

decode_ok() {
    # Returns 0 when en=1 and busy=1 are present in the line, else 1.
    local line="$1"
    echo "$line" | grep -q "en=1" && echo "$line" | grep -q "busy=1"
}

require_root

echo "Sweeping pistorm GPCLK: src=${SRC}, div=[${DIV_MIN}..${DIV_MAX}], batch=${BATCH}, berr=${BERR}"

printf "%-6s | %-4s | %s\n" "DIV" "OK" "dmesg"
printf "%s\n" "----------------------------------------------------------------------"

for ((div = DIV_MIN; div <= DIV_MAX; div++)); do
    mod_unload
dmesg -c >/dev/null || true

if ! mod_load "$div" 2>/dev/null; then
    printf "%-6s | %-4s | %s\n" "$div" "NO" "modprobe failed"
    continue
fi

sleep "$SLEEP_AFTER_LOAD"
timeout 10 ./emulator --config min.cfg
new="$(dmesg -c || true)"
line="$(echo "$new" | awk '/pistorm: gpclk0 ctl=/ { last=$0 } END { print last }')"

if echo "$line" | grep -q "en=1" && echo "$line" | grep -q "busy=1"; then
    ok="YES"
else
    ok="NO"
fi

printf "%-6s | %-4s | %s\n" "$div" "$ok" "$line"
done

echo
echo "Next step for any promising DIV: run the emulator and watch for BERRs/timeouts under load."

