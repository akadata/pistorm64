#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "This script must run as root. Re-run with: sudo $0"
  exit 1
fi

echo "[a314-deps] Updating package index..."
apt-get update

echo "[a314-deps] Installing apt Python dependencies..."
apt-get install -y \
  python3 \
  python3-pip \
  python3-pyudev \
  python3-websockets

# python-pytun may or may not exist as a distro package.
if apt-cache show python3-pytun >/dev/null 2>&1; then
  echo "[a314-deps] Installing python3-pytun from apt..."
  apt-get install -y python3-pytun
fi

if ! python3 -c "import pytun" >/dev/null 2>&1; then
  echo "[a314-deps] python-pytun not present via apt; installing via pip..."
  python3 -m pip install --break-system-packages --upgrade python-pytun || \
    python3 -m pip install --upgrade python-pytun
fi

echo "[a314-deps] Verifying imports..."
python3 - <<'PY'
import importlib
mods = ("pyudev", "websockets", "pytun")
missing = [m for m in mods if importlib.util.find_spec(m) is None]
if missing:
    raise SystemExit(f"Missing Python modules after install: {', '.join(missing)}")
print("A314 Python dependencies OK:", ", ".join(mods))
PY

echo "[a314-deps] Done."
