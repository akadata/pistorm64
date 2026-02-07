// SPDX-License-Identifier: MIT

#include "config_file/config_file.h"
#include "memory_mapped.h"
#include "m68k.h"
#include "platforms/amiga/Gayle.h"
#include "log.h"
#include <endian.h>
#include <stdlib.h>
#include <strings.h>

#define CHKRANGE(a, b, c) a >= (unsigned int)b&& a < (unsigned int)(b + c)
#define CHKRANGE_ABS(a, b, c) a >= (unsigned int)b&& a < (unsigned int)c

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

int handle_mapped_read(struct emulator_config* cfg, unsigned int addr, unsigned int* val,
                       unsigned char type) {
  unsigned char* read_addr = NULL;

  // OVL remap must win over base mappings (for example chip RAM at 0x000000).
  if (ovl) {
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
      if (cfg->map_type[i] == MAPTYPE_NONE)
        continue;
      if (cfg->map_type[i] != MAPTYPE_ROM && cfg->map_type[i] != MAPTYPE_RAM_WTC)
        continue;
      if (cfg->map_mirror[i] != ((unsigned int)-1) &&
          CHKRANGE(addr, cfg->map_mirror[i], cfg->map_size[i])) {
        read_addr = cfg->map_data[i] + ((addr - cfg->map_mirror[i]) % cfg->rom_size[i]);
        goto read_value;
      }
    }
  }

  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE)
      continue;
    if (CHKRANGE_ABS(addr, cfg->map_offset[i], cfg->map_high[i])) {
      switch (cfg->map_type[i]) {
      case MAPTYPE_ROM:
        read_addr = cfg->map_data[i] + ((addr - cfg->map_offset[i]) % cfg->rom_size[i]);
        goto read_value;
        break;
      case MAPTYPE_RAM:
      case MAPTYPE_RAM_WTC:
      case MAPTYPE_RAM_NOALLOC:
        read_addr = cfg->map_data[i] + (addr - cfg->map_offset[i]);
        goto read_value;
        break;
      case MAPTYPE_REGISTER:
        if (cfg->platform && cfg->platform->register_read) {
          unsigned int local_target;
          if (cfg->platform->register_read(addr, type, &local_target) != -1) {
            *val = local_target;
            return 1;
          }
        }
        return -1;
        break;
      }
    }
  }

  return -1;

read_value:;
  switch (type) {
  case OP_TYPE_BYTE:
    *val = read_addr[0];
    return 1;
//    break;
  case OP_TYPE_WORD: {
  //  *val = be16toh(((unsigned short*)read_addr)[0]);
  //  return 1;
  //  break;
    uint16_t tmp;
    __builtin_memcpy(&tmp, read_addr, sizeof(tmp));
    *val = be16toh(tmp);
    return 1;
  }
  case OP_TYPE_LONGWORD: {
    //*val = be32toh(((unsigned int*)read_addr)[0]);
    //return 1;
    //break;
    uint32_t tmp;
    __builtin_memcpy(&tmp, read_addr, sizeof(tmp));
    *val = be32toh(tmp);
    return 1;
  }
  case OP_TYPE_MEM:
    return -1;
//    break;
  }

  return 1;
}

int handle_mapped_write(struct emulator_config* cfg, unsigned int addr, unsigned int value,
                        unsigned char type) {
  int res = -1;
  unsigned char* write_addr = NULL;

  // OVL write-through remap must win over base mappings.
  if (ovl) {
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
      if (cfg->map_type[i] == MAPTYPE_NONE)
        continue;
      if (cfg->map_type[i] != MAPTYPE_RAM_WTC)
        continue;
      if (cfg->map_mirror[i] != ((unsigned int)-1) &&
          CHKRANGE(addr, cfg->map_mirror[i], cfg->map_size[i])) {
        write_addr = cfg->map_data[i] + ((addr - cfg->map_mirror[i]) % cfg->rom_size[i]);
        res = -1;
        goto write_value;
      }
    }
  }

  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE)
      continue;
    if (CHKRANGE_ABS(addr, cfg->map_offset[i], cfg->map_high[i])) {
      switch (cfg->map_type[i]) {
      case MAPTYPE_ROM:
        if (rom_write_passthrough_enabled() && cfg->map_data[i] && cfg->rom_size[i] > 0) {
          write_addr = cfg->map_data[i] + ((addr - cfg->map_offset[i]) % cfg->rom_size[i]);
          res = 1;
          goto write_value;
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
        break;
      case MAPTYPE_RAM:
      case MAPTYPE_RAM_NOALLOC:
        write_addr = cfg->map_data[i] + (addr - cfg->map_offset[i]);
        res = 1;
        goto write_value;
        break;
      case MAPTYPE_RAM_WTC:
        // printf("Some write to WTC RAM.\n");
        write_addr = cfg->map_data[i] + (addr - cfg->map_offset[i]);
        res = -1;
        goto write_value;
        break;
      case MAPTYPE_REGISTER:
        if (cfg->platform && cfg->platform->register_write) {
          return cfg->platform->register_write(addr, value, type);
        }
        break;
      }
    }
  }

  return res;

write_value:;
  switch (type) {
  case OP_TYPE_BYTE:
    write_addr[0] = (unsigned char)value;
    return res;
//    break;
  case OP_TYPE_WORD: {
//    ((short*)write_addr)[0] = htobe16(value);
//    return res;
//    break;
    uint16_t tmp = htobe16((uint16_t)value);
    __builtin_memcpy(write_addr, &tmp, sizeof(tmp));
    return res;
  }
  case OP_TYPE_LONGWORD: {
//    ((int*)write_addr)[0] = htobe32(value);
//   return res;
//    break;
    uint32_t tmp = htobe32((uint32_t)value);
    __builtin_memcpy(write_addr, &tmp, sizeof(tmp));
    return res;
  }
  case OP_TYPE_MEM:
    return -1;
//    break;
  }

  // This should never actually happen.
  return res;
}
