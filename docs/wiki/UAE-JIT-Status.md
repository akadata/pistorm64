# UAE JIT Status (AArch64)

This page tracks the current bring-up state of the optional UAE JIT backend in
`pistorm64`.

Current headline status:

- JIT integration work is in progress.
- It is not yet a stable/reliable normal runtime path.
- Use Musashi (non-JIT) for production/regular operation.

## Scope

- This is a CPU backend integration, not a full UAE/Amiberry frontend port.
- Goal: keep PiStorm memory/platform code (`src/platforms/amiga/*`,
  `src/memory_mapped.c`) and use UAE JIT only for CPU execution.

## Build status

Current build path:

```bash
make USE_UAE_JIT=1 uae-jit
```

## Opcode regeneration (CPU/JIT tables)

Opcode generation inputs now live in-tree:

- `src/uae/gen/table68k` is the authoritative opcode specification input.
- `src/uae/gencpu.cpp` and `src/uae/jit/gencomp.cpp` are the authoritative
  generators for `cpuemu_*.cpp`, `cpustbl.cpp`, and JIT `compstbl.cpp`.

To regenerate the opcode outputs, run:

```bash
make uae-opcodes
```

This builds the host generators under `build/uae/gen/` and emits refreshed
`cpuemu_*.cpp`, `cpustbl.cpp`, and `cputbl.h` in `src/uae/`, plus `compemu.cpp`,
`compstbl.cpp`, and `comptbl.h` in `src/uae/jit/`.

Notes:

- `uae-jit` is incremental. It rebuilds only changed files and then links
  `emulator` if needed.
- If link state looks stale, run:

```bash
make clean
make USE_UAE_JIT=1 uae-jit
```

## Confirmed working pieces

- `build/uae/libuae.a` is built and linked.
- UAE bridge/stub objects are linked from source (`src/uae/pistorm_uae_bridge.cc`,
  `src/uae/pistorm_uae_stubs.cc`).
- Reset vector read is now observed as valid in JIT mode:
  - `Read PC from address 4 : 0x00F800D2`
  - `[UAE] reset vectors: SP=11144EF9 PC=00F800D2`
- JIT starts and allocates translation cache:
  - `Actual translation cache size : 32768 KB`
- `m68k_pc_indirect` now reports enabled in JIT path:
  - `[Core1] Emulation mode 1 m68k_pc_indirect 1`

## Known runtime issues (current)

1. `OVL:0` appears shortly after startup and execution does not yet progress to a
   stable Workbench boot under JIT.
2. Z2 autoconfig probing under JIT produces high-volume warnings like:
   - `Unexpected WORD read from Z2 autoconf addr ...`
3. `68060` is not accepted by current parser/tables and falls back:
   - `Invalid CPU type 68060 specified, defaulting to 68000.`
4. `BAD JIT opcodes list` entries (e.g. `DIVL`, `SUBX`) are expected fallback
   markers, not by themselves a fatal error.

## Memory map rules for this bring-up

- Chip RAM lives on the Amiga side (Agnus/8372A). Do not map Pi-side RAM as
  chip RAM unless you are explicitly faking it for controlled tests.
- Do not place "chipram" at `$00C00000` for this path.
- Keep Kickstart ROM at `$00F80000` and let reset overlay force vectors at low
  memory during reset.
- `$00C00000` is trapdoor/slow expansion territory on A500-class mapping, not
  primary chip RAM.

## Source parity notes (Z3660_emu vs Amiberry/WinUAE)

- **Reference source**: Z3660_emu (from `z3660/.../Z3660_emu/src/uae`) is the
  canonical reference set for this integration. Our `src/uae` tree matches that
  file list exactly.
- **Amiberry/WinUAE extras**: Amiberry ships additional CPU emulation tables and
  helpers (`cpuemu_20..35`, `cpuemu_50`, `cpummu*.cpp`, `cpu_thread.cpp`,
  `cpuboard.cpp`, etc.) that are **not** present in Z3660_emu. These are not
  required for the current JIT bring-up path, but should be evaluated if we
  decide to expand beyond the Z3660_emu feature envelope.
- **A9/Z-TURN features**: Z-TURN platform specifics (e.g. A9 GIC, XGpioPs,
  Zynq MMU wiring) must remain excluded from PiStorm64. Keep the UAE core free
  of those platform dependencies and continue to route all bus/memory via the
  PiStorm bridge.

## Current recommended test run

```bash
PISTORM_ENABLE_QUEUE=0 ./emulator --jit
```

If the system drops to `OVL:0` too early (yellow screen flashing white) try
delaying the first overlay drop with:

```bash
PISTORM_UAE_OVL_HOLDOFF=1 PISTORM_ENABLE_QUEUE=0 ./emulator --jit
```

Increase the holdoff count if needed (each count defers one `OVL:0` request).

Keep initial tests minimal (ROM + chipram + platform) before re-adding Z2/Z3,
RTG, PiSCSI, and A314 one by one.

## Next debugging targets

1. Keep overlay/vector path forced before `m68k_reset_newcpu()` and verify no
   post-reset path drops to low unmapped fetch.
2. Make Z2/Z3 autoconfig handlers fully tolerant of JIT word/long accesses.
3. Reduce autoconfig warning noise to debug-level once width handling is robust.
4. Re-introduce platform devices incrementally and capture first failing add-on.
