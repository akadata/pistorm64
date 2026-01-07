# Denise (Video, Sprites, Collision, Input Sampling)

Sources:
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0060.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0027.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0038.html.txt

## Base Address
- Custom chip base: 0xDFF000
- Offsets below are added to 0xDFF000.

## Input Sampling
- JOY0DAT 0x00A Joystick/mouse 0 (vertical/horizontal counters)
- JOY1DAT 0x00C Joystick/mouse 1 (vertical/horizontal counters)

## Collision / Control
- CLXDAT 0x00E Collision data register (read and clear)
- CLXCON 0x098 Collision control register

## Sprites
Sprite registers are in the custom space (pointers in Agnus, position/data in Denise):
- SPR0POS 0x140 / SPR0CTL 0x142 / SPR0DATA 0x144 / SPR0DATB 0x146
- SPR1POS 0x148 / SPR1CTL 0x14A / SPR1DATA 0x14C / SPR1DATB 0x14E
- SPR2POS 0x150 / SPR2CTL 0x152 / SPR2DATA 0x154 / SPR2DATB 0x156
- SPR3POS 0x158 / SPR3CTL 0x15A / SPR3DATA 0x15C / SPR3DATB 0x15E
- SPR4POS 0x160 / SPR4CTL 0x162 / SPR4DATA 0x164 / SPR4DATB 0x166
- SPR5POS 0x168 / SPR5CTL 0x16A / SPR5DATA 0x16C / SPR5DATB 0x16E
- SPR6POS 0x170 / SPR6CTL 0x172 / SPR6DATA 0x174 / SPR6DATB 0x176
- SPR7POS 0x178 / SPR7CTL 0x17A / SPR7DATA 0x17C / SPR7DATB 0x17E

## Color Palette
- COLOR00..COLOR31 (0x180 - 0x1BE) 12-bit RGB palette entries

## Denise Revision
- DENISEID 0x07C Read-only chip revision level
