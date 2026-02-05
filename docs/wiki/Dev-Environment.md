# Development Environment (/opt/amiga)

This project uses a full AmigaOS cross-development environment under `/opt/amiga/`.

Expected layout (example):
```
/opt/amiga/
  AmigaOS-3.1.4
  bin
  include
  lib
  libexec
  m68k-amigaos
  share
  src
  ndk
```

NDKs:
- AmigaOS 3.1.4 NDK
- NDK 3.2 (optional for older toolchains)

## Typical setup

1) Install the m68k AmigaOS toolchain under `/opt/amiga/bin`.
2) Add `/opt/amiga/bin` to `PATH`.
3) Install NDK headers/libs under `/opt/amiga/include` and `/opt/amiga/lib`.

## Amiga.dev (Picasso96 / SDK components)

If you maintain an `amiga.dev` tree (e.g. Picasso96Develop), install it alongside the toolchain and reference it in builds that require RTG dev headers.

Example layout:
```
/opt/amiga/amiga.dev/Picasso96Develop
```

## Build notes

Some Amiga-side components expect the NDK layout above. Verify any hard-coded paths in scripts if the toolchain is installed elsewhere.

## CPLD toolchain (Arch)

To build the EPM240 CPLD bitstreams you need Quartus (ModelSim alone is not enough):

```
yay -S quartus-free-quartus quartus-free-devinfo-max
```

Optional:

```
yay -S quartus-free-help quartus-free-questa arrow-usb-blaster
```

Quartus should provide `quartus_sh`/`quartus_cpf` under `/opt/intelFPGA/.../quartus/bin`.
