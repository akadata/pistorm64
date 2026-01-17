# PiStorm Development Notes

## Current Status
- Working on fixing PiSCSI hangs when accessing large drives (DH1) on Pi Zero
- Identified that the issue occurs when SysInfo 4.4 performs disk speed tests
- The problem is related to CPU copy operations falling back to chip RAM access
- Recent fixes implemented to force FAST|PUBLIC memory allocation and add yielding to CPU copy loops

## Key Issues Identified
1. **PiSCSI hangs during intensive disk operations** - particularly with SysInfo 4.4 disk speed tests
2. **Chip RAM allocation** - Some operations fall back to chip RAM causing hangs
3. **CPU copy loops** - Need to add yielding to prevent emulator hangs
4. **Peripheral base addresses** - Different between Pi Zero (0x3F000000) and Pi4 (0xFE000000)

## Recent Fixes Applied
1. Forced FAST|PUBLIC memory allocation in DOS node setup to prevent chip RAM usage
2. Added yielding in CPU copy loops to prevent hangs during large operations
3. Batched CPU copy operations to reduce ioctl calls and prevent hangs

## Next Steps for Pi4 Migration
1. The Pi4 has different peripheral base address (0xFE000000 vs 0x3F000000 on Pi Zero)
2. Need to update both user-space and kernel-module code to support Pi4
3. The Makefile already has PI4 platform support with PLATFORM=PI4

## Platform Differences
- **Pi Zero/2/3**: BCM2708_PERI_BASE 0x3F000000
- **Pi 4**: BCM2708_PERI_BASE 0xFE000000
- Need conditional compilation to support both platforms

## Files to Update for Pi4 Support
- `src/gpio/ps_protocol.h` - peripheral base definitions
- `kernel_module/src/pistorm.c` - kernel module peripheral base
- Makefile - ensure proper compilation flags for Pi4

## Current Git Branch
- Branch: `akadata/machine-monitor`
- Latest commit: Contains fixes for PiSCSI hangs and memory allocation improvements