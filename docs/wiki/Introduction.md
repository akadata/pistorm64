# Introduction

This repository contains the PiStorm64 emulator, platform glue, and Amiga-side integration assets. The core goals are:

Architecture guardrails (accelerator work): [AGENTS.md](../../AGENTS.md)

- 680x0 emulation with a Pi-side bus backend (Musashi by default, UAE JIT
  optional and still in bring-up).
- Z2/Z3 memory maps with Pi-side services (PiSCSI, A314, RTG, AHI, networking).
- Deterministic autoconfig and stable boot behavior.
- A clean dev workflow, including an AmigaOS NDK toolchain installed under `/opt/amiga`.

Key directories in this repo:
- `emulator.c`, `emulator.h`: main entry point and platform dispatch.
- `platforms/amiga/`: Amiga platform glue (PiSCSI, RTG, AHI, net, autoconfig).
- `src/config_file/`: config parser and map syntax.
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
- `src/config_file/readme.md`
- `UAE-JIT-Status.md` (experimental UAE JIT backend status)
