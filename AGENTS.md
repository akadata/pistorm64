# AGENTS: PiStorm64 RTG / P96 FAKENATIVE Investigation

Branch: `hotfix/p96offset-bpissue`

This document is for Codex / Qwen / other agents working on the PiStorm64 RTG path. It summarizes what has been observed so far, what is suspected to be wrong, and how fixes should be approached and tested.

## High‑level symptoms

1. **Window decoration offset (2×) in RTG modes**

   * In RTG screen modes, standard Intuition / P96 window decorations (title bars, gadgets, etc.) appear horizontally offset by roughly 2× their expected position.
   * The actual drawable content area seems to be offset as well, not just a visual glitch in the host renderer.
   * This happens both with and without `FAKENATIVEMODE`, however it is much more noticeable with P96 FAKENATIVEMODE enabled.

2. **Text/font rendering without `.info` file looks misaligned**

   * On screens where there is no `.info` icon or where standard Workbench fonts are used without companion metadata, character cells and baselines appear shifted.
   * This is likely the same root cause as the decoration offset: the underlying RTG surface origin / pitch / pan is wrong, so everything drawn by Intuition is landing at the wrong pixel coordinates.

3. **FAKENATIVEMODE tiny bitplane surfaces**

   * With `FAKENATIVEMODE=Yes` in Picasso96, some screens do not use a full‑screen RTG surface.
   * Instead, at least one (and possibly two) RTG bitplanes are **small surfaces**, visually around **256×256** (estimate), composited on a larger 640×480 (or similar) RTG screen.
   * On the host side, these appear as small “sprite‑like” rectangles of valid graphics surrounded by repeated or garbage areas.
   * This is not corruption from the Amiga’s perspective – it is legal P96 behaviour. The RTG card is being asked to expose a framebuffer that does not match simple `width * bpp` assumptions.

4. **RTG is very slow (~5 FPS)**

   * In the problematic modes, the RTG output feels like ~5 frames per second.

   * The log shows repeated messages like:

     * `RTG display enabled.` / `RTG display disabled.`
     * `[RTG/RAYLIB] Mode change detected after frame; reinitializing.`
     * `Reinitializing raylib...`

   * This strongly suggests that the backend is tearing down and recreating the raylib texture and window **every frame** or every few frames because it believes the mode keeps changing.

## Key log evidence

From a typical session with P96 + FAKENATIVEMODE:

* RTG mapping and mode changes:

  * `Pixel format switch from: 4BPP PLANAR (0) to 8BPP CLUT (1)`
  * Later: `Pixel format switch from: 8BPP CLUT (1) to 32BPP RGB (BGRA) (10)`

* Raylib window creation:

  * `Creating 640x480 raylib window...`
  * `Creating 1024x768 raylib window...`

* Critical warnings:

  * `[WARN] [RTG/RAYLIB] Framebuffer bounds invalid: addr=0x00300010 pitch=640 width=1024 height=768 bpp=4 needed=491520`
  * `[WARN] [RTG/RAYLIB] Frame pitch too small: pitch=640 row_bytes=4096`

Interpretation:

* The RTG core reports a mode of **1024×768, 32‑bit** (bpp = 4 bytes), but the configured pitch remains **640 bytes** (from a previous **640×480 8‑bit** mode).
* The raylib backend computes `row_bytes = width * bpp = 1024 * 4 = 4096` and compares it to the reported pitch (640) and to the configured RTG memory size.
* Since 640 < 4096, it declares the framebuffer invalid and/or too small and triggers a mode reinitialization.
* P96 FAKENATIVEMODE uses these kinds of partial surfaces legitimately (small RTG buffers that simulate native PAL/ECS/AGA modes). The backend needs to be tolerant rather than panicking.

## Hypotheses

1. **Pitch is not being normalized when mode or format changes**

   * The RTG core keeps an old pitch value when the driver changes pixel format or logical width.
   * For FAKENATIVE, P96 may intentionally set a pitch smaller than width×bpp (e.g., 640‑byte stride into a larger logical mode).
   * The backend currently assumes `pitch >= width * bpp` and treats anything smaller as an error.

2. **Mode change detection is over‑sensitive**

   * The raylib backend treats changes in pitch, base address, or other non‑visual parameters as a “mode change”.
   * As a result, it spikes the texture and re‑creates the window even when width, height, and pixel format are unchanged.

3. **Pan/offset handling is incomplete**

   * The 2× offset for window decorations and fonts suggests that pan/viewport offsets are being applied incorrectly:

     * Either the origin of the framebuffer is miscomputed when mapping Amiga VRAM to host texture coordinates.
     * Or pitch is misused, effectively doubling the horizontal offset.

## What agents should **not** change

* **Do not change endianness** anywhere in RTG paths. The current code and shaders already respect the existing big‑endian / little‑endian choices and they match the working non‑FAKENATIVE modes.
* **Do not re‑interpret pixel formats**. The enums and format handling in `rtg.c`, `rtg-gfx.c`, and `rtg-output-*.c` are already aligned. Fix the geometry (pitch, width, pan), not the format.
* **Do not remove bounds checks**. Any relaxation of checks must still guarantee no out‑of‑bounds reads from `rtg_mem`.

## Files of interest

* `src/rtg/rtg.c`
* `src/rtg/rtg.h`
* `src/rtg/rtg_enums.h`
* `src/rtg/rtg-gfx.c`
* `src/rtg/rtg-output-raylib.c`
* `src/rtg/rtg-output-null.c`
* `src/rtg/rtg-output-sdl2.c`

The active backend in this configuration is **raylib DRM**, so most changes should focus on `rtg-output-raylib.c` and the core RTG pitch/mode logic in `rtg.c` / `rtg-gfx.c`.

## Suggested approach for Codex / agents

### 1. Add detailed logging (temporary)

Goal: understand, for each frame and each mode change, what RTG thinks the mode is and how the backend sees it.

Log the following whenever:

* A mode is changed or set (SETMODE, SETPITCH, SETPAN, SETCLUT, etc.).
* A frame is about to be drawn.

Log fields:

* `width`, `height`
* `format` (enum + human‑readable string)
* `pitch` (bytes)
* framebuffer `addr`
* `row_bytes = width * bytes_per_pixel`
* `rtg_mem_size`

This log will confirm where pitch is inconsistent and how often mode reinit is triggered.

### 2. Normalize pitch on mode/format change (core RTG)

In RTG core (likely in `rtg.c` when handling SETMODE / SETPITCH), enforce a minimum pitch:

* Compute `min_pitch = width * bytes_per_pixel`.
* If `pitch < min_pitch`, **either**:

  * adjust pitch up to `min_pitch`, or
  * leave pitch as is but expose an `effective_width = pitch / bpp` to the backend.

The first option is simpler for the card emulation but may diverge from what P96 expects.

### 3. Make raylib backend tolerant and stop re‑creating textures unnecessarily

In `rtg-output-raylib.c`:

* Only consider it a **mode change** when **width, height, or pixel format** change.
* Treat pitch and base address changes as state updates, not full reinitializations.
* If `pitch < width * bpp`, log a warning once and **clamp the effective width** used for texture updates to `pitch / bpp`.

  * This will draw a smaller, correct “window” inside the texture without reading past the buffer.

### 4. Investigate 2× offset for decorations and fonts

With pitch and mode handling fixed, re‑check FAKENATIVE modes:

* If decorations are still offset by ~2× horizontally, inspect:

  * The SETPAN / viewport code in RTG core.
  * Any code that converts Amiga byte offsets into host pixel coordinates.
* Look for places where `pitch` or `width` is multiplied twice or where an extra `<< 1` or `* 2` slipped in for 8‑bit → 16‑bit or 16‑bit → 32‑bit transitions.

## How to test changes

Branch: `hotfix/p96offset-bpissue`

Build:

* Run: `make -j4 full`
* Ensure it completes without warnings newly introduced by the agent.

Runtime test:

1. Start emulator:

   * `./emulator --config min.cfg`

2. In Workbench / P96, test at least two screen modes:

   * A classic **640×480 8‑bit** RTG mode.
   * A **1024×768 32‑bit** RTG mode.

3. For each mode, verify:

   * Window decorations line up correctly (no 2× offset).
   * Fonts and icons without `.info` files render at correct positions.
   * FAKENATIVEMODE screens with partial bitplanes show a stable, non‑garbled image (even if still letterboxed or “mini” in the larger screen).
   * FPS is significantly higher than the current ~5 FPS and the log no longer shows per‑frame mode rebuilds.

4. Capture logs:

   * Save `rtg.log` or main emulator log with new debug output for regression analysis.

## Performance target

* RTG rendering should feel “interactive” (ideally 50–60 FPS) on the Pi4 in the tested modes.
* Occasional mode switches (e.g., opening a screen with different resolution) can re‑initialise raylib, however **not every frame**.
* The Dhrystone / MIPS numbers are already very strong in FAKENATIVE modes; RTG throughput should no longer be the bottleneck for normal Workbench usage.

---

Agents: focus first on **stopping the constant mode reinitialisation** and **fixing pitch / effective width handling**. Once the picture is stable and fast, the residual 2× offset for decorations should become much easier to track down and correct.

