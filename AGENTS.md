# Workplan: Fix Format/Type Warnings (clang)

## Scope
- Address clang warnings from `make.log` about mismatched printf/format types and unsafe narrowing between `uint8_t`/`uint16_t`/`uint32_t` and `char`/`int`/`uint`.
- Ignore everything under `src/musashi/` for now.
- Preserve emulator behavior; prefer localized, explicit fixes over refactors.

## Build Command
- `make full C=clang`

## Plan
1. **Triage warnings (non-Musashi).**
   - Create a filtered list:
     - `rg -n "warning" make.log | rg -v "^.*src/musashi/" > make.nonmusashi.warnings`
   - Group by file to prioritize hot spots:
     - `cut -d: -f1 make.nonmusashi.warnings | sort | uniq -c | sort -nr`

2. **Fix printf/format mismatches first.**
   - Include `<inttypes.h>` where `PRIu8`, `PRIu16`, `PRIu32`, `PRId8`, etc. are needed.
   - Remember default promotions: `uint8_t`/`int8_t` promote to `int` in varargs; use casts or `PRI*` macros.
   - Prefer changing log/print helpers to accept `const char *` when format strings are const.

3. **Fix narrowing/sign conversions with explicit intent.**
   - Add narrow-cast helpers (e.g., `u16_from_u32_checked`) in a shared header if repeated.
   - Clamp or validate configuration-derived values before casting.
   - Use explicit casts only when the range is known and safe.

4. **Iterate and recompile.**
   - Re-run `make full C=clang` after each file group.
   - Track remaining warnings in `make.nonmusashi.warnings`.

5. **Emulation verification.**
   - Run the emulator with existing configs on target hardware as usual.
   - Keep changes strictly to formatting and type-safety to avoid behavior drift.

## Out of Scope (for now)
- `src/musashi/*` warnings.
- Non-type warnings like missing prototypes or shadowing unless they are in files already being touched for type fixes.
