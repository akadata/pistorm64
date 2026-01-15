# PiStorm Kernel Module Implementation

This directory contains a complete implementation of a kernel module for the PiStorm project that replaces the current userspace `/dev/mem` access with a proper kernel module interface.

## Directory Structure

```
kernel_module_implementation/
├── include/
│   └── uapi/
│       └── linux/
│           └── pistorm.h          # UAPI header defining ioctl interface
├── src/
│   └── gpio/
│       └── ps_protocol_kmod.c     # Userspace shim with same API as original
├── tools/
│   └── pistorm_smoke.c            # Basic functionality test
├── pistorm_kernel_module.c        # Kernel module implementation
└── Makefile                       # Build file for kernel module
```

## Overview

This implementation follows the plan to create a kernel module that provides the same API as the original `ps_protocol.c` but operates in kernel space for better security, stability, and performance.

## Components

### 1. UAPI Header (`include/uapi/linux/pistorm.h`)
Defines the ioctl interface between userspace and kernel with commands for:
- Setup: `PISTORM_IOC_SETUP`
- Reset state machine: `PISTORM_IOC_RESET_SM`
- Pulse reset: `PISTORM_IOC_PULSE_RESET`
- Bus operations: `PISTORM_IOC_BUSOP`
- Batch operations: `PISTORM_IOC_BATCH`

### 2. Kernel Module (`pistorm_kernel_module.c`)
- Creates a character device `/dev/pistorm0`
- Handles all GPIO register access in kernel space
- Implements ioctl handlers for all protocol operations
- Manages GPIO pin setup and clock configuration

### 3. Userspace Shim (`src/gpio/ps_protocol_kmod.c`)
- Provides the same API as the original `ps_protocol.c`
- Translates function calls to ioctl calls to the kernel module
- Maintains compatibility with existing emulator code

### 4. Test Tool (`tools/pistorm_smoke.c`)
- Basic functionality test for the new interface
- Verifies setup, reset, and read operations

## Usage

### Building the Kernel Module
```bash
cd kernel_module_implementation
make
```

### Loading the Module
```bash
sudo make load
```

### Testing
```bash
gcc -o pistorm_smoke_test tools/pistorm_smoke.c -I.
sudo ./pistorm_smoke_test
```

### Building Emulator with Kernel Module Backend
Update the emulator Makefile to support conditional compilation:
```makefile
ifeq ($(PISTORM_KMOD),1)
  CFLAGS += -DPISTORM_KMOD
  SRC += src/gpio/ps_protocol_kmod.c
else
  SRC += src/gpio/ps_protocol.c
endif
```

Then build with:
```bash
make PISTORM_KMOD=1
```

## Benefits

1. **Security**: No need for `/dev/mem` access or root privileges for the emulator
2. **Stability**: Protected from userspace crashes
3. **Performance**: Direct kernel access to hardware registers
4. **Compatibility**: Maintains same API as original implementation
5. **Future-proofing**: Can be extended with batching and other optimizations

## Integration Steps

1. Build the kernel module
2. Load the module on the Pi Zero W2
3. Update emulator build system to support conditional compilation
4. Test with smoke test tool
5. Run full emulator with kernel module backend

## Next Steps

1. Test the implementation on actual hardware
2. Optimize with batching for better performance
3. Add advanced features like interrupt handling
4. Integrate with the main emulator build system