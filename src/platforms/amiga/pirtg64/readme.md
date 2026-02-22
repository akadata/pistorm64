# PiRTG64 / PiStorm RTG driver for Amiga

PiRTG64 is the evolved RTG path for PiStorm-based Amiga systems. It replaces the original "PiGFX" driver with a modern, raylib‑based renderer, higher and more stable frame rates, and cleaner integration with Picasso96 (P96).

PiRTG64 has been validated on Raspberry Pi 4 and is designed for 64‑bit Linux userspace. The goal is to provide a solid, maintained RTG backend that can drive high‑resolution Workbench screens, games and applications, and "fake native" 8‑bit modes on ECS/OCS machines.

> Status: PiRTG64 is in the main branch. Pi 4 is the primary target and is working well. Pi Zero 2 W support will be revisited once the new RTG path is fully shaken down.

---

## Hardware & OS requirements

These are practical requirements for PiRTG64 as it stands now:

* Raspberry Pi 4 (primary target)

  * 2–4 GiB RAM minimum recommended
  * 4 GiB+ strongly recommended for heavy RTG use, big screens, and development
* Storage

  * Strongly recommended: USB 3 / USB4 SSD or NVMe
  * SD card is supported, but random I/O will be a bottleneck and can impact RTG smoothness
* OS

  * 64‑bit Linux userspace (e.g. Debian 13.x or Alpine on aarch64)
  * Kernel with DRM/KMS support for the Pi HDMI output
  * vc_tvservice and legacy firmware paths are no longer used in the RTG path

Other Raspberry Pi boards may work, however Pi 4 is the only platform validated with the new RTG and raylib DRM backend.

---

## RTG architecture overview

PiRTG64 has two main halves:

1. **Amiga‑side Picasso96 card driver**

   * `pirtg64.card` (replacing the old `pigfx020.card`)
   * Registers a Picasso96 board ("PiStorm PiRTG64") and exposes a curated set of pixel formats:

     * 8‑bit CLUT (indexed)
     * 16‑bit RGB (RGB565)
     * 32‑bit true‑color with alpha (R8G8B8A8 / B8G8R8A8, depending on configuration)
   * Uses P96 APIs for mode enumeration, BytesPerRow calculation, blitting, and compatible‑format filtering.

2. **Pi‑side RTG renderer inside the PiStorm emulator**

   * Lives under `src/platforms/amiga/rtg/`
   * Handles the RTG register block, command FIFO, and access to RTG framebuffer memory
   * Implements accelerated paths for:

     * Solid and masked rectangle fills
     * Line drawing
     * Block blits (with and without mask)
     * Planar‑to‑chunky blits (P2C/P2D) for fake‑native modes
   * Uses **raylib** with the DRM backend for display:

     * No SDL2 dependency for RTG
     * No reliance on `vc_tvservice`
     * Renders directly to HDMI via DRM/KMS in 64‑bit userspace

The Amiga sees PiRTG64 as a Picasso96 card; the Pi sees it as a raylib texture that is refreshed at high frame‑rates.

---

## Enabling RTG in PiStorm

In the PiStorm configuration file, enable RTG support and adjust RTG memory and resolution as required, for example:

```ini
setvar pirtg64
setvar rtg_width  1280
setvar rtg_height 720
setvar rtg_mem_mb 128
```

The exact variables may differ slightly depending on the branch and configuration helpers; refer to the current `config.example` in the tree for canonical names.

Once RTG is enabled, the emulator will start the PiRTG64 RTG thread and open the HDMI output via raylib/DRM.

---

## Installation on the Amiga side (modern path)

Most users should use the PiStorm HDF installer rather than manual setup.

1. **Install P96 / Picasso96**

   * Install Picasso96 on the Amiga.
   * Requirements (depending on P96 version):

     * Aminet Picasso96: Kickstart 2.04/2.05+.
     * Commercial P96 2.4+: Kickstart 3.1 and a 68020 or higher.

2. **Run the PiRTG64 installer from the PiStorm HDF**

   * Mount the PiStorm HDF via PiSCSI (or your usual PiStorm disk setup).
   * Launch the supplied PiRTG64 installer from Workbench.
   * Follow the prompts; the installer will:

     * Copy `pirtg64.card` into `LIBS:Picasso96/`
     * Install or update a Monitor file in `DEVS:Monitors/`
     * Set the tooltypes so the Monitor loads `pirtg64.card` on boot.

3. **Configure display modes with Picasso96Mode**

   * After rebooting with the new driver:

     * Open `Prefs:Picasso96Mode`.
     * Select the `PiStorm PiRTG64` board.
     * Add the modes you want (e.g. 640×480, 800×600, 1024×768, 1280×720, 1920×1080) in the formats you intend to use.

4. **Select a ScreenMode**

   * Open `Prefs:ScreenMode`.
   * Choose one of the PiRTG64 modes you configured and test it.
   * Once saved, Workbench and RTG‑aware applications can use these modes.

At this point, PiRTG64 is active and Workbench should be running on the Pi RTG output via HDMI.

---

## Legacy / manual installation

These steps are kept for reference and power users who prefer to avoid the installer.

1. Install Picasso96 as above.
2. During P96 install, choose any RTG board (e.g. Picasso IV or CyberVision 64/3D). This is only to seed a Monitor file with sensible tooltypes.
3. Copy the driver:

   * From the Pi side: `pirtg64.card` lives under `src/platforms/amiga/rtg/Amiga/rtg_driver_amiga/`.
   * On the Amiga: copy it to `LIBS:Picasso96/`.
4. Edit the Monitor file in `DEVS:Monitors/`:

   * Change the **BOARDNAME** tooltype to match the PiRTG64 board name (e.g. `BOARDNAME=PiRTG64`).
   * Change the **DRIVER** tooltype to `DRIVER=pirtg64.card` (or equivalent, depending on convention).
5. Reboot, or move the Monitor file out of `DEVS:Monitors` and double‑click it manually to load the board without rebooting.
6. Configure `Picasso96Mode` and `ScreenMode` as described in the modern path.

---

## Pixel formats and modes

PiRTG64 currently focuses on a realistic subset of P96 formats that are implemented end‑to‑end in the Pi‑side renderer:

* **8‑bit CLUT** (indexed)

  * Used for classic 256‑colour Workbench and fake‑native modes.
  * Palette is managed by Picasso96; the Pi‑side renderer converts the 8‑bit indices to RGB pixels for HDMI.

* **16‑bit RGB** (RGB565)

  * Suitable for lighter‑weight true‑colour screens and games.

* **32‑bit true‑colour with alpha**

  * Exposed as one or more of the P96 true‑alpha formats (e.g. `RGBFB_R8G8B8A8`, `RGBFB_B8G8R8A8`).
  * Backed by a 32‑bit raylib texture (`R8G8B8A8`).
  * The driver advertises only the formats that are mapped correctly by the Pi‑side renderer.

Unsupported or partially implemented P96 formats are no longer advertised broadly; the driver mirrors the stricter behaviour used by mature boards such as ZZ9000, which reduces surprises and mono‑only bugs.

---

## Fake native modes (256‑colour ECS/OCS)

PiRTG64 supports "fake native" 8‑bit modes for ECS/OCS systems. These are RTG modes that:

* Use 8‑bit CLUT for 256 colours.
* Mirror classic Amiga resolutions while rendering via the Pi RTG path.
* Allow games and demos targeting Workbench‑friendly 256‑colour modes to run through RTG, even on non‑AGA machines.

Planar‑to‑chunky conversion (P2C/P2D) has been audited and tightened so that decorations, icons, and mouse rendering are correct in these modes.

---

## Performance notes

PiRTG64 is tuned for high‑framerate Workbench and applications on Pi 4:

* Internal profiling has shown stable frame rates in the 60–75 FPS range at 1280×720 in typical desktop workloads.
* The renderer avoids unnecessary per‑frame re‑uploads where possible.
* RTG commands are batched through a simple, low‑overhead command path.

For best results:

* Use a Pi 4 with sufficient RAM and fast storage.
* Keep the host OS lean and avoid heavyweight desktop stacks on the Pi itself; the RTG path runs on the Pi while the Amiga sees only the abstract RTG board.

---

## Current limitations and roadmap

As of this revision:

* Screen‑dragging is not implemented and is not a priority. It would require uploading multiple full‑screen textures per frame, which is not realistic for a low‑latency RTG path.
* Some P96 edge‑case operations may still hit unimplemented code paths; these will be tightened as more software is tested.
* Pi Zero 2 W and other lower‑end boards have not yet been validated with the new RTG and raylib DRM backend.

Planned / desired improvements include:

* Further polish and documentation for 24‑bit and 32‑bit modes side‑by‑side.
* Additional tuning of fake‑native bitplane handling (HAM/EHB‑compatible scenarios where possible).
* More complete coverage of P96 acceleration hooks where they provide real‑world benefit.

---

## Summary

PiRTG64 brings a modern, Pi 4‑friendly RTG path to PiStorm, with raylib DRM output, curated Picasso96 format exposure, and support for both chunky RTG screens and 256‑colour fake‑native modes on classic Amigas.

It is now part of the main branch and intended as the default RTG path going forward.
