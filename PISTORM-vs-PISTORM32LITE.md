# PiStorm vs PiStorm32-Lite: 68k Core Comparison

Scope
- Compared m68k* sources in `/home/smalley/pistorm` (branch `akadata/checkm68k`) against `/home/smalley/pistorm32-lite` (branch `pistorm32-lite`).
- Files checked: `m68kconf.h`, `m68k.h`, `m68kcpu.c`, `m68kcpu.h`, `m68kdasm.c`, `m68kfpu.c`, `m68kmmu.h`, `m68k_in.c`, `m68kmake.c`.

Valid CPU Types (both repos)
- `M68K_CPU_TYPE_68000`
- `M68K_CPU_TYPE_68010`
- `M68K_CPU_TYPE_68EC020`
- `M68K_CPU_TYPE_68020`
- `M68K_CPU_TYPE_68EC030`
- `M68K_CPU_TYPE_68030`
- `M68K_CPU_TYPE_68EC040`
- `M68K_CPU_TYPE_68LC040`
- `M68K_CPU_TYPE_68040`
- `M68K_CPU_TYPE_SCC68070`

68k Core Differences
- `m68kcpu.h`: `m68ki_ic_clear` is `static inline` in `/home/smalley/pistorm/m68kcpu.h` but plain `inline` in `/home/smalley/pistorm32-lite/m68kcpu.h`. This matters for `-Os` builds (linker undefined reference issue), so the PiStorm variant is safer for size-optimized builds.

Identical 68k Core Files
- `m68kconf.h`
- `m68k.h`
- `m68kcpu.c`
- `m68kdasm.c`
- `m68kfpu.c`
- `m68kmmu.h`
- `m68k_in.c`
- `m68kmake.c`

Other 68k-Related Observations
- `/home/smalley/pistorm` contains generated `m68kops.c`/`m68kops.h` in the tree; `/home/smalley/pistorm32-lite` does not. This is a build artifact difference, not a source divergence.

Notes for 68EC040
- The CPU type is present in both repos and is configured in the core as a 68040 variant without PMMU (in `m68kcpu.c`). The core logic is identical between repos, so any PMMU-related behavioral differences should not come from code divergence between these two trees.

Emulator Entry Point and FPGA Setup
- `emulator.c` in `pistorm32-lite` adds Efinix bitstream loading at startup (`bitstream.bin` or argv[1]) via `ps_efinix_setup()`/`ps_efinix_load()`. This is not present in `pistorm`.
- `pistorm` has CLI logging flags (`--log`, `--log-level/-l`) and logging setup; `pistorm32-lite` does not.
- `pistorm32-lite` changes GPIO/IRQ handling:
  - Defines alternate PIN aliases (`PIN_TXN_IN_PROGRESS`, `PIN_IPL_ZERO`, `PIN_RESET`) and reads IPL directly from GPIO with inverted bits.
  - IRQ counting and status reads differ: `ps_read_status_reg()` is replaced with GPIO bit reads.
  - 16/32-bit reads in `m68k_read_memory_16/32` use `ps_read_16/ps_read_32` without odd-address handling.
- `pistorm32-lite` removes PI-NET and PI-AHI register handling in the platform read/write checks.

Raylib DRM Usage
- `pistorm32-lite` uses `raylib_drm` include path in RTG output (`platforms/amiga/rtg/rtg-output-raylib.c`).
- Makefile in `pistorm32-lite` uses `-I./raylib_drm` and links against `-L./raylib_drm -lraylib`, with `-march=armv8-a -mtune=cortex-a53 -O3`.
- `pistorm` uses `-I./raylib` and selects different raylib directories per platform; it also links against `raylib_drm` by default.

A314 Differences
- `pistorm` uses `memcpy` + `ntohl` to parse payload addresses (alignment/endianness safe).
- `pistorm32-lite` uses direct pointer casts into the payload (alignment/endianness dependent).

Platform/Amiga Differences (high level)
- `pistorm32-lite` does not include PI-NET and PI-AHI in the build or runtime paths.
- `pistorm` has expanded logging in Amiga platform/autoconf/PiSCSI paths; `pistorm32-lite` uses plain `printf` and has less context.
- `pistorm` includes a number of alignment/packing fixes in PiSCSI and RTG structs (`platforms/amiga/piscsi/piscsi.c`, `platforms/amiga/rtg/irtg_structs.h`, `platforms/amiga/rtg/rtg.c`); these are not present in `pistorm32-lite`.
