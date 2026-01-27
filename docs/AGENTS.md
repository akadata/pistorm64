# Workplan: Fix Format/Type Warnings (clang)

## Scope

* Fix clang warnings from `clang-build.log` about mismatched printf/format types and unsafe narrowing between `uint8_t`/`uint16_t`/`uint32_t` and `char`/`int`/`uint`.
* Ignore everything under `src/musashi/` and `src/softfloat/`.
* Preserve emulator behavior; prefer localized, explicit fixes over refactors.

## Canonical build workflow

### Build + capture logs

Run:

* `./rebuild.sh`

`rebuild.sh` is the canonical entrypoint and should:

* build with clang and verbose output
* capture warnings into `clang-build.log`
* refresh `core-warnings.log` via `./rebuildcount.sh`
* optionally reload the kernel module

Current `rebuild.sh` (reference):

```sh
make full C=clang V=1 2>clang-build.log
. ./rebuildcount.sh

sudo modprobe pistorm

cp emulator emulator.last

#timeout 30 ./emulator
```

### Count + triage warnings

Run:

* `./rebuildcount.sh`            → print per-file counts only
* `./rebuildcount.sh 1`          → print counts + bottom-up warnings (newest-to-oldest) for all files
* `./rebuildcount.sh piscsi.c`   → print counts + bottom-up warnings filtered to that file
* `./rebuildcount.sh rtg.c`      → print counts + bottom-up warnings filtered to that file

Notes:

* `./rebuildcount.sh` rebuilds `core-warnings.log` by filtering `clang-build.log` and excluding `src/musashi/` and `src/softfloat/`.
* Bottom-up output is intended for Qwen to fix warnings starting from the most recent compiler lines.

## Plan

1. **Triage warnings (non-Musashi / non-softfloat).**

   * Always start with `./rebuild.sh` then `./rebuildcount.sh`.
   * For a focused pass on one file, use `./rebuildcount.sh <file>` to get bottom-up excerpts.

2. **Fix printf/format mismatches first.**

   * Include `<inttypes.h>` where `PRIu8`, `PRIu16`, `PRIu32`, `PRId8`, etc. are needed.
   * Remember default promotions: `uint8_t`/`int8_t` promote to `int` in varargs.
   * Use casts or `PRI*` macros; avoid changing behavior.
   * Prefer changing log helpers to accept `const char *` where format strings are const.

3. **Fix narrowing/sign conversions with explicit intent.**

   * Clamp or validate configuration-derived values before casting.
   * Use explicit casts only when the range is known and safe.
   * Introduce small helper functions only when repeated across a file.

4. **Iterate and recompile.**

   * Re-run `./rebuild.sh` after each group of fixes.
   * Verify the file under focus reaches 0 warnings in `core-warnings.log` before moving on.

5. **Emulation verification.**

   * Keep changes strictly to formatting and type-safety.
   * Run existing configs on target hardware as normal.

## Out of scope

* Any warnings under:

  * `src/musashi/*`
  * `src/softfloat/*`
* Non-type warnings (missing prototypes, shadowing, etc.) unless already in a file being touched.

# clang warning cleanup – RTG, PiSCSI, PiStorm-Dev, AHI

## Scope: files allowed to change

Fix ONLY warnings in:

* `src/platforms/amiga/rtg/rtg.c`
* `src/platforms/amiga/rtg/rtg-output-raylib.c`
* `src/platforms/amiga/rtg/rtg-gfx.c`
* `src/platforms/amiga/piscsi/piscsi.c`
* `src/platforms/amiga/pistorm-dev/pistorm-dev.c`
* `src/platforms/amiga/pi-ahi/pi_ahi.c`

NEVER edit code under:

* `src/musashi/*`
* `src/softfloat/*`

## Order of attack (updated) 

Priority is by current warning volume and hot-path risk:

1. `piscsi.c`
2. `pistorm-dev.c`
3. `pi_ahi.c`
4. `rtg.c` * complete
5. `rtg-output-raylib.c` *complete
6. `rtg-gfx.c` *complete 

Do not advance to the next file until the current file shows **0 warnings** in `core-warnings.log`.

## RTG rules

* Treat RTG framebuffers as `uint8_t *` plus byte offsets.
* Pixel helpers/macros must not cast potentially-misaligned `uint8_t *` to `uint16_t *`/`uint32_t *`.
* Safe patterns:

  * Use `rtg_pixel_size[format]` and byte offsets.
  * Use `memcpy` into local `uint16_t`/`uint32_t` when needed.
  * Or explicit byte stores.
* Allowed: introduce a small helper such as:

  ```c
  static inline void set_rtg_pixel(uint8_t *base, uint32_t offset, uint32_t pix, uint8_t format);
  ```

## Reference codebases (behavioral only)

May *refer* to (do NOT copy verbatim):

* `../zz9000-drivers/rtg/mntgfx-gcc.c`
* `../zz9000-drivers/rtg/settings.h`
* `../zz9000-drivers/rtg/rtg.h`

Also available for behavioral reference:

* `../amiga2000-gfxcard/` (drivers and RTG-related code)

## Definition of done

* A fresh `./rebuild.sh` run produces updated `clang-build.log` and `core-warnings.log`.
* `./rebuildcount.sh <current-file>` reports **0** warnings for that file.

