# DRM/KMS Setup for Raspberry Pi

## Overview
To enable the DRM/KMS (Direct Rendering Manager/Kernel Mode Setting) subsystem required for raylib's DRM backend, you need to configure the appropriate overlay in your Raspberry Pi's config.txt file.

## Current Issue
Your system is missing `/dev/drm` devices, which are required for the raylib DRM backend used by PiStorm64's RTG functionality.

## Solution
Add the following line to your `/boot/firmware/config.txt` file:

```
dtoverlay=vc4-kms-v3d
```

## Complete Updated Config
Your `/boot/firmware/config.txt` should look like this:

```
# /boot/firmware/config.txt

arm_64bit=1
auto_initramfs=1
enable_uart=0

# Let firmware pick best turbo behaviour for the board
arm_boost=1

# Overclock
arm_freq=2000
gpu_freq=600

force_turbo=1

# If pushing to 2000 later, add ONE line like:
over_voltage_delta=2

# Enable DRM/KMS for direct rendering (required for RTG)
dtoverlay=vc4-kms-v3d,cma-512
```

## Additional Options
Since you have an 8GB Raspberry Pi 4, you can allocate more contiguous memory for GPU operations:

```
dtoverlay=vc4-kms-v3d,cma-512
```

For even more demanding RTG operations, you could try:

```
dtoverlay=vc4-kms-v3d,cma-768
```

However, be aware that allocating too much CMA (Contiguous Memory Allocator) memory may reduce available system RAM for other operations.

## After Making Changes
1. Save the config.txt file
2. Reboot your Raspberry Pi for the changes to take effect
3. After reboot, verify that DRM devices are available:
   ```bash
   ls -la /dev/dri/
   ```
   
   You should see devices like:
   - `/dev/dri/card0`
   - `/dev/dri/renderD128`

## Troubleshooting
If DRM devices still don't appear after reboot:
- Check that your kernel supports DRM/KMS
- Verify that the vc4 driver is loaded: `lsmod | grep vc4`
- Ensure you're not using legacy graphics drivers

## Note on Performance
Enabling the DRM/KMS overlay may slightly impact performance, but it's required for proper RTG functionality. The performance benefits of the PiStorm64 emulator should still far outweigh any minor overhead from the graphics subsystem.