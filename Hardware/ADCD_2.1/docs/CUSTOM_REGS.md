# Custom Chip Registers (DFFxxx)

Sources:
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0060.html.txt
- Hardware/ADCD_2.1/_txt/Includes_and_Autodocs_2._guide/node0551.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node00D3.html.txt

## Base Address
- Custom chip registers live at 0xDFF000 - 0xDFFFFF.
- Offsets below are added to 0xDFF000.

## Notes on Access
- Many registers are read-only or write-only.
- Some registers are strobe writes (no data bits).
- Pointers must be even and target chip RAM.
- See the register summary (node0060) for R/W flags and chip attribution.

## Control and Status (selected)
- DMACONR 0x002 Read DMA control / blitter status
- VPOSR   0x004 Read vertical MSB + frame flop
- VHPOSR  0x006 Read vertical/horizontal beam position
- DSKDATR 0x008 Disk data early read (dummy)
- JOY0DAT 0x00A Joystick/mouse 0
- JOY1DAT 0x00C Joystick/mouse 1
- CLXDAT  0x00E Collision data (read/clear)
- ADKCONR 0x010 Audio/disk control read
- POT0DAT 0x012 Pot counter pair 0
- POT1DAT 0x014 Pot counter pair 1
- POTGOR  0x016 Pot port data read
- SERDATR 0x018 Serial data/status read
- DSKBYTR 0x01A Disk data byte/status
- INTENAR 0x01C Interrupt enable read
- INTREQR 0x01E Interrupt request read

## Disk / Refresh / Copper / Window
- DSKPTH  0x020 / DSKPTL 0x022 Disk pointer
- DSKLEN  0x024 Disk length
- DSKDAT  0x026 Disk DMA data write
- REFPTR  0x028 DRAM refresh pointer
- VPOSW   0x02A Write vertical MSB + frame flop
- VHPOSW  0x02C Write vertical/horizontal position
- COPCON  0x02E Copper control (CDANG)
- SERDAT  0x030 Serial data write
- SERPER  0x032 Serial period/control
- POTGO   0x034 Pot port data write/start
- JOYTEST 0x036 Joystick counter test
- STREQU  0x038 Strobe for horiz sync with VB and EQU
- STRVBL  0x03A Strobe for horiz sync with VB
- STRHOR  0x03C Strobe for horiz sync
- STRLONG 0x03E Strobe for long horiz line

## Blitter
- BLTCON0 0x040 / BLTCON1 0x042
- BLTAFWM 0x044 / BLTALWM 0x046
- BLTCPTH 0x048 / BLTCPTL 0x04A
- BLTBPTH 0x04C / BLTBPTL 0x04E
- BLTAPTH 0x050 / BLTAPTL 0x052
- BLTDPTH 0x054 / BLTDPTL 0x056
- BLTSIZE 0x058 / BLTCON0L 0x05A
- BLTSIZV 0x05C / BLTSIZH 0x05E
- BLTCMOD 0x060 / BLTBMOD 0x062 / BLTAMOD 0x064 / BLTDMOD 0x066
- BLTCDAT 0x070 / BLTBDAT 0x072 / BLTADAT 0x074

## Copper
- COP1LCH 0x080 / COP1LCL 0x082
- COP2LCH 0x084 / COP2LCL 0x086
- COPJMP1 0x088 / COPJMP2 0x08A
- COPINS  0x08C

## Display / DMA Control
- DIWSTRT 0x08E / DIWSTOP 0x090
- DDFSTRT 0x092 / DDFSTOP 0x094
- DMACON  0x096
- CLXCON  0x098
- INTENA  0x09A
- INTREQ  0x09C
- ADKCON  0x09E

## Audio Channels (AUD0..AUD3)
- AUD0LCH 0x0A0 / AUD0LCL 0x0A2 / AUD0LEN 0x0A4 / AUD0PER 0x0A6 / AUD0VOL 0x0A8 / AUD0DAT 0x0AA
- AUD1LCH 0x0B0 / AUD1LCL 0x0B2 / AUD1LEN 0x0B4 / AUD1PER 0x0B6 / AUD1VOL 0x0B8 / AUD1DAT 0x0BA
- AUD2LCH 0x0C0 / AUD2LCL 0x0C2 / AUD2LEN 0x0C4 / AUD2PER 0x0C6 / AUD2VOL 0x0C8 / AUD2DAT 0x0CA
- AUD3LCH 0x0D0 / AUD3LCL 0x0D2 / AUD3LEN 0x0D4 / AUD3PER 0x0D6 / AUD3VOL 0x0D8 / AUD3DAT 0x0DA

## Bitplanes
- BPL1PTH 0x0E0 / BPL1PTL 0x0E2
- BPL2PTH 0x0E4 / BPL2PTL 0x0E6
- BPL3PTH 0x0E8 / BPL3PTL 0x0EA
- BPL4PTH 0x0EC / BPL4PTL 0x0EE
- BPL5PTH 0x0F0 / BPL5PTL 0x0F2
- BPL6PTH 0x0F4 / BPL6PTL 0x0F6
- BPLCON0 0x100 / BPLCON1 0x102 / BPLCON2 0x104 / BPLCON3 0x106
- BPL1MOD 0x108 / BPL2MOD 0x10A
- BPL1DAT 0x110 / BPL2DAT 0x112 / BPL3DAT 0x114 / BPL4DAT 0x116 / BPL5DAT 0x118 / BPL6DAT 0x11A

## Sprites
- SPR0PTH 0x120 / SPR0PTL 0x122
- SPR1PTH 0x124 / SPR1PTL 0x126
- SPR2PTH 0x128 / SPR2PTL 0x12A
- SPR3PTH 0x12C / SPR3PTL 0x12E
- SPR4PTH 0x130 / SPR4PTL 0x132
- SPR5PTH 0x134 / SPR5PTL 0x136
- SPR6PTH 0x138 / SPR6PTL 0x13A
- SPR7PTH 0x13C / SPR7PTL 0x13E
- SPR0POS 0x140 / SPR0CTL 0x142 / SPR0DATA 0x144 / SPR0DATB 0x146
- SPR1POS 0x148 / SPR1CTL 0x14A / SPR1DATA 0x14C / SPR1DATB 0x14E
- SPR2POS 0x150 / SPR2CTL 0x152 / SPR2DATA 0x154 / SPR2DATB 0x156
- SPR3POS 0x158 / SPR3CTL 0x15A / SPR3DATA 0x15C / SPR3DATB 0x15E
- SPR4POS 0x160 / SPR4CTL 0x162 / SPR4DATA 0x164 / SPR4DATB 0x166
- SPR5POS 0x168 / SPR5CTL 0x16A / SPR5DATA 0x16C / SPR5DATB 0x16E
- SPR6POS 0x170 / SPR6CTL 0x172 / SPR6DATA 0x174 / SPR6DATB 0x176
- SPR7POS 0x178 / SPR7CTL 0x17A / SPR7DATA 0x17C / SPR7DATB 0x17E

## Color Palette
- COLOR00..COLOR31: 0x180 - 0x1BE (32 entries)

## Beam / Timing (ECS)
- HTOTAL  0x1C0
- HSSTOP  0x1C2
- HBSTRT  0x1C4
- HBSTOP  0x1C6
- VTOTAL  0x1C8
- VSSTOP  0x1CA
- VBSTRT  0x1CC
- VBSTOP  0x1CE
- BEAMCON0 0x1DC
- HSSTRT  0x1DE
- VSSTRT  0x1E0
- HCENTER 0x1E2
- DIWHIGH 0x1E4
