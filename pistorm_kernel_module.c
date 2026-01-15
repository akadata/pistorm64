#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/ioctl.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "uapi/linux/pistorm.h"

#define BCM2708_PERI_BASE 0x3F000000  // Pi3/Pi Zero W2
#define BCM2708_PERI_SIZE 0x01000000
#define GPIO_ADDR 0x200000 /* GPIO controller */
#define GPCLK_ADDR 0x101000

#define CLK_PASSWD 0x5a000000
#define CLK_GP0_CTL 0x070
#define CLK_GP0_DIV 0x074

// GPIO register offsets
#define GPFSEL0 0x00
#define GPFSEL1 0x04
#define GPFSEL2 0x08
#define GPSET0  0x1c
#define GPCLR0  0x28
#define GPLEV0  0x34
#define GPEDS0  0x40
#define GPREN0  0x4c

// Our PiStorm pin mappings (based on ps_protocol.h)
#define PIN_TXN_IN_PROGRESS 0
#define PIN_IPL_ZERO 1
#define PIN_A0 2
#define PIN_A1 3
#define PIN_CLK 4
#define PIN_RESET 5
#define PIN_RD 6
#define PIN_WR 7
#define PIN_D(x) (8 + x)

#define REG_DATA 0
#define REG_ADDR_LO 1
#define REG_ADDR_HI 2
#define REG_STATUS 3

// GPIO setup macros
#define GPFSEL_REG(pin) ((pin)/10)
#define GPFSEL_SHIFT(pin) (((pin)%10)*3)

// Pin function select values
#define GPIO_INPUT  0
#define GPIO_OUTPUT 1
#define GPIO_ALT0   4
#define GPIO_ALT1   5
#define GPIO_ALT2   6
#define GPIO_ALT3   7
#define GPIO_ALT4   3
#define GPIO_ALT5   2

struct pistorm_device {
    struct cdev cdev;
    void __iomem *gpio_base;
    void __iomem *gpclk_base;
    struct mutex lock;
    struct device *device;
    struct class *class;
    dev_t dev_num;
};

static struct pistorm_device *pistorm_dev;

// Helper functions for GPIO register access
static inline void pistorm_write_reg(struct pistorm_device *pdev, unsigned int reg, u32 val)
{
    writel(val, pdev->gpio_base + reg);
}

static inline u32 pistorm_read_reg(struct pistorm_device *pdev, unsigned int reg)
{
    return readl(pdev->gpio_base + reg);
}

// Low-level GPIO protocol functions (similar to original ps_protocol.c)
static void setup_gpclk(struct pistorm_device *pdev)
{
    // Enable 200MHz CLK output on GPIO4
    writel(CLK_PASSWD | (1 << 5), pdev->gpclk_base + (CLK_GP0_CTL / 4));
    udelay(10);
    while ((readl(pdev->gpclk_base + (CLK_GP0_CTL / 4))) & (1 << 7))
        ;
    udelay(100);
    writel(CLK_PASSWD | (6 << 12), pdev->gpclk_base + (CLK_GP0_DIV / 4));  // divider for 200MHz
    udelay(10);
    writel(CLK_PASSWD | 5 | (1 << 4), pdev->gpclk_base + (CLK_GP0_CTL / 4));  // pll? 5=pllc
    udelay(10);
    while (((readl(pdev->gpclk_base + (CLK_GP0_CTL / 4))) & (1 << 7)) == 0)
        ;
    udelay(100);

    // Set GPIO4 to ALT0 (GPCLK0)
    u32 reg_val = pistorm_read_reg(pdev, GPFSEL1);
    reg_val &= ~(7 << 12);  // Clear GPIO4 function
    reg_val |= (GPIO_ALT0 << 12);  // Set to alt function 0 (GPCLK0)
    pistorm_write_reg(pdev, GPFSEL1, reg_val);
}

static void setup_gpio_pins(struct pistorm_device *pdev)
{
    // Set all GPIO pins to input initially
    pistorm_write_reg(pdev, GPFSEL0, 0x0024c240);  // Inputs for data/address/control
    pistorm_write_reg(pdev, GPFSEL1, 0x00000000);
    pistorm_write_reg(pdev, GPFSEL2, 0x00000000);
}

// Core protocol functions
static void pistorm_write_8(struct pistorm_device *pdev, unsigned int address, unsigned int data)
{
    unsigned long flags;
    u32 reg_val;

    mutex_lock(&pistorm_dev->lock);

    // Set GPIO pins to output mode for data and address
    reg_val = pistorm_read_reg(pdev, GPFSEL0);
    // Set data pins (8-23) to output
    for (int i = 8; i <= 23; i++) {
        int reg_idx = GPFSEL_REG(i);
        int shift = GPFSEL_SHIFT(i);
        reg_val &= ~(7 << shift);
        reg_val |= (GPIO_OUTPUT << shift);
    }
    // Set address pins (2-7) to output
    for (int i = 2; i <= 7; i++) {
        int reg_idx = GPFSEL_REG(i);
        int shift = GPFSEL_SHIFT(i);
        reg_val &= ~(7 << shift);
        reg_val |= (GPIO_OUTPUT << shift);
    }
    pistorm_write_reg(pdev, GPFSEL0, reg_val);

    // Write data to D0-D15 (pins 8-23)
    reg_val = pistorm_read_reg(pdev, GPSET0);
    reg_val &= 0x000000FF; // Clear data pins (8-23)
    reg_val |= (data & 0xFFFF) << 8;
    pistorm_write_reg(pdev, GPSET0, reg_val);

    // Write address to A0-A7 (pins 2-9) and A8-A9 (pins 10-11)
    reg_val = pistorm_read_reg(pdev, GPSET0);
    reg_val &= 0xFF000003; // Clear address pins (2-11)
    reg_val |= ((address & 0xFF) << 2); // A0-A7 on pins 2-9
    reg_val |= (((address >> 8) & 0x3) << 10); // A8-A9 on pins 10-11
    pistorm_write_reg(pdev, GPSET0, reg_val);

    // Pulse WR signal
    reg_val = pistorm_read_reg(pdev, GPSET0);
    reg_val |= (1 << PIN_WR);  // WR high initially
    pistorm_write_reg(pdev, GPSET0, reg_val);
    udelay(1);  // Setup time
    
    reg_val = pistorm_read_reg(pdev, GPCLR0);
    reg_val |= (1 << PIN_WR);  // WR low (active)
    pistorm_write_reg(pdev, GPCLR0, reg_val);
    udelay(1);  // Pulse width
    
    reg_val = pistorm_read_reg(pdev, GPSET0);
    reg_val |= (1 << PIN_WR);  // WR high again
    pistorm_write_reg(pdev, GPSET0, reg_val);

    mutex_unlock(&pistorm_dev->lock);
}

static unsigned int pistorm_read_8(struct pistorm_device *pdev, unsigned int address)
{
    unsigned long flags;
    u32 reg_val;
    unsigned int result;

    mutex_lock(&pistorm_dev->lock);

    // Set address pins to output mode
    reg_val = pistorm_read_reg(pdev, GPFSEL0);
    for (int i = 2; i <= 11; i++) {  // A0-A9 pins
        int reg_idx = GPFSEL_REG(i);
        int shift = GPFSEL_SHIFT(i);
        reg_val &= ~(7 << shift);
        reg_val |= (GPIO_OUTPUT << shift);
    }
    pistorm_write_reg(pdev, GPFSEL0, reg_val);

    // Write address to A0-A9
    reg_val = pistorm_read_reg(pdev, GPSET0);
    reg_val &= 0xFF000003; // Clear address pins (2-11)
    reg_val |= ((address & 0xFF) << 2); // A0-A7 on pins 2-9
    reg_val |= (((address >> 8) & 0x3) << 10); // A8-A9 on pins 10-11
    pistorm_write_reg(pdev, GPSET0, reg_val);

    // Set data pins to input mode
    reg_val = pistorm_read_reg(pdev, GPFSEL0);
    for (int i = 8; i <= 23; i++) {  // D0-D15 pins
        int reg_idx = GPFSEL_REG(i);
        int shift = GPFSEL_SHIFT(i);
        reg_val &= ~(7 << shift);
        reg_val |= (GPIO_INPUT << shift);
    }
    pistorm_write_reg(pdev, GPFSEL0, reg_val);

    // Pulse RD signal
    reg_val = pistorm_read_reg(pdev, GPSET0);
    reg_val |= (1 << PIN_RD);  // RD high initially
    pistorm_write_reg(pdev, GPSET0, reg_val);
    udelay(1);  // Setup time
    
    reg_val = pistorm_read_reg(pdev, GPCLR0);
    reg_val |= (1 << PIN_RD);  // RD low (active)
    pistorm_write_reg(pdev, GPCLR0, reg_val);
    udelay(2);  // Access time
    
    reg_val = pistorm_read_reg(pdev, GPSET0);
    reg_val |= (1 << PIN_RD);  // RD high again
    pistorm_write_reg(pdev, GPSET0, reg_val);

    // Read data from D0-D15
    reg_val = pistorm_read_reg(pdev, GPLEV0);
    result = (reg_val >> 8) & 0xFFFF;

    mutex_unlock(&pistorm_dev->lock);
    return result;
}

// IOCTL handler
static long pistorm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int rc = 0;
    struct pistorm_busop busop;
    unsigned int result;

    if (_IOC_TYPE(cmd) != PISTORM_IOC_MAGIC) {
        return -ENOTTY;
    }

    switch (cmd) {
    case PISTORM_IOC_SETUP:
        mutex_lock(&pistorm_dev->lock);
        setup_gpio_pins(pistorm_dev);
        setup_gpclk(pistorm_dev);
        mutex_unlock(&pistorm_dev->lock);
        break;

    case PISTORM_IOC_RESET_SM:
        mutex_lock(&pistorm_dev->lock);
        // Write INIT signal
        pistorm_write_reg(pistorm_dev, GPSET0, (1 << PIN_RESET));  // Assuming RESET pin
        udelay(1500);
        pistorm_write_reg(pistorm_dev, GPCLR0, (1 << PIN_RESET));
        udelay(100);
        mutex_unlock(&pistorm_dev->lock);
        break;

    case PISTORM_IOC_PULSE_RESET:
        mutex_lock(&pistorm_dev->lock);
        // Pulse reset line
        pistorm_write_reg(pistorm_dev, GPCLR0, (1 << PIN_RESET));
        udelay(100000);  // 100ms
        pistorm_write_reg(pistorm_dev, GPSET0, (1 << PIN_RESET));
        mutex_unlock(&pistorm_dev->lock);
        break;

    case PISTORM_IOC_BUSOP:
        if (copy_from_user(&busop, (void __user *)arg, sizeof(busop))) {
            return -EFAULT;
        }

        if (busop.is_read) {
            // Perform read operation
            switch (busop.width) {
            case PISTORM_W8:
                result = pistorm_read_8(pistorm_dev, busop.addr);
                break;
            case PISTORM_W16:
                // For simplicity, just read lower 16 bits for now
                result = pistorm_read_8(pistorm_dev, busop.addr) & 0xFFFF;
                break;
            case PISTORM_W32:
                // For simplicity, just read lower 32 bits for now
                result = pistorm_read_8(pistorm_dev, busop.addr);
                break;
            default:
                return -EINVAL;
            }
            busop.value = result;
            
            if (copy_to_user((void __user *)arg, &busop, sizeof(busop))) {
                return -EFAULT;
            }
        } else {
            // Perform write operation
            switch (busop.width) {
            case PISTORM_W8:
                pistorm_write_8(pistorm_dev, busop.addr, busop.value & 0xFF);
                break;
            case PISTORM_W16:
                pistorm_write_8(pistorm_dev, busop.addr, busop.value & 0xFFFF);
                break;
            case PISTORM_W32:
                pistorm_write_8(pistorm_dev, busop.addr, busop.value);
                break;
            default:
                return -EINVAL;
            }
        }
        break;

    default:
        return -ENOTTY;
    }

    return rc;
}

// File operations
static int pistorm_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int pistorm_release(struct inode *inode, struct file *file)
{
    return 0;
}

static struct file_operations pistorm_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = pistorm_ioctl,
    .open = pistorm_open,
    .release = pistorm_release,
};

// Module init and exit
static int __init pistorm_module_init(void)
{
    int ret;

    // Allocate device structure
    pistorm_dev = kzalloc(sizeof(struct pistorm_device), GFP_KERNEL);
    if (!pistorm_dev) {
        return -ENOMEM;
    }

    // Allocate device number
    ret = alloc_chrdev_region(&pistorm_dev->dev_num, 0, 1, "pistorm");
    if (ret < 0) {
        kfree(pistorm_dev);
        return ret;
    }

    // Initialize mutex
    mutex_init(&pistorm_dev->lock);

    // Map GPIO registers
    pistorm_dev->gpio_base = ioremap(BCM2708_PERI_BASE + GPIO_ADDR, 0x1000);
    if (!pistorm_dev->gpio_base) {
        ret = -ENOMEM;
        goto err_unreg_chrdev;
    }

    // Map GPCLK registers
    pistorm_dev->gpclk_base = ioremap(BCM2708_PERI_BASE + GPCLK_ADDR, 0x1000);
    if (!pistorm_dev->gpclk_base) {
        ret = -ENOMEM;
        goto err_unmap_gpio;
    }

    // Initialize character device
    cdev_init(&pistorm_dev->cdev, &pistorm_fops);
    pistorm_dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&pistorm_dev->cdev, pistorm_dev->dev_num, 1);
    if (ret) {
        goto err_unmap_gpclk;
    }

    // Create device class
    pistorm_dev->class = class_create(THIS_MODULE, "pistorm");
    if (IS_ERR(pistorm_dev->class)) {
        ret = PTR_ERR(pistorm_dev->class);
        goto err_del_cdev;
    }

    // Create device node
    pistorm_dev->device = device_create(pistorm_dev->class, NULL, pistorm_dev->dev_num, 
                                        NULL, "pistorm0");
    if (IS_ERR(pistorm_dev->device)) {
        ret = PTR_ERR(pistorm_dev->device);
        goto err_class_destroy;
    }

    printk(KERN_INFO "pistorm: module loaded, device /dev/pistorm0 created\n");
    return 0;

err_class_destroy:
    class_destroy(pistorm_dev->class);
err_del_cdev:
    cdev_del(&pistorm_dev->cdev);
err_unmap_gpclk:
    iounmap(pistorm_dev->gpclk_base);
err_unmap_gpio:
    iounmap(pistorm_dev->gpio_base);
err_unreg_chrdev:
    unregister_chrdev_region(pistorm_dev->dev_num, 1);
    kfree(pistorm_dev);
    return ret;
}

static void __exit pistorm_module_exit(void)
{
    if (pistorm_dev) {
        device_destroy(pistorm_dev->class, pistorm_dev->dev_num);
        class_destroy(pistorm_dev->class);
        cdev_del(&pistorm_dev->cdev);
        iounmap(pistorm_dev->gpclk_base);
        iounmap(pistorm_dev->gpio_base);
        unregister_chrdev_region(pistorm_dev->dev_num, 1);
        kfree(pistorm_dev);
    }
    printk(KERN_INFO "pistorm: module unloaded\n");
}

module_init(pistorm_module_init);
module_exit(pistorm_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PiStorm Team");
MODULE_DESCRIPTION("PiStorm GPIO protocol kernel module");
MODULE_VERSION("1.0");