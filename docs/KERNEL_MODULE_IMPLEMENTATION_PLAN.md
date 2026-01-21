# PiStorm Kernel Module Implementation

## Overview
This directory contains the implementation of a kernel module for the PiStorm project that replaces the current userspace `/dev/mem` access with a proper kernel module interface.

## Architecture

### 1. UAPI Header
- `include/uapi/linux/pistorm.h`: Defines the ioctl interface between userspace and kernel
- Provides a stable API that both kernel and userspace can use

### 2. Kernel Module
- `pistorm_kernel_module.c`: Implements the kernel-side functionality
- Provides a character device `/dev/pistorm0` with ioctl interface
- Handles all GPIO register access in kernel space

### 3. Userspace Shim
- `src/gpio/ps_protocol_kmod.c`: Provides the same API as the original `ps_protocol.c`
- Translates function calls to ioctl calls to the kernel module
- Maintains compatibility with existing emulator code

### 4. Test Tool
- `tools/pistorm_smoke.c`: Basic functionality test for the new interface

## Implementation Plan

### Phase 1: Basic Kernel Module
- [x] Create UAPI header with ioctl definitions
- [x] Implement basic kernel module with character device
- [x] Implement GPIO register access in kernel space
- [x] Create userspace shim with same API as original

### Phase 2: Core Protocol Implementation
- [x] Implement core protocol functions in kernel module
- [x] Implement ioctl handlers for SETUP, RESET_SM, PULSE_RESET, BUSOP
- [x] Create smoke test tool

### Phase 3: Integration
- Update emulator Makefile to support both backends:
  - Default: Original `/dev/mem` backend (for compatibility)
  - With `PISTORM_KMOD=1`: Kernel module backend
- Update emulator build system to conditionally compile:
  - `src/gpio/ps_protocol_kmod.c` when `PISTORM_KMOD=1`
  - `src/gpio/ps_protocol.c` otherwise

## Usage

### Building the Kernel Module
```bash
cd /path/to/pistorm-kmod
make -f /path/to/this/Makefile_for_kernel_module
```

### Loading the Module
```bash
sudo make -f /path/to/this/Makefile_for_kernel_module load
```

### Testing
```bash
make -f /path/to/this/Makefile_for_kernel_module smoke_test
./pistorm_smoke_test
```

### Building Emulator with Kernel Module Backend
```bash
make PISTORM_KMOD=1
```

## Benefits of This Approach

1. **Security**: No need for `/dev/mem` access or root privileges for the emulator
2. **Stability**: Protected from userspace crashes
3. **Performance**: Direct kernel access to hardware registers
4. **Compatibility**: Maintains same API as original implementation
5. **Future-proofing**: Can be extended with batching and other optimizations

## Next Steps

1. Integrate the kernel module with the existing pistorm-kmod project
2. Update the emulator build system to support the conditional compilation
3. Test the smoke test on the actual hardware
4. Gradually optimize with batching and other performance improvements
5. Add proper error handling and device cleanup

## Files Created

- `include/uapi/linux/pistorm.h` - UAPI header
- `pistorm_kernel_module.c` - Kernel module implementation
- `src/gpio/ps_protocol_kmod.c` - Userspace shim
- `tools/pistorm_smoke.c` - Smoke test tool
- `Makefile_for_kernel_module` - Build file for kernel module
- `README.md` - This documentation