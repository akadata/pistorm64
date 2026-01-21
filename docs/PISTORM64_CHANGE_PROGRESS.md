# PiStorm64 Change Progress

This document summarizes the recent PiStorm64 updates and alignment/logging work.

## Kernel Module + Userspace Integration
- Switched tool builds to the kmod userspace backend (`src/gpio/ps_protocol_kmod.c`) and added
  UAPI header includes where required.
- Updated `build_buptest.sh` and related `build_*.sh` scripts to use the kmod backend.
- Defaulted `PISTORM_KMOD ?= 1` in `Makefile`.
- Added kernel module loading/tuning examples and a dedicated `kernel_module/README.md`.

## Buptest + Protocol Alignment
- Buptest now uses the kmod userspace protocol path, avoiding direct `/dev/mem` access.
- Longword access paths in `src/emulator.c` consistently use two 16-bit operations
  (and odd-address 8/16/8), matching the kernel module implementation.

## Scheduling / RT Priority
- RT scheduling requests are now skipped when not permitted (no `CAP_SYS_NICE` /
  no `RLIMIT_RTPRIO`), with a single concise warning.
- `etc/security/limits.d/pistorm-rt.conf` updated to include `nice` allowance and a note
  about group membership.

## Logging Consistency
- Standardized thread logs for IPL/RTG/AHI to `[TAG]` format.
- Added `[CFG]` prefixes for config-related messages.

## Branding + CLI Improvements
- Added `-h/--help` and `-a/--about` with:
  - `KERNEL PiStorm64`
  - `JANUS BUS ENGINE`
- Exposed `.cfg` options via CLI flags:
  - `--config/-c`, `--cpu/-C`, `--loopcycles/-L`
  - `--jit/-j`, `--jit-fpu/-f`
  - `--map/-m`
  - `--mouse/-M`, `--keyboard/-K`, `--kbfile/-k`
  - `--platform/-p`
  - `--setvar/-sv` (simple 2-token values; complex values remain in `.cfg`)

## Files Added/Updated
- Added: `kernel_module/README.md`
- Added: `PISTORM64_CHANGE_PROGRESS.md`
- Updated: `src/emulator.c`
- Updated: `src/config_file/config_file.c`
- Updated: `src/config_file/config_file.h`
- Updated: `src/buptest/buptest.c`
- Updated: `src/platforms/platforms.c`
- Updated: `src/platforms/amiga/ahi/pi_ahi.c`
- Updated: `src/platforms/amiga/rtg/rtg-output-raylib.c`
- Updated: `src/platforms/amiga/rtg/rtg-output-sdl2.c`
- Updated: `build_buptest.sh`
- Updated: `build_clkpeek.sh`
- Updated: `build_pimodplay.sh`
- Updated: `build_piamisound.sh`
- Updated: `build_reg_tools.sh`
- Updated: `build_zz9tests.sh`
- Updated: `etc/security/limits.d/pistorm-rt.conf`
- Updated: `Makefile`
