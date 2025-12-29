Status

Context
- Goal: Run `sudo ./emulator --config basic.cfg` on Pi 5 Alpine without hard-crashing the system, and get useful diagnostics when GPIO/CPLD handshakes fail.
- Branch base: akadata/master; patches directory exists but most patches do not apply cleanly (likely already integrated or diverged).

What I did last
- Built successfully with `make PLATFORM=PI5_ALPINE_64BIT`.
- Installed packages: `gdb`, `alsa-lib-dev`, `raylib-dev`, `mesa-dev`, `mesa-egl`, `mesa-gles`, `mesa-gbm`, `libdrm-dev`, `libgpiod`, `libgpiod-dev`.
- Ran `sudo gpioinfo` and confirmed `gpiochip0` exposes GPIO0-27 (header pins).
- Checked device-tree nodes for GPIO and clock bases on Pi 5.

Key findings
- GPIO access uses `/dev/mem` + BCM2708 base in `gpio/ps_protocol.c` and `gpio/ps_protocol.h`.
- Pi 5 uses RP1; `/dev/gpiomem` is replaced with `/dev/gpiomem0..4` and `/dev/gpiochip*`.
- Old BCM2708 base mapping fails with `errno=1 (EPERM)` on Pi 5; RP1 mapping via device-tree works.
- `gpioinfo` works only under `sudo`.

Current hypothesis
- RP1 GPIO MMIO mapping works, but CPLD handshakes can hang if GPCLK is not running or pin muxing is wrong.

Next steps
- Confirm GPCLK is actually present on GPIO4 (PiStorm clock) and the CPLD responds (TXN_IN_PROGRESS clears).
- If TXN_IN_PROGRESS never clears, capture the printed GPIO dump and gdb backtrace for later analysis.

Update
- Removed unsafe gpiomem/MMIO probing (it can hard-crash Pi 5 when it hits the wrong device/register layout).
- Added `sudo ./emulator --gpio-probe` which uses libgpiod and targets `/dev/gpiochip0` only (override with `PISTORM_GPIOCHIP`).
- Added `ps_dump_protocol_state()` print just before `m68k_set_cpu_type()` so we can see GPIO levels before the first bus accesses.
- Added RP1 transaction timeout: if `PIN_TXN_IN_PROGRESS` never clears, the emulator now dumps GPIO state and exits instead of hanging forever.
  - Tune via `PISTORM_TXN_TIMEOUT_US` (default 500000).
- RP1 pinmux: GPIO4 is now left as `GPCLK[0]` (FUNCSEL a0/0) while the other protocol pins use `SYS_RIO[n]` (a5/5).
- If a crash leaves build artifacts corrupted (e.g., empty `.o` files / many link undefined refs), run `make clean` and rebuild.

Current safe command to run first
- `sudo ./emulator --gpio-probe`

GDB quickstart (logs to `gdb.log`)
- `sudo gdb -q -x gdb_pistorm_init.gdb --args ./emulator --config basic.cfg`
- In gdb: `run`, then if it stalls `Ctrl-C`, then `bt` / `thread apply all bt`

Legacy backend reference
- `LEGACY_GPIO_BACKEND.md` describes the pre–Pi 5 `/dev/mem` BCM GPIO path, pin roles, and where the timings/handshakes live.

Build changes (Pi 5 Alpine)
- `Makefile` now adds `-DHAVE_LIBGPIOD` and links `-lgpiod` for `PLATFORM=PI5_ALPINE_64BIT`.
- `Makefile` now also defines `-DPISTORM_RP1=1` to use RP1 IO_BANK0 + SYS_RIO0 for GPIO on Pi 5.
- `Makefile` on Pi 5 Alpine now links against the system `libraylib` (the bundled `raylib_drm/libraylib.a` is 32-bit ARM and incompatible with aarch64).

RP1 notes
- RP1 GPIO blocks (from `rp1-peripherals.txt`): `io_bank0` @ `0x400d0000`, `sys_rio0` @ `0x400e0000`, `pads_bank0` @ `0x400f0000`.
- SYS_RIO provides fast OUT/OE/IN with atomic set/clear aliases (+0x2000/+0x3000) to avoid slow read-modify-write across PCIe.
