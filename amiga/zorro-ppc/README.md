# zorro-ppc (Stage 6A handshake tool)

Minimal Amiga-side CLI tool for the experimental PiStorm PPC accelerator board model.

## Build

```sh
make
```

Defaults use `/opt/amiga/bin/m68k-amigaos-gcc`.

## Emulator config

Add to your PiStorm config:

```ini
setvar zorro-ppc
```

## Usage

Run from Amiga shell:

```sh
ppcshake
```

Optional loop count:

```sh
ppcshake 10
```

IRQ/doorbell semantics check:

```sh
ppcshake --irq
```

Expected output includes:

- detected board base from `FindConfigDev(0x07DB, 0x0040)`
- `TIME32[...] = $XXXXXXXX (...)` lines
- with `--irq`: `IRQ test OK: doorbell raise/ack and cmd_done raise/ack.`

This validates Stage 6A discovery + mailbox round-trip over Zorro.
