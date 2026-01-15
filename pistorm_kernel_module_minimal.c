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
#include <linux/ioctl.h>

// Include our UAPI header
#include "include/uapi/linux/pistorm.h"

#define BCM2708_PERI_BASE 0x3F000000  // For Pi Zero W2
#define GPIO_BASE 0x200000
#define GPCLK_BASE 0x101000

// GPIO register offsets
#define GPFSEL0 0x00
#define GPFSEL1 0x04
#define GPFSEL2 0x08
#define GPFSEL3 0x0c
#define GPFSEL4 0x10
#define GPFSEL5 0x14
#define GPSET0  0x1c
#define GPSET1  0x20
#define GPCLR0  0x28
#define GPCLR1  0x2c
#define GPLEV0  0x34
#define GPLEV1  0x38

// Our PiStorm pin mappings
#define PIN_TXN_IN_PROGRESS 0
#define PIN_IPL_ZERO 1
#define PIN_A0 2
#define PIN_A1 3
#define PIN_CLK 4
#define PIN_RESET 5
#define PIN_RD 6
#define PIN_WR 7
#define PIN_D(x) (8 + x)

struct pistorm_device {
    struct cdev cdev;
    void __iomem *gpio_base;
    struct mutex lock;
    struct device *device;
    struct class *class;
    dev_t dev_num;
};

static struct pistorm_device *pistorm_dev;

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
        // Basic setup - just acknowledge
        mutex_unlock(&pistorm_dev->lock);
        break;

    case PISTORM_IOC_RESET_SM:
        mutex_lock(&pistorm_dev->lock);
        // Simulate reset state machine
        mutex_unlock(&pistorm_dev->lock);
        break;

    case PISTORM_IOC_PULSE_RESET:
        mutex_lock(&pistorm_dev->lock);
        // Simulate pulse reset
        mutex_unlock(&pistorm_dev->lock);
        break;

    case PISTORM_IOC_BUSOP:
        if (copy_from_user(&busop, (void __user *)arg, sizeof(busop))) {
            return -EFAULT;
        }

        mutex_lock(&pistorm_dev->lock);

        if (busop.is_read) {
            // Simulate a read operation - in a real implementation, this would
            // perform the actual GPIO operations to read from the Amiga bus
            result = 0x00FF; // Default value
            busop.value = result;
            
            if (copy_to_user((void __user *)arg, &busop, sizeof(busop))) {
                mutex_unlock(&pistorm_dev->lock);
                return -EFAULT;
            }
        } else {
            // Simulate a write operation - in a real implementation, this would
            // perform the actual GPIO operations to write to the Amiga bus
            // (in this case, busop.value contains the value to write)
        }

        mutex_unlock(&pistorm_dev->lock);
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
    pistorm_dev = kmalloc(sizeof(struct pistorm_device), GFP_KERNEL);
    if (!pistorm_dev) {
        return -ENOMEM;
    }
    memset(pistorm_dev, 0, sizeof(struct pistorm_device));

    // Allocate device number
    ret = alloc_chrdev_region(&pistorm_dev->dev_num, 0, 1, "pistorm");
    if (ret < 0) {
        kfree(pistorm_dev);
        return ret;
    }

    // Initialize mutex
    mutex_init(&pistorm_dev->lock);

    // Initialize character device
    cdev_init(&pistorm_dev->cdev, &pistorm_fops);
    pistorm_dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&pistorm_dev->cdev, pistorm_dev->dev_num, 1);
    if (ret) {
        unregister_chrdev_region(pistorm_dev->dev_num, 1);
        kfree(pistorm_dev);
        return ret;
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