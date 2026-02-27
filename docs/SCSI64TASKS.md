# SCSI64 Tasks

## Purpose
Track the staged migration of `piscsi64` to a backend-driven SCSI architecture, while keeping legacy `piscsi` in maintenance mode.

## Project Policy
- New SCSI/storage features go to `src/platforms/amiga/piscsi64/` only.
- Legacy `src/platforms/amiga/piscsi/` remains compatibility/maintenance.
- Safety first: block devices default to read-only unless explicitly set to `mode=rw`.

## Current Status
- Step 1 backend scaffolding: implemented and builds.
- Runtime validation for Step 1: validated on hardware (boot/install/regression).
- Runtime media eject/reinsert on same unit: implemented (1:1 unit/spec model).
- Runtime unplug -> offline transition: implemented for block/remote backend errors, block probe path, and remote ping probe path.
- Remote backend (phase 1): implemented (`remote:` mapping + TLS-PSK transport + server/client utilities).
- Remote utility builds: Linux + mac makefiles verified; Windows probe build path provided via MinGW/WSL2.
- Remote endpoint parser: IPv4 + native IPv6 endpoint syntax supported across Pi/backend + Linux/macOS/Windows probe clients.
- Remote failure behavior: fail-closed validated (offline unit on unreachable/wrong endpoint; emulator stays up).

## Task Board

### Step 1: Backend Abstraction (no behavior change)
- [x] Add backend enum (`FILE`, `BLOCK`, `REMOTE`) in `piscsi64` headers.
- [x] Add per-unit backend metadata and backend ops vtable.
- [x] Route core I/O callsites through backend wrappers.
- [x] Keep existing behavior using `FILE` backend.
- [ ] Runtime validation on hardware (boot/install/regression).

### Step 2: Local `BACKEND_BLOCK`
- [x] Implement block backend ops for `/dev/*` targets.
- [x] Support `/dev/disk/by-id`, `/dev/disk/by-uuid`, `/dev/disk/by-partuuid`, `/dev/disk/by-partlabel`, and raw `/dev/sdX` style nodes (via `/dev/...` backend classification).
- [x] Default block backend to read-only.
- [x] Allow opt-in read/write only via explicit `mode=rw`.
- [x] Emit one loud warning log when opening block backend in RW.
- [x] Return proper SCSI error/sense on backend I/O failures.

### Step 3: Config Prefix Parsing
- [x] Add typed source parsing for `disk:` and `cdrom:`.
- [x] Keep no-prefix behavior as compatibility fallback (file backend).
- [x] `disk:/dev/...` -> `BACKEND_BLOCK`, `disk:/path/file` -> `BACKEND_FILE`.
- [x] `cdrom:` uses same backend selection but forces CD semantics.
- [x] Add `mode=ro|rw` parsing (default `ro` for block/remote, implicit `ro` for `cdrom:`).
- [x] Add `remote:` backend prefix parsing (`remote:token@host:port/export`).

### Step 4: Minimal CD-ROM Semantics
- [x] INQUIRY peripheral type `0x05`, removable set.
- [x] READ CAPACITY(10) reports 2048-byte logical blocks.
- [x] READ(10) serves 2048-byte LBAs.
- [x] TEST UNIT READY reflects media presence/open state.
- [x] Keep baseline only (no audio/multisession yet).

### Step 5: Regression and Acceptance
- [ ] Existing unprefixed configs behave exactly as before.
- [x] `disk:/dev/disk/by-id/...` works and is visible as SCSI disk.
- [ ] `disk:/dev/disk/by-partuuid/...` works as single-disk target.
- [x] `cdrom:/path/to.iso` mounts via Amiga CD filesystem stack.
- [x] RW block mode only when explicitly requested.
- [x] Runtime eject/reinsert on same unit validated from Amiga tools (`TD_REMOVE`/`TD_EJECT`/SCSI START STOP UNIT).
- [ ] Runtime unplug/offline path validated on hardware (USB pull + reinsert/remap).

### Step 7: Runtime Media UX
- [x] Add Pi-side media control commands for eject/insert.
- [x] Wire Amiga driver `TD_REMOVE`/`TD_EJECT` and SCSI START STOP UNIT to media control.
- [x] Preserve unit config for same-unit reinsertion.
- [ ] Add optional per-unit media pool (multiple selectable sources per one SCSI ID).

### Step 6: Remote Backend Placeholder
- [x] Keep `BACKEND_REMOTE` enum and slot in vtable.
- [x] Implement initial TCP protocol and `remote:` backend mapping.
- [x] Add utilities: `piscsi64-remote-server` + `piscsi64-remote-client`.
- [x] Add native Windows probe-client source (`piscsi64_remote_client_win.c`).
- [x] Add TLS-PSK encrypted transport for remote control and data path.
- [x] Add native IPv6 endpoint support in remote parser and tools.
- [ ] Multi-export config file support and per-export ACLs/tokens.
- [ ] Windows-native server implementation (service model).
- [x] Strong auth/TLS transport hardening.

### Step 8: Unit/LUN Scaling
- [ ] Investigate and resolve SCSI ID `>=7` access limitations on current branch (`hotfix/fixpiscsi64_id_issue`).
- [ ] If unresolved by unit-ID path, implement multiple-LUN exposure strategy per SCSI ID.

## Config Examples (target state)
- `setvar piscsi64_0 disk:../disks/system.hdf`
- `setvar piscsi64_1 disk:/dev/disk/by-id/usb-ExampleDisk,mode=ro`
- `setvar piscsi64_2 disk:/dev/disk/by-id/usb-ExampleDisk,mode=rw`
- `setvar piscsi64_cdrom cdrom:../AmigaOS39.iso`

## Notes
- CD-ROM is always read-only.
- HID devices are not SCSI block devices; USB mass-storage devices are.
- Remote backend work starts only after local backend and config gates are stable.
