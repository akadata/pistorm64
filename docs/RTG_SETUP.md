# RTG Setup Guide for PiStorm64

## Overview
This guide explains how to set up RTG (Real Time Graphics) on your PiStorm64 system using the raylib DRM backend.

## Prerequisites
- PiStorm64 hardware properly connected
- Working emulator build with RTG support
- Picasso96 installed on your Amiga
- PiGFX card files properly configured

## Raylib Backend Configuration

The PiStorm64 emulator supports multiple raylib backends:
- `./src/raylib_drm` - Direct Rendering Manager (recommended for Pi4)
- `./src/raylib_pi4_test` - Alternative Pi4-specific implementation
- Standard raylib - General purpose (may not work well on embedded systems)

## Current Configuration
The Makefile has been updated to use the `./src/raylib_drm` backend for all Pi4 platforms (PI4, PI4_64BIT, PI4_64BIT_DEBUG) as this is typically the most reliable option for Raspberry Pi systems.

## Building with RTG Support

To build the emulator with RTG support:

```bash
make clean
make PLATFORM=PI4_64BIT  # or PI4 for 32-bit
```

To explicitly enable RTG (though it's enabled by default):
```bash
make PLATFORM=PI4_64BIT USE_RAYLIB=1
```

To disable RTG for testing purposes:
```bash
make PLATFORM=PI4_64BIT USE_RAYLIB=0
```

## Installing PiGFX on the Amiga Side

1. Copy the PiGFX Install files to your Amiga work disk
2. Run the PiGFX installer from the Files drawer
3. Point it to your original Picasso96 installation folder
4. The installer will create the necessary card files and monitor settings

## Troubleshooting Common RTG Issues

### No Screen Modes Available
- Verify that raylib_drm is properly compiled and linked
- Check that the PiStorm64 emulator has permission to access DRM devices
- Ensure you're running the emulator with sufficient privileges

### Black Screen or No Display
- Check that your display is properly connected and powered
- Verify that the PiStorm64 kernel module is loaded
- Try running the emulator with different display settings

### Performance Issues
- Ensure your Pi is adequately powered (official power supply recommended)
- Check for thermal throttling
- Verify that GPU memory split is adequate (512MB or higher recommended)

### Building raylib_drm
If you're having issues with the raylib_drm backend, you may need to rebuild it:

```bash
cd src/raylib_drm
# Follow the build instructions for raylib DRM backend
```

## Testing RTG Functionality

After setup:
1. Start the emulator with RTG enabled
2. Boot your Amiga with Picasso96 and PiGFX drivers installed
3. Check if new screen modes are available in Preferences > Screen Mode
4. Try opening a RTG screen to verify functionality

## Known Issues
- Some older Amiga software may not work properly with RTG
- Certain screen modes might not be available depending on your display
- Performance may vary based on the complexity of graphics being rendered

## Additional Resources
- Join the PiStorm Discord community for support
- Check the official PiStorm documentation
- Review raylib documentation for DRM backend specifics