# PiStorm64 Wiki

Welcome to the PiStorm64 wiki for this repository. These pages document build, install, configuration, and platform features, plus how we integrated Pi-side services (A314, PiSCSI, RTG, AHI, networking) and the development environment under `/opt/amiga`.

Milestones:
- PiSCSI64 remote backend is now validated for real boot use: remote disk on a separate host, partitioned/formatted, copied, and booted over a wired 1Gb LAN link.
- Native PiSCSI64 CD-ROM workflow is validated (`cdrom:` + `pi-scsi64.device` + `CDFileSystem`) including AmigaOS install media usage.

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
