/*
 * JIT PiStorm Memory Backend
 * 
 * Real Amiga memory access via PiStorm backend for the emulator.
 * 
 * This is used when the JIT runs in the actual PiStorm hardware environment.
 */

#ifndef JIT_MEM_PISTORM_H
#define JIT_MEM_PISTORM_H

#include <stdint.h>
#include "../pistorm/backend.h"

/* ======================================================================== */
/* ====================== PiStorm Backend Memory Access =================== */
/* ======================================================================== */

/* 
 * Real Amiga memory access via PiStorm backend.
 * Handles:
 * - Big Endian ↔ Little Endian conversion
 * - Function codes (FC0-FC2)
 * - Bus timing and hardware behavior
 */

static inline int jit_mem_read8(uint32_t addr, uint8_t fc, uint8_t *value)
{
    return ps_backend_read8(addr, fc, value);
}

static inline int jit_mem_read16(uint32_t addr, uint8_t fc, uint16_t *value)
{
    return ps_backend_read16(addr, fc, value);
}

static inline int jit_mem_read32(uint32_t addr, uint8_t fc, uint32_t *value)
{
    return ps_backend_read32(addr, fc, value);
}

static inline int jit_mem_write8(uint32_t addr, uint8_t value, uint8_t fc)
{
    return ps_backend_write8(addr, value, fc);
}

static inline int jit_mem_write16(uint32_t addr, uint16_t value, uint8_t fc)
{
    return ps_backend_write16(addr, value, fc);
}

static inline int jit_mem_write32(uint32_t addr, uint32_t value, uint8_t fc)
{
    return ps_backend_write32(addr, value, fc);
}

/* ======================================================================== */
/* ====================== Instruction Fetch =============================== */
/* ======================================================================== */

/* 
 * Fetch instruction from PC.
 * In real hardware, this may involve prefetch buffers.
 */

static inline uint16_t jit_fetch_word(uint32_t pc, uint8_t fc)
{
    uint16_t value;
    ps_backend_read16(pc, fc, &value);
    return value;
}

static inline uint32_t jit_fetch_long(uint32_t pc, uint8_t fc)
{
    uint32_t value;
    ps_backend_read32(pc, fc, &value);
    return value;
}

#endif /* JIT_MEM_PISTORM_H */
