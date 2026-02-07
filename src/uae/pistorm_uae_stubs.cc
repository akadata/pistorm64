// SPDX-License-Identifier: MIT

#include "sysconfig.h"
#include "sysdeps.h"
#include "uae/types.h"
#include "memory.h"
#include "newcpu.h"
#include "machdep/maccess.h"
#include "../config_file/config_file.h"
#include "../memory_mapped.h"
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
static int g_mem_trace_ea_only = -1;

static inline bool mem_trace_enabled(void) {
  if (g_mem_trace == -1) {
    const char* e = getenv("PISTORM_UAE_MEM_TRACE");
    const char* b = getenv("PISTORM_UAE_MEM_TRACE_BUDGET");
    const char* ea_only = getenv("PISTORM_UAE_MEM_TRACE_EA_ONLY");
    g_mem_trace = (e && atoi(e) != 0) ? 1 : 0;
    if (b && atoi(b) > 0) {
      g_mem_trace_budget = atoi(b);
    }
    g_mem_trace_ea_only = (ea_only && atoi(ea_only) != 0) ? 1 : 0;
  }
  return g_mem_trace != 0;
}

enum class ptr_mode {
  ptr,
  ptr32,
  ea
};

struct ptr_resolve_result {
  ptr_mode mode;
  uintptr_t raw;
  unsigned int ea;
  uae_u8* host;
};

static inline bool host_ptr_in_maps(uintptr_t p, uae_u32 size) {
  if (!cfg || p == 0 || size == 0) {
    return false;
  }
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE || !cfg->map_data[i]) {
      continue;
    }
    uintptr_t lo = (uintptr_t)cfg->map_data[i];
    uintptr_t hi = lo + (uintptr_t)cfg->map_size[i];
    if (p >= lo && (p + (uintptr_t)size) <= hi) {
      return true;
    }
  }
  return false;
}

static inline ptr_resolve_result resolve_mem_ptr(const void* p, uae_u32 size) {
  uintptr_t raw = (uintptr_t)p;
  unsigned int ea = (unsigned int)(raw & 0xFFFFFFFFu);
  bool low32 = ((raw >> 32) == 0) || ((raw & 0xFFFFFFFF00000000ull) == 0xFFFFFFFF00000000ull);

  if (!low32) {
    if (host_ptr_in_maps(raw, size)) {
      return {ptr_mode::ptr, raw, ea, (uae_u8*)raw};
    }
    if (regs.natmem_offset) {
      uintptr_t nat_hi = ((uintptr_t)regs.natmem_offset) & 0xFFFFFFFF00000000ull;
      if ((raw & 0xFFFFFFFF00000000ull) == nat_hi) {
        return {ptr_mode::ea, raw, ea, nullptr};
      }
    }
    return {ptr_mode::ptr, raw, ea, (uae_u8*)raw};
  }
  if (regs.natmem_offset) {
    uintptr_t full = (uintptr_t)regs.natmem_offset + (uintptr_t)ea;
    if (host_ptr_in_maps(full, size)) {
      return {ptr_mode::ptr32, raw, ea, (uae_u8*)full};
    }
  }
  return {ptr_mode::ea, raw, ea, nullptr};
}

static inline void trace_mem(const char* op, uintptr_t raw, uae_u32 val, const char* mode) {
  if (!mem_trace_enabled() || g_mem_trace_budget <= 0) {
    return;
  }
  if (g_mem_trace_ea_only > 0 && strcmp(mode, "ea") != 0) {
    return;
  }
  g_mem_trace_budget--;
  printf("[UAE-MEM] %s %s raw=%016lX val=%08X\n", op, mode, (unsigned long)raw,
         (unsigned int)val);
}

// CPU indirect memory helpers used by UAE - these should work with host pointers as expected by JIT
// The JIT core passes host pointers to emulator memory, not 68k bus addresses
uae_u32 do_get_mem_long(uae_u32* a) {
  ptr_resolve_result r = resolve_mem_ptr(a, 4);
  if (r.mode == ptr_mode::ea) {
    uae_u32 v = (uae_u32)read_long(r.ea);
    trace_mem("R32", r.raw, v, "ea");
    return v;
  }
  uae_u8* b = r.host;
  uae_u32 v = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3]);
  trace_mem("R32", r.raw, v, r.mode == ptr_mode::ptr32 ? "ptr32" : "ptr");
  return v;
}

uint16_t do_get_mem_word(uint16_t* a) {
  ptr_resolve_result r = resolve_mem_ptr(a, 2);
  if (r.mode == ptr_mode::ea) {
    uint16_t v = (uint16_t)read_word(r.ea);
    trace_mem("R16", r.raw, (uae_u32)v, "ea");
    return v;
  }
  uae_u8* b = r.host;
  uint16_t v = (b[0] << 8) | (b[1]);
  trace_mem("R16", r.raw, (uae_u32)v, r.mode == ptr_mode::ptr32 ? "ptr32" : "ptr");
  return v;
}

uint8_t do_get_mem_byte(uint8_t* a) {
  ptr_resolve_result r = resolve_mem_ptr(a, 1);
  if (r.mode == ptr_mode::ea) {
    uint8_t v = (uint8_t)read_byte(r.ea);
    trace_mem("R08", r.raw, (uae_u32)v, "ea");
    return v;
  }
  uint8_t v = *r.host;
  trace_mem("R08", r.raw, (uae_u32)v, r.mode == ptr_mode::ptr32 ? "ptr32" : "ptr");
  return v;
}

void do_put_mem_long(uae_u32* a, uae_u32 v) {
  ptr_resolve_result r = resolve_mem_ptr(a, 4);
  if (r.mode == ptr_mode::ea) {
    m68k_write_memory_32(r.ea, (unsigned int)v);
    trace_mem("W32", r.raw, v, "ea");
    return;
  }
  uae_u8* b = r.host;
  b[0] = (v >> 24) & 0xFF;
  b[1] = (v >> 16) & 0xFF;
  b[2] = (v >>  8) & 0xFF;
  b[3] = (v >>  0) & 0xFF;
  trace_mem("W32", r.raw, v, r.mode == ptr_mode::ptr32 ? "ptr32" : "ptr");
}

void do_put_mem_word(uint16_t* a, uint16_t v) {
  ptr_resolve_result r = resolve_mem_ptr(a, 2);
  if (r.mode == ptr_mode::ea) {
    m68k_write_memory_16(r.ea, (unsigned int)v);
    trace_mem("W16", r.raw, (uae_u32)v, "ea");
    return;
  }
  uae_u8* b = r.host;
  b[0] = (v >> 8) & 0xFF;
  b[1] = (v >> 0) & 0xFF;
  trace_mem("W16", r.raw, (uae_u32)v, r.mode == ptr_mode::ptr32 ? "ptr32" : "ptr");
}

void do_put_mem_byte(uint8_t* a, uint8_t v) {
  ptr_resolve_result r = resolve_mem_ptr(a, 1);
  if (r.mode == ptr_mode::ea) {
    m68k_write_memory_8(r.ea, (unsigned int)v);
    trace_mem("W08", r.raw, (uae_u32)v, "ea");
    return;
  }
  *r.host = v;
  trace_mem("W08", r.raw, (uae_u32)v, r.mode == ptr_mode::ptr32 ? "ptr32" : "ptr");
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

// Z3660-specific hook (unused on PiStorm64 path).
extern "C" void cpu_emulator_reset_core0(void) {}

extern "C" void reset_autoconfig(void) {
  if (cfg && cfg->platform && cfg->platform->handle_reset) {
    cfg->platform->handle_reset(cfg);
  }
}

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
