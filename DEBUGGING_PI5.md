# Pi 5 debugging notes (Alpine + Raspberry Pi OS)

## Safe first checks

- `sudo ./emulator --gpio-probe`
- `sudo ./emulator --gpclk-probe` (attempts to enable GPCLK0 and prints `clk_transitions`; no CPLD transactions)
- `sudo env PISTORM_TXN_TIMEOUT_US=200000 ./emulator --bus-probe` (does a bounded status + reset-vector read, then exits)
- If you suspect the hardware uses the legacy SA0/SA1/SA2 protocol (Rev B schematic): add `PISTORM_PROTOCOL=old` to the `--bus-probe` run.

## Run under gdb (logs to `gdb.log`)

- `sudo gdb -q -x gdb_pistorm_init.gdb --args ./emulator --config basic.cfg`
- Pi 5 bring-up helper (sets env + useful breakpoints): `sudo gdb -q -x gdb_pi5_run.gdb --args ./emulator --config basic.cfg`
- Convenience wrapper: `./tools/pi5_gdb.sh --config basic.cfg`
- Build with debug symbols first:
  - `make clean`
  - `make PLATFORM=PI5_DEBIAN_64BIT WITH_RAYLIB=0 WITH_ALSA=0 DEBUG=1 -j4`
- At the `(gdb)` prompt:
  - `run`
  - If it hangs: press `Ctrl-C`
  - `bt`
  - `thread apply all bt`
  - `info threads`
  - Optional (after `start`): `pistorm_watch_vectors` (adds a conditional breakpoint for reset-vector fetches at 0x0/0x4)
  - Don’t run `run <something>` unless you mean to change argv; `run foo` replaces `--config basic.cfg` and will make the emulator try `default.cfg`.

## Transaction timeout

- Set `PISTORM_TXN_TIMEOUT_US` (microseconds) to control how long we wait for `PIN_TXN_IN_PROGRESS` to clear before dumping GPIO state and exiting.
  - Example: `sudo env PISTORM_TXN_TIMEOUT_US=2000000 ./emulator --config basic.cfg`

## GPCLK0 on GPIO4 (PiStorm clock)

- The CPLD design expects a clock on GPIO4 (`PI_CLK` in `rtl/pistorm.v`).
- The Pi 5 RP1 path tries to enable GPCLK0 via `/dev/mem` by default; you can control it with:
  - Disable: `PISTORM_ENABLE_GPCLK=0`
  - Source select: `PISTORM_GPCLK_SRC` (default `5`)
  - Integer divisor: `PISTORM_GPCLK_DIV_INT` (default `6`)
  - Extra log: `PISTORM_GPCLK_DEBUG=1`
  - Clock-manager base overrides (Pi 5): `PISTORM_GPCLK_CM_CHILD` (e.g. `0x7c700000`) or `PISTORM_GPCLK_CM_PHYS` (e.g. `0x107c700000`)
  - By default, the base is derived from the DT symbol `dvp` (`/sys/firmware/devicetree/base/__symbols__/dvp`).

## Recommended Pi 5 clock bring-up (GPCLK0 via DT overlay + clk framework)

If your kernel blocks enabling GPCLK0 from userspace (common on Pi 5), use the repo helper:

- `./tools/pi5_gpclk0_enable.sh` (optional low-rate test: `./tools/pi5_gpclk0_enable.sh --freq 1000000`)
- Note: `./tools/pi5_gpclk0_enable.sh` refuses `--freq > 50000000` unless you add `--force-high` (high-rate requests have caused hard crashes on this setup).
- Verify:
  - `sudo pinctrl get 4` shows `GPIO4 = GPCLK0`
  - `sudo ./emulator --gpclk-probe` shows `clk_transitions>0`

Then run the emulator with GPCLK configuration disabled (so it doesn’t fight the kernel clock setup):

- `sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 PISTORM_TXN_TIMEOUT_US=200000 ./emulator --config basic.cfg`

For a quick bounded “is the bus alive” check after setting the clock:

- `./setcheckclock 2000000 --no-emulator-probe --bus-probe --timeout-us 200000`

Note: `gpiomon`/libgpiod edge monitoring on GPIO4 will request the line as GPIO input and can override the GPCLK pinmux. Don’t run `gpiomon` on GPIO4 while expecting GPCLK output; use debugfs (`/sys/kernel/debug/clk/clk_gp0/*`) or a scope/logic analyzer.

## Alternative clock source (kernel overlay / PIO)

If your kernel blocks enabling GPCLK0 from userspace, you can provide a clock on GPIO4 via a kernel overlay (Pi 5), then tell PiStorm to not override the GPIO4 function:

- Load overlay (runtime): `sudo dtoverlay pwm-pio,gpio=4`
- Configure PWM (example, start low like 1MHz then increase):
  - Find the pwmchip under `/sys/class/pwm/`
  - Export and configure `period`/`duty_cycle` in nanoseconds
- Run PiStorm without GPCLK setup and without touching GPIO4 mux:
  - `sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 ./emulator --gpclk-probe`
  - If you want PiStorm to explicitly mux GPIO4 to RP1 PIO, set `PISTORM_RP1_CLK_FUNCSEL=7` (PIO is typically a7 on RP1).
 - If you use a slow PWM frequency (e.g. 10Hz) and want the probe to detect it, set `PISTORM_CLK_SAMPLE_US=1000` to sample GPIO4 over time.

Note: Some high frequencies may fail with `I/O error` via sysfs PWM on Pi 5; GPCLK0 is the preferred source for a high-speed PiStorm clock.
