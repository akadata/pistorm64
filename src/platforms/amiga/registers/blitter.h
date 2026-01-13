// SPDX-License-Identifier: MIT
// BLITTER chip register definitions for Amiga
// Based on Amiga Hardware Reference Manual (ADCD 2.1)

#ifndef BLITTER_H
#define BLITTER_H

// BLITTER base address is 0xDFF000
#define BLITTER_BASE 0xDFF000

// Blitter Control Registers
#define BLTCON0  (BLITTER_BASE + 0x040)  // Blitter control 0
#define BLTCON1  (BLITTER_BASE + 0x042)  // Blitter control 1
#define BLTAFWM  (BLITTER_BASE + 0x044)  // First word mask (source A)
#define BLTALWM  (BLITTER_BASE + 0x046)  // Last word mask (source A)

// Blitter Pointer Registers
#define BLTCPTH  (BLITTER_BASE + 0x048)  // Source C pointer high
#define BLTCPTL  (BLITTER_BASE + 0x04A)  // Source C pointer low
#define BLTBPTH  (BLITTER_BASE + 0x04C)  // Source B pointer high
#define BLTBPTL  (BLITTER_BASE + 0x04E)  // Source B pointer low
#define BLTAPTH  (BLITTER_BASE + 0x050)  // Source A pointer high
#define BLTAPTL  (BLITTER_BASE + 0x052)  // Source A pointer low
#define BLTDPTH  (BLITTER_BASE + 0x054)  // Destination D pointer high
#define BLTDPTL  (BLITTER_BASE + 0x056)  // Destination D pointer low

// Blitter Size and Control Registers
#define BLTSIZE  (BLITTER_BASE + 0x058)  // Start and size (width/height)
#define BLTCON0L (BLITTER_BASE + 0x05A)  // Lower 8 bits of BLTCON0 (minterms)
#define BLTSIZV  (BLITTER_BASE + 0x05C)  // Vertical size (15-bit height)
#define BLTSIZH  (BLITTER_BASE + 0x05E)  // Horizontal size/start (11-bit width)

// Blitter Modulo Registers
#define BLTCMOD  (BLITTER_BASE + 0x060)  // Modulo source C
#define BLTBMOD  (BLITTER_BASE + 0x062)  // Modulo source B
#define BLTAMOD  (BLITTER_BASE + 0x064)  // Modulo source A
#define BLTDMOD  (BLITTER_BASE + 0x066)  // Modulo destination D

// Blitter Data Registers
#define BLTCDAT  (BLITTER_BASE + 0x070)  // Source C data
#define BLTBDAT  (BLITTER_BASE + 0x072)  // Source B data
#define BLTADAT  (BLITTER_BASE + 0x074)  // Source A data

// Blitter Early Read (Dummy Address)
#define BLTDDAT  (BLITTER_BASE + 0x000)  // Blitter destination early read (dummy address)

// Blitter Control 0 (BLTCON0) bits
#define BLTCON0_USEA    0x0001  // Use A channel
#define BLTCON0_USEB    0x0002  // Use B channel
#define BLTCON0_USEC    0x0004  // Use C channel
#define BLTCON0_USED    0x0008  // Use D channel
#define BLTCON0_SOLID   0x0010  // Solid pixels
#define BLTCON0_DRAW    0x0020  // Draw line
#define BLTCON0_LNPAF   0x0040  // Line draw pattern fill
#define BLTCON0_A_TO_D  0x0080  // A to D mode
#define BLTCON0_DESC    0x0100  // Descending mode
#define BLTCON0_FCMASK  0x0200  // Fill carry mask
#define BLTCON0_SHIFT0  0x0400  // Shift value bit 0
#define BLTCON0_SHIFT1  0x0800  // Shift value bit 1
#define BLTCON0_SHIFT2  0x1000  // Shift value bit 2
#define BLTCON0_SHIFT3  0x2000  // Shift value bit 3
#define BLTCON0_SHIFT4  0x4000  // Shift value bit 4

// Blitter Control 1 (BLTCON1) bits
#define BLTCON1_BBUSY   0x0001  // Blitter busy (read-only)
#define BLTCON1_FIRST   0x0002  // First word flag
#define BLTCON1_FILL    0x0004  // Fill mode
#define BLTCON1_E_LINE  0x0008  // End of line
#define BLTCON1_SIGN    0x0010  // Sign bit (line draw)
#define BLTCON1_ADI     0x0020  // A channel direction increment
#define BLTCON1_BDI     0x0040  // B channel direction increment
#define BLTCON1_CDI     0x0080  // C channel direction increment
#define BLTCON1_DDI     0x0100  // D channel direction increment
#define BLTCON1_ASH0    0x0200  // A shift bit 0
#define BLTCON1_ASH1    0x0400  // A shift bit 1
#define BLTCON1_ASH2    0x0800  // A shift bit 2
#define BLTCON1_BSH0    0x1000  // B shift bit 0
#define BLTCON1_BSH1    0x2000  // B shift bit 1
#define BLTCON1_BSH2    0x4000  // B shift bit 2

// Blitter Size Register Masks
#define BLTSIZE_HEIGHT_MASK 0xFF00  // Height mask (upper 8 bits)
#define BLTSIZE_WIDTH_MASK  0x001F  // Width mask (lower 5 bits)
#define BLTSIZE_HSTART_MASK 0x00E0  // Horizontal start mask (bits 5-7)

// Blitter Status (from DMACONR)
#define DMACONR_BLTDONE   0x4000  // Blitter done
#define DMACONR_BLTNZERO  0x2000  // Blitter not zero (busy)

// Blitter Priority (from DMACON)
#define DMACON_BLTPRI     0x0800  // Blitter priority (nasty)

// Common blitter operations
#define BLT_CLEAR         0x00  // Clear (0)
#define BLT_NAND          0x78  // Not AND
#define BLT_AND           0x88  // AND
#define BLT_NOR           0x28  // Not OR
#define BLT_OR            0xd8  // OR
#define BLT_XOR           0x68  // XOR
#define BLT_EQUIV         0x98  // Equivalent (XNOR)
#define BLT_NOTA          0x50  // NOT A
#define BLT_NOTB          0x38  // NOT B
#define BLT_A             0xc0  // A
#define BLT_B             0xf0  // B
#define BLT_SET           0xff  // Set (1)

#endif // BLITTER_H