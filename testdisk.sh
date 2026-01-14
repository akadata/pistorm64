#!/usr/bin/env bash
# Quick helper to rebuild tools and run a single-track dump on DF0:
# - Builds regtool/ioharness/dumpdisk
# - Ensures the floppy motor is off before starting
# - Dumps track 0, side 0 to dump.raw
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

echo "Building register tools..."
bash build_reg_tools.sh

echo "Stopping motor before test..."
sudo ./build/ioharness --disk-motor off || true

echo "Dumping track 0, side 0 to dump.raw..."
sudo ./build/dumpdisk --out dump.raw --drive 0 --tracks 1 --sides 1

echo "Done. Output: $ROOT/dump.raw"
