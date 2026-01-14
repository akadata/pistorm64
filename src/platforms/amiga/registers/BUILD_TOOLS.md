# Amiga Register Tools (Pi-side)

Helpers for exercising Amiga hardware via PiStorm bus access.

## Binaries

- `regtool`: peek/poke, memory dump, simple AUD0 audio test, disk LED.
- `ioharness`: interactive I/O toggles (LED, joystick/mouse counters, POTs, keyboard serial stream, disk motor/select/side/step, serial/parallel signals).
- `dumpdisk`: raw floppy track dumper (Paula disk DMA to chip RAM, writes raw track bytes to a file).

## Build

From repo root:
```sh
./build_reg_tools.sh
```
Outputs will land in `build/` (e.g., `build/regtool`, `build/ioharness`, `build/dumpdisk`).

## Usage highlights

### regtool
- Read/write: `--read8 <addr>`, `--write16 <addr> <val>`, `--dump <addr> <len> --width 8|16`
- Audio test: `--audio-test [--audio-addr <hex>] [--audio-len <bytes>] [--audio-period <val>] [--audio-vol <0-64>]`
- Stop audio: `--audio-stop`
- Disk LED: `--disk-led on|off`

### ioharness
- LEDs: `--disk-led on|off`
- Joystick/mouse: `--poll-joy [count] [ms]`
- POTs: `--poll-pot [count] [ms]`
- Keyboard (raw CIAA serial byte stream): `--kbd-poll [count] [ms]`
- Disk control (CIAB): `--disk-motor on|off`, `--disk-select 0-3`, `--disk-side 0|1`, `--disk-step in|out`
- Serial/parallel (CIAB PRA): `--serial-set <dtr> <rts>`, `--serial-read`, `--parallel-read`

### dumpdisk
Raw track dump via Paula disk DMA (MFM-encoded, not ADF-decoded):
```sh
./build/dumpdisk --out dump.raw --drive 0 --tracks 80 --sides 2
```
Notes:
- Dumps ~0x1A00 bytes per track/side from CHIP RAM buffer `0x00040000`.
- Uses DSKSYNC=0x4489, DSKLEN read direction, waits for INTF_DSKBLK.
- Still needs MFM decode to get sector data / ADF.
