# PiStorm64 RTG: 24/32-bit Modes and FakeNative Plan

This document is a task spec for Codex working in the `pistorm64` repo, branch `features/rtg_rock_8_16_14_42_bit_modes`.

The goal is to extend and clean up the RTG pipeline so that:

1. 16-bit paths stay fast and correct (already ~70–75 FPS at 1280×720 / 1920×1080).
2. Proper 24-bit and 32-bit modes exist (RGB888 and ARGB8888-style) alongside the current 16-bit RGB565/BGR565, fully wired end-to-end.
3. "Fake native" planar modes for 8-bit (and later HAM/EHB-style) modes are supported in a clean way, with correct palette handling.
4. The code style is consistent and readable (full names instead of x/y/w/h/sx/dx/fgcol/bgcol/etc.).
5. Old VideoCore / `USE_VC` / `vc_tvservice` code paths are removed or isolated, because this build targets 64-bit OS (Debian/Alpine) using DRM+raylib only.

The work should be done carefully and incrementally, with a build + run check after each major step.

---

## 0. Repo and files of interest

Codex will be working in the existing `pistorm64` tree. RTG-related files include (non-exhaustive):

* `src/platforms/amiga/rtg/rtg.c`
* `src/platforms/amiga/rtg/rtg.h`
* `src/platforms/amiga/rtg/rtg-gfx.c`
* `src/platforms/amiga/rtg/rtg_enums.h`
* `src/platforms/amiga/rtg/rtg-output-raylib.c`
* `src/platforms/amiga/rtg/rtg-output-sdl2.c`
* `src/platforms/amiga/rtg/rtg-output-null.c`
* Any remaining legacy iRTG/RTG_DEBUGME code that has not already been removed.

Amiga-side documentation is present under:

* `Hardware/ADCD_2.1/_txt/`
* `Hardware/autodocs/`
* `Hardware/wiki.amigaos.net/wiki/`

These docs cover Picasso96, graphics.device, DisplayInfo, and related structures that matter for RTG modes.

High-level JIT / memory ownership design is described in the existing design note (JIT + RTG memory mapping). That design must keep working.

---

## 1. Current state (summary)

* RTG is currently using 16-bit formats as the main path (RGB565 or close equivalents). Helper functions:

  * `rtg_rgb555_to_rgb565` / `rtg_bgr565_to_rgb565` / `rtg_bgr555_to_rgb565`
  * YUV→RGB packer: `rtg_yuv601_to_rgba` (currently returns a 0xFFxxxxxxxx ARGB value).
* The RTG register interface is accessed via `rtg_write` and friends, with `RTG_X*`, `RTG_Y*`, `RTG_U*`, `RTG_RGB*`, `RTG_ADDR*`, `RTG_COMMAND`, etc.
* RTG RAM is configured via `RTG_GFX_MEM` (currently 128 MB) and RTG address adjust arrays (`rtg_address_adj[...]`).
* The RTG inner loops have recently been cleaned up: better naming, removal of unused iRTG code, and debug-only blocks guarded behind `#ifdef DEBUG_RTG` or commented.
* Raylib DRM output now runs at ~70–75 FPS locked on 720p/1080p in 16-bit modes.
* `USE_VC` and `vc_tvservice` are obsolete for the target platform.

This spec does **not** assume that the Picasso96 driver already requests 24/32-bit modes; the code changes should be safe even if 16-bit modes are the only ones exercised right now.

---

## 2. Style and safety rules

When editing C files in this repo:

1. Use K&R brace style with braces even for single-line conditionals/loops, for example:

```c
if (a) {
    b;
}

while (x) {
    step();
}

for (int i = 0; i < n; i++) {
    work(i);
}
```

2. Prefer full descriptive names for variables and parameters:

   * `x`, `y` used as coordinates → `source_x`, `source_y` or `destination_x`, `destination_y`.
   * `w`, `h` → `width`, `height`.
   * `sx`, `sy` → `source_x`, `source_y`.
   * `dx`, `dy` → `destination_x`, `destination_y`.
   * `pitch`, `srcpitch`, `dstpitch` → `line_pitch`, `source_pitch`, `destination_pitch`.
   * `fgcol`, `bgcol` → `foreground_color`, `background_color`.
   * `mask` → `plane_mask` or `color_mask` (choose based on semantics).

3. Use one variable declaration per line, initialised explicitly where appropriate, for example:

```c
uint8_t current_bit      = 0;
uint8_t base_bit         = 0;
uint8_t base_byte_index  = 0;
uint8_t current_byte     = 0;
uint8_t foreground_u8    = 0;
```

4. Do **not** change the binary layout of any structs that mirror Amiga/Picasso96 structures (e.g. BoardInfo, RenderInfo, BitMap, P96Template, P96Pattern). Field names can remain as they are. Comments may be added to clarify meaning.

5. Function signatures in `rtg.h` may have parameter names changed for clarity, the order and types must stay identical.

6. Avoid reintroducing any dependency on `vc_tvservice` or other VideoCore-only APIs. Output should rely on DRM+raylib/SDL2.

---

## 3. Task A – Add 24-bit and 32-bit RTG formats

### A1. Discover and define pixel formats

1. Inspect `rtg_enums.h` and any related enums to see what formats already exist.

   * Identify constants for 16-bit formats (RGB565, BGR565, etc.).
   * Add or confirm constants for:

     * `RTG_FORMAT_RGB888` (packed 24-bit RGB, 3 bytes per pixel, no alpha).
     * `RTG_FORMAT_ARGB8888` or `RTG_FORMAT_RGBA8888` (32-bit, 8 bits per component + alpha).

2. Update the pixel-size lookup table (likely `rtg_pixel_size[]` in `rtg-gfx.c` or similar) so:

   * 16-bit formats map to 2 bytes.
   * 24-bit formats map to 3 bytes.
   * 32-bit formats map to 4 bytes.

3. Ensure any assertions or bounds checks that assume 2 bytes per pixel are updated to use `rtg_pixel_size[format]` instead of literals.

### A2. Implement pack/unpack helpers for 24/32-bit

In `rtg-gfx.c` (or an appropriate helper module):

1. Add packers:

   * `static inline uint32_t rtg_pack_rgba8888(uint8_t r, uint8_t g, uint8_t b, uint8_t a);`
   * `static inline uint32_t rtg_pack_rgb888(uint8_t r, uint8_t g, uint8_t b);`

2. Adapt `rtg_yuv601_to_rgba` so that it can feed either 32-bit ARGB/ RGBA paths directly, or be cleanly down-converted to 16-bit when necessary.

3. For 16-bit conversion, keep the existing `rtg_rgb555_to_rgb565`/`rtg_bgr565_to_rgb565` helpers unchanged.

### A3. Extend blitters and fill functions

Update functions in `rtg-gfx.c` such as:

* `rtg_fillrect`
* `rtg_fillrect_solid`
* `rtg_invertrect`
* `rtg_blitrect`
* `rtg_blitrect_solid`
* `rtg_blitrect_nomask_complete`
* `rtg_blittemplate`
* `rtg_blitpattern`
* `rtg_drawline` / `rtg_drawline_solid`
* `rtg_p2c` / `rtg_p2d` / `rtg_p2c_ex`

The changes should:

1. Respect `rtg_pixel_size[format]` for calculating byte offsets and line strides, avoiding hardcoded `*2` assumptions.

2. For 24-bit formats:

   * Treat pixel data as tightly packed 3-byte tuples in framebuffer memory.
   * Ensure inner loops advance by 3 bytes per pixel.

3. For 32-bit formats:

   * Treat framebuffer as `uint32_t *` where this makes sense, or as `uint8_t*` with 4-byte steps.
   * In mask/minterm operations, be explicit about how the mask applies to multi-byte pixels (usually mask is in terms of planes or channels; confirm via existing 16-bit handling and Picasso96 docs).

4. Where code already checks `format` to select a conversion path (e.g. RGB565 vs BGR565), extend those switch statements to handle 24/32-bit formats and either:

   * Pass through the pixel unchanged when the incoming data is already in the correct 24/32-bit layout.
   * Or use the packers to translate into the canonical framebuffer format.

### A4. Wire formats into output modules

In each RTG output backend:

* `rtg-output-raylib.c`
* `rtg-output-sdl2.c`
* `rtg-output-null.c`

1. Identify how the current code interprets `rtg_format` and maps it into the underlying API’s pixel format.

2. Extend those mappings so that:

   * 16-bit modes continue to use the same raylib/SDL2 texture formats as before.
   * 24-bit RGB888 framebuffer can be uploaded correctly (raylib/SDL2 may require treating it as 32-bit, or using a specific pixel format; use the simplest correct path while preserving performance).
   * 32-bit ARGB8888/RGBA8888 modes are mapped directly to native 32-bit texture formats.

3. Keep the performance logging intact (the existing `[RTG/RAYLIB][PERF]` logs with `draw`, `render`, etc.).

4. Ensure that any hardcoded assumptions about bytes-per-pixel in these backends are replaced with runtime values derived from `rtg_format`.

---

## 4. Task B – Remove obsolete USE_VC / vc_tvservice paths

1. Search for `USE_VC`, `vc_tvservice`, and other VideoCore-specific symbols.
2. Remove or `#if 0` these paths for the current 64-bit build.
3. Keep only DRM+raylib (and SDL2/null) codepaths active.
4. Verify that the build succeeds with `-DRPI4_TEST` and whatever current RTG defines are in the Makefile, and that RTG still starts and renders at the expected FPS.

The goal is to leave the codebase cleaner and focused on the modern Linux/DRM stack, without breaking any non-Pi builds that might rely on SDL2.

---

## 5. Task C – FakeNative modes and 256-colour palette

FakeNative here means: exposing modes to the Amiga that look like native planar modes (8-bit chunky / 256 colours, and later possibly HAM/EHB), while actually rendering them into RTG framebuffer memory in a convenient chunky format.

### C1. Identify existing FakeNative / planar hooks

1. Scan `rtg.c`, `rtg-gfx.c`, and driver code under `Amiga/rtg_driver_amiga/` for:

   * Any references to FakeNative, palette, or planar-to-chunky conversion.
   * P96 commands that specifically mention planar sources.
2. Identify the existing `rtg_p2c`, `rtg_p2d`, and `rtg_p2c_ex` paths and how they are used from the Amiga side.

### C2. Ensure 8-bit indexed modes work cleanly

1. Confirm that there is a format constant for 8-bit indexed chunky (CLUT8-style) framebuffer.

2. Ensure that palette uploads from the Amiga are honoured and stored in a palette table.

3. In the draw paths where pixels are written into an 8-bit framebuffer:

   * The pixel value should be a palette index.
   * The output modules (raylib/SDL2) should expand these indices through the palette into RGB(A) values for display.

4. Implement or clean up palette→RGB expansion in the output backend. This might take the form of:

   * Converting the 8-bit framebuffer into a 16/24/32-bit staging buffer each frame.
   * Or using a texture format that supports 8-bit indices plus palette. Use whichever is simpler and supported by the libraries.

### C3. Plan for HAM/EHB (future work)

For now, just ensure the plumbing does not preclude adding HAM/EHB-style emulation later:

* Do not hardcode assumptions that all 8-bit modes are plain CLUT.
* Leave room in enums and in the palette logic to add HAM/EHB translation in a later task.

No immediate implementation is required in this pass, just keep the design extensible.

---

## 6. Testing guidance

After completing each major subtask (A, B, C), run these tests on a Pi 4 target with the existing config:

1. Build: `make clean && make -jN` (where `N` is appropriate) for the emulator.
2. Boot an Amiga system with the RTG driver configured.
3. In 16-bit mode:

   * Open multiple Workbench windows.
   * Drag windows around and observe FPS logs and mouse responsiveness.
   * Play a video or run a demo that exercises RTG blits.
4. Once 24/32-bit modes are wired and configured driver-side:

   * Switch to a 24-bit or 32-bit Workbench screen.
   * Repeat window operations and ensure colours are correct and performance remains acceptable.
5. Confirm there are no crashes on mode switches, window closes, or RTG disable/enable cycles.

Log any suspicious warnings like out-of-bounds RTG accesses, unexpected format values, or palette index issues, and tighten checks as needed.

---

## 7. Non-goals for this pass

The following are explicitly **out of scope** for this task, to keep the work focused:

1. Changing the JIT or memory mapping design described in the existing JIT/MMU/RTG document.
2. Adding new Amiga-side RTG driver features beyond those strictly required to exercise 24/32-bit modes.
3. Implementing full HAM/EHB emulation; only palette-friendly plumbing is required now.
4. Changing any public APIs that other platform backends might use.

---

## 8. Summary

Codex should:

* Extend the RTG format handling to cover 24-bit and 32-bit pixel formats end-to-end.
* Clean up RTG blitters to be byte-size aware via `rtg_pixel_size[format]`.
* Wire the new formats into raylib/SDL2 outputs.
* Remove obsolete `USE_VC`/`vc_tvservice` code.
* Improve 8-bit FakeNative/palette handling so that 256-colour modes are clean and ready for later HAM/EHB work.

All of this must preserve the existing high-speed 16-bit path and keep the RTG pipeline solid at ~70–75 FPS on the Pi 4.

