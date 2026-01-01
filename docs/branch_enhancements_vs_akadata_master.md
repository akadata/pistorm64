# Branch Enhancements vs akadata/master

Summary of changes in this branch compared to `origin/akadata/master`.

## Build system
- Added configurable optimisation level (`O=`) with default `-O3`.
- Added CPU tuning overrides (`MARCH=`, `MCPU=`, `MTUNE=`) plus `ARCH_FEATURES` for AArch64 feature modifiers (e.g. `+crc+simd+fp16+lse`).
- Added optional PMMU and EC/020/EC040 FPU toggles (`USE_PMMU`, `USE_EC_FPU`) and documented in `docs/mmu_fpu_toggles.md`.
- Added `USE_VC` guard that stubs out Pi host SDK, and `USE_ALSA` guard with a null AHI backend.
- Introduced `USE_RAYLIB` null RTG backend and cleaned duplicate symbols in the null path.

## Emulator/runtime
- Fixed CLI config handling for `--config/--config-file` even when `USE_VC=0` (stub now stores filename).
- Added FC-enabled CPLD variant under `rtl/fc_amiga/` with BGACK tri-state gating and FC capture.
- Added PMMU/FPU enable knobs in Musashi init to force FPU on EC parts when desired.
- Experimental JIT backend switch: `--jit` flag or config `jit on|yes|1` sets the backend to the (currently Musashi-backed) JIT path.
- Optional FPU-only JIT hook (`--jit-fpu` or config `jitfpu on|yes|1`) routes F-line opcodes through the JIT hook (still Musashi underneath).
- ROM (e.g., Kickstart) mappings are now mlock()'d on load to keep them resident; failure is warned but non-fatal.
- Added optional thread affinity via env `PISTORM_AFFINITY=cpu=1,io=2,input=3` (defaults: CPU core 1, IO core 2, input core 3) to reduce contention/jitter.

## Documentation
- New docs: `docs/mmu_fpu_toggles.md`, FC/backport notes, CPLD build notes, and comparison/reference docs in `docs/`.

## Artifacts
- Full diff against `akadata/master` saved at `patches/akadata-master-diff.patch` (apply with `patch -p1 < patches/akadata-master-diff.patch`).

Notes: defaults keep conservative behaviour (PMMU off, EC FPU off, VC/ALSA on) unless toggled at build time.
