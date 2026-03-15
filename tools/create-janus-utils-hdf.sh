#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

SIZE_MB="${1:-32}"
OUT_PATH="${2:-${REPO_ROOT}/src/platforms/amiga/janus-utils.hdf}"
FORCE="${FORCE:-0}"

usage() {
  cat <<'EOF'
Usage:
  create-janus-utils-hdf.sh [size_mb] [output_path]

Defaults:
  size_mb     = 32
  output_path = ./src/platforms/amiga/janus-utils.hdf

Examples:
  tools/create-janus-utils-hdf.sh
  tools/create-janus-utils-hdf.sh 16
  FORCE=1 tools/create-janus-utils-hdf.sh 32 ./src/platforms/amiga/janus-utils.hdf
EOF
}

if [[ "${SIZE_MB}" == "-h" || "${SIZE_MB}" == "--help" ]]; then
  usage
  exit 0
fi

if ! [[ "${SIZE_MB}" =~ ^[0-9]+$ ]]; then
  echo "[janus-hdf] size must be an integer number of MiB: '${SIZE_MB}'" >&2
  exit 1
fi

if [[ "${SIZE_MB}" -lt 8 ]]; then
  echo "[janus-hdf] refusing size < 8 MiB (got ${SIZE_MB})" >&2
  exit 1
fi

mkdir -p "$(dirname -- "${OUT_PATH}")"

if [[ -e "${OUT_PATH}" && "${FORCE}" != "1" ]]; then
  echo "[janus-hdf] file exists: ${OUT_PATH}"
  echo "[janus-hdf] set FORCE=1 to overwrite."
  exit 1
fi

if [[ -e "${OUT_PATH}" ]]; then
  rm -f "${OUT_PATH}"
fi

if command -v rdbtool >/dev/null 2>&1 && command -v xdftool >/dev/null 2>&1; then
  # Build an Amiga-friendly RDB/FFS utility image when amitools are available.
  rdbtool "${OUT_PATH}" create "size=${SIZE_MB}Mi" + init rdb_cyls=2
  rdbtool "${OUT_PATH}" add size=100% name=DH98 dostype=ffs
  xdftool "${OUT_PATH}" open part=DH98 + format JanusUtils ffs
  echo "[janus-hdf] created RDB/FFS image: ${OUT_PATH} (${SIZE_MB} MiB, DH98: JanusUtils)"
else
  # Fallback: sparse raw image, initialize from Amiga-side HDToolBox/format workflow.
  truncate -s "${SIZE_MB}M" "${OUT_PATH}"
  echo "[janus-hdf] created sparse raw image: ${OUT_PATH} (${SIZE_MB} MiB)"
  echo "[janus-hdf] note: rdbtool/xdftool not found, initialize partition/filesystem on Amiga."
fi

