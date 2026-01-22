Bridgeboard Janus reference notes
=================================

Scope and intent
----------------
These notes summarize legacy Bridgeboard (A2088/A2286/A2386) Janus
behavior and packaging. They are reference-only and do not include
any proprietary code or binaries.

Key points
----------
- janus.library is a Bridgeboard IPC library used for shared memory,
  devices, and I/O coordination between AmigaOS and the PC side.
- janus.library is typically installed in SYS:Expansion (not Libs:).
- Janus 2.1 is commonly cited as stable for A2286/A2386.
- Data cache on 68020/030/040 can break shared memory; disable or
  ensure shared regions are non-cacheable.

Local artifacts (reference only)
--------------------------------
- data/a314-shared/janus/bridge/janus.library
- data/a314-shared/janus/devs/JDisk.device
- data/a314-shared/janus/bridge/pc.boot

These are proprietary binaries from Commodore/Bridgeboard media and
must not be redistributed in-source. Keep them local for testing.

pc.boot patching
---------------
There is an MIT-licensed tool that patches pc.boot option ROM logic to
avoid clobbering XTIDE INT13/INT19 vectors:
- janus-misc/bridgeboard-pc-boot-patcher-0.2.0

This is separate from PiStorm64 IPC work but is useful for legacy
Bridgeboard usage and diagnostics.
