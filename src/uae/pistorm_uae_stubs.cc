// SPDX-License-Identifier: MIT

#include "sysconfig.h"
#include "sysdeps.h"
#include "uae/types.h"
#include "memory.h"
#include "newcpu.h"
#include "events.h"
#include "machdep/maccess.h"
#include "../config_file/config_file.h"
#include "../memory_mapped.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

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
addrbank* mem_banks[MEMORY_BANKS];
uae_u8* baseaddr[MEMORY_BANKS];
int special_mem = 0;
int special_mem_default = 0;
int jit_n_addr_unsafe = 0;
bool canbang = false;
bool jit_direct_compatible_memory = false;
uaecptr highest_ram = 0;

static inline addrbank* bank_for_addr(uaecptr addr) {
  addrbank* b = mem_banks[bankindex(addr)];
  return b ? b : &dmmy_bank;
}

uae_u32 memory_get_long(uaecptr addr) {
  return bank_for_addr(addr)->lget(addr);
}

uae_u32 memory_get_word(uaecptr addr) {
  return bank_for_addr(addr)->wget(addr);
}

uae_u32 memory_get_byte(uaecptr addr) {
  return bank_for_addr(addr)->bget(addr);
}

uae_u32 memory_get_longi(uaecptr addr) {
  addrbank* b = bank_for_addr(addr);
  return b->lgeti ? b->lgeti(addr) : b->lget(addr);
}

uae_u32 memory_get_wordi(uaecptr addr) {
  addrbank* b = bank_for_addr(addr);
  return b->wgeti ? b->wgeti(addr) : b->wget(addr);
}

void memory_put_long(uaecptr addr, uae_u32 v) {
  bank_for_addr(addr)->lput(addr, v);
}

void memory_put_word(uaecptr addr, uae_u32 v) {
  bank_for_addr(addr)->wput(addr, v);
}

void memory_put_byte(uaecptr addr, uae_u32 v) {
  bank_for_addr(addr)->bput(addr, v);
}

uae_u8* memory_get_real_address(uaecptr addr) {
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
        default:
          break;
      }
    }
  }
  return nullptr;
}

int memory_valid_address(uaecptr addr, uae_u32 size) {
  if (!size) {
    return 1;
  }
  uae_u8* a0 = memory_get_real_address(addr);
  uae_u8* a1 = memory_get_real_address(addr + size - 1);
  return (a0 && a1) ? 1 : 0;
}

void do_cycles_ce(int cycles) {
  do_cycles_cpu_norm(cycles);
}

void do_cycles_ce020(int cycles) {
  do_cycles_cpu_norm(cycles);
}

void jit_abort(const TCHAR* format, ...) {
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fputc('\n', stderr);
  abort();
}

static int g_mem_trace = -1;
static int g_mem_trace_budget = 128;
static int g_reset_trace = -1;

static inline bool mem_trace_enabled(void) {
  if (g_mem_trace == -1) {
    const char* e = getenv("PISTORM_UAE_MEM_TRACE");
    const char* b = getenv("PISTORM_UAE_MEM_TRACE_BUDGET");
    g_mem_trace = (e && atoi(e) != 0) ? 1 : 0;
    if (b && atoi(b) > 0) {
      g_mem_trace_budget = atoi(b);
    }
  }
  return g_mem_trace != 0;
}

static inline void trace_mem(const char* op, uintptr_t raw, uae_u32 val) {
  if (!mem_trace_enabled() || g_mem_trace_budget <= 0) {
    return;
  }
  g_mem_trace_budget--;
  printf("[UAE-MEM] %s ptr=%016lX val=%08X\n", op, (unsigned long)raw, (unsigned int)val);
}

static inline bool reset_trace_enabled(void) {
  if (g_reset_trace == -1) {
    const char* e = getenv("PISTORM_UAE_RESET_TRACE");
    g_reset_trace = (e && atoi(e) != 0) ? 1 : 0;
  }
  return g_reset_trace != 0;
}

// Return-address hint for reset callers when supported by the compiler.
static inline void* reset_caller_hint(void) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_extract_return_addr(__builtin_return_address(0));
#else
  return nullptr;
#endif
}

enum class mem_ptr_mode {
  host_ptr,
  ea32
};

struct mem_ptr_ref {
  mem_ptr_mode mode;
  uintptr_t raw;
  uae_u32 ea;
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

static inline mem_ptr_ref resolve_mem_ptr(const void* p, uae_u32 size) {
  uintptr_t raw = (uintptr_t)p;
  uae_u32 ea = (uae_u32)(raw & 0xFFFFFFFFu);

  if (host_ptr_in_maps(raw, size)) {
    return {mem_ptr_mode::host_ptr, raw, ea, (uae_u8*)raw};
  }

  if ((raw >> 32) == 0 || (raw >> 32) == 0xFFFFFFFFu) {
    return {mem_ptr_mode::ea32, raw, ea, nullptr};
  }

  if (regs.natmem_offset) {
    uintptr_t base = (uintptr_t)regs.natmem_offset;
    if (raw >= base && (raw - base) <= 0xFFFFFFFFu) {
      return {mem_ptr_mode::ea32, raw, (uae_u32)(raw - base), nullptr};
    }
  }

  return {mem_ptr_mode::host_ptr, raw, ea, (uae_u8*)raw};
}

// UAE JIT passes host pointers here. Use direct host-pointer accesses.
uae_u32 do_get_mem_long(uae_u32* a) {
  mem_ptr_ref r = resolve_mem_ptr(a, 4);
  if (r.mode == mem_ptr_mode::ea32) {
    uae_u32 v = (uae_u32)read_long((unsigned int)r.ea);
    trace_mem("R32-EA", r.raw, v);
    return v;
  }
  uae_u8* b = r.host;
  uae_u32 v = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3]);
  trace_mem("R32", r.raw, v);
  return v;
}

uint16_t do_get_mem_word(uint16_t* a) {
  mem_ptr_ref r = resolve_mem_ptr(a, 2);
  if (r.mode == mem_ptr_mode::ea32) {
    uint16_t v = (uint16_t)read_word((unsigned int)r.ea);
    trace_mem("R16-EA", r.raw, (uae_u32)v);
    return v;
  }
  uae_u8* b = r.host;
  uint16_t v = (b[0] << 8) | (b[1]);
  trace_mem("R16", r.raw, (uae_u32)v);
  return v;
}

uint8_t do_get_mem_byte(uint8_t* a) {
  mem_ptr_ref r = resolve_mem_ptr(a, 1);
  if (r.mode == mem_ptr_mode::ea32) {
    uint8_t v = (uint8_t)read_byte((unsigned int)r.ea);
    trace_mem("R08-EA", r.raw, (uae_u32)v);
    return v;
  }
  uint8_t v = *r.host;
  trace_mem("R08", r.raw, (uae_u32)v);
  return v;
}

void do_put_mem_long(uae_u32* a, uae_u32 v) {
  mem_ptr_ref r = resolve_mem_ptr(a, 4);
  if (r.mode == mem_ptr_mode::ea32) {
    m68k_write_memory_32((unsigned int)r.ea, (unsigned int)v);
    trace_mem("W32-EA", r.raw, v);
    return;
  }
  uae_u8* b = r.host;
  b[0] = (v >> 24) & 0xFF;
  b[1] = (v >> 16) & 0xFF;
  b[2] = (v >>  8) & 0xFF;
  b[3] = (v >>  0) & 0xFF;
  trace_mem("W32", r.raw, v);
}

void do_put_mem_word(uint16_t* a, uint16_t v) {
  mem_ptr_ref r = resolve_mem_ptr(a, 2);
  if (r.mode == mem_ptr_mode::ea32) {
    m68k_write_memory_16((unsigned int)r.ea, (unsigned int)v);
    trace_mem("W16-EA", r.raw, (uae_u32)v);
    return;
  }
  uae_u8* b = r.host;
  b[0] = (v >> 8) & 0xFF;
  b[1] = (v >> 0) & 0xFF;
  trace_mem("W16", r.raw, (uae_u32)v);
}

void do_put_mem_byte(uint8_t* a, uint8_t v) {
  mem_ptr_ref r = resolve_mem_ptr(a, 1);
  if (r.mode == mem_ptr_mode::ea32) {
    m68k_write_memory_8((unsigned int)r.ea, (unsigned int)v);
    trace_mem("W08-EA", r.raw, (uae_u32)v);
    return;
  }
  *r.host = v;
  trace_mem("W08", r.raw, (uae_u32)v);
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

static inline bool softreset_no_sidefx_enabled(void) {
  const char* e = getenv("PISTORM_SOFTRESET_NO_SIDEFX");
  return e && atoi(e) != 0;
}

extern "C" void reset_autoconfig(void) {
  if (cfg && cfg->platform && cfg->platform->handle_reset) {
    cfg->platform->handle_reset(cfg);
  }
}

void custom_reset(bool hardreset, bool keyboardreset) {
  if (reset_trace_enabled()) {
    uaecptr pc = m68k_getpc();
    uae_u16 opcode = (uae_u16)read_word((unsigned int)pc);
    const char* kind = hardreset ? "hard" : (keyboardreset ? "keyboard" : "soft");
    printf("[UAE-RESET] kind=%s pc=%08X opcode=%04X caller=%p\n",
           kind,
           (unsigned int)pc,
           (unsigned int)opcode,
           reset_caller_hint());
  }
  printf("custom_reset\n");
  if (!hardreset && !keyboardreset && softreset_no_sidefx_enabled()) {
    return;
  }
  reset_autoconfig();
}

// Provide a minimal intlev() to satisfy newcpu references.
extern "C" int read_irq;
extern "C" int intlev(void) {
  return read_irq;
}

bool is_cycle_ce(uaecptr) {
  return false;
}
