/*
 * JIT Test Memory Model
 * 
 * Flat, deterministic memory for ProcessorTests validation harness.
 * 
 * NO PiStorm backend. NO hardware. NO memory map complexity.
 * 
 * This provides a clean oracle for instruction semantics validation.
 */

#ifndef JIT_MEM_TEST_H
#define JIT_MEM_TEST_H

#include <stdint.h>
#include <string.h>

/* ======================================================================== */
/* ====================== Test Memory Configuration ======================= */
/* ======================================================================== */

/* Flat 16MB memory space - enough for all ProcessorTests */
#define JIT_TEST_MEM_SIZE (16 * 1024 * 1024)

/* Test memory context - one per test case */
typedef struct {
    uint8_t ram[JIT_TEST_MEM_SIZE];
    uint32_t ram_initialized;  /* Bitmask of initialized 256KB blocks */
} jit_test_mem_t;

/* Global test memory (set before each test) */
extern jit_test_mem_t *g_jit_test_mem;

/* ======================================================================== */
/* ====================== Memory Initialization =========================== */
/* ======================================================================== */

static inline void jit_test_mem_init(jit_test_mem_t *mem)
{
    memset(mem, 0, sizeof(*mem));
    g_jit_test_mem = mem;
}

static inline void jit_test_mem_load(jit_test_mem_t *mem, uint32_t addr, const uint8_t *data, uint32_t size)
{
    if (addr + size <= JIT_TEST_MEM_SIZE) {
        memcpy(mem->ram + addr, data, size);
        mem->ram_initialized |= (1u << (addr >> 18));  /* Mark 256KB block as initialized */
    }
}

/* ======================================================================== */
/* ====================== Memory Access (Flat, No Backend) ================ */
/* ======================================================================== */

/* 
 * Direct memory access - no PiStorm backend, no hardware.
 * Function codes are ignored in test mode (no MMU, no protection).
 */

static inline int jit_mem_read8(uint32_t addr, uint8_t fc, uint8_t *value)
{
    (void)fc;  /* Ignored in test mode */
    if (addr >= JIT_TEST_MEM_SIZE) return -1;
    *value = g_jit_test_mem->ram[addr];
    return 0;
}

static inline int jit_mem_read16(uint32_t addr, uint8_t fc, uint16_t *value)
{
    (void)fc;
    if (addr + 1 >= JIT_TEST_MEM_SIZE) return -1;
    *value = (g_jit_test_mem->ram[addr] << 8) | g_jit_test_mem->ram[addr + 1];
    return 0;
}

static inline int jit_mem_read32(uint32_t addr, uint8_t fc, uint32_t *value)
{
    (void)fc;
    if (addr + 3 >= JIT_TEST_MEM_SIZE) return -1;
    *value = (g_jit_test_mem->ram[addr] << 24) | 
             (g_jit_test_mem->ram[addr + 1] << 16) |
             (g_jit_test_mem->ram[addr + 2] << 8) |
             g_jit_test_mem->ram[addr + 3];
    return 0;
}

static inline int jit_mem_write8(uint32_t addr, uint8_t value, uint8_t fc)
{
    (void)fc;
    if (addr >= JIT_TEST_MEM_SIZE) return -1;
    g_jit_test_mem->ram[addr] = value;
    return 0;
}

static inline int jit_mem_write16(uint32_t addr, uint16_t value, uint8_t fc)
{
    (void)fc;
    if (addr + 1 >= JIT_TEST_MEM_SIZE) return -1;
    g_jit_test_mem->ram[addr] = value >> 8;
    g_jit_test_mem->ram[addr + 1] = value & 0xFF;
    return 0;
}

static inline int jit_mem_write32(uint32_t addr, uint32_t value, uint8_t fc)
{
    (void)fc;
    if (addr + 3 >= JIT_TEST_MEM_SIZE) return -1;
    g_jit_test_mem->ram[addr] = value >> 24;
    g_jit_test_mem->ram[addr + 1] = (value >> 16) & 0xFF;
    g_jit_test_mem->ram[addr + 2] = (value >> 8) & 0xFF;
    g_jit_test_mem->ram[addr + 3] = value & 0xFF;
    return 0;
}

/* ======================================================================== */
/* ====================== Instruction Fetch =============================== */
/* ======================================================================== */

static inline uint16_t jit_fetch_word(uint32_t pc, uint8_t fc)
{
    uint16_t value;
    jit_mem_read16(pc, fc, &value);
    return value;
}

static inline uint32_t jit_fetch_long(uint32_t pc, uint8_t fc)
{
    uint32_t value;
    jit_mem_read32(pc, fc, &value);
    return value;
}

#endif /* JIT_MEM_TEST_H */
