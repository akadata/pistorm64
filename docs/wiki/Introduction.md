# Introduction

Kernel PiStorm64 is a fork and rework of the original PiStorm emulator for
classic Amiga systems. The focus is 64-bit awareness, improved integration of
Pi-side services, and expanded tooling for direct hardware interaction.

Key project statements (see `docs/README.md` for full context):

- 64-bit aware emulator path for modern Raspberry Pi systems
- Enhanced PiSCSI support for 64-bit host environments
- A314 services integrated for filesystem, RemoteWB and networking use cases
- Direct register access tooling (regtool, clkpeek, pimodplay)
- RTG/P96 support (raylib based) and Pi-side graphics tooling

License: MIT (inherits from upstream PiStorm, see `docs/README.md`).

This wiki documents how to build, configure, and use the features added in this
fork, plus the development environment conventions used in this repo.
