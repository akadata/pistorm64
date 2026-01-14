// SPDX-License-Identifier: MIT
// Canonical custom chip base address definitions and shared helpers.
// Single source of truth for base addresses: include from per-chip headers.

#ifndef AMIGA_CUSTOM_CHIPS_BASES_H
#define AMIGA_CUSTOM_CHIPS_BASES_H

// Custom chip register page (all Agnus/Paula/Denise/Blitter regs)
#define CUSTOM_CHIP_BASE 0xDFF000

// Per-chip base aliases (same page)
#define AGNUS_BASE   CUSTOM_CHIP_BASE
#define PAULA_BASE   CUSTOM_CHIP_BASE
#define DENISE_BASE  CUSTOM_CHIP_BASE
#define BLITTER_BASE CUSTOM_CHIP_BASE

// CIA bases (odd/even byte lanes)
#define CIAA_BASE 0xBFE001
#define CIAB_BASE 0xBFD000

// Custom chip aperture range
#define CUSTOM_CHIP_START CUSTOM_CHIP_BASE
#define CUSTOM_CHIP_END   0xDFFFFF
#define CUSTOM_CHIP_SIZE  (CUSTOM_CHIP_END - CUSTOM_CHIP_START + 1)

// CIA ranges
#define CIAA_START CIAA_BASE
#define CIAA_END   0xBFEF01
#define CIAB_START CIAB_BASE
#define CIAB_END   0xBFDF00

// Common bit helpers
#define BIT0  0x0001
#define BIT1  0x0002
#define BIT2  0x0004
#define BIT3  0x0008
#define BIT4  0x0010
#define BIT5  0x0020
#define BIT6  0x0040
#define BIT7  0x0080
#define BIT8  0x0100
#define BIT9  0x0200
#define BIT10 0x0400
#define BIT11 0x0800
#define BIT12 0x1000
#define BIT13 0x2000
#define BIT14 0x4000
#define BIT15 0x8000

// Offset helpers
#define CUSTOM_REG_OFFSET(reg_addr) ((reg_addr) - CUSTOM_CHIP_BASE)
#define CIAA_REG_OFFSET(reg_addr)   ((reg_addr) - CIAA_BASE)
#define CIAB_REG_OFFSET(reg_addr)   ((reg_addr) - CIAB_BASE)

#endif // AMIGA_CUSTOM_CHIPS_BASES_H
