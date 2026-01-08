# Regtool Amiga Hardware Peek/Poke How-To

This tool uses the PiStorm GPIO protocol to read and write Amiga bus registers from the Pi.

Build (from repo root):
```
gcc -O2 -Wall -Wextra -I./ platforms/amiga/registers/regtool.c gpio/ps_protocol.c gpio/rpi_peri.c -o regtool
```

## Basic Reads
Read a CIA register (8-bit):
```
./regtool --read8 0xBFE001
```

Read a custom register (16-bit):
```
./regtool --read16 0xDFF002
```

Dump a block (16-bit words):
```
./regtool --dump 0xDFF000 0x40 --width 16
```

## Writes (Requires --force)
Write a custom register:
```
./regtool --force --write16 0xDFF096 0x8200
```

Toggle disk LED (CIAA Port A, active low):
```
./regtool --force --disk-led on
./regtool --force --disk-led off
./regtool --force --power-led on
./regtool --force --power-led off

Notes:
- Disk LED is active-low on CIAA Port A bit 1. The tool prints DDRA/PRA readback.
- If the LED does not appear to change, the OS or disk DMA may be immediately
  overriding the bit. Try again with minimal disk activity.
```

## Audio Test (AUD0)
Writes a small square wave into chip RAM and enables audio DMA:
```
./regtool --force --audio-test --audio-addr 0x00010000 --audio-len 256 --audio-period 200 --audio-vol 64
```
Stop audio DMA:
```
./regtool --force --audio-stop
```

## Addressing Rules (CIA vs Custom)
- Custom chip registers: 0xDFF000 - 0xDFFFFF, 16-bit register map.
- CIAA base: 0xBFE001 (odd address, low byte lane).
- CIAB base: 0xBFD000 (even address, high byte lane).
- CIA registers are spaced by 0x100 offsets (0x0000, 0x0100, ... 0x0F00).
- Use 8-bit access for CIA.

Examples:
- CIAA PRA: 0xBFE001
- CIAA PRB: 0xBFE101
- CIAB PRA: 0xBFD000
- CIAB PRB: 0xBFD100

## What You Can Do With It
Safe reads:
- Identify chip revisions (DENISEID, VPOSR).
- Inspect DMA status (DMACONR) and interrupt state (INTREQR).
- Read CIA timer and port state.

Careful writes (can affect the running system):
- Enable/disable DMA channels (DMACON).
- Manipulate audio channel registers (AUDx*).
- Adjust display window registers (DIWSTRT/DIWSTOP).
- Toggle CIA port bits (keyboard LED, disk control lines).

## Safety Notes
- Writes can crash or reset the Amiga. Use `--force` only when you intend to modify hardware state.
- CIA ICR reads clear interrupt flags; avoid rapid polling of CIAICR.
- Avoid writing to undefined bits or registers.
- For chip RAM writes, keep addresses in the chip range and aligned to the access width.
- A500 power LED is hardware-wired; `--power-led` is a no-op.

## Next Ideas (If You Want)
- Sprite helper: load sprite data to chip RAM and set SPRx pointers.
- Copper list helper: write a simple Copper list and enable COPEN DMA.
- Keyboard matrix probe (CIAA Port B) with safe read-only scanning.
- Burst register dumps with labeled output (decode list using the register headers).
