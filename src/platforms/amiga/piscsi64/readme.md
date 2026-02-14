# PiSCSI64 (Current)

This directory contains the active PiSCSI64 backend and Amiga boot/device components for PiStorm64.

PiSCSI64 is the current storage path. Legacy `piscsi` remains for compatibility/maintenance only.

## Core Model

- Controller: `setvar piscsi64`
- Units: `piscsi64_1` to `piscsi64_15` (`piscsi64_0` is reserved for controller identity)
- Amiga device name: `pi-scsi64.device`
- Boot ROM: `src/platforms/amiga/piscsi64/piscsi64.rom`

Amiga sees normal SCSI-style devices. Pi side chooses backend type per unit.

## Backends and Spec Prefixes

Supported backend forms:

- File-backed image (default):
  - `setvar piscsi64_1 /opt/Amiga/hdf/workbench.hdf`
- Forced disk semantics:
  - `setvar piscsi64_2 disk:/opt/Amiga/hdf/data.hdf`
- Block node:
  - `setvar piscsi64_5 disk:/dev/disk/by-id/usb-EXAMPLE`
- CD-ROM:
  - `setvar piscsi64_3 cdrom:../AmigaOS39.iso`
- Remote:
  - `setvar piscsi64_6 remote:token@172.16.0.2:4964/remotewb,mode=rw`

Mode option:

- Append `,mode=ro` or `,mode=rw`
- Safety defaults:
  - block and remote default to `ro`
  - `cdrom:` is read-only

## Remote Transport Security

Remote backend now uses TLS-PSK transport.

- Protocol headers and payload are encrypted in transit.
- Token is used for PSK authentication material.
- Token is not sent in protocol payload.

Current implementation uses TLS 1.2 PSK ciphers.

Expected logs:

- Pi side:
  - `[PISCSI64-REMOTE] Unit X TLS established: version=... cipher=... bits=...`
- Remote server:
  - `[piscsi64-remote] tls client=... version=... cipher=... bits=...`

## HDToolBox and DOSDriver Notes

- For hard disks in HDToolBox, use `pi-scsi64.device` (not `scsi.device`).
- CD-ROM targets are not RDB disks; mount with `CDFileSystem` using `Device=pi-scsi64.device`.

Boot priority caution:

- Do not mark test/removable media bootable unless intended.
- A higher boot priority disk can unexpectedly take over boot.

## Runtime Media Behavior

- PiSCSI64 tracks media online/offline status.
- Disconnect-class failures force unit offline.
- Runtime eject/insert commands are supported for configured units.

## Building

### Emulator

From repo root:

```sh
make -j4 emulator
```

### Remote tools

From repo root:

```sh
make piscsi64-remote
make piscsi64-remote-server
make piscsi64-remote-client
```

Or in `tools/piscsi64_remote`:

```sh
make
```

### Amiga ROM/device artifacts

In `src/platforms/amiga/piscsi64/device_driver_amiga`:

```sh
make
make install
```

`make install` copies:

- `pi-scsi64.device` -> `/opt/pistorm64/data/a314-shared/pi-scsi64.device`
- `piscsi64.rom` -> `/opt/pistorm64/src/platforms/amiga/piscsi64/piscsi64.rom`

## Permissions and Safety

For `/dev/...` mappings, emulator user must have permission to open block nodes.

Common failure:

- `errno=13` permission denied on `/dev/disk/by-id/...`

Use group/udev policy from repo (`etc/udev/99-pistorm.rules`) or run with suitable privileges for testing.

Never map unknown physical disks RW unless you accept destructive risk.

## Documentation

Primary user-facing documentation:

- `docs/wiki/piscsi64.md`

Implementation tracking:

- `SCSI64TASKS.md`
