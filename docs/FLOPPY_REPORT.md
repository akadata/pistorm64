# Floppy Debug Report

## Probe wiring (Sigrok FX2 LA, 24 MHz)

- CH0: pin 10 SEL0 (DS0)
- CH1: pin 16 MTR0 (motor enable)
- CH2: pin 18 DIR
- CH3: pin 20 STEP
- CH4: pin 30 DKRD (RDATA)
- CH5: pin 8 INDEX
- CH6: pin 32 SIDE
- CH7: pin 34 RDY (or CHNG/RDY as present)
- Ground: any odd pin (e.g., 17)

## Software bit mapping (Amiga / X-Copy)

- CIAB PB7: motor (active low)
- CIAB PB3: drive select 0 (active low)
- CIAB PB2: side select
- CIAB PB1: direction
- CIAB PB0: step (pulse low)

## What we tried

- Updated `dumpdisk` to use the Amiga/X-Copy mapping (PB7 motor, PB3 DS0, PB2 side, PB1 dir, PB0 step).
- Added helper scripts to capture PRB sweeps with sigrok (`sweep_prb_remote.sh`, `decode_prb_sweep.py`) and decoded captures.
- Ran multiple short captures (24 MHz) during `dumpdisk` and manual PRB toggles.
- Tested `dumpdisk` with `--tracks 1 --sides 1` after rebuilding tools.

## Observations

- Probes on SEL0/SIDE/DIR/STEP (CH0/CH6/CH2/CH3) show expected activity; RDATA (CH4) stays mostly idle during `dumpdisk`.
- Motor pin (CH1/pin16) has shown at most a single edge in sweeps; in many captures it never toggles. This suggests motor is not being driven to the drive by any CIAB PB bit in practice.
- Pre-DMA status often good after seek (RDY=1/TRK0=1, PRB ~0x70), but before DMA, RDY/TRK0 drop and PRB/DDR drift (e.g., PRB=0xFF, DDRB=0x70), then DMA times out (INTREQ DSKBLK never set).
- `dumpdisk` run (latest):
  ```
  after motor on: PRA=0xC7 (RDY=1 TRK0=1) PRB=0x70 DDRB=0xFF
  after seek to track0: PRA=0xC7 (RDY=1 TRK0=1) PRB=0x73 DDRB=0xFF
  WARN: drive not ready after 200ms
  before DMA: PRA=0xFF (RDY=0 TRK0=0) PRB=0xFF DDRB=0x70
  DMA armed ... polling ... timeout
  ```
- Earlier probe mapping (from bit sweeps) shows PB3→CH0 (SEL0), PB2→CH6 (SIDE), PB1→CH2 (DIR), PB0→CH3 (STEP). Motor (PB7) has not been observed on CH1.

## Conclusions / next steps

- Drive select/step/side/dir are reaching the drive; motor likely is not. Without motor asserted, RDY/TRK0 drop and DMA times out.
- Verify wiring of motor: PB7 (or another PB bit) must reach pin16. If it is wired, confirm on the LA that toggling PB7 (e.g., writing PRB=0x7F/0xFF) actually toggles CH1.
- Once motor is observed on CH1, rerun `dumpdisk` with the current Amiga mapping. If RDY/TRK0 stay asserted before DMA, RDATA should appear on CH4 and DMA should complete.
