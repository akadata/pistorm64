# Memory Map Reference (A500 Focus + Annex)

Source:
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node00D3.html.txt

## A500 (Rev 6A, ECS Fat Agnus 8372A) Practical Map
This is the subset that matches the current test platform.

- 0x000000 - 0x0FFFFF: 1MB Chip RAM (Fat Agnus)
- 0x00C00000 - 0x00C7FFFF: A500 trapdoor “slow” RAM (typical)
- 0x00BF0000 - 0x00BFFFFF: CIA range
  - 0xBFE001 - 0xBFEF01: CIAA (odd addresses only)
  - 0xBFD000 - 0xBFDF00: CIAB (even addresses only)
- 0x00DFF000 - 0x00DFFFFF: Custom chip registers
- 0x00E80000 - 0x00E8FFFF: Z2 AUTOCONFIG (boards appear here before relocation)
- 0x00E90000 - 0x00EFFFFF: Z2 secondary AUTOCONFIG / I/O
- 0x00F80000 - 0x00FFFFFF: Kickstart ROM (512KB on A500)

## Legacy A1000/A500/A2000 Manual Map (Reference)
Address ranges listed as in the hardware manual.

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

## Annex: A1200/A4000 Highlights (Non‑A500)
This is a reminder of ranges that exist on later machines but are not present on the A500.

- 0x00A00000 - 0x00A7FFFF: Z2 I/O / PCMCIA windows (A600/A1200)
- 0x00DA0000 - 0x00DAFFFF: A1200 IDE/PCMCIA config
- 0x00D80000 - 0x00D8FFFF: A1200 clock port / spare CS
- 0x01000000 - 0x07FFFFFF: A3000/A4000 motherboard RAM ranges
- 0x08000000 - 0x0FFFFFFF: CPU‑slot 32‑bit Fast RAM (common on accelerators)
- 0x10000000 - 0x7FFFFFFF: Zorro III expansion space
- 0xFF000000 - 0xFF00FFFF: Zorro III autoconfig space
