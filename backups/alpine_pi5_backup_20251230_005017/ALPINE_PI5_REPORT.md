# Alpine (musl) + Raspberry Pi 5 (aarch64) PiStorm bring-up report

## Snapshot
- Host: Raspberry Pi 5, Alpine Linux (aarch64, musl)
- Target: `sudo ./emulator --config basic.cfg` with PiStorm on 68000 socket
- Current build target: `make PLATFORM=PI5_ALPINE_64BIT`
- Current status: **build succeeds**, emulator **times out waiting for CPLD transaction** because **GPCLK0 cannot be enabled from userspace** on this kernel (and GPIO4 shows no transitions).

## What works
- Builds cleanly on Alpine with `make clean && make PLATFORM=PI5_ALPINE_64BIT -j4`
- Safe GPIO visibility using libgpiod:
  - `sudo ./emulator --gpio-probe` reads `/dev/gpiochip0` line levels (0–23) without touching unsafe chips.
  - Example: GPIO0..7 often read `1`, GPIO8..23 read `0` (varies with pullups/CPLD state).
- RP1 GPIO MMIO mapping for fast protocol I/O works:
  - Emulator maps RP1 `io_bank0` and `sys_rio0` via device-tree `aliases/gpio0`.
  - Example log: `RP1: mapped io_bank0 @ 0xc0400d0000 (len=0xc000), sys_rio0 @ 0xc0400e0000 (len=0xc000)`

## What fails (current blocker)
### 1) GPCLK0 enable on GPIO4 fails (EPERM)
- `sudo ./emulator --gpclk-probe` attempts to enable GPCLK0 and prints diagnostic state.
- Observed on Alpine kernel:
  - `GPCLK: mmap failed @ 0x107c700000, errno=1; skipping GPCLK setup.`
  - `clk_transitions=0/128` and GPIO4 remains static.
- `/proc/iomem` shows:
  - `107c700000-107c70000f : 107c700000.clock clock@7c700000`
  - The exposed resource is only 16 bytes; `/dev/mem` typically maps page-sized regions, and kernels commonly deny mapping beyond the declared resource (often `CONFIG_STRICT_DEVMEM`).
- Result: CPLD never sees a running clock on `PI_CLK` (GPIO4), and bus transactions never complete.

### 2) Bus read stalls at first reset transaction
- With bounded wait enabled:
  - `sudo env PISTORM_TXN_TIMEOUT_US=200000 ./emulator --config basic.cfg`
- The first `ps_read_16(addr=0)` times out:
  - `RP1: timeout waiting for CPLD transaction to complete (ps_read_16 addr=0x00000000).`
- GPIO dump at timeout typically shows:
  - `txn=1/1` (TXN_IN_PROGRESS stuck high)
  - `clk_transitions=0/128` (no clock)

## Safety constraints discovered
- Avoid probing “random” gpiochips/gpiomem nodes on Pi 5: user observed hard crashes involving PCIe/NVMe when touching the wrong device/register sets.
- Only `/dev/gpiochip0` is treated as “safe” for probing in this repo state.

## Code changes made in this repo (Pi 5 bring-up)
- Added libgpiod probe mode: `--gpio-probe` (only `/dev/gpiochip0`, lines 0–23).
- Added GPCLK probe mode: `--gpclk-probe` (tries to enable GPCLK0 then prints GPIO state; no CPLD transactions).
- Added transaction timeout + state dump to avoid infinite hangs:
  - `PISTORM_TXN_TIMEOUT_US` (default 500000us)
- Added extra GPCLK logging:
  - `PISTORM_GPCLK_DEBUG=1`
- RP1 pinmux strategy:
  - GPIO4 set to `GPCLK[0]` (FUNCSEL a0/0)
  - Protocol pins GPIO0..23 set to `SYS_RIO[n]` (FUNCSEL a5/5), and **avoid touching GPIO24..27**
- **Critical electrical fix:** never drive GPIO5 from the Pi side (CPLD drives `PI_RESET` on GPIO5 per `rtl/pistorm.v`).
- GPCLK base derivation:
  - Attempts to resolve BCM2712 clock block from DT symbol `dvp` (typically `/soc@107c000000/clock@7c700000`)
  - Supports overrides:
    - `PISTORM_GPCLK_CM_CHILD=0x7c700000`
    - `PISTORM_GPCLK_CM_PHYS=0x107c700000`
  - Still blocked on Alpine kernel due to `mmap` EPERM.

## Commands used (reference)
### Build
- `make clean`
- `make PLATFORM=PI5_ALPINE_64BIT -j4`

### Safe probes
- `sudo ./emulator --gpio-probe`
- `sudo env PISTORM_GPCLK_DEBUG=1 ./emulator --gpclk-probe`

### Run with bounded bus wait (prevents indefinite hangs)
- `sudo env PISTORM_TXN_TIMEOUT_US=200000 ./emulator --config basic.cfg`

### Useful system info
- `sudo cat /proc/iomem | grep -i clock`
- `sudo gpioinfo -c /dev/gpiochip0 | head -n 80`
- `sudo pinctrl set 4 a0` (force GPIO4 mux to GPCLK[0])

## Packages installed on Alpine (so far)
- Build/runtime: `gdb`, `libgpiod`, `libgpiod-dev`, `raspberrypi-utils`, `dtc`, `dtc-dev`
- Graphics deps (for raylib/DRM paths): `mesa-dev`, `mesa-egl`, `mesa-gles`, `mesa-gbm`, `libdrm-dev`, `alsa-lib-dev`, `raylib-dev`

## Recommendation: next OS for GPCLK validation
- Move to **Raspberry Pi OS 64-bit** (first) to validate GPCLK enable via DT overlays/firmware config.
- Goal on Pi OS: enable a GPCLK output on GPIO4 at boot, then re-run the emulator with:
  - `PISTORM_ENABLE_GPCLK=0` (so it doesn’t try `/dev/mem` clock programming)

## Backup / restore checklist (for NVMe re-image)
- Repo state:
  - `git diff > pistorm_pi5_alpine.patch`
  - Keep: `STATUS.md`, `DEBUGGING_PI5.md`, this file (`ALPINE_PI5_REPORT.md`)
- Logs worth saving:
  - `gdb.log`
  - `amiga.log` / `pistorm.log` (if relevant)
- Configs used:
  - `basic.cfg`
- Rebuild recipe (after restore):
  - Install packages listed above
  - `make clean && make PLATFORM=PI5_ALPINE_64BIT -j4`

