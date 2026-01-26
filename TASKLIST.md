# TASKLIST: clang warning cleanup (non-Musashi)

Source: `make.nonmusashi.warnings` (generated from `make.log`).

Rules of engagement (matches `AGENTS.md`):
- Do NOT touch anything under `src/musashi/` yet.
- Preserve emulator behaviour. No logic changes unless absolutely necessary to fix undefined behaviour.
- Prefer local, obvious fixes over big refactors.
- After each file or small group: run `make full C=clang` and re-inspect warnings.

Legend:
- [ ] = not started
- [~] = in progress
- [x] = done

---

## Phase 1 — RTG path (highest priority for P96/window issues)

These are the hot path for RTG / display. Fixes here are highest leverage.

### [ ] `src/platforms/amiga/rtg/rtg-gfx.c`  *(~1202 warnings)*

Dominant warnings: `-Wcast-align`, `-Wconversion`, `-Wsign-conversion`, `-Wimplicit-int-conversion`, `-Wunused-but-set-variable`.

Tasks:
- Standardise integer types:
  - Use `size_t` for buffer indices and loop counters.
  - Use `uint32_t`/`uint16_t` for pixel values and packed colours.
  - Avoid mixing `int` with `uint8_t/uint16_t/uint32_t` without explicit casts.
- Replace unaligned pointer casts such as:
  - `*((uint16_t*)&buf[i])`, `*((uint32_t*)&buf[i])`
  with small local helpers:
  - `static inline uint16_t load_u16_be(const uint8_t *p) { uint16_t v; memcpy(&v, p, sizeof v); return be16toh(v); }`
  - same for `u32`, and little-endian if needed.
- Clean up unused or “set but not used” temporaries, or mark them `(void)var;` only when truly needed for debugging.
- Fix any `printf`/logging format mismatches (e.g. `%d` vs `uint32_t`) by:
  - Using `PRIu32`/`PRId32` macros from `<inttypes.h>`, or
  - Casting to a well-defined type (`(unsigned)foo`) and using the matching format.

### [ ] `src/platforms/amiga/rtg/rtg.c`  *(~110 warnings)*

Dominant warnings: `-Wconversion`, `-Wsign-conversion`, format issues.

Tasks:
- Audit API surface of RTG functions:
  - Ensure width/height/format/pitch types match the shared `rtg.h` expectations (`uint16_t` or `uint32_t`, but consistent).
  - Avoid passing `int` into functions that expect `uint16_t` without an explicit, range-checked cast.
- Fix logging/printf usage:
  - Make sure all `rtg_*` logs use correct format specifiers for `uint16_t/uint32_t`.
- Where “mode numbers” or flags are stored in `uint8_t` but used in arithmetic, promote to `unsigned`/`uint32_t` first, then narrow back if safe.

### [ ] `src/platforms/amiga/rtg/rtg-output-raylib.c`  *(~19 warnings)*

Dominant warnings: `-Wconversion`, `-Wsign-conversion`, format issues.

Tasks:
- Keep the external API types exactly as declared in `rtg.h`, however make internals consistent:
  - Use `uint16_t` for RTG width/height/pitch/format values.
  - Use `int32_t` or `float` for Raylib side (`screen_w`, `screen_h`, coordinates).
- Fix sign/width conversions:
  - When passing to Raylib calls that expect `int`, cast explicitly from `uint16_t`.
  - For logs, use explicit casts or `<inttypes.h>` macros so format strings line up.
- Make sure comparisons like `if (pitch != *data->pitch || …)` operate on the same type on both sides.

---

## Phase 2 — Amiga core platform

These touch autoconfig, registers, Gayle, and platform glue. Important, however less directly tied to RTG.

### [ ] `src/platforms/amiga/amiga-registers.c`  *(count in full list; many warnings)*

Tasks:
- Standardise register widths:
  - Hardware registers should be `uint8_t`/`uint16_t`/`uint32_t` consistently with the real Amiga mapping.
- For warnings about `-Wimplicit-int-conversion` or `-Wsign-conversion`:
  - Promote to a larger unsigned type while doing arithmetic, then narrow back with a cast once range is checked.
- Fix any `char` used as a small integer:
  - Use `uint8_t`/`int8_t` instead of plain `char` where value range matters.

### [ ] `src/platforms/amiga/amiga-platform.c`  *(~21 warnings)*

Known warning example: `tolower(cfg->subsys[i])` losing precision and using `char` unsafely.

Tasks:
- For `tolower` / `toupper` usage:
  - Always cast via `unsigned char`:
    - `int c = (unsigned char)cfg->subsys[i];`
    - `cfg->subsys[i] = (char)tolower(c);`
- Audit other `char` usage:
  - When used as a small integer, convert to `uint8_t`/`int8_t`.
- Align printf/logging formats with argument types, especially for sizes and indices (prefer `size_t` + `%zu`).

### [ ] `src/platforms/amiga/Gayle.c`  *(~27 warnings)*

Tasks:
- Make address and length types explicit (`uint32_t`, `size_t`) to avoid sign-conversion warnings.
- Fix unaligned pointer casts in any IO buffer access using the same `load_u16/load_u32` pattern as RTG/piscsi.
- Ensure register offsets and masks use unsigned constants (e.g. `0xFFFFu`).

### [ ] `src/platforms/amiga/amiga-autoconf.c`  *(~19 warnings)*

Tasks:
- Standardise config-space types (`uint8_t` for byte-sized fields, `uint16_t`/`uint32_t` for larger).
- Fix sign/width conversions when stepping through config ROM or tables (loop indices as `size_t`).

### [ ] `src/platforms/amiga/hunk-reloc.c`  *(~17 warnings)*

Tasks:
- Use `size_t` for offsets into relocation tables and buffers.
- Replace casts from `uint8_t*` to `uint16_t*`/`uint32_t*` with alignment-safe helpers using `memcpy`.

---

## Phase 3 — Subsystems (SCSI, RTC, AHI, Mac68k, config)

These are smaller clusters of warnings and good candidates for “one file per session”.

### [ ] `src/platforms/amiga/piscsi/piscsi.c`  *(~88 warnings)*

Major warnings: `-Wcast-align`, `-Wconversion`.

Tasks:
- Replace unaligned IO reads/writes (`(uint16_t*)buf`, `(uint32_t*)buf`) with helper functions based on `memcpy`.
- Use `size_t` for block indices and transfer sizes.
- Normalise SCSI ID / LUN types (likely `uint8_t`).

### [ ] `src/platforms/shared/rtc.c`  *(~23 warnings)*

Tasks:
- Make time/offset types explicit: `uint8_t` for BCD fields, `uint32_t` for seconds, etc.
- Fix format specifiers in logging (e.g. `%02" PRIu8 "` style if using `<inttypes.h>`).

### [ ] `src/platforms/amiga/ahi/pi_ahi.c`  *(~20 warnings)*

Tasks:
- Standardise audio sample/count types (`uint32_t`/`size_t`).
- Fix conversion warnings around sample formats (signed vs unsigned).

### [ ] `src/platforms/mac68k/mac68k-platform.c`  *(~11 warnings)*

Tasks:
- Align endian conversions and address math with explicit types (`uint32_t` addresses, `size_t` lengths).
- Fix any mismatch between Mac68k platform callbacks and the core emulator prototypes.

### [ ] `src/config_file/config_file.c`  *(~19 warnings)*

Tasks:
- Clean up `int`/`size_t` mixing when walking strings and buffers.
- Fix `printf`/log formats for sizes and indices (`%zu` for `size_t`).

---

## Phase 4 — Core + devices (smaller but important)

### [ ] `src/emulator.c`  *(~27 warnings)*

Tasks:
- Fix any format/printf mismatches (`uint32_t` vs `%d` etc.).
- Make cycle counts, frame counts, and timestamps consistently `uint64_t`/`uint32_t` and logged with matching formats.
- Remove or explicitly mark unused static functions/variables if warnings are noisy and genuinely unused.

### [ ] `src/platforms/amiga/pistorm-dev/pistorm-dev.c`  *(~31 warnings)*

Tasks:
- Ensure the new `kmod` protocol types line up with `uapi/linux/pistorm.h` (no silent narrowing).
- Fix any logging format mismatches for GPIO addresses, flags, etc.

### [ ] `src/a314/a314.cc`  *(~27 warnings)*

Common warnings: `-Wunused-parameter`, `-Wsign-conversion`.

Tasks:
- For unused callback parameters, either:
  - Remove the name in the definition, or
  - Cast to void: `(void)cc;` to silence warnings explicitly.
- Standardise length/index types to `size_t` where possible.
- Align logging formats with the underlying integer types.

---

## Process checklist for Qwen (per file)

For each file listed above:

1. Open the file and locate all warnings for it in `make.nonmusashi.warnings`.
2. Fix:
   - Format string / printf mismatches.
   - Dangerous or noisy conversions (`-Wconversion`, `-Wsign-conversion`).
   - Unaligned pointer casts (`-Wcast-align`) using small helpers.
3. Rebuild with:
   - `make full C=clang`
4. Confirm:
   - Warning count for that file drops to zero (or at least decreases).
   - Emulator still builds and runs with existing configs.
5. Mark the file as `[x]` in this `TASKLIST.md` (and optionally note the commit hash).

Out of scope for this pass:
- `src/musashi/*` (entire directory)
- Behaviour-changing refactors; only touch types, casts, and format strings unless there is a clear UB fix.

