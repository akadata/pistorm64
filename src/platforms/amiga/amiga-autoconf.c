// SPDX-License-Identifier: MIT

#include "m68k.h"
#include "platforms/platforms.h"
#include "pistorm-dev/pistorm-dev-enums.h"
#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/amiga-platform.h"
#include "platforms/amiga/amiga_zorro.h"

/* Sanity check: PIC list must be at least as large as Zorro device table. */
#if AC_PIC_LIMIT < MAX_ZORRO_DEVICES
#error "AC_PIC_LIMIT must be >= MAX_ZORRO_DEVICES"
#endif

#include "a314/a314.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// PiStorm Zorro II AutoConfig Fast RAM ROM
static unsigned char ac_fast_ram_rom[] = {
    Z2_Z2 | Z2_FAST,
    AC_MEM_SIZE_8MB, // 00/02, link into memory free list, 8 MB
    0x6,
    0x9, // 06/09, product id
    0x8,
    0x0, // 08/0a, preference to 8 MB space
    0x0,
    0x0,                 // 0c/0e, reserved
    PISTORM_AC_MANUF_ID, // Manufacturer ID
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x4,
    0x2,
    0x0, // 18/.../26, serial
    0x0,
    0x0,
    0x0,
    0x0, // Optional BOOT ROM vector
};

// PiSCSI AutoConfig Device ROM
unsigned char ac_piscsi_rom[] = {
    Z2_Z2 | Z2_BOOTROM,
    AC_MEM_SIZE_64KB, // 00/01, Z2, bootrom, 64 KB
    0x6,
    0xA, // 06/0A, product id
    0x0,
    0x0, // 00/0a, any space where it fits
    0x0,
    0x0,                 // 0c/0e, reserved
    PISTORM_AC_MANUF_ID, // Manufacturer ID
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x4,
    0x2,
    0x1, // 18/.../26, serial
    0x4,
    0x0,
    0x0,
    0x0, // Optional BOOT ROM vector
};

// PiStorm Device Interaction ROM
unsigned char ac_pistorm_rom[] = {
    Z2_Z2,
    AC_MEM_SIZE_64KB, // 00/01, Z2, bootrom, 64 KB
    0x6,
    0xB, // 06/0B, product id
    0x0,
    0x0, // 00/0a, any space where it fits
    0x0,
    0x0,                 // 0c/0e, reserved
    PISTORM_AC_MANUF_ID, // Manufacturer ID
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x4,
    0x2,
    0x2, // 18/.../26, serial
    0x4,
    0x0,
    0x0,
    0x0, // Optional BOOT ROM vector
};

// z3bus demo AutoConfig Device ROM moved into zorro device registration.

// A314 Emulation ROM
static unsigned char ac_a314_rom[] = {
    0xc, AC_MEM_SIZE_64KB, // 00/02, 64 kB
    0xa, 0x3,              // 04/06, product id
    0x0, 0x0,              // 08/0a, any space okay
    0x0, 0x0,              // 0c/0e, reserved
    0x0, 0x7,
    0xd, 0xb, // 10/12/14/16, mfg id
    0xa, 0x3,
    0x1, 0x4,
    0x0, 0x0,
    0x0, 0x0, // 18/.../26, serial
    0x0, 0x0,
    0x0, 0x0, // Optional BOOT ROM vector
};

void autoconf_register_zorro_device(uint8_t zorro_index) {
  zorro_device_t *dev = zorro_get_device_by_index(zorro_index);
  if (!dev) {
    LOG_WARN("[AUTOCONF] Tried to register missing Zorro device index %u\n", zorro_index);
    return;
  }

  bool ok;
  if (dev->bus == ZORRO_BUS_Z3) {
    ok = add_z3_pic(ACTYPE_ZORRO_GENERIC, zorro_index);
  } else {
    ok = add_z2_pic(ACTYPE_ZORRO_GENERIC, zorro_index);
  }

  if (!ok) {
    LOG_WARN("[AUTOCONF] Zorro device index %u not registered, PIC table full.\n",
             zorro_index);
  }
}



int ac_z2_type[AC_PIC_LIMIT];
int ac_z2_index[AC_PIC_LIMIT];
unsigned int ac_base[AC_PIC_LIMIT];

int ac_z3_type[AC_PIC_LIMIT];
int ac_z3_index[AC_PIC_LIMIT];

uint32_t ac_z2_pic_count = 0;
uint32_t ac_z3_pic_count = 0;

uint32_t ac_z2_current_pic = 0;
uint32_t ac_z3_current_pic = 0;

int ac_z2_done = 0;
int ac_z3_done = 0;

// why put special cases in global code, commented them out  and fixed in the switch.
//uint32_t piscsi_base = 0;
//uint32_t pistorm_dev_base = 0;

extern uint8_t* piscsi_rom_ptr;
extern uint32_t piscsi_base;
extern uint32_t pistorm_dev_base;

typedef struct {
    unsigned int mbytes;     /* size in megabytes */
    unsigned char code;      /* AC_MEM_SIZE_* constant */
} autoconf_size_entry;

static const autoconf_size_entry autoconf_sizes[] = {
    {  2u, AC_MEM_SIZE_2MB   },
    {  4u, AC_MEM_SIZE_4MB   },
    {  8u, AC_MEM_SIZE_8MB   },
};

static const autoconf_size_entry autoconf_sizes_ext[] = {
    {  16u,   AC_MEM_SIZE_EXT_16MB   },
    {  32u,   AC_MEM_SIZE_EXT_32MB   },
    {  64u,   AC_MEM_SIZE_EXT_64MB   },
    { 128u,   AC_MEM_SIZE_EXT_128MB  },
    { 256u,   AC_MEM_SIZE_EXT_256MB  },
    { 512u,   AC_MEM_SIZE_EXT_512MB  },
    { 1024u,  AC_MEM_SIZE_EXT_1024MB },
};

static unsigned char lookup_autoconf_size(unsigned int size,
                     const autoconf_size_entry *table,
                     size_t count,
                     unsigned char default_code) {
    /* Reject non-MB multiples early */
    if (size % SIZE_MEGA != 0) {
        return default_code;
    }

    unsigned int mbytes = size / SIZE_MEGA;

    for (size_t i = 0; i < count; ++i) {
        if (table[i].mbytes == mbytes) {
            return table[i].code;
        }
    }

    return default_code;
}
static unsigned char get_autoconf_size(unsigned int size)
{
    return lookup_autoconf_size(
        size,
        autoconf_sizes,
        sizeof autoconf_sizes / sizeof autoconf_sizes[0],
        AC_MEM_SIZE_64KB  /* fallback */
    );
}

static unsigned char get_autoconf_size_ext(unsigned int size)
{
    return lookup_autoconf_size(
        size,
        autoconf_sizes_ext,
        sizeof autoconf_sizes_ext / sizeof autoconf_sizes_ext[0],
        AC_MEM_SIZE_EXT_64MB  /* old “else” default */
    );
}


extern void adjust_ranges_amiga(struct emulator_config* cfg);

void autoconfig_reset_all(void) {
  LOG_INFO("[AUTOCONF] Resetting all autoconf data.\n");
  for (int i = 0; i < AC_PIC_LIMIT; i++) {
    ac_z2_type[i]   = ACTYPE_NONE;
    ac_z3_type[i]   = ACTYPE_NONE;
    ac_z2_index[i]  = 0;
    ac_z3_index[i]  = 0;
  }

  ac_z2_pic_count    = 0;
  ac_z3_pic_count    = 0;
  ac_z2_current_pic  = 0;
  ac_z3_current_pic  = 0;
  ac_z2_done         = 0;
  ac_z3_done         = 0;
}


unsigned int autoconfig_read_memory_z3_8(struct emulator_config* cfg, unsigned int address) {
  int index = ac_z3_index[ac_z3_current_pic];
  unsigned char val = 0;

  if (ac_z3_type[ac_z3_current_pic] == ACTYPE_ZORRO_GENERIC) {
    zorro_device_t *dev = zorro_get_device_by_index((uint8_t)index);
    if (!dev) {
      return 0;
    }
    if ((address & 0xFF) >= AC_Z3_REG_RES50 && (address & 0xFF) <= AC_Z3_REG_RES7C) {
      val = 0;
    } else {
      switch (address & 0xFF) {
      case AC_Z3_REG_ER_TYPE:
        val |= BOARDTYPE_Z3;
        if (dev->flags & Z3_FLAGS_MEMORY)
          val |= BOARDTYPE_FREEMEM;
        if (dev->size > 8 * SIZE_MEGA)
          val |= get_autoconf_size_ext(dev->size);
        else
          val |= get_autoconf_size(dev->size);
        if (ac_z3_current_pic + 1 < ac_z3_pic_count)
          val |= BOARDTYPE_LINKED;
        val ^= 0xFF;
        break;
      case AC_Z3_REG_ER_PRODUCT:
        val = (unsigned char)(dev->product & 0xFF);
        break;
      case AC_Z3_REG_ER_FLAGS:
        val |= (unsigned char)(dev->flags & 0xF0);
        if (dev->size > 8 * SIZE_MEGA)
          val |= Z3_FLAGS_EXTENSION;
        val |= Z3_FLAGS_RESERVED;
        break;
      case AC_Z3_REG_MAN_LO:
        val = (unsigned char)(dev->manufacturer & 0x00FF);
        break;
      case AC_Z3_REG_MAN_HI:
        val = (unsigned char)(dev->manufacturer >> 8);
        break;
      case AC_Z3_REG_SER_BYTE0:
      case AC_Z3_REG_SER_BYTE1:
      case AC_Z3_REG_SER_BYTE2:
      case AC_Z3_REG_SER_BYTE3:
        val = 0;
        break;
      case AC_Z3_REG_INIT_DIAG_VEC_LO:
      case AC_Z3_REG_INIT_DIAG_VEC_HI:
        val = 0;
        break;
      case AC_Z3_REG_ER_RES03:
      case AC_Z3_REG_ER_RES0D:
      case AC_Z3_REG_ER_RES0E:
      case AC_Z3_REG_ER_RES0F:
      case AC_Z3_REG_ER_Z2_INT:
        val = 0;
        break;
      default:
        val = 0;
        break;
      }
    }
    return (address & 0x100) ? (unsigned int)((val << 4) ^ 0xFF)
                             : (unsigned int)((val & 0xF0) ^ 0xFF);
  }

  if ((address & 0xFF) >= AC_Z3_REG_RES50 && (address & 0xFF) <= AC_Z3_REG_RES7C) {
    val = 0;
  } else {
    switch (address & 0xFF) {
    case AC_Z3_REG_ER_TYPE:
      val |= BOARDTYPE_Z3;
      if (cfg->map_type[index] == MAPTYPE_RAM)
        val |= BOARDTYPE_FREEMEM;
      if (cfg->map_size[index] > 8 * SIZE_MEGA)
        val |= get_autoconf_size_ext(cfg->map_size[index]);
      else
        val |= get_autoconf_size(cfg->map_size[index]);
      if (ac_z3_current_pic + 1 < ac_z3_pic_count)
        val |= BOARDTYPE_LINKED;
      // Pre-invert this value, since it's the only value not physically complemented
      // for Zorro III.
      val ^= 0xFF;
      break;
    case AC_Z3_REG_ER_PRODUCT:
      // 1.1... maybe...
      val = 0x11;
      break;
    case AC_Z3_REG_ER_FLAGS:
      if (cfg->map_type[index] == MAPTYPE_RAM)
        val |= Z3_FLAGS_MEMORY;
      if (cfg->map_size[index] > 8 * SIZE_MEGA)
        val |= Z3_FLAGS_EXTENSION;
      val |= Z3_FLAGS_RESERVED;
      // Bottom four bits are zero, useless unles you want really odd RAM sizes.
      break;
    // Manufacturer ID low/high bytes.
    case AC_Z3_REG_MAN_LO:
      val = PISTORM_MANUF_ID & 0x00FF;
      break;
    case AC_Z3_REG_MAN_HI:
      val = (PISTORM_MANUF_ID >> 8);
      break;
    case AC_Z3_REG_SER_BYTE0:
    case AC_Z3_REG_SER_BYTE1:
    case AC_Z3_REG_SER_BYTE2:
    case AC_Z3_REG_SER_BYTE3:
      // Expansion board serial assigned by manufacturer.
      val = 0;
      break;
    case AC_Z3_REG_INIT_DIAG_VEC_LO:
    case AC_Z3_REG_INIT_DIAG_VEC_HI:
      // 16-bit offset to boot ROM in assigned memory range.
      val = 0;
      break;
    // Additional reserved/unused registers.
    case AC_Z3_REG_ER_RES03:
    case AC_Z3_REG_ER_RES0D:
    case AC_Z3_REG_ER_RES0E:
    case AC_Z3_REG_ER_RES0F:
    case AC_Z3_REG_ER_Z2_INT:
      val = 0;
      break;
    default:
      val = 0;
      break;
    }
  }
  // printf("Read byte %d from Z3 autoconf for PIC %d (%.2X).\n", address, ac_z3_current_pic, val);
  return (address & 0x100) ? (unsigned int)((val << 4) ^ 0xFF)
                           : (unsigned int)((val & 0xF0) ^ 0xFF);
}

int nib_latch = 0;

void autoconfig_write_memory_z3_8(struct emulator_config* cfg, unsigned int address,
                                  unsigned int value) {
  int index = ac_z3_index[ac_z3_current_pic];
  unsigned char val = (unsigned char)value;
  int done = 0;
  int is_zorro = (ac_z3_type[ac_z3_current_pic] == ACTYPE_ZORRO_GENERIC);

  switch (address & 0xFF) {
  case AC_Z3_REG_WR_ADDR_LO:
    if (nib_latch) {
      ac_base[ac_z3_current_pic] =
          (ac_base[ac_z3_current_pic] & 0xFF0F0000) | ((unsigned int)(val & 0xF0) << 16);
      nib_latch = 0;
    } else
      ac_base[ac_z3_current_pic] =
          (ac_base[ac_z3_current_pic] & 0xFF000000) | ((unsigned int)val << 16);
    break;
  case AC_Z3_REG_WR_ADDR_HI:
    if (nib_latch) {
      ac_base[ac_z3_current_pic] =
          (ac_base[ac_z3_current_pic] & 0x0FFF0000) | ((unsigned int)(val & 0xF0) << 24);
      nib_latch = 0;
    }
    ac_base[ac_z3_current_pic] =
        (ac_base[ac_z3_current_pic] & 0x00FF0000) | ((unsigned int)val << 24);
    done = 1;
    break;
  case AC_Z3_REG_WR_ADDR_NIB_LO:
    ac_base[ac_z3_current_pic] =
        (ac_base[ac_z3_current_pic] & 0xFFF00000) | ((unsigned int)(val & 0xF0) << 12);
    nib_latch = 1;
    break;
  case AC_Z3_REG_WR_ADDR_NIB_HI:
    ac_base[ac_z3_current_pic] =
        (ac_base[ac_z3_current_pic] & 0xF0FF0000) | ((unsigned int)(val & 0xF0) << 20);
    nib_latch = 1;
    break;
  case AC_Z3_REG_SHUTUP:
    printf("Write to Z3 shutup register for PIC %d.\n", ac_z3_current_pic);
    done = 1;
    break;
  default:
    break;
  }

  if (done) {
    nib_latch = 0;
    if (is_zorro) {
      zorro_device_t *dev = zorro_get_device_by_index((uint8_t)index);
      if (dev) {
        dev->base = ac_base[ac_z3_current_pic];
      }
      LOG_INFO("[AUTOCONF] Address of Z3 autoconf device assigned to $%.8x\n",
               ac_base[ac_z3_current_pic]);
    } else {
      LOG_INFO("[AUTOCONF] Address of Z3 autoconf RAM assigned to $%.8x [B]\n",
               ac_base[ac_z3_current_pic]);
      cfg->map_offset[index] = ac_base[ac_z3_current_pic];
      cfg->map_high[index] = cfg->map_offset[index] + cfg->map_size[index];
      m68k_add_ram_range((uint32_t)cfg->map_offset[index], (uint32_t)cfg->map_high[index],
                         cfg->map_data[index]);
    }
    ac_z3_current_pic++;
    if (ac_z3_current_pic == ac_z3_pic_count) {
      ac_z3_done = 1;
      adjust_ranges_amiga(cfg);
    }
  }

  return;
}

void autoconfig_write_memory_z3_16(struct emulator_config* cfg, unsigned int address,
                                   unsigned int value) {
  int index = ac_z3_index[ac_z3_current_pic];
  unsigned short val = (unsigned short)value;
  int done = 0;
  int is_zorro = (ac_z3_type[ac_z3_current_pic] == ACTYPE_ZORRO_GENERIC);

  switch (address & 0xFF) {
  case AC_Z3_REG_WR_ADDR_HI:
    // This is, as far as I know, the only register it should write a 16-bit value to.
    ac_base[ac_z3_current_pic] =
        (ac_base[ac_z3_current_pic] & 0x00000000) | ((unsigned int)val << 16);
    done = 1;
    break;
  default:
    LOG_WARN("[AUTOCONF] Unknown WORD write to Z3 autoconf address $%.2X\n", address & 0xFF);
    // stop_cpu_emulation();
    break;
  }

  if (done) {
    if (is_zorro) {
      zorro_device_t *dev = zorro_get_device_by_index((uint8_t)index);
      if (dev) {
        dev->base = ac_base[ac_z3_current_pic];
      }
      LOG_INFO("[AUTOCONF] Address of Z3 autoconf device assigned to $%.8x\n",
               ac_base[ac_z3_current_pic]);
    } else {
      LOG_INFO("[AUTOCONF] Address of Z3 autoconf RAM assigned to $%.8x [W]\n",
               ac_base[ac_z3_current_pic]);
      cfg->map_offset[index] = ac_base[ac_z3_current_pic];
      cfg->map_high[index] = cfg->map_offset[index] + cfg->map_size[index];
      m68k_add_ram_range((uint32_t)cfg->map_offset[index], (uint32_t)cfg->map_high[index],
                         cfg->map_data[index]);
    }
    ac_z3_current_pic++;
    if (ac_z3_current_pic == ac_z3_pic_count) {
      ac_z3_done = 1;
      adjust_ranges_amiga(cfg);
    }
  }

  return;
}

// Common helper for Z2/Z3 PIC tables
static bool add_pic(int        *type_arr,
                    int        *index_arr,
                    uint32_t   *counter_ptr,
                    const char *bus_name,
                    int         type,
                    int         index) {
  const uint32_t count = *counter_ptr;

  if (count < AC_PIC_LIMIT) {
    type_arr[count]  = type;
    index_arr[count] = index;
    (*counter_ptr)++;
    return true;
  } else {
    LOG_WARN("[AUTOCONF] Failed to add %s PIC of type %d, limit %d exceeded.\n",
             bus_name, type, AC_PIC_LIMIT);
    return false;
  }
}


bool add_z2_pic(uint8_t type, uint8_t index)
{
  return add_pic(ac_z2_type, ac_z2_index, &ac_z2_pic_count, "Z2", type, index);
}

bool add_z3_pic(uint8_t type, uint8_t index)
{
  return add_pic(ac_z3_type, ac_z3_index, &ac_z3_pic_count, "Z3", type, index);
}



void remove_z2_pic(uint8_t type, uint8_t index) {
  uint8_t pic_found = 0;
  if (index) {
  }

  for (uint32_t i = 0; i < ac_z2_pic_count; i++) {
    if (ac_z2_type[i] == type && !pic_found) {
      pic_found = 1;
    }
    if (pic_found && i < AC_PIC_LIMIT - 1) {
      ac_z2_type[i] = ac_z2_type[i + 1];
      ac_z2_index[i] = ac_z2_index[i + 1];
    }
  }

  if (pic_found) {
    ac_z2_type[AC_PIC_LIMIT - 1] = ACTYPE_NONE;
    ac_z2_index[AC_PIC_LIMIT - 1] = 0;
    ac_z2_pic_count--;
  } else {
    LOG_WARN("[AUTOCONF] Tried to remove Z2 PIC of type %d, but it wasn't found.\n", type);
  }
}

unsigned int autoconfig_read_memory_8(struct emulator_config* cfg, unsigned int address) {
  const unsigned char* rom = NULL;
  size_t rom_size = sizeof(ac_fast_ram_rom);
  unsigned char val = 0;

  switch (ac_z2_type[ac_z2_current_pic]) {
  case ACTYPE_MAPFAST_Z2:
    rom = ac_fast_ram_rom;
    rom_size = sizeof(ac_fast_ram_rom);
    break;
  case ACTYPE_A314:
    rom = ac_a314_rom;
    rom_size = sizeof(ac_a314_rom);
    break;
  case ACTYPE_PISCSI:
    rom = ac_piscsi_rom;
    rom_size = sizeof(ac_piscsi_rom);
    break;
  case ACTYPE_PISTORM_DEV:
    rom = ac_pistorm_rom;
    rom_size = sizeof(ac_pistorm_rom);
    break;
  case ACTYPE_ZORRO_GENERIC: {
    zorro_device_t *dev =
        zorro_get_device_by_index((uint8_t)ac_z2_index[ac_z2_current_pic]);
    if (dev && dev->ac_rom) {
      rom = dev->ac_rom;
      rom_size = dev->ac_rom_size;
    } else {
      return 0;
    }
    break;
  }
  default:
    return 0;
    break;
  }

  if ((address & 1) == 0 && (size_t)(address / 2) < rom_size) {
    if (ac_z2_type[ac_z2_current_pic] == ACTYPE_MAPFAST_Z2 && address / 2 == 1) {
      val = get_autoconf_size(cfg->map_size[ac_z2_index[ac_z2_current_pic]]);
      if (ac_z2_current_pic + 1 < ac_z2_pic_count)
        val |= BOARDTYPE_LINKED;
    } else
      val = rom[address / 2];
      //printf("Read byte %d from Z2 autoconf for PIC %d (%.2X).\n", address/2, ac_z2_current_pic, val);
  }
  val <<= 4;
  if (address != 0 && address != 2 && address != 0x40 && address != 0x42)
    val ^= 0xff;

  return (unsigned int)val;
}

unsigned int autoconfig_read_memory_16(struct emulator_config* cfg, unsigned int address) {
  unsigned int hi = autoconfig_read_memory_8(cfg, address + 0);
  unsigned int lo = autoconfig_read_memory_8(cfg, address + 1);
  return ((hi & 0xFFu) << 8) | (lo & 0xFFu);
}

unsigned int autoconfig_read_memory_32(struct emulator_config* cfg, unsigned int address) {
  unsigned int b0 = autoconfig_read_memory_8(cfg, address + 0);
  unsigned int b1 = autoconfig_read_memory_8(cfg, address + 1);
  unsigned int b2 = autoconfig_read_memory_8(cfg, address + 2);
  unsigned int b3 = autoconfig_read_memory_8(cfg, address + 3);
  return ((b0 & 0xFFu) << 24) | ((b1 & 0xFFu) << 16) | ((b2 & 0xFFu) << 8) | (b3 & 0xFFu);
}



void autoconfig_write_memory_8(struct emulator_config* cfg, unsigned int address,
                               unsigned int value) {
  int done = 0;
  int index = ac_z2_index[ac_z2_current_pic];

  unsigned int* base = NULL;
  uint32_t zorro_temp_base = 0;
  zorro_device_t *zorro_dev = NULL;

  switch (ac_z2_type[ac_z2_current_pic]) {
  case ACTYPE_MAPFAST_Z2:
    base = &ac_base[ac_z2_current_pic];
    break;
  case ACTYPE_A314:
    base = &a314_base;
    break;
  case ACTYPE_PISCSI:
    base = &piscsi_base;
    break;
  case ACTYPE_PISTORM_DEV:
    base = &pistorm_dev_base;
    break;
  case ACTYPE_ZORRO_GENERIC:
    zorro_dev = zorro_get_device_by_index((uint8_t)ac_z2_index[ac_z2_current_pic]);
    base = &zorro_temp_base;
    break;
  default:
    break;
  }

  if (!base) {
     printf("Failed to set up the base for autoconfig PIC %d.\n", ac_z2_current_pic);
    done = 1;
  } else {
    if (address == 0x4a) { // base[19:16]
      *base = (value & 0xf0) << (16 - 4);
    } else if (address == 0x48) { // base[23:20]
      *base &= 0xff0fffff;
      *base |= (value & 0xf0) << (20 - 4);

      if (ac_z2_type[ac_z2_current_pic] == ACTYPE_A314) {
        // a314_set_mem_base_size(*base, cfg->map_size[ac_index[ac_z2_current_pic]]);
      }
      done = 1;
    } else if (address == 0x4c) { // shut up
       printf("Write to Z2 shutup register for PIC %d.\n", ac_z2_current_pic);
      done = 1;
    }
  }

  if (done) {
  switch (ac_z2_type[ac_z2_current_pic]) {
    case ACTYPE_MAPFAST_Z2:
      cfg->map_offset[index] = ac_base[ac_z2_current_pic];
      cfg->map_high[index]   = cfg->map_offset[index] + cfg->map_size[index];

      LOG_INFO("[AUTOCONF] Address of Z2 autoconf RAM assigned to $%.8X\n",
               ac_base[ac_z2_current_pic]);

      m68k_add_ram_range((uint32_t)cfg->map_offset[index],
                         (uint32_t)cfg->map_high[index],
                         cfg->map_data[index]);

      LOG_INFO("[AUTOCONF] Z2 PIC %d at $%.8lX-%.8lX, Size: %d MB\n",
               ac_z2_current_pic,
               cfg->map_offset[index],
               cfg->map_high[index],
               cfg->map_size[index] / SIZE_MEGA);
      break;

    case ACTYPE_PISCSI:
      LOG_INFO("[AUTOCONF] PiSCSI Z2 device assigned to $%.8X\n", *base);

      // When ready to actually map the ROM:
      // m68k_add_rom_range(
      //     *base + (16 * SIZE_KILO),
      //     *base + (32 * SIZE_KILO),
      //     ac_piscsi_rom
      // );
      break;

    case ACTYPE_A314:
      LOG_INFO("[AUTOCONF] A314 emulation device assigned to $%.8X\n", a314_base);
      break;

    case ACTYPE_PISTORM_DEV:
      LOG_INFO("[AUTOCONF] PiStorm Interaction Z2 device assigned to $%.8X\n", *base);

      // Similar pattern once that ROM wants mapping:
      // m68k_add_rom_range(
      //     *base + (16 * SIZE_KILO),
      //     *base + (16 * SIZE_KILO) + sizeof(ac_pistorm_rom),
      //     ac_pistorm_rom
      // );
      break;

    case ACTYPE_ZORRO_GENERIC:
      if (zorro_dev) {
        zorro_dev->base = zorro_temp_base;
        LOG_INFO("[AUTOCONF] Zorro device %s assigned to $%.8X\n",
                 zorro_dev->name ? zorro_dev->name : "unnamed",
                 zorro_dev->base);
      }
      break;



    default:
      LOG_WARN("[AUTOCONF] Unknown Z2 device assigned to $%.8X?\n",
               base ? *base : 0);
      break;
  }

  ac_z2_current_pic++;
  if (ac_z2_current_pic == ac_z2_pic_count) {
    ac_z2_done = 1;
    adjust_ranges_amiga(cfg);
  }
}

}
