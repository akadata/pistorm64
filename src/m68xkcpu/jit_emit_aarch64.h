/*
 * JIT AArch64 Emitter Header
 * 
 * Low-level AArch64 code emission utilities.
 */

#ifndef JIT_EMIT_AARCH64_H
#define JIT_EMIT_AARCH64_H

#include <stdint.h>
#include <stdbool.h>
#include "jit.h"
#include "jit_block.h"

/* Emitter context */
typedef struct {
    uint8_t *buffer;        /* Code buffer */
    size_t size;            /* Buffer size */
    size_t offset;          /* Current offset */
    bool error;             /* Error flag */
    
    /* Register allocation tracking */
    uint32_t used_regs;     /* Bitmap of used registers */
    
    /* Current block being emitted */
    jit_block_t *block;
} jit_emit_context_t;

/* AArch64 register definitions */
#define AARCH64_R0      0
#define AARCH64_R1      1
#define AARCH64_R2      2
#define AARCH64_R3      3
#define AARCH64_R4      4
#define AARCH64_R5      5
#define AARCH64_R6      6
#define AARCH64_R7      7
#define AARCH64_R8      8
#define AARCH64_R9      9
#define AARCH64_R10     10
#define AARCH64_R11     11
#define AARCH64_R12     12
#define AARCH64_R13     13
#define AARCH64_R14     14
#define AARCH64_R15     15
#define AARCH64_R16     16  /* IP0 */
#define AARCH64_R17     17  /* IP1 */
#define AARCH64_R18     18  /* Platform register */
#define AARCH64_R19     19  /* Callee-saved */
#define AARCH64_R20     20  /* Callee-saved */
#define AARCH64_R21     21  /* Callee-saved */
#define AARCH64_R22     22  /* Callee-saved */
#define AARCH64_R23     23  /* Callee-saved */
#define AARCH64_R24     24  /* Callee-saved */
#define AARCH64_R25     25  /* Callee-saved */
#define AARCH64_R26     26  /* Callee-saved */
#define AARCH64_R27     27  /* Callee-saved */
#define AARCH64_R28     28  /* Callee-saved */
#define AARCH64_R29     29  /* FP */
#define AARCH64_R30     30  /* LR */
#define AARCH64_SP      31  /* Stack pointer */
#define AARCH64_ZR      31  /* Zero register (when reading) */

/* Proposed JIT calling convention:
 * 
 * Input:
 *   W0 = max_cycles
 * 
 * Output:
 *   W0 = cycles_used (or negative for fallback)
 * 
 * Preserved (callee-saved):
 *   X19-X28, X29 (FP), X30 (LR)
 * 
 * Clobbered (caller-saved):
 *   X0-X18
 */

/* Register assignments for JIT:
 * 
 * We'll map 68k state to AArch64 registers for performance:
 * 
 * D0-D7   -> X19-X26 (callee-saved)
 * A0-A6   -> X27-X29, X9 (A6 in X29, A7 in X9 - SP)
 * A7 (SP) -> X9  (special handling)
 * PC      -> X10
 * CCR     -> W11 (packed: XNZVC in bits 3-0)
 * 
 * For simplicity in initial implementation, we may use a 
 * CPU state structure pointer instead.
 */

/* Initialize emitter context */
void jit_emit_init(jit_emit_context_t *ctx, uint8_t *buffer, size_t size);

/* Emit raw bytes */
void jit_emit_byte(jit_emit_context_t *ctx, uint8_t byte);
void jit_emit_word(jit_emit_context_t *ctx, uint16_t word);
void jit_emit_dword(jit_emit_context_t *ctx, uint32_t dword);
void jit_emit_qword(jit_emit_context_t *ctx, uint64_t qword);

/* Emit AArch64 instructions */

/* Data processing - immediate */
void jit_emit_mov(jit_emit_context_t *ctx, uint8_t rd, uint64_t imm);
void jit_emit_movz(jit_emit_context_t *ctx, uint8_t rd, uint16_t imm, int shift);
void jit_emit_movk(jit_emit_context_t *ctx, uint8_t rd, uint16_t imm, int shift);
void jit_emit_add(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint64_t imm);
void jit_emit_sub(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint64_t imm);
void jit_emit_and(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint64_t imm);
void jit_emit_orr(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint64_t imm);
void jit_emit_eor(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint64_t imm);

/* Data processing - register */
void jit_emit_add_reg(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint8_t rm);
void jit_emit_sub_reg(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint8_t rm);
void jit_emit_and_reg(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint8_t rm);
void jit_emit_orr_reg(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint8_t rm);
void jit_emit_eor_reg(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint8_t rm);
void jit_emit_lsl(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, int shift);
void jit_emit_lsr(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, int shift);
void jit_emit_asr(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, int shift);
void jit_emit_ror(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, int shift);

/* Compare and test */
void jit_emit_cmp(jit_emit_context_t *ctx, uint8_t rn, uint64_t imm);
void jit_emit_cmp_reg(jit_emit_context_t *ctx, uint8_t rn, uint8_t rm);
void jit_emit_tst(jit_emit_context_t *ctx, uint8_t rn, uint64_t imm);
void jit_emit_tst_reg(jit_emit_context_t *ctx, uint8_t rn, uint8_t rm);

/* Conditional branches */
void jit_emit_b(jit_emit_context_t *ctx, int offset);
void jit_emit_bl(jit_emit_context_t *ctx, int offset);
void jit_emit_beq(jit_emit_context_t *ctx, int offset);
void jit_emit_bne(jit_emit_context_t *ctx, int offset);
void jit_emit_bcs(jit_emit_context_t *ctx, int offset);
void jit_emit_bcc(jit_emit_context_t *ctx, int offset);
void jit_emit_bmi(jit_emit_context_t *ctx, int offset);
void jit_emit_bpl(jit_emit_context_t *ctx, int offset);
void jit_emit_bvs(jit_emit_context_t *ctx, int offset);
void jit_emit_bvc(jit_emit_context_t *ctx, int offset);
void jit_emit_bhi(jit_emit_context_t *ctx, int offset);
void jit_emit_bls(jit_emit_context_t *ctx, int offset);
void jit_emit_bge(jit_emit_context_t *ctx, int offset);
void jit_emit_blt(jit_emit_context_t *ctx, int offset);
void jit_emit_bgt(jit_emit_context_t *ctx, int offset);
void jit_emit_ble(jit_emit_context_t *ctx, int offset);

/* Load/Store */
void jit_emit_ldr(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset);
void jit_emit_ldr_w(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset);
void jit_emit_ldr_b(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset);
void jit_emit_ldr_h(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset);
void jit_emit_str(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset);
void jit_emit_str_w(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset);
void jit_emit_str_b(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset);
void jit_emit_str_h(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset);

/* Stack operations */
void jit_emit_push(jit_emit_context_t *ctx, uint8_t reg);
void jit_emit_pop(jit_emit_context_t *ctx, uint8_t reg);

/* Prologue/Epilogue */
void jit_emit_prologue(jit_emit_context_t *ctx, jit_block_t *block);
void jit_emit_epilogue(jit_emit_context_t *ctx, jit_block_t *block);

/* Helper functions */
void jit_emit_unimplemented(jit_emit_context_t *ctx, uint16_t opcode, const char *name);
void jit_emit_comment(jit_emit_context_t *ctx, const char *comment);

/* Patch helpers */
size_t jit_emit_get_offset(jit_emit_context_t *ctx);
void jit_emit_patch_branch(jit_emit_context_t *ctx, size_t offset, int target_offset);

/* Additional helper functions */
void jit_emit_movn(jit_emit_context_t *ctx, uint8_t rd, uint16_t imm, int shift);
void jit_emit_mov_reg(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn);
void jit_emit_add_immed(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, int imm);
void jit_emit_sub_immed(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, int imm);
void jit_emit_and_immed(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint64_t imm);
void jit_emit_str_offset(jit_emit_context_t *ctx, uint8_t rn, uint8_t rt, int offset);
void jit_emit_cset(jit_emit_context_t *ctx, uint8_t rd, int condition);
void jit_emit_patch_bcond(jit_emit_context_t *ctx, size_t offset, int target_offset);
void jit_emit_patch_b(jit_emit_context_t *ctx, size_t offset, int target_offset);

#endif /* JIT_EMIT_AARCH64_H */
