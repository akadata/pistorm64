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

## GPIO Backend Policy
- Always build and run against `src/gpio/ps_protocol_kmod.c` (the `/dev/pistorm` kernel backend). The legacy `ps_protocol.c` should no longer be referenced or compiled in any emulator, daemon, or support tool unless you are explicitly working on the fallback userspace GPIO path and know exactly why.
- Under no circumstances should new changes or experiments include `ps_protocol.c`; it is retired and must not be part of the build unless explicitly restoring the legacy userspace GPIO backend.

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

## AmigaDOS Primer (Project-Specific)
AmigaDOS is not Unix. Use canonical Amiga paths and assigns in docs/scripts/tools.

- **Volumes/devices:** `DF0:` floppy, `DH0:`/`DH1:` hard disk partitions, `SYS:` current boot volume.
- **Assigns:** `C:`, `S:`, `LIBS:`, `DEVS:`, `L:`, `FONTS:`, `PREFS:`, `ENV:`, `ENVARC:`, `T:`. Use these in instructions instead of `/`.
- **Boot scripts:** `S:Startup-Sequence` (system), `S:User-Startup` (third-party). Prefer appending to `S:User-Startup`.
- **Binaries:** copy CLI tools to `C:`.
- **Libraries:** copy `.library` files to `LIBS:`.
- **Handlers / filesystems:** copy to `L:`; mountfiles go in `DEVS:DosDrivers/`.
- **Monitors / RTG:** use `DEVS:Monitors/`.
- **Network devices:** use `DEVS:Networks/`.
- **Prefs tools:** `SYS:Prefs/` (GUI tools).
- **Environment:** write persistent settings to `ENVARC:` (copied to `ENV:` at boot).
- **Case:** AmigaDOS is case-insensitive, but the host filesystem is not. Use canonical names (`C`, `S`, `LIBS`, etc.) in repo paths and docs.

## Host Serial Bridge (PiSide)
The emulator runs unprivileged; do not assume it can write under `/dev/`.
Serial devices should publish a stable user‑writable path:

- Prefer: `$XDG_RUNTIME_DIR/amiga/serial/z2serial0`
- Fallback: `/run/user/<uid>/amiga/serial/z2serial0`
- Final fallback: `/tmp/amiga/serial/z2serial0`

Always log the real PTY path and the published symlink path.

## Commit & Pull Request Guidelines
- Use short, descriptive commit subjects (current history is informal, often lowercase).
- PRs should note hardware/OS tested, config changes (`etc/`, `boot/`), and include logs/screenshots for hardware-visible changes.
