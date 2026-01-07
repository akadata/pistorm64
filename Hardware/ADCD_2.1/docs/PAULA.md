# Paula (Audio, Disk, Serial, Pots)

Sources:
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0060.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0014.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0034.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0037.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node00E0.html.txt
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node0170.html.txt

## Base Address
- Custom chip base: 0xDFF000
- Offsets below are added to 0xDFF000.

## Audio DMA (4 channels)
Each channel has location, length, period, volume, and data registers:

- AUD0LCH 0x0A0 / AUD0LCL 0x0A2
- AUD0LEN 0x0A4
- AUD0PER 0x0A6
- AUD0VOL 0x0A8
- AUD0DAT 0x0AA

- AUD1LCH 0x0B0 / AUD1LCL 0x0B2
- AUD1LEN 0x0B4
- AUD1PER 0x0B6
- AUD1VOL 0x0B8
- AUD1DAT 0x0BA

- AUD2LCH 0x0C0 / AUD2LCL 0x0C2
- AUD2LEN 0x0C4
- AUD2PER 0x0C6
- AUD2VOL 0x0C8
- AUD2DAT 0x0CA

- AUD3LCH 0x0D0 / AUD3LCL 0x0D2
- AUD3LEN 0x0D4
- AUD3PER 0x0D6
- AUD3VOL 0x0D8
- AUD3DAT 0x0DA

Notes:
- AUDxLCH/LCL hold the 18-bit start address for DMA audio data.
- DMA must be enabled via DMACON (DMAEN + AUDxEN bits).

## Disk DMA / Disk Control
- DSKDATR 0x008 (ER) Disk data early read (dummy address)
- DSKBYTR 0x01A Disk data byte and status
- DSKPTH  0x020 / DSKPTL 0x022 Disk pointer
- DSKLEN  0x024 Disk length
- DSKDAT  0x026 Disk DMA data write
- DSKSYNC 0x07E Disk sync register

## Serial Port
- SERDATR 0x018 Serial port data and status read
- SERDAT  0x030 Serial port data and stop bits write
- SERPER  0x032 Serial port period and control

## Pot / Input
- POT0DAT 0x012 Pot counter pair 0 (vertical/horizontal)
- POT1DAT 0x014 Pot counter pair 1 (vertical/horizontal)
- POTGOR  0x016 Pot port data read (formerly POTINP)
- POTGO   0x034 Pot port data write/start

## Interrupt Control (Paula)
- INTENAR 0x01C Read interrupt enable bits
- INTREQR 0x01E Read interrupt request bits
- INTENA  0x09A Write interrupt enable bits
- INTREQ  0x09C Write interrupt request bits

## DMA Control
- DMACONR 0x002 Read DMA control and blitter status
- DMACON  0x096 Write DMA control (set/clear)

DMACON bit summary (see source):
- DMAEN (bit 9) is the master DMA enable.
- AUD0EN..AUD3EN (bits 0..3) enable audio channels.
- DSKEN (bit 4) enables disk DMA.
