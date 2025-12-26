# Musashi vs PiStorm 68k Core: Investigation Notes

Scope
- Compared `/home/smalley/pistorm` (PiStorm fork) to `/home/smalley/Musashi` (upstream) and `/home/smalley/captain-amygdala/pistorm`.
- Focused on 68k core sources: `m68kconf.h`, `m68k.h`, `m68kcpu.c`, `m68kcpu.h`, `m68kdasm.c`, `m68kfpu.c`, `m68kmmu.h`, `m68k_in.c`, `m68kmake.c`.
- Goal is to identify “missing” 68040/PMMU behaviors, while preserving PiStorm host hooks and stability.

Executive Summary
- PiStorm and the captain‑amygdala fork are identical in 68k core code except for a single `inline` vs `static inline` in `m68kcpu.h` (instruction cache clear helper).
- Upstream Musashi diverges heavily in configuration defaults and PMMU implementation structure, and lacks PiStorm‑specific hooks (GPIO, address mapping ranges, custom fast memory paths).
- We cannot assert a single “missing” upstream PMMU feature will fix 68040 issues in PiStorm. The PMMU is disabled by default in PiStorm (`M68K_EMULATE_PMMU OPT_OFF`), and the host hooks are intertwined with memory access paths.

PiStorm vs captain‑amygdala
- Only difference: `m68kcpu.h` `m68ki_ic_clear` is `static inline` in PiStorm vs `inline` in captain‑amygdala. This affects `-Os` link behavior.

PiStorm vs upstream Musashi: Key Differences

Configuration defaults (`m68kconf.h`)
- PiStorm:
  - `M68K_EMULATE_PMMU OPT_OFF`
  - `M68K_EMULATE_INT_ACK OPT_SPECIFY_HANDLER` (PiStorm hooks)
  - `M68K_EMULATE_RESET OPT_SPECIFY_HANDLER` (PiStorm hooks)
  - `M68K_EMULATE_PREFETCH OPT_ON`
- Musashi:
  - `M68K_EMULATE_PMMU OPT_ON`
  - `M68K_EMULATE_INT_ACK OPT_OFF`
  - `M68K_EMULATE_RESET OPT_OFF`
  - `M68K_EMULATE_PREFETCH OPT_OFF`

PiStorm host hooks (`m68kcpu.h`, `m68kcpu.c`)
- PiStorm adds:
  - `gpio/ps_protocol.h` include
  - “fast mapping” ranges (`read_ranges`, `write_ranges`, mapped ROM/RAM pointers)
  - translation cache helpers and mapping arrays
- Musashi lacks these hooks and expects generic memory callbacks.

PMMU implementation (`m68kmmu.h`)
- PiStorm:
  - Large, MAME‑derived PMMU implementation with ATC, bus error tracking, and translation tables.
  - Designed to integrate with PiStorm’s memory and exception tracking.
- Musashi:
  - Much smaller PMMU implementation centered around `pmmu_translate_addr`.
  - Different structure; fewer helpers and no MAME‑style ATC/bus error apparatus.

Opcode generation and decode (`m68k_in.c`, `m68kmake.c`)
- Differs significantly from upstream; PiStorm uses a version aligned with its fork.

Disassembler and FPU (`m68kdasm.c`, `m68kfpu.c`)
- Substantial diffs vs upstream. PiStorm includes changes aligned with its host hooks and layout.

What is “missing” for 68040/PMMU?
- We cannot state a single missing feature will 100% fix 68040 PMMU issues.
- Likely contributors:
  - PMMU disabled in PiStorm (`M68K_EMULATE_PMMU OPT_OFF`) despite 68040 PMMU expectations.
  - PiStorm’s PMMU implementation is present but may be incomplete for 68040 edge cases.
  - Differences in memory access paths (fast mapping, cache, FC behavior) can change exception timing and PMMU interactions.

Risk Notes for Merging Upstream Musashi
- Upstream lacks PiStorm‑specific hooks that the emulator depends on.
- Dropping upstream files directly will remove fast‑path memory mappings, GPIO signaling, and PiStorm’s custom address translation caches.
- Any PMMU merge must re‑apply PiStorm’s hooks and verify exception timing and cache behavior.

Practical Path Forward (safe)
1. Extract and diff PMMU‑related logic in upstream Musashi vs PiStorm’s `m68kmmu.h` and PMMU call sites in `m68kcpu.c`.
2. Identify self‑contained PMMU fixes in upstream (if any), and re‑apply them behind a build flag.
3. Keep PiStorm’s memory hook layer intact, and validate with 68040 + PiSCSI boot tests.

Related Long‑Term Direction (context)
- QEMU JIT for 68040 (and potential AArch64 bridgeboard concept) remains a separate, future path.
- This investigation is strictly about interpreter/PMMU correctness and stability; any JIT integration should be planned as a separate track.
