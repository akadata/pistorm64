Status

Context
- Goal: Run `sudo ./emulator --config basic.cfg` on Pi 5 Alpine without hard-crashing the system, and get useful diagnostics when GPIO/CPLD handshakes fail.
- Branch base: akadata/master; patches directory exists but most patches do not apply cleanly (likely already integrated or diverged).
- Alpine snapshot report: `ALPINE_PI5_REPORT.md`

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
- RP1 GPIO MMIO mapping works, but CPLD handshakes can hang if GPCLK is not running or if we accidentally drive pins that the CPLD drives.

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
- Important: the CPLD drives GPIO5 (`PI_RESET` is an output on GPIO5 in `rtl/pistorm.v`), so Pi-side code must not enable output drive on GPIO5.
- RP1 GPCLK0: the Pi 5 build now attempts to enable GPCLK0 for GPIO4 via `/dev/mem` (disable with `PISTORM_ENABLE_GPCLK=0`; tune with `PISTORM_GPCLK_SRC` / `PISTORM_GPCLK_DIV_INT`).
  - On Pi 5 the clock-manager MMIO base is derived from the DT symbol `dvp` (typically `/soc@107c000000/clock@7c700000`, seen in `/proc/iomem` as `107c700000.clock`).
  - Overrides (if needed): `PISTORM_GPCLK_CM_CHILD` (32-bit child address, e.g. `0x7c700000`) or `PISTORM_GPCLK_CM_PHYS` (full physical address, e.g. `0x107c700000`).
  - Extra log: `PISTORM_GPCLK_DEBUG=1` prints the raw `ctl/div` values before/after programming.
- Added `sudo ./emulator --gpclk-probe` to attempt GPCLK enable and print `[GPIO] gpclk-probe ... clk_transitions=...` without doing any CPLD transactions.
- Current blocker: on this Pi 5 kernel, mapping the SoC clock block via `/dev/mem` fails with `errno=1 (EPERM)`:
  - Example: `GPCLK: mmap failed @ 0x107c700000, errno=1; skipping GPCLK setup.`
  - `/proc/iomem` shows the region is extremely small (`107c700000-107c70000f`), which likely causes `/dev/mem` to deny mapping a full page.
  - This means GPCLK0 probably must be enabled via kernel clock framework / DT overlay (boot-time), not by direct userspace MMIO.
- If a crash leaves build artifacts corrupted (e.g., empty `.o` files / many link undefined refs), run `make clean` and rebuild.

Current safe command to run first
- `sudo ./emulator --gpio-probe`
  - Then: `sudo ./emulator --gpclk-probe` (expect `clk_transitions` to be non-zero if the clock is actually running).

Latest observations (gpiochip probe)
- `sudo ./emulator --gpio-probe` currently shows lines 0-7 reading `1` and lines 8-23 reading `0` on `/dev/gpiochip0`.
  - Note: `line 0 (ID_SDA)` and `line 1 (ID_SCL)` are GPIO0/GPIO1 (HAT ID pins) and are normally pulled high on the Pi; if the CPLD is alive and driving `PI_TXN_IN_PROGRESS`/`PI_IPL_ZERO`, these levels may change.
- Manual libgpiod tests on `/dev/gpiochip0`:
  - `sudo gpioinfo -c /dev/gpiochip0 17/22/23` initially reported `input`.
  - `sudo gpioset -c /dev/gpiochip0 -t 200ms,0 17=1` / `17=0` resulted in `GPIO17` showing `output` in `gpioinfo`.
  - Caution: GPIO17 is within the PiStorm protocol pin range on legacy builds (GPIO8..23 = data bus); avoid `gpioset` on protocol pins when the PiStorm is connected to the Amiga/CPLD unless you explicitly intend to drive the bus.

Monitoring for edges (libgpiod tools)
- `gpiomon` does not support `-r`; use edges selection instead, e.g.:
  - `sudo gpiomon -c /dev/gpiochip0 -e both 0 1` (watch GPIO0/GPIO1 transitions)
  - `sudo gpiomon -c /dev/gpiochip0 -e both 0 --idle-timeout 2000ms` (exit after 2s of no edges)

GDB quickstart (logs to `gdb.log`)
- `sudo gdb -q -x gdb_pistorm_init.gdb --args ./emulator --config basic.cfg`
- In gdb: `run`, then if it stalls `Ctrl-C`, then `bt` / `thread apply all bt`
  - If you type commands manually: prefer `set logging enabled on` (the older `set logging on` alias is deprecated and can be confusing).

Legacy backend reference
- `LEGACY_GPIO_BACKEND.md` describes the pre–Pi 5 `/dev/mem` BCM GPIO path, pin roles, and where the timings/handshakes live.

Build changes (Pi 5 Alpine)
- `Makefile` now adds `-DHAVE_LIBGPIOD` and links `-lgpiod` for `PLATFORM=PI5_ALPINE_64BIT`.
- `Makefile` now also defines `-DPISTORM_RP1=1` to use RP1 IO_BANK0 + SYS_RIO0 for GPIO on Pi 5.
- `Makefile` on Pi 5 Alpine now links against the system `libraylib` (the bundled `raylib_drm/libraylib.a` is 32-bit ARM and incompatible with aarch64).

RP1 notes
- RP1 GPIO blocks (from `rp1-peripherals.txt`): `io_bank0` @ `0x400d0000`, `sys_rio0` @ `0x400e0000`, `pads_bank0` @ `0x400f0000`.
- SYS_RIO provides fast OUT/OE/IN with atomic set/clear aliases (+0x2000/+0x3000) to avoid slow read-modify-write across PCIe.
