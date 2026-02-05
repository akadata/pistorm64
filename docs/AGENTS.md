# Repository Guidelines

## Project Structure & Module Organization
- `src/` holds the emulator core, platform adapters (`platforms/amiga`, `platforms/zorro`, `platforms/rtg`), GPIO helpers and the Musashi-derived CPU glue.
- `kernel_module/` keeps the Pi 4 driver plus a README/Makefile that build and install the pistorm/z3bus character devices.
- `docs/` lists architecture, testing, and branching plans such as the commit plan and kernel-module roadmap.
- `tools/`, `amiga/`, `media/`, `boot/`, `include/`, `data/`, `web/`, and `bin/` host helpers, assets, FPGA configs, and shared headers.

## Build, Test, and Development Commands
- `make` — builds the emulator for the default PI4_64BIT platform while honoring the kernel-module shim; run `make clean` before changing flags.
- `make PISTORM_KMOD=1` — compiles `ps_protocol_kmod.c` so the emulator talks through `/dev/pistorm`.
- `make USE_ALSA=0` / `make USE_RAYLIB=0` — drop the Pi AHI audio or raylib-based RTG backend when tooling is unavailable.
- `./build_regtool.sh`, `./build_clkpeek.sh`, `./build_pimodplay.sh` — rebuild the diagnostic tools highlighted in `docs/README.md`.
- `make -C kernel_module module` (as a normal user), `sudo make -C kernel_module load`, `sudo rmmod pistorm`, and `make -C kernel_module clean` cover kernel driver lifecycle.

## Coding Style & Naming Conventions
C files prefer two-space indentation, grouped headers (project includes before `<...>`), and explicit `uint8_t`/`uint32_t` usage. Runtime flags/macros stay uppercase with `PISTORM_`/`USE_` prefixes while helpers/functions stay in `snake_case` (e.g., `fc_shadow_touch`). Align logging with `log_get_level()` and keep the tree whitespace-consistent.

## Testing Guidelines
Smoke and regression tests stem from `tools/pistorm_smoke.c`, which produces `pistorm_smoke_test`/`pistorm_truth_test`. After building the kernel module, run the smoke binary on the hardware path to validate ioctl coverage; note any `gpclk_div` or `gpclk_src` tuning in your report.

## Commit & Pull Request Guidelines
Follow the branching plan in `docs/COMMIT_PLAN.md`: create a `feature/*` branch per change, keep commits focused, and describe them in short phrases (e.g., “fix gpclk tuning”). Each PR should document which hardware or smoke tests ran, cite related docs/issues, and ideally stay narrow so the maintainers can review quickly.

## Kernel Module & Hardware Notes
`kernel_module/Makefile` intentionally refuses root builds to prevent dirty `.d` files; compile as your normal user. After `sudo insmod pistorm.ko` you can adjust clock tuning (`gpclk_div=12` or `gpclk_src=6`) to stabilize the CPLD path, then chmod `/dev/pistorm` for the emulator. Log these parameters in your PR description when they matter.
