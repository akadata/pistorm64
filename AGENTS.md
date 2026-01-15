# Repository Guidelines (PiStorm)

This repo is actively developed on a **build host** (currently a Pi 4), and tested on a **target host** (currently a Pi Zero 2 W) that is mounted over **NFS**. The goal of this file is to make that workflow obvious and reduce foot‑guns.

## Working setup (Pi4 build host → Pi Zero 2 W target, sockets not NFS)

* **Build host (Pi 4):** edit code, run `make`, build tools, stage artifacts locally.
* **Target host (Pi Zero 2 W):** runs the emulator against real hardware; GPIO access typically requires `sudo`.
* **Transport:** NFS is offline; use SSH (`amiga` helper) + `rsync/scp` for file sync, and socket pipes per `NETWORK_PIPE.md` for markers/control.

### Canonical workflow (no NFS)

1. Edit and build on the Pi 4 working tree.
2. Sync outputs to the Pi Zero 2 W (examples):
   * `rsync -av --delete build/ amiga:/home/smalley/pistorm/build/`
   * `rsync -av --delete data/ amiga:/home/smalley/pistorm/data/ --exclude '*.rom' --exclude '*.hdf'`
3. Run on the Pi Zero 2 W via `amiga '<cmd>'` (real GPIO + timing + audio + PiStorm hardware).
4. Use socket markers (see `NETWORK_PIPE.md`) for coordinated captures/logging instead of NFS visibility.

> Rule of thumb: anything involving GPIO / real bus timing belongs on the Zero; compilation and refactors belong on the Pi 4. Do not assume shared filesystems.

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

## Work order: PiStorm GPIO kernel module (Pi Zero 2 W focus, Pi 4 compatible)

Goal: stop using `/dev/mem` for pins/clock, claim GPIO + GPCLK cleanly, and ship a sane ABI foundation before touching the timing-critical bus loop.

Deliverables:
- Platform driver `pistorm.ko` bound via DT overlay for Pi Zero 2 W + Pi 4 (no hard-coded base addresses). Use `devm_platform_ioremap_resource`, `devm_clk_get`/`clk_set_rate`, pinctrl/gpiod for ownership.
- `/dev/pistorm0` ioctls (fix direction macros: writes use `_IOW`, reads use `_IOR`, structs use `__u32`):
  - `SETUP`, `TEARDOWN`
  - `PULSE_RESET`
  - `SET_MOTOR` on/off
  - `RUN_BATCH` (`_IOWR`): array of ops executed in one kernel loop (optionally with short `preempt_disable()`); results written back in place.
- Userspace test tool `psctl` covering: `setup`, `motor on/off`, `reset`, `run-batch`.
- Keep emulator unchanged for now (still current userspace bus loop); add conditional path later.
- Docs: which pins are claimed, how to load the overlay, expected clock source/rate (GPIO4 GPCLK), and udev/perms for `/dev/pistorm0`.

Structs (recommendation to avoid exploding ioctls):
```
struct ps_op {
  __u32 addr;
  __u32 data;   // input for write, output for read
  __u32 width;  // 8/16/32
  __u32 flags;  // e.g., side, autoinc, reserved for future
};
#define PS_IOCTL_OP        _IOWR('p', 1, struct ps_op)

struct ps_batch {
  __u32 n;
  __u32 reserved;
  struct ps_op ops[];
};
#define PS_IOCTL_RUN_BATCH _IOWR('p', 2, struct ps_batch)
```

Timing expectations:
- Avoid one bus op per ioctl; batching/ring buffer is how we get wins.
- If/when tighter timing is needed, run the hot loop in one kernel context (batch or future mmap ring) with minimal `preempt_disable()`/CPU pinning; avoid broad `local_irq_disable()` unless proven necessary.

## Git / commits / PR hygiene

* Commit messages: short, present tense (e.g., `fix pimodplay stereo channel map`).
* Keep one change set per commit.
* **Do not commit**: binaries, ROMs, HDF images, generated objects.
* Use focused branches (see `docs/COMMIT_PLAN.md`).
* PRs should include: problem statement, summary of changes, hardware/config tested, and command output/screenshots if logs/UI changed.
