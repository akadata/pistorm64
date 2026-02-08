// SPDX-License-Identifier: MIT
#include <string.h>

#include "z3bus_demo.h"
#include "log.h"

#define Z3BUS_DEMO_SIZE (64 * 1024)

static uint8_t z3bus_demo_read8(zorro_device_t *dev, uint32_t offset) {
  (void)dev;
  (void)offset;
  return 0;
}

static void z3bus_demo_write8(zorro_device_t *dev, uint32_t offset, uint8_t value) {
  (void)dev;
  (void)offset;
  (void)value;
}

static zorro_device_t z3bus_demo_device = {
    .name = "z3bus-demo",
    .bus = ZORRO_BUS_Z3,
    .size = Z3BUS_DEMO_SIZE,
    .manufacturer = 0x07DB,
    .product = 0x060C,
    .flags = 0,
    .ac_rom = NULL,
    .ac_rom_size = 0,
    .read8 = z3bus_demo_read8,
    .write8 = z3bus_demo_write8,
    .priv = NULL,
};

void z3bus_demo_register(void) {
  LOG_INFO("[Z3BUS] Registering z3bus demo device.\n");
  int slot = zorro_register_device(&z3bus_demo_device);
  if (slot < 0) {
    LOG_INFO("[Z3BUS] Failed to register z3bus demo device.\n");
  }
}
