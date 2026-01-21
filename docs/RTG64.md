# RTG Setup for PiStorm64 (64-bit Migration Complete)

## Overview
This document outlines the RTG setup for PiStorm64 on 64-bit systems, including troubleshooting steps and verification procedures.

## Current Status
Based on emulator output, RTG is properly configured:
- ✅ RTG Enabled in emulator
- ✅ 39MB RTG buffer allocated (41,877,504 bytes)
- ✅ RTG screen dimensions: 1280x720
- ✅ DPMS enabled for RTG
- ✅ RTG memory mapped to range 70010000-727FFFFF

## RTG Driver Installation Checklist

### 1. Picasso96 Installation
- [ ] Ensure Picasso96 is installed on your Amiga system
- [ ] Verify Picasso96 version is compatible (v2.0 from Aminet or v3.02 from Individual Computers)

### 2. PiGFX Card Files
- [ ] Locate the PiGFX card files in your PiStorm distribution
- [ ] Run the PiGFX installer from `PDH1:data/a314-shared/rtg/PiGFX Install/`
- [ ] Point installer to original Picasso96 installation folder
- [ ] Verify card files are installed to `DH99:ENVARC/Picasso96/PiGFX/`

### 3. Monitor File Setup
- [ ] Ensure monitor file is installed to `DH99:MONITORFILES/PiGFX_MONITOR`
- [ ] Verify monitor file is properly configured for your display

### 4. Assignment Configuration
- [ ] Add to your `S:Startup-Sequence`:
  ```
  assign Pigfx: DH99:ENVARC/Picasso96/PiGFX
  ```

### 5. Picasso96 Configuration
- [ ] Run Picasso96 Setup from the System screen
- [ ] Select PiGFX as your graphics card
- [ ] Configure appropriate screen modes for your display

## Troubleshooting Steps

### If RTG is not appearing in Picasso96:
1. Check that the PiGFX card files are in the correct location
2. Verify the assignment is working: `iconinfo Pigfx:`
3. Ensure Picasso96 is properly installed and recognized

### If screen modes are not available:
1. Verify the monitor file is correctly installed
2. Check that the PiGFX card files match your Picasso96 version
3. Ensure the PiGFX driver is properly detected by Picasso96

### If display is not working:
1. Check that your Pi does NOT have the `vc4-kms-v3d` overlay enabled in config.txt (this interferes with PiGFX RTG)
2. Verify framebuffer settings are correct in config.txt
3. Ensure the emulator is running with sufficient privileges

## Verification Commands

### On the Pi side:
```bash
# Check DRM devices
ls -la /dev/dri/

# Check if vc4 module is loaded
lsmod | grep vc4

# Check DRM information
sudo dmesg | grep -i drm
```

### On the Amiga side:
- Open Picasso96 Setup and verify PiGFX card is detected
- Check available screen modes in Preferences > Screen Mode
- Test RTG functionality with a graphics application

## Performance Notes
- The emulator is configured with 39MB of RTG memory
- Screen resolution is set to 1280x720 (HD-ready)
- DPMS power management is enabled for the RTG display

## Known Issues
- Some older Amiga software may not work properly with RTG
- Certain screen modes might not be available depending on your display
- Performance may vary based on the complexity of graphics being rendered

## Next Steps
1. Install Picasso96 on your Amiga system if not already done
2. Run the PiGFX installer using the script provided in this repository
3. Configure Picasso96 to use the PiGFX card
4. Test RTG functionality with sample applications

## Success Indicators
- Picasso96 recognizes the PiGFX card
- New screen modes become available in Preferences
- Graphics applications can open RTG screens
- Performance is smooth and responsive

## Resources
- Join the PiStorm Discord community for support
- Check the official PiStorm documentation
- Refer to the Picasso96 documentation for advanced configuration