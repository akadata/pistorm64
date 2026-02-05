# FC Lines (WIP)

This document describes the current Function Code (FC) line work. **This is
work-in-progress and needs testing.** Please try it and report results.

## Status

- FC shadow logging is available in the emulator.
- CPLD support is **stubbed** in userspace; the CPLD hook is ready but still a no-op.
- RTL for FC/BERR is in `rtl.fc/pistorm_fc.v` (EPM240) and is not enabled by default.

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

This enables CPLD mode and will call the CPLD hook (currently a no-op stub).
It **does not** program or drive the CPLD yet.

## BERR multiplexing on GPIO5

The FC/BERR CPLD bitstream multiplexes BERR onto GPIO5. To sample it:

- Load the kernel module with `berr_reset_input=1` so GPIO5 is configured as input.
- Keep `berr_reset_input=0` for legacy CPLD (GPIO5 is RESET output).

## What we need from testers

- Confirm FC logging does not affect stability.
- Report any regressions or unexpected behavior.
- If you build the RTL bitstream, report whether FC pins behave as expected.
