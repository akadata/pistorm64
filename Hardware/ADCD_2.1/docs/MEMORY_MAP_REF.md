# Memory Map Reference (A1000/A500/A2000)

Source:
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node00D3.html.txt

## System Memory Map
Address ranges are listed as in the hardware manual.

- 0x000000 - 0x03FFFF: 256K Chip RAM (A1000 chip RAM, first 256K for A500/A2000)
- 0x040000 - 0x07FFFF: 256K Chip RAM (second 256K for A500/A2000)
- 0x080000 - 0x0FFFFF: 512K Extended chip RAM (to 1MB for A2000)
- 0x100000 - 0x1FFFFF: Reserved (do not use)
- 0x200000 - 0x9FFFFF: Primary 8MB AUTOCONFIG space
- 0xA00000 - 0xBEFFFF: Reserved (do not use)
- 0xBFD000 - 0xBFDF00: 8520-B (CIAB), even addresses only
- 0xBFE001 - 0xBFEF01: 8520-A (CIAA), odd addresses only
- 0xC00000 - 0xDFEFFF: Reserved (do not use)
- 0xC00000 - 0xD7FFFF: Internal expansion (slow) memory (some systems)
- 0xD80000 - 0xDBFFFF: Reserved (do not use)
- 0xDC0000 - 0xDCFFFF: Real-time clock (not on all systems)
- 0xDFF000 - 0xDFFFFF: Custom chip registers
- 0xE00000 - 0xE7FFFF: Reserved (do not use)
- 0xE80000 - 0xE8FFFF: AUTOCONFIG space (boards appear here before relocation)
- 0xE90000 - 0xEFFFFF: Secondary AUTOCONFIG space (usually 64K I/O boards)
- 0xF00000 - 0xFBFFFF: Reserved (do not use)
- 0xFC0000 - 0xFFFFFF: 256K System ROM

