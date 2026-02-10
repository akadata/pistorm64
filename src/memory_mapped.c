// SPDX-License-Identifier: MIT

#include "config_file/config_file.h"
#include "memory_mapped.h"
#include "m68k.h"
#include "platforms/amiga/Gayle.h"
#include "log.h"
#include <endian.h>
#include <stdlib.h>
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

int handle_mapped_read(struct emulator_config* cfg, unsigned int addr, unsigned int* val,
                       unsigned char type) {
  unsigned char* read_addr = NULL;

  // OVL remap must win over base mappings (for example chip RAM at 0x000000).
  if (ovl) {
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
      if (cfg->map_type[i] == MAPTYPE_NONE) {
        continue;
      }
      if (cfg->map_type[i] != MAPTYPE_ROM && cfg->map_type[i] != MAPTYPE_RAM_WTC) {
        continue;
      }
      if (cfg->map_mirror[i] != ((unsigned int)-1) &&
          CHKRANGE(addr, cfg->map_mirror[i], cfg->map_size[i])) {
        read_addr = cfg->map_data[i] + ((addr - cfg->map_mirror[i]) % cfg->rom_size[i]);
        return do_read_value(type, read_addr, val);
      }
    }
  }

  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE) {
      continue;
    }
    if (CHKRANGE_ABS(addr, cfg->map_offset[i], cfg->map_high[i])) {
      switch (cfg->map_type[i]) {
      case MAPTYPE_ROM:
        read_addr = cfg->map_data[i] + ((addr - cfg->map_offset[i]) % cfg->rom_size[i]);
        return do_read_value(type, read_addr, val);

      case MAPTYPE_RAM:
      case MAPTYPE_RAM_WTC:
      case MAPTYPE_RAM_NOALLOC:
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
        break;
      }
    }
  }

  return -1;
}

int handle_mapped_write(struct emulator_config* cfg, unsigned int addr, unsigned int value,
                        unsigned char type) {
  int res = -1;
  unsigned char* write_addr = NULL;

  // OVL write-through remap must win over base mappings.
  if (ovl) {
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
      if (cfg->map_type[i] == MAPTYPE_NONE) {
        continue;
      }
      if (cfg->map_type[i] != MAPTYPE_RAM_WTC) {
        continue;
      }
      if (cfg->map_mirror[i] != ((unsigned int)-1) &&
          CHKRANGE(addr, cfg->map_mirror[i], cfg->map_size[i])) {
        write_addr = cfg->map_data[i] + ((addr - cfg->map_mirror[i]) % cfg->rom_size[i]);
        res = -1;
        return do_write_value(type, write_addr, value, res);
      }
    }
  }

  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE) {
      continue;
    }
    if (CHKRANGE_ABS(addr, cfg->map_offset[i], cfg->map_high[i])) {
      switch (cfg->map_type[i]) {
      case MAPTYPE_ROM:
        if (rom_write_passthrough_enabled() && cfg->map_data[i] && cfg->rom_size[i] > 0) {
          write_addr = cfg->map_data[i] + ((addr - cfg->map_offset[i]) % cfg->rom_size[i]);
          res = 1;
          return do_write_value(type, write_addr, value, res);
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
        write_addr = cfg->map_data[i] + (addr - cfg->map_offset[i]);
        res = 1;
        return do_write_value(type, write_addr, value, res);

      case MAPTYPE_RAM_WTC:
        write_addr = cfg->map_data[i] + (addr - cfg->map_offset[i]);
        res = -1;
        return do_write_value(type, write_addr, value, res);

      case MAPTYPE_REGISTER:
        if (cfg->platform && cfg->platform->register_write) {
          return cfg->platform->register_write(addr, value, type);
        }
        return -1;

      default:
        break;
      }
    }
  }

  return res;
}
