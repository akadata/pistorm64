# Pi 5 (Alpine) debugging notes

## Safe first checks

- `sudo ./emulator --gpio-probe`
- `sudo ./emulator --gpclk-probe` (attempts to enable GPCLK0 and prints `clk_transitions`; no CPLD transactions)

## Run under gdb (logs to `gdb.log`)

- `sudo gdb -q -x gdb_pistorm_init.gdb --args ./emulator --config basic.cfg`
- At the `(gdb)` prompt:
  - `run`
  - If it hangs: press `Ctrl-C`
  - `bt`
  - `thread apply all bt`
  - `info threads`

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
