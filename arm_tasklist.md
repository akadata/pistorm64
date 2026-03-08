# ARM64 Zorro Mailbox Task List

## Goal

Build a Zorro-visible execution device that accepts ARM64 ELF jobs from Amiga software and executes them natively on the Pi (AArch64), returning status/output through mailbox + shared memory.

## Design Rules

- Keep Musashi as the 68k engine.
- Do not use UAE 68k JIT.
- Reuse existing PiStorm Zorro + mailbox patterns where possible.
- Start with a freestanding ARM job ABI (no Linux process emulation).

## Non-Goals (for first implementation)

- No fake "ARM CPU replacement" semantics.
- No generic Linux AArch64 binary compatibility.
- No full POSIX syscall layer.
- No complicated scheduler before single-job path is stable.

## Phase 0: Contract and Naming

- [ ] Choose device naming:
- [ ] `setvar zorro-arm64` and alias `setvar arm64-accel`
- [ ] Define AutoConfig IDs (manufacturer/product) for this device
- [ ] Define register map offsets and constants in a shared header
- [ ] Define v1 job descriptor layout in shared window
- [ ] Define v1 completion/error codes

## Phase 1: Minimal Vertical Slice (must boot and run one job)

- [ ] Add new Z2 device backend (`src/platforms/amiga/zorro/arm64_accel/`)
- [ ] Register window with:
- [ ] `MAGIC`, `ABI_VERSION`, `CONTROL`, `STATUS`, `IRQ_STATUS`, `IRQ_ACK`
- [ ] `MAILBOX_OFFSET`, `MAILBOX_SIZE`, `SHARED_OFFSET`, `SHARED_SIZE`
- [ ] Shared info block at start of shared window
- [ ] Single command lane: `RUN_JOB`
- [ ] Host worker thread:
- [ ] wait for command
- [ ] parse job descriptor
- [ ] load ELF64 little-endian AArch64 payload
- [ ] jump to entry with defined ABI argument
- [ ] publish return code + completion status
- [ ] optional IRQ raise/ack path
- [ ] Add tracing env vars (`ARM_ACCEL_MMIO_TRACE`, `ARM_ACCEL_TRACE_LIMIT`, etc.)

## Phase 2: Amiga-side launcher

- [ ] Add Amiga tool `armrun` under `amiga/zorro-arm64/C/`
- [ ] Probe board by manufacturer/product + magic
- [ ] Upload ELF + argv data into shared region
- [ ] Submit job descriptor + command
- [ ] Wait for completion and print return code
- [ ] Add timeout/reset handling in tool

## Phase 3: Robustness

- [ ] Busy/ownership rules for single in-flight command
- [ ] Add crash-safe status transitions (`IDLE`, `BUSY`, `DONE`, `ERR`)
- [ ] Add hard reset command to recover wedged jobs
- [ ] Add guardrails for invalid ELF headers/segments
- [ ] Add bounds checks for all shared offsets and sizes

## Phase 4: Developer UX

- [ ] Add host build script for ARM64 payloads (`clang --target=aarch64...`)
- [ ] Add sample payloads:
- [ ] hello/status
- [ ] buffer transform example
- [ ] hash example
- [ ] Add end-to-end test notes and expected log markers
- [ ] Add docs/wiki page with usage and limits

## Initial Job ABI (v1 proposal)

- Input register/argument: pointer to `arm_job_v1` struct in shared memory
- Struct includes:
- `flags`
- `elf_offset`, `elf_size`
- `argv_offset`, `argv_size`
- `in_offset`, `in_size`
- `out_offset`, `out_size`
- `ret_code`
- `log_offset`, `log_size`

Payload entry signature (concept):

`int arm_job_main(struct arm_job_v1 *job);`

## Immediate Next Actions

- [ ] Create `arm64_accel_regs.h` (v1 register/shared layout)
- [ ] Scaffold `arm64_accel.c` with read/write handlers + reset
- [ ] Hook device registration in `amiga_zorro.c`
- [ ] Add `setvar zorro-arm64` parser hook
- [ ] Add MMIO smoke logs and verify AutoConfig assignment

