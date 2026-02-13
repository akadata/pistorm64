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
- `file:` treat as file-backed media (default when no prefix is given)

Current mode option:

- append `,mode=ro` or `,mode=rw` to a device spec
- examples:
  - `disk:/dev/disk/by-id/usb-EXAMPLE,mode=ro`
  - `disk:/dev/disk/by-id/usb-EXAMPLE,mode=rw`
  - `./somefile.hdf,mode=ro`

Current convenience behavior:

- `.iso` paths are treated as CD-ROM media automatically.

Planned/roadmap prefixes:

- `floppy:`
- `zip:`
- `scanner:`
- `usb:`

Goal: expose these as normal SCSI devices/disks to Amiga software, instead of custom one-off interfaces.

## Development Tracking

The implementation checklist is tracked in:

- `SCSI64TASKS.md`

This is the source of truth for staged work (backend abstraction, block backend, config parsing, CD baseline, remote placeholder).

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

- Remote backend placeholder wiring (implemented as controlled unsupported path until protocol lands).
- Return richer SCSI sense/error codes on backend I/O failures.
- Optional per-unit media pool / source cycling (future feature).

## Validation Flow (Each Step)

1. Build:
   - `make -j4 emulator`
2. Boot with known-good config and verify:
   - normal boot path
   - mapped hard disks visible in HDToolBox via `pi-scsi64.device`
   - existing ISO/CD path still mounts as before
3. Check logs for new backend-layer regressions before moving to next task in `SCSI64TASKS.md`.
