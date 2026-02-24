// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <string.h>

#include "z2_rng.h"
#include "log.h"
#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/amiga_zorro.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"
#include "host/crypto/pi_crypto.h"

#define Z2_RNG_SIZE 0x100u
#define REG_RNG_DATA32 0x00u
#define REG_RNG_STATUS 0x04u
#define REG_RNG_CTRL 0x08u

#define RNG_STATUS_DATA_READY 0x01u
#define RNG_STATUS_ENTROPY_OK 0x02u

typedef struct {
  uint8_t entropy_ok;
} z2_rng_state_t;

static z2_rng_state_t rng_state;

static uint32_t z2_rng_status(void) {
  uint32_t status = RNG_STATUS_DATA_READY;
  if (rng_state.entropy_ok) {
    status |= RNG_STATUS_ENTROPY_OK;
  }
  return status;
}

static uint32_t z2_rng_read32(zorro_device_t *dev, uint32_t offset) {
  (void)dev;
  if (offset == REG_RNG_DATA32) {
    uint32_t word = 0;
    if (pi_random_bytes((uint8_t *)&word, sizeof(word))) {
      rng_state.entropy_ok = 1;
    } else {
      rng_state.entropy_ok = 0;
    }
    return word;
  }
  if (offset == REG_RNG_STATUS) {
    return z2_rng_status();
  }
  return 0;
}

static uint16_t z2_rng_read16(zorro_device_t *dev, uint32_t offset) {
  uint32_t base = offset & ~0x3u;
  uint32_t val = z2_rng_read32(dev, base);
  if ((offset & 0x2u) != 0) {
    return (uint16_t)(val & 0xFFFFu);
  }
  return (uint16_t)((val >> 16) & 0xFFFFu);
}

static uint8_t z2_rng_read8(zorro_device_t *dev, uint32_t offset) {
  uint32_t base = offset & ~0x3u;
  uint32_t val = z2_rng_read32(dev, base);
  uint32_t shift = (3u - (offset & 0x3u)) * 8u;
  return (uint8_t)((val >> shift) & 0xFFu);
}

static void z2_rng_write32(zorro_device_t *dev, uint32_t offset, uint32_t value) {
  (void)dev;
  if (offset == REG_RNG_CTRL) {
    if (value & 0x01u) {
      rng_state.entropy_ok = 0;
    }
  }
}

static void z2_rng_write16(zorro_device_t *dev, uint32_t offset, uint16_t value) {
  uint32_t base = offset & ~0x3u;
  uint32_t val32 = (uint32_t)value;
  if ((offset & 0x2u) == 0) {
    val32 <<= 16;
  }
  z2_rng_write32(dev, base, val32);
}

static void z2_rng_write8(zorro_device_t *dev, uint32_t offset, uint8_t value) {
  uint32_t base = offset & ~0x3u;
  uint32_t val32 = (uint32_t)value;
  uint32_t shift = (3u - (offset & 0x3u)) * 8u;
  val32 <<= shift;
  z2_rng_write32(dev, base, val32);
}

static void z2_rng_reset(zorro_device_t *dev) {
  (void)dev;
  rng_state.entropy_ok = 0;
}

static uint8_t z2_rng_rom[] = {
    Z2_Z2,
    AC_MEM_SIZE_64KB,
    0x2,
    0x1,
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
    0x0,
    0x0,
    0x0,
    0x0,
};

static zorro_device_t z2_rng_device = {
    .name = "z2-rng",
    .bus = ZORRO_BUS_Z2,
    .size = Z2_RNG_SIZE,
    .manufacturer = PISTORM_MANUF_ID,
    .product = PISTORM_PROD_Z2_RNG,
    .flags = 0,
    .ac_rom = z2_rng_rom,
    .ac_rom_size = sizeof(z2_rng_rom),
    .reset = z2_rng_reset,
    .read8 = z2_rng_read8,
    .read16 = z2_rng_read16,
    .read32 = z2_rng_read32,
    .write8 = z2_rng_write8,
    .write16 = z2_rng_write16,
    .write32 = z2_rng_write32,
    .priv = &rng_state,
};

void z2_rng_register(void) {
  LOG_INFO("[ZORRO] Registering Z2 RNG device.\n");
  int slot = zorro_register_device(&z2_rng_device);
  if (slot < 0) {
    LOG_INFO("[ZORRO] Failed to register Z2 RNG device.\n");
  }
}
