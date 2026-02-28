# PiSCSI64

PiSCSI64 is the newer SCSI backend for PiStorm64 on Amiga.  
It presents host-backed storage to the Amiga as normal SCSI devices (hard disk, CD-ROM), with boot ROM support and direct `pi-scsi64.device` integration.

Status: feature-complete backend, but currently not recommended in configs that also enable extra Zorro demo/utility devices.

## Important compatibility warning

- Do not combine `piscsi64` with `z3bus` / `zorro-*` devices in the same boot config right now.
- For those mixed setups, use `piscsi` (which now supports `cdrom:` and `remote:`) until `piscsi64` + Zorro coexistence is fixed.

Validation status:

- Linux host path (local disk, block devices, remote server, and CD-ROM ISO) is validated.
- Windows/macOS remote client builds are present, but full runtime validation is still pending.
- Native dual-stack endpoint support is validated (IPv4 and true IPv6 addressing on Pi and remote endpoint).

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
setvar piscsi64_7 remote:token@[2001:db8::10]:4964/workbench,mode=ro
```

Notes:

- Unit `0` is reserved for controller identity.
- Use units `1..15` for targets.
- PiSCSI64 supports units/SCSI IDs `1..15`.
- Practical tool note: some HDToolBox versions only scan IDs `0..6` even when the device itself supports wider IDs.
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

Important scan-range behavior:

- HDToolBox may only probe SCSI IDs `0..6` depending on version/tooltype behavior.
- This is an Amiga tooling limitation, not a PiSCSI64 unit limit.
- PiSCSI64 units above 6 (for example ID 14) are valid and can be used once prepared/mounted.
- If you need to prepare a high ID disk, initialize it on a lower ID first, then move it to the target high ID.

Boot priority warning:

- Avoid marking removable/test disks as bootable unless you intentionally want them in the boot chain.
- If a new disk has higher boot priority than your expected system disk, the Amiga may boot from the wrong target or appear to boot-loop.
- Keep data/test/USB media non-bootable, or set their boot priority lower than your primary system disk.

## Known Issue: Large Disk Geometry in Legacy Amiga Tools

PiSCSI64 can expose large disks correctly (including remote exports), but classic Amiga tooling can still mis-handle geometry on large targets after "Install" in HDToolBox.

Observed behavior:

- First screen can show full size, then second screen can show a much smaller size (or even `0` for some exact capacities).
- This is typically a legacy CHS/display/math limitation in old tooling, not a PiSCSI64 block backend size bug.

Practical policy (recommended):

- For predictable setup, keep partitions at or below `16GB`.
- Over `16GB`, split media into multiple partitions of max `16GB` each.
- For remote media, prefer exporting a specific partition path instead of a huge whole-disk target when using legacy setup tools.
- Field-tested behavior: partition-backed targets are reliable; very large whole-raw-disk targets can still trigger legacy geometry/tooling misreporting.
- Empirical legacy-tool threshold (auto geometry): around `48GB` can work cleanly without manual geometry edits; above this, classic tools often need manual intervention.

Example (remote, partition node):

```ini
setvar piscsi64_5 remote:token@192.168.1.50:4964/workbench_part3,mode=rw
```

On remote host, map that export to a partition path (example):

```sh
sudo piscsi64-remote \
  --listen 0.0.0.0:4964 \
  --export workbench_part3 \
  --path /dev/disk/by-id/usb-EXAMPLE-0:0-part3 \
  --token token \
  --mode rw
```

### Manual CHS/LBA Math

If you need to sanity-check values manually:

```text
total_blocks = bytes / sector_size
cylinders    = floor(total_blocks / (heads * sectors_per_track))
chs_blocks   = cylinders * heads * sectors_per_track
usable_bytes = chs_blocks * sector_size
```

LBA to CHS (for fixed geometry):

```text
C = floor(LBA / (H * S))
tmp = LBA % (H * S)
Hn = floor(tmp / S)
Sn = (tmp % S) + 1
```

Notes:

- `BLOCKS` from PiSCSI64 is LBA-based capacity and is the authoritative value.
- `CHS` is compatibility geometry for older tools and may not represent every last sector exactly.
- Large-capacity values shown in first detection screens can be reduced on later "Install Drive" screens by legacy geometry fallback logic in old tools.

### About >104G and PFS3

- `pfs3aio` includes experimental `>104G` partition support and may warn accordingly during format.
- Large media can work (including tested 128GB scenarios), but for reliable day-to-day setup on classic tools, keep partition sizes sane (`<=16GB` each).
- Historical context matters: classic Amiga workflows were designed in an era where `30MB` was considered a large hard drive on an Amiga 500 (circa 1989).

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

Current block-size option (disk/file backends):

- append `,blocksize=N` (or `,bs=N`)
- supported values: `512` or `4096`
- examples:
  - `disk:/dev/disk/by-id/usb-EXAMPLE,mode=rw,blocksize=4096`
  - `file:/opt/Amiga/test.hdf,bs=4096`

Notes:

- `cdrom:` uses fixed 2048-byte blocks.
- `remote:` uses the server-advertised block size (`--block-size` on `piscsi64-remote`), local `blocksize=` is ignored for remote specs.

Current convenience behavior:

- `.iso` paths are treated as CD-ROM media automatically.

### Remote Prefix

Format:

- `remote:token@host:port/export`
- token is required; port defaults to `4964` if omitted.
- IPv6 literals are supported:
  - with explicit port: `remote:token@[2001:db8::10]:4964/export`
  - default port: `remote:token@2001:db8::10/export`

Examples:

- `setvar piscsi64_6 remote:token@192.168.1.50:4964/workbench,mode=ro`
- `setvar piscsi64_7 cdrom:remote:token@192.168.1.50:4964/os39iso`
- `setvar piscsi64_8 remote:token@[2001:db8::10]:4964/workbench,mode=rw`

Notes:

- Remote defaults to `mode=ro` unless explicitly `,mode=rw`.
- `cdrom:` remains effectively read-only.
- Remote FSHD extraction from backing media is not enabled yet; existing fs handler flow is unchanged.
- Remote liveness is probe-polled (PING) and offline is forced on disconnect-class failures.
- Remote transport is TLS-PSK encrypted end-to-end (headers + payload, current implementation using TLS 1.2 PSK ciphers).
- Token is used as PSK material and is not sent in clear in protocol payload.
- Fail-closed behavior:
  - If endpoint is unreachable/not started, emulator continues and unit stays offline.
  - Wrong port/export/token does not silently attach to media.

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

# IPv6 listen + export
sudo ./out/piscsi64-remote \
  --listen [::]:4964 \
  --export remotewb \
  --path /dev/zvol/tank/piscsi64remotedisk \
  --token 12345678 \
  --mode rw
```

PiStorm64 config:

```ini
setvar piscsi64
setvar piscsi64_6 remote:12345678@172.16.0.2:4964/remotewb,mode=rw
setvar piscsi64_7 remote:12345678@[2001:db8::10]:4964/remotewb,mode=rw
```

## nftables (Secure Remote Port)

To keep remote SCSI exposure local, restrict TCP `4964` to trusted LAN interfaces/subnets.

Repository template (`etc/nftables.conf`) now includes:

- `iifname "end0"`/`"wlan0"` + RFC1918 source ranges for allow
- Explicit drop for all other traffic to `tcp dport 4964`
- IPv6 source filtering examples (`ip6 saddr`) and port-set examples for multiple remote instances

Apply/reload:

```sh
sudo nft -f /etc/nftables.conf
sudo nft list ruleset
```

Security notes:

- Replace RFC1918-wide ranges with your exact subnet where possible (example: `172.16.0.0/24`).
- Replace IPv6 examples with your exact prefix (example: `2a02:8012:bc57:1::/64`).
- Adjust interface names for your host (`end0`, `eth0`, `wlan0`, etc.).
- Firewall rules complement, but do not replace, local block-device permissions.

Multiple remote services:

- Each `piscsi64-remote` instance must use a unique listen port.
- Example:
  - instance A: `--listen [::]:4964 --export workbench ...`
  - instance B: `--listen [::]:4965 --export games ...`
- Open those exact ports in nftables (prefer allow-list, then explicit drop).

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

- Remote multi-export management and per-export ACL/token policy.
- Optional per-unit media pool / source cycling (future feature).
- Documented HDToolBox scan-range limitation (`0..6`) alongside PiSCSI64 wide-ID support (`1..15`).

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

./out/piscsi64-remote \
  --listen [::]:4964 \
  --export workbench \
  --path /dev/disk/by-id/usb-EXAMPLE \
  --token YOUR_SHARED_TOKEN \
  --mode ro
```

Client probe example:

```sh
./out/piscsi64-remote-client 192.168.1.50:4964 workbench token 0 512
./out/piscsi64-remote-client [2001:db8::10]:4964 workbench token 0 512
```

Probe client is optional and only for diagnostics; normal operation only needs the remote service plus `remote:...` mapping in Pi config.

Transport security note:

- Remote connection is TLS-PSK encrypted.
- Token is used for PSK authentication and not sent in protocol payload.

Windows utility path:

- Native Windows probe client source:
  - `tools/piscsi64_remote/piscsi64_remote_client_win.c`
- WSL2 cross-build:
  - `sudo apt install mingw-w64`
  - `make -f tools/piscsi64_remote/Makefile.windows`
  - output: `tools/piscsi64_remote/out/piscsi64-remote-client.exe`
- Build with MSVC:
  - `cl /O2 /W3 tools\\piscsi64_remote\\piscsi64_remote_client_win.c ws2_32.lib libssl.lib libcrypto.lib`
- Windows probe now uses the same TLS-PSK protocol as Linux/macOS client probe.
- Native Windows daemon/service support is planned as a follow-on phase.

macOS note:

- macOS client/server build support is present in tooling targets.
- Full macOS runtime validation is pending.

## Validation Flow (Each Step)

1. Build:
   - `make -j4 emulator`
2. Boot with known-good config and verify:
   - normal boot path
   - mapped hard disks visible in HDToolBox via `pi-scsi64.device`
   - existing ISO/CD path still mounts as before
3. Check logs for new backend-layer regressions before moving to next task in `SCSI64TASKS.md`.
