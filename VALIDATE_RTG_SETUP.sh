#!/bin/bash

echo "PiStorm64 RTG Setup Validation Script"
echo "====================================="

echo
echo "1. Checking DRM/KMS Overlay Configuration..."
if ! grep -q "dtoverlay=vc4-kms-v3d" /boot/firmware/config.txt; then
    echo "   ✓ DRM/KMS overlay is correctly DISABLED in config.txt (required for PiGFX RTG)"
else
    echo "   ⚠ DRM/KMS overlay is enabled in config.txt (may interfere with PiGFX RTG)"
fi

echo
echo "2. Checking Framebuffer Resolution Settings..."
if grep -q "framebuffer_width=1280" /boot/firmware/config.txt && grep -q "framebuffer_height=720" /boot/firmware/config.txt; then
    echo "   ✓ Framebuffer resolution set to 1280x720"
else
    echo "   ✗ Framebuffer resolution NOT set to 1280x720"
fi

echo
echo "3. Checking DRM Devices..."
if [ -d /dev/dri ] && [ -e /dev/dri/card0 ]; then
    echo "   ✓ DRM devices are available"
    ls -la /dev/dri/
else
    echo "   ⚠ DRM devices are NOT available - reboot may be required"
fi

echo
echo "4. Checking VC4 Module..."
if lsmod | grep -q vc4; then
    echo "   ✓ VC4 module is loaded"
else
    echo "   ⚠ VC4 module is NOT loaded"
fi

echo
echo "5. Checking Transparent Hugepages Configuration..."
if [ -f /etc/sysctl.d/10-hugepages.conf ]; then
    echo "   ✓ Hugepages configuration file exists"
    if sysctl vm.nr_hugepages 2>/dev/null | grep -q "vm.nr_hugepages = "; then
        echo "   ✓ Hugepages settings are loaded"
    else
        echo "   ⚠ Hugepages settings may not be supported on this system (this is OK)"
    fi
else
    echo "   ⚠ Hugepages configuration file does NOT exist"
fi

echo
echo "6. Checking PiStorm Kernel Module..."
if lsmod | grep -q pistorm; then
    echo "   ✓ PiStorm kernel module is loaded"
else
    echo "   ⚠ PiStorm kernel module is NOT loaded"
fi

echo
echo "7. Checking PiStorm Device Access..."
if [ -e /dev/pistorm ]; then
    echo "   ✓ /dev/pistorm device exists"
    ls -la /dev/pistorm
else
    echo "   ✗ /dev/pistorm device does NOT exist"
fi

echo
echo "8. Checking System Limits for RTG..."
if groups | grep -q pistorm; then
    echo "   ✓ Current user is in pistorm group"
else
    echo "   ⚠ Current user is NOT in pistorm group - add with: sudo usermod -aG pistorm $USER"
fi

echo
echo "9. Checking GPU Memory Allocation..."
if grep -q "gpu_mem=" /boot/firmware/config.txt; then
    gpu_mem=$(grep "gpu_mem=" /boot/firmware/config.txt | cut -d'=' -f2)
    echo "   ✓ GPU memory is set to ${gpu_mem}MB"
else
    echo "   ⚠ GPU memory may be using default allocation"
fi

echo
echo "10. Checking CMA Memory Allocation..."
if grep -q "cma-" /boot/firmware/config.txt; then
    cma_setting=$(grep "cma-" /boot/firmware/config.txt | cut -d',' -f2)
    echo "   ✓ CMA memory is set to ${cma_setting}"
else
    echo "   ⚠ CMA memory allocation not found"
fi

echo
echo "Validation Complete!"
echo
echo "Note: If any items show warnings (⚠), please address them."
echo "For items showing errors (✗), these need to be fixed for RTG to work."
echo
echo "After making changes to config.txt, reboot your Pi for settings to take effect."