# QWEN.md - AArch64 JIT for Motorola 68000

## Project Goal

Build a test-driven AArch64 JIT for the Motorola 68000 CPU used by pistorm64.

Musashi remains the architectural reference interpreter.
The JIT must produce identical CPU state, memory effects, and exceptions.

**Correctness is always more important than speed.**

All behaviour must pass the vendored ProcessorTests corpus and the Musashi reference test suite.

---

# Current Project State

## Test Infrastructure Status

All Musashi reference tests currently pass:

```
SUMMARY mode=68040 pass=18 fail=0 xfail=0 xpass=0 miss=0 total=18
```

68000 instruction tests are also passing.

ProcessorTests results:

**Old corpus:**
```
files=124
 tests=1,000,060
 transactions=5,190,532
 errors=0
```

**New corpus:**
```
files=127
 tests=317,500
 transactions=1,783,580
 errors=0
```

Both test sets are wired into CI.

The project has a verified interpreter baseline suitable for JIT development.

---

# Test Infrastructure

The following targets must remain passing at all times:

```bash
make processortests-quick
make processortests-full
make processortests-quick-new
make processortests-full-new
make musashi-ref-tests
```

The compare utility also exists:

```bash
make processortests-compare-sets
```

This reports differences between the old and new corpora.

The new dataset adds additional instruction coverage:

```
ILLEGAL_LINEA
ILLEGAL_LINEF
STOP
```

These tests verify correct exception behaviour.

---

# JIT Status

## Phase 1: Infrastructure ✅ COMPLETE

### Opcode Metadata Generator
- `src/m68xkcpu/generate_opinfo.py` - Generates 65536-entry opcode table
- `src/m68xkcpu/generated/jit_68000_opinfo.h` - Opcode info header
- `src/m68xkcpu/generated/jit_68000_opinfo.c` - Opcode metadata table

All 68000 instruction families correctly classified including:
- MOVEQ, ADDQ, SUBQ (immediate arithmetic)
- BRA (distinct from BCC)
- SWAP, UNLK, STOP (control operations)
- LINE_A, LINE_F (exception handlers)
- ORI/ANDI/EORI to CCR/SR (special moves)

### Validation Infrastructure
- `tools/jit_vs_musashi.py` - Test runner for ProcessorTests corpus
- `tools/musashi_json_driver.c` - Musashi execution driver for JSON tests
- `src/m68xkcpu/VALIDATION_STATUS.md` - Detailed status documentation

**Validation harness capabilities:**
- ✅ Corpus walking (both 680x0 and m68000 ProcessorTests)
- ✅ JSON structure validation
- ✅ CPU state extraction (D0-D7, A0-A7, PC, SR, USP, SSP)
- ✅ RAM initialization from test data
- ✅ Prefetch buffer loading
- ✅ Comparison scaffolding (`compare_states()`)
- ✅ Musashi execution (single instruction)
- ✅ Clean JSON output for automated comparison

### Two-Environment Architecture

**CRITICAL:** There are TWO distinct execution environments that must not be mixed:

1. **Validation Harness (Test Environment)**
   - Flat 16MB memory buffer (deterministic, no hardware)
   - No PiStorm backend
   - Pure 68000 instruction semantics
   - ProcessorTests JSON → flat RAM → Musashi/JIT → compare state

2. **Real Emulator (PiStorm Runtime)**
   - PiStorm backend (`src/pistorm/backend.*`)
   - Real Amiga memory map, MMU, chipset
   - Function codes, bus behavior, timing
   - JIT → `jit_mem_read/write` → PiStorm backend → Amiga memory

**Files:**
- `src/m68xkcpu/jit_arch.h` - Environment abstraction layer
- `src/m68xkcpu/jit_mem_test.h` - Test environment (flat memory)
- `src/m68xkcpu/jit_mem_pistorm.h` - PiStorm environment (real hardware)

### Core JIT Infrastructure
- `src/m68xkcpu/jit.h` / `jit.c` - JIT core interface and execution engine
- `src/m68xkcpu/jit_block.h` / `jit_block.c` - Basic block management
- `src/m68xkcpu/jit_cache.h` / `jit_cache.c` - Code cache with executable memory
- `src/m68xkcpu/jit_emit_aarch64.h` / `jit_emit_aarch64.c` - AArch64 code emitter
- `src/m68xkcpu/jit_translate.h` - Translator interface (stubs for instruction translators)

### Build Integration
- `make m68xkcpu-opinfo` - Regenerate opcode tables
- `make m68xkcpu-jit` - Build JIT objects (with `USE_M68XK_JIT=1`)
- `python3 tools/jit_vs_musashi.py --suite-dir ... --mode quick` - Run validation

---

# Architecture Overview

## JIT Execution Flow (Validation)

```
┌─────────────────────────────────────────────────────────┐
│              ProcessorTests JSON Corpus                 │
│  - 680x0/68000/v1/ (deep: ~8065 cases per op)          │
│  - m68000/v1/ (light: ~2500 cases per op)              │
└────────────────────┬────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────┐
│           Validation Harness (Python + C)               │
│  - Parse JSON test cases                                │
│  - Initialize CPU state (D0-D7, A0-A7, PC, SR)          │
│  - Load RAM image from test data                        │
│  - Execute single instruction                           │
│  - Compare final state with expected                    │
└────────────────────┬────────────────────────────────────┘
                     │
         ┌───────────┴───────────┐
         │                       │
         ▼                       ▼
┌─────────────────┐     ┌─────────────────┐
│    Musashi      │     │      JIT        │
│  (oracle)       │     │  (candidate)    │
│ src/musashi/    │     │ src/m68xkcpu/   │
└────────┬────────┘     └────────┬────────┘
         │                       │
         └───────────┬───────────┘
                     │
                     ▼
         ┌───────────────────────┐
         │   compare_states()    │
         │   - D0-D7 match?      │
         │   - A0-A7 match?      │
         │   - PC match?         │
         │   - SR/CCR match?     │
         │   - RAM mutations?    │
         └───────────────────────┘
```

## Real Emulator Integration (Future)

```
┌─────────────────────────────────────────────────────────┐
│                    emulator.c                           │
│  - Main emulation loop                                  │
│  - CPU state management                                 │
│  - Hardware integration                                 │
└────────────────────┬────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────┐
│              Execution Dispatcher                       │
│  - Check JIT cache for compiled block                   │
│  - If found: execute JIT code                           │
│  - If not found: fall back to Musashi                   │
│  - Optionally compile block after interpretation        │
└────────────────────┬────────────────────────────────────┘
                     │
         ┌───────────┴───────────┐
         │                       │
         ▼                       ▼
┌─────────────────┐     ┌─────────────────┐
│   Musashi       │     │   JIT Cache     │
│ src/musashi/    │     │ src/m68xkcpu/   │
└────────┬────────┘     └────────┬────────┘
         │                       │
         └───────────┬───────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────┐
│              PiStorm Backend                            │
│  src/pistorm/backend.*                                  │
│  - Real Amiga memory access                             │
│  - Function codes (FC0-FC2)                             │
│  - Endianness conversion (BE↔LE)                        │
│  - Bus timing and hardware behavior                     │
└─────────────────────────────────────────────────────────┘
```

---

# Memory Model

## Test Environment (Validation Harness)

```c
// Flat 16MB memory - deterministic, no hardware
#define JIT_ENV_TEST
#include "jit_arch.h"

jit_test_mem_t test_mem;
jit_test_mem_init(&test_mem);
jit_test_mem_load(&test_mem, addr, data, size);

// Direct memory access - no backend
jit_mem_read16(addr, FC, &value);
jit_mem_write32(addr, value, FC);
```

## PiStorm Environment (Real Emulator)

```c
// Real Amiga memory via PiStorm backend
#define JIT_ENV_PISTORM
#include "jit_arch.h"

// Backend handles everything:
// - Real hardware access
// - Endianness conversion
// - Function codes
// - Bus timing
jit_mem_read16(addr, FC, &value);
jit_mem_write32(addr, value, FC);
```

## Endianness

- **68000**: Big Endian
- **AArch64**: Little Endian
- **PiStorm backend**: Handles BE↔LE conversion automatically
- **JIT cache**: Native AArch64 (LE) - no swapping needed internally
- **AArch64 REV instruction**: Available for register byte-swapping when needed

---

# Opcode Metadata Source

Opcode metadata is generated from:

```
third_party/ProcessorTests/680x0/map/68000.official.json
```

This produces a 65536-entry opcode metadata table.

Each entry includes:

```
handler family
size kind
extension word count
source EA class
destination EA class
reads CCR
writes CCR
privileged
may trap
block end
```

This metadata drives JIT dispatch.

---

# Source Layout

## Interpreter Implementation

```
src/musashi/
```

This directory contains the Musashi interpreter and must remain the canonical execution engine.

Example contents:

```
m68kcpu.c
m68kcpu.h
m68k_in.c
m68kops.c
m68kdasm.c
m68kmmu.h
m68kfpu.c
softfloat/
```

These files implement the reference 68k CPU semantics.

The interpreter must remain readable and close to upstream Musashi.

The JIT must **not modify these files**.

## JIT Implementation

```
src/m68xkcpu/
```

All JIT work lives here, completely separate from Musashi.

```
src/m68xkcpu/
  jit.h / jit.c                      - Core JIT interface
  jit_block.h / jit_block.c          - Basic block management
  jit_cache.h / jit_cache.c          - Code cache
  jit_emit_aarch64.h / jit_emit_aarch64.c - AArch64 emitter
  jit_translate.h                    - Translator interface
  jit_arch.h                         - Environment abstraction
  jit_mem_test.h                     - Test environment memory
  jit_mem_pistorm.h                  - PiStorm environment memory
  generated/
    jit_68000_opinfo.h / .c          - Opcode metadata
  VALIDATION_STATUS.md               - Detailed status
```

---

# JIT Execution Model

## Execution Pipeline

1. Fetch opcode from memory
2. Look up compiled block in JIT cache
3. If block found: execute compiled code
4. If block not found: fall back to Musashi interpreter
5. Optionally compile block after interpretation
6. Continue execution

Unsupported instructions must always fall back to Musashi.

## Block Termination Conditions

Blocks should terminate when encountering:

* branches (BRA, Bcc, BSR)
* traps (TRAP, TRAPV, illegal instructions)
* interrupts
* RTS (return from subroutine)
* RTE (return from exception)
* unknown/unimplemented instructions

Keeping blocks small simplifies invalidation and debugging.

---

# Memory Correctness

## Test Environment

- Flat memory model
- Self-modifying code invalidates compiled blocks
- RAM mutations tracked for comparison

## Real Emulator

- PiStorm backend handles real Amiga memory
- Self-modifying code must invalidate compiled blocks
- Any write to memory that belongs to compiled code must invalidate that block
- Memory behaviour must match interpreter behaviour exactly

---

# Exception Handling

The JIT must synchronize CPU state before:

* exceptions
* traps
* interrupts
* interpreter fallback

The interpreter must see a consistent CPU state.

Function codes (FC0-FC2) must be preserved:
- FC0-FC2: 0=User Data, 1=User Program, 5=Supervisor Data, 6=Supervisor Program

---

# Initial Instruction Coverage

Begin with a minimal safe subset:

```
MOVE
MOVEQ
ADD
SUB
CMP
BRA
Bcc
basic shifts (ASL, ASR, LSL, LSR)
NOP
```

Other instructions may fall back to the interpreter until implemented.

---

# Metrics

The JIT should track:

```
compiled block count
cache size used
cache free space
cache hit count
cache miss count
fallback instruction count
validation pass/fail count
```

These metrics help evaluate performance improvements and correctness.

---

# Development Order

## Phase 1: Infrastructure ✅ COMPLETE

1. ✅ opcode metadata generator
2. ✅ JIT block cache
3. ✅ minimal AArch64 emitter
4. ✅ Validation harness
5. ✅ Musashi JSON driver
6. ✅ Two-environment architecture

## Phase 2: MOVE Translator (NEXT)

7. Implement MOVE translator
8. Validate against ProcessorTests
9. Fix any mismatches

## Phase 3: Arithmetic Translators

10. Implement ADD translator
11. Implement SUB translator
12. Implement CMP translator
13. Validate all arithmetic operations

## Phase 4: Branch Translators

14. Implement BRA translator
15. Implement Bcc translators
16. Validate branch operations

## Phase 5: Expand Coverage

17. Implement remaining instructions
18. Optimize common paths
19. Integrate with emulator.c

---

# Testing Strategy

## Validation Harness Tests

```bash
# Quick validation (10 files)
python3 tools/jit_vs_musashi.py \
    --suite-dir third_party/ProcessorTests/680x0/68000/v1/ \
    --mode quick

# Full validation (all files)
python3 tools/jit_vs_musashi.py \
    --suite-dir third_party/ProcessorTests/680x0/68000/v1/ \
    --mode full

# Single test file
python3 tools/jit_vs_musashi.py \
    --test third_party/ProcessorTests/680x0/68000/v1/ADD.b.json
```

## Existing Test Suite (Must Remain Passing)

```bash
make processortests-quick
make processortests-full
make processortests-quick-new
make processortests-full-new
make musashi-ref-tests
```

---

# Coding Guidelines

* keep code readable
* avoid giant functions
* prefer modular translators
* do not remove interpreter paths
* always run the full test suites
* **NEVER mix test environment with PiStorm environment**
* **ALWAYS use jit_mem_read/write for memory access**
* **Function codes matter - pass them correctly**

---

# Project Philosophy

**Correctness first.**
**Architecture clarity second.**
**Speed third.**

A fast emulator that is incorrect is not useful.

**The validation harness is the truth machine.**
**Musashi is the oracle.**
**The JIT is the candidate.**

When the JIT disagrees with Musashi, the JIT is wrong.
Always.

---

# Handover Notes

## For New Developers

1. **Read `src/m68xkcpu/VALIDATION_STATUS.md`** for detailed current status
2. **Understand the two environments** - test harness vs real emulator
3. **Start with MOVE translator** - simplest instruction family
4. **Run validation after every change** - catch bugs early
5. **Never modify Musashi** - it's the oracle, not the candidate

## Key Files

| File | Purpose |
|------|---------|
| `tools/jit_vs_musashi.py` | Test runner - validates JIT against Musashi |
| `tools/musashi_json_driver.c` | Musashi execution for ProcessorTests |
| `src/m68xkcpu/jit_arch.h` | Environment abstraction |
| `src/m68xkcpu/jit_mem_test.h` | Test environment (flat memory) |
| `src/m68xkcpu/jit_mem_pistorm.h` | PiStorm environment (real hardware) |
| `src/m68xkcpu/generated/jit_68000_opinfo.*` | Opcode metadata |

## Architecture Principles

1. **Musashi is the oracle** - its behavior defines correctness
2. **Test harness is isolated** - no hardware, no complexity
3. **Backend is shared** - both Musashi and JIT use same backend
4. **Endianness is handled** - backend handles BE↔LE conversion
5. **Function codes matter** - always pass correct FC for access type

## Next Steps

1. Implement MOVE translator in `src/m68xkcpu/jit_translate_move.c`
2. Wire up translator in `jit_block.c`
3. Run validation: `python3 tools/jit_vs_musashi.py --suite-dir ... --mode quick`
4. Fix any mismatches
5. Repeat for next instruction family
