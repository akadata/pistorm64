# Pi 5 (Alpine) debugging notes

## Safe first checks

- `sudo ./emulator --gpio-probe`

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

