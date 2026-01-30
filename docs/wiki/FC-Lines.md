# FC Lines (WIP)

This document describes the current Function Code (FC) line work. **This is
work-in-progress and needs testing.** Please try it and report results.

## Status

- FC shadow logging is available in the emulator.
- CPLD support is **stubbed**; it does not yet drive hardware.
- RTL exists under `rtl/fc_amiga/fc_amiga/` and is not enabled by default.

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

## What we need from testers

- Confirm FC logging does not affect stability.
- Report any regressions or unexpected behavior.
- If you build the RTL bitstream, report whether FC pins behave as expected.

