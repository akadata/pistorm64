# PiStorm64 – Project Context

## Overview

PiStorm64 is a Raspberry Pi–based accelerator platform for classic Amiga computers. A Raspberry Pi (typically Pi Zero / Pi 3 / Pi 4 class depending on variant) connects to the Amiga 68000 CPU socket through a CPLD. The Pi emulates or accelerates the CPU while the CPLD mediates bus timing and signal compatibility with the original Amiga motherboard.

The design goal is to provide extremely fast CPU execution while preserving compatibility with the Amiga chipset, DMA behaviour, and system timing.

This document exists to give development agents and contributors a consistent mental model of the project.

---

# Target Hardware

## Primary Machine

Primary reference system:

Amiga 500 (PAL timing)

Core characteristics:

CPU: Motorola MC68000
Clock: ~7.09379 MHz (PAL)
Bus width: 16-bit data / 24-bit address

Chipset:

* Agnus – DMA controller
* Denise – video output
* Paula – audio, disk, interrupts

These custom chips share memory through DMA and can halt the CPU during bus arbitration.

Correct emulation must respect DMA contention.

---

# PiStorm64 Architecture

## Components

PiStorm64 consists of three major elements:

1. Raspberry Pi
2. CPLD interface logic
3. Amiga motherboard bus

### Raspberry Pi

Runs the CPU emulator / JIT and device services.

Typical responsibilities:

* CPU execution
* RTG graphics
* storage devices
* networking
* memory services

### CPLD

Acts as a deterministic bus interface between the Pi GPIO and the Amiga bus.

Responsibilities:

* signal level mediation
* bus timing
* interrupt propagation
* bus ownership arbitration

Typical clock input:

High speed reference clock from Pi (commonly GPIO4 ~200 MHz) used internally for timing.

### Amiga Bus

The CPLD connects directly to the 68000 socket and therefore participates in the system bus.

Key signals:

Address / data

* A[23:1]
* D[15:0]

Control signals

* AS
* R/W
* UDS
* LDS
* DTACK
* VPA
* VMA

Interrupts

* IPL[2:0]

Bus arbitration

* BR
* BG
* BGACK

System control

* RESET
* HALT
* BERR

Function codes

* FC[2:0]

These signals must be handled with correct timing relative to the Amiga chipset.

---

# Execution Model

The Raspberry Pi executes 68k instructions using either:

* interpreter
* dynamic JIT

Current development focuses on the custom CPU implementation located in:

```
src/m68xkcpu
```

Musashi is also present as a reference implementation:

```
src/musashi
```

Musashi is used for validation and behavioural comparison.

---

# Memory Model

The Amiga memory map includes:

* Chip RAM (DMA shared)
* Fast RAM
* Autoconfig expansion space

DMA capable devices include:

* Agnus
* Paula
* floppy controller
* disk DMA

The CPU cannot assume exclusive access to memory.

Bus cycles must respect chipset arbitration.

---

# Autoconfig Devices

PiStorm can expose devices on the Amiga Zorro bus using the Amiga autoconfig protocol.

Examples:

* RTG graphics
* PiSCSI
* networking
* expansion RAM

Autoconfig logic assigns addresses dynamically during boot.

Correct implementation must mirror Zorro II / Zorro III behaviour.

---

# Development Goals

Near-term goals:

* robust 68000 CPU implementation
* development of custom JIT core
* improved bus accuracy

Medium-term goals:

* 68040 CPU support
* FPU behaviour
* MMU implementation

Long-term goals:

* extremely high performance while preserving Amiga compatibility
* maintain deterministic behaviour for hardware interaction

---

# Coding Principles

General philosophy:

Accuracy before cleverness.

Where possible:

* preserve hardware semantics
* maintain deterministic bus behaviour
* favour explicit logic over opaque abstractions

Coding style:

K&R brace style.

Always use braces even for single-statement conditionals.

Example:

```
if (condition) {
    action();
}
```

---

# Repository Layout

Important directories:

```
pistorm64/

src/
    m68xkcpu/
    musashi/

third_party/
    ProcessorTests/

reference/
    hardware docs
```

These directories are exposed through the MCP filesystem server.

---

# Development Workflow

Typical workflow:

1. modify CPU core
2. build project
3. run ProcessorTests
4. validate behaviour against Musashi
5. test on real Amiga hardware

Tools commonly used:

* gcc / clang
* bear
* llvm tools
* gdb
* perf

---

# Key Constraints

Things that must always be considered:

* Amiga DMA bus sharing
* custom chip timing
* interrupt propagation
* bus ownership changes

Incorrect handling of these will break software compatibility.

---

# Philosophy

The goal of PiStorm64 is not simply to run code quickly.

The goal is to integrate extremely fast CPU execution into the Amiga hardware ecosystem while respecting the behaviour of the original machine.

Compatibility with the real hardware environment remains the guiding principle.

