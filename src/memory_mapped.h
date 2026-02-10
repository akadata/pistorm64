// SPDX-License-Identifier: MIT

#ifndef PISTORM_MEMORY_MAPPED_H
#define PISTORM_MEMORY_MAPPED_H

#include <stdint.h>
#include "config_file/config_file.h"

#ifdef __cplusplus
extern "C" {
#endif

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
