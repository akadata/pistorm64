// SPDX-License-Identifier: MIT
// AGNUS chip register definitions for Amiga
// Based on Amiga Hardware Reference Manual (ADCD 2.1)

#ifndef AGNUS_H
#define AGNUS_H

// AGNUS base address is 0xDFF000
#define AGNUS_BASE 0xDFF000

// DMA Control Registers (shared with Paula)
#ifndef DMACONR
#define DMACONR  (AGNUS_BASE + 0x002)  // Read DMA control / blitter status
#endif
#ifndef DMACON
#define DMACON   (AGNUS_BASE + 0x096)  // Write DMA control (set/clear)
#endif

// Beam Position Registers
#define VPOSR    (AGNUS_BASE + 0x004)  // Read vertical MSB + frame flop
#define VHPOSR   (AGNUS_BASE + 0x006)  // Read vertical/horizontal beam position
#define VPOSW    (AGNUS_BASE + 0x02A)  // Write vertical MSB + frame flop
#define VHPOSW   (AGNUS_BASE + 0x02C)  // Write vertical/horizontal position

// Display Window / Data Fetch
#define DIWSTRT  (AGNUS_BASE + 0x08E)  // Display window start
#define DIWSTOP  (AGNUS_BASE + 0x090)  // Display window stop
#define DDFSTRT  (AGNUS_BASE + 0x092)  // Display data fetch start (horizontal)
#define DDFSTOP  (AGNUS_BASE + 0x094)  // Display data fetch stop (horizontal)

// Copper Registers
#define COPCON   (AGNUS_BASE + 0x02E)  // Copper control (CDANG)
#define COP1LCH  (AGNUS_BASE + 0x080)  // Copper list 1 pointer high
#define COP1LCL  (AGNUS_BASE + 0x082)  // Copper list 1 pointer low
#define COP2LCH  (AGNUS_BASE + 0x084)  // Copper list 2 pointer high
#define COP2LCL  (AGNUS_BASE + 0x086)  // Copper list 2 pointer low
#define COPJMP1  (AGNUS_BASE + 0x088)  // Copper restart 1
#define COPJMP2  (AGNUS_BASE + 0x08A)  // Copper restart 2
#define COPINS   (AGNUS_BASE + 0x08C)  // Copper instruction fetch identify

// Bitplane Pointers (DMA)
#define BPL1PTH  (AGNUS_BASE + 0x0E0)  // Bitplane 1 pointer high
#define BPL1PTL  (AGNUS_BASE + 0x0E2)  // Bitplane 1 pointer low
#define BPL2PTH  (AGNUS_BASE + 0x0E4)  // Bitplane 2 pointer high
#define BPL2PTL  (AGNUS_BASE + 0x0E6)  // Bitplane 2 pointer low
#define BPL3PTH  (AGNUS_BASE + 0x0E8)  // Bitplane 3 pointer high
#define BPL3PTL  (AGNUS_BASE + 0x0EA)  // Bitplane 3 pointer low
#define BPL4PTH  (AGNUS_BASE + 0x0EC)  // Bitplane 4 pointer high
#define BPL4PTL  (AGNUS_BASE + 0x0EE)  // Bitplane 4 pointer low
#define BPL5PTH  (AGNUS_BASE + 0x0F0)  // Bitplane 5 pointer high
#define BPL5PTL  (AGNUS_BASE + 0x0F2)  // Bitplane 5 pointer low
#define BPL6PTH  (AGNUS_BASE + 0x0F4)  // Bitplane 6 pointer high
#define BPL6PTL  (AGNUS_BASE + 0x0F6)  // Bitplane 6 pointer low

// Bitplane Modulo Registers
#define BPL1MOD  (AGNUS_BASE + 0x108)  // Bitplane 1 modulo
#define BPL2MOD  (AGNUS_BASE + 0x10A)  // Bitplane 2 modulo

// Bitplane Control Registers
#define BPLCON0  (AGNUS_BASE + 0x100)  // Bitplane control 0
#define BPLCON1  (AGNUS_BASE + 0x102)  // Bitplane control 1
#define BPLCON2  (AGNUS_BASE + 0x104)  // Bitplane control 2
#define BPLCON3  (AGNUS_BASE + 0x106)  // Bitplane control 3

// Bitplane Data Registers
#define BPL1DAT  (AGNUS_BASE + 0x110)  // Bitplane 1 data
#define BPL2DAT  (AGNUS_BASE + 0x112)  // Bitplane 2 data
#define BPL3DAT  (AGNUS_BASE + 0x114)  // Bitplane 3 data
#define BPL4DAT  (AGNUS_BASE + 0x116)  // Bitplane 4 data
#define BPL5DAT  (AGNUS_BASE + 0x118)  // Bitplane 5 data
#define BPL6DAT  (AGNUS_BASE + 0x11A)  // Bitplane 6 data

// ECS Beam / Timing Registers
#define HTOTAL   (AGNUS_BASE + 0x1C0)  // Horizontal line count (ECS)
#define HSSTOP   (AGNUS_BASE + 0x1C2)  // HSYNC stop (ECS)
#define HBSTRT   (AGNUS_BASE + 0x1C4)  // HBLANK start (ECS)
#define HBSTOP   (AGNUS_BASE + 0x1C6)  // HBLANK stop (ECS)
#define VTOTAL   (AGNUS_BASE + 0x1C8)  // Vertical line count (ECS)
#define VSSTOP   (AGNUS_BASE + 0x1CA)  // VSYNC stop (ECS)
#define VBSTRT   (AGNUS_BASE + 0x1CC)  // VBLANK start (ECS)
#define VBSTOP   (AGNUS_BASE + 0x1CE)  // VBLANK stop (ECS)
#define BEAMCON0 (AGNUS_BASE + 0x1DC)  // Beam counter control (ECS)
#define HSSTRT   (AGNUS_BASE + 0x1DE)  // HSYNC start (ECS)
#define VSSTRT   (AGNUS_BASE + 0x1E0)  // VSYNC start (ECS)
#define HCENTER  (AGNUS_BASE + 0x1E2)  // Horizontal position for Vsync on interlace (ECS)
#define DIWHIGH  (AGNUS_BASE + 0x1E4)  // Display window upper bits (ECS)

// Refresh
#define REFPTR   (AGNUS_BASE + 0x028)  // DRAM refresh pointer (test use only)

// DMA Control Bits (DMACON/DMACONR)
#define DMAF_SETCLR   0x8000  // Set/Clear control bit
#define DMAF_AUDIO    0x000F  // Mask for AUD0..AUD3
#define DMAF_AUD0     0x0001  // Audio channel 0 enable
#define DMAF_AUD1     0x0002  // Audio channel 1 enable
#define DMAF_AUD2     0x0004  // Audio channel 2 enable
#define DMAF_AUD3     0x0008  // Audio channel 3 enable
#define DMAF_DISK     0x0010  // Disk DMA enable
#define DMAF_SPRITE   0x0020  // Sprite DMA enable
#define DMAF_BLITTER  0x0040  // Blitter DMA enable
#define DMAF_COPPER   0x0080  // Copper DMA enable
#define DMAF_RASTER   0x0100  // Raster DMA enable
#define DMAF_MASTER   0x0200  // Master DMA enable
#define DMAF_BLITHOG  0x0400  // Blitter hog CPU (priority)
#define DMAF_ALL      0x01FF  // All DMA channels

// DMACONR specific bits
#define DMAF_BLTDONE  0x4000  // Blitter done
#define DMAF_BLTNZERO 0x2000  // Blitter not zero (busy)

// Bit numbers for DMACON operations
#define DMAB_AUD0     0
#define DMAB_AUD1     1
#define DMAB_AUD2     2
#define DMAB_AUD3     3
#define DMAB_DISK     4
#define DMAB_SPRITE   5
#define DMAB_BLITTER  6
#define DMAB_COPPER   7
#define DMAB_RASTER   8
#define DMAB_MASTER   9
#define DMAB_BLITHOG  10
#define DMAB_BLTNZERO 13
#define DMAB_BLTDONE  14
#define DMAB_SETCLR   15

#endif // AGNUS_H
