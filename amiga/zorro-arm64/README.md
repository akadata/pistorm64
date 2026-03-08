# zorro-arm64

Amiga-side ARM64 acceleration stack for PiStorm.

Canonical model:

- app artifact is AArch64 payload
- payload executes on coprocessor side
- AmigaOS services remain Amiga-side and are proxied via ARMAccel runtime ABI

## Layer boundaries

- `armaccel_device.*`: low-level transport to the Zorro ARM board.
- `armaccel_library.*`: public runtime/policy API for ELF personality checks and execution.
- `armshake`: diagnostics-only tool.

`armshake` does not launch payloads.

## Build

```sh
make
```

Defaults use `/opt/amiga/bin/m68k-amigaos-gcc`.

Build outputs:

- `C/armshake`
- `C/armrun`
- `C/armaccel.device`
- `C/armaccel.library`

Install location default:

- `/opt/pistorm64/data/a314-shared/`
- override with `make INSTALL_DIR=<path> install`

Example:

- `make -C amiga/zorro-arm64 install INSTALL_DIR=/opt/pistorm64/data/a314-shared/`

## Emulator config

Add one of these to your PiStorm config:

```ini
setvar zorro-arm64
```

or alias:

```ini
setvar arm64-accel
```

## Diagnostics (`armshake`)

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

Expected outputs include:

- detected board base from `FindConfigDev(0x07DB, 0x0041)`
- `PING[...] result=$41524D41` (`ARMA` magic) for successful ping
- with `--irq`: `IRQ test OK: job-done raise/ack.`

## Public Runtime API (`armaccel_library`)

Current API entry points:

- `ARMACCEL_IsSupportedELF(path)`
- `ARMACCEL_QueryELF(path, struct ArmAccelELFInfo *)`
- `ARMACCEL_ExecuteELF(path, struct ArmAccelRunOpts *, struct ArmAccelResult *)`

Current personality checks include:

- ELF class = 64-bit
- machine = AArch64
- endianness = little-endian
- embedded ELF NOTE `.note.armaccel` with ABI/personality/services/class

## Running payload ELFs

`*.elf` payload files are ARM64 binaries and are not AmigaDOS commands.

CLI path:

```sh
armrun path/to/payload.elf
```

Recommended Amiga placement:

- `DEVS:armaccel.device`
- `LIBS:armaccel.library`
- `SYS:Tools/armrun`

## DOS/Workbench Dispatch Path

Primary path is Workbench/DOS association through `.info` metadata:

1. `foo.elf` has companion `foo.elf.info` (type `WBPROJECT`).
2. `foo.elf.info` `Default Tool` is set to `SYS:Tools/armrun`.
3. Workbench launches `SYS:Tools/armrun` with WBStartup arguments.
4. `armrun` hands off to `armaccel.library` (query + execute) and reports status only.
5. `armaccel.device` and `armaccel.library` remain the generic runtime components for ongoing integration.

This keeps `armaccel.device` transport-only and `armaccel.library` policy/runtime-only.
`armrun` must remain a thin launcher stub with no app logic.

Service/OS integration contract is tracked in:

- `docs/armaccel_service_abi_v1.md`
- `src/platforms/amiga/zorro/arm64_accel/armaccel_service_abi.h`

Optional global association:

- configure DefIcons match for `#?.elf` with `Default Tool = SYS:Tools/armrun`.
- per-file `foo.elf.info` can still override behavior as needed.

## Demo ARM64 payload

Build the demo ARM64 payload on the Pi host:

```sh
make elf-julia
```

Copy demo payload to shared folder for Amiga-side access:

```sh
make install-julia-elf
```

This installs both:

- `julia-fractal.elf`

Build/install the Service ABI vertical-slice smoke payload:

```sh
make elf-abi-smoke
make install-abi-smoke-elf
```

This installs:

- `abi-smoke.elf`
