/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PISTORM_Z3BUS_H
#define _PISTORM_Z3BUS_H

#include <linux/types.h>

#define PISTORM_Z3_ANY 0xFFFF

struct pistorm_z3_id {
  u16 vendor;
  u16 product;
  u16 revision;
  u16 reserved;
};

struct pistorm_z3_resource {
  u32 start;
  u32 size;
  u32 flags;
};

struct pistorm_z3_device {
  struct pistorm_z3_id id;
  const char *name;
  int slot;
  struct pistorm_z3_resource res;
  void *driver_data;
};

struct pistorm_z3_driver {
  const char *name;
  const struct pistorm_z3_id *id_table;
  int (*probe)(struct pistorm_z3_device *dev, const struct pistorm_z3_id *id);
  void (*remove)(struct pistorm_z3_device *dev);
  struct list_head node;
};

int pistorm_z3_register_driver(struct pistorm_z3_driver *drv);
void pistorm_z3_unregister_driver(struct pistorm_z3_driver *drv);

#endif /* _PISTORM_Z3BUS_H */
