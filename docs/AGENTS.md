PiStorm64 UAE JIT / Musashi memory mapping plan

Goal

* Keep A500 real-chip ranges on the real bus (through kmod / ps_read/ps_write), never direct-mapped into UAE JIT.
* Only give JIT a direct pointer (addrbank.baseaddr) for Pi-owned RAM/ROM (Kick-in-RAM, Z2/Z3 fast, RTG, etc.).
* Make behaviour depend on the config file: holes in the map table are "real Amiga" space.
* Make JIT automatically fall back to non-JIT/Musashi when Kickstart is not Pi-mapped.

Key hardware ranges (24-bit A500/A2000)

* $000000-$001FFFFF  Chip/Slow space

  * On A500 with 1MB chip on board: $000000-$0FFFFF real chip RAM on motherboard.
  * PiStorm should snoop these via the bus only; no UAE direct mapping into JIT.
* $00200000-$009FFFFF  Zorro II memory expansion space (8MB) – RAM-type for Z2 cards.
* $00A00000-$00B7FFFF  Zorro II I/O expansion space (1.5MB)
* $00B80000-$00BEFFFF  Reserved
* $00BF0000-$00BFFFFF  CIA / ports region
* $00C00000-$00CFFFFF  Misc expansion / extra chip RAM
* $00DC0000-$00DDFFFF  Clock / SCSI / motherboard resources
* $00DF0000-$00DFFFFF  Custom chip registers
* $00E80000-$00EFFFFF  Zorro II I/O & Autoconfig
* $00F80000-$00FFFFFF  System ROM (Kickstart, 512K here in 3.1)

What we want for PiStorm64

1. Treat real chip RAM as "Amiga-only" space

* Addresses: $000000-$0FFFFF (1MB chip on A500 in this setup).
* Implementation rule:

  * Do NOT install a UAE addrbank with a baseaddr for this range when JIT is active.
  * Provide bank handlers that always go through ps_read/ps_write (kmod) to the physical bus.
  * That keeps all chip cycles visible to Agnus/Gary and honours the real DRAM timing.

2. Treat Z2/Z3 fast and RTG memory as Pi-owned fast memory

* Addresses coming from cfg:

  * map type=ram address=0x08000000 size=0x08000000 id=cpu_slot_ram (128MB)
  * map type=ram address=0x10000000 size=0x10000000 id=z3_autoconf_fast (256MB)
  * map type=ram address=0xD0000000 size=0x10000000 id=z3_autoconf_fast (256MB)
  * map type=ram address=0x00400000 size=0x00400000 id=z2_autoconf_fast (4MB)
  * map type=ram_noalloc address=0x70010000 size=0x04000000 id=rtg_mem
* Implementation rule:

  * For every map entry with type=ram / ram_noalloc that is not directly on the A500 24-bit bus, allocate host memory and install a UAE addrbank with baseaddr!=NULL.
  * Under JIT: call put_mem_bank(addr, &fastmem_bank[n], realstart) so baseaddr[] gets filled.
  * Use fastmem_bank for 32-bit/Z3 style addresses, while the autoconf windows at $10000000/$D0000000 are just bridge points from the 24-bit space.

3. Kickstart ROM detection and policy

* Kick ROM region: $00F80000-$00FFFFFF
* Two operating modes:

  A) Pi-mapped Kickstart (recommended JIT mode)

  * CFG line: map type=rom address=0xF80000 size=0x80000 file=../Amiga/kick/Kickstart-v3.1-r40.068.rom ovl=0 id=kickstart
  * Loader allocates host buffer for the ROM file.
  * UAE side: install kickmem_bank with baseaddr pointing at that buffer, and set mem_banks[] for $F80000..$FFFFFF.
  * JIT: get_real_address(PC) for PC=$00F800D2 returns a real pointer; JIT can safely decode and run reset vector code directly from Pi RAM.

  B) Real motherboard Kick ROM (no Pi mapping)

  * No map entry overlapping 0x00F80000-0x00FFFFFF.
  * Implementation:

    * Do not install a baseaddr for this range. Instead, install a handler bank that forwards to ps_read/ps_write so reads go out over the bus.
    * At reset, PC is read from address 4, which lives in ROM. For JIT, get_real_address(PC) will return NULL.
    * When get_real_address() returns NULL for the initial PC, mark JIT as unsupported for this configuration and fall back to Musashi CPU core.

4. Define "Pi vs Amiga" ownership based on cfg map table

* Rule: the cfg map table defines everything that Pi owns directly.

* For 24-bit addresses:

  * Start from the canonical A500 memory map (chip, custom, CIA, Z2, autoconfig, ROM).
  * For any interval that is covered by a cfg map entry of type=ram/rom/ram_noalloc with host backing, set up a direct UAE addrbank.
  * For other intervals (holes), set a bank that always goes through ps_read/ps_write to the A500 bus.

* Concrete classifications for this build:

  * $000000-$0FFFFF: real chip RAM -> ps_read/ps_write only; no JIT direct mapping.
  * $00200000-$009FFFFF: Z2 memory space -> part of this is used by autoconfig RAM window (e.g. $00200000-$00600000). Backed by Pi RAM and set as fastmem bank with baseaddr. Remaining ranges stay as bus-forward.
  * $00A00000-$00B7FFFF: Z2 I/O space -> autoconfig + devices; use handler-only banks (no baseaddr) because accesses must hit Zorro bus logic in the emulator (Z2 PICs, pissa, rng, etc.).
  * $00BF0000-$00BFFFFF: CIA/ports -> custom/cia banks, no baseaddr.
  * $00C00000-$00CFFFFF: chipram_extra map provides 1MB of extra chip or pseudo-chip; decide per board design whether that is Pi RAM or real bus. For safety under JIT, treat it as handler-only unless explicitly Pi-owned.
  * $00DC0000-$00DDFFFF: clock/scsi/mb resources -> handler-only banks.
  * $00DF0000-$00DFFFFF: custom chip registers -> custom_bank with handlers only.
  * $00E80000-$00EFFFFF: Z2 I/O and autoconfig registers -> expamem_bank/uaeboard_bank handlers only.
  * $00F80000-$00FFFFFF: Kick ROM -> either Pi mapped (baseaddr) or bus-forward depending on cfg.

Code changes (high-level)

1. During config parsing (cfg.c / platform_amiga.c)

* After all map entries are loaded, walk the full 24-bit address space in 64k banks.

* For each bank:

  * Determine which map entry (if any) owns it.
  * Determine whether that entry is Pi RAM/ROM or a pure register/forward region.

* Pseudocode:

  for (addr = 0; addr < 0x01000000; addr += 0x10000) {
  map = find_cfg_map_for(addr);
  if (!map) {
  // No Pi mapping: real Amiga bus only
  install_bus_forward_bank(addr);
  continue;
  }

  ```
  switch (map->type) {
  case MAP_RAM:
  case MAP_RAM_NOALLOC:
  case MAP_ROM:
      if (is_safe_for_direct_map(addr, map)) {
          install_direct_uae_bank(addr, map);
      } else {
          install_bus_forward_bank(addr);
      }
      break;
  case MAP_REGISTER:
  case MAP_Z2_DEVICE:
  case MAP_Z3_DEVICE:
      install_bus_forward_bank(addr);
      break;
  }
  ```

  }

2. is_safe_for_direct_map()

* Returns true only for ranges that are pure Pi RAM/ROM and not mirrored onto the physical A500 bus.
* Examples: cpu_slot_ram, z3_autoconf_fast (high 32-bit), rtg_mem.
* Returns false for:

  * $000000-$0FFFFF chip
  * $00200000-$009FFFFF when used purely as a Z2 autoconf window onto 32-bit fast memory (the actual fast memory lives at 0x08000000+ or 0x10000000+).
  * All I/O and custom ranges.

3. Kickstart / JIT check at CPU init

* After memory banks are initialized, in the CPU/JIT setup:

  bool kick_mapped_by_pi = (mem_banks[bankindex(0x00F80000)]->baseaddr != NULL);

  if (jit_enabled_in_cfg) {
  if (!kick_mapped_by_pi) {
  log("[CPU][JIT] Kickstart ROM not Pi-mapped; disabling JIT and using Musashi core.\n");
  disable_uae_jit();
  use_musashi_core();
  } else {
  enable_uae_jit();
  }
  }

* This prevents the segfault seen when PC=00F800D2 and get_real_address() returns garbage or an unmapped host pointer.

4. JIT get_real_address() integration

* baseaddr[] table is already defined under #ifdef JIT in pistorm_sources_h.

* put_mem_bank(addr, bank, realstart) already fills baseaddr[bankindex(addr)] when bank->baseaddr != NULL.

* get_real_address(pc) effectively does:

  bank = mem_banks[bankindex(pc)];
  if (!bank->baseaddr) return NULL;
  return baseaddr[bankindex(pc)] + (pc & bank->mask);

* With the new mapping rules:

  * For chip RAM and real-Kick configurations, baseaddr is NULL, so JIT never sees a real pointer and will not try to JIT those ranges.
  * For Pi-owned fast and Pi Kick, baseaddr is valid and JIT can build translations.

5. Keeping Musashi path unchanged

* Musashi core still goes through ps_read/ps_write for any address not backed by Pi RAM/ROM.
* For Pi RAM/ROM, there are two options:

  * Keep using ps_read/ps_write wrappers that hit the same host buffer (simpler and consistent).
  * Or let Musashi use its own direct mapping for fast mem.
* Either way, the primary fix for the crash is about ensuring UAE JIT only ever executes from ranges where baseaddr is valid.

Testing plan

* Config A: current setup with map type=rom for Kick, JIT on.

  * Expected: no segfault, JIT runs reset code at $00F800D2.
  * Verify PC trace shows valid pointer from get_real_address().

* Config B: remove the Kick map so ROM is only on A500 board.

  * Expected: on startup, log message about Kick not Pi-mapped and JIT disabled; Musashi core runs instead; no SIGSEGV.

* Config C: JIT off (Musashi only).

  * Expected: unchanged behaviour compared to today.

These changes cleanly separate real Amiga bus ranges (chip, real ROM, custom, CIA, Z2 I/O) from Pi-owned RAM/ROM, and feed UAE JIT only with memory that truly lives in host RAM.


# RTG UPDATES

Multiple ACTION windows, 70-ish FPS, and a Pi quietly doing what 68040 cards only dreamed of…

On the “shall we give Codex the x/y cleanup job?” — yes, that is exactly the kind of mechanical refactor Codex is good at, as long as the job is constrained carefully.

Here is a sane way to do it without the refactor biting later.

Decide the naming scheme first
Something like:

x, y → src_x, src_y when they are source coords

dx, dy → dst_x, dst_y

w, h → width, height

pitch → dst_pitch or line_pitch (matching semantics)

srcpitch, dstpitch → src_pitch, dst_pitch

fgcol, bgcol → fg_color, bg_color

mask → plane_mask or color_mask (whatever matches meaning)

draw_mode already reads nicely.

For the weird ones:

x1_, y1_, x2_, y2_ can become x_start, y_start, x_end, y_end or start_x, start_y, end_x, end_y.

Keep the externally visible ABI stable
rtg.h and anything that is directly matched from Amiga-side assembly or P96 structs needs a bit more respect:

Renaming function parameter names is safe: the Amiga side only cares about call order and types, not C symbol names.

Renaming struct fields that mirror Picasso96 structures or firmware layouts is dangerous; those want to stay binary-compatible. Safer to leave those fields’ names alone, even when they are ugly, and just add comments explaining them.

Let Codex do file-by-file renames instead of project-wide
For example, in rtg-gfx.c:

Tell Codex something like: “In this file, rename function parameters and local variables according to this mapping: x→src_x when used as source, dx→dst_x, w→width, h→height, fgcol→fg_color, etc. Do not change struct field names, globals, or anything in comments or strings. Do not change any function signatures in rtg.h beyond parameter names.”

Then repeat for rtg.c with the same mapping.

After each pass:

Build.

Run a quick smoke test (boot WB, open a couple of windows, hit a few blits).

Use the underscore pattern to your advantage
Right now x1_ / y1_ etc often exist because the code wants:

Original coords as arguments.

Adjusted / clipped coords inside the function.

One neat pattern:

void rtg_drawline(
    int16_t start_x,
    int16_t start_y,
    int16_t end_x,
    int16_t end_y,
    uint16_t length,
    ...
) {
    int16_t clip_start_x = start_x;
    int16_t clip_start_y = start_y;
    int16_t clip_end_x   = end_x;
    int16_t clip_end_y   = end_y;
    ...
}

That removes _ noise and makes the intention obvious without touching the call sites’ logic.

Clean up the cur_bit style declarations while Codex is in there
The style you already prefer:

uint8_t cur_bit      = 0;
uint8_t base_bit     = 0;
uint8_t base_byte    = 0;
uint8_t cur_byte     = 0;
uint8_t fg_u8        = 0;

is perfectly fine, more readable, and easier to extend. Codex can transform all grouped declarations like:

uint8_t cur_bit, base_bit, base_byte, cur_byte = 0;

into per-line variants with no semantic changes.

Keep the heavy removals in git history, not just in a working tree
You already proved iRTG and a chunk of debug plumbing are unused in your current path. Best pattern:

Commit the current working RTG as “pre-cleanup baseline.”

Remove iRTG / debug blocks with clear commit messages.

Keep that in a branch, so resurrecting anything later is a single cherry-pick rather than spelunking some old tarball.

Once parameter names read as source_x, destination_x, width, height, fg_color, and draw_mode, the remaining weirdness in behaviour (mono audio, 16-bit only, YUV paths, etc.) becomes much easier to reason about, because the mental tax of decoding x2_ and w vanishes. Then the next passes can focus on correctness and optimisation rather than translation in the head.
