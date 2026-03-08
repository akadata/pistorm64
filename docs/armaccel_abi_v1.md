# ARMAccel ABI v1 (Draft)

## Scope

This document defines the v1 contract for generic ARM64 payload execution on PiStorm ARM Zorro boards.

Layers:

1. `armaccel.device` (transport only)
2. `armaccel.library` (runtime/policy)
3. ARM64 payload ELF (application logic)

`armshake` remains diagnostics and contract exerciser only.

## v1 Goals

- Stable generic launch path for AArch64 ELF payloads.
- No payload-specific behavior in launcher/runtime.
- Scalable transport for payloads larger than immediate board window assumptions.

## Personality and Metadata

v1 personality check order:

1. ELF note section `.note.armaccel`

Required metadata fields (v1):

- `personality`: string (`generic` in v1 baseline)
- `abi_major`: integer (`1`)
- `abi_minor`: integer (`0+`)
- `entry_contract`: string (`jobdesc-v1`)

Optional fields:

- `requires`: feature bitmask
- `preferred_stack`
- `declared_io`: capabilities (`pixel_surface`, `input`, `audio`, `fs`, etc.)

If the ELF note is absent, runtime should classify payload as not ARMAccel personality.

## ELF Validation Rules (v1)

`armaccel.library` must validate:

- ELF class: `ELF64`
- machine: `AArch64`
- endianness: little-endian
- executable or compatible ET_DYN policy
- segment bounds and loadability against staging policy

Failures map to explicit runtime error codes.

## Launch Descriptor (v1)

Transport-level descriptor is the existing board `JOBDESC` block (`AJOB`), plus shared metadata region.

Core fields:

- magic/version
- state/flags
- payload offset/size
- entry arg pointer
- retval lo/hi
- result code

State values:

- `IDLE`
- `QUEUED`
- `RUNNING`
- `DONE`
- `ERROR`

## Result/Error Codes (v1)

Transport/result baseline:

- `OK`
- `ERR_FORMAT`
- `ERR_RANGE`
- `ERR_LOAD`
- `ERR_EXEC`
- `ERR_INTERNAL`

Library-level errors extend this with parse/policy failures and map to human-readable messages.

## Device Transport API (v1)

`armaccel.device` command set (logical API):

- `PROBE`
- `GET_CAPS`
- `UPLOAD_CHUNK`
- `DOWNLOAD_CHUNK`
- `RUN`
- `WAIT`
- `RESET`
- `GET_STATUS`

Completion modes:

- polling
- IRQ-assisted wait

No payload-specific assumptions are allowed at this layer.

## Chunked Transfer Rules (v1)

- Payload upload must support chunking.
- Runtime must not assume entire payload fits in a tiny contiguous host-visible aperture.
- Chunk sequencing and integrity checks are required.

Initial recommended chunk size:

- host configurable; default 32 KiB or board-advertised optimum.

## Service Model (v1 Baseline)

v1 execution baseline:

- execute payload with job descriptor contract only.

Planned generic service channels (next revisions):

- pixel surface (shared drawable buffer; presentation stays on Amiga runtime side)
- input event queue
- timers
- filesystem RPC
- audio

All service growth must remain generic and runtime-driven.

Service ABI freeze draft lives at:

- `docs/armaccel_service_abi_v1.md`
- `src/platforms/amiga/zorro/arm64_accel/armaccel_service_abi.h`

## Tool Boundaries

- `armshake`: diagnostics only (probe/ping/irq/raw contract smoke tests).
- `armaccel.device`: transport only.
- `armaccel.library`: single app-facing execution interface.
- No per-application wrapper launchers and no parallel launcher binaries per payload class.

## Versioning

Compatibility rule:

- `abi_major` mismatch: reject.
- `abi_minor` higher than runtime: reject unless explicitly backward-compatible.

Any incompatible change increments major.
