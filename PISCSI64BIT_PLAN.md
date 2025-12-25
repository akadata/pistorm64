# PiSCSI 64-bit Alignment Plan

## Overview
This document outlines the plan to address 64-bit alignment issues in the PiSCSI (Pi SCSI) module that currently prevents proper compilation and operation on 64-bit systems.

## Current Issues Identified

### 1. Platform/Architecture Mismatch
- The build system assumes 32-bit ARM architecture but is running on 64-bit ARM (aarch64)
- Compiler flags like `-mfloat-abi=hard` and `-mfpu=neon-fp-armv8` are not valid for aarch64
- Memory layout and pointer sizes differ between 32-bit and 64-bit systems

### 2. Data Structure Alignment
- Potential issues with structure packing and alignment in SCSI command structures
- Pointer arithmetic that assumes 32-bit pointers
- Data type size mismatches (int vs long on 64-bit systems)

### 3. Memory Addressing
- Issues with memory addresses that may be truncated or improperly handled on 64-bit systems
- Potential issues with large memory addresses that require 64-bit values

## Required Changes for 64-bit Compatibility

### 1. Compiler and Build System Adjustments
- Update Makefile to use appropriate flags for aarch64 architecture
- Remove or conditionally apply 32-bit specific flags
- Ensure proper definition of `_FILE_OFFSET_BITS=64` and related macros

### 2. Data Type Corrections
- Use fixed-width integer types (uint32_t, uint64_t) instead of platform-dependent types
- Review all pointer arithmetic and casting operations
- Ensure proper handling of size_t and ptrdiff_t types

### 3. Structure Packing
- Review all SCSI command structures for proper packing
- Ensure structures are aligned properly for 64-bit systems
- Add appropriate `#pragma pack` or `__attribute__((packed))` where needed

### 4. Function Declaration Fixes
- Address the `m68ki_ic_clear` function linking issue by ensuring proper visibility
- Review all inline functions that may have linking issues across modules

## Implementation Strategy

### Phase 1: Build System Fixes
- Update compiler flags in Makefile for aarch64 compatibility
- Remove invalid ARM-specific flags for 64-bit builds
- Test basic compilation with new flags

### Phase 2: Core Data Structure Alignment
- Audit all SCSI-related data structures for proper alignment
- Fix any pointer/integer casting issues
- Ensure all file I/O operations use 64-bit safe functions

### Phase 3: Memory Management
- Review memory allocation and access patterns
- Ensure proper handling of large memory addresses
- Fix any array indexing issues that assume 32-bit sizes

### Phase 4: Testing and Validation
- Test on 64-bit ARM systems (like Raspberry Pi Zero W2)
- Verify SCSI operations work correctly
- Ensure no performance degradation

## Files to Review

- `platforms/amiga/piscsi/piscsi.c` - Main PiSCSI implementation
- `m68kcpu.h` and `m68kcpu.c` - CPU core functions with linking issues
- `m68kops.c` - Generated operations with function references
- Makefile - Build system configuration
- All header files used by PiSCSI module

## Success Criteria

- Successful compilation on 64-bit ARM systems
- Proper SCSI functionality without alignment-related crashes
- No performance regression
- Maintained compatibility with existing 32-bit systems (if needed)

## Risks

- Changes may affect 32-bit compatibility if not done carefully
- Structure alignment changes could break compatibility with existing disk images
- Some optimizations may need to be architecture-specific

## Notes for the later PISCSI64BIT_PLAN.md work

The warnings you showed are 64-bit related (format specifiers, pointer-to-int casts), but they are warnings, not this link failure.

When you get to PiSCSI:

fix %llu / %lld to use PRIu64/PRId64 (or cast to unsigned long long consistently), or make the variable types match the format.

replace "pointer cast to uint32_t" patterns with uintptr_t (or compute offsets via offsetof() rather than (uint32_t)&field hacks). Those are big ones in RTG code.

But first: get the build deterministic via the inline fix.