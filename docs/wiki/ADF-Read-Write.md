# ADF Read/Write

ADF handling is supported via Pi-side files and device handlers. ADFs live under the runtime data tree and are exposed to the Amiga as block devices.

See:
- `data/README.md`
- `data/fs/README.md`
- `platforms/amiga/piscsi/readme.md`

Operational guidelines:
- Keep ADF files in `data/` or another stable host path.
- Ensure the device handlers are installed on the Amiga side.
- Use the PiSCSI tooling to mount/unmount safely.
