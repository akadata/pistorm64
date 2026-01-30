/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PISTORM_Z3BUS_UAPI_H
#define _PISTORM_Z3BUS_UAPI_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define PISTORM_Z3BUS_MAX_DEVS 16

struct pistorm_z3bus_dev {
  __u16 vendor;
  __u16 product;
  __u16 revision;
  __u16 reserved;
  __u32 slot;
  __u32 start;
  __u32 size;
  __u32 flags;
};

struct pistorm_z3bus_enum {
  __u32 count; /* in: max entries, out: actual count */
  struct pistorm_z3bus_dev devs[PISTORM_Z3BUS_MAX_DEVS];
};

#define PISTORM_Z3BUS_IOC_ENUM _IOWR('Z', 0x01, struct pistorm_z3bus_enum)

#endif /* _PISTORM_Z3BUS_UAPI_H */
