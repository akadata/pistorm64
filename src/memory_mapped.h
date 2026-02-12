// SPDX-License-Identifier: MIT

#ifndef PISTORM_MEMORY_MAPPED_H
#define PISTORM_MEMORY_MAPPED_H

#include <stdint.h>
#include "config_file/config_file.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mem_map_kind {
  MEM_MAP_KIND_NONE = 0,
  MEM_MAP_KIND_ROM,
  MEM_MAP_KIND_RAM,
  MEM_MAP_KIND_Z2,
  MEM_MAP_KIND_Z3,
  MEM_MAP_KIND_IO,
  MEM_MAP_KIND_RTG,
  MEM_MAP_KIND_PISCSI,
  MEM_MAP_KIND_PINET,
  MEM_MAP_KIND_AUTOCONFIG,
} mem_map_kind_t;

typedef struct mem_map_entry_info {
  int index;
  uint8_t map_type;
  uint32_t amiga_base;
  uint32_t size;
  uint32_t amiga_end_exclusive;
  void* host_ptr;
  uint32_t host_span;
  const char* map_id;
  mem_map_kind_t kind;
  uint8_t cacheable;
  uint8_t executable;
  uint8_t is_autoconfig;
} mem_map_entry_info_t;

int memmap_lookup(const struct emulator_config* cfg, uint32_t addr, mem_map_entry_info_t* out);
const char* memmap_kind_name(mem_map_kind_t kind);

/*
 * type uses OP_TYPE_* values (see map_op_types in config_file.h):
 *   OP_TYPE_BYTE / OP_TYPE_WORD / OP_TYPE_LONGWORD / OP_TYPE_MEM
 */
int handle_mapped_read(struct emulator_config* cfg,
                       uint32_t addr,
                       uint32_t* val,
                       uint8_t type);

int handle_mapped_write(struct emulator_config* cfg,
                        uint32_t addr,
                        uint32_t value,
                        uint8_t type);

#ifdef __cplusplus
}
#endif

#endif /* PISTORM_MEMORY_MAPPED_H */
