#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
  cat <<'EOF'
Usage:
  ./tools/pi5_gdb.sh [--config <cfg>] [--] [extra emulator args...]

Examples:
  ./tools/pi5_gdb.sh
  ./tools/pi5_gdb.sh --config basic.cfg
  ./tools/pi5_gdb.sh --config basic.cfg -- --serial

Notes:
  - Runs `sudo gdb` with `gdb_pi5_run.gdb` and logs to `gdb.log`.
  - Do NOT append env vars after `--args`; set them via the gdb script or `sudo env ... gdb ...`.
  - In gdb, run helper commands directly (e.g. `pistorm_watch_vectors`), then `run`.
EOF
}

cfg="basic.cfg"
extra_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --config)
      cfg="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      extra_args+=("$@")
      break
      ;;
    *)
      extra_args+=("$1")
      shift
      ;;
  esac
done

if [[ -z "${cfg:-}" ]]; then
  echo "Missing --config value" >&2
  exit 2
fi

if [[ ! -x "$repo_root/emulator" ]]; then
  echo "Missing executable: $repo_root/emulator" >&2
  exit 1
fi

if ! file "$repo_root/emulator" | rg -q "with debug_info"; then
  cat <<'EOF' >&2
WARNING: ./emulator does not appear to contain debug info.
Rebuild with debug symbols to get better backtraces:
  make clean
  make DEBUG=1 PLATFORM=PI5_DEBIAN_64BIT   # Raspberry Pi OS (Debian) on Pi 5
  # or:
  make DEBUG=1 PLATFORM=PI5_ALPINE_64BIT   # Alpine on Pi 5 (also works on Debian)
EOF
fi

exec sudo gdb -q -x "$repo_root/gdb_pi5_run.gdb" --args "$repo_root/emulator" --config "$cfg" "${extra_args[@]}"
