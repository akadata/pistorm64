// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include "z3bus.h"
#include "pistorm_z3bus.h"

#define Z3BUS_NAME "z3bus"

/*
 * Skeleton Z3 bus: minimal driver/device registry.
 * No ps_protocol wiring yet; this is a placeholder to validate the API shape.
 */

static LIST_HEAD(z3bus_drivers);
static DEFINE_MUTEX(z3bus_lock);

static struct pistorm_z3_device z3bus_devices[] = {
  {
    .id = { .vendor = 0x50C1, .product = 0x0001, .revision = 0, .reserved = 0 },
    .name = "piscsi0",
    .slot = 0,
    .res = { .start = 0x60000000, .size = 0x01000000, .flags = 0 },
    .driver_data = NULL,
  },
  {
    .id = { .vendor = 0x50C1, .product = 0x0002, .revision = 0, .reserved = 0 },
    .name = "rtg0",
    .slot = 1,
    .res = { .start = 0x70000000, .size = 0x02800000, .flags = 0 },
    .driver_data = NULL,
  },
};
static const int z3bus_device_count = sizeof(z3bus_devices) / sizeof(z3bus_devices[0]);

static bool z3_id_match(const struct pistorm_z3_id *want,
                        const struct pistorm_z3_id *have) {
  if (want->vendor != PISTORM_Z3_ANY && want->vendor != have->vendor) {
    return false;
  }
  if (want->product != PISTORM_Z3_ANY && want->product != have->product) {
    return false;
  }
  if (want->revision != PISTORM_Z3_ANY && want->revision != have->revision) {
    return false;
  }
  return true;
}

static const struct pistorm_z3_id *z3_find_id(const struct pistorm_z3_id *table,
                                              const struct pistorm_z3_device *dev) {
  if (!table) {
    return NULL;
  }
  for (; table->vendor || table->product || table->revision; table++) {
    if (z3_id_match(table, &dev->id)) {
      return table;
    }
  }
  return NULL;
}

static void z3_probe_all(struct pistorm_z3_driver *drv) {
  int i;

  for (i = 0; i < z3bus_device_count; i++) {
    struct pistorm_z3_device *dev = &z3bus_devices[i];
    const struct pistorm_z3_id *id = NULL;

    if (drv->id_table) {
      id = z3_find_id(drv->id_table, dev);
      if (!id) {
        continue;
      }
    }

    if (drv->probe) {
      int rc = drv->probe(dev, id);
      if (rc == 0) {
        pr_info(Z3BUS_NAME ": bound %s to %s\n",
                dev->name ? dev->name : "(unnamed)",
                drv->name ? drv->name : "(unnamed)");
      }
    }
  }
}

int pistorm_z3_register_driver(struct pistorm_z3_driver *drv) {
  if (!drv || !drv->name) {
    return -EINVAL;
  }

  mutex_lock(&z3bus_lock);
  list_add_tail(&drv->node, &z3bus_drivers);
  z3_probe_all(drv);
  mutex_unlock(&z3bus_lock);

  return 0;
}
EXPORT_SYMBOL_GPL(pistorm_z3_register_driver);

void pistorm_z3_unregister_driver(struct pistorm_z3_driver *drv) {
  if (!drv) {
    return;
  }

  mutex_lock(&z3bus_lock);
  list_del(&drv->node);
  mutex_unlock(&z3bus_lock);
}
EXPORT_SYMBOL_GPL(pistorm_z3_unregister_driver);

int pistorm_z3_get_device_count(void) {
  return z3bus_device_count;
}
EXPORT_SYMBOL_GPL(pistorm_z3_get_device_count);

const struct pistorm_z3_device *pistorm_z3_get_device(int index) {
  if (index < 0 || index >= z3bus_device_count) {
    return NULL;
  }
  return &z3bus_devices[index];
}
EXPORT_SYMBOL_GPL(pistorm_z3_get_device);

static long z3bus_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
  struct pistorm_z3bus_enum req;
  int i;

  if (cmd != PISTORM_Z3BUS_IOC_ENUM) {
    return -ENOTTY;
  }

  if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
    return -EFAULT;
  }

  if (req.count > PISTORM_Z3BUS_MAX_DEVS) {
    req.count = PISTORM_Z3BUS_MAX_DEVS;
  }

  for (i = 0; i < (int)req.count && i < z3bus_device_count; i++) {
    const struct pistorm_z3_device *dev = &z3bus_devices[i];
    req.devs[i].vendor = dev->id.vendor;
    req.devs[i].product = dev->id.product;
    req.devs[i].revision = dev->id.revision;
    req.devs[i].reserved = dev->id.reserved;
    req.devs[i].slot = (u32)dev->slot;
    req.devs[i].start = dev->res.start;
    req.devs[i].size = dev->res.size;
    req.devs[i].flags = dev->res.flags;
  }

  req.count = (z3bus_device_count < (int)req.count) ? z3bus_device_count : req.count;

  if (copy_to_user((void __user *)arg, &req, sizeof(req))) {
    return -EFAULT;
  }

  return 0;
}

static const struct file_operations z3bus_fops = {
  .owner = THIS_MODULE,
  .unlocked_ioctl = z3bus_ioctl,
  .compat_ioctl = z3bus_ioctl,
};

static struct miscdevice z3bus_miscdev = {
  .minor = MISC_DYNAMIC_MINOR,
  .name = "z3bus",
  .fops = &z3bus_fops,
};

static int __init z3bus_init(void) {
  int rc;
  pr_info(Z3BUS_NAME ": skeleton bus loaded (%zu devices)\n",
          sizeof(z3bus_devices) / sizeof(z3bus_devices[0]));
  rc = misc_register(&z3bus_miscdev);
  if (rc) {
    pr_err(Z3BUS_NAME ": failed to register /dev/z3bus (%d)\n", rc);
    return rc;
  }
  return 0;
}

static void __exit z3bus_exit(void) {
  misc_deregister(&z3bus_miscdev);
  pr_info(Z3BUS_NAME ": skeleton bus unloaded\n");
}

module_init(z3bus_init);
module_exit(z3bus_exit);

MODULE_AUTHOR("PiStorm64");
MODULE_DESCRIPTION("PiStorm64 Z3 bus skeleton");
MODULE_LICENSE("GPL");
