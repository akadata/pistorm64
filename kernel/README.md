# PiStorm Kernel GPIO Integration Analysis

## Overview
This directory contains Linux kernel GPIO header files for version 6.12.62, which matches the kernel running on the Pi Zero W2 (Linux zero 6.12.62+rpt-rpi-v8).

## Kernel Version Information
- **Target System**: Pi Zero W2 (amiga)
- **Kernel Version**: 6.12.62+rpt-rpi-v8
- **Architecture**: aarch64
- **Raspberry Pi OS**: Debian-based with Real-time patches (rpt)

## Relevant GPIO Header Files

### 1. `gpio-consumer.h`
- Defines the GPIO consumer interface for drivers that use GPIOs
- Contains functions for requesting GPIOs from device tree or ACPI
- Defines structures like `gpio_desc`, `gpio_descs` for GPIO descriptor management
- Key functions: `gpiod_get()`, `gpiod_put()`, `gpiod_direction_input/output()`, `gpiod_set_value()`

### 2. `gpio-driver.h`
- Defines the GPIO driver interface for GPIO controller drivers
- Contains functions for registering GPIO controllers
- Defines the `gpio_chip` structure that GPIO controller drivers implement
- Key functions: `gpiochip_add_data()`, `gpiochip_remove()`

### 3. `gpio.h`
- Main GPIO header file with legacy interfaces
- Contains deprecated functions (should be avoided in new code)
- Provides compatibility layer for older code

### 4. `aspeed.h`
- GPIO driver for ASPEED BMC chips
- Not directly relevant to PiStorm but shows how GPIO drivers are structured

### 5. `gpio-nomadik.h`
- GPIO driver for Nomadik platforms
- Example of platform-specific GPIO driver implementation

### 6. `gpio-reg.h`
- GPIO driver for memory-mapped register-based GPIO controllers
- Most relevant to PiStorm as it shows how register-based GPIO controllers work

## PiStorm GPIO Protocol Integration Possibilities

### Current Implementation
The current PiStorm GPIO protocol in `src/gpio/ps_protocol.c` directly accesses hardware registers via `/dev/mem`:
- Uses `mmap()` to map GPIO registers into userspace
- Direct register manipulation using virtual memory addresses
- Requires root privileges to access `/dev/mem`

### Kernel Module Approach
A kernel module could integrate with the standard GPIO subsystem:

```c
// Example structure for a PiStorm GPIO controller
static struct gpio_chip pistorm_gpio_chip = {
    .label = "pistorm-gpio",
    .owner = THIS_MODULE,
    .base = -1,  // Dynamic allocation
    .ngpio = 24, // Number of GPIOs (D0-D15, A0-A7, control lines)
    .can_sleep = false,
    .direction_input = pistorm_gpio_direction_input,
    .direction_output = pistorm_gpio_direction_output,
    .get = pistorm_gpio_get,
    .set = pistorm_gpio_set,
    .get_multiple = pistorm_gpio_get_multiple,
    .set_multiple = pistorm_gpio_set_multiple,
};
```

### Benefits of Kernel Integration
1. **Standard Interface**: Use standard GPIO sysfs interface (`/sys/class/gpio/`)
2. **Device Tree Integration**: Integrate with device tree for proper hardware description
3. **Security**: No need for `/dev/mem` access or root privileges
4. **Concurrency**: Proper locking and synchronization
5. **Power Management**: Integration with kernel power management
6. **Userspace API**: Standardized userspace API through libgpiod

### Implementation Strategy
1. **Register-based GPIO Controller**: Model after `gpio-reg.h` approach
2. **Memory Mapping**: Map Pi Zero GPIO registers in kernel space using `ioremap()`
3. **Clock Management**: Properly configure GPCLK0 for 200MHz clock
4. **Interrupt Support**: Handle transaction completion interrupts
5. **Amiga Bus Protocol**: Implement the specific timing and protocol requirements

### Key Structures to Study
- `struct gpio_chip` - Core GPIO controller interface
- `struct gpio_desc` - Individual GPIO descriptor
- `struct bgpio_chip` - Generic bitmapped GPIO controller (from `gpio-generic.h`)

## Next Steps for Kernel Module Development

1. **Study `gpio-reg.h`** - Understand register-based GPIO controller implementation
2. **Review `bgpio-*.c`** - Bitmapped GPIO implementations in kernel
3. **Design PiStorm-specific chip** - Map Amiga bus signals to GPIO lines
4. **Implement core functions** - Direction control, read/write, multiple ops
5. **Integrate with emulator** - Update emulator to use kernel GPIO interface
6. **Test and validate** - Ensure timing and functionality match current implementation

## References
- Kernel GPIO subsystem documentation: `Documentation/driver-api/gpio/`
- GPIO consumer API: `drivers/gpio/gpiolib.c`
- Sample GPIO drivers: `drivers/gpio/`