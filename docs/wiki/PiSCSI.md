# PiSCSI

PiSCSI provides Amiga-side SCSI devices backed by local files/block devices and remote exports.

This is the recommended storage path when using Zorro extras (`z3bus-demo`, `zorro-serial`, `zorro-rng`, `zorro-pissa`) in the same setup.

Validation status:

- Local HDF boot: verified.
- CD-ROM mapping: verified.
- Remote disk boot (`remote:`): verified.

## Compatibility note

- Avoid `piscsi64` in configs that also enable Zorro demo/utility devices.
- Use `piscsi` instead for mixed setups that need stable boot + Zorro devices.

## Supported media

- Disk: plain path or `disk:...`
- CD-ROM: `cdrom:...` (read-only, 2048-byte sectors)
- Remote: `remote:token@host:port/export[,mode=ro|rw]`

PiSCSI remote/CD-ROM support does not require A314.

## Example config

```ini
setvar piscsi
setvar piscsi0 ../KernelPiStormBench.hdf
setvar piscsi5 remote:token@172.16.0.2:4964/remote,mode=rw
setvar piscsi6 cdrom:../amiga_iso/tsvideo.iso
```

Remote server example:

```sh
sudo piscsi64-remote \
  --listen 0.0.0.0:4964 \
  --export remote \
  --path /dev/zvol/tank/piscsi64remotedisk \
  --token token \
  --kind disk \
  --mode rw \
  --block-size 4096
```

Block size notes:

- `cdrom:` is fixed at `2048` bytes/sector.
- Disk media can be `512` or `4096` (and larger where tooling supports it).
- For `remote:`, the server-defined block size is authoritative.

## HDToolBox / device identity

`pi-scsi.device` now exposes media type in SCSI inquiry/product identity:

- Local disk: `PISCSI DISK`
- CD-ROM: `PISCSI CDROM`
- Remote disk: `PISCSI REMOTE`
- Remote CD-ROM: `PISCSI R-CDROM`

Use this to verify unit mapping from Amiga tools.

## Performance note

If running `--log-level debug`, PiSCSI I/O traces are very verbose and can make remote I/O look slow. Use `info` level for normal runtime performance tests.
