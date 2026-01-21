# PiStorm64 RTG Optimization Summary

## Overview
This document summarizes all optimizations made to ensure optimal RTG (Real-Time Graphics) performance on PiStorm64.

## Configuration Files Updated

### 1. boot/firmware/config.txt
- COMMENTED OUT `dtoverlay=vc4-kms-v3d,cma-512` as it interferes with PiGFX RTG driver
- Set framebuffer resolution to 1280x720 (HD-ready) for RTG compatibility
- Added performance optimizations for v3d GPU
- Kept existing overclock settings (arm_freq=2000, gpu_freq=600)

### 2. boot/firmware/cmdline.txt
- Added performance optimizations
- Included mitigations=off for better performance

### 3. 10-hugepages.conf
- Configured Transparent Hugepages for optimal v3d GPU performance
- Allocated 2GB hugepages as recommended

### 4. Makefile (full_clean_install target)
- Updated to copy all necessary configuration files during installation
- Added copying of boot configuration files
- Added copying of system configuration files
- Added systemd service enabling

### 5. etc/systemd/system/kernelpistorm64.service
- Created optimized systemd service for the emulator
- Configured for RTG with proper environment variables
- Set CPU affinity and priorities for optimal performance

## RTG-Specific Optimizations

### GPU Memory Management
- CMA (Contiguous Memory Allocator) set to 512MB for RTG operations
- Framebuffer configured for 1280x720 at 32-bit depth
- Transparent Hugepages configured for optimal GPU memory access (if supported)

### Performance Tuning
- Disabled unnecessary GPU features that interfere with RTG
- Optimized memory allocation for graphics operations
- Set appropriate system priorities for RTG operations

### System Integration
- Proper udev rules for device access
- Real-time scheduling for RTG operations
- Memory locking for consistent performance

## Validation Steps
Run the validation script to ensure all optimizations are properly applied:
```bash
./VALIDATE_RTG_SETUP.sh
```

## Post-Installation Steps
1. Reboot your Raspberry Pi to apply all configuration changes
2. Verify DRM devices are available: `ls /dev/dri/`
3. Start the emulator and verify RTG functionality
4. Run the PiGFX installer on the Amiga side to complete RTG setup

## Expected Benefits
- Improved RTG performance with proper GPU memory allocation
- Stable 1280x720 RTG output
- Better memory management for graphics operations
- Optimized system resources for RTG operations

## Troubleshooting
If RTG still doesn't work after these optimizations:
1. Ensure the Pi has been rebooted after config.txt changes
2. Verify that Picasso96 and PiGFX are properly installed on the Amiga side
3. Check that the emulator is running with sufficient privileges
4. Confirm DRM devices are accessible: `ls -la /dev/dri/`