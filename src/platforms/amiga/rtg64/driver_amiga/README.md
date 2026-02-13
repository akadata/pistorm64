# RTG64 Amiga Driver Skeleton

This folder is the starting point for a dedicated RTG64 Picasso96 card driver.

## Scope of scaffold

- Minimal `boardinfo64.h` with product/manufacturer and framebuffer metadata.
- `rtg64-card.c` placeholder entrypoints for the P96 card API surface:
  - `FindCard`, `InitCard`
  - mode setup (`SetGC`, `SetPanning`, `SetDisplay`, `SetSwitch`)
  - core 2D hooks (`FillRect`, `BlitRect`)
- No real transport code yet; all functions are no-op placeholders.
- Packaging assets:
  - `rtg64.card`
  - `rtg64.info` (generated from PiGFX icon template with `BOARDTYPE=rtg64`)
  - `rtg64.monitor` (copied from Picasso96 monitor binary when available)

## Install

Run:

```sh
make install
```

Artifacts are copied to:

`/opt/pistorm64/data/a314-shared/drivers/rtg64`

### Install behavior details

- If `src/platforms/amiga/rtg/rtg_driver_amiga/PiGFX.info` exists, install generates `rtg64.info` from it and patches tooltypes to `BOARDTYPE=rtg64`.
- If `/opt/pistorm64/data/a314-shared/Picasso96Install/Devs/Monitors/Picasso96` exists, install uses it as the `rtg64.monitor` binary.
- If the monitor binary source does not exist, install falls back to the text scaffold `rtg64.monitor`.

## First detection test (Picasso96)

1. Ensure emulator config has `setvar rtg64 yes`.
2. Build/install this driver and copy artifacts from `/opt/pistorm64/data/a314-shared/drivers/rtg64` to Amiga:
   - `LIBS:Picasso96/rtg64.card`
   - `DEVS:Monitors/RTG64` (from `rtg64.monitor`)
   - `DEVS:Monitors/RTG64.info` (from `rtg64.info`)
3. Reboot Amiga.
4. Run `Prefs:Picasso96Mode` and verify board detection (`PiStorm RTG64`/`rtg64`).

## Immediate next step

Implement probe + mailbox handshake:

1. Find autoconfig board (manuf `2011`, product `0x0042`).
2. Validate magic/version from MMIO.
3. Send `SET_MODE` and `ALLOC_FB` command pair.
4. Emit one `FILL_RECT` frame and confirm visible output.
