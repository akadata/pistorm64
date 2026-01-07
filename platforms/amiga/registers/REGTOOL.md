# regtool (Amiga Register Peek/Poke)

Build:
```
gcc -O2 -Wall -Wextra -I./ platforms/amiga/registers/regtool.c gpio/ps_protocol.c gpio/rpi_peri.c -o regtool
```

Usage:
```
./regtool --read8  0xBFE001
./regtool --read16 0xDFF002
./regtool --dump 0xDFF000 0x40 --width 16

./regtool --force --write16 0xDFF096 0x8200

./regtool --force --audio-test --audio-addr 0x00010000 --audio-len 256 --audio-period 200 --audio-vol 64
./regtool --force --audio-stop

./regtool --force --kbd-led on
./regtool --force --kbd-led off
```

Notes:
- CIAA uses odd addresses (0xBFE001 base); CIAB uses even addresses (0xBFD000 base).
- CIA registers are spaced 0x100 apart. Use 8-bit accesses for CIA.
- Writes require `--force` because they can disrupt the running system.
- Audio test writes a simple square wave into chip RAM and enables AUD0 DMA.

Common addresses:
- DMACONR: 0xDFF002
- DMACON:  0xDFF096
- INTENA:  0xDFF09A
- INTREQ:  0xDFF09C
- CIAA PRA: 0xBFE001
- CIAB PRA: 0xBFD000
