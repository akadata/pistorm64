# PiStorm64 Documentation Index

This page is the top-level documentation entry for this repository.

## Project Status Snapshot

- PiSCSI64 is live and is now the active storage path.
- Remote PiSCSI64 mount and boot over 1Gb Ethernet is validated.
- PiSCSI64 CD-ROM mounts from ISO are validated (`cdrom:` + `pi-scsi64.device` + `CDFileSystem`).
- Dhrystone performance has passed the 64,000 mark on the validated Pi4 path.
- RTG is usable, but there is a known window-decoration issue still being worked.
- UAE JIT is still bring-up only and does not currently provide a reliable boot path.
- Windows and macOS remote client builds exist, but full runtime validation is still pending.

## Clock Guidance (Important)

Use:

```sh
sudo modprobe pistorm gpclk_src=5 gpclk_div=6
```

Do not switch `gpclk_src` away from `5` unless you are actively debugging hardware timing.
If a specific board needs a slower clock for stability testing, adjust `gpclk_div` carefully and document it.

## Where To Start

- Wiki home: `docs/wiki/Home.md`
- Quickstart: `docs/wiki/Quickstart.md`
- Build and install: `docs/wiki/Build-and-Install.md`
- Configuration: `docs/wiki/Configuration.md`
- PiSCSI64 guide: `docs/wiki/piscsi64.md`
- Known issues: `docs/wiki/Known-Issues.md`

## Complete Wiki Index

- `docs/wiki/A314.md`
- `docs/wiki/ADF-Read-Write.md`
- `docs/wiki/Amiga-Primer.md`
- `docs/wiki/Amiga.md`
- `docs/wiki/Atari.md`
- `docs/wiki/Audio-AHI.md`
- `docs/wiki/Build-and-Install.md`
- `docs/wiki/CPLD.md`
- `docs/wiki/Changes-and-Comparison.md`
- `docs/wiki/Compatibility.md`
- `docs/wiki/Configuration.md`
- `docs/wiki/Dev-Environment.md`
- `docs/wiki/FC-Lines.md`
- `docs/wiki/Home.md`
- `docs/wiki/Introduction.md`
- `docs/wiki/Janus-Bus-Engine.md`
- `docs/wiki/Known-Issues.md`
- `docs/wiki/Networking.md`
- `docs/wiki/Pi0-Mounts.md`
- `docs/wiki/Pi4-64bit-native-pistorm64-benchmarks.md`
- `docs/wiki/PiSCSI.md`
- `docs/wiki/Platform-Amiga.md`
- `docs/wiki/Quickstart.md`
- `docs/wiki/RNG-SSL-AES-Zorro-Design.md`
- `docs/wiki/RTG.md`
- `docs/wiki/RemoteWB.md`
- `docs/wiki/Tools.md`
- `docs/wiki/Troubleshooting.md`
- `docs/wiki/UAE-JIT-Status.md`
- `docs/wiki/Zorro-Devices.md`
- `docs/wiki/piscsi64.md`
- `docs/wiki/z3bus_roadmap.md`
- `docs/wiki/z3bus_tasks.md`

## Current Focus

- Finish documentation alignment, then merge.
- Continue RTG stabilization work (window decorations and related polish).
- Continue UAE JIT bring-up behind explicit experimental flags.

## Third-Party Materials and Media Policy

- `amiga.dev/` and `NDK/` are third-party development/support materials; see:
  - `amiga.dev/README.md`
  - `NDK/README.md`
- This repository does **not** ship Amiga ROM images or Amiga Workbench images.
