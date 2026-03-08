# ARMAccel and Accelerator Framework

## Overview

ARMAccel is the first implementation of a general accelerator framework for PiStorm-based Amiga systems. The goal is to allow Amiga software to dispatch compute workloads to other processor architectures while keeping the Amiga as the controlling host.

The system exposes accelerator hardware through a Zorro device that provides a mailbox and shared-memory interface. Applications are delivered as AArch64 payloads that execute on the accelerator. AmigaOS remains the host system and provides operating-system services through the runtime Service ABI.

The service interface is defined in:

* `docs/armaccel_service_abi_v1.md`

ARMAccel is the first accelerator personality. The architecture is intentionally designed so other personalities can be added later.

Examples of future accelerators include:

* x86_64accel (local or remote x86_64 execution)
* cuda_accel (NVIDIA CUDA GPU compute)
* opencl_accel (OpenCL devices)
* vector_accel (SIMD engines)

All accelerators follow the same job submission contract so Amiga applications do not need to change when new accelerator types appear.

## Core Design Principles

1. The Amiga remains the host system.
2. Accelerators execute compute workloads only.
3. Application semantics live in the payload, not the launcher.
4. Transport mechanisms are separate from runtime policy.
5. New accelerator types must reuse the same ABI model.

This structure prevents fragmentation and avoids creating separate launchers or custom interfaces for each accelerator.

## System Architecture

The system is divided into layered components.

### Layer 1: Hardware Accelerator

Example: z2-arm64-accel

Responsibilities:

* Zorro AutoConfig device
* register window
* mailbox interface
* shared memory aperture
* job descriptor management
* optional interrupt signaling

This layer is purely hardware transport. It does not understand application semantics.

### Layer 2: Accelerator Device Driver

Example: armaccel.device

Responsibilities:

* board discovery
* register access
* mailbox communication
* shared memory staging
* interrupt handling
* error recovery

This driver is responsible only for communicating with the hardware accelerator.

### Layer 3: Accelerator Runtime Library

Example: armaccel.library

Responsibilities:

* executable inspection
* accelerator personality validation
* ABI compatibility checks
* job submission orchestration
* result retrieval
* capability queries

Applications interact with the library rather than the device driver.

The library decides whether a given executable matches the accelerator personality.

### Layer 4: Payload Executables

Payloads are native binaries for the accelerator architecture.

For ARMAccel they are AArch64 ELF64 executables.

These payload files are not AmigaDOS-executable commands. They must be inspected and launched through `armaccel.library` (for CLI/Workbench integration this is routed via the thin `armrun` stub).

Workbench/DOS association model:

* `foo.elf` with companion `foo.elf.info` (`WBPROJECT`)
* `foo.elf.info` Default Tool -> `SYS:Tools/armrun`
* Workbench launches `armrun` with WBStartup args
* `armrun` hands off to `armaccel.library` and reports status

`armrun` is launcher glue only. It must not contain app behavior, window policy, menu logic, requesters, or file-manager semantics.
`armrun` performs only ELF dispatch through `armaccel.library`; it must not implement UI, rendering logic, application policy, or service behavior.

Service-driven UI/OS behavior is defined by the ARMAccel Service ABI:

* `docs/armaccel_service_abi_v1.md`

Payload responsibilities include:

* computation
* rendering
* numerical workloads
* compression
* AI inference
* cryptographic operations

Payloads are the application artifact and execute on the coprocessor.
AmigaOS services (Intuition/DOS/Exec/devices/libraries) remain Amiga-side and are accessed through the runtime Service ABI.

## Current Implementation

Currently implemented components:

* Zorro ARM64 accelerator board
* ARM64 ELF loader on Pi side
* mailbox protocol
* shared-memory job descriptor
* cooperative Service ABI dispatch hook during `RUN_ELF`
* baseline service families in `armaccel.library` (session/window/surface/input/requester/file/timer/event-ring)
* armshake diagnostic tool

The armshake tool currently provides:

* board identity verification
* mailbox ping testing
* interrupt signaling tests
* raw transport/contract smoke tests

armshake exists for diagnostics and development purposes only.

## Enabling the Accelerator

The accelerator can be enabled in PiStorm configuration:

```
setvar arm64-accel
```

This registers the ARM accelerator on the Zorro bus.

## Diagnostic Tool

The development utility is armshake.

Example commands:

```
armshake --id
armshake --ping
armshake --irq
```

These commands verify communication with accelerator hardware and transport behavior.

## Amiga-Side Runtime Role

Amiga-side code in this stack is runtime/broker code, not app-specific frontend code.

Responsibilities:

* launch orchestration (`armrun` -> `armaccel.library`)
* personality/ABI/capability checks
* service proxy fulfillment via NDK-backed m68k calls
* transport through `armaccel.device`

## Building ARM64 Payload ELFs

Payloads must be compiled as freestanding AArch64 ELF executables.

Requirements:

* ELF64 format
* little-endian
* EM_AARCH64 architecture
* valid loadable segments

Recommended entry function:

```
uint64_t arm_job_entry(void *job_ptr);
```

Example build command:

```
clang --target=aarch64-linux-gnu \
-std=c11 -O2 -ffreestanding \
-nostdlib -nodefaultlibs -nostartfiles -no-pie \
-Wl,-e,arm_job_entry \
-Wl,-Ttext=0x400000 \
-o payload.elf payload.c
```

## Accelerator ABI Version 1

### Board Identity

Manufacturer: 0x07DB

Product: 0x0041

Magic: ARMA

ABI Version: 1

### Zorro Window Layout

0x0000-0x0FFF : register window

0x1000-0x1FFF : mailbox

0x2000-...    : shared memory

Total size: 4 MiB

### Core Registers

All registers are big-endian 32-bit values.

MAGIC

ABI_VERSION

CONTROL

STATUS

IRQ_STATUS

IRQ_ACK

MAILBOX_OFFSET

MAILBOX_SIZE

SHARED_OFFSET

SHARED_SIZE

JOBDESC_OFFSET

JOBDESC_SIZE

HEARTBEAT

### Mailbox Commands

NONE

PING

RUN_ELF

### Mailbox Status

IDLE

BUSY

DONE

ERROR

## Job Descriptor

The job descriptor describes the payload execution request.

Fields include:

MAGIC

VERSION

STATE

FLAGS

ELF_OFFSET

ELF_SIZE

ENTRY_ARG

RETVAL

RESULT

States:

IDLE

QUEUED

RUNNING

DONE

ERROR

## Execution Flow

Typical runtime flow:

1. `armrun` launches a payload through `armaccel.library`.
2. Runtime stages payload bytes into shared memory.
3. Runtime prepares the job descriptor.
4. Runtime issues mailbox `RUN_ELF`.
5. Accelerator executes `arm_job_entry`.
6. Payload performs computation and uses the Service ABI for AmigaOS services.
7. Runtime reads completion state and results.

## Payload Size Considerations

The current implementation stages payloads through the shared window, which limits payload size to the visible aperture size.

Future versions will support staged or streamed payload loading so applications and data sets are not constrained by the Zorro aperture.

## Future Accelerator Personalities

The architecture is designed to support additional compute accelerators.

Examples:

x86_64accel (remote or local Intel/AMD x86_64 systems via a personality client)

cuda_accel (NVIDIA CUDA GPU systems accessed through a remote or local CUDA personality client)

opencl_accel (OpenCL compute devices exposed through a compatible personality client)

These accelerator personalities may run on many operating systems including macOS, Linux, BSD, and Windows. A personality client running on the remote machine exposes the compute capability to the Amiga host using the common accelerator protocol.

Future personalities may include Apple Silicon systems such as M2, M3, or M4 machines acting as remote compute nodes (for example a Mac Pro acting as a high‑performance accelerator).

Each accelerator personality implements the same transport protocol and Service ABI so applications remain architecture-agnostic.

## Long Term Goal

The long-term vision is for the Amiga to act as a dispatcher for heterogeneous compute engines.

The Amiga remains the operating-system host and user interface environment. Accelerator personalities provide additional compute resources while preserving the native Amiga programming model.

This allows modern compute capability to be integrated into classic Amiga systems without compromising system architecture.
