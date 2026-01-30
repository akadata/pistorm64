# PiSCSI

PiSCSI provides an Amiga-side SCSI-like block device backed by host files.

Key files:
- `platforms/amiga/piscsi/`
- `platforms/amiga/piscsi/readme.md`

## HDF mapping

HDF images are listed in the config or platform settings. On boot, PiSCSI:

- Loads the PiSCSI ROM.
- Scans partitions.
- Installs filesystem handlers (e.g. PFS/3, DOS/1).

## DMA and memory safety

For stability:

- Keep ROM ranges read-only in the mapper.
- Ensure DMA destinations are within RAM maps (Z2/Z3/RTG) and never into ROM.
- If using a dedicated DMA window, prefer Z3 space (e.g. `0x60000000+`).

## ADF read/write

ADF support is handled via host files in `data/` and device handlers. See ADF-Read-Write.md.
