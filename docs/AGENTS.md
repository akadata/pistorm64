# Repository Guidelines (PiStorm)

This repo is actively developed on a **build host** (currently a Pi 4), and tested on a **target host** (currently a Pi Zero 2 W) that is mounted over **NFS**. The goal of this file is to make that workflow obvious and reduce foot‑guns.

## Working setup (Pi4 build host → Pi Zero 2 W target)

* **Build host (Pi 4):** edit code, run `make`, build tools, and stage artifacts.
* **Target host (Pi Zero 2 W):** runs the emulator against real hardware; GPIO access typically requires `sudo`.
* **NFS mount:** the target exports its working tree (or a subdir) and the build host mounts it, so edits/builds on the Pi 4 are immediately visible on the Pi Zero 2 W.

### Canonical workflow

1. **Edit on Pi 4** in the NFS-mounted repo.
2. **Build on Pi 4** (fast compile).
3. **Run on Pi Zero 2 W** (real GPIO + timing + audio + PiStorm hardware).

> Rule of thumb: anything involving GPIO / real bus timing belongs on the Zero; compilation and refactors belong on the Pi 4.

### NFS sanity checks

* On the **Pi 4**, confirm you are working inside the NFS mount:

  * `mount | grep nfs`
  * `stat .` and verify path is the mounted one
* On the **Pi Zero 2 W**, confirm the same tree has your latest edits:

  * `git status` shows the same changes

## Project structure & module organization

* `src/`: core emulator sources, platforms (`src/platforms/amiga`, `src/platforms/mac68k`), GPIO helpers, Musashi 68k core, and self-tests.

  * GPIO diagnostics: `src/buptest` and `src/test`
  * Amiga registers/tools (examples): `src/platforms/amiga/registers/`
* Configs (examples: `amiga.cfg`, `mac68k.cfg`, `default.cfg`) define ROM/HDF paths and runtime options.
* `data/`: supporting assets (do not commit ROMs/HDFs).
* Tools/helpers:

  * `tools/` (audio converters, misc helpers)
  * build scripts: `build_*.sh`
  * demo binaries (example): `pimodplay` + wrapper `pimodplay.sh`
* Docs: `docs/`
* CPLD/bitstream sources: `cpld_code/`
* Hardware assets: `Hardware/`
* Service/deployment units: `systemd/`

## Build, test, and development

### Build (on Pi 4)

* Main build:

  * `make [USE_RAYLIB=0 USE_ALSA=0 USE_PMMU=0 PLATFORM=ZEROW2_64]`
  * `make clean`
* Tool builds:

  * `./build_regtool.sh`
  * `./build_clkpeek.sh`
  * `./build_pimodplay.sh`
  * `./build_buptest.sh`
  * `./build_zz9tests.sh`

### Run (on Pi Zero 2 W)

* Emulator (GPIO access usually requires `sudo`):

  * `sudo ./emulator --config <cfg> [--log <file>]`
* GPIO / clock checks:

  * `sudo ./buptest`
* Audio sanity:

  * `./pimodplay.sh sample.wav`

### Nix builds

* From `flake/`:

  * `nix build .#pistorm`
  * output staged under `$out`.

## Coding style & conventions

* Format C/C++ using `.clang-format` (LLVM base):

  * 2-space indent, no tabs, 100-column limit, attached braces, left-aligned pointers.
  * run: `clang-format -i <file>` before sending changes.
* Use `snake_case` for functions/variables, ALL_CAPS for constants/defines.
* Keep logging concise and hardware-focused; avoid speculative comments in hot paths.
* Prefer Makefile toggles (`USE_RAYLIB`, `USE_ALSA`, `USE_PMMU`, `PLATFORM`) over new ad-hoc `#ifdef`s.

## Testing guidelines

* The emulator runs an alignment self-test on startup.
* Validate wiring/clock/bitstream with `buptest` on the target hardware.
* Additional GPIO loopbacks live in `src/test` (hardware only).
* There is no automated CI.

When reporting results, always include:

* hardware model (Pi Zero 2 W / Pi 4 / etc.)
* bitstream version (if applicable)
* config file used
* exact command line
* relevant log excerpts

## Git / commits / PR hygiene

* Commit messages: short, present tense (e.g., `fix pimodplay stereo channel map`).
* Keep one change set per commit.
* **Do not commit**: binaries, ROMs, HDF images, generated objects.
* Use focused branches (see `docs/COMMIT_PLAN.md`).
* PRs should include: problem statement, summary of changes, hardware/config tested, and command output/screenshots if logs/UI changed.

