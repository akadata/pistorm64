# Introduction

This repository contains the PiStorm64 emulator, platform glue, and Amiga-side integration assets. The core goals are:

- 680x0 emulation with a Pi-side bus backend.
- Z2/Z3 memory maps with Pi-side services (PiSCSI, A314, RTG, AHI, networking).
- Deterministic autoconfig and stable boot behavior.
- A clean dev workflow, including an AmigaOS NDK toolchain installed under `/opt/amiga`.

Key directories in this repo:
- `emulator.c`, `emulator.h`: main entry point and platform dispatch.
- `platforms/amiga/`: Amiga platform glue (PiSCSI, RTG, AHI, net, autoconfig).
- `config_file/`: config parser and map syntax.
- `a314/`: A314 host services and Amiga-side components.
- `gpio/`: backend to `/dev/pistorm` (kernel module).
- `data/`: runtime data (filesystems, a314 shared, etc.).

Related docs in-tree:
- `README.md`
- `platforms/amiga/readme.md`
- `platforms/amiga/piscsi/readme.md`
- `platforms/amiga/rtg/readme.md`
- `platforms/amiga/ahi/readme.md`
- `platforms/amiga/net/readme.md`
- `config_file/readme.md`
