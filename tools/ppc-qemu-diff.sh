#!/usr/bin/env bash
set -euo pipefail

REFERENCE_ROOT="${REFERENCE_ROOT:-/home/smalley/reference}"
PISTORM_ROOT="${PISTORM_ROOT:-/home/smalley/pistorm64}"

QEMU_UAE_DIR="${QEMU_UAE_DIR:-${REFERENCE_ROOT}/qemu-uae}"
QEMU_UPSTREAM_DIR="${QEMU_UPSTREAM_DIR:-${REFERENCE_ROOT}/qemu-upstream}"
FS_UAE_DIR="${FS_UAE_DIR:-${REFERENCE_ROOT}/fs-uae}"

SUMMARY_ONLY=0
SHOW_API_DIFF=0

usage() {
  cat <<'EOF'
Usage: ppc-qemu-diff.sh [options]

Cross-check PPC-related sources in:
  - reference/qemu-uae
  - reference/qemu-upstream
  - reference/fs-uae
  - local pistorm64 tree

Options:
  --summary-only     Show repo status only
  --api-diff         Show API/header diff (qemu-uae ppc.h vs local mirror)
  -h, --help         Show this help

Environment overrides:
  REFERENCE_ROOT, PISTORM_ROOT, QEMU_UAE_DIR, QEMU_UPSTREAM_DIR, FS_UAE_DIR
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --summary-only) SUMMARY_ONLY=1 ;;
    --api-diff) SHOW_API_DIFF=1 ;;
    -h|--help) usage; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
  shift
done

have_dir() {
  [[ -d "$1" ]]
}

repo_info() {
  local name="$1"
  local dir="$2"
  if ! have_dir "$dir"; then
    printf "[%s] missing: %s\n" "$name" "$dir"
    return
  fi
  if [[ ! -d "$dir/.git" ]]; then
    printf "[%s] not a git checkout: %s\n" "$name" "$dir"
    return
  fi
  local branch head
  branch="$(git -C "$dir" branch --show-current 2>/dev/null || true)"
  head="$(git -C "$dir" log --date=short --pretty=format:'%h %ad %an %s' -n 1 2>/dev/null || true)"
  printf "[%s] %s\n" "$name" "$dir"
  printf "  branch: %s\n" "${branch:-<detached>}"
  printf "  head:   %s\n" "${head:-<unknown>}"
}

show_hits() {
  local title="$1"
  local dir="$2"
  local pattern="$3"
  local globs=("${@:4}")
  if ! have_dir "$dir"; then
    return
  fi
  echo
  echo "## ${title}"
  # shellcheck disable=SC2086
  rg -n $pattern "$dir" "${globs[@]}" 2>/dev/null | head -n 80 || true
}

echo "# PPC/QEMU source status"
repo_info "qemu-uae" "$QEMU_UAE_DIR"
repo_info "qemu-upstream" "$QEMU_UPSTREAM_DIR"
repo_info "fs-uae" "$FS_UAE_DIR"
repo_info "pistorm64" "$PISTORM_ROOT"

if [[ "$SUMMARY_ONLY" -eq 1 ]]; then
  exit 0
fi

show_hits \
  "qemu-uae exported PPC API surfaces" \
  "$QEMU_UAE_DIR/uae" \
  "qemu_uae_ppc_init|ppc_cpu_init|ppc_cpu_map_memory|ppc_cpu_set_state|uae_ppc_io_mem_(read|write)(64)?|PPCMemoryRegion" \
  --glob '*.c' --glob '*.h'

show_hits \
  "local pistorm64 qemu-uae integration points" \
  "$PISTORM_ROOT/src" \
  "qemu_uae_loader|qemu_uae_ppc_init|ppc_cpu_map_memory|ppc_cpu_set_state|PPCMemoryRegion|PPC_ACCEL_(BOOT|PPC_RAM)" \
  --glob '*.c' --glob '*.cc' --glob '*.h'

show_hits \
  "upstream qemu PPC core touchpoints" \
  "$QEMU_UPSTREAM_DIR/target/ppc" \
  "cpu_init|ppc_set_irq|ppc_cpu|mmu|interrupt|tb|translate" \
  --glob '*.c' --glob '*.h'

if have_dir "$FS_UAE_DIR/ppc"; then
  echo
  echo "## fs-uae PPC tree summary"
  printf "files under fs-uae/ppc: "
  find "$FS_UAE_DIR/ppc" -type f | wc -l
  rg -n "pearpc|PPC|ppc_cpu|interrupt|mmu|jit" "$FS_UAE_DIR/ppc" \
    --glob '*.c' --glob '*.cc' --glob '*.cpp' --glob '*.h' \
    2>/dev/null | head -n 80 || true
fi

if [[ "$SHOW_API_DIFF" -eq 1 ]]; then
  echo
  echo "## API header diff (qemu-uae vs local mirror)"
  local_ref="$QEMU_UAE_DIR/uae/ppc.h"
  local_mirror="$PISTORM_ROOT/src/uae/include/uae/ppc.h"
  if [[ -f "$local_ref" && -f "$local_mirror" ]]; then
    diff -u "$local_ref" "$local_mirror" || true
  else
    echo "missing file(s):"
    [[ -f "$local_ref" ]] || echo "  $local_ref"
    [[ -f "$local_mirror" ]] || echo "  $local_mirror"
  fi
fi
