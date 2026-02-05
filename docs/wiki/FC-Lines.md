# FC Lines (WIP)

This document describes the current Function Code (FC) line work. **This is
work-in-progress and needs testing.** Please try it and report results.

## Status

- FC shadow logging is available in the emulator.
- CPLD support is live; FC/BERR RTL is in `rtl.fc/pistorm_fc.v` (EPM240).

FC is treated as a full **3-bit** value (0-7) end-to-end. The CPLD drives
`M68K_FC[2:0]` and tri-states the lines when `BGACK` is asserted.

## Enable FC (stub)

```
setvar enable_fc stub
```

This records FC transitions and logs them at verbose level without driving
hardware.

## Enable FC (CPLD mode)

```
setvar enable_fc cpld
```

This enables CPLD mode and will drive FC lines via the CPLD when the FC-capable
bitstream is programmed.

## BERR multiplexing on GPIO5

The FC/BERR CPLD bitstream multiplexes BERR onto GPIO5. To sample it:

- Load the kernel module with `berr_reset_input=1` so GPIO5 is configured as input.
- Keep `berr_reset_input=0` for legacy CPLD (GPIO5 is RESET output).

## What we need from testers

- Confirm FC logging does not affect stability.
- Report any regressions or unexpected behavior.
- If you build the RTL bitstream, report whether FC pins behave as expected.
