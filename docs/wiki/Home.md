# PiStorm64 Wiki

Welcome to the PiStorm64 wiki for this repository. These pages document build, install, configuration, and platform features, plus how we integrated Pi-side services (A314, PiSCSI, RTG, AHI, networking) and the development environment under `/opt/amiga`.

Milestones:
- PiSCSI remote backend is now validated for real boot use in mixed Zorro setups (`setvar piscsi` + `remote:`).
- PiSCSI64 remote backend is now validated for real boot use: remote disk on a separate host, partitioned/formatted, copied, and booted over a wired 1Gb LAN link.
- Native PiSCSI64 CD-ROM workflow is validated (`cdrom:` + `pi-scsi64.device` + `CDFileSystem`) including AmigaOS install media usage.
- Dhrystone has passed the 64,000 mark on the validated Pi4 baseline.

Current status:
- For mixed Zorro setups (`z3bus`, `zorro-serial`, `zorro-rng`, `zorro-pissa`), use PiSCSI (legacy path with new `cdrom:`/`remote:` support).
- Avoid enabling PiSCSI64 together with those Zorro devices until coexistence is fixed.
- RTG is usable, with a known window-decoration issue under active work.
- UAE JIT remains bring-up only and is not yet a reliable day-to-day runtime path.
- Windows/macOS remote clients build, but full runtime validation is still pending.

Start here:
- Introduction.md
- Quickstart.md
- Build-and-Install.md
- Configuration.md
- Platform-Amiga.md
- Amiga.md
- Atari.md
- Compatibility.md

Feature how-tos:
- PiSCSI.md
- piscsi64.md
- A314.md
- RemoteWB.md
- RTG.md
- Networking.md
- Audio-AHI.md
- ADF-Read-Write.md
- Pi0-Mounts.md

Development:
- Dev-Environment.md
- Setup_Amiga_Compiler.md
- Tools.md
- UAE-JIT-Status.md (experimental UAE JIT backend status)
- Janus-Bus-Engine.md
- CPLD.md
- Zorro-Devices.md
- z3bus_roadmap.md
- z3bus_tasks.md
- Changes-and-Comparison.md
- Known-Issues.md
- Troubleshooting.md
