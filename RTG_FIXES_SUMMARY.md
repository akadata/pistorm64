# RTG Fixes Summary

## Overview
This document summarizes the fixes applied to resolve the PiGFX RTG memory addressing issue where the system was trying to access addresses >= 0x80000000, causing the "Suspend #80000004" error.

## Root Cause
The issue was caused by improper memory address translation between Amiga-side addresses and ARM-side memory access. When Amiga programs passed memory addresses to RTG functions, these addresses were being converted incorrectly, resulting in rtg_address_adj values that exceeded the allocated RTG memory bounds.

## Fixes Applied

### 1. Bounds Checking in RTG GFX Functions
Added bounds checking to all functions that access rtg_mem to prevent out-of-bounds access:

- `rtg_fillrect_solid()`
- `rtg_fillrect()`
- `rtg_invertrect()`
- `rtg_blitrect()`
- `rtg_blitrect_solid()`
- `rtg_blitrect_nomask_complete()`
- `rtg_blittemplate()`
- `rtg_blitpattern()`
- `rtg_drawline_solid()` (with enhanced per-pixel bounds checking)
- `rtg_drawline()` (with enhanced per-pixel bounds checking)

### 2. Address Validation in RTG Command Handlers
Added validation to prevent rtg_address_adj values from exceeding the RTG memory bounds:

- iRTG DrawLine command handler
- iRTG FillRect command handler
- iRTG InvertRect command handler
- iRTG BlitRect command handler
- iRTG BlitTemplate command handler
- iRTG BlitPattern command handler
- iRTG P2C command handler

### 3. Register Write Validation
Added validation for RTG_ADDR1 and RTG_ADDR2 register writes to ensure address adjustments stay within bounds.

### 4. Safe Clamping Strategy
When out-of-bounds addresses are detected:
- Log a warning message if realtime_graphics_debug is enabled
- Clamp the address to 0 (beginning of RTG memory) instead of allowing access to invalid memory

## Configuration Recommendations

Based on the diagnostic information provided, the Amiga-side PiGFX configuration should use these tooltypes:

```
BOARDTYPE=ZORRO2
ZORRO2=YES
ADDRESS=0x20000000
MEMORY=64
```

Avoid using:
- ZORRO3
- ADDRESS=0x80000000
- ADDRESS=AUTO

## Impact
These fixes should resolve the "#80000004" error by ensuring that all RTG memory accesses remain within the allocated 40MB RTG memory space, while providing appropriate fallback behavior when invalid addresses are encountered.