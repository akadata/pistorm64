# Atari Platform

The Atari platform is back in-tree and shares the same MC68k core and bus engine
stack as the Amiga. This is no longer “Amiga vs Atari” — it’s one 68k core with
platform-specific front-ends.

## Status

- Platform exists and boots.
- Feature parity with Amiga is in progress (FC/BERR, batching, device bridges).
- Shares the same kernel + CPLD stack and MC68k core; platform glue is what
  differs now.

## Notes

- Expect gaps while the Atari path is re-aligned with the current kernel + CPLD
  stack.

Only the Amiga Makes it Possible.
