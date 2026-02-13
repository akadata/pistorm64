// SPDX-License-Identifier: MIT

#include <stdint.h>

#include "log.h"
#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/amiga_zorro.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"

#include "net64_autoconfig.h"
#include "net64_bus.h"

static uint8_t net64_rom[] = {
    Z2_Z2,
    AC_MEM_SIZE_64KB,
    0x3,
    0x2,
    0x0,
    0x0,
    0x0,
    0x0,
    PISTORM_AC_MANUF_ID,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x6,
    0x4,
    0x0,
    0x1,
};

static uint8_t net64_read8(zorro_device_t *dev, uint32_t offset) {
  return (uint8_t)handle_net64_read(dev->base + offset, OP_TYPE_BYTE);
}

static uint16_t net64_read16(zorro_device_t *dev, uint32_t offset) {
  return (uint16_t)handle_net64_read(dev->base + offset, OP_TYPE_WORD);
}

static uint32_t net64_read32(zorro_device_t *dev, uint32_t offset) {
  return handle_net64_read(dev->base + offset, OP_TYPE_LONGWORD);
}

static void net64_write8(zorro_device_t *dev, uint32_t offset, uint8_t value) {
  handle_net64_write(dev->base + offset, value, OP_TYPE_BYTE);
}

static void net64_write16(zorro_device_t *dev, uint32_t offset, uint16_t value) {
  handle_net64_write(dev->base + offset, value, OP_TYPE_WORD);
}

static void net64_write32(zorro_device_t *dev, uint32_t offset, uint32_t value) {
  handle_net64_write(dev->base + offset, value, OP_TYPE_LONGWORD);
}

static zorro_device_t net64_z2_device = {
    .name = "z2-net64",
    .bus = ZORRO_BUS_Z2,
    .size = NET64_REGSIZE,
    .manufacturer = PISTORM_MANUF_ID,
    .product = PISTORM_PROD_NET64_Z2,
    .flags = 0,
    .ac_rom = net64_rom,
    .ac_rom_size = sizeof(net64_rom),
    .read8 = net64_read8,
    .read16 = net64_read16,
    .read32 = net64_read32,
    .write8 = net64_write8,
    .write16 = net64_write16,
    .write32 = net64_write32,
    .priv = NULL,
};

void net64_register(void) {
  LOG_INFO("[NET64] Registering Z2 net64 device.\n");
  int slot = zorro_register_device(&net64_z2_device);
  if (slot < 0) {
    LOG_WARN("[NET64] Failed to register Z2 net64 device.\n");
  }
}
