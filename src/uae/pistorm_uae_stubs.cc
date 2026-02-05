// SPDX-License-Identifier: MIT

#include "sysconfig.h"
#include "sysdeps.h"
#include "uae/types.h"
#include "memory.h"
#include "newcpu.h"
#include "machdep/maccess.h"

extern "C" {
unsigned int read_long(unsigned int address);
unsigned int read_word(unsigned int address);
unsigned int read_byte(unsigned int address);
void m68k_write_memory_8(unsigned int address, unsigned int value);
void m68k_write_memory_16(unsigned int address, unsigned int value);
void m68k_write_memory_32(unsigned int address, unsigned int value);
}

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

// CPU indirect memory helpers used by UAE.
uae_u32 do_get_mem_long(uae_u32* a) {
  return (uae_u32)read_long((unsigned int)a);
}

uint16_t do_get_mem_word(uint16_t* a) {
  return (uint16_t)read_word((unsigned int)a);
}

uint8_t do_get_mem_byte(uint8_t* a) {
  return (uint8_t)read_byte((unsigned int)a);
}

void do_put_mem_long(uae_u32* a, uae_u32 v) {
  m68k_write_memory_32((unsigned int)a, (unsigned int)v);
}

void do_put_mem_word(uint16_t* a, uint16_t v) {
  m68k_write_memory_16((unsigned int)a, (unsigned int)v);
}

void do_put_mem_byte(uint8_t* a, uint8_t v) {
  m68k_write_memory_8((unsigned int)a, (unsigned int)v);
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

bool is_cycle_ce(uaecptr) {
  return false;
}
