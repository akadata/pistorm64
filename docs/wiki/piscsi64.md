# PiSCSI64

PiSCSI64 is the newer SCSI backend for PiStorm64 on Amiga.  
It presents host-backed storage to the Amiga as normal SCSI devices (hard disk, CD-ROM), with boot ROM support and direct `pi-scsi64.device` integration.

Status: active development target. Legacy `PiSCSI` is compatibility/maintenance only.

## Why PiSCSI64 Exists (and Why PiSCSI Still Exists)

PiSCSI64 is not just a rename of legacy PiSCSI.

- `PiSCSI` (legacy) remains for compatibility with existing setups and known-good workflows.
- `PiSCSI64` is the newer path for ongoing development (typed media, better CD-ROM workflow, expanded SCSI-device model).

Both can exist in the tree/config while migration is in progress.

## Enable PiSCSI64

Example `cfg` lines:

```ini
setvar piscsi64
setvar piscsi64_1 /opt/Amiga/hdf/KernelPiStormBench.hdf
setvar piscsi64_3 cdrom:../AmigaOS39.iso
setvar piscsi64_5 disk:/dev/disk/by-id/usb-EXAMPLE,mode=ro
setvar piscsi64_6 remote:token@192.168.1.50:4964/workbench,mode=ro
```

Notes:

- Unit `0` is reserved for controller identity.
- Use units `1..15` for targets.
- `cdrom:` media is read-only by design.
- If you map `/dev/...` paths, the emulator process must have permission to open that block node.
  - A common failure is `errno=13` (permission denied) when opening `/dev/disk/by-id/...`.
  - Check the emulator stdout/stderr for `[PISCSI64] Failed to open ... (errno=13)`.
  - This now also appears in `--log` output as `[ERROR] [PISCSI64] Failed to open ...`.
  - PiStorm64 ships optional udev rules in `etc/udev/99-pistorm.rules` for `sd*`/`mmcblk*`/`nvme*` block nodes (group `pistorm`).
- `/dev/...` targets default to read-only unless explicitly overridden with `,mode=rw`.
- When opened RW, PiSCSI64 emits a warning log line for visibility.
- Startup/map logs now include a per-unit summary line showing `media`, `mode` (`ro`/`rw`), `backend`, and `spec`.
- Current runtime model is one media spec per SCSI unit (1:1).
  - No per-LUN media pool/playlist yet.
  - For different simultaneous devices, use different units (`piscsi64_1..piscsi64_15`).

## Hot-Unplug Behavior

- Local block media unplug is now detected and unit state is forced offline (`DRVTYPE` no-present).
- On backend I/O or probe errors (`ENODEV`/`ENOMEDIUM`/disconnect classes), PiSCSI64 drops runtime media and keeps only the configured spec.
- Amiga-side `TD_CHANGESTATE`/`TEST UNIT READY` then report the unit as removed until reinsert/remap.

## Runtime Eject/Reinsert (Same Unit)

PiSCSI64 now supports safe runtime media detach/reattach on the same unit.

- From Amiga side, explicit eject commands (`TD_EJECT` and SCSI `START STOP UNIT` with LoEj) trigger Pi-side media eject.
- Reinsert on the same unit uses the unit's configured spec (same `setvar piscsi64_X ...` path).
- This is intended for "same unit, same configured source path" workflows.

Important:

- If you want a *different* backing path, remap that unit via config/update flow (or assign another unit).
- A per-unit "media pool" (multiple selectable sources on one SCSI ID) is not implemented yet.

## HDToolBox: Use `pi-scsi64.device`

To manage PiSCSI64 hard disks in HDToolBox, point HDToolBox at the correct device.

Typical method:

1. Open Workbench and select the `HDToolBox` icon.
2. Open `Icon > Information`.
3. In tool types, change the SCSI device from `scsi.device` to `pi-scsi64.device`.
4. Save and run HDToolBox again.

Boot priority warning:

- Avoid marking removable/test disks as bootable unless you intentionally want them in the boot chain.
- If a new disk has higher boot priority than your expected system disk, the Amiga may boot from the wrong target or appear to boot-loop.
- Keep data/test/USB media non-bootable, or set their boot priority lower than your primary system disk.

## CD-ROM via ISO (CD0:)

`HDToolBox` is for hard disks and will correctly report CD-ROM targets as unsupported.  
Use a DOSDriver mount entry (`CD0:`) with `CDFileSystem`.

Example:

```ini
CD0:
  FileSystem      = L:CDFileSystem
  Device          = pi-scsi64.device
  Unit            = 3
  Flags           = 0
  Surfaces        = 1
  SectorsPerTrack = 1
  SectorSize      = 2048
  Mask            = 0x7ffffffe
  MaxTransfer     = 0x100000
  Reserved        = 0
  Interleave      = 0
  LowCyl          = 0
  HighCyl         = 0
  Buffers         = 5
  BufMemType      = 0
  StackSize       = 1000
  Priority        = 10
  GlobVec         = -1
  DosType         = 0x43443031
```

If you are using the existing Workbench `CD0` icon:

1. Go to `DEVS:DOSDrivers/CD0`.
2. Select `CD0`, then open `Icon > Information`.
3. In icon tool types, change `DEVICE=scsi.device` to `DEVICE=pi-scsi64.device`.
4. Set `UNIT=3` (or whichever unit your ISO is mapped to).
5. Reboot Amiga.

Then test in Shell:

```sh
mount CD0:
list CD0:
```

## Prefixes (Current and Planned)

Current prefixes:

- `disk:` force hard-disk behavior
- `cdrom:` force CD-ROM behavior
- `remote:` use TCP remote backend export
- `file:` treat as file-backed media (default when no prefix is given)

Current mode option:

- append `,mode=ro` or `,mode=rw` to a device spec
- examples:
  - `disk:/dev/disk/by-id/usb-EXAMPLE,mode=ro`
  - `disk:/dev/disk/by-id/usb-EXAMPLE,mode=rw`
  - `./somefile.hdf,mode=ro`

Current convenience behavior:

- `.iso` paths are treated as CD-ROM media automatically.

### Remote Prefix

Format:

- `remote:token@host:port/export`
- token is required; port defaults to `4964` if omitted.

Examples:

- `setvar piscsi64_6 remote:token@192.168.1.50:4964/workbench,mode=ro`
- `setvar piscsi64_7 cdrom:remote:token@192.168.1.50:4964/os39iso`

Notes:

- Remote defaults to `mode=ro` unless explicitly `,mode=rw`.
- `cdrom:` remains effectively read-only.
- Remote FSHD extraction from backing media is not enabled yet; existing fs handler flow is unchanged.
- Remote liveness is probe-polled (PING) and offline is forced on disconnect-class failures.
- Remote transport is TLS-PSK encrypted end-to-end (headers + payload, current implementation using TLS 1.2 PSK ciphers).
- Token is used as PSK material and is not sent in clear in protocol payload.

## Remote Boot Milestone

Validated result:

- Remote disk exported from another machine (`piscsi64-remote`) over wired 1Gb Ethernet.
- PiStorm64 mapped that export as a normal PiSCSI64 unit.
- Amiga initialized/partitioned/formatted the remote disk, copied system data, and booted from it.
- Example proven size: 8GB (`16777216` blocks @ `512` bytes).

Verified test example:

Server (remote host):

```sh
sudo ./out/piscsi64-remote \
  --listen 172.16.0.2:4964 \
  --export remotewb \
  --path /dev/zvol/tank/piscsi64remotedisk \
  --token 12345678 \
  --mode rw
```

PiStorm64 config:

```ini
setvar piscsi64
setvar piscsi64_6 remote:12345678@172.16.0.2:4964/remotewb,mode=rw
```

## nftables (Secure Remote Port)

To keep remote SCSI exposure local, restrict TCP `4964` to trusted LAN interfaces/subnets.

Repository template (`etc/nftables.conf`) now includes:

- `iifname "end0"`/`"wlan0"` + RFC1918 source ranges for allow
- Explicit drop for all other traffic to `tcp dport 4964`

Apply/reload:

```sh
sudo nft -f /etc/nftables.conf
sudo nft list ruleset
```

Security notes:

- Replace RFC1918-wide ranges with your exact subnet where possible (example: `172.16.0.0/24`).
- Adjust interface names for your host (`end0`, `eth0`, `wlan0`, etc.).
- Firewall rules complement, but do not replace, local block-device permissions.

Planned/roadmap prefixes:

- `floppy:`
- `zip:`
- `scanner:`
- `usb:`

Goal: expose these as normal SCSI devices/disks to Amiga software, instead of custom one-off interfaces.

## Development Tracking

The implementation checklist is tracked in:

- `SCSI64TASKS.md`

This is the source of truth for staged work (backend abstraction, block backend, config parsing, CD baseline, remote phase-1 and hardening tasks).

## Current Implementation Status

Implemented:

- Shared DOSType/FS-name normalization used by both `piscsi64` and legacy `piscsi`.
- Backend abstraction scaffolding in `piscsi64`:
  - backend enum (`FILE`, `BLOCK`, `REMOTE`)
  - per-unit backend metadata
  - backend ops interface
  - core I/O call sites routed through backend wrappers
- Backend selection and mode control:
  - `/dev/...` => `BLOCK` backend (default `ro`, optional `,mode=rw`)
  - non-`/dev` => `FILE` backend (existing behavior preserved)
  - explicit mode override `,mode=ro|rw` is supported

Pending (next steps):

- Return richer SCSI sense/error codes on backend I/O failures.
- Remote multi-export management and auth hardening.
- Optional per-unit media pool / source cycling (future feature).

## Remote Utilities

Built from repo root:

- `make piscsi64-remote`
- `make piscsi64-remote-server`
- `make piscsi64-remote-client`

Server example (Linux/Unix):

```sh
./out/piscsi64-remote \
  --listen 0.0.0.0:4964 \
  --export workbench \
  --path /dev/disk/by-id/usb-EXAMPLE \
  --token YOUR_SHARED_TOKEN \
  --mode ro
```

Client probe example:

```sh
./out/piscsi64-remote-client 192.168.1.50:4964 workbench token 0 512
```

Probe client is optional and only for diagnostics; normal operation only needs the remote service plus `remote:...` mapping in Pi config.

Transport security note:

- Remote connection is TLS-PSK encrypted.
- Token is used for PSK authentication and not sent in protocol payload.

Windows utility path:

- Native Windows probe client source:
  - `tools/piscsi64_remote/piscsi64_remote_client_win.c`
- Build with MSVC:
  - `cl /O2 /W3 tools\\piscsi64_remote\\piscsi64_remote_client_win.c ws2_32.lib libcrypto.lib`
- Windows probe source currently targets the pre-TLS protocol and needs TLS-PSK update.
- Native Windows daemon/service support is planned as a follow-on phase.

## Validation Flow (Each Step)

1. Build:
   - `make -j4 emulator`
2. Boot with known-good config and verify:
   - normal boot path
   - mapped hard disks visible in HDToolBox via `pi-scsi64.device`
   - existing ISO/CD path still mounts as before
3. Check logs for new backend-layer regressions before moving to next task in `SCSI64TASKS.md`.
