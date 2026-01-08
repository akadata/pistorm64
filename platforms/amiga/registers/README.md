# Amiga Register Headers

Sources:
- Hardware/ADCD_2.1/_txt/Includes_and_Autodocs_2._guide/node00CA.html.txt (CIA)
- Hardware/ADCD_2.1/_txt/Includes_and_Autodocs_2._guide/node00CB.html.txt (DMACON bits)
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0060.html.txt (custom registers)
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node00D3.html.txt (memory map)

## CIAA/CIA B Addressing
- CIAA base: 0xBFE001 (odd address, low byte lane)
- CIAB base: 0xBFD000 (even address, high byte lane)
- CIA registers are spaced by 0x100 in the 68k map (offsets 0x0000, 0x0100, ... 0x0F00).

Examples:
- CIAA PRA: 0xBFE001
- CIAA PRB: 0xBFE101
- CIAB PRA: 0xBFD000
- CIAB PRB: 0xBFD100

Access guidance:
- Use 8-bit accesses to CIA registers.
- CIAA is on odd addresses; CIAB is on even addresses.
- Avoid 16-bit writes to CIA registers unless you explicitly handle byte lanes.

## Overlaps and Shared Custom Space
All custom-chip registers live in the same 0xDFF000 space. Different headers group
registers by functional ownership, so some addresses appear in multiple headers:

- DMACON/DMACONR: in AGNUS and PAULA (shared DMA control)
- INTENA/INTREQ/INTENAR/INTREQR: in PAULA (interrupt controller)
- JOY0DAT/JOY1DAT, CLXDAT/CLXCON: in DENISE and PAULA (input/collision)
- BPLCONx and bitplane pointers: in AGNUS (DMA) but functionally part of display

This is expected: the chip headers are logical groupings, not disjoint address maps.
Use the register offsets and base addresses as the source of truth.

## Header Layout
- amiga_custom_chips.h: convenience include + base ranges
- agnus.h, paula.h, blitter.h, denise.h, cia.h: per-chip groupings

## Pi-Side Tools
### pimodplay (raw Paula DMA test)
Build:
- `./build_pimodplay.sh`

Usage:
- `sudo ./pimodplay --raw <file> --period <val> --vol <0-64> --seconds <n>`
- `sudo ./pimodplay --saints --tempo 180`
- Default timing is PAL (50 Hz). Use `--ntsc` to select 60 Hz.
  If the tune is too long for AUD0LEN, increase `--tempo`.

WAV playback:
- `sudo ./pimodplay --wav <file> --rate <hz>`
  (Rate overrides period; WAV must be PCM mono 8/16-bit.)

Note:
- `--mod` is reserved for the full MOD replay engine and is not implemented yet.
