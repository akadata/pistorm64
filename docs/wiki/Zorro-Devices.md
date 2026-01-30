# Zorro Devices (z3bus + Z2 cards)

This project now has a small **Zorro device registry** in the emulator. Devices are registered
via `setvar` options in the config and appear as Z2 autoconfig boards. The **long‑term goal**
is to move device logic into Linux drivers bound to `z3bus.ko`, while the emulator provides
the 68k bus cycles and autoconfig behavior.

Current status:
- Z2 devices use 64KB windows for bring‑up and stability.
- Z3 autoconfig is now supported for **Z3 bus devices** (separate from Z3 RAM).
- Z2 space is limited to 8MB total ($00200000–$00A00000).
- Devices register through `amiga_zorro.c` and are autoconfig‑visible to AmigaOS.

## Config options (setvar)

Use these in `default.cfg` or another config:

```
setvar z3bus-demo 1
setvar zorro-serial 1
setvar zorro-rng 1
setvar zorro-pissa 1
```

Notes:
- `z3bus-demo` is a **Z3 autoconfig** demo device.
- `zorro-serial`, `zorro-rng`, `zorro-pissa` are **Z2** devices.
- You can enable any subset, but Z2 space is limited so keep Z2 devices modest in count.

## Device IDs

Manufacturer ID uses `PISTORM_AC_MANUF_ID` = `0x07DB`.

| Device | Product ID | Notes |
|---|---:|---|
| z3bus demo | `0x060C` | Z3 autoconfig demo |
| z2 serial echo | `0x0010` | Host PTY echo bridge |
| z2 RNG | `0x0002` | Host RNG source |
| z2 PISSA | `0x0003` | AES‑GCM crypto accelerator |

## Z2 Serial Echo

The serial echo device is a **Z2 memory‑mapped serial** that bridges to a host PTY.

Host side:
- The emulator runs unprivileged; it **does not** write to `/dev`.
- It publishes a stable symlink at:
  - `$XDG_RUNTIME_DIR/amiga/serial/z2serial0`
  - `/run/user/<uid>/amiga/serial/z2serial0` (fallback)
  - `/tmp/amiga/serial/z2serial0` (final fallback)
- The emulator logs both the real PTY path and the symlink.

Test from host:
```
cat /tmp/amiga/serial/z2serial0
```

Amiga side (example):
- A test CLI exists under `amiga/z3dev-serial/`.
- Copy CLI to `C:` and run; it writes to the device window and reads the echo back.

## Z2 RNG

Simple RNG device backed by host entropy:
- `DATA32` returns a 32‑bit random value.
- `STATUS` indicates RNG OK / data ready.

Amiga‑side plan:
- `pirng.library` will provide a proper Amiga API.

## Z2 PISSA (AES‑GCM)

Crypto accelerator device:
- AES‑256‑GCM via OpenSSL EVP on host.
- Registers control command, offsets, and lengths.

Amiga‑side plan:
- `pissa.library` will expose AES‑GCM APIs (library name is fixed).
- `pissl.library` (planned) will add TLS‑style session helpers once the core device is stable.

## z3bus.ko integration

`z3bus.ko` is built/installed as a Linux misc device:

```
/dev/z3bus
```

Access is controlled via udev and group membership:

```
/etc/udev/rules.d/99-pistorm.rules
```

The emulator opens `/dev/z3bus` and logs availability. This is the future bridge
for moving device logic into kernel drivers.

## Z3 roadmap

Z3 devices will be enabled after:
- Z2 registry is stable.
- Z3 address assignment is defined.
- Device windows and DMA are validated.

See:
- `docs/wiki/z3bus_roadmap.md`
- `docs/wiki/z3bus_tasks.md`
