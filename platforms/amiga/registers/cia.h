// SPDX-License-Identifier: MIT
// CIA (Complex Interface Adapter) chip register definitions for Amiga
// Based on Amiga Hardware Reference Manual (ADCD 2.1)

#ifndef CIA_H
#define CIA_H

// CIAA base address (odd addresses, low byte)
#define CIAA_BASE 0xBFE001

// CIAB base address (even addresses, high byte)
#define CIAB_BASE 0xBFD000

// CIA Register Offsets (added to base address)
// Note: CIA registers are spaced 0x100 apart in the 68k address map.
#define CIAPRA    0x0000  // CIA Port A data register
#define CIAPRB    0x0100  // CIA Port B data register
#define CIADDRA   0x0200  // CIA Port A data direction register
#define CIADDRB   0x0300  // CIA Port B data direction register
#define CIATALO   0x0400  // CIA Timer A low byte
#define CIATAHI   0x0500  // CIA Timer A high byte
#define CIATBLO   0x0600  // CIA Timer B low byte
#define CIATBHI   0x0700  // CIA Timer B high byte
#define CIATODLOW 0x0800  // CIA Time Of Day low
#define CIATODMID 0x0900  // CIA Time Of Day mid
#define CIATODHI  0x0A00  // CIA Time Of Day high
#define CIASDR    0x0C00  // CIA Serial Data Register
#define CIAICR    0x0D00  // CIA Interrupt Control Register
#define CIACRA    0x0E00  // CIA Control Register A
#define CIACRB    0x0F00  // CIA Control Register B

// CIAA Register Addresses
#define CIAAPRA   (CIAA_BASE + CIAPRA)    // CIA A Port A data
#define CIAAPRB   (CIAA_BASE + CIAPRB)    // CIA A Port B data
#define CIAADDR_A (CIAA_BASE + CIADDRA)   // CIA A Port A direction
#define CIAADDR_B (CIAA_BASE + CIADDRB)   // CIA A Port B direction
#define CIAATALO  (CIAA_BASE + CIATALO)   // CIA A Timer A low
#define CIAATAHI  (CIAA_BASE + CIATAHI)   // CIA A Timer A high
#define CIAATBLO  (CIAA_BASE + CIATBLO)   // CIA A Timer B low
#define CIAATBHI  (CIAA_BASE + CIATBHI)   // CIA A Timer B high
#define CIAATODLOW (CIAA_BASE + CIATODLOW) // CIA A Time Of Day low
#define CIAATODMID (CIAA_BASE + CIATODMID) // CIA A Time Of Day mid
#define CIAATODHI  (CIAA_BASE + CIATODHI)  // CIA A Time Of Day high
#define CIAASDR   (CIAA_BASE + CIASDR)    // CIA A Serial Data Register
#define CIAAICR   (CIAA_BASE + CIAICR)    // CIA A Interrupt Control Register
#define CIAACRA   (CIAA_BASE + CIACRA)    // CIA A Control Register A
#define CIAACRB   (CIAA_BASE + CIACRB)    // CIA A Control Register B

// CIAB Register Addresses
#define CIABPRA   (CIAB_BASE + CIAPRA)    // CIA B Port A data
#define CIABPRB   (CIAB_BASE + CIAPRB)    // CIA B Port B data
#define CIABDDRA  (CIAB_BASE + CIADDRA)   // CIA B Port A direction
#define CIABDDRB  (CIAB_BASE + CIADDRB)   // CIA B Port B direction
#define CIABTALO  (CIAB_BASE + CIATALO)   // CIA B Timer A low
#define CIABTAHI  (CIAB_BASE + CIATAHI)   // CIA B Timer A high
#define CIABTBLO  (CIAB_BASE + CIATBLO)   // CIA B Timer B low
#define CIABTBHI  (CIAB_BASE + CIATBHI)   // CIA B Timer B high
#define CIABTODLOW (CIAB_BASE + CIATODLOW) // CIA B Time Of Day low
#define CIABTODMID (CIAB_BASE + CIATODMID) // CIA B Time Of Day mid
#define CIABTODHI  (CIAB_BASE + CIATODHI)  // CIA B Time Of Day high
#define CIABSDR   (CIAB_BASE + CIASDR)    // CIA B Serial Data Register
#define CIABICR   (CIAB_BASE + CIAICR)    // CIA B Interrupt Control Register
#define CIABCRA   (CIAB_BASE + CIACRA)    // CIA B Control Register A
#define CIABCRB   (CIAB_BASE + CIACRB)    // CIA B Control Register B

// CIA Interrupt Control Register (CIAICR) bits
#define CIAICR_TAIRQ   0x01  // Timer A interrupt
#define CIAICR_TBIRQ   0x02  // Timer B interrupt
#define CIAICR_ALRMIRQ 0x04  // Alarm interrupt
#define CIAICR_SPIOIRQ 0x08  // Serial port interrupt
#define CIAICR_FLGIRQ  0x10  // Flag pin interrupt
#define CIAICR_IRRQ    0x80  // Interrupt request (output)

// CIA Control Register A (CIACRA) bits
#define CIACRA_START    0x01  // Start/stop timer
#define CIACRA_PBON     0x02  // Timer A controls port B bit 6
#define CIACRA_OUTMODE  0x04  // Output mode
#define CIACRA_RUNMODE  0x08  // Run mode
#define CIACRA_LOAD     0x10  // Load timer from latch
#define CIACRA_INMODE   0x20  // Input mode
#define CIACRA_SPMODE   0x40  // Serial port mode
#define CIACRA_TODIN    0x80  // Time of day/increment mode

// CIA Control Register B (CIACRB) bits
#define CIACRB_START    0x01  // Start/stop timer
#define CIACRB_PBON     0x02  // Timer B controls port B bit 7
#define CIACRB_OUTMODE  0x04  // Output mode
#define CIACRB_RUNMODE  0x08  // Run mode
#define CIACRB_LOAD     0x10  // Load timer from latch
#define CIACRB_INMODE0  0x20  // Input mode bit 0
#define CIACRB_INMODE1  0x40  // Input mode bit 1
#define CIACRB_ALARM    0x80  // Alarm/TOD mode

// CIA Control Register A bit positions
#define CIACRAB_START    0
#define CIACRAB_PBON     1
#define CIACRAB_OUTMODE  2
#define CIACRAB_RUNMODE  3
#define CIACRAB_LOAD     4
#define CIACRAB_INMODE   5
#define CIACRAB_SPMODE   6
#define CIACRAB_TODIN    7

// CIA Control Register B bit positions
#define CIACRBB_START    0
#define CIACRBB_PBON     1
#define CIACRBB_OUTMODE  2
#define CIACRBB_RUNMODE  3
#define CIACRBB_LOAD     4
#define CIACRBB_INMODE0  5
#define CIACRBB_INMODE1  6
#define CIACRBB_ALARM    7

// Selected Port Bit Definitions

// CIAB Port A (0xBFD000) - serial/printer control:
#define CIAB_COMDTR    0x80  // Serial Data Terminal Ready
#define CIAB_COMRTS    0x40  // Serial Request to Send
#define CIAB_COMCD     0x20  // Serial Carrier Detect
#define CIAB_COMCTS    0x10  // Serial Clear to Send
#define CIAB_COMDSR    0x08  // Serial Data Set Ready
#define CIAB_PRTRSEL   0x04  // Printer SELECT
#define CIAB_PRTRPOUT  0x02  // Printer paper out
#define CIAB_PRTRBUSY  0x01  // Printer busy

// CIAB Port B (0xBFD100) - disk control:
#define CIAB_DSKMOTOR  0x80  // Disk motor
#define CIAB_DSKSEL3   0x40  // Drive select 3
#define CIAB_DSKSEL2   0x20  // Drive select 2
#define CIAB_DSKSEL1   0x10  // Drive select 1
#define CIAB_DSKSEL0   0x08  // Drive select 0
#define CIAB_DSKSIDE   0x04  // Disk side
#define CIAB_DSKDIREC  0x02  // Disk direction
#define CIAB_DSKSTEP   0x01  // Disk step

// CIAA Port A (CIAAPRA) - system control/input (bit numbers from hardware/cia.h)
#define CIAA_GAMEPORT1 0x80  // Gameport 1, pin 6 (fire button*)
#define CIAA_GAMEPORT0 0x40  // Gameport 0, pin 6 (fire button*)
#define CIAA_DSKRDY    0x20  // Disk ready*
#define CIAA_DSKTRACK0 0x10  // Disk on track 00*
#define CIAA_DSKPROT   0x08  // Disk write protect*
#define CIAA_DSKCHANGE 0x04  // Disk change*
#define CIAA_LED       0x02  // LED control (0 == bright/on)
#define CIAA_OVERLAY   0x01  // Memory overlay bit

// CIA Port B (CIAAPRB) - keyboard matrix:
// Bits 7-0 represent keyboard matrix columns

#endif // CIA_H
