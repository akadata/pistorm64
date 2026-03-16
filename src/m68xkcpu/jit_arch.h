/*
 * JIT Architecture Notes
 * 
 * CRITICAL: There are TWO distinct execution environments:
 * 
 * 1. VALIDATION HARNESS (Test Environment)
 *    - Flat memory buffer (deterministic, no hardware)
 *    - No PiStorm backend
 *    - Pure 68000 instruction semantics
 *    - ProcessorTests JSON → flat RAM → Musashi/JIT → compare state
 * 
 * 2. REAL EMULATOR (PiStorm Runtime)
 *    - PiStorm backend (src/pistorm/backend.*)
 *    - Real Amiga memory map, MMU, chipset
 *    - Function codes, bus behavior, timing
 *    - JIT → m68k_read/write → PiStorm backend → Amiga memory
 * 
 * THESE MUST NOT BE MIXED.
 * 
 * The validation harness exists to validate instruction semantics
 * in isolation from hardware complexity.
 * 
 * The real emulator integrates the validated JIT with actual hardware.
 */

#ifndef JIT_ARCH_H
#define JIT_ARCH_H

/* ======================================================================== */
/* ====================== Environment Detection =========================== */
/* ======================================================================== */

/* 
 * JIT_ENV_TEST     - Validation harness with flat memory
 * JIT_ENV_PISTORM  - Real emulator with PiStorm backend
 * 
 * Define JIT_ENV_TEST when building for ProcessorTests validation.
 * Define JIT_ENV_PISTORM (or neither) for real emulator.
 */

#if defined(JIT_ENV_TEST)
    /* Validation harness - flat memory model */
    #include "jit_mem_test.h"
#elif defined(JIT_ENV_PISTORM) || !defined(JIT_ENV_TEST)
    /* Real emulator - PiStorm backend */
    #include "jit_mem_pistorm.h"
#else
    #error "JIT environment not defined - set JIT_ENV_TEST or JIT_ENV_PISTORM"
#endif

/* ======================================================================== */
/* ====================== Common JIT Interface ============================ */
/* ======================================================================== */

/* 
 * The JIT translator uses these abstract memory operations.
 * The actual implementation depends on the environment.
 * 
 * In TEST mode: direct access to flat test memory
 * In PISTORM mode: calls to PiStorm backend
 */

/* Read from 68000 memory space */
int jit_mem_read8(uint32_t addr, uint8_t fc, uint8_t *value);
int jit_mem_read16(uint32_t addr, uint8_t fc, uint16_t *value);
int jit_mem_read32(uint32_t addr, uint8_t fc, uint32_t *value);

/* Write to 68000 memory space */
int jit_mem_write8(uint32_t addr, uint8_t value, uint8_t fc);
int jit_mem_write16(uint32_t addr, uint16_t value, uint8_t fc);
int jit_mem_write32(uint32_t addr, uint32_t value, uint8_t fc);

/* Fetch instruction from PC (handles prefetch buffer in real hardware) */
uint16_t jit_fetch_word(uint32_t pc, uint8_t fc);
uint32_t jit_fetch_long(uint32_t pc, uint8_t fc);

/* ======================================================================== */
/* ====================== Function Codes ================================== */
/* ======================================================================== */

#define JIT_FC_USER_DATA        0
#define JIT_FC_USER_PROGRAM     1
#define JIT_FC_SUPERVISOR_DATA  5
#define JIT_FC_SUPERVISOR_PROG  6

/* Helper to get FC from CPU state */
static inline uint8_t jit_get_fc(int is_supervisor, int is_program)
{
    if (is_supervisor) {
        return is_program ? JIT_FC_SUPERVISOR_PROG : JIT_FC_SUPERVISOR_DATA;
    }
    return is_program ? JIT_FC_USER_PROGRAM : JIT_FC_USER_DATA;
}

/* ======================================================================== */
/* ====================== JIT Cache (Always Native) ======================= */
/* ======================================================================== */

/* 
 * JIT compiled code cache is ALWAYS in native AArch64 memory.
 * This is true for both TEST and PISTORM environments.
 * 
 * No byte swapping needed for JIT internal operations.
 */

static inline uint16_t jit_cache_read16(const uint8_t *cache, uint32_t offset)
{
    return *(uint16_t*)(cache + offset);
}

static inline uint32_t jit_cache_read32(const uint8_t *cache, uint32_t offset)
{
    return *(uint32_t*)(cache + offset);
}

static inline void jit_cache_write16(uint8_t *cache, uint32_t offset, uint16_t value)
{
    *(uint16_t*)(cache + offset) = value;
}

static inline void jit_cache_write32(uint8_t *cache, uint32_t offset, uint32_t value)
{
    *(uint32_t*)(cache + offset) = value;
}

/* AArch64 byte-swap instructions for register operations */
#define AARCH64_REV_Wd_Wn(Wd, Wn)  (0xDAC00C00 | ((Wn) << 5) | (Wd))
#define AARCH64_REVH_Wd_Wn(Wd, Wn) (0xDAC00800 | ((Wn) << 5) | (Wd))

#endif /* JIT_ARCH_H */
