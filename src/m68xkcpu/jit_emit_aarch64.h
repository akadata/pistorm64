/*
 * JIT AArch64 Emitter Interface
 *
 * Single source of truth:
 * - one emitter context type
 * - one external implementation in jit_emit_aarch64.c
 */

#ifndef JIT_EMIT_AARCH64_H
#define JIT_EMIT_AARCH64_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* AArch64 register assignments for JIT */
#define AARCH64_CPU_PTR   19  /* X19: pointer to m68ki_cpu state (callee-saved) */
#define AARCH64_R0         0
#define AARCH64_R1         1
#define AARCH64_R2         2
#define AARCH64_R3         3
#define AARCH64_R4         4
#define AARCH64_R5         5
#define AARCH64_R6         6
#define AARCH64_R7         7

typedef struct {
    uint8_t *buffer;
    size_t size;
    size_t offset;
    bool error;
} jit_emit_context_t;

/* AArch64 Instruction Encodings */
#define AARCH64_MOVZ(rd, imm16, lsl)  (0xD2800000u | ((imm16) << 5) | ((lsl & 3) << 21) | ((rd) & 0x1F))
#define AARCH64_MOVK(rd, imm16, lsl)  (0xF2800000u | ((imm16) << 5) | ((lsl & 3) << 21) | ((rd) & 0x1F))

#define AARCH64_LDR_W(rt, rn, imm)    (0xB9400000u | (((imm) >> 2) << 10) | ((rn) << 5) | ((rt) & 0x1F))
#define AARCH64_STR_W(rt, rn, imm)    (0xB9000000u | (((imm) >> 2) << 10) | ((rn) << 5) | ((rt) & 0x1F))
#define AARCH64_LDR(rt, rn, imm)      (0xF9400000u | (((imm) >> 3) << 10) | ((rn) << 5) | ((rt) & 0x1F))
#define AARCH64_STR(rt, rn, imm)      (0xF9000000u | (((imm) >> 3) << 10) | ((rn) << 5) | ((rt) & 0x1F))

#define AARCH64_LDRB(rt, rn, imm)     (0x39400000u | ((imm) << 10) | ((rn) << 5) | ((rt) & 0x1F))
#define AARCH64_STRB(rt, rn, imm)     (0x39000000u | ((imm) << 10) | ((rn) << 5) | ((rt) & 0x1F))

#define AARCH64_LDRH(rt, rn, imm)     (0x79400000u | (((imm) >> 1) << 10) | ((rn) << 5) | ((rt) & 0x1F))
#define AARCH64_STRH(rt, rn, imm)     (0x79000000u | (((imm) >> 1) << 10) | ((rn) << 5) | ((rt) & 0x1F))

#define AARCH64_ADD(rd, rn, rm)       (0x8B000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_SUB(rd, rn, rm)       (0xCB000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_ADDS(rd, rn, rm)      (0xAB000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_SUBS(rd, rn, rm)      (0xEB000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))

#define AARCH64_AND(rd, rn, rm)       (0x8A000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_ORR(rd, rn, rm)       (0xAA000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_EOR(rd, rn, rm)       (0xCA000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))

#define AARCH64_AND_W(rd, rn, rm)     (0x0A000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_ORR_W(rd, rn, rm)     (0x2A000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_EOR_W(rd, rn, rm)     (0x4A000000u | ((rm) << 16) | ((rn) << 5) | ((rd) & 0x1F))

#define AARCH64_B(imm)                (0x14000000u | ((imm) & 0x03FFFFFF))
#define AARCH64_BL(imm)               (0x94000000u | ((imm) & 0x03FFFFFF))
#define AARCH64_BLR(rn)               (0xD63F0000u | (((rn) & 0x1F) << 5))
#define AARCH64_BCOND(cond, imm)      (0x54000000u | ((cond) & 0xF) | (((imm) & 0x7FFFF) << 5))
#define AARCH64_CBZ(rt, imm)          (0x34000000u | ((((uint32_t)(imm) >> 2) & 0x7FFFFu) << 5) | ((rt) & 0x1F))
#define AARCH64_CBNZ(rt, imm)         (0x35000000u | ((((uint32_t)(imm) >> 2) & 0x7FFFFu) << 5) | ((rt) & 0x1F))

#define AARCH64_COND_EQ  0x0
#define AARCH64_COND_NE  0x1
#define AARCH64_COND_CS  0x2
#define AARCH64_COND_CC  0x3
#define AARCH64_COND_MI  0x4
#define AARCH64_COND_PL  0x5
#define AARCH64_COND_VS  0x6
#define AARCH64_COND_VC  0x7
#define AARCH64_COND_HI  0x8
#define AARCH64_COND_LS  0x9
#define AARCH64_COND_GE  0xA
#define AARCH64_COND_LT  0xB
#define AARCH64_COND_GT  0xC
#define AARCH64_COND_LE  0xD

#define AARCH64_RET                  0xD65F03C0u
#define AARCH64_NOP                  0xD503201Fu
#define AARCH64_MRS_NZCV(rt)         (0xD53B4200u | ((rt) & 0x1F))
#define AARCH64_REV(rd, rn)          (0xDAC00C00u | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_LSL(rd, rn, shift)   (0x53000000u | ((((32u - ((uint32_t)(shift) & 0x1Fu)) & 0x1Fu)) << 16) | ((((31u - ((uint32_t)(shift) & 0x1Fu)) & 0x1Fu)) << 10) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_LSR(rd, rn, shift)   (0x53000000u | ((((uint32_t)(shift)) & 0x1Fu) << 16) | (31u << 10) | ((rn) << 5) | ((rd) & 0x1F))
#define AARCH64_ASR(rd, rn, shift)   (0x13000000u | ((((uint32_t)(shift)) & 0x1Fu) << 16) | (31u << 10) | ((rn) << 5) | ((rd) & 0x1F))

void jit_emit_init(jit_emit_context_t *ctx, uint8_t *buffer, size_t size);
void jit_emit_byte(jit_emit_context_t *ctx, uint8_t byte);
void jit_emit_word(jit_emit_context_t *ctx, uint16_t word);
void jit_emit_dword(jit_emit_context_t *ctx, uint32_t dword);

void jit_emit_mov64(jit_emit_context_t *ctx, uint8_t rd, uint64_t imm);
void jit_emit_load_cpu_reg(jit_emit_context_t *ctx, uint8_t rt, int offset);
void jit_emit_store_cpu_reg(jit_emit_context_t *ctx, uint8_t rt, int offset);

void jit_emit_load_dn(jit_emit_context_t *ctx, uint8_t rt, int dn);
void jit_emit_store_dn(jit_emit_context_t *ctx, uint8_t rt, int dn);
void jit_emit_load_an(jit_emit_context_t *ctx, uint8_t rt, int an);
void jit_emit_store_an(jit_emit_context_t *ctx, uint8_t rt, int an);
void jit_emit_load_pc(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_store_pc(jit_emit_context_t *ctx, uint8_t rt);

void jit_emit_store_itt0(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_load_itt0(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_store_itt1(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_load_itt1(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_store_dtt0(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_load_dtt0(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_store_dtt1(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_load_dtt1(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_store_tc(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_load_tc(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_store_acr(jit_emit_context_t *ctx, uint8_t rt, int acr_num);
void jit_emit_load_acr(jit_emit_context_t *ctx, uint8_t rt, int acr_num);

void jit_emit_load_sr(jit_emit_context_t *ctx, uint8_t rt);
void jit_emit_store_sr(jit_emit_context_t *ctx, uint8_t rt);

void jit_emit_prologue(jit_emit_context_t *ctx);
void jit_emit_epilogue(jit_emit_context_t *ctx);
void jit_emit_inc_pc(jit_emit_context_t *ctx, int instr_size);
void jit_emit_unimplemented(jit_emit_context_t *ctx, uint16_t opcode);
void jit_emit_store_nzcv_flags(jit_emit_context_t *ctx, int invert_carry);
void jit_emit_call_read8(jit_emit_context_t *ctx, uint8_t dst_reg, uint8_t addr_reg);
void jit_emit_call_read16(jit_emit_context_t *ctx, uint8_t dst_reg, uint8_t addr_reg);
void jit_emit_call_read32(jit_emit_context_t *ctx, uint8_t dst_reg, uint8_t addr_reg);

#endif /* JIT_EMIT_AARCH64_H */
