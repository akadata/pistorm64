# Blitter (Agnus Blitter Unit)

Sources:
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0060.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node001C.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node011C.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0120.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0127.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0170.html.txt

## Base Address
- Custom chip base: 0xDFF000
- Offsets below are added to 0xDFF000.

## Core Registers (offsets)
- BLTDDAT 0x000 (ER) Blitter destination early read (dummy address)
- BLTCON0 0x040 Blitter control 0
- BLTCON1 0x042 Blitter control 1
- BLTAFWM  0x044 First word mask (source A)
- BLTALWM  0x046 Last word mask (source A)
- BLTCPTH  0x048 / BLTCPTL 0x04A Source C pointer
- BLTBPTH  0x04C / BLTBPTL 0x04E Source B pointer
- BLTAPTH  0x050 / BLTAPTL 0x052 Source A pointer
- BLTDPTH  0x054 / BLTDPTL 0x056 Destination D pointer
- BLTSIZE  0x058 Start and size (width/height)
- BLTCON0L 0x05A Lower 8 bits of BLTCON0 (minterms)
- BLTSIZV  0x05C Vertical size (15-bit height)
- BLTSIZH  0x05E Horizontal size/start (11-bit width)
- BLTCMOD  0x060 Modulo source C
- BLTBMOD  0x062 Modulo source B
- BLTAMOD  0x064 Modulo source A
- BLTDMOD  0x066 Modulo destination D
- BLTCDAT  0x070 Source C data
- BLTBDAT  0x072 Source B data
- BLTADAT  0x074 Source A data

## Status / Control
- DMACONR: BLTDONE and BLTNZERO bits reflect blitter completion/zero status.
- DMACON: BLTEN enables blitter DMA. BLTPRI ("blitter-nasty") gives priority over CPU.

## Logic Function (BLTCON0)
- BLTCON0 includes a Logic Function (LF) byte used to define the minterm truth table.
- There are 256 possible logic operations for combining sources A, B, C.
- LF is derived by filling the 8-entry truth table (A,B,C -> D) and reading bits bottom-up.

## Descending Mode (BLTCON1)
- Descending mode enables reverse address stepping for overlapping moves.
- When enabled, pointers decrement by 2 per word, modulos are subtracted, and masks swap roles.

## Pipeline Behavior
- Blitter is pipelined: it can fetch multiple sources before first destination write.
- Overlap behavior depends on internal pipelining and bus contention.

## Notes
- BLTDDAT is a dummy address; CPU does not read it directly.
- Use BLTDONE from DMACONR (or DMAB_BLTDONE) to wait for completion.
