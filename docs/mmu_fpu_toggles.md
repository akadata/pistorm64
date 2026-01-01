# PMMU / FPU toggles

- `USE_PMMU=1` (Makefile): builds Musashi with PMMU emulation enabled (`PISTORM_EXPERIMENT_PMMU`). Use with 030/040 CPU types; expect some performance cost and test carefully on real hardware.
- `USE_EC_FPU=1` (Makefile): forces FPU present on 68020/68EC020/68EC040/68LC040 to emulate external 68881/68882 even on EC/LC parts.
- Defaults remain off to match the known-good Amiga behavior.

Examples:
- `make PLATFORM=ZEROW2_64 USE_PMMU=1`
- `make PLATFORM=ZEROW2_64 USE_EC_FPU=1 USE_PMMU=1`

Notes:
- These are compile-time toggles; runtime config still controls CPU type selection (`cpu_type` in the cfg). Pick an MMU-capable CPU (68030/68040) if you want SysInfo to report an MMU.
- PMMU is still experimental; keep logging on and validate memory-heavy workloads before relying on it.
