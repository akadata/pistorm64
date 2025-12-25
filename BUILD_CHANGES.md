# Build Changes Documentation

## Overview
This document details all changes made to fix the build issues in the PiStorm emulator, particularly the linking issue with the m68ki_ic_clear function and 64-bit ARM compatibility.

## Changes Made

### 1. Fixed m68ki_ic_clear linking issue
**File**: Makefile
**Change**: Added compiler definition to handle inline function visibility
- Added `-DINLINE_INTO_M68KCPU_H=1` to all CFLAGS definitions in the Makefile
- This ensures the inline function m68ki_ic_clear is properly visible across compilation units

### 2. Updated compiler flags for 64-bit ARM compatibility
**File**: Makefile
**Change**: Removed invalid ARM-specific flags for aarch64 architecture
- Removed `-mfloat-abi=hard` flag (not valid for aarch64)
- Removed `-mfpu=neon-fp-armv8` flag (not valid for aarch64)
- Kept `-march=armv8-a` as it is valid for aarch64

### 3. Added new 64-bit platform configuration
**File**: Makefile
**Change**: Added PI_64BIT platform target
- Added new platform configuration specifically for 64-bit ARM systems
- Uses same flags as PI3_BULLSEYE but with the inline fix

### 4. Updated all compilation targets
**File**: Makefile
**Change**: Updated buptest and a314 compilation lines
- Removed invalid ARM flags from buptest compilation command
- Removed invalid ARM flags from a314/a314.o compilation command

## Build Results
- **Before**: Build failed with "undefined reference to 'm68ki_ic_clear'" linker error
- **After**: Build completes successfully with only warnings (no errors)
- The emulator binary is now generated successfully

## Warnings Still Present
The build still produces some 64-bit compatibility warnings:
- Format specifier mismatches (%lld with uint64_t)
- Pointer-to-integer cast warnings
- These are expected and do not prevent compilation

## Git Ignore Requirements
The following files contain copyright material and should be added to .gitignore:
- Amiga/kickstart.rom
- Amiga/myamiga.cfg
- Amiga/BlankPFS3-*
- Amiga/amiga.cfg
- Amiga/Workbench2.04-PS3-8GB.hdf
- pistorm/ (copied files)

## Test Command
To test the build:
```
timeout 30 sudo ./emulator --config myamiga.cfg
```
This will verify if the PiStorm SCSI functionality works with the build changes.