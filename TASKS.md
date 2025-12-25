# PiStorm Build Tasks

## Overview
This document outlines the tasks required to fix the build issues in the PiStorm emulator, particularly the linking issue with m68ki_ic_clear function and 64-bit compatibility.

## Current Build Issues

1. **Linker Error**: `undefined reference to 'm68ki_ic_clear'` in both `m68kcpu.o` and `m68kops.o`
2. **Architecture Mismatch**: Using 32-bit ARM flags on 64-bit ARM (aarch64) architecture
3. **PiSCSI 64-bit Issues**: Potential alignment and data type issues for 64-bit systems

## Completed Tasks

### Task 1: Fix m68ki_ic_clear Linking Issue
- **Status**: Completed
- **Description**: The inline function `m68ki_ic_clear` in m68kcpu.h was not being properly linked
- **Solution**: Modified the Makefile to add `-DINLINE_INTO_M68KCPU_H=1` compiler flag to ensure inline functions are properly exported for linking
- **Priority**: High

### Task 2: Update Build Flags for 64-bit ARM
- **Status**: Completed
- **Description**: Removed invalid ARM flags (`-mfloat-abi=hard`, `-mfpu=neon-fp-armv8`) that don't work on aarch64
- **Solution**: Updated Makefile with appropriate aarch64 flags
- **Priority**: High

### Task 3: Test Build Process
- **Status**: Completed
- **Description**: Verified that the build completes successfully after changes
- **Solution**: Ran `make clean && make PLATFORM=PI_64BIT` to ensure no linking errors
- **Priority**: High

## Remaining Tasks

### Task 4: Address PiSCSI 64-bit Alignment Issues
- **Status**: Pending
- **Description**: Fix format specifiers and pointer-to-integer casts for 64-bit compatibility
- **Solution**:
  - Replace `%llu/%lld` with `PRIu64/PRId64` or consistent casting
  - Replace "pointer cast to uint32_t" patterns with `uintptr_t`
- **Priority**: Medium

### Task 5: Verify Runtime Functionality
- **Status**: Completed
- **Description**: Ensure the emulator runs correctly after build fixes
- **Solution**: Test basic functionality of the emulator with timeout command - emulator runs successfully
- **Priority**: Medium

### Task 6: Add Copyrighted Files to .gitignore
- **Status**: Completed
- **Description**: Add files with copyrighted material to .gitignore to prevent committing them
- **Solution**: Updated .gitignore with copyrighted files
- **Priority**: High

### Task 7: Organize Changes into Separate Git Branches
- **Status**: Pending
- **Description**: Create separate branches for each feature following one-change-per-branch principle
- **Solution**: Follow the plan in COMMIT_PLAN.md to create separate branches for:
  - m68kcpu inline function fix
  - 64-bit platform support
  - Documentation
  - Default 64-bit build
- **Priority**: High

### Task 8: Document aarch64 Build Environment
- **Status**: Completed
- **Description**: Document exact GCC and Make options needed for systemd-nspawn aarch64 build environment
- **Solution**: Created AARCH64_BUILD_ENV.md with complete build environment documentation
- **Priority**: High

## Dependencies
- Task 4 can be done independently
- Task 7 requires all changes to be properly organized

## Expected Outcomes
- Successful compilation on 64-bit ARM systems (Raspberry Pi Zero W2)
- Proper linking of all required functions
- 64-bit compatibility for PiSCSI module
- Maintained functionality of the emulator
- No copyrighted material committed to repository
- Clean Git history with one feature per branch