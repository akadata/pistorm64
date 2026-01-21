# PiStorm GPIO Protocol Kernel Module Proposal

## Overview
This document outlines the approach to convert the current userspace GPIO protocol implementation (`src/gpio/ps_protocol.c`) into a Linux kernel module that can provide a Hardware Abstraction Layer (HAL) and Application Binary Interface (ABI) for the PiStorm system.

## Current Architecture Analysis

### Userspace Implementation
The current implementation in `ps_protocol.c` uses:
- Direct `/dev/mem` access for GPIO register manipulation
- Memory mapping via `mmap()` to access hardware registers
- Direct hardware register access using virtual memory addresses
- Requires root privileges (sudo) to run

### Key Functions
- `setup_io()` - Maps hardware registers into virtual memory
- `setup_gpclk()` - Configures 200MHz clock output on GPIO4
- `ps_setup_protocol()` - Initializes GPIO pins
- `ps_reset_state_machine()` - Resets the state machine
- `ps_pulse_reset()` - Pulses the reset line
- Various read/write functions for Amiga bus communication

## Kernel Module Approach

### Benefits of Kernel Module
1. **Performance**: Direct hardware access without userspace overhead
2. **Reliability**: Protected from userspace crashes
3. **Access Control**: Better security model than /dev/mem
4. **API Stability**: Stable kernel/userspace ABI
5. **Resource Management**: Proper device lifecycle management

### Proposed Architecture

#### 1. Character Device Driver
```c
// Main device structure
struct pistorm_device {
    struct cdev cdev;           // Character device
    void __iomem *gpio_base;   // GPIO register mapping
    void __iomem *gpclk_base;  // GPCLK register mapping
    struct mutex lock;         // Synchronization
    struct device *device;     // Device model
    struct class *class;       // Device class
};
```

#### 2. IOCTL Interface
```c
#define PS_PROTOCOL_SETUP           _IO('p', 0)
#define PS_RESET_STATE_MACHINE      _IO('p', 1)
#define PS_PULSE_RESET             _IO('p', 2)
#define PS_WRITE_8                 _IOR('p', 3, struct ps_op)
#define PS_WRITE_16                _IOR('p', 4, struct ps_op)
#define PS_WRITE_32                _IOR('p', 5, struct ps_op)
#define PS_READ_8                  _IOW('p', 6, struct ps_op)
#define PS_READ_16                 _IOW('p', 7, struct ps_op)
#define PS_READ_32                 _IOW('p', 8, struct ps_op)
```

#### 3. Operation Structure
```c
struct ps_op {
    uint32_t address;
    uint32_t data;
    uint32_t result;
};
```

### Implementation Strategy

#### 1. Basic Kernel Module Framework
```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>

// Module initialization and cleanup functions
static int __init pistorm_module_init(void);
static void __exit pistorm_module_exit(void);

// File operations
static long pistorm_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int pistorm_open(struct inode *inode, struct file *file);
static int pistorm_release(struct inode *inode, struct file *file);
```

#### 2. Hardware Access Functions
```c
// Safe register access functions using ioread32/iowrite32
static inline void pistorm_write_reg(struct pistorm_device *pdev, 
                                     unsigned int reg, u32 val);
static inline u32 pistorm_read_reg(struct pistorm_device *pdev, 
                                   unsigned int reg);

// GPIO pin configuration
static int pistorm_gpio_setup(struct pistorm_device *pdev);
static void pistorm_gpio_cleanup(struct pistorm_device *pdev);
```

#### 3. Clock Configuration
```c
// GPCLK setup using proper kernel clock APIs
static int pistorm_setup_clock(struct pistorm_device *pdev);
static void pistorm_cleanup_clock(struct pistorm_device *pdev);
```

### Userspace API

#### 1. Header File (pistorm_kernel.h)
```c
#ifndef _PISTORM_KERNEL_H
#define _PISTORM_KERNEL_H

#include <linux/ioctl.h>

#define PISTORM_DEVICE "/dev/pistorm"

struct pistorm_op {
    uint32_t addr;
    uint32_t data;
    uint32_t result;
};

#define PS_SETUP_PROTOCOL          _IO('p', 0)
#define PS_RESET_STATE_MACHINE     _IO('p', 1)
#define PS_PULSE_RESET            _IO('p', 2)
#define PS_WRITE_8                _IOR('p', 3, struct pistorm_op)
#define PS_WRITE_16               _IOR('p', 4, struct pistorm_op)
#define PS_WRITE_32               _IOR('p', 5, struct pistorm_op)
#define PS_READ_8                 _IOW('p', 6, struct pistorm_op)
#define PS_READ_16                _IOW('p', 7, struct pistorm_op)
#define PS_READ_32                _IOW('p', 8, struct pistorm_op)

// Convenience functions
int pistorm_open(void);
int pistorm_close(int fd);
int pistorm_setup_protocol(int fd);
int pistorm_reset_state_machine(int fd);
int pistorm_pulse_reset(int fd);
int pistorm_write_8(int fd, uint32_t addr, uint8_t data);
int pistorm_write_16(int fd, uint32_t addr, uint16_t data);
int pistorm_write_32(int fd, uint32_t addr, uint32_t data);
int pistorm_read_8(int fd, uint32_t addr, uint8_t *data);
int pistorm_read_16(int fd, uint32_t addr, uint16_t *data);
int pistorm_read_32(int fd, uint32_t addr, uint32_t *data);

#endif
```

#### 2. Userspace Library
```c
// Implementation of convenience functions
// Wraps ioctl calls for easier usage
// Maintains compatibility with existing code
```

### Integration with Existing Code

#### 1. Conditional Compilation
```c
#ifdef CONFIG_PISTORM_KERNEL_MODULE
  // Use kernel module interface
  #include "pistorm_kernel.h"
#else
  // Use current userspace implementation
  #include "ps_protocol.h"
#endif
```

#### 2. Emulator Integration
The emulator would use the same API regardless of implementation:
- If kernel module is available, use ioctl-based communication
- If not, fall back to current /dev/mem implementation
- Same function signatures and behavior

### Advantages of This Approach

1. **Performance**: Eliminates userspace overhead for frequent operations
2. **Security**: No need for /dev/mem access or root privileges
3. **Stability**: Protected from userspace crashes
4. **Maintainability**: Centralized hardware access logic
5. **Scalability**: Can support multiple concurrent users
6. **Debugging**: Better kernel-level debugging capabilities

### Implementation Phases

#### Phase 1: Basic Module
- Implement character device driver
- Basic register read/write via ioctl
- GPIO pin setup and configuration

#### Phase 2: Clock Support
- GPCLK configuration using kernel clock APIs
- Proper clock management and cleanup

#### Phase 3: Advanced Features
- DMA support if needed
- Interrupt handling
- Power management

#### Phase 4: Integration
- Modify emulator to use kernel module when available
- Maintain backward compatibility
- Performance testing and optimization

### Potential Challenges

1. **Real-time Requirements**: Amiga bus timing may require careful attention
2. **Clock Configuration**: GPCLK setup in kernel space
3. **GPIO Pin Management**: Ensuring exclusive access
4. **Error Handling**: Robust error handling in kernel space
5. **Compatibility**: Maintaining API compatibility

### Conclusion

Converting the ps_protocol to a kernel module would provide significant benefits in terms of performance, security, and reliability. The proposed architecture maintains API compatibility while providing a proper HAL/ABI for PiStorm applications.