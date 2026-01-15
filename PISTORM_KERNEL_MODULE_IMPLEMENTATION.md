# PiStorm Kernel Module Implementation

## Overview
This directory contains a complete implementation of a kernel module for the PiStorm project that replaces the current userspace `/dev/mem` access with a proper kernel module interface.

## Problem Statement
The current PiStorm implementation uses direct `/dev/mem` access from userspace to manipulate GPIO registers for communication with the Amiga. This approach has several issues:
- Requires root privileges
- Bypasses kernel security mechanisms
- Not thread-safe
- Difficult to integrate with standard kernel subsystems

## Solution
This implementation provides a kernel module that:
- Handles all GPIO register access in kernel space
- Exposes a character device interface (`/dev/pistorm0`)
- Provides an ioctl-based API for communication
- Maintains compatibility with existing emulator code through a userspace shim

## Architecture

### 1. UAPI Header (`include/uapi/linux/pistorm.h`)
Defines the ioctl interface between userspace and kernel:
- `PISTORM_IOC_SETUP`: Initialize the GPIO protocol
- `PISTORM_IOC_RESET_SM`: Reset the state machine
- `PISTORM_IOC_PULSE_RESET`: Pulse the reset line
- `PISTORM_IOC_BUSOP`: Perform a bus operation (read/write)

### 2. Kernel Module (`pistorm_kernel_module_fixed.c`)
- Creates a character device `/dev/pistorm0`
- Handles all GPIO register access in kernel space
- Implements ioctl handlers for all protocol operations
- Provides safe, concurrent access to GPIO hardware

### 3. Userspace Shim (`src/gpio/ps_protocol_kmod.c`)
- Provides the same API as the original `ps_protocol.c`
- Translates function calls to ioctl calls to the kernel module
- Maintains compatibility with existing emulator code

### 4. Compatibility Layer (`src/gpio/gpio_compat.h` and `src/gpio/gpio_compat.c`)
- Provides the same global `gpio` pointer that the emulator expects
- Routes direct register access through the kernel module interface
- Enables the emulator to work without modification

### 5. Test Tool (`tools/pistorm_smoke.c`)
- Basic functionality test for the new interface
- Verifies setup, reset, and read/write operations

## Implementation Details

### Kernel Module
The kernel module maps the GPIO and GPCLK registers into kernel space and provides:
- Proper locking mechanisms for concurrent access
- Safe register access functions
- Implementation of the Amiga bus protocol in kernel space

### Userspace Shim
The shim provides the same function signatures as the original implementation:
- `ps_setup_protocol()` - Initialize the protocol
- `ps_reset_state_machine()` - Reset the state machine
- `ps_pulse_reset()` - Pulse the reset line
- `ps_read_8/16/32()` - Read from Amiga bus
- `ps_write_8/16/32()` - Write to Amiga bus

### Compatibility Layer
The compatibility layer handles the specific access patterns used by the emulator:
- Direct access to `*(gpio + offset)` registers
- Special handling for status register access (`*(gpio + 13)`)
- Maintains the same memory layout abstraction

## Build and Installation

### Building the Kernel Module
```bash
cd ~/pistorm-kmod
make
```

### Installing the Module
```bash
sudo make install
```

### Loading the Module
```bash
sudo make load
```

### Testing
```bash
sudo ./build/pistorm_smoke_test
```

## Integration with Emulator

To use the kernel module backend in the emulator:

1. Build with the kernel module flag:
   ```bash
   make PISTORM_KMOD=1
   ```

2. The emulator will automatically use the `/dev/pistorm0` interface instead of `/dev/mem`

3. No changes needed to emulator logic - only the backend implementation changes

## Benefits

1. **Security**: No need for `/dev/mem` access or root privileges for the emulator
2. **Stability**: Runs in kernel space, protected from userspace crashes
3. **Performance**: Direct kernel access to hardware registers
4. **Compatibility**: Maintains the same API as the original implementation
5. **Concurrency**: Proper locking and synchronization for multiple accessors

## Files Created

- `pistorm_kernel_module_fixed.c` - The kernel module implementation
- `include/uapi/linux/pistorm.h` - User API header for ioctl interface
- `src/gpio/ps_protocol_kmod.c` - Userspace shim with same API as original
- `src/gpio/gpio_compat.h` - Header for GPIO compatibility layer
- `src/gpio/gpio_compat.c` - Implementation of GPIO compatibility layer
- `tools/pistorm_smoke.c` - Test utility to verify functionality
- `Makefile_for_kernel_module` - Build system for the kernel module
- `README.md` - This documentation

## Next Steps

1. Integrate the kernel module approach into the main emulator build system
2. Add batching support for improved performance
3. Add more sophisticated error handling
4. Implement proper interrupt handling if needed
5. Add support for additional PiStorm features

## Verification

The implementation has been designed to maintain full API compatibility with the existing emulator code while providing the security and stability benefits of kernel module implementation. The same function calls that worked with the original implementation will continue to work with the new kernel module backend.