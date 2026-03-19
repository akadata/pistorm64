/*
 * JIT AArch64 Emitter - Unified Model
 * 
 * Conventions:
 * - X19 holds pointer to m68ki_cpu state structure
 * - Translators append to emit context (no buffer allocation)
 * - Single block builder manages instruction_count and code_ptr
 */

#ifndef JIT_EMIT_AARCH64_H
#define JIT_EMIT_AARCH64_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* AArch64 register assignments for JIT */
#define AARCH64_CPU_PTR   19  /* X19: pointer to m68ki_cpu state (callee-saved) */
#define AARCH64_R0         0  /* Temporary / return value */
#define AARCH64_R1         1  /* Temporary */
#define AARCH64_R2         2  /* Temporary */
#define AARCH64_R3         3  /* Temporary */
#define AARCH64_R4         4  /* Temporary */
#define AARCH64_R5         5  /* Temporary */
#define AARCH64_R6         6  /* Temporary */

/* Emitter context - translators append to this */
typedef struct {
    uint8_t *buffer;        /* Code buffer (allocated by block builder) */
    size_t size;            /* Buffer size */
    size_t offset;          /* Current offset (where next instruction goes) */
    bool error;             /* Error flag */
} jit_emit_context_t;

/* Initialize emitter context */
static inline void jit_emit_init(jit_emit_context_t *ctx, uint8_t *buffer, size_t size)
{
    ctx->buffer = buffer;
    ctx->size = size;
    ctx->offset = 0;
    ctx->error = false;
}

/* Emit raw bytes */
static inline void jit_emit_byte(jit_emit_context_t *ctx, uint8_t byte)
{
    if (ctx->offset >= ctx->size) {
        ctx->error = true;
        return;
    }
    ctx->buffer[ctx->offset++] = byte;
}

static inline void jit_emit_word(jit_emit_context_t *ctx, uint16_t word)
{
    jit_emit_byte(ctx, word & 0xFF);
    jit_emit_byte(ctx, (word >> 8) & 0xFF);
}

static inline void jit_emit_dword(jit_emit_context_t *ctx, uint32_t dword)
{
    jit_emit_word(ctx, dword & 0xFFFF);
    jit_emit_word(ctx, (dword >> 16) & 0xFFFF);
}

/* ============================================================================
 * AArch64 Instruction Encodings
 * ============================================================================ */

/* MOVZ/MOVK - load immediate */
#define AARCH64_MOVZ(rd, imm16, lsl)  (0xD2800000u | ((imm16) << 5) | ((lsl & 3) << 21) | ((rd) & 0x1F))
#define AARCH64_MOVK(rd, imm16, lsl)  (0xF2800000u | ((imm16) << 5) | ((lsl & 3) << 21) | ((rd) & 0x1F))

/* Load/Store with immediate offset */
#define AARCH64_LDR_W(rt, rn, imm)    (0xB9400000u | (((imm) >> 2) << 10) | ((rn) << 5) | ((rt) & 0x1F))
#define AARCH64_STR_W(rt, rn, imm)    (0xB9000000u | (((imm) >> 2) << 10) | ((rn) << 5) | ((rt) & 0x1F))
#define AARCH64_LDR(rt, rn, imm)      (0xF9400000u | (((imm) >> 3) << 10) | ((rn) << 5) | ((rt) & 0x1F))
#define AARCH64_STR(rt, rn, imm)      (0xF9000000u | (((imm) >> 3) << 10) | ((rn) << 5) | ((rt) & 0x1F))

/* Load/Store byte */
#define AARCH64_LDRB(rt, rn, imm)     (0x39400000u | ((imm) << 10) | ((rn) << 5) | ((rt) & 0x1F))
#define AARCH64_STRB(rt, rn, imm)     (0x39000000u | ((imm) << 10) | ((rn) << 5) | ((rt) & 0x1F))

/* Load/Store halfword */
#define AARCH64_LDRH(rt, rn, imm)     (0x79400000u | (((imm) >> 1) << 10) | ((rn) << 5) | ((rt) & 0x1F))
#define AARCH64_STRH(rt, rn, imm)     (0x79000000u | (((imm) >> 1) << 10) | ((rn) << 5) | ((rt) & 0x1F))

/* Arithmetic */
#define AARCH64_ADD(rd, rn, rm)       (0x8B000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_SUB(rd, rn, rm)       (0xCB000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_ADDS(rd, rn, rm)      (0xAB000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_SUBS(rd, rn, rm)      (0xEB000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))

/* Logical - 64-bit */
#define AARCH64_AND(rd, rn, rm)       (0x8A000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_ORR(rd, rn, rm)       (0xAA000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_EOR(rd, rn, rm)       (0xCA000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))

/* Logical - 32-bit */
#define AARCH64_AND_W(rd, rn, rm)     (0x2A000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_ORR_W(rd, rn, rm)     (0x6A000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_EOR_W(rd, rn, rm)     (0x4A000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))

/* Branch */
#define AARCH64_B(imm)                (0x14000000u | ((imm) & 0x03FFFFFF))
#define AARCH64_BL(imm)               (0x94000000u | ((imm) & 0x03FFFFFF))
#define AARCH64_BCOND(cond, imm)      (0x54000000u | ((cond) & 0xF) | (((imm) & 0x7FFFF) << 5))
#define AARCH64_CBZ(rt, imm)          (0x34000000u | (((rt) & 0x1F) << 5) | (((imm >> 2) & 0x7FFFF) << 5))
#define AARCH64_CBNZ(rt, imm)         (0x35000000u | (((rt) & 0x1F) << 5) | (((imm >> 2) & 0x7FFFF) << 5))

/* Condition codes */
#define AARCH64_COND_EQ  0x0  /* Equal */
#define AARCH64_COND_NE  0x1  /* Not equal */
#define AARCH64_COND_CS  0x2  /* Carry set */
#define AARCH64_COND_CC  0x3  /* Carry clear */
#define AARCH64_COND_MI  0x4  /* Minus */
#define AARCH64_COND_PL  0x5  /* Plus */
#define AARCH64_COND_VS  0x6  /* Overflow set */
#define AARCH64_COND_VC  0x7  /* Overflow clear */
#define AARCH64_COND_HI  0x8  /* Unsigned higher */
#define AARCH64_COND_LS  0x9  /* Unsigned lower or same */
#define AARCH64_COND_GE  0xA  /* Signed >= */
#define AARCH64_COND_LT  0xB  /* Signed < */
#define AARCH64_COND_GT  0xC  /* Signed > */
#define AARCH64_COND_LE  0xD  /* Signed <= */

/* Return */
#define AARCH64_RET                     0xD65F03C0u

/* No-op */
#define AARCH64_NOP                     0xD503201Fu

/* Byte reverse */
#define AARCH64_REV(rd, rn)             (0xDAC00C00u | ((rn) << 5) | ((rd) & 0x1F))

/* Shifts */
#define AARCH64_LSL(rd, rn, shift)      (0xD320F800u | (((shift) & 0x3F) << 10) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_LSR(rd, rn, shift)      (0xD320FC00u | (((shift) & 0x3F) << 10) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_ASR(rd, rn, shift)      (0xD340FC00u | (((shift) & 0x3F) << 10) | ((rn) << 5) | ((rd) & 0x1F))

/* ============================================================================
 * High-level emit functions
 * ============================================================================ */

/* Load 64-bit immediate */
static inline void jit_emit_mov64(jit_emit_context_t *ctx, uint8_t rd, uint64_t imm)
{
    if (imm == 0) {
        jit_emit_dword(ctx, AARCH64_EOR(rd, rd, rd));
        return;
    }
    
    jit_emit_dword(ctx, AARCH64_MOVZ(rd, imm & 0xFFFF, 0));
    
    for (int shift = 16; shift < 64; shift += 16) {
        uint16_t chunk = (imm >> shift) & 0xFFFF;
        if (chunk != 0) {
            jit_emit_dword(ctx, AARCH64_MOVK(rd, chunk, shift / 16));
        }
    }
}

/* Load CPU state register (from m68ki_cpu structure) */
static inline void jit_emit_load_cpu_reg(jit_emit_context_t *ctx, uint8_t rt, int offset)
{
    jit_emit_dword(ctx, AARCH64_LDR(rt, AARCH64_CPU_PTR, offset));
}

/* Store to CPU state register */
static inline void jit_emit_store_cpu_reg(jit_emit_context_t *ctx, uint8_t rt, int offset)
{
    jit_emit_dword(ctx, AARCH64_STR(rt, AARCH64_CPU_PTR, offset));
}

/* Load CPU Dn register (32-bit) */
static inline void jit_emit_load_dn(jit_emit_context_t *ctx, uint8_t rt, int dn)
{
    jit_emit_load_cpu_reg(ctx, rt, dn * 4);
}

/* Store to CPU Dn register (32-bit) */
static inline void jit_emit_store_dn(jit_emit_context_t *ctx, uint8_t rt, int dn)
{
    jit_emit_store_cpu_reg(ctx, rt, dn * 4);
}

/* Load CPU An register (32-bit) */
static inline void jit_emit_load_an(jit_emit_context_t *ctx, uint8_t rt, int an)
{
    jit_emit_load_cpu_reg(ctx, rt, 64 + an * 4);
}

/* Store to CPU An register (32-bit) */
static inline void jit_emit_store_an(jit_emit_context_t *ctx, uint8_t rt, int an)
{
    jit_emit_store_cpu_reg(ctx, rt, 64 + an * 4);
}

/* Load PC (32-bit) - pc is at offset 132 (after dar[16]=64, dar_save[16]=64, ppc=4) */
static inline void jit_emit_load_pc(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_dword(ctx, AARCH64_LDR_W(rt, AARCH64_CPU_PTR, 132));
}

/* Store PC (32-bit) */
static inline void jit_emit_store_pc(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_dword(ctx, AARCH64_STR_W(rt, AARCH64_CPU_PTR, 132));
}

/* 68040 MMU Register access */
/* ITT0/ITT1/DTT0/DTT1 at offsets 320/324/328/332, TC at 336, ACR0-3 at 340/344/348/352 */
static inline void jit_emit_store_itt0(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_store_cpu_reg(ctx, rt, 320);
}
static inline void jit_emit_load_itt0(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_load_cpu_reg(ctx, rt, 320);
}
static inline void jit_emit_store_itt1(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_store_cpu_reg(ctx, rt, 324);
}
static inline void jit_emit_load_itt1(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_load_cpu_reg(ctx, rt, 324);
}
static inline void jit_emit_store_dtt0(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_store_cpu_reg(ctx, rt, 328);
}
static inline void jit_emit_load_dtt0(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_load_cpu_reg(ctx, rt, 328);
}
static inline void jit_emit_store_dtt1(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_store_cpu_reg(ctx, rt, 332);
}
static inline void jit_emit_load_dtt1(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_load_cpu_reg(ctx, rt, 332);
}
static inline void jit_emit_store_tc(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_store_cpu_reg(ctx, rt, 336);
}
static inline void jit_emit_load_tc(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_load_cpu_reg(ctx, rt, 336);
}
static inline void jit_emit_store_acr(jit_emit_context_t *ctx, uint8_t rt, int acr_num)
{
    jit_emit_store_cpu_reg(ctx, rt, 340 + acr_num * 4);
}
static inline void jit_emit_load_acr(jit_emit_context_t *ctx, uint8_t rt, int acr_num)
{
    jit_emit_load_cpu_reg(ctx, rt, 340 + acr_num * 4);
}

/* Load SR/CCR (16-bit) */
static inline void jit_emit_load_sr(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_dword(ctx, AARCH64_LDRH(rt, AARCH64_CPU_PTR, 192));
}

/* Store SR/CCR (16-bit) */
static inline void jit_emit_store_sr(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_dword(ctx, AARCH64_STRH(rt, AARCH64_CPU_PTR, 192));
}

/* Prologue - save callee-saved registers, load CPU pointer */
static inline void jit_emit_prologue(jit_emit_context_t *ctx)
{
    /* Save X19-X28 (callee-saved) - simplified for now */
    /* X19 should already contain &m68ki_cpu from the caller */
}

/* Epilogue - restore callee-saved registers, return */
static inline void jit_emit_epilogue(jit_emit_context_t *ctx)
{
    /* Return 0 (cycles used - placeholder) */
    jit_emit_mov64(ctx, AARCH64_R0, 0);
    
    /* RET */
    jit_emit_dword(ctx, AARCH64_RET);
}

/* Increment PC by instruction size (2 + ext_count*2) */
static inline void jit_emit_inc_pc(jit_emit_context_t *ctx, int instr_size)
{
    /* Load PC (offset 132) - use LDR_W for 32-bit */
    jit_emit_dword(ctx, AARCH64_LDR_W(AARCH64_R0, AARCH64_CPU_PTR, 132));
    /* Add instruction size */
    jit_emit_mov64(ctx, AARCH64_R1, instr_size);
    jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
    /* Store PC - use STR_W for 32-bit */
    jit_emit_dword(ctx, AARCH64_STR_W(AARCH64_R0, AARCH64_CPU_PTR, 132));
}

/* Emit unimplemented instruction handler */
static inline void jit_emit_unimplemented(jit_emit_context_t *ctx, uint16_t opcode)
{
    jit_emit_dword(ctx, AARCH64_NOP);
    (void)opcode;
}

#endif /* JIT_EMIT_AARCH64_H */
