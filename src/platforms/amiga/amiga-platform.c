// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "m68k.h"
#include "log.h"
#include "amiga-autoconf.h"
#include "amiga-registers.h"
#include "amiga-interrupts.h"
#include "gpio/ps_protocol.h"
#include "hunk-reloc.h"
#include "net/pi-net-enums.h"
#include "net/pi-net.h"
#include "net64/net64_bus.h"
#include "net64/net64_config.h"
#include "net64/net64_autoconfig.h"
#include "piscsi/piscsi-enums.h"
#include "piscsi/piscsi.h"
#include "piscsi64/piscsi64-api.h"
#include "ahi/pi_ahi.h"
#include "ahi/pi-ahi-enums.h"
#include "pistorm-dev/pistorm-dev-enums.h"
#include "pistorm-dev/pistorm-dev.h"
#include "platforms/platforms.h"
#include "platforms/shared/rtc.h"
#include "pirtg64/pirtg64.h"
#include "amiga-platform.h"
#include "a314/a314.h"
#include "emulator_fc.h"
#include "amiga_zorro.h"
#include "zorro/z3_piscsi64/z3_piscsi64.h"
#include "memory_mapped.h"

#define DEBUG_AMIGA_PLATFORM

#ifdef DEBUG_AMIGA_PLATFORM
#define DEBUG LOG_DEBUG
#else
#define DEBUG(...)
#endif

/* Convert half-open [lo, hi) to inclusive end for printing.
 * Handles hi==0 or hi<=lo safely (prints hi as-is in those edge cases).
 */
#define RANGE_END_INCL(_lo, _hi) \
  ((uint32_t)(((_hi) != 0 && (_hi) > (_lo)) ? ((uint32_t)(_hi) - 1u) : (uint32_t)(_hi)))

#define CUSTOM_RANGE_STEP(TAG, NEW_LO_IN, NEW_HI_IN) do {                     \
  uint32_t _old_lo = (uint32_t)cfg->custom_low;                               \
  uint32_t _old_hi = (uint32_t)cfg->custom_high;                              \
  uint32_t _new_lo = (uint32_t)(NEW_LO_IN);                                   \
  uint32_t _new_hi = (uint32_t)(NEW_HI_IN);                                   \
                                                                               \
  /* cfg->custom_* remain half-open: [low, high) */                            \
  cfg->custom_low  = (_old_lo == 0) ? _new_lo : min(_old_lo, _new_lo);         \
  cfg->custom_high = max(_old_hi, _new_hi);                                   \
                                                                               \
  int mapped = ((uint32_t)cfg->custom_low  != _old_lo) ||                      \
               ((uint32_t)cfg->custom_high != _old_hi);                        \
                                                                               \
  LOG_INFO("[AMIGA][CUSTOM] %-12s changed=%d now=%08X-%08X (was %08X-%08X) "   \
           "add=%08X-%08X\n",                                                  \
           (TAG), mapped,                                                      \
           (uint32_t)cfg->custom_low,  RANGE_END_INCL((uint32_t)cfg->custom_low,  (uint32_t)cfg->custom_high), \
           _old_lo,               RANGE_END_INCL(_old_lo, _old_hi),            \
           _new_lo,               RANGE_END_INCL(_new_lo, _new_hi));           \
} while (0)



int handle_register_read_amiga(unsigned int addr, unsigned char type, unsigned int* val);
int handle_register_write_amiga(unsigned int addr, unsigned int value, unsigned char type);

extern uint32_t ac_z2_current_pic;
extern uint32_t ac_z2_pic_count;

extern int ac_z2_done;
extern int ac_z2_type[AC_PIC_LIMIT];
extern int ac_z2_index[AC_PIC_LIMIT];

extern uint32_t ac_z3_current_pic;
extern uint32_t ac_z3_pic_count;

extern int ac_z3_done;
extern int ac_z3_type[AC_PIC_LIMIT];
extern int ac_z3_index[AC_PIC_LIMIT];
extern unsigned int ac_base[AC_PIC_LIMIT];
extern int nib_latch;

extern uint8_t gayle_emulation_enabled;

const char* z2_autoconf_id = "z2_autoconf_fast";
const char* z2_autoconf_zap_id = "^2_autoconf_fast";
const char* z3_autoconf_id = "z3_autoconf_fast";
const char* z3_autoconf_zap_id = "^3_autoconf_fast";

extern const char* op_type_names[OP_TYPE_NUM];
extern uint8_t cdtv_mode;
extern uint8_t rtc_type;
extern unsigned char cdtv_sram[32 * SIZE_KILO];
extern unsigned int a314_base;

extern int kb_hook_enabled;
extern int mouse_hook_enabled;

extern int swap_df0_with_dfx;
extern int spoof_df0_id;
extern int move_slow_to_chip;
extern int force_move_slow_to_chip;

#define min(a, b) (a < b) ? a : b
#define max(a, b) (a > b) ? a : b

uint8_t rtg_enabled = 0;
uint8_t piscsi_enabled = 0;
uint8_t piscsi64_enabled = 0;
uint8_t pinet_enabled = 0;
uint8_t net64_enabled = 0;
uint8_t kick13_mode = 0;
uint8_t pistorm_dev_enabled = 1;
uint8_t pi_ahi_enabled = 0;
uint8_t physical_z2_first = 0;
uint8_t a314_emulation_enabled = 0;
uint8_t a314_initialized = 0;

extern uint32_t piscsi_base;
extern uint32_t piscsi64_base;
extern uint32_t pistorm_dev_base;
extern uint8_t rtg_dpms;

extern void stop_cpu_emulation(uint8_t disasm_cur);

static uint32_t ac_waiting_for_physical_pic = 0;
static unsigned int autoconf_warn_budget = 16;

static inline const char* autoconf_op_name(unsigned char type) {
  if (type < OP_TYPE_NUM) {
    return op_type_names[type];
  }
  return "UNKNOWN";
}

static inline void autoconf_warn_once(const char* dir, const char* space, unsigned int addr,
                                      unsigned int base, unsigned char req_type,
                                      unsigned char norm_type) {
  if (autoconf_warn_budget == 0) {
    return;
  }
  autoconf_warn_budget--;
  LOG_WARN("[AUTOCONF] Unexpected %s %s req=%s norm=%s bus=$%.8X off=$%.4X\n", dir, space,
           autoconf_op_name(req_type), autoconf_op_name(norm_type), addr, addr - base);
}

static inline unsigned int autoconf_z2_read_width(struct emulator_config* cfg, unsigned int offset,
                                                  unsigned char type) {
  // Z2 autoconfig byte lanes are spaced by 2 bytes on the 16-bit bus.
  if (type == OP_TYPE_BYTE) {
    return autoconfig_read_memory_8(cfg, offset);
  }
  if (type == OP_TYPE_WORD) {
    unsigned int hi = autoconfig_read_memory_8(cfg, offset);
    unsigned int lo = autoconfig_read_memory_8(cfg, offset + 2);
    return ((hi & 0xFF) << 8) | (lo & 0xFF);
  }
  if (type == OP_TYPE_LONGWORD) {
    unsigned int b0 = autoconfig_read_memory_8(cfg, offset);
    unsigned int b1 = autoconfig_read_memory_8(cfg, offset + 2);
    unsigned int b2 = autoconfig_read_memory_8(cfg, offset + 4);
    unsigned int b3 = autoconfig_read_memory_8(cfg, offset + 6);
    return ((b0 & 0xFF) << 24) | ((b1 & 0xFF) << 16) | ((b2 & 0xFF) << 8) | (b3 & 0xFF);
  }
  return 0;
}

static inline void autoconf_z2_write_width(struct emulator_config* cfg, unsigned int offset,
                                           unsigned int value, unsigned char type) {
  if (type == OP_TYPE_BYTE) {
    autoconfig_write_memory_8(cfg, offset, value);
    return;
  }
  if (type == OP_TYPE_WORD) {
    autoconfig_write_memory_8(cfg, offset, (value >> 8) & 0xFF);
    autoconfig_write_memory_8(cfg, offset + 2, value & 0xFF);
    return;
  }
  if (type == OP_TYPE_LONGWORD) {
    autoconfig_write_memory_8(cfg, offset, (value >> 24) & 0xFF);
    autoconfig_write_memory_8(cfg, offset + 2, (value >> 16) & 0xFF);
    autoconfig_write_memory_8(cfg, offset + 4, (value >> 8) & 0xFF);
    autoconfig_write_memory_8(cfg, offset + 6, value & 0xFF);
  }
}

static inline unsigned int autoconf_z3_remap_offset(unsigned int offset) {
  // Z3 config appears in the Z2 aperture with an address swizzle on bit 1.
  return (offset & 0x02) ? ((offset - 2) + 0x100) : offset;
}

static inline unsigned int autoconf_z3_read_width(struct emulator_config* cfg, unsigned int offset,
                                                  unsigned char type) {
  if (type == OP_TYPE_BYTE) {
    return autoconfig_read_memory_z3_8(cfg, offset);
  }
  if (type == OP_TYPE_WORD) {
    unsigned int hi = autoconfig_read_memory_z3_8(cfg, offset);
    unsigned int lo = autoconfig_read_memory_z3_8(cfg, offset + 2);
    return ((hi & 0xFF) << 8) | (lo & 0xFF);
  }
  if (type == OP_TYPE_LONGWORD) {
    unsigned int b0 = autoconfig_read_memory_z3_8(cfg, offset);
    unsigned int b1 = autoconfig_read_memory_z3_8(cfg, offset + 2);
    unsigned int b2 = autoconfig_read_memory_z3_8(cfg, offset + 4);
    unsigned int b3 = autoconfig_read_memory_z3_8(cfg, offset + 6);
    return ((b0 & 0xFF) << 24) | ((b1 & 0xFF) << 16) | ((b2 & 0xFF) << 8) | (b3 & 0xFF);
  }
  return 0;
}

static inline void autoconf_z3_write_width(struct emulator_config* cfg, unsigned int offset,
                                           unsigned int value, unsigned char type) {
  if (type == OP_TYPE_BYTE) {
    autoconfig_write_memory_z3_8(cfg, offset, value);
    return;
  }
  if (type == OP_TYPE_WORD) {
    autoconfig_write_memory_z3_16(cfg, offset, value);
    return;
  }
  if (type == OP_TYPE_LONGWORD) {
    autoconfig_write_memory_z3_16(cfg, offset, (value >> 16) & 0xFFFF);
    autoconfig_write_memory_z3_16(cfg, offset + 4, value & 0xFFFF);
  }
}

static inline unsigned char normalize_autoconf_width(unsigned char type) {
  switch (type) {
  case OP_TYPE_BYTE:
  case OP_TYPE_WORD:
  case OP_TYPE_LONGWORD:
    return type;
  default:
    // JIT can probe with non-core op types; treat as WORD to keep autoconfig progressing.
    return OP_TYPE_WORD;
  }
}

int custom_read_amiga(struct emulator_config* cfg, unsigned int addr, unsigned int* val,
                             unsigned char type) {
  const amiga_zorro_layout_t* zlayout = amiga_get_zorro_layout();
  const uint32_t z2_cfg_base = zlayout ? zlayout->z2_config_base : AC_Z2_BASE;
  const uint32_t z3_cfg_base = zlayout ? zlayout->z3_config_base : AC_Z3_BASE;
  const uint32_t cfg_win_size = zlayout ? zlayout->config_window_size : AC_SIZE;
  unsigned char width = normalize_autoconf_width(type);
  if (kick13_mode) {
    ac_z3_done = 1;
  }
  if ((!ac_z2_done || !ac_z3_done) && addr >= z2_cfg_base && addr < z2_cfg_base + cfg_win_size) {
    if (physical_z2_first) {
      if (addr == z2_cfg_base) {
        uint8_t zchk = (uint8_t)ps_read_8(addr);
        DEBUG("[AUTOCONF] Read from AC_Z2_BASE: %.2X\n", zchk);
        if (((zchk & BOARDTYPE_Z2) == BOARDTYPE_Z2) || ((zchk & BOARDTYPE_Z3) == BOARDTYPE_Z3)) {
          if (!ac_waiting_for_physical_pic) {
            LOG_INFO("[AUTOCONF] Found physical Zorro board, pausing processing until done.\n");
            ac_waiting_for_physical_pic = 1;
          }
          *val = zchk;
          return 1;
        } else {
          if (ac_waiting_for_physical_pic) {
            LOG_INFO("[AUTOCONF] Resuming virtual Zorro board processing.\n");
            ac_waiting_for_physical_pic = 0;
          }
        }
      }
      if (ac_waiting_for_physical_pic) {
        return -1;
      }
    }
    if (!ac_z2_done && ac_z2_current_pic < ac_z2_pic_count) {
      *val = autoconf_z2_read_width(cfg, addr - z2_cfg_base, width);
      return 1;
    }
    if (!ac_z3_done && ac_z3_current_pic < ac_z3_pic_count) {
      uint32_t addr_ = autoconf_z3_remap_offset(addr - z2_cfg_base);
      *val = autoconf_z3_read_width(cfg, addr_, width);
      return 1;
    }
  }
  if (!ac_z3_done && addr >= z3_cfg_base && addr < z3_cfg_base + cfg_win_size) {
    if (ac_z3_pic_count == 0) {
      ac_z3_done = 1;
      return -1;
    }

    if (width == OP_TYPE_BYTE || width == OP_TYPE_WORD || width == OP_TYPE_LONGWORD) {
      *val = autoconf_z3_read_width(cfg, addr - z3_cfg_base, width);
      return 1;
    }
    autoconf_warn_once("read", "Z3", addr, z3_cfg_base, type, width);
    return -1;
  }

  if (pistorm_dev_enabled && addr >= pistorm_dev_base &&
      addr < pistorm_dev_base + (64 * SIZE_KILO)) {
    *val = handle_pistorm_dev_read(addr, type);
    return 1;
  }

  if (piscsi_enabled && addr >= piscsi_base && addr < piscsi_base + (64 * SIZE_KILO)) {
    // printf("[Amiga-Custom] %s read from PISCSI base @$%.8X.\n", op_type_names[type], addr);
    // stop_cpu_emulation(1);
    *val = handle_piscsi_read(addr, type);
    return 1;
  }

  if (piscsi64_enabled && piscsi64_base &&
      addr >= piscsi64_base && addr < piscsi64_base + (64 * SIZE_KILO)) {
    *val = handle_piscsi64_read(addr, type);
    return 1;
  }

  if (zorro_handle_read(addr, type, val) == 1) {
    return 1;
  }

  if (a314_emulation_enabled && addr >= a314_base && addr < a314_base + (64 * SIZE_KILO)) {
    //printf("%s read from A314 @$%.8X\n", op_type_names[type], addr);
    switch (type) {
    case OP_TYPE_BYTE:
      *val = a314_read_memory_8(addr - a314_base);
      return 1;
      break;
    case OP_TYPE_WORD:
      *val = a314_read_memory_16(addr - a314_base);
      return 1;
      break;
    case OP_TYPE_LONGWORD:
      *val = a314_read_memory_32(addr - a314_base);
      return 1;
      break;
    default:
      break;
    }
    return 1;
  }

  return -1;
}

int custom_write_amiga(struct emulator_config* cfg, unsigned int addr, unsigned int val,
                              unsigned char type) {
  const amiga_zorro_layout_t* zlayout = amiga_get_zorro_layout();
  const uint32_t z2_cfg_base = zlayout ? zlayout->z2_config_base : AC_Z2_BASE;
  const uint32_t z3_cfg_base = zlayout ? zlayout->z3_config_base : AC_Z3_BASE;
  const uint32_t cfg_win_size = zlayout ? zlayout->config_window_size : AC_SIZE;
  unsigned char width = normalize_autoconf_width(type);
  if (kick13_mode) {
    ac_z3_done = 1;
  }
  if ((!ac_z2_done || !ac_z3_done) && addr >= z2_cfg_base && addr < z2_cfg_base + cfg_win_size) {
    if (physical_z2_first && ac_waiting_for_physical_pic) {
      return -1;
    }
    if (!ac_z2_done && ac_z2_current_pic < ac_z2_pic_count) {
      autoconf_z2_write_width(cfg, addr - z2_cfg_base, val, width);
      return 1;
    }
    if (!ac_z3_done && ac_z3_current_pic < ac_z3_pic_count) {
      uint32_t addr_ = autoconf_z3_remap_offset(addr - z2_cfg_base);
      autoconf_z3_write_width(cfg, addr_, val, width);
      return 1;
    }
  }

  if (!ac_z3_done && addr >= z3_cfg_base && addr < z3_cfg_base + cfg_win_size) {
    if (width == OP_TYPE_BYTE || width == OP_TYPE_WORD || width == OP_TYPE_LONGWORD) {
      if (ac_z3_pic_count == 0) {
        ac_z3_done = 1;
        return -1;
      }
      autoconf_z3_write_width(cfg, addr - z3_cfg_base, val, width);
      return 1;
    }
    autoconf_warn_once("write", "Z3", addr, z3_cfg_base, type, width);
  }

  if (pistorm_dev_enabled && addr >= pistorm_dev_base &&
      addr < pistorm_dev_base + (64 * SIZE_KILO)) {
    handle_pistorm_dev_write(addr, val, type);
    return 1;
  }

  if (piscsi_enabled && addr >= piscsi_base && addr < piscsi_base + (64 * SIZE_KILO)) {
    printf("[Amiga-Custom] %s write to PISCSI base @$%.8x: %.8X\n", op_type_names[type], addr, val);
    handle_piscsi_write(addr, val, type);
    return 1;
  }

  if (piscsi64_enabled && piscsi64_base &&
      addr >= piscsi64_base && addr < piscsi64_base + (64 * SIZE_KILO)) {
    handle_piscsi64_write(addr, val, type);
    return 1;
  }

  if (zorro_handle_write(addr, type, val) == 1) {
    return 1;
  }

  if (a314_emulation_enabled &&
    addr >= a314_base &&
    addr <  a314_base + (64 * SIZE_KILO)) {

    printf("%s write to A314 @$%.8X: %d\n", op_type_names[type], addr, val);

    unsigned int off = (unsigned int)(addr - a314_base);

    switch (type) {
      case OP_TYPE_BYTE:
        a314_write_memory_8(off, (unsigned int)val);
        return 1;

      case OP_TYPE_WORD:
        a314_write_memory_16(off, (unsigned int)val);
        return 1;

      case OP_TYPE_LONGWORD:
        a314_write_memory_32(off, (unsigned int)val);
        return 1;

      default:
        break;
    }
}


  return -1;
}



void adjust_ranges_amiga(struct emulator_config* cfg) {
  const amiga_zorro_layout_t* zlayout = amiga_get_zorro_layout();
  const uint32_t z2_cfg_base = zlayout ? zlayout->z2_config_base : AC_Z2_BASE;
  const uint32_t z3_cfg_base = zlayout ? zlayout->z3_config_base : AC_Z3_BASE;
  const uint32_t cfg_win_size = zlayout ? zlayout->config_window_size : AC_SIZE;

  cfg->mapped_high = 0;
  cfg->mapped_low = 0;
  cfg->custom_high = 0;
  cfg->custom_low = 0;
  int have_mapped_range = 0;

  // Set up the min/max ranges for mapped reads/writes (map_high is treated as EXCLUSIVE)
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE)
      continue;

    uint32_t off      = (uint32_t)cfg->map_offset[i];
    uint32_t sz       = (uint32_t)cfg->map_size[i];
    uint32_t end_excl = off + sz;                 // exclusive end
    uint32_t end_incl = (sz ? (end_excl - 1) : off); // inclusive end for logging only

    uint32_t old_lo      = cfg->mapped_low;
    uint32_t old_hi_excl = cfg->mapped_high;
    uint32_t old_hi_incl = (old_hi_excl ? (old_hi_excl - 1) : 0);

    int lo_changed = 0, hi_changed = 0;

    if (!have_mapped_range || off < cfg->mapped_low) {
      cfg->mapped_low = off;
      lo_changed = 1;
    }
    if (!have_mapped_range || end_excl > cfg->mapped_high) {
      cfg->mapped_high = end_excl;  // keep exclusive internally
      hi_changed = 1;
    }

    have_mapped_range = 1;

    uint32_t now_hi_incl = (cfg->mapped_high ? (cfg->mapped_high - 1) : 0);
    int mapped = lo_changed || hi_changed;

    mem_map_entry_info_t map_info;
    int have_info = (memmap_lookup(cfg, off, &map_info) >= 0);
    LOG_INFO(
      "[AMIGA][MAP] i=%02d type=%d amiga_base=%08X size=%08X amiga_end=%08X host=%p "
      "kind=%s cacheable=%u executable=%u mapped=%d lochg=%d hichg=%d "
      "now=%08X-%08X (was %08X-%08X)\n",
      i, cfg->map_type[i],
      off, sz, end_incl, cfg->map_data[i],
      have_info ? memmap_kind_name(map_info.kind) : "none",
      have_info ? (unsigned int)map_info.cacheable : 0u,
      have_info ? (unsigned int)map_info.executable : 0u,
      mapped, lo_changed, hi_changed,
      cfg->mapped_low, now_hi_incl,
      old_lo, old_hi_incl
    );
  }

  if (rtg_enabled)    CUSTOM_RANGE_STEP("rtg",   PIRTG64_BASE, PIRTG64_UPPER);
  if (piscsi_enabled) {
    uint32_t pbase = piscsi_base ? piscsi_base : PISCSI_OFFSET;
    CUSTOM_RANGE_STEP("piscsi", pbase, pbase + PISCSI_REGSIZE);
  }
  if (piscsi64_enabled && piscsi64_base) {
    CUSTOM_RANGE_STEP("piscsi64", piscsi64_base, piscsi64_base + PISCSI64_REGSIZE);
  }
  if (pinet_enabled)  CUSTOM_RANGE_STEP("pinet", PINET_OFFSET, PINET_UPPER);
  if (pi_ahi_enabled) CUSTOM_RANGE_STEP("ahi",   PI_AHI_OFFSET, PI_AHI_UPPER);

  if (zorro_get_device_count() > 0) {
    CUSTOM_RANGE_STEP("zorro", z2_cfg_base, z2_cfg_base + cfg_win_size);
  }

  for (uint8_t i = 0; i < zorro_get_device_count(); i++) {
    zorro_device_t *dev = zorro_get_device_by_index(i);
    if (dev == NULL) {
      continue;
    }
    if (dev->base == 0 || dev->size == 0) {
      continue;
    }
    CUSTOM_RANGE_STEP(dev->name ? dev->name : "zdev", dev->base, dev->base + dev->size);
  }

  if (ac_z2_pic_count && !ac_z2_done) CUSTOM_RANGE_STEP("ac_z2", z2_cfg_base, z2_cfg_base + cfg_win_size);
  if (ac_z3_pic_count && !ac_z3_done) CUSTOM_RANGE_STEP("ac_z3", z3_cfg_base, z3_cfg_base + cfg_win_size);

}



int setup_platform_amiga(struct emulator_config* cfg) {
  LOG_INFO("[AMIGA] Performing setup for Amiga platform.\n");
  {
    const amiga_zorro_layout_t* zlayout = amiga_get_zorro_layout();
    if (zlayout) {
      LOG_INFO("[AMIGA][ZORRO] Z2 config=$%.8X size=$%.8X Z2 mem=$%.8X-$%.8X\n",
               zlayout->z2_config_base, zlayout->config_window_size,
               zlayout->z2_mem_base, zlayout->z2_mem_base + zlayout->z2_mem_size - 1u);
      LOG_INFO("[AMIGA][ZORRO] Z3 config=$%.8X size=$%.8X Z3 mem=$%.8X-$%.8X\n",
               zlayout->z3_config_base, zlayout->config_window_size,
               zlayout->z3_mem_base, zlayout->z3_mem_base + zlayout->z3_mem_size - 1u);
    }
  }

  /* --------------------------------------------------------------------
   * Subsystem handling (board “personality”)
   * ------------------------------------------------------------------ */
  if (cfg->platform && cfg->platform->subsys && strlen(cfg->platform->subsys)) {
    const char *sub = cfg->platform->subsys;
    LOG_INFO("[AMIGA] Subsystem is [%s]\n", sub);

    /* Big-box / tower machines using the A3000/A4000-style Gayle quirks */
    if (!strcmp(sub, "3000") || !strcmp(sub, "3000T") ||
        !strcmp(sub, "4000") || !strcmp(sub, "4000T")) {
      LOG_INFO("[AMIGA] Adjusting Gayle accesses for A3000/4000 Kickstart.\n");
      adjust_gayle_4000();

    /* Compact Gayle-based machines */
    } else if (!strcmp(sub, "600")  ||
               !strcmp(sub, "1200") ||
               !strcmp(sub, "cd32")) {
      LOG_INFO("[AMIGA] Adjusting Gayle accesses for A600/A1200/CD32 Kickstart.\n");
      adjust_gayle_1200();

    /* CDTV personality (emulated on 500/2000) */
    } else if (!strcmp(sub, "cdtv")) {
      LOG_INFO("[AMIGA] Configuring platform for CDTV emulation.\n");
      cdtv_mode = 1;
      rtc_type  = RTC_TYPE_MSM;

    } else {
      /* Known-to-us, but no special handling yet: 500, 500+, 1000, 1500, 2000, 2500, etc. */
      LOG_INFO("[AMIGA] Subsystem [%s] has no special Gayle/RTC handling.\n", sub);
    }
  } else {
    LOG_INFO("[AMIGA] No sub system specified.\n");
  }

  /* --------------------------------------------------------------------
   * Z2 AutoConfig Fast RAM
   * ------------------------------------------------------------------ */
  int index;
  int z2_found = 0;

  while ((index = get_named_mapped_item(cfg, z2_autoconf_id)) != -1) {
    /* "Zap" config items as they are processed so they are not found again */
    cfg->map_id[index][0] = '^';

    unsigned int resize_data = 0;

    if (cfg->map_size[index] > 8 * SIZE_MEGA) {
      LOG_WARN("[AMIGA] Attempted to configure more than 8MB of Z2 Fast RAM, downsizing to 8MB.\n");
      resize_data = 8 * SIZE_MEGA;
    } else if (cfg->map_size[index] != 2 * SIZE_MEGA &&
               cfg->map_size[index] != 4 * SIZE_MEGA &&
               cfg->map_size[index] != 8 * SIZE_MEGA) {
      if (cfg->map_size[index] > 8 * SIZE_MEGA) {
        resize_data = 8 * SIZE_MEGA;
      } else if (cfg->map_size[index] > 4 * SIZE_MEGA) {
        resize_data = 4 * SIZE_MEGA;
      } else {
        resize_data = 2 * SIZE_MEGA;
      }
      LOG_WARN("[AMIGA] Z2 Fast RAM may only provision 2, 4 or 8MB of memory, resizing to %dMB.\n",
               resize_data / SIZE_MEGA);
    }

    if (resize_data) {
      cfg_release_map_data(cfg, index);
      cfg->map_size[index] = (unsigned int)resize_data;
      unsigned char alloc_kind = MAPALLOC_NONE;
      unsigned char* map_ptr = cfg_alloc_mapped_data(cfg->map_size[index], 1, &alloc_kind, z2_autoconf_id);
      cfg_set_map_data_allocation(cfg, index, map_ptr, cfg->map_size[index], alloc_kind);
      if (!cfg->map_data[index]) {
        LOG_ERROR("[AMIGA] Failed to reallocate Z2 Fast RAM map[%d].\n", index);
        return -1;
      }
    }

    LOG_INFO("[AMIGA] %dMB of Z2 Fast RAM configured at $%lx\n",
             cfg->map_size[index] / SIZE_MEGA,
             cfg->map_offset[index]);

    add_z2_pic(ACTYPE_MAPFAST_Z2, (uint8_t)(index & 0xFF));
    z2_found = 1;
  }

  if (!z2_found) {
    LOG_INFO("[AMIGA] No Z2 Fast RAM configured.\n");
  }

  /* Restore any "zapped" Z2 autoconf items so they can be reinitialized later */
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_id[i] && strcmp(cfg->map_id[i], z2_autoconf_zap_id) == 0) {
      cfg->map_id[i][0] = z2_autoconf_id[0];
    }
  }

  /* --------------------------------------------------------------------
   * Z3 AutoConfig Fast RAM
   * ------------------------------------------------------------------ */
  int z3_found = 0;

  while ((index = get_named_mapped_item(cfg, z3_autoconf_id)) != -1) {
    cfg->map_id[index][0] = '^';

    LOG_INFO("[AMIGA] %dMB of Z3 Fast RAM configured at $%lx\n",
             cfg->map_size[index] / SIZE_MEGA,
             cfg->map_offset[index]);

    ac_z3_type[ac_z3_pic_count]  = ACTYPE_MAPFAST_Z3;
    ac_z3_index[ac_z3_pic_count] = index;
    ac_z3_pic_count++;

    z3_found = 1;
  }

  if (!z3_found) {
    LOG_INFO("[AMIGA] No Z3 Fast RAM configured.\n");
  }

  /* Restore any "zapped" Z3 autoconf items */
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_id[i] && strcmp(cfg->map_id[i], z3_autoconf_zap_id) == 0) {
      cfg->map_id[i][0] = z3_autoconf_id[0];
    }
  }

  /* --------------------------------------------------------------------
   * CPU slot RAM (if present)
   * ------------------------------------------------------------------ */
  index = get_named_mapped_item(cfg, "cpu_slot_ram");
  if (index != -1) {
    m68k_add_ram_range((uint32_t)cfg->map_offset[index],
                       (uint32_t)cfg->map_high[index],
                       cfg->map_data[index]);
  }

  /* --------------------------------------------------------------------
   * Final range bookkeeping + CDTV SRAM + PiStorm dev
   * ------------------------------------------------------------------ */
  adjust_ranges_amiga(cfg);

  if (cdtv_mode) {
    FILE *in = fopen("data/cdtv.sram", "rb");
    if (in != NULL) {
      LOG_INFO("[AMIGA] Loaded CDTV SRAM.\n");
      fread(cdtv_sram, 32 * SIZE_KILO, 1, in);
      fclose(in);
    }
  }

  if (pistorm_dev_enabled) {
    add_z2_pic(ACTYPE_PISTORM_DEV, (uint8_t)0);
  }

  return 0;
}

#define CHKVAR(a) (strcmp(var, a) == 0)

static void set_env_if_value(const char *key, const char *val) {
  if (!val || strlen(val) == 0) {
    return;
  }
  setenv(key, val, 1);
}

static void set_env_if_unset(const char *key, const char *val) {
  if (!val || strlen(val) == 0) {
    return;
  }
  if (getenv(key) == NULL) {
    setenv(key, val, 1);
  }
}

static int parse_bool_flag(const char *val, int default_on) {
  if (!val || strlen(val) == 0) {
    return default_on;
  }
  if (strcasecmp(val, "1") == 0 || strcasecmp(val, "true") == 0 ||
      strcasecmp(val, "yes") == 0 || strcasecmp(val, "on") == 0) {
    return 1;
  }
  if (strcasecmp(val, "0") == 0 || strcasecmp(val, "false") == 0 ||
      strcasecmp(val, "no") == 0 || strcasecmp(val, "off") == 0) {
    return 0;
  }
  return -1;
}

void setvar_amiga(struct emulator_config* cfg, const char* var, const char* val) {
  if (!var) {
    return;
  }

  if CHKVAR ("pistorm_root") {
    set_env_if_value("PISTORM_ROOT", val);
    if (val && strlen(val) != 0) {
      char path_buf[PATH_MAX];
      snprintf(path_buf, sizeof(path_buf), "%s/a314", val);
      set_env_if_unset("PISTORM_A314", path_buf);
      snprintf(path_buf, sizeof(path_buf), "%s/data", val);
      set_env_if_unset("PISTORM_DATA", path_buf);
      snprintf(path_buf, sizeof(path_buf), "%s/data/a314-shared", val);
      set_env_if_unset("A314_SHARED", path_buf);
      snprintf(path_buf, sizeof(path_buf), "%s/a314/a314d.conf", val);
      set_env_if_unset("A314_CONF", path_buf);
      snprintf(path_buf, sizeof(path_buf), "%s/a314/a314fs.conf", val);
      set_env_if_unset("A314_FS_CONF", path_buf);
    }
  }
  if CHKVAR ("pistorm_a314") {
    set_env_if_value("PISTORM_A314", val);
    if (val && strlen(val) != 0) {
      char path_buf[PATH_MAX];
      snprintf(path_buf, sizeof(path_buf), "%s/a314d.conf", val);
      set_env_if_unset("A314_CONF", path_buf);
      snprintf(path_buf, sizeof(path_buf), "%s/a314fs.conf", val);
      set_env_if_unset("A314_FS_CONF", path_buf);
    }
  }
  if CHKVAR ("pistorm_data") {
    set_env_if_value("PISTORM_DATA", val);
    if (val && strlen(val) != 0) {
      char path_buf[PATH_MAX];
      snprintf(path_buf, sizeof(path_buf), "%s/a314-shared", val);
      set_env_if_unset("A314_SHARED", path_buf);
    }
  }
  if CHKVAR ("a314_shared") {
    set_env_if_value("A314_SHARED", val);
  }
  if CHKVAR ("a314_fs_conf") {
    set_env_if_value("A314_FS_CONF", val);
  }
  if CHKVAR ("queue") {
    int enabled = parse_bool_flag(val, 1);
    if (enabled >= 0) {
      setenv("PISTORM_ENABLE_QUEUE", enabled ? "1" : "0", 1);
      LOG_INFO("[AMIGA] Queue %s via setvar.\n", enabled ? "enabled" : "disabled");
    } else {
      LOG_WARN("[AMIGA] Invalid setvar queue value: %s\n", val ? val : "(null)");
    }
  }
  if CHKVAR ("batch_bits") {
    if (!val || strlen(val) == 0 || strcasecmp(val, "true") == 0) {
      setenv("PISTORM_BATCH_BITS", "2048", 1);
      LOG_INFO("[AMIGA] Batch bits set to 2048 via setvar.\n");
    } else if (strcasecmp(val, "false") == 0 || strcmp(val, "0") == 0) {
      setenv("PISTORM_BATCH_BITS", "0", 1);
      LOG_INFO("[AMIGA] Batch bits disabled via setvar.\n");
    } else {
      setenv("PISTORM_BATCH_BITS", val, 1);
      LOG_INFO("[AMIGA] Batch bits set to %s via setvar.\n", val);
    }
  }
  if CHKVAR ("enable_rtc_emulation") {
    unsigned int rtc_enabled = 0;
    if (!val || strlen(val) == 0) {
      rtc_enabled = 1;
    } else {
      rtc_enabled = get_int(val);
    }
    if (rtc_enabled != (unsigned int)-1) {
      configure_rtc_emulation_amiga((uint8_t)rtc_enabled);
    }
  }
  if CHKVAR ("hdd0") {
    if (val && strlen(val) != 0) {
      set_hard_drive_image_file_amiga(0, val);
    }
  }
  if CHKVAR ("hdd1") {
    if (val && strlen(val) != 0) {
      set_hard_drive_image_file_amiga(1, val);
    }
  }
  if CHKVAR ("cdtv") {
    LOG_INFO("[AMIGA] CDTV mode enabled.\n");
    cdtv_mode = 1;
  }
  if (CHKVAR("rtg") && !rtg_enabled) {
    if (init_rtg_data(cfg)) {
      LOG_INFO("[AMIGA] RTG Enabled.\n");
      rtg_enabled = 1;
      adjust_ranges_amiga(cfg);
    } else {
      LOG_WARN("[AMIGA] Failed to enable RTG.\n");
    }
  }
  if (CHKVAR("rtg-dpms")) {
    rtg_dpms = 1;
    LOG_INFO("[AMIGA] DPMS enabled for RTG.\n");
  }
  if (CHKVAR("rtg-width")) {
    if (val && strlen(val) != 0) {
      uint32_t rtg_width = get_int(val);
      rtg_set_screen_width(rtg_width);
      LOG_INFO("[AMIGA] RTG screen width set to %d.\n", rtg_width);
    }
  }
  if (CHKVAR("rtg-height")) {
    if (val && strlen(val) != 0) {
      uint32_t rtg_height = get_int(val);
      rtg_set_screen_height(rtg_height);
      LOG_INFO("[AMIGA] RTG screen height set to %d.\n", rtg_height);
    }
  }
  if CHKVAR ("kick13") {
    LOG_INFO("[AMIGA] Kickstart 1.3 mode enabled, Z3 PICs will not be enumerated.\n");
    kick13_mode = 1;
  }
  if CHKVAR ("physical-z2-first") {
    LOG_INFO("[AMIGA] Explicitly initializing physical Z2 devices before virtual ones.\n");
    physical_z2_first = 1;
  }
  if CHKVAR ("a314") {
    if (!a314_initialized) {
      int32_t res = a314_init();
      if (res != 0) {
        LOG_WARN("[AMIGA] Failed to enable A314 emulation, error return code: %d.\n", res);
      } else {
        LOG_INFO("[AMIGA] A314 emulation enabled.\n");
        add_z2_pic(ACTYPE_A314, (uint8_t)0);
        a314_emulation_enabled = 1;
        a314_initialized = 1;
      }
    } else {
      add_z2_pic(ACTYPE_A314, (uint8_t)0);
      a314_emulation_enabled = 1;
    }
  }
  if CHKVAR ("a314_conf") {
    if (val && strlen(val) != 0) {
      a314_set_config_file(val);
    }
  }

  if (CHKVAR("net64") || strncmp(var, "net64_", 6) == 0) {
    (void)net64_config_setvar(var, val);
  }

  zorro_setvar(cfg, var, val);

  if (CHKVAR("net64") && !net64_enabled) {
    if (net64_init() == 0) {
      LOG_INFO("[AMIGA] net64 interface enabled (manuf=$%04X product=$%04X).\n",
               PISTORM_MANUF_ID, PISTORM_PROD_NET64_Z2);
      net64_register();
      net64_enabled = 1;
      adjust_ranges_amiga(cfg);
    } else {
      LOG_WARN("[AMIGA] net64 initialization failed.\n");
    }
  }

  // PiSCSI stuff
  if (CHKVAR("piscsi") && !piscsi_enabled) {
    LOG_INFO("[AMIGA] PISCSI Interface Enabled.\n");
    piscsi_enabled = 1;
    piscsi_init();
    add_z2_pic(ACTYPE_PISCSI, (uint8_t)0);
    adjust_ranges_amiga(cfg);
  }
  if (piscsi_enabled) {
    if CHKVAR ("piscsi0") {
      piscsi_map_drive(val, 0);
    }
    if CHKVAR ("piscsi1") {
      piscsi_map_drive(val, 1);
    }
    if CHKVAR ("piscsi2") {
      piscsi_map_drive(val, 2);
    }
    if CHKVAR ("piscsi3") {
      piscsi_map_drive(val, 3);
    }
    if CHKVAR ("piscsi4") {
      piscsi_map_drive(val, 4);
    }
    if CHKVAR ("piscsi5") {
      piscsi_map_drive(val, 5);
    }
    if CHKVAR ("piscsi6") {
      piscsi_map_drive(val, 6);
    }
  }

  // PiSCSI64 stuff (Z2 virtual HBA)
  if (CHKVAR("piscsi64") && !piscsi64_enabled) {
    LOG_INFO("[AMIGA] PISCSI64 Interface Enabled (manuf=$%04X product=$%04X).\n",
             PISTORM_MANUF_ID, PISTORM_PROD_PISCSI64_Z2);
    piscsi64_enabled = 1;
    piscsi64_init();
    z3_piscsi64_register();
    adjust_ranges_amiga(cfg);
  }
  if (piscsi64_enabled && strncmp(var, "piscsi64_", 9) == 0) {
    if (strcmp(var, "piscsi64_cdrom") == 0) {
      if (val && strlen(val) != 0) {
        int idx = piscsi64_find_free_unit();
        if (idx < 0) {
          LOG_WARN("[AMIGA] No free PiSCSI64 unit available for %s\n", val);
        } else {
          char spec[PATH_MAX + 8];
          if (strncmp(val, "disk:", 5) == 0 ||
              strncmp(val, "cdrom:", 6) == 0 ||
              strncmp(val, "file:", 5) == 0) {
            snprintf(spec, sizeof(spec), "%s", val);
          } else {
            snprintf(spec, sizeof(spec), "cdrom:%s", val);
          }
          piscsi64_map_drive(spec, (uint8_t)idx);
          LOG_INFO("[AMIGA] PiSCSI64 CD-ROM mapped to unit %d: %s\n", idx, val);
        }
      }
      return;
    }

    char *end = NULL;
    long idx = strtol(var + 9, &end, 10);
    if (end && *end == '\0' && idx > 0 && idx < PISCSI64_NUM_UNITS) {
      if (val && strlen(val) != 0) {
        piscsi64_map_drive(val, (uint8_t)idx);
      }
    } else if (end && *end == '\0' && idx == 0) {
      LOG_INFO("[AMIGA] PiSCSI64 unit 0 is reserved for controller identity.\n");
    } else {
      LOG_WARN("[AMIGA] Invalid PiSCSI64 unit selector '%s' (expected piscsi64_0..piscsi64_15)\n", var);
    }
  }

  // Pi-Net stuff
  if (CHKVAR("pi-net") && !pinet_enabled) {
    LOG_INFO("[AMIGA] PI-NET Interface Enabled.\n");
    pinet_enabled = 1;
    pinet_init(val);
    adjust_ranges_amiga(cfg);
  }

  // Pi-AHI stuff
  if (CHKVAR("pi-ahi") && !pi_ahi_enabled) {
    LOG_INFO("[AMIGA] PI-AHI Audio Card Enabled.\n");
    uint32_t res = 1;
    if (val && strlen(val) != 0) {
      res = pi_ahi_init(val);
    } else {
      res = pi_ahi_init("plughw:1,0");
    }
    if (res == 0) {
      pi_ahi_enabled = 1;
      adjust_ranges_amiga(cfg);
    } else {
      LOG_WARN("[AMIGA] Failed to enable PI-AHI.\n");
    }
  }
  if (CHKVAR("pi-ahi-samplerate")) {
    if (val && strlen(val) != 0) {
      pi_ahi_set_playback_rate(get_int(val));
    }
  }

  if CHKVAR ("no-pistorm-dev") {
    pistorm_dev_enabled = 0;
    LOG_INFO("[AMIGA] Disabling PiStorm interaction device.\n");
  }

  // RTC stuff
  if CHKVAR ("rtc_type") {
    if (val && strlen(val) != 0) {
      if (strcmp(val, "msm") == 0) {
        LOG_INFO("[AMIGA] RTC type set to MSM.\n");
        rtc_type = RTC_TYPE_MSM;
      } else {
        LOG_INFO("[AMIGA] RTC type set to Ricoh.\n");
        rtc_type = RTC_TYPE_RICOH;
      }
    }
  }

  if CHKVAR ("swap-df0-df") {
    if (val && strlen(val) != 0 && get_int(val) >= 1 && get_int(val) <= 3) {
      swap_df0_with_dfx = (int)get_int(val);
      LOG_INFO("[AMIGA] DF0 and DF%d swapped.\n", swap_df0_with_dfx);
    }
  }

  if CHKVAR ("move-slow-to-chip") {
    move_slow_to_chip = 1;
    LOG_INFO("[AMIGA] Slow ram moved to Chip.\n");
  }

  if CHKVAR ("enable_fc") {
    if (!val || strlen(val) == 0 || strcmp(val, "stub") == 0) {
      fc_set_mode(FC_MODE_STUB);
    } else if (strcmp(val, "cpld") == 0) {
      fc_set_mode(FC_MODE_CPLD);
      LOG_WARN("[FC] CPLD mode selected; requires FC-enabled CPLD bitstream.\n");
    } else if (strcmp(val, "off") == 0) {
      fc_set_mode(FC_MODE_OFF);
    } else {
      LOG_WARN("[FC] Unknown enable_fc value '%s' (use off|stub|cpld)\n", val);
      fc_set_mode(FC_MODE_OFF);
    }
  }

  if CHKVAR ("force-move-slow-to-chip") {
    force_move_slow_to_chip = 1;
    LOG_INFO("[AMIGA] Forcing slowram move to chip, bypassing Agnus version check.\n");
  }
}

void handle_reset_amiga(struct emulator_config* cfg) {
  ac_z2_done = (ac_z2_pic_count == 0);
  ac_z3_done = (ac_z3_pic_count == 0);
  ac_z2_current_pic = 0;
  ac_z3_current_pic = 0;
  ac_waiting_for_physical_pic = 0;
  nib_latch = 0;
  for (int i = 0; i < AC_PIC_LIMIT; i++) {
    ac_base[i] = 0;
  }
  piscsi_base = 0;
  piscsi64_base = 0;
  pistorm_dev_base = 0;

  spoof_df0_id = 0;

  DEBUG("[AMIGA] Reset handler.\n");
  DEBUG("[AMIGA] AC done - Z2: %d Z3: %d.\n", ac_z2_done, ac_z3_done);

  if (piscsi_enabled) {
    piscsi_refresh_drives();
  }
  if (piscsi64_enabled) {
    piscsi64_refresh_drives();
  }
  if (net64_enabled) {
    net64_handle_reset();
  }

  if (move_slow_to_chip && !force_move_slow_to_chip) {
    ps_write_16(VPOSW, 0x00); // Poke poke... wake up Agnus!
    int agnus_rev = ((ps_read_16(VPOSR) >> 8) & 0x6F);
    if (agnus_rev != 0x20) {
      move_slow_to_chip = 0;
      LOG_WARN("[AMIGA] Requested move slow ram to chip but 8372 Agnus not found - Disabling.\n");
    }
  }

  amiga_clear_emulating_irq();
  adjust_ranges_amiga(cfg);
}

void shutdown_platform_amiga(struct emulator_config* cfg) {
  LOG_INFO("[AMIGA] Performing Amiga platform shutdown.\n");
  if (cfg) {
    // do nothing 
  }
  if (cdtv_mode) {
    FILE* out = fopen("data/cdtv.sram", "wb+");
    if (out != NULL) {
      LOG_INFO("[AMIGA] Saving CDTV SRAM.\n");
      fwrite(cdtv_sram, 32 * SIZE_KILO, 1, out);
      fclose(out);
    } else {
      LOG_WARN("[AMIGA] Failed to write CDTV SRAM to disk.\n");
    }
  }
  if (cfg->platform->subsys) {
    free(cfg->platform->subsys);
    cfg->platform->subsys = NULL;
  }
  if (piscsi_enabled) {
    piscsi_shutdown();
    piscsi_enabled = 0;
  }
  if (piscsi64_enabled) {
    piscsi64_shutdown();
    piscsi64_enabled = 0;
  }
  if (rtg_enabled) {
    shutdown_rtg();
    rtg_enabled = 0;
  }
  if (pinet_enabled) {
    pinet_enabled = 0;
  }
  if (net64_enabled) {
    net64_shutdown();
    net64_enabled = 0;
  }
  if (pi_ahi_enabled) {
    pi_ahi_shutdown();
    pi_ahi_enabled = 0;
  }
  if (a314_emulation_enabled) {
    a314_emulation_enabled = 0;
  }

  mouse_hook_enabled = 0;
  kb_hook_enabled = 0;

  kick13_mode = 0;
  cdtv_mode = 0;

  swap_df0_with_dfx = 0;
  spoof_df0_id = 0;
  move_slow_to_chip = 0;
  force_move_slow_to_chip = 0;
  physical_z2_first = 0;
  ac_waiting_for_physical_pic = 0;

  autoconfig_reset_all();
  LOG_INFO("[AMIGA] Platform shutdown completed.\n");
}

void create_platform_amiga(struct platform_config* cfg, const char* subsys) {
  cfg->register_read = handle_register_read_amiga;
  cfg->register_write = handle_register_write_amiga;
  cfg->custom_read = custom_read_amiga;
  cfg->custom_write = custom_write_amiga;
  cfg->platform_initial_setup = setup_platform_amiga;
  cfg->handle_reset = handle_reset_amiga;
  cfg->shutdown = shutdown_platform_amiga;

  cfg->setvar = setvar_amiga;
  cfg->id = PLATFORM_AMIGA;

  if (subsys) {
    cfg->subsys = malloc(strlen(subsys) + 1);
    strcpy(cfg->subsys, subsys);
    for (unsigned int i = 0; i < strlen(cfg->subsys); i++) {
      cfg->subsys[i] = (char)tolower((int)(unsigned char)cfg->subsys[i]);
    }
  }
}
