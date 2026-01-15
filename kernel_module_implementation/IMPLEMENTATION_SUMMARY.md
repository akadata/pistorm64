# PiStorm Kernel Module Implementation

## Overview
This directory contains a complete implementation of a kernel module for the PiStorm project that replaces the current userspace `/dev/mem` access with a proper kernel module interface.

## Architecture

### 1. UAPI Header (`include/uapi/linux/pistorm.h`)
Defines the ioctl interface between userspace and kernel:
- `PISTORM_IOC_SETUP`: Initialize the GPIO protocol
- `PISTORM_IOC_RESET_SM`: Reset the state machine
- `PISTORM_IOC_PULSE_RESET`: Pulse the reset line
- `PISTORM_IOC_BUSOP`: Perform a bus operation (read/write)

### 2. Kernel Module (`pistorm_kernel_module.c`)
- Creates a character device `/dev/pistorm0`
- Handles all GPIO register access in kernel space
- Implements ioctl handlers for all protocol operations
- Provides safe, concurrent access to GPIO hardware

### 3. Userspace Shim (`src/gpio/ps_protocol_kmod.c`)
- Provides the same API as the original `ps_protocol.c`
- Translates function calls to ioctl calls to the kernel module
- Maintains compatibility with existing emulator code

### 4. Test Tool (`pistorm_smoke_test.c`)
- Basic functionality test for the new interface
- Verifies setup, reset, and read/write operations

## Benefits

1. **Security**: No need for `/dev/mem` access or root privileges for the emulator
2. **Stability**: Runs in kernel space, protected from userspace crashes
3. **Performance**: Direct kernel access to hardware registers
4. **Compatibility**: Maintains the same API as the original implementation
5. **Concurrency**: Proper locking and synchronization for multiple accessors

## Build and Installation

### Building the Kernel Module
```bash
cd ~/pistorm-kmod
make
```

### Loading the Module
```bash
cd ~/pistorm-kmod
sudo make load
```

### Testing
```bash
cd ~/pistorm-kmod
sudo ./pistorm_smoke_test
```

## Integration with Emulator

To use the kernel module backend in the emulator:

1. Build with the kernel module flag:
   ```bash
   make PISTORM_KMOD=1
   ```

2. The emulator will automatically use the `/dev/pistorm0` interface instead of `/dev/mem`

3. No changes needed to emulator logic - only the backend implementation changes

## Implementation Details

The kernel module implements the same protocol as the original userspace implementation but with the following advantages:

- GPIO register access happens in kernel space with proper locking
- No need for direct `/dev/mem` access
- Proper device file interface with standard permissions
- Standard ioctl interface for communication

## Verification

The smoke test successfully demonstrated:
- Device file creation (`/dev/pistorm0`)
- Successful opening of the device
- Setup and reset operations
- Read and write operations to Amiga registers
- Proper cleanup and closing

## Next Steps

1. Integrate the kernel module approach into the main emulator build system
2. Add batching support for improved performance
3. Add more sophisticated error handling
4. Implement proper interrupt handling if needed
5. Add support for additional PiStorm features

## Files Created

- `pistorm_kernel_module.c` - The kernel module implementation
- `pistorm_smoke_test.c` - Test utility to verify functionality
- `include/uapi/linux/pistorm.h` - User API header for ioctl interface
- `Makefile` - Build system for the kernel module
- `ps_protocol_kmod.c` - Userspace shim with same API as original