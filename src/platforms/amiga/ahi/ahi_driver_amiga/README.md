# AHI Driver Sources (Amiga Side)

This folder contains PiStorm64 Amiga-side AHI driver build glue plus a local copy
of the AHI developer archive used for headers/examples and `prefsfile.a`.

## Upstream Source / Credits

- Package: `driver/audio/m68k-amigaos-ahidev.lha`
- Aminet page: https://aminet.net/package/driver/audio/m68k-amigaos-ahidev
- Short description: Retargetable audio, Developer's Archive
- Author: Martin Blom (`martin@blom.org`)
- Version: 6.0
- Architecture: `m68k-amigaos`
- Date: 2006-06-26

AHI naming note from upstream docs: use "AHI audio system" / "AHI".

## What is used here

- Headers from:
  - `m68k-amigaos-ahi/Developer/Include/C`
- Paula prefs source from:
  - `m68k-amigaos-ahi/Developer/Drivers/Paula/prefsfile.a`

These are consumed by `Makefile` to build:

- `PiAHI020.audio` (GCC path)
- `PI-AHI` (assembled from `prefsfile.a`)

## Build

From this directory:

```sh
make clean
make
```

Optional overrides:

```sh
make AHI_INC=/path/to/Developer/Include/C \
     PREFS_SRC=/path/to/Developer/Drivers/Paula/prefsfile.a
```

## License / Distribution

Per upstream archive metadata and included files:

- Copyright © 1994-2005 Martin Blom
- AHI v6 is (L)GPL-licensed (see upstream `COPYING` / `COPYING.LIB`)

If redistributing this folder, keep upstream notices and license files intact.
