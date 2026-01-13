# PiStorm64 - Enhanced PiStorm Emulator

## Overview

PiStorm64 is a fork and rework of the original PiStorm emulator, designed to provide enhanced compatibility and new features for connecting Raspberry Pi hardware to classic Amiga systems. This project maintains the core functionality of the original PiStorm while adding 64-bit awareness and expanded capabilities.

## Origin and Licensing

This project is a fork of the original PiStorm developed by the PiStorm community. Full credit and thanks go to Claude and his repository from which we forked this code before reorganization: [https://github.com/captain-amygdala/pistorm](https://github.com/captain-amygdala/pistorm)

### Original License (PiStorm)
The original PiStorm software was released under the MIT License, as shown in `pistorm.LICENSE`:
```
Copyright (c) 2021 PiStorm developers

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### Our License (MIT)
This derivative work is also released under the MIT License, continuing the open-source tradition of the original project.

## Key Features and Enhancements

### 64-Bit Awareness
- Full 64-bit compatibility for modern Raspberry Pi systems
- Tested on Pi Zero 2 W with various Linux distributions
- Maintains backward compatibility with 32-bit systems

### PiSCSI64
- Enhanced version of PiSCSI that works in 64-bit environments
- Improved storage device emulation for Amiga systems
- Better performance and stability in 64-bit contexts

### Cross-Platform Compatibility
- Works on both musl libc and glibc systems
- Tested on Raspberry Pi Zero 2 W with:
  - Debian Trixie
  - Raspbian Buster
  - Alpine Linux

### New Tools Added

#### regtool
- Register access tool for low-level hardware interaction
- Allows direct manipulation of Amiga custom chips registers
- Built for debugging and development purposes

#### clkpeek
- Clock monitoring and analysis tool
- Helps diagnose timing issues between Pi and Amiga
- Useful for performance optimization

#### pimodplay
- **First tool demonstrating direct Amiga communication**
- Enables non-emulation code to talk directly to Amiga 500 OCS/ECS hardware
- Bypasses the emulator for specific operations
- Located in `src/platforms/amiga/registers/`

### Direct Hardware Access Registers
- Added new registers to enable direct communication with Amiga hardware
- Allows code to interact with Amiga 500 OCS/ECS without running the full emulator
- Located in `src/platforms/amiga/registers/` directory
- Includes header files for: agnus.h, amiga_custom_chips.h, blitter.h, cia.h, denise.h, paula.h

### PiRTG Support
- Raytracing graphics support (needs testing)
- Enhanced graphics capabilities for Amiga systems
- Optional RTG implementation using raylib (can be disabled via `USE_RAYLIB=0`)

## Building and Installation

### Prerequisites
- Raspberry Pi (Zero 2 W, 3 recommended)
- Compatible FPGA board with PiStorm CPLD
- Amiga computer (A500, A1000, or compatible)

**Note:** Pi 4 and 400 are not currently supported but work is in progress (WIP). Suggestions and contributions welcome!

### Build Process
```bash
# Build the main emulator
make

# Build specific tools
./build_regtool.sh
./build_clkpeek.sh
./build_pimodplay.sh
```

## Testing Status

- ✅ Pi Zero 2 W with Debian Trixie
- ✅ Pi Zero 2 W with Raspbian Buster  
- ✅ Pi Zero 2 W with Alpine Linux
- ⚠️ PiRTG - Needs additional testing
- ✅ Direct register access functionality
- ✅ pimodplay demonstration tool

## Architecture Notes

Based on the bus interface notes:
- Data bus: raw 16-bit, always present on GPIO 8–23 ↔ D0–D15
- Address bus: multiplexed over two register writes using GPIO2/3 as register select
- GPCLK0 on GPIO4 drives CPLD at high frequency (nominal 200 MHz)
- Compatible with both Amiga (USERCODE 0x00185866) and Atari (USERCODE 0x0017F4B8) bitstreams

## Contributing

This project builds upon the excellent work of the original PiStorm developers. Contributions are welcome, especially for:
- Testing on additional platforms
- Improving 64-bit compatibility
- Expanding direct hardware access features
- Documentation improvements

## Acknowledgments

- Original PiStorm developers for the foundational work
- Claude for the repository we forked from
- The Amiga community for continued support and inspiration
- Raspberry Pi Foundation for the hardware platform

## Disclaimer

This project is provided as-is under the MIT License. Please ensure you have proper hardware knowledge before connecting any devices. The authors are not responsible for any hardware damage resulting from improper use.