# PiStorm64

PiStorm64 is an Amiga-focused PiStorm fork with ongoing platform, storage, and tooling work.

## Current Highlights

- PiSCSI64 remote disk mount and boot is validated.
- PiSCSI64 ISO-backed CD-ROM mounts are validated.
- Dhrystone has passed 64,000 on the validated Pi4 baseline.
- RTG is functional with a known window-decoration issue under active investigation.
- UAE JIT is still work in progress and is not considered a reliable runtime path yet.

## Read This First

- Main docs index: `docs/README.md`
- Wiki home: `docs/wiki/Home.md`
- PiSCSI64 guide and how-to: `docs/wiki/piscsi64.md`
- Build/install: `docs/wiki/Build-and-Install.md`
- Quickstart: `docs/wiki/Quickstart.md`
- Known issues: `docs/wiki/Known-Issues.md`

## PiSCSI64 Notes

For storage setup, remote mounts, CD-ROM workflow, and operational examples, use:

- `docs/wiki/piscsi64.md`

Windows and macOS client builds exist for the remote tooling, but full validation is still pending.

## DiagROM Credit

**DiagROM is made by John "Chucky" Hertell.**

**We do not take credit for DiagROM.**

Official DiagROM site:

- https://www.diagrom.com

Repository attribution file:

- `DiagROMV2/README.md`

## Kernel Module Clock Defaults

Use:

```sh
sudo modprobe pistorm gpclk_src=5 gpclk_div=6
```

Changing `gpclk_src` from `5` is not recommended for normal users.
