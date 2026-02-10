// SPDX-License-Identifier: MIT

#include "config_file/config_file.h"
#include <stdbool.h>

#define AC_Z2_BASE 0xE80000
#define AC_Z3_BASE 0xFF000000
#define AC_SIZE (64 * 1024)
#define AC_PIC_LIMIT 16

#define AC_MEM_SIZE_8MB 0
#define AC_MEM_SIZE_64KB 1
#define AC_MEM_SIZE_128KB 2
#define AC_MEM_SIZE_256KB 3
#define AC_MEM_SIZE_512KB 4
#define AC_MEM_SIZE_1MB 5
#define AC_MEM_SIZE_2MB 6
#define AC_MEM_SIZE_4MB 7

#define AC_MEM_SIZE_EXT_16MB 0
#define AC_MEM_SIZE_EXT_32MB 1
#define AC_MEM_SIZE_EXT_64MB 2
#define AC_MEM_SIZE_EXT_128MB 3
#define AC_MEM_SIZE_EXT_256MB 4
#define AC_MEM_SIZE_EXT_512MB 5
#define AC_MEM_SIZE_EXT_1024MB 6
#define AC_MEM_SIZE_EXT_RES 7

// Z2 autoconfig helpers (shared with zorro devices)
#define Z2_Z2 0xC
#define Z2_FAST 0x2
#define Z2_BOOTROM 0x1

enum autoconf_types {
  ACTYPE_NONE,
  ACTYPE_MAPFAST_Z2,
  ACTYPE_MAPFAST_Z3,
  ACTYPE_A314,
  ACTYPE_PISCSI,
  ACTYPE_PISTORM_DEV,
  ACTYPE_Z3BUS_DEMO,
  ACTYPE_ZORRO_GENERIC,
  ACTYPE_NUM,
};

enum autoconfg_z3_regs {
  AC_Z3_REG_ER_TYPE = 0x00,
  AC_Z3_REG_ER_PRODUCT = 0x04,
  AC_Z3_REG_ER_FLAGS = 0x08,
  AC_Z3_REG_ER_RES03 = 0x0C,
  AC_Z3_REG_MAN_HI = 0x10,
  AC_Z3_REG_MAN_LO = 0x14,
  AC_Z3_REG_SER_BYTE0 = 0x18,
  AC_Z3_REG_SER_BYTE1 = 0x1C,
  AC_Z3_REG_SER_BYTE2 = 0x20,
  AC_Z3_REG_SER_BYTE3 = 0x24,
  AC_Z3_REG_INIT_DIAG_VEC_HI = 0x28,
  AC_Z3_REG_INIT_DIAG_VEC_LO = 0x2C,
  AC_Z3_REG_ER_RES0C = 0x30,
  AC_Z3_REG_ER_RES0D = 0x34,
  AC_Z3_REG_ER_RES0E = 0x38,
  AC_Z3_REG_ER_RES0F = 0x3C,
  AC_Z3_REG_ER_Z2_INT = 0x40,
  AC_Z3_REG_WR_ADDR_HI = 0x44,
  AC_Z3_REG_WR_ADDR_NIB_HI = 0x46,
  AC_Z3_REG_WR_ADDR_LO = 0x48,
  AC_Z3_REG_WR_ADDR_NIB_LO = 0x4A,
  AC_Z3_REG_SHUTUP = 0x4C,
  AC_Z3_REG_RES50 = 0x50,
  AC_Z3_REG_RES54 = 0x54,
  AC_Z3_REG_RES58 = 0x58,
  AC_Z3_REG_RES5C = 0x5C,
  AC_Z3_REG_RES60 = 0x60,
  AC_Z3_REG_RES64 = 0x64,
  AC_Z3_REG_RES68 = 0x68,
  AC_Z3_REG_RES6C = 0x6C,
  AC_Z3_REG_RES70 = 0x70,
  AC_Z3_REG_RES74 = 0x74,
  AC_Z3_REG_RES78 = 0x78,
  AC_Z3_REG_RES7C = 0x7C,
};

void autoconf_register_zorro_device(uint8_t zorro_index);

#define BOARDTYPE_Z3 0x80
#define BOARDTYPE_Z2 (0x80 | 0x40)
#define BOARDTYPE_FREEMEM 0x20
#define BOARDTYPE_BOOTROM 0x10
#define BOARDTYPE_LINKED 0x08

#define Z3_FLAGS_MEMORY 0x80
#define Z3_FLAGS_NOSHUTUP 0x40
#define Z3_FLAGS_EXTENSION 0x20
#define Z3_FLAGS_RESERVED 0x10

#define PISTORM_MANUF_ID     0x07DB     // existing IT IS not 0xDEBE
#define PISTORM_PRODUCT_ID   0xBABE     // example


// PiStorm virtual product IDs (one per logical device)
typedef enum pistorm_product_id {
  PISTORM_PROD_Z2_FAST       = 0x0001,
  PISTORM_PROD_Z3_FAST       = 0x0002,

  PISTORM_PROD_PISCSI_Z2     = 0x0010,
  PISTORM_PROD_PISTORM_DEV   = 0x0011,

  PISTORM_PROD_Z2_SERIAL     = 0x0020,
  PISTORM_PROD_Z2_RNG        = 0x0021,
  PISTORM_PROD_Z2_PISSA      = 0x0022,

  PISTORM_PROD_PI_NET        = 0x0030,
  PISTORM_PROD_PI_AHI        = 0x0031,

  PISTORM_PROD_Z3BUS_DEMO    = 0x00F0,
} pistorm_product_id_t;


unsigned int autoconfig_read_memory_8(struct emulator_config* cfg, unsigned int address);
void autoconfig_write_memory_8(struct emulator_config* cfg, unsigned int address,
                               unsigned int value);

unsigned int autoconfig_read_memory_z3_8(struct emulator_config* cfg, unsigned int address);
void autoconfig_write_memory_z3_8(struct emulator_config* cfg, unsigned int address,
                                  unsigned int value);

//added 16 bit reads
unsigned int autoconfig_read_memory_z3_16(struct emulator_config* cfg, unsigned int address);
void autoconfig_write_memory_z3_16(struct emulator_config* cfg, unsigned int address,
                                   unsigned int value);

//added 32 bit read  write 
unsigned int autoconfig_read_memory_z3_32(struct emulator_config* cfg, unsigned int address);
void autoconfig_write_memory_z3_32(struct emulator_config* cfg, unsigned int address,
                                   unsigned int value);
void autoconfig_reset_all(void);

bool add_z2_pic(uint8_t type, uint8_t index);
bool add_z3_pic(uint8_t type, uint8_t index);

void remove_z2_pic(uint8_t type, uint8_t index);
void remove_z3_pic(uint8_t type, uint8_t index);
