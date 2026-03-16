# JIT Validation Suite Status

## Overview

This is the validation infrastructure for a staged AArch64 JIT effort, beginning with **68000 correctness first**. Later CPU and FPU variants may be added only after the 68000 JIT path is stable and fully validated.

**Design Principles (from QWEN.md):**
- Musashi remains the architectural reference / oracle
- Correctness is always more important than speed
- JIT support for 020/030/040 comes later
- FPU support is later still

**Critical Architecture Note:**

There are **TWO distinct execution environments**:

1. **Validation Harness (Test Environment)**
   - Flat memory buffer (deterministic, no hardware)
   - No PiStorm backend
   - Pure 68000 instruction semantics
   - ProcessorTests JSON → flat RAM → Musashi/JIT → compare state

2. **Real Emulator (PiStorm Runtime)**
   - PiStorm backend (`src/pistorm/backend.*`)
   - Real Amiga memory map, MMU, chipset
   - Function codes, bus behavior, timing
   - JIT → `m68k_read/write` → PiStorm backend → Amiga memory

**THESE MUST NOT BE MIXED.**

The validation harness validates instruction semantics in isolation.
The real emulator integrates the validated JIT with actual hardware.

See `jit_arch.h` for environment abstraction and `jit_mem_*.h` for implementations.

## Current Implementation Status

### ✅ IMPLEMENTED

| Component | Status | Notes |
|-----------|--------|-------|
| JSON structure validation | ✅ Complete | Validates ProcessorTests JSON format |
| Test discovery | ✅ Complete | Finds all .json files in suite directory |
| Test counting | ✅ Complete | Reports cases per file |
| CPU state extraction | ✅ Complete | Extracts D0-D7, A0-A7, PC, SR, USP, SSP |
| State comparison | ✅ Complete | `compare_states()` diffs all registers |
| Quick mode | ✅ Complete | Runs first 10 test files |
| Full mode | ✅ Complete | Runs all test files |

### ⏳ PENDING

| Component | Status | What's Needed |
|-----------|--------|---------------|
| Musashi execution | ⏳ Stub only | Instruction fetch functions (m68ki_read_program_16/32) need proper implementation |
| JIT execution | ⏳ Pending | JIT integration with test harness |
| Memory setup from JSON | ✅ Implemented | RAM initialization working |
| Per-instruction execution | ⏳ Pending | Requires Musashi execution fix |
| Transaction validation | ⏳ Pending | Verify memory access patterns |

**Note:** A Musashi JSON driver exists at `tools/musashi_json_driver.c` with:
- ✅ JSON parsing working
- ✅ CPU state initialization working  
- ✅ RAM loading working
- ⏳ Instruction execution needs m68ki_read_program_* functions

The Musashi core uses macros for instruction fetch that need state parameter. Fixing this requires either:
1. Defining `m68ki_read_program_16/32` with correct signature
2. Or modifying m68kconf.h to use direct memory functions

## Usage

### Validate JSON Structure (Current Capability)

```bash
# Quick validation (10 files)
python3 tools/jit_vs_musashi.py \
    --suite-dir third_party/ProcessorTests/680x0/68000/v1 \
    --mode quick

# Full validation (all files)
python3 tools/jit_vs_musashi.py \
    --suite-dir third_party/ProcessorTests/680x0/68000/v1 \
    --mode full

# Single test file
python3 tools/jit_vs_musashi.py \
    --test third_party/ProcessorTests/680x0/68000/v1/ADD.b.json
```

### With Execution (When Implemented)

```bash
# Run with Musashi execution
python3 tools/jit_vs_musashi.py \
    --suite-dir third_party/ProcessorTests/680x0/68000/v1 \
    --mode quick \
    --musashi

# Run with JIT execution (future)
python3 tools/jit_vs_musashi.py \
    --suite-dir third_party/ProcessorTests/680x0/68000/v1 \
    --mode quick \
    --jit
```

## ProcessorTests JSON Format

Each test case in the JSON files has this structure:

```json
{
  "name": "test name",
  "initial": {
    "d0": 1234567890,
    "d1": ...,
    ...
    "a0": ...,
    ...
    "usp": ...,
    "ssp": ...,
    "sr": 12345,
    "pc": 3072,
    "prefetch": [word1, word2],
    "ram": [[address1, value1], [address2, value2], ...]
  },
  "final": {
    ... same structure as initial ...
  },
  "length": 4,
  "transactions": [["r", 4, 6, 3076, ".w", 12345], ...]
}
```

## Next Steps for Full Execution Support

### 1. Musashi JSON Test Driver (C)

Need to create a new C driver that:
1. Parses ProcessorTests JSON format
2. Sets up Musashi CPU state from `initial` dict
3. Initializes memory from `ram` entries
4. Executes exactly one instruction
5. Outputs final CPU state as JSON

Location: `tools/musashi_json_driver.c`

### 2. JIT Test Integration

Once JIT execution is wired up:
1. Add `--jit` flag to Python script
2. Implement `run_jit_test()` to call JIT execution
3. Compare JIT output against Musashi output

### 3. Makefile Integration

```makefile
jit-validate-quick:
	python3 tools/jit_vs_musashi.py --suite-dir $(PROCESSORTESTS_SOURCE) --mode quick

jit-validate-full:
	python3 tools/jit_vs_musashi.py --suite-dir $(PROCESSORTESTS_SOURCE) --mode full

jit-validate-musashi:
	python3 tools/jit_vs_musashi.py --suite-dir $(PROCESSORTESTS_SOURCE) --mode quick --musashi
```

## Files

| File | Purpose |
|------|---------|
| `tools/jit_vs_musashi.py` | Main test runner script |
| `src/m68xkcpu/VALIDATION_STATUS.md` | This status document |
| `tools/musashi_ref_test_driver.c` | Existing driver (for .bin files, not JSON) |
| `tools/run_musashi_ref_tests.sh` | Existing test runner script |

## Test Corpus

- **Location**: `third_party/ProcessorTests/680x0/68000/v1/`
- **Format**: JSON (one file per instruction family)
- **Size**: ~8000 test cases per file
- **Coverage**: Full 68000 instruction set

## Comparison with Existing Tests

| Test Type | Format | Execution | Purpose |
|-----------|--------|-----------|---------|
| `musashi-ref-tests` | Binary (.bin) | Yes | Musashi regression |
| `processortests-quick` | Binary (generated) | Yes | ProcessorTests validation |
| `jit_vs_musashi.py` | JSON | No (yet) | JIT vs Musashi differential |

The JSON format is more detailed than the binary format - it includes
full CPU state before/after, not just pass/fail. This makes it ideal
for differential testing where we need to compare exact register values.

---

## Summary

**The validation infrastructure is now established.** The Python runner fully supports both vendored ProcessorTests corpora for discovery, parsing, state extraction, and result comparison scaffolding. The native Musashi JSON driver is in place and initializes CPU state and RAM correctly, however still needs the final Musashi program-read execution path completed before it becomes a full interpreter-backed correctness runner.

### What's Working:

| Component | Status |
|-----------|--------|
| Corpus walking (both 680x0 and m68000) | ✅ Complete |
| JSON structure validation | ✅ Complete |
| State extraction | ✅ Complete |
| Comparison scaffolding | ✅ Complete |

### What's Stubbed:

| Component | Status |
|-----------|--------|
| Musashi JSON driver execution | ⏳ Needs `m68ki_read_program_*` functions |
| JIT execution integration | ⏳ Pending (by design) |

### Next Steps (In Order):

1. **Finish Musashi JSON driver read/execute path** - Complete the `m68ki_read_program_16/32` functions so Musashi can actually fetch and execute instructions
2. **Validate expected final state against Musashi** - Run full test corpus through Musashi and verify output matches ProcessorTests `final` state
3. **Wire JIT execution into the same harness** - Only after steps 1-2 are passing, add `--jit` flag to compare JIT output against Musashi

**This order matters.** It keeps the interpreter-backed runner as the truth before the JIT starts making noise.
