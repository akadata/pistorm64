#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

REMOTE_HOST="${REMOTE_HOST:-homer}"
REMOTE_TMP="${REMOTE_TMP:-/tmp/pistorm64_ppc_elf}"
REMOTE_TC="${REMOTE_TC:-/opt/amiga/ppc-amigaos/bin}"

OUT_DIR="${OUT_DIR:-${BASE_DIR}/C}"
OUT_NAME="${OUT_NAME:-ppc_hello_marker.elf}"
COPY_TO_PI0="${COPY_TO_PI0:-1}"

SRC_S="${SCRIPT_DIR}/hello_marker.S"
SRC_LD="${SCRIPT_DIR}/hello_marker.ld"
OUT_PATH="${OUT_DIR}/${OUT_NAME}"

mkdir -p "${OUT_DIR}"

echo "[ppc-elf] remote host: ${REMOTE_HOST}"
ssh "${REMOTE_HOST}" "mkdir -p '${REMOTE_TMP}'"
scp "${SRC_S}" "${SRC_LD}" "${REMOTE_HOST}:${REMOTE_TMP}/"

ssh "${REMOTE_HOST}" "\
  '${REMOTE_TC}/ppc-amigaos-gcc' -c -x assembler-with-cpp \
    -o '${REMOTE_TMP}/hello_marker.o' '${REMOTE_TMP}/hello_marker.S' && \
  '${REMOTE_TC}/ppc-amigaos-ld' -nostdlib \
    -T '${REMOTE_TMP}/hello_marker.ld' \
    -o '${REMOTE_TMP}/${OUT_NAME}' \
    '${REMOTE_TMP}/hello_marker.o' && \
  '${REMOTE_TC}/ppc-amigaos-readelf' -h -l '${REMOTE_TMP}/${OUT_NAME}' | sed -n '1,80p'"

scp "${REMOTE_HOST}:${REMOTE_TMP}/${OUT_NAME}" "${OUT_PATH}"
echo "[ppc-elf] wrote ${OUT_PATH}"

if [[ "${COPY_TO_PI0}" == "1" ]]; then
  cp -f "${OUT_PATH}" "/opt/pistorm64/data/a314-shared/${OUT_NAME}"
  echo "[ppc-elf] copied to /opt/pistorm64/data/a314-shared/${OUT_NAME}"
fi
