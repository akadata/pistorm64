# DMA (DMACON / dmabits)

Sources:
- Hardware/ADCD_2.1/_txt/Includes_and_Autodocs_2._guide/node00CB.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0060.html.txt

## DMACON Write Bits (DMACONW)
Write to DMACON to set or clear DMA enables. Use DMAF_SETCLR to select set/clear.

- DMAF_SETCLR: 0x8000
- DMAF_AUDIO: 0x000F (mask for AUD0..AUD3)
- DMAF_AUD0: 0x0001
- DMAF_AUD1: 0x0002
- DMAF_AUD2: 0x0004
- DMAF_AUD3: 0x0008
- DMAF_DISK: 0x0010
- DMAF_SPRITE: 0x0020
- DMAF_BLITTER: 0x0040
- DMAF_COPPER: 0x0080
- DMAF_RASTER: 0x0100
- DMAF_MASTER: 0x0200
- DMAF_BLITHOG: 0x0400
- DMAF_ALL: 0x01FF

## DMACON Read Bits (DMACONR)
Read-only status bits.

- DMAF_BLTDONE: 0x4000
- DMAF_BLTNZERO: 0x2000

## Bit Numbers (DMAB_*)
Bit positions for use with bit-test operations:

- DMAB_AUD0: 0
- DMAB_AUD1: 1
- DMAB_AUD2: 2
- DMAB_AUD3: 3
- DMAB_DISK: 4
- DMAB_SPRITE: 5
- DMAB_BLITTER: 6
- DMAB_COPPER: 7
- DMAB_RASTER: 8
- DMAB_MASTER: 9
- DMAB_BLITHOG: 10
- DMAB_BLTNZERO: 13
- DMAB_BLTDONE: 14
- DMAB_SETCLR: 15

## Register Address
From the custom register summary:
- DMACONR (read): offset 0x002
- DMACON (write): offset 0x096

Custom registers live at base 0xDFF000; addresses are base + offset.
