# Agnus (DMA, Display Timing, Copper)

Sources:
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0060.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0028.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node002C.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node003C.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0043.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node00AC.html.txt

## Base Address
- Custom chip base: 0xDFF000
- Offsets below are added to 0xDFF000.

## Beam / Timing
- VPOSR  0x004 Read vertical MSB + frame flop
- VHPOSR 0x006 Read vertical/horizontal beam position
- VPOSW  0x02A Write vertical MSB + frame flop
- VHPOSW 0x02C Write vertical/horizontal position
- HTOTAL 0x1C0 Horizontal line count (ECS)
- HSSTOP 0x1C2 HSYNC stop (ECS)
- HBSTRT 0x1C4 HBLANK start (ECS)
- HBSTOP 0x1C6 HBLANK stop (ECS)
- VTOTAL 0x1C8 Vertical line count (ECS)
- VSSTOP 0x1CA VSYNC stop (ECS)
- VBSTRT 0x1CC VBLANK start (ECS)
- VBSTOP 0x1CE VBLANK stop (ECS)
- BEAMCON0 0x1DC Beam counter control (ECS)
- HSSTRT 0x1DE HSYNC start (ECS)
- VSSTRT 0x1E0 VSYNC start (ECS)
- HCENTER 0x1E2 Horizontal position for Vsync on interlace (ECS)
- DIWHIGH 0x1E4 Display window upper bits (ECS)

## Display Window / Data Fetch
- DIWSTRT 0x08E Display window start
- DIWSTOP 0x090 Display window stop
- DDFSTRT 0x092 Display data fetch start (horizontal)
- DDFSTOP 0x094 Display data fetch stop (horizontal)

## Copper
- COPCON  0x02E Copper control (CDANG)
- COP1LCH 0x080 / COP1LCL 0x082 Copper list 1 pointer
- COP2LCH 0x084 / COP2LCL 0x086 Copper list 2 pointer
- COPJMP1 0x088 / COPJMP2 0x08A Copper restart
- COPINS  0x08C Copper instruction fetch identify

## Bitplane Pointers (DMA)
- BPL1PTH 0x0E0 / BPL1PTL 0x0E2
- BPL2PTH 0x0E4 / BPL2PTL 0x0E6
- BPL3PTH 0x0E8 / BPL3PTL 0x0EA
- BPL4PTH 0x0EC / BPL4PTL 0x0EE
- BPL5PTH 0x0F0 / BPL5PTL 0x0F2
- BPL6PTH 0x0F4 / BPL6PTL 0x0F6

## Refresh
- REFPTR 0x028 DRAM refresh pointer (test use only)

Notes:
- COPCON behavior is extended on ECS (CDANG allows broader Copper access).
