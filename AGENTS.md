# AGENTS.md

## Current focus: UAE JIT backend bring-up (AArch64)

We are integrating the UAE/JIT core (from Z3660-derived UAE tree) into `pistorm64` and wiring it into the existing emulator. This is **not** a full UAE emulator port; it is a CPU backend using PiStorm memory + bus code.

### What exists now
- **UAE JIT library** builds into `build/uae/libuae.a`.
- **Bridge layer:** `src/uae/pistorm_uae_bridge.cc` and `.h` provide the UAE → PiStorm glue.
- **Stubs:** `src/uae/pistorm_uae_stubs.cc` fills in missing symbols (dmmy_bank, do_get_mem_*, fpuop_* stubs, etc.).
- **Memory address translation:** `pistorm_xlate()` uses `cfg->map_*` to map emulated address to host buffers (ROM/RAM).
- **OVL handling:** JIT resets now force ROM overlay at 0 by setting `map_mirror` for ROM mappings at runtime (see `pistorm_force_rom_overlay()`).

### Key files
- `src/uae/pistorm_uae_bridge.cc`
  - Implements `pistorm_*` addrbank (lget/wget/bget/lput/wput/bput).
  - Hooks `cpu_set_fc()` using `regs.sfc/dfc`.
  - `uae_pistorm_init/run/set_irq/pulse_reset` exported for emulator.
  - **OVL fix:** `pistorm_force_rom_overlay()` sets ROM map_mirror to 0 when JIT enabled.
  - Applies reset vectors from ROM and sets `m68k_pc_indirect = 1` for indirect fetch.

- `src/uae/pistorm_uae_stubs.cc`
  - Minimal UAE support glue (dummy addrbank, do_get_mem_*, fpuop_* no-ops, etc.).

- `src/uae/include/memory.h`
  - `get_real_address()` uses `memory_get_real_address()` when `USE_UAE_JIT` defined.

- **Makefile**
  - `USE_UAE_JIT=1` builds `libuae.a` and links it into `emulator`.
  - Adds `pistorm_uae_bridge.o` and `pistorm_uae_stubs.o`.

### Build commands
- Build UAE JIT backend and emulator:
  ```bash
  make USE_UAE_JIT=1 uae-jit
  ```
- Run:
  ```bash
  PISTORM_ENABLE_QUEUE=0 ./emulator --jit
  ```

### Current runtime issue
Before the overlay fix, UAE reset was reading vectors from **chipram at 0** (because default.cfg sets `ovl=0`), causing:
```
Read PC from address 4 : 0x00000000
PC map: unmapped
```
Now `pistorm_force_rom_overlay()` forces ROM mirror at 0 during JIT init/reset, so UAE reset should read Kickstart vectors correctly:
```
Read PC from address 4 : 0x00F800D2
```
If still 0, the overlay is still not forced early enough and must be applied before `m68k_reset_newcpu()` or in UAE `get_long()`.

### Important config notes
- **default.cfg** currently maps Kickstart with `ovl=0`:
  ```
  map type=rom address=0xF80000 size=0x80000 file=... ovl=0 id=kickstart
  ```
- For JIT bring-up, ROM must be visible at 0 during reset. The bridge now forces this at runtime.
- Chip RAM should be mapped at `0x00000000` (1 MB) for post‑OVL execution:
  ```
  map type=ram address=0x00000000 size=0x100000 id=chipram
  ```

### CPU models
- 68060 is **not** accepted by config parser and falls back to 68000.
- JIT currently tested with 68020/68030/68040.

### Next steps
1. Verify new overlay fix works (see `Read PC from address 4` log).
2. If PC still drops to 0, move overlay forcing earlier (or patch UAE get_long to check `ovl`/ROM mirror).
3. Once reset vectors are stable, re-enable devices incrementally (Z2/Z3, RTG, PISCSI, A314).

