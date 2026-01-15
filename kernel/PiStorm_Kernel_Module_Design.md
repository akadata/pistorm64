# PiStorm GPIO Protocol Kernel Module Design

## Current Implementation Analysis

### ps_protocol.c Current Architecture
The current implementation in `src/gpio/ps_protocol.c` directly manipulates hardware registers:

```c
// Direct memory mapping
void *gpio_map = mmap(
    NULL,                    // Any address in our space will do
    BCM2708_PERI_SIZE,       // Map length
    PROT_READ | PROT_WRITE,  // Enable reading & writing to mapped memory
    MAP_SHARED,              // Shared with other processes
    fd,                      // File to map (/dev/mem)
    BCM2708_PERI_BASE        // Offset to GPIO peripheral
);

// Direct register access
*(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
*(gpio + 7) = 1 << PIN_WR;
*(gpio + 10) = 1 << PIN_WR;
*(gpio + 10) = 0xffffec;
```

### Issues with Current Approach
1. **Security**: Requires `/dev/mem` access (root privileges)
2. **Stability**: Userspace access to hardware registers
3. **Concurrency**: No protection against race conditions
4. **Timing**: Potential timing issues with userspace scheduling
5. **Integration**: Difficult to integrate with standard GPIO subsystem

## Kernel Module Design

### Core Structure
```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio/driver.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>

#define PISTORM_NGPIO 24  // GPIO pins D0-D15, A0-A7

struct pistorm_gpio {
    struct gpio_chip chip;
    void __iomem *gpio_base;    // Mapped GPIO registers
    void __iomem *gpclk_base;   // Mapped GPCLK registers
    spinlock_t lock;            // Protection for register access
    struct clk *clk;            // GPCLK0 clock reference
};
```

### GPIO Chip Operations
```c
static int pistorm_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
    struct pistorm_gpio *pistorm = gpiochip_get_data(gc);
    unsigned long flags;
    u32 reg_val;

    spin_lock_irqsave(&pistorm->lock, flags);
    
    // Set GPIO pin to input mode
    reg_val = readl(pistorm->gpio_base + GPFSEL_REG(offset));
    reg_val &= ~(7 << GPFSEL_SHIFT(offset));
    writel(reg_val, pistorm->gpio_base + GPFSEL_REG(offset));
    
    spin_unlock_irqrestore(&pistorm->lock, flags);
    return 0;
}

static int pistorm_gpio_direction_output(struct gpio_chip *gc, unsigned int offset, int value)
{
    struct pistorm_gpio *pistorm = gpiochip_get_data(gc);
    unsigned long flags;
    u32 reg_val;

    spin_lock_irqsave(&pistorm->lock, flags);
    
    // Set GPIO pin to output mode
    reg_val = readl(pistorm->gpio_base + GPFSEL_REG(offset));
    reg_val &= ~(7 << GPFSEL_SHIFT(offset));
    reg_val |= (1 << GPFSEL_SHIFT(offset));  // Output mode
    writel(reg_val, pistorm->gpio_base + GPFSEL_REG(offset));
    
    // Set initial value
    if (value)
        writel(1 << offset, pistorm->gpio_base + GPSET_REG);
    else
        writel(1 << offset, pistorm->gpio_base + GPCLR_REG);
    
    spin_unlock_irqrestore(&pistorm->lock, flags);
    return 0;
}

static int pistorm_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
    struct pistorm_gpio *pistorm = gpiochip_get_data(gc);
    u32 reg_val;
    
    reg_val = readl(pistorm->gpio_base + GPLEV_REG);
    return !!(reg_val & (1 << offset));
}

static void pistorm_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
    struct pistorm_gpio *pistorm = gpiochip_get_data(gc);
    unsigned long flags;

    spin_lock_irqsave(&pistorm->lock, flags);
    
    if (value)
        writel(1 << offset, pistorm->gpio_base + GPSET_REG);
    else
        writel(1 << offset, pistorm->gpio_base + GPCLR_REG);
    
    spin_unlock_irqrestore(&pistorm->lock, flags);
}
```

### Amiga Bus Protocol Implementation
```c
// Specific functions for Amiga bus protocol
static int pistorm_write_16(struct pistorm_gpio *pistorm, unsigned int address, unsigned int data)
{
    unsigned long flags;
    u32 reg_val;
    
    spin_lock_irqsave(&pistorm->lock, flags);
    
    // Set data bus
    reg_val = readl(pistorm->gpio_base + GPFSEL0_REG);
    reg_val &= ~GPFSEL0_OUTPUT_MASK;  // Clear data pins
    reg_val |= GPFSEL0_OUTPUT_VAL;    // Set data pins to output
    writel(reg_val, pistorm->gpio_base + GPFSEL0_REG);
    
    // Write data to D0-D15
    reg_val = readl(pistorm->gpio_base + GPSET0_REG);
    reg_val &= ~DATA_PIN_MASK;
    reg_val |= (data & 0xFFFF);
    writel(reg_val, pistorm->gpio_base + GPSET0_REG);
    
    // Write address to A0-A7 (GPIO 2-9)
    reg_val = readl(pistorm->gpio_base + GPSET0_REG);
    reg_val &= ~ADDR_PIN_MASK;
    reg_val |= ((address & 0xFF) << 2);
    writel(reg_val, pistorm->gpio_base + GPSET0_REG);
    
    // Set address high (GPIO 10-11 for A8-A9)
    reg_val = readl(pistorm->gpio_base + GPSET1_REG);
    reg_val &= ~ADDR_HI_MASK;
    reg_val |= ((address >> 8) & 0x3);
    writel(reg_val, pistorm->gpio_base + GPSET1_REG);
    
    // Pulse WR signal
    writel(1 << PIN_WR, pistorm->gpio_base + GPSET0_REG);  // WR high
    udelay(1);  // Setup time
    writel(1 << PIN_WR, pistorm->gpio_base + GPCLR0_REG);  // WR low
    udelay(1);  // Pulse width
    writel(1 << PIN_WR, pistorm->gpio_base + GPSET0_REG);  // WR high
    
    spin_unlock_irqrestore(&pistorm->lock, flags);
    return 0;
}

static unsigned int pistorm_read_16(struct pistorm_gpio *pistorm, unsigned int address)
{
    unsigned long flags;
    u32 reg_val;
    unsigned int result;
    
    spin_lock_irqsave(&pistorm->lock, flags);
    
    // Configure data pins as inputs
    reg_val = readl(pistorm->gpio_base + GPFSEL0_REG);
    reg_val &= ~GPFSEL0_DATA_MASK;  // Clear data pins
    reg_val |= GPFSEL0_INPUT_VAL;   // Set data pins to input
    writel(reg_val, pistorm->gpio_base + GPFSEL0_REG);
    
    // Write address
    // ... (similar to write_16 but for address)
    
    // Pulse RD signal
    writel(1 << PIN_RD, pistorm->gpio_base + GPSET0_REG);  // RD high
    udelay(1);  // Setup time
    writel(1 << PIN_RD, pistorm->gpio_base + GPCLR0_REG);  // RD low
    udelay(2);  // Access time
    writel(1 << PIN_RD, pistorm->gpio_base + GPSET0_REG);  // RD high
    
    // Read data from D0-D15
    reg_val = readl(pistorm->gpio_base + GPLEV0_REG);
    result = reg_val & DATA_PIN_MASK;
    
    spin_unlock_irqrestore(&pistorm->lock, flags);
    return result;
}
```

### Device Tree Integration
```dts
// Device Tree entry for PiStorm
pistorm_gpio: pistorm-gpio@20200000 {
    compatible = "pistorm,gpio";
    reg = <0x20200000 0x1000>,  // GPIO registers
          <0x20101000 0x1000>;  // GPCLK registers
    interrupts = <25>;          // GPIO interrupt number
    #gpio-cells = <2>;
    gpio-controller;
    interrupt-controller;
    #interrupt-cells = <2>;
};
```

### Clock Configuration
```c
static int pistorm_setup_clock(struct pistorm_gpio *pistorm)
{
    int ret;
    u32 reg_val;
    
    // Get GPCLK0 clock
    pistorm->clk = devm_clk_get(&pdev->dev, "gpclk0");
    if (IS_ERR(pistorm->clk)) {
        dev_err(&pdev->dev, "Failed to get GPCLK0\n");
        return PTR_ERR(pistorm->clk);
    }
    
    // Enable GPCLK0
    ret = clk_prepare_enable(pistorm->clk);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable GPCLK0\n");
        return ret;
    }
    
    // Configure GPCLK0 for 200MHz on GPIO4
    reg_val = readl(pistorm->gpclk_base + CM_GP0CTL_REG);
    reg_val &= ~CM_PASSWORD_MASK;
    reg_val |= CM_PASSWORD | CM_GATE;  // Gate clock during setup
    writel(reg_val, pistorm->gpclk_base + CM_GP0CTL_REG);
    udelay(10);
    
    // Set divisor for 200MHz (assuming 500MHz source)
    reg_val = readl(pistorm->gpclk_base + CM_GP0DIV_REG);
    reg_val &= ~CM_PASSWORD_MASK;
    reg_val |= CM_PASSWORD | (2 << CM_DIV_FRAC_SHIFT) | (2 << CM_DIV_INT_SHIFT);
    writel(reg_val, pistorm->gpclk_base + CM_GP0DIV_REG);
    
    // Enable and configure source
    reg_val = readl(pistorm->gpclk_base + CM_GP0CTL_REG);
    reg_val &= ~CM_PASSWORD_MASK;
    reg_val |= CM_PASSWORD | CM_ENAB | CM_SRC_PLLC_CORE1;  // 500MHz source
    writel(reg_val, pistorm->gpclk_base + CM_GP0CTL_REG);
    
    // Set GPIO4 to alternate function 0 (GPCLK0)
    reg_val = readl(pistorm->gpio_base + GPFSEL1_REG);
    reg_val &= ~(7 << 12);  // Clear GPIO4 function
    reg_val |= (4 << 12);   // Set to alt function 0
    writel(reg_val, pistorm->gpio_base + GPFSEL1_REG);
    
    return 0;
}
```

## Userspace API Transition

### Current API
```c
// Current ps_protocol.h functions
unsigned int ps_read_8(unsigned int address);
unsigned int ps_read_16(unsigned int address);
unsigned int ps_read_32(unsigned int address);
void ps_write_8(unsigned int address, unsigned int data);
void ps_write_16(unsigned int address, unsigned int data);
void ps_write_32(unsigned int address, unsigned int data);
```

### New Kernel Module API
```c
// Using standard GPIO sysfs interface
// Or custom character device with IOCTL interface
#define PISTORM_WRITE_16 _IOW('p', 1, struct pistorm_op)
#define PISTORM_READ_16  _IOR('p', 2, struct pistorm_op)

struct pistorm_op {
    uint32_t address;
    uint32_t data;
    uint32_t result;
};
```

## Benefits of Kernel Module Approach

1. **Security**: No `/dev/mem` access required
2. **Stability**: Protected from userspace crashes
3. **Performance**: Direct hardware access without userspace overhead
4. **Integration**: Standard GPIO subsystem integration
5. **Concurrency**: Proper locking and synchronization
6. **Timing**: Better timing guarantees with kernel scheduling
7. **Maintainability**: Cleaner separation of concerns

## Implementation Phases

### Phase 1: Basic GPIO Controller
- Implement basic GPIO chip operations
- Map GPIO registers in kernel space
- Implement simple read/write operations

### Phase 2: Amiga Bus Protocol
- Implement Amiga-specific bus protocol
- Add proper timing delays
- Implement address/data bus handling

### Phase 3: Clock Management
- Implement GPCLK0 configuration
- Add proper clock setup and teardown

### Phase 4: Integration
- Update emulator to use new interface
- Implement character device for userspace access
- Add device tree binding

This kernel module approach would provide a much more robust and secure foundation for the PiStorm project while maintaining the same functionality as the current implementation.