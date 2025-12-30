Status

Context
- Goal: Run `sudo ./emulator --config basic.cfg` on Raspberry Pi 5 + PiStorm without hard-crashing the system, and get useful diagnostics when GPIO/CPLD handshakes fail.
- Branch base: akadata/master; patches directory exists but most patches do not apply cleanly (likely already integrated or diverged).
- Alpine snapshot report: `ALPINE_PI5_REPORT.md` (kept so we can return to Alpine later).

What I did last
- Built successfully with `make PLATFORM=PI5_ALPINE_64BIT`.
- Installed packages: `gdb`, `alsa-lib-dev`, `raylib-dev`, `mesa-dev`, `mesa-egl`, `mesa-gles`, `mesa-gbm`, `libdrm-dev`, `libgpiod`, `libgpiod-dev`.
- Ran `sudo gpioinfo` and confirmed `gpiochip0` exposes GPIO0-27 (header pins).
- Checked device-tree nodes for GPIO and clock bases on Pi 5.
- Switched to Raspberry Pi OS (Debian) on Pi 5 so we can use `/boot/firmware` DTBs + overlays for clock bring-up.

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
- If you are providing the clock via another mechanism (e.g. Pi 5 `pwm-pio` overlay), you can tell PiStorm not to touch GPIO4:
  - `PISTORM_RP1_LEAVE_CLK_PIN=1` (leave GPIO4 mux as-is)
  - or `PISTORM_RP1_CLK_FUNCSEL=<0..8>` (force a specific ALT; PIO is typically `7` on RP1)
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
- Build knobs for fresh Pi OS installs:
  - `WITH_RAYLIB=0` uses a headless RTG backend (no `-lraylib` / DRM deps).
  - `WITH_ALSA=0` uses an AHI stub backend (no `alsa/asoundlib.h` / `-lasound`).

RP1 notes
- RP1 GPIO blocks (from `rp1-peripherals.txt`): `io_bank0` @ `0x400d0000`, `sys_rio0` @ `0x400e0000`, `pads_bank0` @ `0x400f0000`.
- SYS_RIO provides fast OUT/OE/IN with atomic set/clear aliases (+0x2000/+0x3000) to avoid slow read-modify-write across PCIe.

## Raspberry Pi OS (Pi 5) update
- Switched from Alpine to Raspberry Pi OS (Debian trixie) to get full `/boot/firmware` DTBs + overlays.
- Added a local GPCLK0 bring-up overlay + module in `pi5/gpclk0/` (helper scripts: `tools/pi5_gpclk0_enable.sh`, `tools/pi5_gpclk0_disable.sh`).
- `dtoverlay=pwm-pio` exists and can generate a clock-like waveform on GPIO4 without using GPCLK0.
  - Runtime load: `sudo dtoverlay pwm-pio,gpio=4`
  - This creates a new PWM device (typically `/sys/class/pwm/pwmchip1` -> `.../pwm_pio@4/...`).
  - Important: configure the **pwm-pio** device (`pwmchip1`), not the hardware PWM (`pwmchip0`).
  - Safe initial test (10Hz square wave):
    - `sudo sh -c 'cd /sys/class/pwm/pwmchip1 && echo 0 > export && echo 100000000 > pwm0/period && echo 50000000 > pwm0/duty_cycle && echo 1 > pwm0/enable'`
    - Verify edges: `sudo gpiomon -c /dev/gpiochip0 -e both 4 --num-events 6`
  - PiStorm run with external clock (don’t touch GPCLK):
    - `sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_CLK_FUNCSEL=7 ./emulator --gpclk-probe`
    - `sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_CLK_FUNCSEL=7 PISTORM_TXN_TIMEOUT_US=200000 ./emulator --config basic.cfg`

## Critical finding (Pi 5 / Pi OS): CPLD output pins float when Amiga is off
- When the Amiga is powered off (CPLD unpowered), the Pi-side pull resistors can change the observed pad level on `PI_TXN_IN_PROGRESS`, `PI_IPL0`, and `PI_RESET`.
- This is expected for an unpowered/floating bus; it does *not* mean the GPIO code is wrong.

CPLD presence test (safe, no bus transactions)
- With the `pistorm-gpclk0` overlay loaded (GPIO4 = GPCLK0) and **no emulator transactions**, run:
  - Baseline:
    - `sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 ./emulator --gpclk-probe`
  - Force pull-down on the suspected CPLD-driven inputs (GPIO0/GPIO1/GPIO5) and re-probe:
    - `sudo pinctrl set 0 ip pd`
    - `sudo pinctrl set 1 ip pd`
    - `sudo pinctrl set 5 ip pd`
    - `sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 ./emulator --gpclk-probe`
  - Force pull-up and re-probe:
    - `sudo pinctrl set 0 ip pu`
    - `sudo pinctrl set 1 ip pu`
    - `sudo pinctrl set 5 ip pu`
    - `sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 ./emulator --gpclk-probe`
  - Restore neutral pulls:
    - `sudo pinctrl set 0 ip pn`
    - `sudo pinctrl set 1 ip pn`
    - `sudo pinctrl set 5 ip pn`

Observed results (on this Pi 5)
- Pull-down forces `txn_pad` and `ipl0_pad` to `0` (they were `1` by default).
- Pull-up forces `rst_pad` to `1` (it was `0` by default).
- Therefore: those lines are **not being actively driven** when the CPLD is unpowered (floating).

Implication
- If the Amiga/CPLD is off, `ps_read_16()` can time out or behave erratically because the handshake lines are floating.
- Make sure the Amiga is powered before drawing conclusions from GPIO sampling.

## New result: Amiga powered on
- With the Amiga powered on, `TXN_IN_PROGRESS` can read as driven low (`txn=0/0`) and the emulator proceeds past startup (IPL/KBD/CPU threads created).
- Example preflight line (from `--config basic.cfg`):
  - `txn=0/0 ... rst=1/1 ...`
- Remaining issue in that run: `gpio4_funcsel=5` and `clk_transitions=0/128` indicated GPIO4 was not providing a clock. This is expected when running with `PISTORM_ENABLE_GPCLK=0` unless an external clock source (GPCLK0 overlay/module or `pwm-pio`) is configured and GPIO4 is muxed appropriately.
  - Quick fix: force GPIO4 mux to GPCLK0 with `PISTORM_RP1_CLK_FUNCSEL=0` (or `sudo pinctrl set 4 a0`) and provide a real clock source.

## Working GPCLK0 (Pi 5) bring-up result
- Using the local DT overlay + helper module, GPIO4 can be driven as `GPCLK0` and shows toggling:
  - `sudo pinctrl get 4` shows: `GPIO4 = GPCLK0`
  - `sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 ./emulator --gpclk-probe` shows:
    - `gpio4_funcsel=0` and `clk_transitions>0`
    - `txn=0/0` (CPLD handshake line driven)
- Note: `./emulator --gpclk-probe` is a standalone mode; don’t combine it with `--config ...` expecting a full run.
- Note: at very high rates (e.g. 200MHz), the RP1 status bits may not reflect rapid toggling; prefer verifying via debugfs (`/sys/kernel/debug/clk/clk_gp0/*`) or by temporarily setting a low overlay `freq=` and validating edges with `gpiomon`.
  - Overlay supports `freq=` (see `pi5/gpclk0/pistorm-gpclk0-overlay.dts`); helper supports `./tools/pi5_gpclk0_enable.sh --freq 1000000`.
 - Note: `gpiomon` (libgpiod) will request GPIO4 as a GPIO input for edge detection, which can override the GPCLK pinmux and effectively *turn off* the clock output on the pin while it is running. Don’t run `gpiomon` on GPIO4 at the same time as PiStorm clock output; use debugfs or a scope/LA instead.
  - Verified: `./tools/pi5_gpclk0_enable.sh --freq 1000000` results in `clk_parent=xosc`, `clk_rate=1000000`, and `clk_transitions~30/128` in `--gpclk-probe` (meaning the pad status bits do reflect low-rate toggling).

## Kernel stability note (Pi OS)
- We saw kernel oopses in VFS (`__d_lookup_rcu`) during earlier clock bring-up experiments while an out-of-tree module was loaded.
- If that happens again:
  - Immediately unload: `./tools/pi5_gpclk0_disable.sh`
  - Reboot and re-test to confirm whether the issue reproduces without the module.
 - Empirical note: switching `pistorm-gpclk0` to higher rates (e.g. `--freq 200000000`) caused SSH connection drops/hard crashes. Low-rate validation (`--freq 1000000`) is stable and proves the GPCLK0 chain works end-to-end.
 - Hypothesis: if the system is booting/running from NVMe, anything that destabilizes RP1/PCIe (or the NVMe HAT’s pogo-pin header wiring) can drop storage and take down the system.

## NVMe HAT pin conflict note
- PiStorm + Amiga power rails are intentionally isolated (Pi 5 must not backfeed the Amiga; Amiga must not feed the Pi 5). Grounds are common.
- Note: power-rail isolation does **not** prevent a 40-pin HAT/pogo-pin connection from loading/contending on GPIO signals, because signals still share a ground reference.
- PiStorm protocol uses **GPIO0-23**, including:
  - GPIO2/3 for `PIN_A0`/`PIN_A1` (control)
  - GPIO14/15 as data bits (`PIN_D(6)` / `PIN_D(7)`)
- Many NVMe HATs use the 40-pin header for power and may also contact GPIO2/3 (I2C) and GPIO14/15 (UART) via pogo pins.
- If those pins are electrically connected, that is a direct conflict with PiStorm and can cause contention, noise, or crashes.
- Recommended for bring-up:
  - Physically isolate the NVMe HAT from GPIO2/3/14/15 (tape/remove pogo pins) or temporarily boot from SD/USB with the NVMe HAT removed.
  - Verify pin consumers: `sudo gpioinfo -c /dev/gpiochip0 2 3 14 15` and `sudo pinctrl get 2 3 14 15`.
  - Observed on this setup: those lines are currently unclaimed by Linux (`gpioinfo` shows `input`, `pinctrl` shows `GPIOx = none` with pulls), but the HAT can still load the lines electrically if it is physically connected.

## Terminology clarification: `A0`/`A1` in logs are PiStorm protocol lines
- The log fields `a0=` and `a1=` (GPIO2/GPIO3) are **Pi-to-CPLD register select** bits `PI_A[1:0]` (see `rtl/pistorm.v`), not the Amiga address bus.
  - In `rtl/pistorm.v`, `input [1:0] PI_A // GPIO[3..2]` selects `REG_DATA/REG_ADDR_LO/REG_ADDR_HI/REG_STATUS`.
- The “address bit 0” concept for 68000 byte lanes is captured separately inside the CPLD as `reg a0;` latched from `PI_D[0]` on a `REG_ADDR_LO` write (also in `rtl/pistorm.v`), and then used to derive `UDS/LDS`.
- Reminder: a 68000 CPU does not expose an `A0` pin; byte selection is via `UDS/LDS`.

## New: safer clock + bus bring-up helpers (Pi 5)
GPCLK scripts
- `tools/pi5_gpclk0_enable.sh` now parses multiple args correctly and supports `--no-emulator-probe`:
  - Example: `./tools/pi5_gpclk0_enable.sh --freq 2000000 --no-emulator-probe`
- `setcheckclock --no-emulator-probe` now truly suppresses the `./emulator --gpclk-probe` step (previously it still happened inside the enable script).
- `setcheckclock` prints a warning if GPIO4 is not currently muxed to `GPCLK0` (common causes: `gpiomon`/`gpioset` on GPIO4, or another overlay claiming the pin).

Bus probe (bounded, no threads)
- New emulator mode: `./emulator --bus-probe` (alias `--txn-probe`)
  - Prints protocol state, reads `ps_read_status_reg()`, then reads reset vectors (`SP` @ 0x0, `PC` @ 0x4), then exits.
  - Uses the existing `PISTORM_TXN_TIMEOUT_US` to avoid hanging forever.
  - Typical run (when GPCLK0 is provided by the overlay/module):
    - `./setcheckclock 2000000 --no-emulator-probe`
    - `sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 PISTORM_TXN_TIMEOUT_US=200000 ./emulator --bus-probe`
  - If you run it **without** enabling a clock on GPIO4 first, you will usually see:
    - `gpio4_funcsel=5` and `clk_transitions=0/128`
    - vectors `SP=0x00000000 PC=0x00000000` (invalid), and a warning.

GDB helper
- New helper to reduce “wrong gdb invocation” mistakes:
  - `./tools/pi5_gdb.sh --config basic.cfg`
  - It runs `sudo gdb -x gdb_pi5_run.gdb ...` and logs output to `gdb.log`.

## New: protocol selection + clearer GPIO dumps (Pi 5 / RP1)
- Pi 5 builds now support selecting between two GPIO/CPLD protocols via `PISTORM_PROTOCOL`:
  - `PISTORM_PROTOCOL=new` (default): reg-select protocol (`PI_A[1:0]` on GPIO2/3, `REG_*` in `rtl/pistorm.v`)
  - `PISTORM_PROTOCOL=old` (aka `sa3`/`gpio_old`): SA0/SA1/SA2 protocol from `gpio/gpio_old.c` and the Rev B schematic (`docs/RPI5_HEADER_MAP.md`)
- `ps_dump_protocol_state()` now prints `proto=regsel` vs `proto=sa3` and uses protocol-appropriate names:
  - `regsel`: `txn/ipl0/a0/a1/reset/rd/wr`
  - `sa3`: `busy/irq/sa2/sa1/sa0/soe/swe`
- When a CPLD transaction times out, the emulator now dumps GPIO state *before and after* it releases OUT/OE, so we don’t lose the “what were we driving” context.

## New: RP1 SYS_RIO OE/OUT persistence fix (important)
- On Pi 5 (RP1), the SYS_RIO `OUT`/`OE` registers are global to the SoC, not per-process.
  - A crashed emulator run can leave pins actively driven across subsequent runs (we saw GPIO5 stuck output-enabled).
- Fix:
  - `ps_setup_protocol()` now starts by releasing **all GPIO0..GPIO23** (`OE=0`, `OUT=0`) before configuring the selected protocol.
  - `rp1_txn_timeout_fatal()` now releases **all GPIO0..GPIO23** on timeout before exiting.
- Symptom that this addresses:
  - `ps_dump_protocol_state()` showing `gpio5(oetopad=1)` in `proto=regsel` runs (should normally be `0` in regsel mode).

## New: setcheckclock can run a bus probe
- `setcheckclock` now supports:
  - `--bus-probe` (runs `./emulator --bus-probe` after enabling the GPCLK overlay)
  - `--protocol <new|old>` (passed as `PISTORM_PROTOCOL=...` for the bus probe)
  - `--timeout-us <us>` (passed as `PISTORM_TXN_TIMEOUT_US=...` for the bus probe)
- Example:
  - `./setcheckclock 2000000 --no-emulator-probe --bus-probe --timeout-us 200000`

## Reference docs
- PiStorm Rev B schematic uploaded: `Pistorm_Rev_B_schematic.pdf`
- Header net mapping (authoritative for this effort): `docs/RPI5_HEADER_MAP.md`
