// SPDX-License-Identifier: MIT

#include "pistorm_uae_bridge.h"

#include "uae/types.h"
#include "options.h"
#include "sysconfig.h"
#include "sysdeps.h"
#include "custom.h"
#include "memory.h"
#include "newcpu.h"
#include "events.h"

#include "../emulator_fc.h"
#include "../config_file/config_file.h"
#include "../log.h"
#include "../memory_mapped.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
unsigned int m68k_read_memory_8(unsigned int address);
unsigned int m68k_read_memory_16(unsigned int address);
unsigned int m68k_read_memory_32(unsigned int address);
void m68k_write_memory_8(unsigned int address, unsigned int value);
void m68k_write_memory_16(unsigned int address, unsigned int value);
void m68k_write_memory_32(unsigned int address, unsigned int value);
}

struct uae_prefs currprefs, changed_prefs;
int read_irq = 0;
extern struct emulator_config* cfg;
extern int ovl;

extern void fill_prefetch_quick(void);
extern void m68k_reset_newcpu(bool hardreset);
extern void build_cpufunctbl(void);
extern "C" void cpu_set_fc(uint32_t fc);
extern addrbank* thread_mem_banks[MEMORY_BANKS];
extern int m68k_pc_indirect;

extern "C" void z3660_printf(const TCHAR* format, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  log_message(LOG_LEVEL_INFO, "%s", buffer);
}

static void pistorm_force_rom_overlay(void) {
  if (!cfg) {
    return;
  }
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_ROM) {
      if (cfg->map_mirror[i] == (unsigned int)-1) {
        cfg->map_mirror[i] = 0x00000000;
      }
    }
  }
}

static inline bool uae_jit_invalid_addr(uaecptr addr) {
  return ((uae_u32)addr) == 0xFFFFFFFFu;
}

// Do not let JIT consume host-side low-memory maps (chip/custom space):
// those addresses belong to the real Amiga bus. Reset vectors are handled
// explicitly in read_long/read_word/read_byte.
static inline bool uae_jit_has_lowmem_map(uaecptr addr, bool is_write) {
  if (!cfg) {
    return false;
  }
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE) {
      continue;
    }
    if (ovl && cfg->map_mirror[i] != ((unsigned int)-1) &&
        addr >= cfg->map_mirror[i] &&
        addr < (cfg->map_mirror[i] + cfg->map_size[i])) {
      if (!is_write) {
        return cfg->map_type[i] == MAPTYPE_ROM || cfg->map_type[i] == MAPTYPE_RAM_WTC;
      }
      return cfg->map_type[i] == MAPTYPE_RAM_WTC;
    }
    if (addr >= cfg->map_offset[i] && addr < cfg->map_high[i]) {
      if (cfg->map_type[i] == MAPTYPE_ROM) {
        return !is_write;
      }
      return cfg->map_type[i] == MAPTYPE_RAM || cfg->map_type[i] == MAPTYPE_RAM_WTC ||
             cfg->map_type[i] == MAPTYPE_RAM_NOALLOC;
    }
  }
  return false;
}

static inline bool uae_jit_allow_mapped_access(uaecptr addr, bool is_write) {
  if (addr < 0x00200000) {
    return uae_jit_has_lowmem_map(addr, is_write);
  }
  return true;
}

static inline bool bridge_mapped_read(uaecptr addr, unsigned char type, unsigned int* val) {
  if (!cfg || !val) {
    return false;
  }
  if (!uae_jit_allow_mapped_access(addr, false)) {
    return false;
  }
  return handle_mapped_read(cfg, (unsigned int)addr, val, type) != -1;
}

static inline bool bridge_mapped_write(uaecptr addr, unsigned int v, unsigned char type) {
  if (!cfg) {
    return false;
  }
  if (!uae_jit_allow_mapped_access(addr, true)) {
    return false;
  }
  return handle_mapped_write(cfg, (unsigned int)addr, v, type) != -1;
}

static int g_jit_trace = 0;
static int g_jit_trace_ifetch_left = 0;
static int g_jit_trace_data_left = 0;

static inline void uae_jit_trace_access(const char* kind, uaecptr addr, uae_u32 val,
                                        bool mapped, bool ifetch) {
  if (!g_jit_trace) {
    return;
  }
  int* budget = ifetch ? &g_jit_trace_ifetch_left : &g_jit_trace_data_left;
  if (*budget <= 0) {
    return;
  }
  (*budget)--;
  LOG_DEBUG("[UAE-JIT] %s %-6s addr=%08X val=%08X fc=%u\n", mapped ? "MAP" : "BUS", kind,
            (unsigned int)addr, (unsigned int)val,
            (unsigned int)(ifetch ? (regs.sfc & 0x7) : (regs.dfc & 0x7)));
}

static inline void uae_jit_trace_reset_vec(const char* src, unsigned int addr, unsigned int val) {
  if (!g_jit_trace) {
    return;
  }
  LOG_INFO("[UAE-JIT] RESETV %-8s addr=%08X val=%08X\n", src, addr, val);
}

static inline bool bridge_read_rom_data(unsigned int addr, unsigned char type, unsigned int* val) {
  if (!cfg || !val) {
    return false;
  }
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] != MAPTYPE_ROM || !cfg->map_data[i] || cfg->rom_size[i] == 0) {
      continue;
    }
    if (addr < cfg->map_offset[i] || addr >= cfg->map_high[i]) {
      continue;
    }
    unsigned int off = (addr - cfg->map_offset[i]) % cfg->rom_size[i];
    unsigned char* p = cfg->map_data[i] + off;
    switch (type) {
      case OP_TYPE_BYTE:
        *val = p[0];
        return true;
      case OP_TYPE_WORD:
        if (off + 1 >= cfg->rom_size[i]) {
          return false;
        }
        *val = ((unsigned int)p[0] << 8) | (unsigned int)p[1];
        return true;
      case OP_TYPE_LONGWORD:
        if (off + 3 >= cfg->rom_size[i]) {
          return false;
        }
        *val = ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
               ((unsigned int)p[2] << 8) | (unsigned int)p[3];
        return true;
      default:
        return false;
    }
  }
  return false;
}

static uae_u8* REGPARAM2 pistorm_xlate(uaecptr addr) {
  if (!cfg) {
    return nullptr;
  }
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE) {
      continue;
    }
    if (ovl && (cfg->map_type[i] == MAPTYPE_ROM || cfg->map_type[i] == MAPTYPE_RAM_WTC)) {
      if (cfg->map_mirror[i] != ((unsigned int)-1) &&
          addr >= cfg->map_mirror[i] &&
          addr < (cfg->map_mirror[i] + cfg->map_size[i])) {
        return cfg->map_data[i] + ((addr - cfg->map_mirror[i]) % cfg->rom_size[i]);
      }
    }
    if (addr >= cfg->map_offset[i] && addr < cfg->map_high[i]) {
      switch (cfg->map_type[i]) {
      case MAPTYPE_ROM:
        return cfg->map_data[i] + ((addr - cfg->map_offset[i]) % cfg->rom_size[i]);
      case MAPTYPE_RAM:
      case MAPTYPE_RAM_WTC:
      case MAPTYPE_RAM_NOALLOC:
        return cfg->map_data[i] + (addr - cfg->map_offset[i]);
      case MAPTYPE_REGISTER:
        return nullptr;
      default:
        break;
      }
    }
  }
  return nullptr;
}

static int REGPARAM2 pistorm_check(uaecptr addr, uae_u32 size) {
  uae_u8* base = pistorm_xlate(addr);
  if (!base) {
    return 0;
  }
  // Best-effort bounds check using map ranges.
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE) {
      continue;
    }
    if (addr >= cfg->map_offset[i] && addr < cfg->map_high[i]) {
      uae_u32 end = addr + size;
      return end <= cfg->map_high[i];
    }
  }
  return 0;
}

static uae_u32 REGPARAM2 pistorm_lget(uaecptr addr) {
  if (uae_jit_invalid_addr(addr)) {
    uae_jit_trace_access("D32R", addr, 0xFFFFFFFFu, false, false);
    return 0xFFFFFFFFu;
  }
  unsigned int val = 0;
  if (bridge_mapped_read(addr, OP_TYPE_LONGWORD, &val)) {
    uae_jit_trace_access("D32R", addr, (uae_u32)val, true, false);
    return (uae_u32)val;
  }
  cpu_set_fc(regs.dfc & 0x7);
  uae_u32 v = (uae_u32)m68k_read_memory_32((unsigned int)addr);
  uae_jit_trace_access("D32R", addr, v, false, false);
  return v;
}

static uae_u32 REGPARAM2 pistorm_wget(uaecptr addr) {
  if (uae_jit_invalid_addr(addr)) {
    uae_jit_trace_access("D16R", addr, 0x0000FFFFu, false, false);
    return 0x0000FFFFu;
  }
  unsigned int val = 0;
  if (bridge_mapped_read(addr, OP_TYPE_WORD, &val)) {
    uae_jit_trace_access("D16R", addr, (uae_u32)val, true, false);
    return (uae_u32)val;
  }
  cpu_set_fc(regs.dfc & 0x7);
  uae_u32 v = (uae_u32)m68k_read_memory_16((unsigned int)addr);
  uae_jit_trace_access("D16R", addr, v, false, false);
  return v;
}

static uae_u32 REGPARAM2 pistorm_bget(uaecptr addr) {
  if (uae_jit_invalid_addr(addr)) {
    uae_jit_trace_access("D08R", addr, 0x000000FFu, false, false);
    return 0x000000FFu;
  }
  unsigned int val = 0;
  if (bridge_mapped_read(addr, OP_TYPE_BYTE, &val)) {
    uae_jit_trace_access("D08R", addr, (uae_u32)val, true, false);
    return (uae_u32)val;
  }
  cpu_set_fc(regs.dfc & 0x7);
  uae_u32 v = (uae_u32)m68k_read_memory_8((unsigned int)addr);
  uae_jit_trace_access("D08R", addr, v, false, false);
  return v;
}

static void REGPARAM2 pistorm_lput(uaecptr addr, uae_u32 v) {
  if (uae_jit_invalid_addr(addr)) {
    uae_jit_trace_access("D32W", addr, v, false, false);
    return;
  }
  if (bridge_mapped_write(addr, (unsigned int)v, OP_TYPE_LONGWORD)) {
    uae_jit_trace_access("D32W", addr, v, true, false);
    return;
  }
  cpu_set_fc(regs.dfc & 0x7);
  uae_jit_trace_access("D32W", addr, v, false, false);
  m68k_write_memory_32((unsigned int)addr, (unsigned int)v);
}

static void REGPARAM2 pistorm_wput(uaecptr addr, uae_u32 v) {
  if (uae_jit_invalid_addr(addr)) {
    uae_jit_trace_access("D16W", addr, v, false, false);
    return;
  }
  if (bridge_mapped_write(addr, (unsigned int)v, OP_TYPE_WORD)) {
    uae_jit_trace_access("D16W", addr, v, true, false);
    return;
  }
  cpu_set_fc(regs.dfc & 0x7);
  uae_jit_trace_access("D16W", addr, v, false, false);
  m68k_write_memory_16((unsigned int)addr, (unsigned int)v);
}

static void REGPARAM2 pistorm_bput(uaecptr addr, uae_u32 v) {
  if (uae_jit_invalid_addr(addr)) {
    uae_jit_trace_access("D08W", addr, v, false, false);
    return;
  }
  if (bridge_mapped_write(addr, (unsigned int)v, OP_TYPE_BYTE)) {
    uae_jit_trace_access("D08W", addr, v, true, false);
    return;
  }
  cpu_set_fc(regs.dfc & 0x7);
  uae_jit_trace_access("D08W", addr, v, false, false);
  m68k_write_memory_8((unsigned int)addr, (unsigned int)v);
}

static uae_u32 REGPARAM2 pistorm_lgeti(uaecptr addr) {
  if (uae_jit_invalid_addr(addr)) {
    uae_jit_trace_access("I32R", addr, 0xFFFFFFFFu, false, true);
    return 0xFFFFFFFFu;
  }
  unsigned int val = 0;
  if (bridge_mapped_read(addr, OP_TYPE_LONGWORD, &val)) {
    uae_jit_trace_access("I32R", addr, (uae_u32)val, true, true);
    return (uae_u32)val;
  }
  cpu_set_fc(regs.sfc & 0x7);
  uae_u32 v = (uae_u32)m68k_read_memory_32((unsigned int)addr);
  uae_jit_trace_access("I32R", addr, v, false, true);
  return v;
}

static uae_u32 REGPARAM2 pistorm_wgeti(uaecptr addr) {
  if (uae_jit_invalid_addr(addr)) {
    uae_jit_trace_access("I16R", addr, 0x0000FFFFu, false, true);
    return 0x0000FFFFu;
  }
  unsigned int val = 0;
  if (bridge_mapped_read(addr, OP_TYPE_WORD, &val)) {
    uae_jit_trace_access("I16R", addr, (uae_u32)val, true, true);
    return (uae_u32)val;
  }
  cpu_set_fc(regs.sfc & 0x7);
  uae_u32 v = (uae_u32)m68k_read_memory_16((unsigned int)addr);
  uae_jit_trace_access("I16R", addr, v, false, true);
  return v;
}

static addrbank pistorm_bank = {
    pistorm_lget,  pistorm_wget,  pistorm_bget,
    pistorm_lput,  pistorm_wput,  pistorm_bput,
    pistorm_xlate, pistorm_check, NULL, _T("pistorm"), _T("PiStorm bridge"),
    pistorm_lgeti, pistorm_wgeti,
    ABFLAG_IO | ABFLAG_THREADSAFE, S_READ, S_WRITE};

extern "C" unsigned int read_long(unsigned int address) {
  if (address == 0xFFFFFFFFu) {
    return 0xFFFFFFFFu;
  }
  unsigned int val = 0;
  // Reset vector safety net: if low-memory overlay lookup races, read from Kick ROM base.
  if (ovl && address < 8 && bridge_read_rom_data(0x00F80000u + address, OP_TYPE_LONGWORD, &val)) {
    uae_jit_trace_reset_vec("rom-data", address, val);
    return val;
  }
  if (ovl && address < 8 && cfg &&
      handle_mapped_read(cfg, 0x00F80000u + address, &val, OP_TYPE_LONGWORD) != -1) {
    uae_jit_trace_reset_vec("map-read", address, val);
    return val;
  }
  if (ovl && address < 8) {
    val = m68k_read_memory_32(0x00F80000u + address);
    uae_jit_trace_reset_vec("bus-f8", address, val);
    return val;
  }
  if (bridge_mapped_read((uaecptr)address, OP_TYPE_LONGWORD, &val)) {
    return val;
  }
  cpu_set_fc(regs.sfc & 0x7);
  val = m68k_read_memory_32(address);
  if (address < 8) {
    uae_jit_trace_reset_vec("bus-low", address, val);
  }
  return val;
}

extern "C" unsigned int read_word(unsigned int address) {
  if (address == 0xFFFFFFFFu) {
    return 0x0000FFFFu;
  }
  unsigned int val = 0;
  if (ovl && address < 8 && bridge_read_rom_data(0x00F80000u + address, OP_TYPE_WORD, &val)) {
    uae_jit_trace_reset_vec("rom-data", address, val);
    return val;
  }
  if (ovl && address < 8 && cfg &&
      handle_mapped_read(cfg, 0x00F80000u + address, &val, OP_TYPE_WORD) != -1) {
    uae_jit_trace_reset_vec("map-read", address, val);
    return val;
  }
  if (ovl && address < 8) {
    val = m68k_read_memory_16(0x00F80000u + address);
    uae_jit_trace_reset_vec("bus-f8", address, val);
    return val;
  }
  if (bridge_mapped_read((uaecptr)address, OP_TYPE_WORD, &val)) {
    return val;
  }
  cpu_set_fc(regs.sfc & 0x7);
  val = m68k_read_memory_16(address);
  if (address < 8) {
    uae_jit_trace_reset_vec("bus-low", address, val);
  }
  return val;
}

extern "C" unsigned int read_byte(unsigned int address) {
  if (address == 0xFFFFFFFFu) {
    return 0x000000FFu;
  }
  unsigned int val = 0;
  if (ovl && address < 8 && bridge_read_rom_data(0x00F80000u + address, OP_TYPE_BYTE, &val)) {
    uae_jit_trace_reset_vec("rom-data", address, val);
    return val;
  }
  if (ovl && address < 8 && cfg &&
      handle_mapped_read(cfg, 0x00F80000u + address, &val, OP_TYPE_BYTE) != -1) {
    uae_jit_trace_reset_vec("map-read", address, val);
    return val;
  }
  if (ovl && address < 8) {
    val = m68k_read_memory_8(0x00F80000u + address);
    uae_jit_trace_reset_vec("bus-f8", address, val);
    return val;
  }
  if (bridge_mapped_read((uaecptr)address, OP_TYPE_BYTE, &val)) {
    return val;
  }
  cpu_set_fc(regs.sfc & 0x7);
  val = m68k_read_memory_8(address);
  if (address < 8) {
    uae_jit_trace_reset_vec("bus-low", address, val);
  }
  return val;
}

static void uae_pistorm_set_defaults(int cpu_model, int enable_jit, int enable_fpu) {
  memset(&currprefs, 0, sizeof(currprefs));
  memset(&changed_prefs, 0, sizeof(changed_prefs));

  currprefs.cpu_model = changed_prefs.cpu_model = cpu_model;
  currprefs.mmu_model = changed_prefs.mmu_model = 0;
  currprefs.cpu_compatible = changed_prefs.cpu_compatible = true;
  currprefs.address_space_24 = changed_prefs.address_space_24 = false;
  currprefs.cpu_cycle_exact = changed_prefs.cpu_cycle_exact = false;
  currprefs.cpu_memory_cycle_exact = changed_prefs.cpu_memory_cycle_exact = false;
  currprefs.int_no_unimplemented = changed_prefs.int_no_unimplemented = false;
  currprefs.fpu_no_unimplemented = changed_prefs.fpu_no_unimplemented = false;
  currprefs.crash_auto_reset = changed_prefs.crash_auto_reset = true;
  currprefs.m68k_speed = changed_prefs.m68k_speed = -1;
  currprefs.comptrustbyte = changed_prefs.comptrustbyte = 1;

  if (enable_jit) {
    currprefs.cachesize = changed_prefs.cachesize = 32 * 1024;
    currprefs.compfpu = changed_prefs.compfpu = enable_fpu ? true : false;
  } else {
    currprefs.cachesize = changed_prefs.cachesize = 0;
    currprefs.compfpu = changed_prefs.compfpu = false;
  }
  currprefs.fpu_strict = changed_prefs.fpu_strict = true;

  if (enable_fpu) {
    if (cpu_model >= 68040) {
      currprefs.fpu_model = changed_prefs.fpu_model = 68040;
    } else {
      currprefs.fpu_model = changed_prefs.fpu_model = 68882;
    }
  } else {
    currprefs.fpu_model = changed_prefs.fpu_model = 0;
  }
}

static void uae_pistorm_apply_reset_vectors(void) {
  unsigned int sp_m = 0;
  unsigned int pc_m = 0;
  uae_u32 sp = 0;
  uae_u32 pc = 0;

  sp = (uae_u32)read_long(0x00000000);
  pc = (uae_u32)read_long(0x00000004);

  if ((sp == 0x00000000 && pc == 0x00000000) ||
      (sp == 0xFFFFFFFFu && pc == 0xFFFFFFFFu)) {
    if (bridge_read_rom_data(0x00F80000, OP_TYPE_LONGWORD, &sp_m) &&
        bridge_read_rom_data(0x00F80004, OP_TYPE_LONGWORD, &pc_m)) {
      sp = (uae_u32)sp_m;
      pc = (uae_u32)pc_m;
    } else if (cfg && handle_mapped_read(cfg, 0x00F80000, &sp_m, OP_TYPE_LONGWORD) != -1 &&
               handle_mapped_read(cfg, 0x00F80004, &pc_m, OP_TYPE_LONGWORD) != -1) {
      sp = (uae_u32)sp_m;
      pc = (uae_u32)pc_m;
    } else {
      sp = (uae_u32)m68k_read_memory_32(0x00F80000);
      pc = (uae_u32)m68k_read_memory_32(0x00F80004);
    }
  }

  regs.isp = sp;
  regs.usp = sp;
  regs.regs[15] = sp;  // A7
  regs.pc = pc;
  regs.s = 1;
  regs.m = 0;
  regs.t0 = regs.t1 = 0;
  regs.intmask = 7;
  regs.sr = 0x2700;
  LOG_INFO("[UAE] reset vectors: SP=%08X PC=%08X\n", sp, pc);
}

extern "C" int uae_pistorm_init(int cpu_model, int enable_jit, int enable_fpu) {
  uae_pistorm_set_defaults(cpu_model, enable_jit, enable_fpu);
  const char* trace = getenv("PISTORM_UAE_JIT_TRACE");
  g_jit_trace = (trace && atoi(trace) != 0) ? 1 : 0;
  g_jit_trace_ifetch_left = 128;
  g_jit_trace_data_left = 128;
  if (g_jit_trace) {
    LOG_INFO("[UAE-JIT] trace enabled (PISTORM_UAE_JIT_TRACE=1)\n");
  }
  // Force ROM overlay on at reset so vectors are visible at 0x000000.
  ovl = 1;
  pistorm_force_rom_overlay();
  m68k_pc_indirect = 1;

  regs.natmem_offset = (uae_u8*)0;
  for (int i = 0; i < MEMORY_BANKS; i++) {
    mem_banks[i] = &pistorm_bank;
    thread_mem_banks[i] = &pistorm_bank;
  }

  m68k_reset_newcpu(true);
  uae_pistorm_apply_reset_vectors();
  init_m68k();
  build_cpufunctbl();
  // Force indirect PC mode so opcode fetches go through addrbank helpers.
  m68k_pc_indirect = 1;
  m68k_setpc_normal(regs.pc);
  doint();
  fill_prefetch_quick();
  set_cycles(start_cycles);
  regs.stopped = false;
  set_special(0);
  return 0;
}

extern "C" void uae_pistorm_run(void) {
  // Ensure indirect PC mode stays enabled for Pistorm memory accessors.
  m68k_pc_indirect = 1;
  m68k_go(1);
}

extern "C" void uae_pistorm_set_irq(int level) {
  read_irq = level;
}

extern "C" void uae_pistorm_pulse_reset(void) {
  m68k_reset_newcpu(true);
  ovl = 1;
  pistorm_force_rom_overlay();
  m68k_pc_indirect = 1;
  uae_pistorm_apply_reset_vectors();
  m68k_setpc_normal(regs.pc);
  fill_prefetch_quick();
  set_cycles(start_cycles);
  regs.stopped = false;
  set_special(0);
}

extern "C" uint32_t uae_pistorm_get_pc(void) {
  return (uint32_t)m68k_getpc();
}
