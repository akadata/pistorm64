/*
 * JIT AArch64 Emitter Implementation
 * 
 * Low-level AArch64 code emission utilities.
 */

#include "jit.h"
#include "jit_emit_aarch64.h"
#include <stdio.h>
#include <string.h>

/* AArch64 instruction encodings */
#define ENC_MOVZ        0xD2800000  /* Move with zero (immediate) */
#define ENC_MOVK        0xF2800000  /* Move with keep (immediate) */
#define ENC_ADD_IMM     0x91000000  /* Add immediate */
#define ENC_SUB_IMM     0xD1000000  /* Subtract immediate */
#define ENC_AND_IMM     0x92000000  /* AND immediate */
#define ENC_ORR_IMM     0xB2000000  /* ORR immediate */
#define ENC_EOR_IMM     0xB2400000  /* EOR immediate */

#define ENC_ADD_REG     0x8B000000  /* Add register */
#define ENC_SUB_REG     0xCB000000  /* Subtract register */
#define ENC_AND_REG     0x8A000000  /* AND register */
#define ENC_ORR_REG     0xAA000000  /* ORR register */
#define ENC_EOR_REG     0xCA000000  /* EOR register */

#define ENC_LSL         0xD37FF000  /* Logical shift left */
#define ENC_LSR         0xD35FF000  /* Logical shift right */
#define ENC_ASR         0xD37FC000  /* Arithmetic shift right */
#define ENC_ROR         0x1AC02000  /* Rotate right */

#define ENC_CMP_IMM     0xF100001F  /* Compare immediate */
#define ENC_CMP_REG     0xEB00001F  /* Compare register */
#define ENC_TST_IMM     0xF200001F  /* Test immediate */
#define ENC_TST_REG     0xEA00001F  /* Test register */

#define ENC_B           0x14000000  /* Unconditional branch */
#define ENC_BL          0x94000000  /* Branch with link */
#define ENC_BCOND       0x54000000  /* Conditional branch */

#define ENC_LDR_IMM     0xF9400000  /* Load register (unscaled immediate) */
#define ENC_LDR_W_IMM   0xB9400000  /* Load register word */
#define ENC_LDR_B_IMM   0x39400000  /* Load register byte */
#define ENC_LDR_H_IMM   0x79400000  /* Load register halfword */
#define ENC_STR_IMM     0xF9000000  /* Store register */
#define ENC_STR_W_IMM   0xB9000000  /* Store register word */
#define ENC_STR_B_IMM   0x39000000  /* Store register byte */
#define ENC_STR_H_IMM   0x79000000  /* Store register halfword */


/**
 * Initialize emitter context
 */
void jit_emit_init(jit_emit_context_t *ctx, uint8_t *buffer, size_t size)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->buffer = buffer;
    ctx->size = size;
    ctx->offset = 0;
    ctx->error = false;
    ctx->used_regs = 0;
    ctx->block = NULL;
}


/**
 * Emit a single byte
 */
void jit_emit_byte(jit_emit_context_t *ctx, uint8_t byte)
{
    if (ctx->offset >= ctx->size) {
        ctx->error = true;
        return;
    }
    ctx->buffer[ctx->offset++] = byte;
}


/**
 * Emit a 16-bit word (little-endian)
 */
void jit_emit_word(jit_emit_context_t *ctx, uint16_t word)
{
    jit_emit_byte(ctx, word & 0xFF);
    jit_emit_byte(ctx, (word >> 8) & 0xFF);
}


/**
 * Emit a 32-bit dword (little-endian)
 */
void jit_emit_dword(jit_emit_context_t *ctx, uint32_t dword)
{
    jit_emit_word(ctx, dword & 0xFFFF);
    jit_emit_word(ctx, (dword >> 16) & 0xFFFF);
}


/**
 * Emit a 64-bit qword (little-endian)
 */
void jit_emit_qword(jit_emit_context_t *ctx, uint64_t qword)
{
    jit_emit_dword(ctx, qword & 0xFFFFFFFF);
    jit_emit_dword(ctx, (qword >> 32) & 0xFFFFFFFF);
}


/**
 * Emit MOVZ instruction (move with zero)
 */
void jit_emit_movz(jit_emit_context_t *ctx, uint8_t rd, uint16_t imm, int shift)
{
    uint32_t instr = ENC_MOVZ;
    int shift_enc = (shift / 16) & 3;
    
    instr |= ((uint32_t)imm << 5);
    instr |= (shift_enc << 21);
    instr |= (rd & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit MOVK instruction (move with keep)
 */
void jit_emit_movk(jit_emit_context_t *ctx, uint8_t rd, uint16_t imm, int shift)
{
    uint32_t instr = ENC_MOVK;
    int shift_enc = (shift / 16) & 3;
    
    instr |= ((uint32_t)imm << 5);
    instr |= (shift_enc << 21);
    instr |= (rd & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit MOV (alias for MOVZ with shift 0)
 */
void jit_emit_mov(jit_emit_context_t *ctx, uint8_t rd, uint64_t imm)
{
    if (imm == 0) {
        /* EOR Rn, Rn -> zero register */
        uint32_t instr = 0xCA000000 | ((rd & 0x1F) << 5) | (rd & 0x1F);
        jit_emit_dword(ctx, instr);
        return;
    }
    
    /* First 16 bits */
    jit_emit_movz(ctx, rd, imm & 0xFFFF, 0);
    
    /* Remaining 16-bit chunks */
    for (int shift = 16; shift < 64; shift += 16) {
        uint16_t chunk = (imm >> shift) & 0xFFFF;
        if (chunk != 0) {
            jit_emit_movk(ctx, rd, chunk, shift);
        }
    }
}


/**
 * Emit ADD immediate
 */
void jit_emit_add(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint64_t imm)
{
    uint32_t instr = ENC_ADD_IMM;
    
    if (imm > 0xFFF) {
        /* Need shifted immediate - simplified handling */
        jit_emit_mov(ctx, 16, imm);  /* Use IP0 */
        jit_emit_add_reg(ctx, rd, rn, 16);
        return;
    }
    
    instr |= ((imm & 0xFFF) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rd & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit SUB immediate
 */
void jit_emit_sub(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint64_t imm)
{
    uint32_t instr = ENC_SUB_IMM;
    
    if (imm > 0xFFF) {
        jit_emit_mov(ctx, 16, imm);
        jit_emit_sub_reg(ctx, rd, rn, 16);
        return;
    }
    
    instr |= ((imm & 0xFFF) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rd & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit ADD register
 */
void jit_emit_add_reg(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint8_t rm)
{
    uint32_t instr = ENC_ADD_REG;
    instr |= ((rm & 0x1F) << 16);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rd & 0x1F);
    jit_emit_dword(ctx, instr);
}


/**
 * Emit SUB register
 */
void jit_emit_sub_reg(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint8_t rm)
{
    uint32_t instr = ENC_SUB_REG;
    instr |= ((rm & 0x1F) << 16);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rd & 0x1F);
    jit_emit_dword(ctx, instr);
}


/**
 * Emit AND register
 */
void jit_emit_and_reg(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint8_t rm)
{
    uint32_t instr = ENC_AND_REG;
    instr |= ((rm & 0x1F) << 16);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rd & 0x1F);
    jit_emit_dword(ctx, instr);
}


/**
 * Emit ORR register
 */
void jit_emit_orr_reg(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint8_t rm)
{
    uint32_t instr = ENC_ORR_REG;
    instr |= ((rm & 0x1F) << 16);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rd & 0x1F);
    jit_emit_dword(ctx, instr);
}


/**
 * Emit EOR register
 */
void jit_emit_eor_reg(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, uint8_t rm)
{
    uint32_t instr = ENC_EOR_REG;
    instr |= ((rm & 0x1F) << 16);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rd & 0x1F);
    jit_emit_dword(ctx, instr);
}


/**
 * Emit logical shift left
 */
void jit_emit_lsl(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, int shift)
{
    uint32_t instr = ENC_LSL;
    instr |= ((shift & 0x3F) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rd & 0x1F);
    jit_emit_dword(ctx, instr);
}


/**
 * Emit logical shift right
 */
void jit_emit_lsr(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, int shift)
{
    uint32_t instr = ENC_LSR;
    instr |= ((shift & 0x3F) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rd & 0x1F);
    jit_emit_dword(ctx, instr);
}


/**
 * Emit arithmetic shift right
 */
void jit_emit_asr(jit_emit_context_t *ctx, uint8_t rd, uint8_t rn, int shift)
{
    uint32_t instr = ENC_ASR;
    instr |= ((shift & 0x3F) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rd & 0x1F);
    jit_emit_dword(ctx, instr);
}


/**
 * Emit compare immediate
 */
void jit_emit_cmp(jit_emit_context_t *ctx, uint8_t rn, uint64_t imm)
{
    uint32_t instr = ENC_CMP_IMM;
    
    if (imm > 0xFFF) {
        jit_emit_mov(ctx, 16, imm);
        jit_emit_cmp_reg(ctx, rn, 16);
        return;
    }
    
    instr |= ((imm & 0xFFF) << 10);
    instr |= ((rn & 0x1F) << 5);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit compare register
 */
void jit_emit_cmp_reg(jit_emit_context_t *ctx, uint8_t rn, uint8_t rm)
{
    uint32_t instr = ENC_CMP_REG;
    instr |= ((rm & 0x1F) << 16);
    instr |= ((rn & 0x1F) << 5);
    jit_emit_dword(ctx, instr);
}


/**
 * Emit unconditional branch
 */
void jit_emit_b(jit_emit_context_t *ctx, int offset)
{
    uint32_t instr = ENC_B;
    int rel_offset = offset - (int)ctx->offset;
    int imm26 = (rel_offset / 4) & 0x03FFFFFF;
    
    instr |= (imm26 << 0);
    jit_emit_dword(ctx, instr);
}


/**
 * Emit conditional branch
 */
void jit_emit_bcond(jit_emit_context_t *ctx, int condition, int offset)
{
    uint32_t instr = ENC_BCOND;
    int rel_offset = offset - (int)ctx->offset;
    int imm19 = (rel_offset / 4) & 0x07FFFF;
    
    instr |= ((condition & 0xF) << 0);
    instr |= (imm19 << 5);
    jit_emit_dword(ctx, instr);
}


/* Conditional branch helpers */
void jit_emit_beq(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0x0, offset); }
void jit_emit_bne(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0x1, offset); }
void jit_emit_bcs(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0x2, offset); }
void jit_emit_bcc(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0x3, offset); }
void jit_emit_bmi(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0x4, offset); }
void jit_emit_bpl(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0x5, offset); }
void jit_emit_bvs(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0x6, offset); }
void jit_emit_bvc(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0x7, offset); }
void jit_emit_bhi(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0x8, offset); }
void jit_emit_bls(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0x9, offset); }
void jit_emit_bge(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0xA, offset); }
void jit_emit_blt(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0xB, offset); }
void jit_emit_bgt(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0xC, offset); }
void jit_emit_ble(jit_emit_context_t *ctx, int offset) { jit_emit_bcond(ctx, 0xD, offset); }


/**
 * Emit load register (64-bit)
 */
void jit_emit_ldr(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset)
{
    uint32_t instr = ENC_LDR_IMM;
    
    if (offset < 0 || offset > 0x1FF8) {
        /* Need to load offset into temp register */
        jit_emit_mov(ctx, 16, offset);
        jit_emit_ldr(ctx, rt, rn, 0);  /* Will need post-indexed form */
        return;
    }
    
    instr |= ((offset / 8) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rt & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit load register word (32-bit)
 */
void jit_emit_ldr_w(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset)
{
    uint32_t instr = ENC_LDR_W_IMM;
    
    if (offset < 0 || offset > 0xFFC) {
        jit_emit_mov(ctx, 16, offset);
        return;
    }
    
    instr |= ((offset / 4) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rt & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit load register byte (8-bit)
 */
void jit_emit_ldr_b(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset)
{
    uint32_t instr = ENC_LDR_B_IMM;
    
    if (offset < 0 || offset > 0xFFF) {
        jit_emit_mov(ctx, 16, offset);
        return;
    }
    
    instr |= ((offset & 0xFFF) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rt & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit load register halfword (16-bit)
 */
void jit_emit_ldr_h(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset)
{
    uint32_t instr = ENC_LDR_H_IMM;
    
    if (offset < 0 || offset > 0xFFE) {
        jit_emit_mov(ctx, 16, offset);
        return;
    }
    
    instr |= ((offset / 2) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rt & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit store register (64-bit)
 */
void jit_emit_str(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset)
{
    uint32_t instr = ENC_STR_IMM;
    
    if (offset < 0 || offset > 0x1FF8) {
        jit_emit_mov(ctx, 16, offset);
        return;
    }
    
    instr |= ((offset / 8) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rt & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit store register word (32-bit)
 */
void jit_emit_str_w(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset)
{
    uint32_t instr = ENC_STR_W_IMM;
    
    if (offset < 0 || offset > 0xFFC) {
        jit_emit_mov(ctx, 16, offset);
        return;
    }
    
    instr |= ((offset / 4) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rt & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit store register byte (8-bit)
 */
void jit_emit_str_b(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset)
{
    uint32_t instr = ENC_STR_B_IMM;
    
    if (offset < 0 || offset > 0xFFF) {
        jit_emit_mov(ctx, 16, offset);
        return;
    }
    
    instr |= ((offset & 0xFFF) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rt & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit store register halfword (16-bit)
 */
void jit_emit_str_h(jit_emit_context_t *ctx, uint8_t rt, uint8_t rn, int offset)
{
    uint32_t instr = ENC_STR_H_IMM;
    
    if (offset < 0 || offset > 0xFFE) {
        jit_emit_mov(ctx, 16, offset);
        return;
    }
    
    instr |= ((offset / 2) << 10);
    instr |= ((rn & 0x1F) << 5);
    instr |= (rt & 0x1F);
    
    jit_emit_dword(ctx, instr);
}


/**
 * Emit block prologue
 */
void jit_emit_prologue(jit_emit_context_t *ctx, jit_block_t *block)
{
    (void)block;
    
    /* Save callee-saved registers we'll use */
    /* For initial implementation, we'll use mostly caller-saved regs */
    
    /* Prologue: nothing special for now */
}


/**
 * Emit block epilogue
 */
void jit_emit_epilogue(jit_emit_context_t *ctx, jit_block_t *block)
{
    (void)block;
    
    /* Return cycles used in W0 */
    /* For now, just return 0 */
    jit_emit_mov(ctx, 0, 0);
    
    /* Return to caller */
    jit_emit_dword(ctx, 0xD65F03C0);  /* RET */
}


/**
 * Emit unimplemented instruction handler
 */
void jit_emit_unimplemented(jit_emit_context_t *ctx, uint16_t opcode, const char *name)
{
    (void)opcode;
    (void)name;
    
    /* For unimplemented instructions, we need to:
     * 1. Save CPU state
     * 2. Call interpreter for this instruction
     * 3. Restore CPU state
     * 4. Return to dispatcher
     * 
     * For now, just emit a breakpoint
     */
    
    /* BRK #0xF000 - will cause trap */
    jit_emit_dword(ctx, 0xD4200000 | (0xF000 << 5));
    
    ctx->error = true;
}


/**
 * Get current offset
 */
size_t jit_emit_get_offset(jit_emit_context_t *ctx)
{
    return ctx->offset;
}


/**
 * Patch a branch instruction
 */
void jit_emit_patch_branch(jit_emit_context_t *ctx, size_t offset, int target_offset)
{
    if (offset >= ctx->size) {
        return;
    }
    
    uint32_t *instr = (uint32_t *)(ctx->buffer + offset);
    uint32_t old = *instr;
    
    /* Check if it's a B or BL instruction */
    if ((old & 0xFC000000) == ENC_B) {
        int rel_offset = target_offset - (int)offset;
        int imm26 = (rel_offset / 4) & 0x03FFFFFF;
        *instr = (old & 0xFC000000) | imm26;
    }
}
