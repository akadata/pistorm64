# data/fs

Runtime filesystem storage used by PiSCSI and PiSCSI64.

Handlers are saved and loaded using `<TAG>.<rev>` filenames, for example:
- `DOS/1` -> `DOS.1`
- `DOS/3` -> `DOS.3`
- `PFS/3` -> `PFS.3`
- `MSD/0` -> `MSD.0`
- `MSH/0` -> `MSH.0`
- `UNI/1` -> `UNI.1`

Common mappings currently recognized by PiSCSI64:
- `DOS.0` .. `DOS.7`
- `PFS.0` .. `PFS.3`
- `PDS.2`, `PDS.3`
- `SFS.0`, `SFS.2`
- `MSD.0`, `MSH.0`
- `UNI.0`, `UNI.1`
- `CFS.0` (when present)

Notes:
- Files in this directory are generated/used at runtime and are ignored by git.
- Embedded RDB filesystem handlers are still preferred when present.
