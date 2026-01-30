// SPDX-License-Identifier: MIT
#ifndef AMIGA_ZORRO_H
#define AMIGA_ZORRO_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
  ZORRO_BUS_Z2 = 0,
  ZORRO_BUS_Z3 = 1,
} zorro_bus_t;

typedef struct zorro_device zorro_device_t;
struct emulator_config;

typedef uint8_t (*zorro_read8_fn)(zorro_device_t *dev, uint32_t offset);
typedef uint16_t (*zorro_read16_fn)(zorro_device_t *dev, uint32_t offset);
typedef uint32_t (*zorro_read32_fn)(zorro_device_t *dev, uint32_t offset);
typedef void (*zorro_write8_fn)(zorro_device_t *dev, uint32_t offset, uint8_t value);
typedef void (*zorro_write16_fn)(zorro_device_t *dev, uint32_t offset, uint16_t value);
typedef void (*zorro_write32_fn)(zorro_device_t *dev, uint32_t offset, uint32_t value);
typedef void (*zorro_reset_fn)(zorro_device_t *dev);

struct zorro_device {
  const char *name;
  zorro_bus_t bus;
  uint32_t size;
  uint16_t manufacturer;
  uint16_t product;
  uint8_t flags;

  uint32_t base;
  const uint8_t *ac_rom;
  size_t ac_rom_size;

  zorro_reset_fn reset;
  zorro_read8_fn read8;
  zorro_read16_fn read16;
  zorro_read32_fn read32;
  zorro_write8_fn write8;
  zorro_write16_fn write16;
  zorro_write32_fn write32;
  void *priv;
};

void zorro_bus_init(void);
void zorro_setvar(struct emulator_config *cfg, const char *var, const char *val);
int zorro_register_device(zorro_device_t *dev);
zorro_device_t *zorro_find_by_addr(uint32_t addr);
zorro_device_t *zorro_get_device_by_index(uint8_t index);
uint8_t zorro_get_device_count(void);

int zorro_handle_read(uint32_t addr, uint8_t type, uint32_t *val);
int zorro_handle_write(uint32_t addr, uint8_t type, uint32_t val);

#endif
