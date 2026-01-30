# Known Issues

## SysInfo 4.4 benchmark instability on accelerated emulation

**Status:** Known / not actionable (SysInfo issue under fast emulation)

**Summary:** Running the SysInfo 4.4 benchmark on PiStorm64 can lead to
reboots, filesystem loss, or a fallback to the AmigaDOS 1> prompt when
the system is configured to run significantly faster than early classic
Amigas. This behavior matches SysInfo’s own warning that benchmark results
are not reliable on fast emulators, and it is also reported on WinUAE when
“Fast as possible”/JIT is enabled.

**Notes:**
- SysInfo 4.4 reports “results are currently not verified” on 68060 and
  “useless in emulators set up to emulate faster than early classic Amigas.”
- WinUAE users report crashes after the speed test with fast/JIT settings.
- PiStorm64 logs show intermittent invalid filesystem list pointers during
  PiSCSI ROM FS scanning when SysInfo benchmarks are running, consistent
  with a benchmark-driven emulator stress edge case.

