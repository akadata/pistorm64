// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include "z3bus.h"

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

static bool z3_id_match(const struct pistorm_z3_id *want,
                        const struct pistorm_z3_id *have)
{
  if (want->vendor != PISTORM_Z3_ANY && want->vendor != have->vendor)
    return false;
  if (want->product != PISTORM_Z3_ANY && want->product != have->product)
    return false;
  if (want->revision != PISTORM_Z3_ANY && want->revision != have->revision)
    return false;
  return true;
}

static const struct pistorm_z3_id *z3_find_id(const struct pistorm_z3_id *table,
                                              const struct pistorm_z3_device *dev)
{
  if (!table)
    return NULL;
  for (; table->vendor || table->product || table->revision; table++) {
    if (z3_id_match(table, &dev->id))
      return table;
  }
  return NULL;
}

static void z3_probe_all(struct pistorm_z3_driver *drv)
{
  int i;

  for (i = 0; i < (int)(sizeof(z3bus_devices) / sizeof(z3bus_devices[0])); i++) {
    struct pistorm_z3_device *dev = &z3bus_devices[i];
    const struct pistorm_z3_id *id = NULL;

    if (drv->id_table) {
      id = z3_find_id(drv->id_table, dev);
      if (!id)
        continue;
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

int pistorm_z3_register_driver(struct pistorm_z3_driver *drv)
{
  if (!drv || !drv->name)
    return -EINVAL;

  mutex_lock(&z3bus_lock);
  list_add_tail(&drv->node, &z3bus_drivers);
  z3_probe_all(drv);
  mutex_unlock(&z3bus_lock);

  return 0;
}
EXPORT_SYMBOL_GPL(pistorm_z3_register_driver);

void pistorm_z3_unregister_driver(struct pistorm_z3_driver *drv)
{
  if (!drv)
    return;

  mutex_lock(&z3bus_lock);
  list_del(&drv->node);
  mutex_unlock(&z3bus_lock);
}
EXPORT_SYMBOL_GPL(pistorm_z3_unregister_driver);

static int __init z3bus_init(void)
{
  pr_info(Z3BUS_NAME ": skeleton bus loaded (%zu devices)\n",
          sizeof(z3bus_devices) / sizeof(z3bus_devices[0]));
  return 0;
}

static void __exit z3bus_exit(void)
{
  pr_info(Z3BUS_NAME ": skeleton bus unloaded\n");
}

module_init(z3bus_init);
module_exit(z3bus_exit);

MODULE_AUTHOR("PiStorm64");
MODULE_DESCRIPTION("PiStorm64 Z3 bus skeleton");
MODULE_LICENSE("GPL");
