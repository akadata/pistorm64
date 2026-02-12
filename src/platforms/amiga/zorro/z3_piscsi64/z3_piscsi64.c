// SPDX-License-Identifier: MIT

#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/amiga_zorro.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"
#include "platforms/amiga/piscsi64/piscsi64-api.h"
#include "platforms/amiga/piscsi64/piscsi64-enums.h"
#include "config_file/config_file.h"
#include "z3_piscsi64.h"
#include "log.h"

static uint8_t z3_piscsi64_rom[] = {
    Z2_Z2 | Z2_BOOTROM,
    AC_MEM_SIZE_64KB,
    0x01,
    0x02,
    0x00,
    0x00,
    0x00,
    0x00,
    PISTORM_AC_MANUF_ID,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x04,
    0x02,
    0x00, // serial
    0x04,
    0x00,
    0x00,
    0x00, // Optional BOOT ROM vector (nibble format, matching legacy Z2 layout)
};

static uint8_t z3_piscsi64_read8(zorro_device_t *dev, uint32_t offset) {
  return (uint8_t)handle_piscsi64_read(dev->base + offset, OP_TYPE_BYTE);
}

static uint16_t z3_piscsi64_read16(zorro_device_t *dev, uint32_t offset) {
  return (uint16_t)handle_piscsi64_read(dev->base + offset, OP_TYPE_WORD);
}

static uint32_t z3_piscsi64_read32(zorro_device_t *dev, uint32_t offset) {
  return handle_piscsi64_read(dev->base + offset, OP_TYPE_LONGWORD);
}

static void z3_piscsi64_write8(zorro_device_t *dev, uint32_t offset, uint8_t value) {
  handle_piscsi64_write(dev->base + offset, value, OP_TYPE_BYTE);
}

static void z3_piscsi64_write16(zorro_device_t *dev, uint32_t offset, uint16_t value) {
  handle_piscsi64_write(dev->base + offset, value, OP_TYPE_WORD);
}

static void z3_piscsi64_write32(zorro_device_t *dev, uint32_t offset, uint32_t value) {
  handle_piscsi64_write(dev->base + offset, value, OP_TYPE_LONGWORD);
}

static zorro_device_t z3_piscsi64_device = {
    .name = "z2-piscsi64",
    .bus = ZORRO_BUS_Z2,
    .size = PISCSI64_REGSIZE,
    .manufacturer = PISTORM_MANUF_ID,
    .product = PISTORM_PROD_PISCSI64_Z2,
    .flags = ZORRO_DEV_FLAG_BOOTROM,
    .ac_rom = z3_piscsi64_rom,
    .ac_rom_size = sizeof(z3_piscsi64_rom),
    .read8 = z3_piscsi64_read8,
    .read16 = z3_piscsi64_read16,
    .read32 = z3_piscsi64_read32,
    .write8 = z3_piscsi64_write8,
    .write16 = z3_piscsi64_write16,
    .write32 = z3_piscsi64_write32,
    .priv = NULL,
};

void z3_piscsi64_register(void) {
  LOG_INFO("[ZORRO] Registering Z2 PiSCSI64 device.\n");
  int slot = zorro_register_device(&z3_piscsi64_device);
  if (slot < 0) {
    LOG_WARN("[ZORRO] Failed to register Z2 PiSCSI64 device.\n");
  }
}
