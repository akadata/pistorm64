# Repository Guidelines

## Project Structure & Module Organization
- `src/` holds the emulator sources (C/C++). A314 compiled code is only `src/a314/a314.cc`.
- `src/a314/files_pi/` contains runtime Python services and assets; these are copied verbatim at install time and are not compiled.
- `data/` is runtime data only (`lsegout.bin`, `adfs/`, `fs/`, `a314-shared/`). No code or configs live in `data/`.
- `kernel_module/` builds `pistorm.ko`. `tools/` and `build_*.sh` build auxiliary utilities.
- `docs/` contains project background and hardware notes.

## Build, Test, and Development Commands
- `make` builds the emulator plus `buptest` and `pistorm_truth_test`.
- `make clean` removes build artifacts.
- `make PISTORM_KMOD=0` builds with legacy userspace GPIO.
- `make USE_RAYLIB=0` disables RTG/raylib; `make USE_ALSA=0` disables ALSA audio.
- `make kernel_module` / `make kernel_install` build/install the kernel module.
- `./build_regtool.sh`, `./build_clkpeek.sh`, `./build_pimodplay.sh` build specific tools.

## Coding Style & Naming Conventions
- Format C/C++ with `.clang-format` (LLVM base, 2-space indents, 100-column limit, spaces only).
- Keep naming consistent with existing modules (snake_case files, `src/platforms/*`).

## Testing Guidelines
- No formal test framework; rely on built utilities and hardware runs.
- `buptest` and `pistorm_truth_test` are primary diagnostics.

## A314 Install Layout (Strict)
Install prefix layout must be:
- `/opt/pistorm64/emulator`
- `/opt/pistorm64/a314/` (everything from `src/a314/files_pi/`)
- `/opt/pistorm64/data/` containing `lsegout.bin`, `adfs/`, `fs/`, and `a314-shared/` (empty initially)
Never place Python code under `data/a314-shared/`.

## Environment & Path Rules (Strict)
The emulator sets environment variables before starting Python services:
- Required: `PISTORM_ROOT`, `PISTORM_A314`, `PISTORM_DATA`, `A314_SHARED`
- Recommended: `A314_CONF`, `A314_FS_CONF`
Python must use env-based paths only and must not use `~`, CWD, or hard-coded `/opt` paths.

## lsegout.bin Policy (Strict)
`lsegout.bin` must be bounded (hard max ~64MB). Prefer tmpfs or anonymous RAM backing; otherwise preallocate and refuse growth beyond the limit, logging and failing safely.

## Commit & Pull Request Guidelines
- Use short, descriptive commit subjects (current history is informal, often lowercase).
- PRs should note hardware/OS tested, config changes (`etc/`, `boot/`), and include logs/screenshots for hardware-visible changes.
