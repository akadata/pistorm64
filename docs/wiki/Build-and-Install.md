# Build and Install

## Build Targets

From `AGENTS.md` and `Makefile`:

```bash
make                     # build emulator + diagnostics
make clean               # clean build outputs
make PISTORM_KMOD=0      # legacy userspace GPIO backend
make USE_RAYLIB=0        # disable raylib RTG
make USE_ALSA=0          # disable ALSA audio
make kernel_module       # build kernel module (pistorm.ko)
make kernel_install      # install kernel module
```

Tools:

```bash
./build_regtool.sh
./build_clkpeek.sh
./build_pimodplay.sh
```

## Install Layout (strict)

PiStorm64 install prefix is fixed to:

- `/opt/pistorm64/emulator`
- `/opt/pistorm64/a314/` (from `src/a314/files_pi/`)
- `/opt/pistorm64/data/` containing:
  - `lsegout.bin`
  - `adfs/`
  - `fs/`
  - `a314-shared/` (empty initially)

Do **not** place Python code under `data/a314-shared/`.

## Environment Variables (strict)

The emulator sets these before starting Python services:

- Required: `PISTORM_ROOT`, `PISTORM_A314`, `PISTORM_DATA`, `A314_SHARED`
- Recommended: `A314_CONF`, `A314_FS_CONF`

Python services must use env-based paths only. Do not use `~`, CWD, or hard-coded
`/opt` paths.

## Kernel Module

Build and install using:

```bash
make kernel_module
sudo make kernel_install
```

For more kernel tuning, see `docs/README.md` (gpclk options) and
`kernel_module/rpi_kernel.config-6.18.6-v8+` for the Pi kernel config.
