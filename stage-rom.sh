#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROM_PATH="${PPC_ACCEL_RESET_ROM:-$HOME/BlizzardPPC040.rom}"

cd "$ROOT_DIR"

if [[ ! -f "$ROM_PATH" ]]; then
  cat <<EOF
ERROR: PPC reset ROM not found:
  $ROM_PATH

Set PPC_ACCEL_RESET_ROM to the ROM location and re-run, for example:
  export PPC_ACCEL_RESET_ROM=/home/smalley/BlizzardPPC040.rom
  ./stage-rom.sh
EOF
  exit 1
fi

make -j4 emulator
make -C amiga/zorro-ppc

cat <<EOF
Stage ROM build complete.

ROM in use (not copied into repo):
  $ROM_PATH

Manual test steps:
1) Enable PPC board in config:
   setvar zorro-ppc

2) Launch emulator with clean runtime + external reset ROM:
   unset LD_LIBRARY_PATH
   export QEMU_UAE_SO=/usr/local/lib/qemu-uae.so
   export PPC_ACCEL_RESET_ROM=$ROM_PATH
   export PPC_ACCEL_RESET_ROM_ALLOW=1
   # Optional debug:
   # export PPC_ACCEL_QEMU_LOG=1
   # export PPC_ACCEL_MMIO_TRACE=1
   # export PPC_ACCEL_AC_TRACE=1
   ./emulator --log ppc.log --log-level info

3) On Amiga side, probe board and IRQ path:
   ppcshake --id
   ppcshake --irq

Expected host log markers:
- loaded PPC reset ROM '...'
- external PPC reset ROM active; built-in reset trampoline bypassed

To return to built-in trampoline path:
   unset PPC_ACCEL_RESET_ROM PPC_ACCEL_RESET_ROM_ALLOW
EOF
