# QWEN.md

## Project goal

Build a test-driven AArch64 JIT for the Motorola 68000 CPU used by pistorm64.

Musashi remains the architectural reference interpreter.
The JIT must produce identical CPU state, memory effects, and exceptions.

Correctness is always more important than speed.

All behaviour must pass the vendored ProcessorTests corpus and the Musashi reference test suite.

---

# Current Project State

The project has already achieved a clean interpreter baseline.

All Musashi reference tests currently pass:

```
SUMMARY mode=68040 pass=18 fail=0 xfail=0 xpass=0 miss=0 total=18
```

68000 instruction tests are also passing.

ProcessorTests results:

Old corpus:

```
files=124
 tests=1,000,060
 transactions=5,190,532
 errors=0
```

New corpus:

```
files=127
 tests=317,500
 transactions=1,783,580
 errors=0
```

Both test sets are wired into CI.

The project now has a verified interpreter baseline suitable for JIT development.

---

# Test Infrastructure

The following targets must remain passing at all times:

```
make processortests-quick
make processortests-full

make processortests-quick-new
make processortests-full-new

make musashi-ref-tests
```

The compare utility also exists:

```
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

# Opcode Metadata Source

Opcode metadata is generated from:

```
third_party/ProcessorTests/680x0/map/68000.official.json
```

Opcode JSON  with later mc68000
```
third_party/ProcessorTests/680x0/68000/v1/
third_party/ProcessorTests/m68000/v1/
```
This produces a 65536-entry opcode metadata table.

Each entry should include:

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

Interpreter implementation:

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

The JIT must **not modify these files heavily**.

---

# JIT Development Location

All new JIT work must live in:

```
src/m68xkcpu/
```

This directory is currently empty and intentionally reserved for the JIT implementation.

The goal is to keep the interpreter and JIT clearly separated.

Musashi provides semantics.

m68xkcpu provides acceleration.

---

# Suggested Directory Structure

```
src/m68xkcpu/

jit.h
jit.c

jit_block.h
jit_block.c

jit_cache.h
jit_cache.c

jit_emit_aarch64.h
jit_emit_aarch64.c

jit_translate.h
jit_translate.c

jit_translate_move.c
jit_translate_alu.c
jit_translate_branch.c

jit_sync.h
jit_sync.c

jit_opinfo.h
jit_opinfo.c

/generated/
    jit_68000_opinfo.c
    jit_68000_opinfo.h
```

Generated metadata should be placed in the generated folder.

---

# JIT Execution Model

Execution pipeline:

1. fetch opcode
2. lookup compiled block in JIT cache
3. execute compiled block if present
4. otherwise fall back to Musashi interpreter
5. interpreter executes instruction
6. block may be compiled after execution

Unsupported instructions must always fall back to Musashi.

---

# Block Termination Conditions

Blocks should terminate when encountering:

* branches
* traps
* interrupts
* RTS
* RTE
* unknown instructions

Keeping blocks small simplifies invalidation.

---

# Memory Correctness

Self-modifying code must invalidate compiled blocks.

Any write to memory that belongs to compiled code must invalidate that block.

Memory behaviour must match interpreter behaviour exactly.

---

# Exception Handling

The JIT must synchronize CPU state before:

* exceptions
* traps
* interrupts
* interpreter fallback

The interpreter must see a consistent CPU state.

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
basic shifts
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
```

These metrics help evaluate performance improvements.

---

# Development Order

1. opcode metadata generator
2. JIT block cache
3. minimal AArch64 emitter
4. MOVE translator
5. arithmetic translators
6. branch translators
7. differential testing vs interpreter
8. expand coverage

Do not attempt 020/030/040 JIT support until the 68000 path is stable.

---

# Coding Guidelines

* keep code readable
* avoid giant functions
* prefer modular translators
* do not remove interpreter paths
* always run the full test suites

---

# Project Philosophy

Correctness first.
Architecture clarity second.
Speed third.

A fast emulator that is incorrect is not useful.

