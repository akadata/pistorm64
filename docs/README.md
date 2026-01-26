# Kernel PiStorm64 - Enhanced PiStorm Emulator

## Overview

Kernel PiStorm64 is a fork and rework of the original PiStorm emulator, designed 
to provide enhanced compatibility and new features for connecting Raspberry Pi
hardware to classic Amiga systems. This project maintains the core functionality
of the original PiStorm while adding 64-bit awareness and expanded capabilities.

## Origin and Licensing

This project is a fork of the original PiStorm developed by the PiStorm community. 
Full credit and thanks go to Claude and his repository from which we forked this 
code before reorganization: [https://github.com/captain-amygdala/pistorm](https://github.com/captain-amygdala/pistorm)

### Original License (PiStorm)
The original PiStorm software was released under the MIT License, 
as shown in `pistorm.LICENSE`:
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
This derivative work is also released under the MIT License,
continuing the open-source tradition of the original project.

## Key Features and Enhancements

### 64-Bit Awareness

- Full 64-bit compatibility for modern Raspberry Pi systems
- Tested on Pi4 8GB & Pi Zero 2 W with various Linux distributions
- Maintains backward compatibility with 32-bit systems

### PiSCSI64

- Enhanced version of PiSCSI that works in 64-bit environments
- Improved storage device emulation for Amiga systems
- Better performance and stability in 64-bit contexts

### Cross-Platform Compatibility

- Works on both musl libc and glibc systems
- Tested on Raspberry Pi4 8GB & Zero 2 W with:
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

### JanusD

- Read all about it in the janus branch

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

- Raspberry Pi (Zero 2 W, 3, Pi4B 8gb recommended)
- Compatible FPGA board with PiStorm CPLD
- Amiga computer (A500, A1000, or compatible)

**Note:** 
Latest code fully tested, developed and implimented on Pi 4 Model B+ 8GB, Should work on Pi400

Suggestions and contributions welcome!

### Build Process
```bash
# Build the main emulator
make

# Build specific tools
./build_regtool.sh
./build_clkpeek.sh
./build_pimodplay.sh
```

The default build prioritises correctness and diagnostics. A release /
performance-optimised build target will be introduced once behaviour is
fully validated across supported platforms.

### Kernel Module (Pi 4 Tuning)
Default (src=5 div=6):
```bash
sudo insmod pistorm.ko
```

Try a slower GPCLK if the CPLD/bus wiring is marginal (example div=12):
```bash
sudo insmod pistorm.ko gpclk_div=12
```

Try different source if needed (example src=6):
```bash
sudo insmod pistorm.ko gpclk_src=6 gpclk_div=12
```

## Current State

- ✅ P96 RTG works with minor window decoration bugs
- ✅ A314 works (Mounting, Read and Write of ADF files functional)
- ✅ Networking works
- ✅ PiSCSI works (92%+ warning reduction achieved)
- ✅ Everything works with significantly improved code quality and stability

**Known Performance Characteristics:**

- Chip RAM path remains a known slow path (by design)
- Storage access follows documented performance characteristics
- Read speeds on PI4 8GB model with SSD top out at around 0.5GB/second (500MB/s)
- Write speeds as documented earlier in the project

**Code Quality Status:**

- ✅ All code except Musashi M68k CPU emulator has been fully corrected
- ✅ Standardized integer types (uint8_t, uint16_t, uint32_t) implemented
  throughout
- ✅ Proper alignment and void pointer usage completed across all modules
- ⚠️ Musashi M68k CPU emulator warning cleanup planned but not yet
  implemented

**RTG/P96 Setup Note:**
- Raylib in src must be rebuilt by the installer for RTG and P96 to fully
  work (we are aware of this)

## compiler + IDE + static analyser + rubber duck… with a mouth.

## Architecture Notes

Based on the bus interface notes:

- Data bus: raw 16-bit, always present on GPIO 8–23 ↔ D0–D15
- Address bus: multiplexed over two register writes using GPIO2/3 as register select
- GPCLK0 on GPIO4 drives CPLD at high frequency (nominal 200 MHz)
- Compatible with both Amiga (USERCODE 0x00185866) and Atari (USERCODE 0x0017F4B8) bitstreams

## Contributing

This project builds upon the excellent work of the original PiStorm developers. 
Contributions are welcome, especially for:

- Testing on additional platforms
- Improving 64-bit compatibility
- Expanding direct hardware access features
- Documentation improvements

## Acknowledgments

- Original PiStorm developers for the foundational work
- Claude for the repository we forked from
- The Amiga community for continued support and inspiration
- Raspberry Pi Foundation for the hardware platform
- Special thanks to our AI assistant Qwen for acting as "compiler + IDE +
  static analyser + rubber duck… with a mouth" during the warning cleanup
  process


Upstream foundations:
  • PiStorm by the captain-amygdala and contributors
  • Musashi 68k core by Karl Stenerud and contributors
  • SoftFloat by John R. Hauser
  • Picasso96, A314, PiSCSI and the wider Amiga community

Tooling and assistance:
  • Built with GCC/Clang, Make, vim, and a lot of bustest.
  • Heavy use of AI code assistants (Qwen/Codex/GPT-style), acting
    as "compiler + IDE + static analyser + rubber duck… with a mouth."
  • Structural risk reduction: 92%+ warning elimination across all
    modules except Musashi M68k CPU emulator
  • Type safety: Standardized integer types (uint8_t, uint16_t, uint32_t) 
    and proper alignment implemented throughout

## Kernel
  • Custom kernel config used on the Pi4 for USB3 SSD Boot
    for optimal performance and features optionally recompile 
    a custom kernel with the .config file below
    kernel_module/rpi_kernel.config-6.18.6-v8+
  • Fully tested with upstream  rpi-6.12 kernel


## Disclaimer

This project is provided as-is under the MIT License. 
Please ensure you have proper hardware knowledge before connecting any devices. 
The authors are not responsible for any hardware damage resulting from improper use.

## Faith
  • Pride comes before a fall and the fear of God is the begining of wisdom.
  • 
  • In the beginning was the Word, and the Word was with God,
    and the Word was God. He was with God in the beginning.
    All things came to be through him, and without him nothing made had being.
    In him was life, and the life was the light of mankind.
    The light shines in the darkness, and the darkness has not suppressed it.


