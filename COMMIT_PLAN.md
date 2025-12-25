# PiStorm Build Changes Commit Plan

## Overview
This document outlines how to properly commit the build changes to maintain a clean Git history with one feature per branch.

## Current Status
- Build issue with `m68ki_ic_clear` function has been resolved
- 64-bit ARM compatibility has been implemented
- Build now completes successfully with default settings
- Files modified but need proper commit organization

## Git Workflow Plan

### Step 1: Sync with upstream master
```bash
git checkout akadata/master
git pull origin akadata/master
```

### Step 2: Create branch for m68kcpu header fix
```bash
git checkout -b feature/m68kcpu-inline-fix akadata/master
# Add only the Makefile changes related to inline function
git add Makefile
git commit -m "Fix m68ki_ic_clear inline function visibility in m68kcpu.h"
git push origin feature/m68kcpu-inline-fix
# Create PR to merge into akadata/master
```

### Step 3: Create branch for 64-bit platform support
```bash
git checkout akadata/master
git checkout -b feature/64bit-platform-support akadata/master
# Add Makefile changes for 64-bit platform
git add Makefile
git commit -m "Add PI_64BIT platform support to Makefile"
git push origin feature/64bit-platform-support
# Create PR to merge into akadata/master
```

### Step 4: Create branch for documentation
```bash
git checkout akadata/master  
git checkout -b feature/build-documentation akadata/master
# Add documentation files
git add BUILD_CHANGES.md m68kcpu-buildissue.md PISCSI64BIT_PLAN.md TASKS.md .gitignore test_emulator.sh
git commit -m "Add build documentation and task tracking files"
git push origin feature/build-documentation
# Create PR to merge into akadata/master
```

### Step 5: Create branch for default 64-bit build
```bash
git checkout akadata/master
git checkout -b feature/default-64bit-build akadata/master
# Add Makefile changes to make 64-bit build default
git add Makefile
git commit -m "Make 64-bit build the default configuration"
git push origin feature/default-64bit-build
# Create PR to merge into akadata/master
```

## Files to be Committed

### Makefile Changes:
- Added `-DINLINE_INTO_M68KCPU_H=1` flag to fix inline function linking
- Removed invalid ARM flags (`-mfloat-abi=hard`, `-mfpu=neon-fp-armv8`) for aarch64
- Added PI_64BIT platform configuration
- Made 64-bit settings the default

### Documentation Files:
- `BUILD_CHANGES.md` - Documentation of all changes made
- `m68kcpu-buildissue.md` - Analysis of the original build issue
- `PISCSI64BIT_PLAN.md` - Plan for 64-bit PiSCSI compatibility
- `TASKS.md` - Task tracking document
- `.gitignore` - Added copyrighted files to ignore
- `test_emulator.sh` - Test script for emulator

## Important Notes
- The emulator binary (2MB) and other large files should NOT be committed
- Copyrighted files like kickstart.rom, HDF images, etc. are already in .gitignore
- Each feature should be merged separately to maintain clean history
- Test each branch individually before merging

## Next Steps
1. Implement the branching and commit plan above
2. Test each branch individually
3. Create pull requests for each feature
4. Once merged, test the final combined functionality