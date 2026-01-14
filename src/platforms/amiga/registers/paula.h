// SPDX-License-Identifier: MIT
// PAULA chip register definitions for Amiga
// Based on Amiga Hardware Reference Manual (ADCD 2.1)

#ifndef PAULA_H
#define PAULA_H

#include "amiga_custom_chips.h"

// Audio Channel 0 Registers
#define AUD0LCH  (PAULA_BASE + 0x0A0)  // Audio channel 0 location high
#define AUD0LCL  (PAULA_BASE + 0x0A2)  // Audio channel 0 location low
#define AUD0LEN  (PAULA_BASE + 0x0A4)  // Audio channel 0 length
#define AUD0PER  (PAULA_BASE + 0x0A6)  // Audio channel 0 period
#define AUD0VOL  (PAULA_BASE + 0x0A8)  // Audio channel 0 volume
#define AUD0DAT  (PAULA_BASE + 0x0AA)  // Audio channel 0 data

// Audio Channel 1 Registers
#define AUD1LCH  (PAULA_BASE + 0x0B0)  // Audio channel 1 location high
#define AUD1LCL  (PAULA_BASE + 0x0B2)  // Audio channel 1 location low
#define AUD1LEN  (PAULA_BASE + 0x0B4)  // Audio channel 1 length
#define AUD1PER  (PAULA_BASE + 0x0B6)  // Audio channel 1 period
#define AUD1VOL  (PAULA_BASE + 0x0B8)  // Audio channel 1 volume
#define AUD1DAT  (PAULA_BASE + 0x0BA)  // Audio channel 1 data

// Audio Channel 2 Registers
#define AUD2LCH  (PAULA_BASE + 0x0C0)  // Audio channel 2 location high
#define AUD2LCL  (PAULA_BASE + 0x0C2)  // Audio channel 2 location low
#define AUD2LEN  (PAULA_BASE + 0x0C4)  // Audio channel 2 length
#define AUD2PER  (PAULA_BASE + 0x0C6)  // Audio channel 2 period
#define AUD2VOL  (PAULA_BASE + 0x0C8)  // Audio channel 2 volume
#define AUD2DAT  (PAULA_BASE + 0x0CA)  // Audio channel 2 data

// Audio Channel 3 Registers
#define AUD3LCH  (PAULA_BASE + 0x0D0)  // Audio channel 3 location high
#define AUD3LCL  (PAULA_BASE + 0x0D2)  // Audio channel 3 location low
#define AUD3LEN  (PAULA_BASE + 0x0D4)  // Audio channel 3 length
#define AUD3PER  (PAULA_BASE + 0x0D6)  // Audio channel 3 period
#define AUD3VOL  (PAULA_BASE + 0x0D8)  // Audio channel 3 volume
#define AUD3DAT  (PAULA_BASE + 0x0DA)  // Audio channel 3 data

// Disk DMA Registers
#define DSKDATR  (PAULA_BASE + 0x008)  // Disk data early read (dummy)
#define DSKBYTR  (PAULA_BASE + 0x01A)  // Disk data byte and status
#define DSKPTH   (PAULA_BASE + 0x020)  // Disk pointer high
#define DSKPTL   (PAULA_BASE + 0x022)  // Disk pointer low
#define DSKLEN   (PAULA_BASE + 0x024)  // Disk length
#define DSKDAT   (PAULA_BASE + 0x026)  // Disk DMA data write
#define DSKSYNC  (PAULA_BASE + 0x07E)  // Disk sync register

// Serial Port Registers
#define SERDATR  (PAULA_BASE + 0x018)  // Serial port data and status read
#define SERDAT   (PAULA_BASE + 0x030)  // Serial port data and stop bits write
#define SERPER   (PAULA_BASE + 0x032)  // Serial port period and control

// Pot/Counter Registers
#define POT0DAT  (PAULA_BASE + 0x012)  // Pot counter pair 0 (vertical/horizontal)
#define POT1DAT  (PAULA_BASE + 0x014)  // Pot counter pair 1 (vertical/horizontal)
#define POTGOR   (PAULA_BASE + 0x016)  // Pot port data read
#define POTGO    (PAULA_BASE + 0x034)  // Pot port data write/start

// Interrupt Control Registers
#define INTENAR  (PAULA_BASE + 0x01C)  // Interrupt enable read
#define INTREQR  (PAULA_BASE + 0x01E)  // Interrupt request read
#define INTENA   (PAULA_BASE + 0x09A)  // Interrupt enable write
#define INTREQ   (PAULA_BASE + 0x09C)  // Interrupt request write

// Audio/Disk Control Register
#define ADKCONR  (PAULA_BASE + 0x010)  // Audio/disk control read
#define ADKCON   (PAULA_BASE + 0x09E)  // Audio/disk control write

// Audio Control Bits (ADKCON/ADKCONR)
#define ADKF_SETCLR   0x8000  // Set/Clear control bit
#define ADKF_MFMPREC  0x4000  // MFM compatibility mode
#define ADKF_WORDSYNC 0x2000  // Word sync mode
#define ADKF_UARTBRK  0x1000  // UART break set
#define ADKF_MSBSYNC  0x0800  // MSB sync mode
#define ADKF_FAST     0x0400  // Fast serial clock
#define ADKF_PRECOMP0 0x0200  // Precompensation bit 0
#define ADKF_PRECOMP1 0x0100  // Precompensation bit 1
#define ADKF_PRECOMP2 0x0080  // Precompensation bit 2
#define ADKF_SERDCY0  0x0040  // Serial delay bit 0
#define ADKF_SERDCY1  0x0020  // Serial delay bit 1
#define ADKF_CH3PEN   0x0010  // Channel 3 in use for input
#define ADKF_CH2PEN   0x0008  // Channel 2 in use for input
#define ADKF_CH1PEN   0x0004  // Channel 1 in use for input
#define ADKF_CH0PEN   0x0002  // Channel 0 in use for input
#define ADKF_ADLNK    0x0001  // Audio channel linking

// ADKCON bit numbers
#define ADKB_SETCLR   15
#define ADKB_MFMPREC  14
#define ADKB_WORDSYNC 13
#define ADKB_UARTBRK  12
#define ADKB_MSBSYNC  11
#define ADKB_FAST     10
#define ADKB_PRECOMP0 9
#define ADKB_PRECOMP1 8
#define ADKB_PRECOMP2 7
#define ADKB_SERDCY0  6
#define ADKB_SERDCY1  5
#define ADKB_CH3PEN   4
#define ADKB_CH2PEN   3
#define ADKB_CH1PEN   2
#define ADKB_CH0PEN   1
#define ADKB_ADLNK    0

// Interrupt Bits (INTENA/INTREQ) — matches Hardware Manual table
#define INTF_SETCLR   0x8000  // Set/Clear control bit
#define INTF_INTEN    0x4000  // Master interrupt enable (no request)
#define INTF_EXTER    0x2000  // External interrupt
#define INTF_DSKSYN   0x1000  // Disk sync pattern matched
#define INTF_RBF      0x0800  // Serial receive buffer full
#define INTF_AUD3     0x0400  // Audio channel 3 block finished
#define INTF_AUD2     0x0200  // Audio channel 2 block finished
#define INTF_AUD1     0x0100  // Audio channel 1 block finished
#define INTF_AUD0     0x0080  // Audio channel 0 block finished
#define INTF_BLIT     0x0040  // Blitter finished
#define INTF_VERTB    0x0020  // Start of vertical blank
#define INTF_COPER    0x0010  // Copper
#define INTF_PORTS    0x0008  // I/O ports and timers
#define INTF_SOFTINT  0x0004  // Software interrupt
#define INTF_DSKBLK   0x0002  // Disk block finished
#define INTF_TBE      0x0001  // Serial transmit buffer empty

// Backward-compatible aliases (previous names)
#define INTF_DSKSYNC INTF_DSKSYN
#define INTF_SOFTWARE INTF_SOFTINT

// INTF bit numbers
#define INTB_SETCLR   15
#define INTB_INTEN    14
#define INTB_EXTER    13
#define INTB_DSKSYN   12
#define INTB_RBF      11
#define INTB_AUD3     10
#define INTB_AUD2     9
#define INTB_AUD1     8
#define INTB_AUD0     7
#define INTB_BLIT     6
#define INTB_VERTB    5
#define INTB_COPER    4
#define INTB_PORTS    3
#define INTB_SOFTINT  2
#define INTB_DSKBLK   1
#define INTB_TBE      0

#endif // PAULA_H
