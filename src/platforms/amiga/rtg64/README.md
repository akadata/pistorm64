# RTG64 Scaffold

`rtg64` is a new experimental RTG stack scaffold for PiStorm64 with explicit separation between:

- Amiga-side card driver API surface (P96/CGX-style operations)
- Pi-side backend transport and rendering path (eventually VC6/DRM/KMS)

This tree is intentionally minimal and safe to iterate without changing the existing `rtg/` path.

## What is included

- `rtg64-zorro.c`: minimal Zorro-II autoconfig identity and MMIO register front-end.
- `rtg64-protocol.h`: mailbox register layout + command/result enums.
- `rtg64-transport.[ch]`: transport shim that captures commands via MMIO.
- `rtg64.[ch]`: host-side skeleton entrypoints (`set_mode`, `alloc/free`, `fill/blit`, `palette`, `present`).
- `rtg64-vc6.[ch]`: VC6 backend seam with mode/fb/present lifecycle.
- `rtg64-bench.[ch]`: debug/test pattern + scroll benchmark helpers.
- `driver_amiga/`: Amiga-side skeleton for future P96 card implementation.
- `VC6_REFERENCE_NOTES.md`: extracted implementation notes from VideoCore.card study.

## Enable (scaffold only)

Add to config:

```ini
setvar rtg64 yes
```

This registers a `z2-rtg64` board with a 64KB MMIO window and command mailbox. It does not replace the existing `rtg` implementation.

## First-light milestone

1. Board appears in autoconfig diagnostics as `z2-rtg64`.
2. Driver can probe protocol magic/version.
3. Driver issues `SET_MODE`, `ALLOC_FB`, `FILL_RECT`, `PRESENT` commands.
4. Pi-side logs a successful smoketest and updates framebuffer bytes.

Once this is stable, swap the transport backend from mailbox-only stubs to VC6 display submission.
