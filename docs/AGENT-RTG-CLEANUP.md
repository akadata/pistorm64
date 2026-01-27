# clang warning cleanup – RTG and friends

Canonical build + log:

  make full C=clang V=1 2>clang-build.log
  rg "warning:" clang-build.log \
    | rg -v 'src/musashi/' \
    | rg -v 'src/softfloat/' \
    > core-warnings.log

Per-file counts:

  TOTAL=$(rg "warning:" core-warnings.log | wc -l || echo 0)
  RTG=$(rg "rtg-gfx.c" core-warnings.log | wc -l || echo 0)
  PISCSI=$(rg "piscsi.c" core-warnings.log | wc -l || echo 0)
  PIDEV=$(rg "pistorm-dev.c" core-warnings.log | wc -l || echo 0)
  PIAHI=$(rg "pi_ahi.c" core-warnings.log | wc -l || echo 0)

Always report:

  - total core warnings: TOTAL
  - rtg-gfx.c: RTG
  - piscsi.c: PISCSI
  - pistorm-dev.c: PIDEV
  - pi_ahi.c: PIAHI

Scope:

- Fix ONLY warnings in:
  - src/platforms/amiga/rtg/rtg-gfx.c
  - src/platforms/amiga/piscsi/piscsi.c
  - src/platforms/amiga/pistorm-dev/pistorm-dev.c
  - src/platforms/amiga/pi-ahi/pi_ahi.c
- NEVER edit code under:
  - src/musashi/*
  - src/softfloat/*

Order:

  1) rtg-gfx.c
  2) piscsi.c
  3) pistorm-dev.c
  4) pi_ahi.c

Do not advance to the next file until the current file has 0 warnings in core-warnings.log.

RTG rules:

- Treat RTG framebuffers as `uint8_t *` plus offsets.
- Pixel helpers/macros must not cast misaligned `uint8_t *` to `uint16_t *`/`uint32_t *`.
- Safe patterns:
  - Use `rtg_pixel_size[format]` and byte offsets.
  - Use `memcpy` into local `uint16_t`/`uint32_t` when needed.
  - Or explicit byte stores.
- It is allowed to introduce helpers like:

    static inline void set_rtg_pixel(
        uint8_t *base,
        uint32_t offset,
        uint32_t pix,
        uint8_t  format);

Reference:

- May *refer* to:
  - ../zz9000-drivers/rtg/mntgfx-gcc.c
  - ../zz9000-drivers/rtg/settings.h
  - ../zz9000-drivers/rtg/rtg.h
- Do NOT copy code verbatim. Use them only as behavioural references for formats and offsets.

“Done” means:

- `rg "<file>" core-warnings.log | wc -l` → 0 for that file, on a fresh build.

