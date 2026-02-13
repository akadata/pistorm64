# VC6 Reference Notes (from VideoCore.card study)

These notes capture architectural learnings from `~/VideoCore.card/src` and map them to PiStorm64 `rtg64` work. This is intentionally not a code copy.

## Useful patterns observed

- Mailbox/property-tag flow for display state:
  - query display size (`0x00040003`)
  - set physical/virtual size (`0x00048003`, `0x00048004`)
  - set depth (`0x00048005`)
  - allocate framebuffer (`0x00040001`)
  - query pitch (`0x00040008`)
- Explicit pixel-format mapping from P96-like formats to HVS/VC format control words.
- A clear mode/panning lifecycle:
  1. mode set
  2. bytes-per-row / pitch selection
  3. active plane or display-list update
  4. present/vsync pacing
- Separation of 2D operations from output submission.

## Differences we keep in PiStorm64

- Emu68 code is bare-metal and uses direct hardware/firmware paths.
- PiStorm64 runs as Linux userspace + kernel module, so display submission should be done through Linux-safe backend paths.
- `rtg64` keeps a transport/backend seam so Amiga-visible protocol stays stable while backend evolves.

## What is now implemented in rtg64

- New backend module: `rtg64-vc6.[ch]`
- Runtime state and flow:
  - backend init
  - mode set
  - framebuffer attach
  - present counter
  - palette entry stubs
- Mailbox tag constants are recorded for future implementation, but no direct bare-metal mailbox access is used.

## Next concrete step

Implement real VC6 present path behind `rtg64_vc6_present()` using a Linux display backend, then validate:

1. `SET_MODE`
2. `ALLOC_FB`
3. `FILL_RECT`
4. `PRESENT`

with visible output on HDMI and stable UI responsiveness.
