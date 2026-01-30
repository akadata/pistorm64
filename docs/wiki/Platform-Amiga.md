# Platform: Amiga

The Amiga platform code lives under `platforms/amiga/` and includes:

- Autoconfig and Z2/Z3 memory setup.
- PiSCSI ROM/device emulation.
- RTG (Picasso96 style) support.
- AHI audio integration.
- Pi networking.

Key references:
- `platforms/amiga/readme.md`
- `platforms/amiga/piscsi/readme.md`
- `platforms/amiga/rtg/readme.md`
- `platforms/amiga/ahi/readme.md`
- `platforms/amiga/net/readme.md`

## Autoconfig notes

Autoconfig assigns:
- Z2 fast RAM in `0x00200000-0x009FFFFF`.
- PiSCSI Z2 ROM/device window (typical `0x00E90000` base).
- A314 device at `0x00EA0000`.
- PiStorm interaction device at `0x00EB0000`.
- Z3 fast RAM in high address space (e.g. `0x40000000+`).

Keep Z2 strictly within 8 MB for compatibility. Use Z3 for larger buffers and DMA windows.
