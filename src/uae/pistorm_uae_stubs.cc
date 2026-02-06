// SPDX-License-Identifier: MIT

#include "sysconfig.h"
#include "sysdeps.h"
#include "uae/types.h"
#include "memory.h"
#include "newcpu.h"
#include "machdep/maccess.h"
#include "../config_file/config_file.h"
#include "../memory_mapped.h"
#include <endian.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern "C" {
unsigned int read_long(unsigned int address);
unsigned int read_word(unsigned int address);
unsigned int read_byte(unsigned int address);
void m68k_write_memory_8(unsigned int address, unsigned int value);
void m68k_write_memory_16(unsigned int address, unsigned int value);
void m68k_write_memory_32(unsigned int address, unsigned int value);
}

extern struct emulator_config* cfg;
extern int ovl;

// Minimal dummy bank to satisfy references from UAE core.
static uae_u32 REGPARAM2 dummy_lget(uaecptr) { return 0; }
static uae_u32 REGPARAM2 dummy_wget(uaecptr) { return 0; }
static uae_u32 REGPARAM2 dummy_bget(uaecptr) { return 0; }
static void REGPARAM2 dummy_lput(uaecptr, uae_u32) {}
static void REGPARAM2 dummy_wput(uaecptr, uae_u32) {}
static void REGPARAM2 dummy_bput(uaecptr, uae_u32) {}
static uae_u8* REGPARAM2 dummy_xlate(uaecptr) { return nullptr; }
static int REGPARAM2 dummy_check(uaecptr, uae_u32) { return 0; }

addrbank dmmy_bank = {
    .lget = dummy_lget,
    .wget = dummy_wget,
    .bget = dummy_bget,
    .lput = dummy_lput,
    .wput = dummy_wput,
    .bput = dummy_bput,
    .xlateaddr = dummy_xlate,
    .check = dummy_check,
    .baseaddr = nullptr,
    .label = _T("dummy"),
    .name = _T("dummy"),
    .lgeti = dummy_lget,
    .wgeti = dummy_wget,
    .flags = ABFLAG_NONE,
    .jit_read_flag = S_READ,
    .jit_write_flag = S_WRITE,
};

// Provide thread_mem_banks for non-threaded builds (newcpu expects it).
addrbank* thread_mem_banks[MEMORY_BANKS];

static int g_mem_trace = -1;
static int g_mem_trace_budget = 128;

static inline bool mem_trace_enabled(void) {
  if (g_mem_trace == -1) {
    const char* e = getenv("PISTORM_UAE_MEM_TRACE");
    g_mem_trace = (e && atoi(e) != 0) ? 1 : 0;
  }
  return g_mem_trace != 0;
}

static inline bool ptr_is_emu_addr(const void* p) {
  uintptr_t u = (uintptr_t)p;
  return u <= 0xFFFFFFFFu;
}

// Keep low memory on the real Amiga bus. Only allow read-only overlay-at-0 fetches during reset.
static inline bool uae_stub_has_lowmem_map(unsigned int addr, bool is_write) {
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

static inline bool uae_stub_allow_mapped_access(unsigned int addr, bool is_write) {
  if (addr < 0x00200000) {
    return uae_stub_has_lowmem_map(addr, is_write);
  }
  return true;
}

static inline void trace_mem(const char* op, uintptr_t raw, uae_u32 val, const char* mode) {
  if (!mem_trace_enabled() || g_mem_trace_budget <= 0) {
    return;
  }
  g_mem_trace_budget--;
  printf("[UAE-MEM] %s %s raw=%016lX val=%08X\n", op, mode, (unsigned long)raw,
         (unsigned int)val);
}

// CPU indirect memory helpers used by UAE.
uae_u32 do_get_mem_long(uae_u32* a) {
  // The pointer 'a' might be an emulator address cast to a pointer
  // If it's within the 32-bit address space, treat it as an emulator address
  if (ptr_is_emu_addr(a)) {
    unsigned int addr = (unsigned int)(uintptr_t)a;
    unsigned int val = 0;
    if (cfg && uae_stub_allow_mapped_access(addr, false) &&
        handle_mapped_read(cfg, addr, &val, OP_TYPE_LONGWORD) != -1) {
      trace_mem("R32", (uintptr_t)a, (uae_u32)val, "addr-map");
      return (uae_u32)val;
    }
    uae_u32 v = (uae_u32)read_long(addr);
    trace_mem("R32", (uintptr_t)a, v, "addr-bus");
    return v;
  }

  uae_u32 tmp;
  memcpy(&tmp, a, sizeof(tmp));
  uae_u32 v = be32toh(tmp);
  trace_mem("R32", (uintptr_t)a, v, "ptr");
  return v;
}

uint16_t do_get_mem_word(uint16_t* a) {
  // The pointer 'a' might be an emulator address cast to a pointer
  // If it's within the 32-bit address space, treat it as an emulator address
  if (ptr_is_emu_addr(a)) {
    unsigned int addr = (unsigned int)(uintptr_t)a;
    unsigned int val = 0;
    if (cfg && uae_stub_allow_mapped_access(addr, false) &&
        handle_mapped_read(cfg, addr, &val, OP_TYPE_WORD) != -1) {
      trace_mem("R16", (uintptr_t)a, (uae_u32)(val & 0xFFFF), "addr-map");
      return (uint16_t)val;
    }
    uint16_t v = (uint16_t)read_word(addr);
    trace_mem("R16", (uintptr_t)a, (uae_u32)v, "addr-bus");
    return v;
  }

  uint16_t tmp;
  memcpy(&tmp, a, sizeof(tmp));
  uint16_t v = be16toh(tmp);
  trace_mem("R16", (uintptr_t)a, (uae_u32)v, "ptr");
  return v;
}

uint8_t do_get_mem_byte(uint8_t* a) {
  // The pointer 'a' might be an emulator address cast to a pointer
  // If it's within the 32-bit address space, treat it as an emulator address
  if (ptr_is_emu_addr(a)) {
    unsigned int addr = (unsigned int)(uintptr_t)a;
    unsigned int val = 0;
    if (cfg && uae_stub_allow_mapped_access(addr, false) &&
        handle_mapped_read(cfg, addr, &val, OP_TYPE_BYTE) != -1) {
      trace_mem("R08", (uintptr_t)a, (uae_u32)(val & 0xFF), "addr-map");
      return (uint8_t)val;
    }
    uint8_t v = (uint8_t)read_byte(addr);
    trace_mem("R08", (uintptr_t)a, (uae_u32)v, "addr-bus");
    return v;
  }

  uint8_t v = *a;
  trace_mem("R08", (uintptr_t)a, (uae_u32)v, "ptr");
  return v;
}

void do_put_mem_long(uae_u32* a, uae_u32 v) {
  if (ptr_is_emu_addr(a)) {
    unsigned int addr = (unsigned int)(uintptr_t)a;
    if (!cfg || !uae_stub_allow_mapped_access(addr, true) ||
        handle_mapped_write(cfg, addr, (unsigned int)v, OP_TYPE_LONGWORD) == -1) {
      m68k_write_memory_32(addr, (unsigned int)v);
      trace_mem("W32", (uintptr_t)a, v, "addr-bus");
    } else {
      trace_mem("W32", (uintptr_t)a, v, "addr-map");
    }
    return;
  }

  uae_u32 tmp = htobe32(v);
  memcpy(a, &tmp, sizeof(tmp));
  trace_mem("W32", (uintptr_t)a, v, "ptr");
}

void do_put_mem_word(uint16_t* a, uint16_t v) {
  if (ptr_is_emu_addr(a)) {
    unsigned int addr = (unsigned int)(uintptr_t)a;
    if (!cfg || !uae_stub_allow_mapped_access(addr, true) ||
        handle_mapped_write(cfg, addr, (unsigned int)v, OP_TYPE_WORD) == -1) {
      m68k_write_memory_16(addr, (unsigned int)v);
      trace_mem("W16", (uintptr_t)a, (uae_u32)v, "addr-bus");
    } else {
      trace_mem("W16", (uintptr_t)a, (uae_u32)v, "addr-map");
    }
    return;
  }

  uint16_t tmp = htobe16(v);
  memcpy(a, &tmp, sizeof(tmp));
  trace_mem("W16", (uintptr_t)a, (uae_u32)v, "ptr");
}

void do_put_mem_byte(uint8_t* a, uint8_t v) {
  if (ptr_is_emu_addr(a)) {
    unsigned int addr = (unsigned int)(uintptr_t)a;
    if (!cfg || !uae_stub_allow_mapped_access(addr, true) ||
        handle_mapped_write(cfg, addr, (unsigned int)v, OP_TYPE_BYTE) == -1) {
      m68k_write_memory_8(addr, (unsigned int)v);
      trace_mem("W08", (uintptr_t)a, (uae_u32)v, "addr-bus");
    } else {
      trace_mem("W08", (uintptr_t)a, (uae_u32)v, "addr-map");
    }
    return;
  }

  *a = v;
  trace_mem("W08", (uintptr_t)a, (uae_u32)v, "ptr");
}

// FPU stubs for now (JIT backend without 68k FPU emulation enabled).
void fpuop_arithmetic(uae_u32, uae_u16) {}
void fpuop_dbcc(uae_u32, uae_u16) {}
void fpuop_scc(uae_u32, uae_u16) {}
void fpuop_trapcc(uae_u32, uaecptr, uae_u16) {}
void fpuop_bcc(uae_u32, uaecptr, uae_u32) {}
void fpuop_save(uae_u32) {}
void fpuop_restore(uae_u32) {}
void fpu_reset(void) {}

// Z3660-specific hooks (no-ops here).
extern "C" void cpu_emulator_reset_core0(void) {}
extern "C" void reset_autoconfig(void) {}

// Provide a minimal intlev() to satisfy newcpu references.
extern "C" int read_irq;
extern "C" int intlev(void) {
  return read_irq;
}

// Logging function needed by UAE JIT - this is just a placeholder since the actual
// log_message function will be provided by the main executable

bool is_cycle_ce(uaecptr) {
  return false;
}
