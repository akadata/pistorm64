// SPDX-License-Identifier: MIT

#include "config_file/config_file.h"

enum base_platforms {
  PLATFORM_NONE,
  PLATFORM_AMIGA,
  PLATFORM_MAC,
  PLATFORM_X68000,
  PLATFORM_NUM,
};

struct platform_config* make_platform_config(const char* name, const char* subsys);

void dump_range_to_file(uint32_t addr, uint32_t size, char* filename);
uint8_t* dump_range_to_memory(uint32_t addr, uint32_t size);

void handle_ovl_mappings_mac68k(struct emulator_config* cfg);

void create_platform_mac68k(struct platform_config* cfg, const char* subsys);
void create_platform_dummy(struct platform_config* cfg, const char* subsys);

int handle_register_read_dummy(unsigned int addr, unsigned char type, unsigned int* val);
int handle_register_write_dummy(unsigned int addr, unsigned int value, unsigned char type);
