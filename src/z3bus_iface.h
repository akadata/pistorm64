// SPDX-License-Identifier: MIT
#ifndef PISTORM_Z3BUS_IFACE_H
#define PISTORM_Z3BUS_IFACE_H

#include <stdint.h>

struct pistorm_z3bus_dev;

int z3bus_open(void);
void z3bus_close(void);
int z3bus_is_available(void);
int z3bus_enum(struct pistorm_z3bus_dev *out_devs, uint32_t max_devs, uint32_t *out_count);

#endif /* PISTORM_Z3BUS_IFACE_H */
