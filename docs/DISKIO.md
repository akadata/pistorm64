# DISKIO.md - PiStorm Floppy Disk I/O System Documentation

## Overview
This document describes the PiStorm floppy disk I/O system, which enables communication between a Raspberry Pi and an Amiga floppy drive via GPIO pins. The system allows for reading and writing Amiga floppy disk data through direct hardware control.

## Hardware Interface
### Pin Mapping (FX2 Logic Analyzer)
- D0 → Floppy pin 10 /SEL0 (DS0) - Drive Select 0
- D1 → Floppy pin 16 /MTR0 (Motor) - Motor Enable
- D2 → Floppy pin 18 DIR - Direction
- D3 → Floppy pin 20 STEP - Step
- D4 → Floppy pin 30 /DKRD (RDATA) - Read Data
- D5 → Floppy pin 8 INDEX - Index
- D6 → Floppy pin 32 SIDE - Side Select
- D7 → Floppy pin 34 /RDY - Ready Signal

### GPIO Pin Mapping (Raspberry Pi)
- PIN_TXN_IN_PROGRESS (GPIO 0) - Transaction in progress
- PIN_IPL_ZERO (GPIO 1) - Interrupt Priority Level
- PIN_A0 (GPIO 2) - Address bit 0
- PIN_A1 (GPIO 3) - Address bit 1
- PIN_CLK (GPIO 4) - Clock (GPCLK0 at 200MHz)
- PIN_RESET (GPIO 5) - Reset
- PIN_RD (GPIO 6) - Read
- PIN_WR (GPIO 7) - Write
- PIN_D(x) (GPIO 8-23) - Data bus (16-bit)

## CIA Registers (Complex Interface Adapter)
### CIAB Port B (Disk Control)
- CIAB_DSKMOTOR (bit 7) - Motor control (active low)
- CIAB_DSKSEL3 (bit 6) - Drive select 3
- CIAB_DSKSEL2 (bit 5) - Drive select 2
- CIAB_DSKSEL1 (bit 4) - Drive select 1
- CIAB_DSKSEL0 (bit 3) - Drive select 0 (active low)
- CIAB_DSKSIDE (bit 2) - Side select
- CIAB_DSKDIREC (bit 1) - Direction
- CIAB_DSKSTEP (bit 0) - Step

### CIAA Port A (Status)
- CIAA_DSKRDY (bit 5) - Disk ready (active low)
- CIAA_DSKTRACK0 (bit 4) - Track 0 sensor (active low)
- CIAA_DSKPROT (bit 3) - Write protect
- CIAA_DSKCHANGE (bit 2) - Disk change

## Communication Protocol
### Initialization Sequence
1. Setup GPIO pins for communication
2. Configure GPCLK0 for 200MHz clock on GPIO4
3. Initialize pins to INPUT mode
4. Call `ps_reset_state_machine()` to reset the state machine
5. Call `ps_pulse_reset()` to pulse the reset line

### Motor Control
- Motor ON: Clear CIAB_DSKMOTOR bit (0 = motor on, active low)
- Motor OFF: Set CIAB_DSKMOTOR bit (1 = motor off)

### Head Movement
1. Select direction (inward/outward)
2. Pulse STEP line low briefly
3. Allow settle time between steps

### Track Seeking
- Move head step-by-step until TRK0 sensor indicates track 0
- Use direction control to move inward or outward
- Monitor TRK0 and RDY signals during movement

### DMA Operations
1. Configure disk DMA registers (DSKPTH, DSKPTL, DSKLEN)
2. Set ADKCON for MFM mode
3. Enable disk DMA via DMACON register
4. Start DMA by writing to DSKLEN register twice
5. Wait for DSKBLK interrupt or timeout

## Register Tools Architecture
### dumpdisk.c
- Main program for reading floppy disk tracks
- Implements motor control, head movement, and DMA operations
- Creates raw track dumps of Amiga MFM data
- Handles drive readiness and track seeking

### Key Functions
- `ensure_motor_on_with_drive_select()` - Properly initializes motor and drive selection
- `seek_track0()` - Moves head to track 0 using sensor feedback
- `read_track_raw()` - Performs DMA to read raw track data
- `wait_for_ready()` - Waits for drive to become ready

## Why It Works
### Hardware Synchronization
The system works because:
1. Proper clock synchronization via GPCLK0 (200MHz)
2. Correct timing for Amiga bus protocol
3. Proper initialization sequence matching emulator behavior
4. Accurate GPIO pin control for direct hardware access

### State Management
- Proper reset sequence ensures clean communication state
- Drive selection and motor control are properly coordinated
- Head movement is verified via sensors before DMA
- DMA operations are properly synchronized with Amiga custom chips

### Error Handling
- Drive readiness verification before operations
- Recovery mechanisms when drive becomes unready
- Proper cleanup after operations

## Troubleshooting
### Common Issues
- Drive not ready after head movement
- Motor control not functioning
- DMA timeouts
- GPIO communication failures

### Solutions
- Ensure proper reset sequence is called
- Verify GPIO pin mappings
- Check for emulator conflicts when running simultaneously
- Allow adequate settling time after head movements

## Performance Notes
- Standard DD floppy: ~0x1A00 bytes per track per side
- Typical track size: 6,656 bytes (0x1A00)
- Requires ~500ms spin-up time for motor
- Head movement requires settle time between operations