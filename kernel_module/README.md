# PiStorm Kernel Module

This directory contains the complete implementation of the PiStorm kernel module that provides a secure, kernel-space interface to the Amiga bus via GPIO, replacing the less secure `/dev/mem` userspace access approach.

## Directory Structure

```
kernel_module/
├── src/                    # Source code for the kernel module and userspace shim
│   ├── pistorm_kernel_module_fixed.c  # Main kernel module implementation
│   └── ps_protocol_kmod.c             # Userspace shim with same API as original
├── include/                # Header files
│   └── uapi/               # User API headers
│       └── linux/
│           └── pistorm.h   # ioctl interface definitions
├── tools/                  # Test and utility tools
│   ├── pistorm_smoke.c     # Basic functionality test
│   └── pistorm_smoke_test.c # Alternative smoke test
├── docs/                   # Documentation
│   ├── IMPLEMENTATION_SUMMARY.md
│   ├── PISTORM_KERNEL_MODULE_IMPLEMENTATION.md
│   ├── KERNEL_MODULE_IMPLEMENTATION_PLAN.md
│   ├── NETWORK_PIPE.md     # Socket communication documentation
│   ├── DISKIO.md           # Disk I/O documentation
│   ├── FLOPPY_REPORT.md    # Floppy debugging report
│   ├── KERNEL_MODULE_PROPOSAL.md
│   └── AGENTS.md           # Agent documentation
├── kernel/                 # Kernel header files for reference
├── kernel_module_implementation/  # Implementation artifacts
├── Makefile_for_kernel_module     # Build system for kernel module
└── Makefile_kmod           # Alternative Makefile
```

## Key Features

1. **Secure Interface**: Replaces `/dev/mem` access with proper kernel module
2. **API Compatibility**: Maintains identical API to original implementation
3. **GPIO Protocol**: Implements Amiga bus protocol in kernel space
4. **Ioctl Interface**: Standard ioctl-based communication between userspace and kernel
5. **Socket Communication**: Integrated socket-based coordination system

## Build Instructions

### Building the Kernel Module
```bash
cd ~/pistorm-kmod  # or wherever the module source is located
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

### Installing the Kernel Module
```bash
sudo make -C /lib/modules/$(uname -r)/build M=$(pwd) modules_install
sudo depmod -a
```

### Loading the Kernel Module
```bash
sudo insmod ./pistorm_kernel_module_fixed.ko
sudo chmod 666 /dev/pistorm0  # Make device accessible
```

## Usage

### With Emulator
1. Load the kernel module: `sudo make load`
2. Build emulator with kernel module support: `make PISTORM_KMOD=1`
3. Run emulator as normal: `sudo ./emulator`

### Testing
Run the smoke test to verify basic functionality:
```bash
sudo ./pistorm_smoke_test
```

## Architecture

### Kernel Space (`src/pistorm_kernel_module_fixed.c`)
- Direct access to GPIO registers in kernel space
- Implements Amiga bus protocol operations
- Provides ioctl interface to userspace
- Handles proper locking for concurrent access

### Userspace Shim (`src/ps_protocol_kmod.c`)
- Provides identical API to original `ps_protocol.c`
- Translates function calls to ioctl calls
- Maintains compatibility with existing emulator code
- Routes operations through kernel module interface

### UAPI Interface (`include/uapi/linux/pistorm.h`)
- Defines ioctl commands for communication
- Specifies data structures for bus operations
- Provides stable ABI between userspace and kernel

## Benefits

1. **Security**: No need for `/dev/mem` access or root privileges for basic operations
2. **Stability**: Runs in kernel space, protected from userspace crashes
3. **Performance**: Direct kernel access to hardware registers
4. **Concurrency**: Proper locking and synchronization
5. **Maintainability**: Clean separation of kernel and userspace concerns

## Integration with Existing Code

The implementation maintains full API compatibility with existing emulator code:
- Same function signatures: `ps_read_8/16/32()`, `ps_write_8/16/32()`, etc.
- Same initialization sequence: `ps_setup_protocol()`, `ps_reset_state_machine()`
- Same register access patterns: `*(gpio + offset)` compatibility layer

To use the kernel module backend, build with: `make PISTORM_KMOD=1`

## Troubleshooting

### Module Loading Issues
- Check kernel version compatibility
- Ensure proper permissions: `sudo chmod 666 /dev/pistorm0`
- Verify GPIO access permissions

### Device Node Issues
- Check if `/dev/pistorm0` exists after loading module
- Verify module loaded successfully: `lsmod | grep pistorm`

### Functionality Issues
- Ensure emulator is built with kernel module support
- Check that module is loaded before running emulator
- Verify proper GPIO pin connections