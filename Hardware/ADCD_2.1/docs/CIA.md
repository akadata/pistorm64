# CIA (8520) Reference

Sources:
- Hardware/ADCD_2.1/_txt/Includes_and_Autodocs_2._guide/node00CA.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node00D3.html.txt

## Base Addresses
- CIAA base: 0xBFE001 (odd address, low byte)
- CIAB base: 0xBFD000 (even address, high byte)

The memory map entry notes CIAA/CIAB register spaces at:
- 0xBFE001 - 0xBFEF01 (CIAA, odd addresses)
- 0xBFD000 - 0xBFDF00 (CIAB, even addresses)

## Register Offsets (CIAA/CIAB)
Offsets are added to the CIA base.

- CIAPRA: 0x0000
- CIAPRB: 0x0100
- CIADDRA: 0x0200
- CIADDRB: 0x0300
- CIATALO: 0x0400
- CIATAHI: 0x0500
- CIATBLO: 0x0600
- CIATBHI: 0x0700
- CIATODLOW: 0x0800
- CIATODMID: 0x0900
- CIATODHI: 0x0A00
- CIASDR: 0x0C00
- CIAICR: 0x0D00
- CIACRA: 0x0E00
- CIACRB: 0x0F00

## Interrupt Control Register (CIAICR) Bits
Bit numbers:
- 0: Timer A
- 1: Timer B
- 2: Alarm
- 3: Serial Port
- 4: Flag
- 7: IR (interrupt request)
- 7: SET/CLR (set/clear control)

## Control Register A (CIACRA) Bits
Bit numbers:
- 0: START
- 1: PBON
- 2: OUTMODE
- 3: RUNMODE
- 4: LOAD
- 5: INMODE
- 6: SPMODE
- 7: TODIN

## Control Register B (CIACRB) Bits
Bit numbers:
- 0: START
- 1: PBON
- 2: OUTMODE
- 3: RUNMODE
- 4: LOAD
- 5: INMODE0
- 6: INMODE1
- 7: ALARM

## Selected Port Bit Definitions
CIAB Port A (0xBFD000) - serial/printer control:
- 7: COMDTR (serial Data Terminal Ready)
- 6: COMRTS (serial Request to Send)
- 5: COMCD (serial Carrier Detect)
- 4: COMCTS (serial Clear to Send)
- 3: COMDSR (serial Data Set Ready)
- 2: PRTRSEL (printer SELECT)
- 1: PRTRPOUT (printer paper out)
- 0: PRTRBUSY (printer busy)

CIAB Port B (0xBFD100) - disk control:
- 7: DSKMOTOR
- 6: DSKSEL3
- 5: DSKSEL2
- 4: DSKSEL1
- 3: DSKSEL0
- 2: DSKSIDE
- 1: DSKDIREC
- 0: DSKSTEP

Notes:
- CIAA base is an odd address; CIAB base is an even address. Access width and address alignment are important.
