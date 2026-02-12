// SPDX-License-Identifier: MIT

#ifndef PISCSI64_API_H
#define PISCSI64_API_H

#include <stdint.h>

#define PISCSI64_NUM_UNITS 16
#define PISCSI64_OFFSET 0x80020000
#define PISCSI64_REGSIZE 0x00010000
#define PISCSI64_UPPER 0x80030000

void piscsi64_init(void);
void piscsi64_shutdown(void);
void piscsi64_map_drive(const char *filename, uint8_t index);
void piscsi64_unmap_drive(uint8_t index);
int piscsi64_find_free_unit(void);
void piscsi64_refresh_drives(void);

void handle_piscsi64_write(uint32_t addr, uint32_t val, uint8_t type);
uint32_t handle_piscsi64_read(uint32_t addr, uint8_t type);

#endif
