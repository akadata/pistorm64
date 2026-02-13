// SPDX-License-Identifier: MIT
#include <string.h>
#include <assert.h>

#include "amiga_zorro.h"
#include "config_file/config_file.h"
#include "amiga-autoconf.h"
#include "zorro/z2_pissa/z2_pissa.h"
#include "zorro/z2_rng/z2_rng.h"
#include "zorro/serial_echo/serial_echo.h"
#include "zorro/z3bus_demo/z3bus_demo.h"
#include "rtg64/rtg64-zorro.h"
#include "log.h"

#define MAX_ZORRO_DEVICES AC_PIC_LIMIT

static zorro_device_t *zorro_devices[MAX_ZORRO_DEVICES];
static_assert(AC_PIC_LIMIT >= MAX_ZORRO_DEVICES, "PIC list smaller than Zorro device table");

static uint8_t zorro_device_count;

static uint32_t z2_next_base = 0x00200000;
static uint32_t z3_next_base = 0x40000000;
static uint8_t zorro_initialized;

void zorro_bus_init(void) {
  zorro_device_count = 0;
  z2_next_base = 0x00200000;
  z3_next_base = 0x40000000;
  zorro_initialized = 1;
}

uint8_t zorro_get_device_count(void) { 
  return zorro_device_count; 
}

zorro_device_t *zorro_get_device_by_index(uint8_t index) {
  if (index >= zorro_device_count) {
    return NULL;
  }
  return zorro_devices[index];
}

int zorro_register_device(zorro_device_t *dev) {
  if (!dev || zorro_device_count >= MAX_ZORRO_DEVICES) {
    LOG_WARN("[ZORRO] Device registration failed (full or null). count=%d max=%d\n",
             zorro_device_count, MAX_ZORRO_DEVICES);
    return -1;
  }

  const uint8_t slot = zorro_device_count;

  dev->slot = slot;
  dev->base = 0;  // autoconfig will assign a real base later

  zorro_devices[slot] = dev;
  zorro_device_count++;

  autoconf_register_zorro_device(slot);

  LOG_INFO("[ZORRO] Registered %s (%s) size=0x%.8X base=0x%.8X slot=%u\n",
           dev->name ? dev->name : "unnamed",
           dev->bus == ZORRO_BUS_Z2 ? "Z2" : "Z3",
           dev->size, dev->base, dev->slot);

  return slot;
}





void zorro_setvar(struct emulator_config *cfg, const char *var, const char *val) {
  (void)cfg;
  (void)val;
  if (!zorro_initialized) {
    zorro_bus_init();
  }

  if (strcmp(var, "z3bus-demo") == 0) {
    z3bus_demo_register();
  } else if (strcmp(var, "zorro-serial") == 0) {
    z2_serial_echo_register();
  } else if (strcmp(var, "zorro-rng") == 0) {
    z2_rng_register();
  } else if (strcmp(var, "zorro-pissa") == 0) {
    z2_pissa_register();
  } else if (strcmp(var, "rtg64") == 0) {
    rtg64_z2_register();
  }
}

zorro_device_t *zorro_find_by_addr(uint32_t addr) {
  for (uint8_t i = 0; i < zorro_device_count; i++) {
    zorro_device_t *dev = zorro_devices[i];
    if (!dev) {
      continue;
    }
    if (dev->base != 0 && addr >= dev->base && addr < dev->base + dev->size) {
      return dev;
    }
  }
  return NULL;
}

static int zorro_read_impl(zorro_device_t *dev, uint8_t type, uint32_t offset, uint32_t *val) {
  switch (type) {
  case OP_TYPE_BYTE:
    if (dev->read8) {
      *val = dev->read8(dev, offset);
      return 1;
    }
    break;
  case OP_TYPE_WORD:
    if (dev->read16) {
      *val = dev->read16(dev, offset);
      return 1;
    }
    break;
  case OP_TYPE_LONGWORD:
    if (dev->read32) {
      *val = dev->read32(dev, offset);
      return 1;
    }
    break;
  default:
    break;
  }
  return -1;
}

static int zorro_write_impl(zorro_device_t *dev, uint8_t type, uint32_t offset, uint32_t val) {
  switch (type) {
  case OP_TYPE_BYTE:
    if (dev->write8) {
      dev->write8(dev, offset, (uint8_t)val);
      return 1;
    }
    break;
  case OP_TYPE_WORD:
    if (dev->write16) {
      dev->write16(dev, offset, (uint16_t)val);
      return 1;
    }
    break;
  case OP_TYPE_LONGWORD:
    if (dev->write32) {
      dev->write32(dev, offset, (uint32_t)val);
      return 1;
    }
    break;
  default:
    break;
  }
  return -1;
}

int zorro_handle_read(uint32_t addr, uint8_t type, uint32_t *val) {
  zorro_device_t *dev = zorro_find_by_addr(addr);
  if (!dev) {
    return -1;
  }
  uint32_t offset = addr - dev->base;
  return zorro_read_impl(dev, type, offset, val);
}

int zorro_handle_write(uint32_t addr, uint8_t type, uint32_t val) {
  zorro_device_t *dev = zorro_find_by_addr(addr);
  if (!dev) {
    return -1;
  }
  uint32_t offset = addr - dev->base;
  return zorro_write_impl(dev, type, offset, val);
}
