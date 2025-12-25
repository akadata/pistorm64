# aarch64 Build Environment for PiStorm

## Overview
This document details the exact GCC and Make options needed to build PiStorm in a systemd-nspawn aarch64 container environment.

## Systemd-nspawn Container Setup

### Create aarch64 container (if not already created):
```bash
# Pull or create aarch64 container (example with Debian/Ubuntu)
sudo debootstrap --arch=arm64 bullseye /var/lib/machines/pistorm-aarch64 http://deb.debian.org/debian/
# or for Ubuntu
sudo debootstrap --arch=arm64 jammy /var/lib/machines/pistorm-aarch64 http://archive.ubuntu.com/ubuntu/
```

### Enter the container:
```bash
sudo systemd-nspawn -M pistorm-aarch64 -D /var/lib/machines/pistorm-aarch64
```

## Required Dependencies

Inside the container, install these dependencies:
```bash
apt update
apt install -y build-essential git libasound2-dev
apt install -y libdrm-dev libegl1-mesa-dev libgles2-mesa-dev libgbm-dev libraspberrypi-dev
```

## GCC Options for aarch64 Build

The following GCC options are required for proper aarch64 compilation:

### CFLAGS:
```
-Wall -Wextra -pedantic -I. -I./raylib -I/opt/vc/include/ -march=armv8-a -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1 -lstdc++
```

### Key Options Explained:
- `-march=armv8-a`: Target ARMv8-A architecture (aarch64)
- `-D_FILE_OFFSET_BITS=64`: Enable 64-bit file offsets for large file support
- `-D_LARGEFILE_SOURCE`: Enable large file support
- `-D_LARGEFILE64_SOURCE`: Enable 64-bit large file support
- `-DINLINE_INTO_M68KCPU_H=1`: Critical flag to fix m68ki_ic_clear inline function visibility
- `-O3`: Optimization level 3
- `-Wall -Wextra -pedantic`: Enable comprehensive warnings

### LFLAGS (Linker flags):
```
-Wall -Wextra -pedantic -L/usr/local/lib -L/opt/vc/lib -L./raylib_drm -lraylib -lGLESv2 -lEGL -lgbm -ldrm -ldl -lstdc++ -lvcos -lvchiq_arm -lvchostif -lasound
```

## Make Configuration

### Default Build (64-bit):
```bash
make
```
This uses the default settings which now target 64-bit ARM.

### Explicit Platform Build:
```bash
make PLATFORM=PI_64BIT
```

## Important Build Notes

### 1. Inline Function Fix
The critical change for aarch64 builds is the addition of `-DINLINE_INTO_M68KCPU_H=1` which ensures the `m68ki_ic_clear` function is properly visible across compilation units.

### 2. Removed Invalid Flags
The following ARM-specific flags are NOT used for aarch64:
- `-mfloat-abi=hard` (not valid for aarch64)
- `-mfpu=neon-fp-armv8` (not valid for aarch64)

### 3. Large File Support
The `-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE` flags are essential for handling large HDF images (>4GB) in PiSCSI.

## Makefile Changes Summary

The following changes were made to the Makefile for aarch64 compatibility:

### Default CFLAGS and LFLAGS:
```makefile
CFLAGS = $(WARNINGS) -I. -I./raylib -I/opt/vc/include/ -march=armv8-a -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1 -lstdc++ $(ACFLAGS)
LFLAGS = $(WARNINGS) -L/usr/local/lib -L/opt/vc/lib -L./raylib_drm -lraylib -lGLESv2 -lEGL -lgbm -ldrm -ldl -lstdc++ -lvcos -lvchiq_arm -lvchostif -lasound
```

### Platform-specific (PI_64BIT):
```makefile
ifeq ($(PLATFORM),PI_64BIT)
    LFLAGS = $(WARNINGS) -L/usr/local/lib -L/opt/vc/lib -L./raylib_drm -lraylib -lGLESv2 -lEGL -lgbm -ldrm -ldl -lstdc++ -lvcos -lvchiq_arm -lvchostif -lasound
    CFLAGS = $(WARNINGS) -I. -I./raylib -I/opt/vc/include/ -march=armv8-a -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DINLINE_INTO_M68KCPU_H=1 -lstdc++ $(ACFLAGS)
endif
```

## Testing the Build

After building, test with:
```bash
timeout 30 ./emulator --config myamiga.cfg
```

## Build Process Summary

1. Clone or copy PiStorm source to container
2. Install dependencies
3. Run `make clean && make` (or `make PLATFORM=PI_64BIT`)
4. Verify emulator binary is created successfully
5. Test functionality as needed

This configuration ensures proper 64-bit ARM compilation with all the fixes needed for PiSCSI large file access and inline function visibility issues.