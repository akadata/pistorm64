#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

CC_BIN="${CC:-gcc}"
OUT_ELF="${SCRIPT_DIR}/julia_arm64.elf"
SRC="${SCRIPT_DIR}/julia_arm64.c"
INCLUDE_DIR="${REPO_ROOT}/src/platforms/amiga/zorro/arm64_accel"

echo "[build] CC=${CC_BIN}"
echo "[build] SRC=${SRC}"
echo "[build] OUT=${OUT_ELF}"

"${CC_BIN}" \
  -std=c11 -O2 -ffreestanding -fno-pic -fno-plt \
  -fno-asynchronous-unwind-tables -fno-unwind-tables \
  -nostdlib -nodefaultlibs -nostartfiles -no-pie \
  -Wl,-e,arm_job_entry -Wl,--build-id=none -Wl,-Ttext=0x400000 \
  -Wl,-z,max-page-size=0x1000 -Wl,-z,common-page-size=0x1000 \
  -Wl,-s \
  -I"${INCLUDE_DIR}" \
  -o "${OUT_ELF}" "${SRC}"

echo "[build] done"
file "${OUT_ELF}"
readelf -h "${OUT_ELF}" | sed -n '1,24p'
