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
- SDK/OS target matrix: `docs/sdk_target_matrix.md`

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

## Third-Party Development Materials

- `amiga.dev/` contains development header/assets integration notes (including
  Picasso96 develop materials). See `amiga.dev/README.md`.
- AmigaOS NDK is **not** shipped in this repository; obtain it from official
  distribution channels and install it locally in your toolchain path.

These are third-party development resources; rights remain with their original
authors/owners.

## ROM/Workbench Media Policy

This repository does **not** ship Amiga ROM images or Amiga Workbench system images.

## Kernel Module Clock Defaults

Use:

```sh
sudo modprobe pistorm gpclk_src=5 gpclk_div=4 run_batch_enable=1 berr_reset_input=1 bus_arb_release=1
```

Changing `gpclk_src` from `5` is not recommended for normal users.

To run emulator without `sudo`, install the udev rule and add your user to the
`pistorm` group:

```sh
sudo groupadd -f pistorm
sudo usermod -aG pistorm "$USER"
sudo cp -f etc/udev/99-pistorm.rules /etc/udev/rules.d/99-pistorm.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```
