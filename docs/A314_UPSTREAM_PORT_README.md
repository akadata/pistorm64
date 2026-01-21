# A314 Upstream Services (PiStorm64) Build and Test

This README covers how to build the Amiga-side binaries with `/opt/amiga/bin/m68k-amigaos-gcc`, run the Pi-side services, and test disk/remotewb/libremote. Paths below assume the repo root is `/home/smalley/pistorm64`.

## Toolchain Prereqs
- `/opt/amiga/bin/m68k-amigaos-gcc`
- `/opt/amiga/bin/vasmm68k_mot` (for `.asm` files)
- Python 3 for `gen_stubs.py` (libremote)

Ensure toolchain is on PATH:
```
export PATH=/opt/amiga/bin:$PATH
```

## Build: disk
```
cd src/a314/software-amiga/disk_pistorm
vasmm68k_mot -Fhunk -quiet romtag.asm -o romtag.o
m68k-amigaos-gcc -m68000 -O2 -c device.c -o device.o
m68k-amigaos-gcc -m68000 -O2 -c debug.c -o debug.o
m68k-amigaos-gcc -m68000 -nostartfiles -o ../a314disk.device romtag.o device.o debug.o -lamiga
```
Install on Amiga:
- `Devs:a314disk.device`
- `Devs:MountList` (merge entry from `src/a314/software-amiga/a314disk-mountlist`)

## Build: remotewb
```
cd src/a314/software-amiga/remotewb_pistorm
vasmm68k_mot -Fhunk -quiet vblank_server.asm -o vblank_server.o
m68k-amigaos-gcc -m68000 -O2 -c remotewb.c -o remotewb.o
m68k-amigaos-gcc -m68000 -o ../remotewb remotewb.o vblank_server.o -lamiga
```
Install on Amiga:
- `C:RemoteWB` (from `src/a314/software-amiga/remotewb` output)

## Build: libremote (bsdsocket.library)
```
cd src/a314/software-amiga/libremote_pistorm
python3 gen_stubs.py bsdsocket
vasmm68k_mot -Fhunk -quiet romtag.asm -o romtag.o
m68k-amigaos-gcc -m68000 -O2 -c library.c -o library.o
m68k-amigaos-gcc -m68000 -nostartfiles -o ../bsdsocket.library romtag.o library.o -lamiga
```
Install on Amiga:
- `L:bsdsocket.library`

## Pi-side services
Enable services by adding entries to `src/a314/files_pi/a314d.conf` (not enabled by default). Example lines:
```
disk       python3 /home/smalley/pistorm64/src/a314/files_pi/disk.py
remotewb   python3 /home/smalley/pistorm64/src/a314/files_pi/remotewb.py
bsl        python3 /home/smalley/pistorm64/src/a314/files_pi/bsdsocket.py
```

Optional dependency for RemoteWB:
```
cd src/a314/files_pi/bpls2gif
python3 setup.py build_ext --inplace
```

## Tests

Disk
1. Start `disk.py` (via a314d on-demand).
2. Use `nc localhost 23890`:
   - `insert 0 /path/to/test.adf`
   - `eject 0`
3. On Amiga, mount `PD0:` and verify read/write to the ADF/HDF.

RemoteWB
1. Start `remotewb.py`.
2. Open `src/a314/files_pi/remotewb_client.html` in a browser.
3. On Amiga, run `RemoteWB` and verify screen updates + input.

Libremote (bsdsocket)
1. Start `bsdsocket.py`.
2. On Amiga, run a minimal bsdsocket client (open socket, gethostname, close).

## Endianness Rules (critical)
- A314 client headers must be explicit little-endian (`<IIB`, `<IIBII`, `<IIBI`).
- Amiga payload structures remain big-endian.
- Avoid `struct.pack('=')` and `struct.unpack('=')`.
