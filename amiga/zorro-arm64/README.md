# zorro-arm64 (Phase 1 probe tool)

Minimal Amiga-side CLI tool for the experimental PiStorm ARM64 mailbox accelerator board.

## Build

```sh
make
```

Defaults use `/opt/amiga/bin/m68k-amigaos-gcc`.

## Emulator config

Add one of these to your PiStorm config:

```ini
setvar zorro-arm64
```

or alias:

```ini
setvar arm64-accel
```

## Usage

Run from Amiga shell:

```sh
armshake
```

Identity dump:

```sh
armshake --id
```

Looped ping:

```sh
armshake 10
```

IRQ semantics check:

```sh
armshake --irq
```

Execute an ARM64 ELF payload through the mailbox worker:

```sh
armshake --elf <path/to/payload.elf>
```

Build the demo ARM64 payload on the Pi host:

```sh
make elf-julia
```

Copy demo payload to shared folder for Amiga-side access:

```sh
make install-julia-elf
```

Expected outputs include:

- detected board base from `FindConfigDev(0x07DB, 0x0041)`
- `PING[...] result=$41524D41` (`ARMA` magic) for successful ping
- with `--irq`: `IRQ test OK: job-done raise/ack.`
- with `--elf`: `ELF_RUN result0=0 ... job_state=3 job_result=0 ...`

`armshake` is diagnostics and generic execution only. It does not implement payload-specific UI behavior.
