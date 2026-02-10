// SPDX-License-Identifier: MIT
#include <string.h>
#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/amiga_zorro.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"
#include "z3bus_demo.h"
#include "log.h"

#define Z3BUS_DEMO_SIZE (64 * 1024)

// 8-bit access

static uint8_t z3bus_demo_read8(zorro_device_t *dev, uint32_t offset) {
  (void)dev;
  (void)offset;
  // Stub: no registers implemented yet
  return 0;
}

static void z3bus_demo_write8(zorro_device_t *dev, uint32_t offset, uint8_t value) {
  (void)dev;
  (void)offset;
  (void)value;
}

// 16-bit access

static uint16_t z3bus_demo_read16(zorro_device_t *dev, uint32_t offset) {
  (void)dev;
  (void)offset;
  // Stub: return 0 for now
  return 0;
}

static void z3bus_demo_write16(zorro_device_t *dev, uint32_t offset, uint16_t value) {
  (void)dev;
  (void)offset;
  (void)value;
}

// 32-bit access

static uint32_t z3bus_demo_read32(zorro_device_t *dev, uint32_t offset) {
  (void)dev;
  (void)offset;
  // Stub: return 0 for now
  return 0;
}

static void z3bus_demo_write32(zorro_device_t *dev, uint32_t offset, uint32_t value) {
  (void)dev;
  (void)offset;
  (void)value;
}

static zorro_device_t z3bus_demo_device = {
    .name         = "z3bus-demo",
    .bus          = ZORRO_BUS_Z3,
    .size         = Z3BUS_DEMO_SIZE,
    .manufacturer = PISTORM_MANUF_ID,
    .product      = PISTORM_PROD_Z3BUS_DEMO,
    .flags        = 0,

    .ac_rom       = NULL,
    .ac_rom_size  = 0,

    .read8        = z3bus_demo_read8,
    .write8       = z3bus_demo_write8,
    .read16       = z3bus_demo_read16,
    .write16      = z3bus_demo_write16,
    .read32       = z3bus_demo_read32,
    .write32      = z3bus_demo_write32,

    .priv         = NULL,
};

void z3bus_demo_register(void) {
  LOG_INFO("[Z3BUS] Registering z3bus demo device.\n");
  int slot = zorro_register_device(&z3bus_demo_device);
  if (slot < 0) {
    LOG_INFO("[Z3BUS] Failed to register z3bus demo device.\n");
  }
}
