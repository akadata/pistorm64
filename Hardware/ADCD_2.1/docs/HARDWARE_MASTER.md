# Amiga Hardware Master Reference

This is an index into the ADCD 2.1 text corpus, with focused summaries for PiStorm work.

## Docs
- CIA: Hardware/ADCD_2.1/docs/CIA.md
- DMA (DMACON/dmabits): Hardware/ADCD_2.1/docs/DMA.md
- AUTOCONFIG: Hardware/ADCD_2.1/docs/AUTOCONFIG.md
- Memory Map (A1000/A500/A2000): Hardware/ADCD_2.1/docs/MEMORY_MAP_REF.md
- Custom Registers: Hardware/ADCD_2.1/docs/CUSTOM_REGS.md
- Blitter: Hardware/ADCD_2.1/docs/BLITTER.md
- Paula (audio/disk/serial): Hardware/ADCD_2.1/docs/PAULA.md
- Agnus (DMA/display timing/copper): Hardware/ADCD_2.1/docs/AGNUS.md
- Denise (video/sprites/collision/input): Hardware/ADCD_2.1/docs/DENISE.md
- Gary (bus/glue): Hardware/ADCD_2.1/docs/GARY.md

## Quick Address Index
Sources: Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node00D3.html.txt, Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0060.html.txt

- CIAA base: 0xBFE001 (odd address)
- CIAB base: 0xBFD000 (even address)
- Custom registers base: 0xDFF000
- DMACONR (read): 0xDFF002
- DMACON (write): 0xDFF096
- AUTOCONFIG Zorro II space: 0x00E8xxxx (64KB)
- AUTOCONFIG Zorro III space: 0xFF00xxxx (64KB)
- System ROM: 0xFC0000 - 0xFFFFFF

## Quick Memory Ranges (A1000/A500/A2000)
Source: Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node00D3.html.txt

- Chip RAM: 0x000000 - 0x07FFFF (512KB)
- Extended Chip RAM: 0x080000 - 0x0FFFFF (512KB)
- Primary AUTOCONFIG space: 0x200000 - 0x9FFFFF
- Custom chip registers: 0xDFF000 - 0xDFFFFF
- AUTOCONFIG space (pre-relocation): 0xE80000 - 0xE8FFFF
- Secondary AUTOCONFIG space: 0xE90000 - 0xEFFFFF

## Model Quick Compare
See Hardware/ADCD_2.1/docs/MEMORY_MAP_REF.md for full details.

| Model | Chip RAM | Notable memory/I/O regions |
| --- | --- | --- |
| A1000 | 256KB (0x000000-0x03FFFF) | Kickstart from disk; early map per node00D3 |
| A500 | 512KB-1MB (0x000000-0x0FFFFF) | Trapdoor slow RAM 0x00C00000-0x00C7FFFF |
| A600 | 1MB chip typical | PCMCIA windows 0x00A0xxxx, IDE config 0x00DAxxxx |
| A1200 | 2MB chip typical | PCMCIA + IDE windows, Z3 via accelerators |
| A2000 | 512KB-1MB chip | Z2 expansions, 0x200000-0x9FFFFF |
| A3000 | 1MB chip + Z3 | 0x01000000+ motherboard RAM regions |
| A4000 | 2MB chip + Z3 | 0x01000000+ motherboard RAM, Z3 space |

## Notes
- AUTOCONFIG chaining uses /CFGINn and /CFGOUTn, allowing only one card to respond at a time. See Hardware/ADCD_2.1/docs/AUTOCONFIG.md.
- DMACON and interrupt registers are in the custom chip space (0xDFF000 base). See Hardware/ADCD_2.1/docs/DMA.md and Hardware/ADCD_2.1/docs/CUSTOM_REGS.md.
