// SPDX-License-Identifier: MIT

#ifndef _CONFIG_FILE_H
#define _CONFIG_FILE_H

#include <stdint.h>
#include <unistd.h>

#define MAX_NUM_MAPPED_ITEMS 64
#define SIZE_KILO 1024
#define SIZE_MEGA (1024 * 1024)
#define SIZE_GIGA (1024 * 1024 * 1024)

typedef enum {
  MAPTYPE_NONE,
  MAPTYPE_ROM,
  MAPTYPE_RAM,
  MAPTYPE_REGISTER,
  MAPTYPE_RAM_NOALLOC,
  MAPTYPE_RAM_WTC,
  MAPTYPE_FILE, /* cryptodad */
  MAPTYPE_NUM,
} map_types;

typedef enum {
  MAPALLOC_NONE = 0,
  MAPALLOC_HEAP,
  MAPALLOC_MMAP_LOW4G,
  MAPALLOC_EXTERNAL,
} map_alloc_kind_t;

typedef enum {
  MAPCMD_UNKNOWN,
  MAPCMD_TYPE,
  MAPCMD_ADDRESS,
  MAPCMD_SIZE,
  MAPCMD_RANGE,
  MAPCMD_FILENAME,
  MAPCMD_OVL_REMAP,
  MAPCMD_MAP_ID,
  MAPCMD_AUTODUMP_FILE,
  MAPCMD_AUTODUMP_MEM,
  MAPCMD_NUM,
} map_cmds;

typedef enum {
  CONFITEM_NONE,
  CONFITEM_CPUTYPE,
  CONFITEM_MAP,
  CONFITEM_LOOPCYCLES,
  CONFITEM_JIT,
  CONFITEM_JIT_FPU,
  CONFITEM_MOUSE,
  CONFITEM_KEYBOARD,
  CONFITEM_PLATFORM,
  CONFITEM_PISTORM,
  CONFITEM_PISTORM_GPCLK_SRC,
  CONFITEM_PISTORM_GPCLK_DIV,
  CONFITEM_PISTORM_MMIO_WR_STRETCH,
  CONFITEM_PISTORM_MMIO_RD_STRETCH,
  CONFITEM_PISTORM_MMIO_LWPAIR,
  CONFITEM_PISTORM_MMIO_R32PAIR,
  CONFITEM_PISTORM_MMIO_RAMSEQ,
  CONFITEM_PISTORM_MMIO_WPIPE,
  CONFITEM_SETVAR,
  CONFITEM_KBFILE,
  CONFITEM_AFFINITY,
  CONFITEM_RTPRIO,
  CONFITEM_NUM,
} config_items;

typedef enum {
  JIT_BACKEND_AUTO = 0,
  JIT_BACKEND_UAE = 1,
  JIT_BACKEND_M68XKCPU = 2,
} jit_backend_kind_t;

typedef enum {
  OP_TYPE_BYTE,
  OP_TYPE_WORD,
  OP_TYPE_LONGWORD,
  OP_TYPE_MEM,
  OP_TYPE_NUM,
} map_op_types;

struct emulator_config {
  unsigned int cpu_type;

  unsigned char map_type[MAX_NUM_MAPPED_ITEMS];
  unsigned long map_offset[MAX_NUM_MAPPED_ITEMS];
  unsigned long map_high[MAX_NUM_MAPPED_ITEMS];
  unsigned int map_size[MAX_NUM_MAPPED_ITEMS];
  unsigned int rom_size[MAX_NUM_MAPPED_ITEMS];
  unsigned char* map_data[MAX_NUM_MAPPED_ITEMS];
  size_t map_alloc_size[MAX_NUM_MAPPED_ITEMS];
  unsigned char map_alloc_kind[MAX_NUM_MAPPED_ITEMS];
  unsigned int map_mirror[MAX_NUM_MAPPED_ITEMS];
  char* map_id[MAX_NUM_MAPPED_ITEMS];

  struct platform_config* platform;

  char *mouse_file;
  char *keyboard_file;

  char mouse_toggle_key;
  char keyboard_toggle_key;

  unsigned char mouse_enabled;
  unsigned char mouse_autoconnect;
  unsigned char keyboard_enabled;
  unsigned char keyboard_grab;
  unsigned char keyboard_autoconnect;
  unsigned char enable_jit;
  unsigned char jit_backend;
  unsigned char enable_fpu_jit;

  unsigned int loop_cycles; 
  unsigned int mapped_low;
  unsigned int mapped_high;
  unsigned int custom_low;
  unsigned int custom_high;
  uint8_t  ranges_dirty;
};


struct platform_config {
  char* subsys;
  unsigned char id;

  int (*custom_read)(
    struct emulator_config* cfg, 
    unsigned int addr, 
    unsigned int* val,
    unsigned char type
    );
  int (*custom_write)(
    struct emulator_config* cfg, 
    unsigned int addr, 
    unsigned int val,
    unsigned char type
    );

  int (*register_read)(
    unsigned int addr, 
    unsigned char type, 
    unsigned int* val
    );
  int (*register_write)(
    unsigned int addr, 
    unsigned int value, 
    unsigned char type
    );

  int (*platform_initial_setup)(struct emulator_config* cfg);
  void (*handle_reset)(struct emulator_config* cfg);
  void (*shutdown)(struct emulator_config* cfg);
  void (*setvar)(struct emulator_config* cfg, 
    const char* var, 
    const char* val);
};

#ifdef __cplusplus
extern "C" {
#endif

unsigned int get_m68k_cpu_type(const char* name);
struct emulator_config* load_config_file(const char* filename);
void free_config_file(struct emulator_config* cfg);
int apply_config_line(struct emulator_config* cfg, 
  const char* line, 
  int line_no);
int preparse_pistorm_backend(const char* filename);

int handle_mapped_read(struct emulator_config* cfg, 
  unsigned int addr, 
  unsigned int* val, 
  unsigned char type);

int handle_mapped_write(struct emulator_config* cfg, 
  unsigned int addr, 
  unsigned int value, 
  unsigned char type);

int get_named_mapped_item(struct emulator_config* cfg, 
  const char* name);

int get_mapped_item_by_address(struct emulator_config* cfg, 
  uint32_t address);

uint8_t* get_mapped_data_pointer_by_address(struct emulator_config* cfg, 
  uint32_t address);

void add_mapping(struct emulator_config* cfg, 
  unsigned int type, 
  unsigned int addr,
  unsigned int size, 
  unsigned int mirr_addr, 
  char* filename, 
  const char* map_id,
  unsigned int autodump);
unsigned int get_int(const char* str);

unsigned char* cfg_alloc_mapped_data(size_t size, int zero_init, unsigned char* alloc_kind,
                                     const char* owner);
void cfg_free_mapped_data(unsigned char* ptr, size_t size, unsigned char alloc_kind);
void cfg_set_map_data_allocation(struct emulator_config* cfg, int index, unsigned char* ptr,
                                 size_t alloc_size, unsigned char alloc_kind);
void cfg_release_map_data(struct emulator_config* cfg, int index);

#ifdef __cplusplus
}
#endif

#endif /* _CONFIG_FILE_H */
