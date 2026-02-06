#!/usr/bin/env bash
# Build helper for PiStorm64 FC/BERR CPLD (EPM240).
# Usage: ./build_pistorm_fc.sh <project_name_without_ext>
# Env:
#   QUARTUS_BIN  - Quartus bin dir to use (preferred when set)
#   QUARTUS_ROOT - Quartus install root (optional; used to derive bin dir)
#   FREQ         - JTAG clock for cpf (-q), default 100KHz
#
# Notes:
# - This script always *executes* the tools from the resolved QUARTUS bin dir.
# - PATH is used only as a last-resort discovery mechanism.

set -euo pipefail

PROJ="${1:?usage: $0 <project_name_without_ext>}"
FREQ="${FREQ:-100KHz}"

# Prefer QUARTUS_BIN when provided; otherwise fall back to common installs.
: "${QUARTUS_BIN:=}"

resolve_quartus_bin() {
  local cand

  # 1) Explicit QUARTUS_BIN
  if [[ -n "${QUARTUS_BIN}" && -x "${QUARTUS_BIN}/quartus_sh" ]]; then
    printf '%s\n' "${QUARTUS_BIN}"
    return 0
  fi

  # 2) QUARTUS_ROOT-derived paths
  if [[ -n "${QUARTUS_ROOT:-}" ]]; then
    for cand in "${QUARTUS_ROOT}/quartus/bin" "${QUARTUS_ROOT}/quartus/bin64"; do
      if [[ -x "${cand}/quartus_sh" ]]; then
        printf '%s\n' "${cand}"
        return 0
      fi
    done
  fi

  # 3) Known fixed paths (add new versions here)
  for cand in \
    "/opt/intelFPGA/25.1/quartus/bin" \
    "/opt/intelFPGA/25.1/quartus/bin64" \
    "/opt/intelFPGA/20.1/quartus/bin" \
    "/opt/intelFPGA/20.1/quartus/bin64" \
    "/opt/intelFPGA_lite/20.1/quartus/bin" \
    "/opt/intelFPGA_lite/20.1/quartus/bin64"; do
    if [[ -x "${cand}/quartus_sh" ]]; then
      printf '%s\n' "${cand}"
      return 0
    fi
  done

  # 4) Best-effort glob search under /opt/intelFPGA (handles new installs)
  #    Chooses the first match in lexicographic order.
  shopt -s nullglob
  local matches=(/opt/intelFPGA/*/quartus/bin /opt/intelFPGA/*/quartus/bin64)
  shopt -u nullglob
  for cand in "${matches[@]}"; do
    if [[ -x "${cand}/quartus_sh" ]]; then
      printf '%s\n' "${cand}"
      return 0
    fi
  done

  # 5) PATH last resort
  if command -v quartus_sh >/dev/null 2>&1; then
    # Derive bin dir from the resolved executable path
    cand="$(command -v quartus_sh)"
    printf '%s\n' "$(cd "$(dirname "${cand}")" && pwd)"
    return 0
  fi

  return 1
}

QBIN="$(resolve_quartus_bin)" || {
  echo "Missing quartus_sh. Install Quartus Prime (Lite is fine), or set QUARTUS_BIN/QUARTUS_ROOT." >&2
  exit 1
}

QSH="${QBIN}/quartus_sh"
QCPF="${QBIN}/quartus_cpf"

if [[ ! -x "${QSH}" ]]; then
  echo "Resolved QUARTUS_BIN does not contain quartus_sh: ${QSH}" >&2
  exit 1
fi
if [[ ! -x "${QCPF}" ]]; then
  echo "Resolved QUARTUS_BIN does not contain quartus_cpf: ${QCPF}" >&2
  exit 1
fi

if [[ ! -f "${PROJ}.qpf" ]]; then
  echo "Missing project file: ${PROJ}.qpf" >&2
  exit 1
fi

echo "Using Quartus: ${QSH}"
"${QSH}" --version || true

echo "[1/3] Compile: ${PROJ}"
"${QSH}" --flow compile "${PROJ}"

POF="output_files/${PROJ}.pof"
SVF="${PROJ}.svf"

if [[ ! -f "${POF}" ]]; then
  echo "Missing POF: ${POF}" >&2
  exit 1
fi

echo "[2/3] Convert POF -> SVF: ${SVF}"
"${QCPF}" -c -q "${FREQ}" -g 3.3 -n p "${POF}" "${SVF}"

echo "[3/3] Done: ${SVF}"
ls -lh "${SVF}"

