// SPDX-License-Identifier: MIT
#ifndef CPU_MAP_IFACE_H
#define CPU_MAP_IFACE_H

#include <stdint.h>
#include "m68k.h"

#ifndef USE_MUSASHI
#define USE_MUSASHI 1
#endif

static inline void cpu_map_add_ram_range(uint32_t addr, uint32_t upper, unsigned char *ptr) {
#if USE_MUSASHI
  m68k_add_ram_range(addr, upper, ptr);
#else
  (void)addr;
  (void)upper;
  (void)ptr;
#endif
}

static inline void cpu_map_add_rom_range(uint32_t addr, uint32_t upper, unsigned char *ptr) {
#if USE_MUSASHI
  m68k_add_rom_range(addr, upper, ptr);
#else
  (void)addr;
  (void)upper;
  (void)ptr;
#endif
}

static inline void cpu_map_remove_range(unsigned char *ptr) {
#if USE_MUSASHI
  m68k_remove_range(ptr);
#else
  (void)ptr;
#endif
}

static inline void cpu_map_clear_ranges(void) {
#if USE_MUSASHI
  m68k_clear_ranges();
#endif
}

#endif
