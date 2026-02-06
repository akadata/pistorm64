// SPDX-License-Identifier: MIT

#ifndef PISTORM_MEMORY_MAPPED_H
#define PISTORM_MEMORY_MAPPED_H

#include "config_file/config_file.h"

#ifdef __cplusplus
extern "C" {
#endif

int handle_mapped_read(struct emulator_config* cfg, unsigned int addr, unsigned int* val,
                       unsigned char type);
int handle_mapped_write(struct emulator_config* cfg, unsigned int addr, unsigned int value,
                        unsigned char type);

#ifdef __cplusplus
}
#endif

#endif  // PISTORM_MEMORY_MAPPED_H
