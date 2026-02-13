# SCSI64 Tasks

## Purpose
Track the staged migration of `piscsi64` to a backend-driven SCSI architecture, while keeping legacy `piscsi` in maintenance mode.

## Project Policy
- New SCSI/storage features go to `src/platforms/amiga/piscsi64/` only.
- Legacy `src/platforms/amiga/piscsi/` remains compatibility/maintenance.
- Safety first: block devices default to read-only unless explicitly set to `mode=rw`.

## Current Status
- Step 1 backend scaffolding: implemented and builds.
- Runtime validation for Step 1: pending user test.

## Task Board

### Step 1: Backend Abstraction (no behavior change)
- [x] Add backend enum (`FILE`, `BLOCK`, `REMOTE`) in `piscsi64` headers.
- [x] Add per-unit backend metadata and backend ops vtable.
- [x] Route core I/O callsites through backend wrappers.
- [x] Keep existing behavior using `FILE` backend.
- [ ] Runtime validation on hardware (boot/install/regression).

### Step 2: Local `BACKEND_BLOCK`
- [ ] Implement block backend ops for `/dev/*` targets.
- [ ] Support `/dev/disk/by-id`, `/dev/disk/by-uuid`, `/dev/disk/by-partuuid`, `/dev/disk/by-partlabel`, and raw `/dev/sdX` style nodes.
- [ ] Default block backend to read-only.
- [ ] Allow opt-in read/write only via explicit `mode=rw`.
- [ ] Emit one loud warning log when opening block backend in RW.
- [ ] Return proper SCSI error/sense on backend I/O failures.

### Step 3: Config Prefix Parsing
- [ ] Add typed source parsing for `disk:` and `cdrom:`.
- [ ] Keep no-prefix behavior as compatibility fallback (file backend).
- [ ] `disk:/dev/...` -> `BACKEND_BLOCK`, `disk:/path/file` -> `BACKEND_FILE`.
- [ ] `cdrom:` uses same backend selection but forces CD semantics.
- [ ] Add `mode=ro|rw` parsing (default `ro` for block, implicit `ro` for `cdrom:`).

### Step 4: Minimal CD-ROM Semantics
- [ ] INQUIRY peripheral type `0x05`, removable set.
- [ ] READ CAPACITY(10) reports 2048-byte logical blocks.
- [ ] READ(10) serves 2048-byte LBAs.
- [ ] TEST UNIT READY reflects media presence/open state.
- [ ] Keep baseline only (no audio/multisession yet).

### Step 5: Regression and Acceptance
- [ ] Existing unprefixed configs behave exactly as before.
- [ ] `disk:/dev/disk/by-id/...` works and is visible as SCSI disk.
- [ ] `disk:/dev/disk/by-partuuid/...` works as single-disk target.
- [ ] `cdrom:/path/to.iso` mounts via Amiga CD filesystem stack.
- [ ] RW block mode only when explicitly requested.

### Step 6: Remote Backend Placeholder
- [ ] Keep `BACKEND_REMOTE` enum and slot in vtable.
- [ ] Fail setup cleanly if `REMOTE` requested before implementation.
- [ ] Defer TCP protocol implementation until Steps 1-5 are green.

## Config Examples (target state)
- `setvar piscsi64_0 disk:../disks/system.hdf`
- `setvar piscsi64_1 disk:/dev/disk/by-id/usb-ExampleDisk,mode=ro`
- `setvar piscsi64_2 disk:/dev/disk/by-id/usb-ExampleDisk,mode=rw`
- `setvar piscsi64_cdrom cdrom:../AmigaOS39.iso`

## Notes
- CD-ROM is always read-only.
- HID devices are not SCSI block devices; USB mass-storage devices are.
- Remote backend work starts only after local backend and config gates are stable.
