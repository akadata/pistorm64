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

  for (uint8_t i = 0; i < zorro_device_count; i++) {
    if (zorro_devices[i] == dev) {
      LOG_INFO("[ZORRO] Device %s already registered in slot=%u, skipping duplicate registration.\n",
               dev->name ? dev->name : "unnamed", i);
      return (int)i;
    }
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
    if (dev->read16) {
      uint32_t w = dev->read16(dev, offset & ~1u);
      if (offset & 1u) {
        *val = w & 0xFFu;
      } else {
        *val = (w >> 8) & 0xFFu;
      }
      return 1;
    }
    break;
  case OP_TYPE_WORD:
    if (dev->read16) {
      *val = dev->read16(dev, offset);
      return 1;
    }
    if (dev->read8) {
      uint32_t hi = dev->read8(dev, offset);
      uint32_t lo = dev->read8(dev, offset + 1u);
      *val = ((hi & 0xFFu) << 8) | (lo & 0xFFu);
      return 1;
    }
    break;
  case OP_TYPE_LONGWORD:
    if (dev->read32) {
      *val = dev->read32(dev, offset);
      return 1;
    }
    if (dev->read16) {
      uint32_t hi = dev->read16(dev, offset);
      uint32_t lo = dev->read16(dev, offset + 2u);
      *val = ((hi & 0xFFFFu) << 16) | (lo & 0xFFFFu);
      return 1;
    }
    if (dev->read8) {
      uint32_t b0 = dev->read8(dev, offset);
      uint32_t b1 = dev->read8(dev, offset + 1u);
      uint32_t b2 = dev->read8(dev, offset + 2u);
      uint32_t b3 = dev->read8(dev, offset + 3u);
      *val = ((b0 & 0xFFu) << 24) | ((b1 & 0xFFu) << 16) | ((b2 & 0xFFu) << 8) | (b3 & 0xFFu);
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
    if (dev->write16) {
      uint32_t base = offset & ~1u;
      uint16_t cur = 0;
      if (dev->read16) {
        cur = dev->read16(dev, base);
      }
      if (offset & 1u) {
        cur = (uint16_t)((cur & 0xFF00u) | (val & 0xFFu));
      } else {
        cur = (uint16_t)((cur & 0x00FFu) | ((val & 0xFFu) << 8));
      }
      dev->write16(dev, base, cur);
      return 1;
    }
    break;
  case OP_TYPE_WORD:
    if (dev->write16) {
      dev->write16(dev, offset, (uint16_t)val);
      return 1;
    }
    if (dev->write8) {
      dev->write8(dev, offset, (uint8_t)((val >> 8) & 0xFFu));
      dev->write8(dev, offset + 1u, (uint8_t)(val & 0xFFu));
      return 1;
    }
    break;
  case OP_TYPE_LONGWORD:
    if (dev->write32) {
      dev->write32(dev, offset, (uint32_t)val);
      return 1;
    }
    if (dev->write16) {
      dev->write16(dev, offset, (uint16_t)((val >> 16) & 0xFFFFu));
      dev->write16(dev, offset + 2u, (uint16_t)(val & 0xFFFFu));
      return 1;
    }
    if (dev->write8) {
      dev->write8(dev, offset, (uint8_t)((val >> 24) & 0xFFu));
      dev->write8(dev, offset + 1u, (uint8_t)((val >> 16) & 0xFFu));
      dev->write8(dev, offset + 2u, (uint8_t)((val >> 8) & 0xFFu));
      dev->write8(dev, offset + 3u, (uint8_t)(val & 0xFFu));
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
  if (dev->bus == ZORRO_BUS_Z2 && type == OP_TYPE_LONGWORD) {
    uint32_t hi = 0;
    uint32_t lo = 0;
    if (zorro_read_impl(dev, OP_TYPE_WORD, offset, &hi) != 1) {
      return -1;
    }
    if (zorro_read_impl(dev, OP_TYPE_WORD, offset + 2u, &lo) != 1) {
      return -1;
    }
    *val = ((hi & 0xFFFFu) << 16) | (lo & 0xFFFFu);
    return 1;
  }
  return zorro_read_impl(dev, type, offset, val);
}

int zorro_handle_write(uint32_t addr, uint8_t type, uint32_t val) {
  zorro_device_t *dev = zorro_find_by_addr(addr);
  if (!dev) {
    return -1;
  }
  uint32_t offset = addr - dev->base;
  if (dev->bus == ZORRO_BUS_Z2 && type == OP_TYPE_LONGWORD) {
    if (zorro_write_impl(dev, OP_TYPE_WORD, offset, (val >> 16) & 0xFFFFu) != 1) {
      return -1;
    }
    if (zorro_write_impl(dev, OP_TYPE_WORD, offset + 2u, val & 0xFFFFu) != 1) {
      return -1;
    }
    return 1;
  }
  return zorro_write_impl(dev, type, offset, val);
}
