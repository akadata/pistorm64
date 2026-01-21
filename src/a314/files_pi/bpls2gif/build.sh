#!/usr/bin/env bash
set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if ! python3 setup.py build_ext --inplace; then
  echo "Build failed. On Debian/Trixie try:"
  echo "  sudo apt install python3-distlib python3-distutils-extra python3-dev build-essential"
  exit 1
fi


echo "Kernel PiStorm64 bpls2gif (Amiga bitplanes → GIF)"
echo "(c) Niklas Ekström — https://github.com/niklasekstrom/a314"
echo "(c) 2026 AKADATA — credit to original author"

PYTHONPATH="$here" python3 - <<'PY'
import bpls2gif
ok_palette = hasattr(bpls2gif, "set_palette")
ok_encode  = hasattr(bpls2gif, "encode")
print(bpls2gif.__file__)
print("has set_palette:", ok_palette)
print("has encode     :", ok_encode)
raise SystemExit(0 if (ok_palette and ok_encode) else 1)
PY

echo "OK: extension built and symbols present."

