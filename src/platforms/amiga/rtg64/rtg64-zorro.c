// SPDX-License-Identifier: MIT

#include "log.h"
#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/amiga_zorro.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"
#include "platforms/amiga/rtg64/rtg64-zorro.h"
#include "platforms/amiga/rtg64/rtg64.h"

static uint8_t z2_rtg64_rom[] = {
    /* RTG64 is an MMIO I/O board, not autoconfig RAM. */
    Z2_Z2,
    AC_MEM_SIZE_64KB,
    0x4,
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
    0x1,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
};

static uint8_t rtg64_read8(zorro_device_t *dev, uint32_t offset) {
  (void)dev;
  return (uint8_t)rtg64_mmio_read(offset, 1);
}

static uint16_t rtg64_read16(zorro_device_t *dev, uint32_t offset) {
  (void)dev;
  return (uint16_t)rtg64_mmio_read(offset, 2);
}

static uint32_t rtg64_read32(zorro_device_t *dev, uint32_t offset) {
  (void)dev;
  return rtg64_mmio_read(offset, 4);
}

static void rtg64_write8(zorro_device_t *dev, uint32_t offset, uint8_t value) {
  (void)dev;
  rtg64_mmio_write(offset, value, 1);
}

static void rtg64_write16(zorro_device_t *dev, uint32_t offset, uint16_t value) {
  (void)dev;
  rtg64_mmio_write(offset, value, 2);
}

static void rtg64_write32(zorro_device_t *dev, uint32_t offset, uint32_t value) {
  (void)dev;
  rtg64_mmio_write(offset, value, 4);
}

static void rtg64_reset_dev(zorro_device_t *dev) {
  (void)dev;
  rtg64_reset();
}

static zorro_device_t z2_rtg64_device = {
    .name = "z2-rtg64",
    .bus = ZORRO_BUS_Z2,
    .size = RTG64_MMIO_SIZE,
    .manufacturer = PISTORM_MANUF_ID,
    .product = PISTORM_PROD_RTG64_Z2,
    .flags = 0,
    .ac_rom = z2_rtg64_rom,
    .ac_rom_size = sizeof(z2_rtg64_rom),
    .reset = rtg64_reset_dev,
    .read8 = rtg64_read8,
    .read16 = rtg64_read16,
    .read32 = rtg64_read32,
    .write8 = rtg64_write8,
    .write16 = rtg64_write16,
    .write32 = rtg64_write32,
    .priv = NULL,
};

void rtg64_z2_register(void) {
  if (rtg64_init() != 0) {
    LOG_WARN("[RTG64] Failed to initialize RTG64 core before registration.\n");
    return;
  }

  int slot = zorro_register_device(&z2_rtg64_device);
  if (slot < 0) {
    LOG_WARN("[RTG64] Failed to register Z2 RTG64 autoconfig device.\n");
    return;
  }

  LOG_INFO("[RTG64] Registered Z2 RTG64 scaffold device (slot=%d).\n", slot);
}
