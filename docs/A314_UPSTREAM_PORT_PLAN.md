# A314 Upstream Forward Port Plan (PiStorm64)

## Scope
- Working baseline: `src/a314/` (PiStorm64). Backup: `src/a314.orig/`.
- Staging reference: `src/a314/import/upstream/` (A501 expansion-port hardware).
- Primary targets: disk, remotewb, libremote.
- Secondary reference only: a314d, a314device.

## Task 1: Device Layer Comparison

### a314device (Amiga-side)

PiStorm64 (`src/a314/a314device/`)
- Single transport model tied to PiStorm shared memory and emulator integration.
- `a314driver.c` implements the driver task for the CPU-socket transport.
- `int_server.asm` signals based on PiStorm shared event flags.
- `device.c` exports only basic device vectors plus `translate_address_a314`.
- `proto_a314.h` only exposes `TranslateAddressA314` (identity macro).

Upstream (`src/a314/import/upstream/a314device/`)
- Multiple models (TD/FE/CP) with separate interrupt servers and bus interfaces.
- Adds `driver_task.c`, `pi_if_*.c`, `int_server_*.asm`, `cmem.*`, `check_a314_mapping.asm`, `handshake_fe.h`.
- `device.c` exports additional LVOs: alloc/free/read/write mem, translate address.
- `memory_allocator.*` implements 64K A314 RAM allocator for CP model.
- `config_file.*` parses `DEVS:a314.config` (clockport address, interrupt line).

A501-specific (do not port)
- `pi_if_td.c`, `pi_if_fe.c`, `pi_if_cp.c` (GPIO/clockport register access).
- `int_server_*` for TD/FE/CP models (hardware IRQ lines).
- `cmem.*` and `check_a314_mapping.asm` (SPI SRAM / mapping probe).
- `handshake_fe.h` and CP config parsing (`config_file.*`).

Transport-agnostic candidates (already present or low value)
- `sockets.*` protocol logic is common but already present in PiStorm.
- `debug.*` is generic logging but uses upstream process logger and VBCC pragmas.

### a314d (Pi-side)

PiStorm64 (`src/a314/a314.cc`)
- Integrated into the emulator; no GPIO/SPI.
- Uses `ps_read_8/ps_write_8` and mapped memory regions.
- Explicit little-endian framing for client message headers (`<IIB`).
- On-demand service spawning from `a314d.conf`.

Upstream (`src/a314/import/upstream/a314d/a314d.cc`)
- Hardware models TD/FE/CP with SPI/GPIO, interrupts, and device overlays.
- Uses `/dev/spidev*`, `/dev/gpiomem`, `spi-a314-overlay.dts`.
- No explicit LE header framing in the Python helper (`a314d.py`).

Conclusion
- Do not port upstream a314d or a314device hardware backends to PiStorm64.
- Only transport-agnostic helpers (Python client helper) are useful, but must be endian-fixed.

## Task 2: Service Feasibility and Port Plan

### 2A: disk

Feasibility verdict: Feasible with minor changes.

Pi-side (copy to `src/a314/files_pi/`)
- `src/a314/files_pi/disk.py`
- `src/a314/files_pi/disk.conf`
- `src/a314/files_pi/a314d.py` (shared helper)

Amiga-side (copy to `src/a314/software-amiga/`)
- `src/a314/software-amiga/disk_pistorm/device.c`
- `src/a314/software-amiga/disk_pistorm/debug.c`
- `src/a314/software-amiga/disk_pistorm/debug.h`
- `src/a314/software-amiga/disk_pistorm/romtag.asm`
- `src/a314/software-amiga/a314disk-mountlist`

PiStorm-specific changes
- Replace `AllocMemA314`/`FreeMemA314` with Exec `AllocMem`/`FreeMem` in disk device.
- Keep `TranslateAddressA314` identity macro (PiStorm transport).

Endianness
- A314d header framing: explicit LE (`<IIB`, `<IIBII`, `<IIBI`).
- Disk payloads remain BE (`>BBHII`, `>BBB`, `>BBBBI`).

Emulator integration
- None required; uses existing A314 read/write memory requests.

Minimal test plan
1. Start service via `a314d.conf` entry.
2. Mount PD0: using `a314disk-mountlist` entry or equivalent Mountlist entry.
3. Use `nc localhost 23890`:
   - `insert 0 /path/to/test.adf`
   - `eject 0`
4. Verify read/write behavior with file copy on PD0:
   - Create file, reboot, verify persistence on ADF/HDF.

### 2B: remotewb

Feasibility verdict: Feasible with performance caveats.

Pi-side (copy to `src/a314/files_pi/`)
- `src/a314/files_pi/remotewb.py`
- `src/a314/files_pi/remotewb_client.html`
- `src/a314/files_pi/pointer.cur`
- `src/a314/files_pi/bpls2gif/` (dependency; build C extension)

Amiga-side (copy to `src/a314/software-amiga/`)
- `src/a314/software-amiga/remotewb_pistorm/remotewb.c`
- `src/a314/software-amiga/remotewb_pistorm/vblank_server.asm`

Performance risks
- Frequent `MSG_READ_MEM_REQ` for 61440 bytes per frame.
- VBlank-triggered updates can saturate bus if CPU is busy.
- Websocket traffic is bursty; ensure `TCP_NODELAY` is set (already upstream).

Endianness
- A314d header framing: explicit LE (`<IIB`, `<IIBII`).
- Amiga-side payloads remain BE (screen geometry, palette, input events).

Emulator integration
- None required. Uses read-mem requests over existing A314 transport.

Minimal test plan
1. Build and install `remotewb` Amiga client (`C:RemoteWB`).
2. Start `remotewb.py` and open `remotewb_client.html` in a browser.
3. Launch `RemoteWB` on Amiga; verify screen updates and mouse/keyboard input.

### 2C: libremote (bsdsocket proxy)

Feasibility verdict: Feasible, moderate risk from message volume and latency.

Scope cut (now)
- Port `bsdsocket.py` service and the `libremote` Amiga library.
- Skip example library (`example.py`, `libdecl-example.json`, `example_client.c`).

Pi-side (copy to `src/a314/files_pi/`)
- `src/a314/files_pi/bsdsocket.py`
- `src/a314/files_pi/a314d.py` (shared helper)

Amiga-side (copy to `src/a314/software-amiga/`)
- `src/a314/software-amiga/libremote_pistorm/library.c`
- `src/a314/software-amiga/libremote_pistorm/messages.h`
- `src/a314/software-amiga/libremote_pistorm/romtag.asm`
- `src/a314/software-amiga/libremote_pistorm/gen_stubs.py`
- `src/a314/software-amiga/libremote_pistorm/gen_proto.py`
- `src/a314/software-amiga/libremote_pistorm/libdecl-bsdsocket.json`

PiStorm-specific changes
- Replace `AllocMemA314`/`FreeMemA314` with Exec `AllocMem`/`FreeMem` for the bounce buffer.

Endianness
- A314d header framing: explicit LE (`<IIB`, `<IIBII`).
- Libremote protocol messages are BE (as in `messages.h`).

Emulator integration
- None required. Uses read/write memory requests already supported in `a314.cc`.

Minimal test plan
1. Install `bsdsocket.library` in `L:` on the Amiga.
2. Start `bsdsocket.py` service.
3. Use a minimal socket client (e.g. open/close socket + gethostname) and verify returns.

## Endianness Checklist (enforce everywhere)
- A314 client header: `struct.pack('<IIB', length, stream_id, type)`.
- A314 read/write mem requests: `struct.pack('<IIBII', 8, 0, MSG_READ_MEM_REQ, addr, len)`.
- Amiga payloads remain BE as per the C side protocol definitions.
- Do not use `struct.pack('=')` or `struct.unpack('=')`.

## Emulator Integration Notes
- No changes required to `src/emulator.c` or `src/emulator.h` for disk/remotewb/libremote.
- Only revisit emulator changes if adding A314Base LVOs (Alloc/Free/Read/Write) or non-identity address translation.

## Patchset Plan

Patchset 1 (introduce files + build wiring; no enable by default)
- Add new Pi-side scripts and Amiga sources for disk/remotewb/libremote.
- Add `a314d.py` helper for services.
- Add build scripts and README for the new services.

Patchset 2 (endian fixes + logging + stable defaults)
- Convert all A314 header framing to explicit LE (`<`).
- Add one-time header sanity check with fail-closed behavior.
- Replace `AllocMemA314` usage in new Amiga sources.
- Set disk.conf default to empty auto-insert and add config file override.

Patchset 3 (integration wiring)
- Not needed unless you want to add new A314 device LVOs or enforce address translation checks.
