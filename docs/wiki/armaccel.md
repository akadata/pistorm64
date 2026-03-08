# ARMAccel

## What This Is

`armaccel` is a Zorro-visible ARM64 offload path for PiStorm.

It lets Amiga software launch ARM64 ELF payloads that execute natively on the Pi side through a mailbox + shared-memory contract.

It is **not** a fake replacement Amiga CPU. It is a generic coprocessor execution path.

## Architecture

The stack is split into three layers:

1. Zorro ARM64 device (`z2-arm64-accel`)
- AutoConfig identity
- MMIO register window
- mailbox page
- shared window/job descriptor
- optional IRQ completion

2. Launcher/runtime bridge (today: `armshake`, later: `.device`/`.library`)
- probe board
- stage payload/job data
- start execution
- wait/report status

3. ARM64 payload ELF (your app logic)
- compute/render/business logic
- app state
- payload-specific behavior

Design rule: payload behavior stays in the payload, not in `armshake`.

## Current Status

Implemented now:

- Zorro board registration (`setvar arm64-accel` or `setvar zorro-arm64`)
- ABI v1 register map, mailbox, and `AJOB` descriptor
- `armshake` generic diagnostics and `--elf` execution path
- ELF64/AArch64 loader/runner on Pi side

Planned next:

- `armaccel.device` transport API
- `armaccel.library` runtime/policy API
- `armrun` thin generic launcher client

## Quick Start

Enable in config:

```ini
setvar arm64-accel
```

Build Amiga launcher tool:

```bash
make -C amiga/zorro-arm64
```

Install to shared folder used by Amiga side:

```bash
make -C amiga/zorro-arm64 install
```

On Amiga shell:

```sh
armshake --id
armshake --ping
armshake --elf <path/to/payload.elf>
```

## Building Amiga-Side Programs (M68K + NDK)

Amiga-side binaries (launcher/UI/frontends) are normal m68k Amiga programs and should link against Amiga NDK APIs.

Current toolchain in this repo uses `/opt/amiga/bin/m68k-amigaos-gcc`.

Example (same model as `armshake`):

```bash
/opt/amiga/bin/m68k-amigaos-gcc \
  -m68030 -O2 -Wall -Wextra -noixemul \
  -I../../src/platforms/amiga/zorro/arm64_accel \
  -o myfrontend myfrontend.c
```

Typical NDK headers for a frontend:

- `exec/types.h`
- `proto/exec.h`
- `proto/intuition.h`
- `proto/graphics.h`
- `proto/dos.h`
- `proto/expansion.h`

Important: if you want Workbench/Intuition windows, menus, requesters, etc., that UI lives in the **m68k frontend** (NDK code). The ARM64 payload does not call Amiga Exec/Intuition directly.

## Building ARM64 Payload ELFs

ARM payloads are freestanding AArch64 ELFs executed by the Pi-side ARM loader.

Current loader requirements:

- ELF64
- little-endian
- `EM_AARCH64`
- valid PT_LOAD segments
- entry point inside loadable range

Recommended entry symbol/signature:

```c
uint64_t arm_job_entry(void *job_ptr);
```

Minimal clang build pattern:

```bash
clang --target=aarch64-linux-gnu \
  -std=c11 -O2 -ffreestanding \
  -fno-pic -fno-plt -fno-asynchronous-unwind-tables -fno-unwind-tables \
  -nostdlib -nodefaultlibs -nostartfiles -no-pie \
  -Wl,-e,arm_job_entry -Wl,--build-id=none -Wl,-Ttext=0x400000 \
  -Wl,-z,max-page-size=0x1000 -Wl,-z,common-page-size=0x1000 -Wl,-s \
  -I./src/platforms/amiga/zorro/arm64_accel \
  -o mypayload.elf mypayload.c
```

If building on an ARM64 Linux host, `--target=aarch64-linux-gnu` is often optional.

Repo example:

```bash
make -C amiga/zorro-arm64 elf-julia
```

## ABI v1 Interface (Current)

### Board Identity

- Manufacturer: `0x07DB`
- Product: `0x0041`
- Board magic: `0x41524D41` (`"ARMA"`)
- ABI version: `1`

### Z2 Window Layout

- `0x0000-0x0FFF`: register window
- `0x1000-0x1FFF`: mailbox page
- `0x2000-...`: shared window
- Total board window (`ARM64_ACCEL_Z2_SIZE`): 4 MiB

### Core Registers (big-endian 32-bit)

- `0x0000` `MAGIC`
- `0x0004` `ABI_VERSION`
- `0x0008` `CONTROL`
- `0x000C` `STATUS`
- `0x0010` `IRQ_STATUS`
- `0x0014` `IRQ_ACK`
- `0x0018` `MAILBOX_OFFSET`
- `0x001C` `MAILBOX_SIZE`
- `0x0020` `SHARED_OFFSET`
- `0x0024` `SHARED_SIZE`
- `0x0028` `JOBDESC_OFFSET`
- `0x002C` `JOBDESC_SIZE`
- `0x0030` `HEARTBEAT`

Control bits:

- `START=0x1`
- `STOP=0x2`
- `RESET=0x4`
- `IRQ_ENABLE=0x8`

Status bits:

- `READY=0x1`
- `BUSY=0x2`
- `FAULT=0x4`

IRQ bits:

- `JOB_DONE=0x1`
- `HOST_EVENT=0x2`

### Shared Info Block

At `0x2000` (size `0x20`), includes:

- signature (`ARMA`)
- ABI version
- mailbox offset/size
- jobdesc offset/size
- feature bits: mailbox/irq/shared-ram

### Mailbox (offset `0x1000`)

Fields:

- `MAGIC` (`AMB1`)
- `VERSION`
- `SEQ`, `ACK_SEQ`
- `CMD`, `STATUS`
- `ARG0..ARG3`
- `RESULT0`, `RESULT1`

Commands:

- `0`: `NONE`
- `1`: `PING`
- `2`: `RUN_ELF`

Statuses:

- `0`: `IDLE`
- `1`: `BUSY`
- `2`: `DONE`
- `3`: `ERR`

### Job Descriptor (`AJOB`)

At `0x2040` (size `0x100`), big-endian 32-bit fields:

- `MAGIC` (`AJOB`)
- `VERSION`
- `STATE`
- `FLAGS`
- `ELF_OFFSET`
- `ELF_SIZE`
- `ENTRY_ARG`
- `RETVAL_LO` / `RETVAL_HI`
- `RESULT`

State values:

- `0` `IDLE`
- `1` `QUEUED`
- `2` `RUNNING`
- `3` `DONE`
- `4` `ERROR`

Result values:

- `0` `OK`
- `1` `ERR_FORMAT`
- `2` `ERR_RANGE`
- `3` `ERR_LOAD`
- `4` `ERR_EXEC`
- `5` `ERR_INTERNAL`

## Generic Run Flow

1. Frontend probes board.
2. Frontend writes payload bytes to shared window.
3. Frontend fills `AJOB` (`ELF_OFFSET`, `ELF_SIZE`, `ENTRY_ARG`, state).
4. Frontend sends mailbox `RUN_ELF` (seq/ack handshake).
5. Worker executes payload entry on Pi ARM64.
6. Worker updates `AJOB` state/result/retval and mailbox result.
7. Frontend polls or waits IRQ, then reads completion.

## Size/Transport Notes

Current `armshake --elf` path uploads payload into board shared window, so payload size is currently bounded by available window space.

ABI direction is chunked/staged transfer so payload/assets are not permanently limited by visible aperture size.

## For App Authors

To start building apps now:

1. Keep app logic inside ARM64 payload (`arm_job_entry`).
2. Use an m68k frontend (NDK) for Amiga UI and user interaction.
3. Exchange data through shared/job memory structures.
4. Keep launcher logic generic so the same runtime can execute many payload types.

See also: `docs/armaccel_abi_v1.md` for the v1 contract direction.
