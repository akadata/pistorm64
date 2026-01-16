#!/usr/bin/env bash
# Create a small tmpfs-based recovery shell at /mnt/recovery (or custom path).
# Copies busybox (for core commands) and e2fsck plus its shared libraries so
# you can repair/mount storage even if the root filesystem is unhealthy.

set -euo pipefail

RECOV_MNT=${1:-/mnt/recovery}
TMPFS_SIZE=${TMPFS_SIZE:-64m}

need_root() {
  if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root." >&2
    exit 1
  fi
}

copy_with_deps() {
  local bin="$1" target_bin_dir="$2"
  [[ -x "$bin" ]] || { echo "Missing executable: $bin" >&2; return 1; }
  install -D -m 755 "$bin" "$target_bin_dir/$(basename "$bin")"
  # Copy shared library dependencies
  while IFS= read -r dep; do
    # Lines look like:   libext2fs.so.2 => /lib/... (0x...)
    dep_path=$(echo "$dep" | awk '{print $3}')
    [[ -f "$dep_path" ]] || continue
    rel_dir=$(dirname "${dep_path#/}") # strip leading /
    install -D -m 644 "$dep_path" "$RECOV_MNT/$rel_dir/$(basename "$dep_path")"
  done < <(ldd "$bin" | grep "=> /")
}

need_root

echo "[recov] creating $RECOV_MNT (tmpfs size=$TMPFS_SIZE)"
mkdir -p "$RECOV_MNT"
if ! mountpoint -q "$RECOV_MNT"; then
  mount -t tmpfs -o size="$TMPFS_SIZE" tmpfs "$RECOV_MNT"
fi

mkdir -p "$RECOV_MNT"/{bin,sbin,lib,lib64,usr/bin,usr/sbin}

# Busybox for core utilities and shell
if command -v busybox >/dev/null 2>&1; then
  install -m 755 "$(command -v busybox)" "$RECOV_MNT/bin/busybox"
  for cmd in sh ls mount umount dmesg reboot cat echo; do
    ln -sf busybox "$RECOV_MNT/bin/$cmd"
  done
else
  echo "[recov] busybox not found; core tools will be missing" >&2
fi

# e2fsck for filesystem repair (plus deps)
if [[ -x /sbin/e2fsck ]]; then
  copy_with_deps /sbin/e2fsck "$RECOV_MNT/sbin"
else
  echo "[recov] /sbin/e2fsck not found; ext fs repair unavailable" >&2
fi

echo "[recov] recovery shell ready at $RECOV_MNT. To enter:"
echo "  sudo chroot $RECOV_MNT /bin/sh"
