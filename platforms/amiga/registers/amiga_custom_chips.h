// SPDX-License-Identifier: MIT
// Main Amiga custom chip register definitions header
// Includes all individual chip register definitions
// Based on Amiga Hardware Reference Manual (ADCD 2.1)

#ifndef AMIGA_CUSTOM_CHIPS_H
#define AMIGA_CUSTOM_CHIPS_H

// Include all individual chip headers
#include "agnus.h"
#include "paula.h"
#include "blitter.h"
#include "denise.h"
#include "cia.h"

// Define base addresses for convenience
#define CUSTOM_CHIP_BASE 0xDFF000
#define CIAA_BASE        0xBFE001
#define CIAB_BASE        0xBFD000

// Common register aliases for easier access
#define CUSTOM_BASE CUSTOM_CHIP_BASE

// Define custom chip register range
#define CUSTOM_CHIP_START 0xDFF000
#define CUSTOM_CHIP_END   0xDFFFFF
#define CUSTOM_CHIP_SIZE  (CUSTOM_CHIP_END - CUSTOM_CHIP_START + 1)

// Define CIA register ranges
#define CIAA_START 0xBFE001
#define CIAA_END   0xBFEF01
#define CIAB_START 0xBFD000
#define CIAB_END   0xBFDF00

// Common bit definitions used across chips
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

// Common register access macros
#define CUSTOM_REG_OFFSET(reg_addr) ((reg_addr) - CUSTOM_CHIP_BASE)
#define CIAA_REG_OFFSET(reg_addr)   ((reg_addr) - CIAA_BASE)
#define CIAB_REG_OFFSET(reg_addr)   ((reg_addr) - CIAB_BASE)

#endif // AMIGA_CUSTOM_CHIPS_H
