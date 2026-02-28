# PiSCSI

Canonical page: `PiSCSI.md`.

Quick summary:

- Use `setvar piscsi` for stable mixed Zorro setups.
- `piscsi64` should not be used together with `z3bus`/`zorro-*` devices.
- PiSCSI now supports:
  - local disk (`disk:`/plain path),
  - CD-ROM (`cdrom:`),
  - remote backend (`remote:`).
- Remote disk boot with `setvar piscsi` + `remote:` is validated.
- `pi-scsi.device` inquiry product now reports media/backend class (`PISCSI DISK`, `PISCSI CDROM`, `PISCSI REMOTE`, `PISCSI R-CDROM`).
