# PiStorm Direction Memo (Architecture Alignment)

## Purpose
Align on the architectural direction for PiStorm beyond Musashi, based on real-world limits and Claude's proven QEMU bus-proxy concept. This memo intentionally avoids timelines, performance promises, or implementation commitments.

## Current State and Limits (Musashi)
- Musashi is a solid, understandable interpreter and remains valuable for correctness, portability, and as a reference implementation.
- Interpreter performance has plateaued at ~6–7 MIPS on typical Pi targets; this reflects interpreter limits, not design flaws.
- 68040 correctness is disproportionately expensive in an interpreter due to PMMU, cache ops, and privilege/exception edge cases.
- Ongoing fixes are possible, but returns diminish rapidly.

## The Architectural Escape Hatch (QEMU TCG + PiStorm Bus Proxy)
- Claude's `qemu.c` proves the concept: a QEMU machine can JIT a CPU (68040/PPC/etc.) and forward all memory operations to the real Amiga bus via PiStorm.
- In this model, PiStorm is not "the CPU," it is the bus/IPC fabric.
- This is a general contract: any QEMU-supported CPU can access Amiga hardware through PiStorm's read/write hooks (subject to timing, IRQ, and DMA semantics exposed by the bus contract).

## Role Definitions Going Forward
- Musashi: correctness and fallback path; also a reference for behavior and debugging.
- QEMU TCG: performance path for 68040+ or non-68k CPUs; enables bridgeboard-style heterogeneous compute.
- PiStorm: stable MMIO/memory contract that exposes Amiga hardware to external CPU cores.

## What This Enables (Conceptually)
- 68040 JIT without over-engineering the interpreter.
- PPC or other architectures as "bridgeboard CPUs."
- A clearer separation of concerns: CPU execution in QEMU, hardware interaction through PiStorm.

## What This Is Not
- Not a near-term replacement for Musashi.
- Not an immediate implementation plan.
- Not about squeezing more MIPS from the interpreter.

## Decision Anchor (North Star)
PiStorm + QEMU is not merely "faster Amiga emulation." It is a heterogeneous compute architecture where Amiga hardware is exposed via a bus contract and the CPU can be swapped or augmented.

## Immediate Next Step (Documentation Only)
- Record this architecture and its implications to prevent future scope drift.
- Keep the focus on clarity and alignment before code investment.
