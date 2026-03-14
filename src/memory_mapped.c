// SPDX-License-Identifier: MIT

#include "config_file/config_file.h"
#include "memory_mapped.h"
#include "m68k.h"
#include "platforms/amiga/Gayle.h"
#include "platforms/amiga/amiga-autoconf.h"
#include "log.h"
#include <endian.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CHKRANGE(a, b, c) a >= (unsigned int)b && a < (unsigned int)(b + c)
#define CHKRANGE_ABS(a, b, c) a >= (unsigned int)b && a < (unsigned int)c

extern int ovl;

extern const char* map_type_names[MAPTYPE_NUM];
const char* op_type_names[OP_TYPE_NUM] = {
    "BYTE",
    "WORD",
    "LONGWORD",
    "MEM",
};

static int allow_rom_writes = -1;
static unsigned char rom_write_warned[MAX_NUM_MAPPED_ITEMS];
static __thread int read_cache_base_idx = -1;
static __thread int read_cache_ovl_idx = -1;
static __thread int write_cache_base_idx = -1;
static __thread int write_cache_ovl_idx = -1;

static inline int map_is_kickstart(const struct emulator_config* cfg, int index) {
  const char* id = cfg->map_id[index];
  return id && strcasecmp(id, "kickstart") == 0;
}

static inline int rom_write_passthrough_enabled(void) {
  if (allow_rom_writes == -1) {
    const char* e = getenv("PISTORM_JIT_ALLOW_ROM_WRITES");
    allow_rom_writes = (e && atoi(e) != 0) ? 1 : 0;
  }
  return allow_rom_writes;
}

static inline int do_read_value(unsigned char type, const unsigned char* p, unsigned int* val) {
  switch (type) {
  case OP_TYPE_BYTE:
    *val = p[0];
    return 1;

  case OP_TYPE_WORD: {
    uint16_t tmp;
    __builtin_memcpy(&tmp, p, sizeof(tmp));
    *val = be16toh(tmp);
    return 1;
  }

  case OP_TYPE_LONGWORD: {
    uint32_t tmp;
    __builtin_memcpy(&tmp, p, sizeof(tmp));
    *val = be32toh(tmp);
    return 1;
  }

  case OP_TYPE_MEM:
  default:
    return -1;
  }
}

static inline int do_write_value(unsigned char type, unsigned char* p, unsigned int value, int res) {
  switch (type) {
  case OP_TYPE_BYTE:
    p[0] = (unsigned char)value;
    return res;

  case OP_TYPE_WORD: {
    uint16_t tmp = htobe16((uint16_t)value);
    __builtin_memcpy(p, &tmp, sizeof(tmp));
    return res;
  }

  case OP_TYPE_LONGWORD: {
    uint32_t tmp = htobe32((uint32_t)value);
    __builtin_memcpy(p, &tmp, sizeof(tmp));
    return res;
  }

  case OP_TYPE_MEM:
  default:
    return -1;
  }
}

static inline uint32_t op_type_width(unsigned char type) {
  switch (type) {
  case OP_TYPE_BYTE:
    return 1;
  case OP_TYPE_WORD:
    return 2;
  case OP_TYPE_LONGWORD:
    return 4;
  case OP_TYPE_MEM:
  default:
    return 0;
  }
}

static inline int range_contains_size(uint32_t addr, uint32_t base, uint32_t size, uint32_t width) {
  if (width == 0 || size < width || addr < base) {
    return 0;
  }
  return (addr - base) <= (size - width);
}

static inline int range_contains_hi(uint32_t addr, uint32_t lo, uint32_t hi, uint32_t width) {
  if (hi <= lo) {
    return 0;
  }
  return range_contains_size(addr, lo, hi - lo, width);
}

const char* memmap_kind_name(mem_map_kind_t kind) {
  switch (kind) {
  case MEM_MAP_KIND_ROM:
    return "rom";
  case MEM_MAP_KIND_RAM:
    return "ram";
  case MEM_MAP_KIND_Z2:
    return "z2";
  case MEM_MAP_KIND_Z3:
    return "z3";
  case MEM_MAP_KIND_IO:
    return "io";
  case MEM_MAP_KIND_RTG:
    return "rtg";
  case MEM_MAP_KIND_PISCSI:
    return "piscsi";
  case MEM_MAP_KIND_PINET:
    return "pinet";
  case MEM_MAP_KIND_AUTOCONFIG:
    return "autoconfig";
  case MEM_MAP_KIND_NONE:
  default:
    return "none";
  }
}

static inline int map_id_contains(const char* id, const char* needle) {
  return id && needle && strstr(id, needle) != NULL;
}

static mem_map_kind_t classify_kind(const struct emulator_config* cfg, int index,
                                    uint32_t addr, uint8_t* is_autoconfig,
                                    uint8_t* cacheable, uint8_t* executable) {
  const amiga_zorro_layout_t* layout = amiga_get_zorro_layout();
  const char* id = cfg->map_id[index];
  uint8_t map_type = cfg->map_type[index];

  *is_autoconfig = 0;
  *cacheable = 0;
  *executable = 0;

  if (layout &&
      ((addr >= layout->z2_config_base &&
        addr < (layout->z2_config_base + layout->config_window_size)) ||
       (addr >= layout->z3_config_base &&
        addr < (layout->z3_config_base + layout->config_window_size)))) {
    *is_autoconfig = 1;
    return MEM_MAP_KIND_AUTOCONFIG;
  }

  switch (map_type) {
  case MAPTYPE_REGISTER:
    return MEM_MAP_KIND_IO;
  case MAPTYPE_ROM:
    *cacheable = 1;
    *executable = 1;
    break;
  case MAPTYPE_RAM:
  case MAPTYPE_RAM_WTC:
  case MAPTYPE_RAM_NOALLOC:
    *cacheable = 1;
    *executable = 1;
    break;
  default:
    break;
  }

  if (map_id_contains(id, "rtg")) {
    *cacheable = 0;
    *executable = 0;
    return MEM_MAP_KIND_RTG;
  }
  if (map_id_contains(id, "piscsi")) {
    *cacheable = 0;
    *executable = 0;
    return MEM_MAP_KIND_PISCSI;
  }
  if (map_id_contains(id, "pinet") || map_id_contains(id, "pi-net")) {
    *cacheable = 0;
    *executable = 0;
    return MEM_MAP_KIND_PINET;
  }

  if (layout) {
    if (addr >= layout->z2_mem_base && addr < (layout->z2_mem_base + layout->z2_mem_size)) {
      return MEM_MAP_KIND_Z2;
    }
    if (addr >= layout->z3_mem_base && addr < (layout->z3_mem_base + layout->z3_mem_size)) {
      return MEM_MAP_KIND_Z3;
    }
  }

  if (map_type == MAPTYPE_ROM) {
    return MEM_MAP_KIND_ROM;
  }
  if (map_type == MAPTYPE_RAM || map_type == MAPTYPE_RAM_WTC || map_type == MAPTYPE_RAM_NOALLOC) {
    return MEM_MAP_KIND_RAM;
  }
  if (map_type == MAPTYPE_REGISTER) {
    return MEM_MAP_KIND_IO;
  }
  return MEM_MAP_KIND_NONE;
}

int memmap_lookup(const struct emulator_config* cfg, uint32_t addr, mem_map_entry_info_t* out) {
  if (!cfg) {
    return -1;
  }
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE || !cfg->map_size[i]) {
      continue;
    }
    uint32_t lo = (uint32_t)cfg->map_offset[i];
    uint32_t hi = (uint32_t)cfg->map_high[i];
    if (!(addr >= lo && addr < hi)) {
      continue;
    }
    if (out) {
      uint32_t host_span = cfg->map_size[i];
      if (cfg->map_type[i] == MAPTYPE_ROM || cfg->map_type[i] == MAPTYPE_RAM_WTC) {
        if (cfg->rom_size[i] > host_span) {
          host_span = cfg->rom_size[i];
        }
      }
      out->index = i;
      out->map_type = cfg->map_type[i];
      out->amiga_base = lo;
      out->size = cfg->map_size[i];
      out->amiga_end_exclusive = hi;
      out->host_ptr = cfg->map_data[i];
      out->host_span = host_span;
      out->map_id = cfg->map_id[i] ? cfg->map_id[i] : "None";
      out->kind = classify_kind(cfg, i, addr, &out->is_autoconfig, &out->cacheable, &out->executable);
    }
    return i;
  }
  return -1;
}

static inline int map_slot_active(const struct emulator_config* cfg, int index) {
  return cfg && index >= 0 && index < MAX_NUM_MAPPED_ITEMS &&
         cfg->map_type[index] != MAPTYPE_NONE;
}

static inline int mapped_read_at_index(struct emulator_config* cfg, int i, unsigned int addr,
                                       unsigned int* val, unsigned char type) {
  uint32_t width = op_type_width(type);
  unsigned char* read_addr = NULL;
  if (width == 0) {
    return -1;
  }
  switch (cfg->map_type[i]) {
  case MAPTYPE_ROM:
    if (!cfg->map_data[i] || cfg->rom_size[i] == 0) {
      return -1;
    }
    if (!range_contains_size((uint32_t)addr, (uint32_t)cfg->map_offset[i], (uint32_t)cfg->map_size[i], width)) {
      return -1;
    }
    {
      uint32_t rel = ((uint32_t)addr - (uint32_t)cfg->map_offset[i]) % (uint32_t)cfg->rom_size[i];
      if (rel > ((uint32_t)cfg->rom_size[i] - width)) {
        return -1;
      }
      read_addr = cfg->map_data[i] + rel;
    }
    return do_read_value(type, read_addr, val);

  case MAPTYPE_RAM:
  case MAPTYPE_RAM_WTC:
  case MAPTYPE_RAM_NOALLOC:
    if (!cfg->map_data[i]) {
      return -1;
    }
    if (!range_contains_size((uint32_t)addr, (uint32_t)cfg->map_offset[i], (uint32_t)cfg->map_size[i], width)) {
      return -1;
    }
    read_addr = cfg->map_data[i] + (addr - cfg->map_offset[i]);
    return do_read_value(type, read_addr, val);

  case MAPTYPE_REGISTER:
    if (cfg->platform && cfg->platform->register_read) {
      unsigned int local_target;
      if (cfg->platform->register_read(addr, type, &local_target) != -1) {
        *val = local_target;
        return 1;
      }
    }
    return -1;

  default:
    return -1;
  }
}

int handle_mapped_read(struct emulator_config* cfg, unsigned int addr, unsigned int* val,
                       unsigned char type) {
  uint32_t width = op_type_width(type);
  if (width == 0) {
    return -1;
  }

  // OVL remap must win over base mappings (for example chip RAM at 0x000000).
  if (ovl) {
    int i = read_cache_ovl_idx;
    if (map_slot_active(cfg, i) &&
        cfg->map_data[i] &&
        cfg->rom_size[i] &&
        (cfg->map_type[i] == MAPTYPE_ROM || cfg->map_type[i] == MAPTYPE_RAM_WTC) &&
        cfg->map_mirror[i] != ((unsigned int)-1) &&
        range_contains_size((uint32_t)addr, (uint32_t)cfg->map_mirror[i], (uint32_t)cfg->map_size[i], width)) {
      uint32_t rel = ((uint32_t)addr - (uint32_t)cfg->map_mirror[i]) % (uint32_t)cfg->rom_size[i];
      if (rel <= ((uint32_t)cfg->rom_size[i] - width)) {
        unsigned char* read_addr = cfg->map_data[i] + rel;
        return do_read_value(type, read_addr, val);
      }
    }
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
      if (cfg->map_type[i] == MAPTYPE_NONE) {
        continue;
      }
      if (cfg->map_type[i] != MAPTYPE_ROM && cfg->map_type[i] != MAPTYPE_RAM_WTC) {
        continue;
      }
      if (cfg->map_mirror[i] != ((unsigned int)-1) &&
          cfg->map_data[i] &&
          cfg->rom_size[i] &&
          range_contains_size((uint32_t)addr, (uint32_t)cfg->map_mirror[i], (uint32_t)cfg->map_size[i], width)) {
        uint32_t rel = ((uint32_t)addr - (uint32_t)cfg->map_mirror[i]) % (uint32_t)cfg->rom_size[i];
        if (rel <= ((uint32_t)cfg->rom_size[i] - width)) {
          unsigned char* read_addr = cfg->map_data[i] + rel;
          read_cache_ovl_idx = i;
          return do_read_value(type, read_addr, val);
        }
      }
    }
  }

  if (map_slot_active(cfg, read_cache_base_idx) &&
      range_contains_hi((uint32_t)addr, (uint32_t)cfg->map_offset[read_cache_base_idx],
                        (uint32_t)cfg->map_high[read_cache_base_idx], width)) {
    int res = mapped_read_at_index(cfg, read_cache_base_idx, addr, val, type);
    if (res != -1) {
      return res;
    }
  }

  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE) {
      continue;
    }
    if (range_contains_hi((uint32_t)addr, (uint32_t)cfg->map_offset[i], (uint32_t)cfg->map_high[i], width)) {
      int res = mapped_read_at_index(cfg, i, addr, val, type);
      if (res != -1) {
        read_cache_base_idx = i;
      }
      return res;
    }
  }

  return -1;
}

static inline int mapped_write_at_index(struct emulator_config* cfg, int i, unsigned int addr,
                                        unsigned int value, unsigned char type) {
  uint32_t width = op_type_width(type);
  unsigned char* write_addr = NULL;
  if (width == 0) {
    return -1;
  }
  switch (cfg->map_type[i]) {
  case MAPTYPE_ROM:
    // Kickstart map is typically mprotect()'d read-only; do not write into host
    // backing memory. Let writes fall through to bus handling instead.
    if (map_is_kickstart(cfg, i)) {
      return -1;
    }

    if (rom_write_passthrough_enabled() &&
        cfg->map_data[i] && cfg->rom_size[i] > 0) {
      if (!range_contains_size((uint32_t)addr, (uint32_t)cfg->map_offset[i], (uint32_t)cfg->map_size[i], width)) {
        return -1;
      }
      {
        uint32_t rel = ((uint32_t)addr - (uint32_t)cfg->map_offset[i]) % (uint32_t)cfg->rom_size[i];
        if (rel > ((uint32_t)cfg->rom_size[i] - width)) {
          return -1;
        }
        write_addr = cfg->map_data[i] + rel;
      }
      return do_write_value(type, write_addr, value, 1);
    }

    if (!rom_write_warned[i]) {
      if (map_is_kickstart(cfg, i)) {
        LOG_WARN("[MMAP] Ignoring writes to Kickstart ROM map=%d range=$%.8lX-$%.8lX id=%s\n",
                 i, cfg->map_offset[i], cfg->map_high[i] - 1,
                 cfg->map_id[i] ? cfg->map_id[i] : "None");
      } else {
        LOG_WARN("[MMAP] Ignoring writes to ROM map=%d range=$%.8lX-$%.8lX id=%s\n",
                 i, cfg->map_offset[i], cfg->map_high[i] - 1,
                 cfg->map_id[i] ? cfg->map_id[i] : "None");
      }
      rom_write_warned[i] = 1;
    }
    // Real hardware ROM writes are acknowledged but not stored.
    return 1;

  case MAPTYPE_RAM:
  case MAPTYPE_RAM_NOALLOC:
    if (!cfg->map_data[i]) {
      return -1;
    }
    if (!range_contains_size((uint32_t)addr, (uint32_t)cfg->map_offset[i], (uint32_t)cfg->map_size[i], width)) {
      return -1;
    }
    write_addr = cfg->map_data[i] + (addr - cfg->map_offset[i]);
    return do_write_value(type, write_addr, value, 1);

  case MAPTYPE_RAM_WTC:
    if (!cfg->map_data[i]) {
      return -1;
    }
    if (!range_contains_size((uint32_t)addr, (uint32_t)cfg->map_offset[i], (uint32_t)cfg->map_size[i], width)) {
      return -1;
    }
    write_addr = cfg->map_data[i] + (addr - cfg->map_offset[i]);
    return do_write_value(type, write_addr, value, -1);

  case MAPTYPE_REGISTER:
    if (cfg->platform && cfg->platform->register_write) {
      return cfg->platform->register_write(addr, value, type);
    }
    return -1;

  default:
    return -1;
  }
}

int handle_mapped_write(struct emulator_config* cfg, unsigned int addr, unsigned int value,
                        unsigned char type) {
  uint32_t width = op_type_width(type);
  int res = -1;
  if (width == 0) {
    return -1;
  }

  // OVL write-through remap must win over base mappings.
  if (ovl) {
    int i = write_cache_ovl_idx;
    if (map_slot_active(cfg, i) &&
        cfg->map_type[i] == MAPTYPE_RAM_WTC &&
        cfg->map_data[i] &&
        cfg->rom_size[i] &&
        cfg->map_mirror[i] != ((unsigned int)-1) &&
        range_contains_size((uint32_t)addr, (uint32_t)cfg->map_mirror[i], (uint32_t)cfg->map_size[i], width)) {
      uint32_t rel = ((uint32_t)addr - (uint32_t)cfg->map_mirror[i]) % (uint32_t)cfg->rom_size[i];
      if (rel <= ((uint32_t)cfg->rom_size[i] - width)) {
        unsigned char* write_addr = cfg->map_data[i] + rel;
        return do_write_value(type, write_addr, value, -1);
      }
    }
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
      if (cfg->map_type[i] == MAPTYPE_NONE) {
        continue;
      }
      if (cfg->map_type[i] != MAPTYPE_RAM_WTC) {
        continue;
      }
      if (cfg->map_mirror[i] != ((unsigned int)-1) &&
          cfg->map_data[i] &&
          cfg->rom_size[i] &&
          range_contains_size((uint32_t)addr, (uint32_t)cfg->map_mirror[i], (uint32_t)cfg->map_size[i], width)) {
        uint32_t rel = ((uint32_t)addr - (uint32_t)cfg->map_mirror[i]) % (uint32_t)cfg->rom_size[i];
        if (rel <= ((uint32_t)cfg->rom_size[i] - width)) {
          unsigned char* write_addr = cfg->map_data[i] + rel;
          write_cache_ovl_idx = i;
          return do_write_value(type, write_addr, value, -1);
        }
      }
    }
  }

  if (map_slot_active(cfg, write_cache_base_idx) &&
      range_contains_hi((uint32_t)addr, (uint32_t)cfg->map_offset[write_cache_base_idx],
                        (uint32_t)cfg->map_high[write_cache_base_idx], width)) {
    int wr = mapped_write_at_index(cfg, write_cache_base_idx, addr, value, type);
    if (wr != -1) {
      return wr;
    }
  }

  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE) {
      continue;
    }
    if (range_contains_hi((uint32_t)addr, (uint32_t)cfg->map_offset[i], (uint32_t)cfg->map_high[i], width)) {
      int wr = mapped_write_at_index(cfg, i, addr, value, type);
      if (wr != -1) {
        write_cache_base_idx = i;
      }
      return wr;
    }
  }

  return res;
}
