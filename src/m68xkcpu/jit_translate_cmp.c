/*
 * JIT CMP Translator
 * 
 * Translates CMP instructions to AArch64.
 * 
 * CMP Formats:
 *   CMP <ea>,Dn    - Compare memory/register with data register
 *   CMP #<data>,Dn - Compare immediate with data register
 * 
 * Operation:
 *   <destination> - <source>  (result discarded, only CCR updated)
 * 
 * CCR Effects:
 *   X: Not affected
 *   N: Set according to result (bit 31/15/7)
 *   Z: Set according to result (==0)
 *   V: Set according to overflow
 *   C: Set according to borrow (NOT carry)
 * 
 * Reference: M68000PRM Section 4.3.1 (CMP)
 */

#include "jit.h"
#include "jit_block.h"
#include "jit_cache.h"
#include "jit_translate.h"
#include "jit_emit_aarch64.h"
#include "jit_arch.h"
#include "musashi/m68kcpu.h"
#include "log.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*
 * CMP opcode structure:
 * 
 * CMP <ea>,Dn:
 *   1011 00SS SSMM MRRR
 *   Bits 11-9: Source EA mode
 *   Bits 8-6:  Source EA register
 *   Bits 3-0:  Destination Dn
 * 
 * CMP #<data>,Dn:
 *   0000 1100 SSSD DDDD
 *   Bits 7-6:  Size
 *   Bits 5-0:  Destination Dn
 */

/* Check if opcode is CMP immediate form */
static inline int cmp_is_immediate(uint16_t opcode)
{
    return (opcode & 0xFF00) == 0x0C00;
}

/* Extract size from CMP opcode */
static inline uint8_t cmp_get_size(uint16_t opcode)
{
    if (cmp_is_immediate(opcode)) {
        return (opcode >> 6) & 0x3;
    }
    /* CMP <ea>,Dn: 1011 0SSS MMMRRR - size SSS is bits 10-8 */
    /* CMPA <ea>,An: 1011 1SSS MMMRRR - bit 11=1, size is still bits 10-8 */
    /* Check for CMPA (bit 11=1) - fall back to interpreter */
    if ((opcode >> 11) & 0x1) {
        return 3;  /* Illegal for CMP translator - CMPA needs separate handling */
    }
    /* For CMP: bits 10-8 encode size: 000=byte, 001=word, 010=long */
    uint8_t size = (opcode >> 8) & 0x7;
    if (size > 2) return 3;  /* Illegal */
    return size;
}

/* Extract destination Dn register */
static inline uint8_t cmp_get_dst_dn(uint16_t opcode)
{
    if (cmp_is_immediate(opcode)) {
        return opcode & 0x7;
    }
    return (opcode >> 9) & 0x7;
}

/* Extract EA mode */
static inline uint8_t cmp_get_ea_mode(uint16_t opcode)
{
    if (cmp_is_immediate(opcode)) {
        return JIT_EA_IMM;
    }
    return (opcode >> 3) & 0x7;
}

/* Extract EA register */
static inline uint8_t cmp_get_ea_reg(uint16_t opcode)
{
    return opcode & 0x7;
}

/*
 * Check if EA mode is supported
 * Implemented: DN, AN, AI, DI, AW, IMM
 */
static int cmp_ea_mode_supported(uint8_t mode)
{
    switch (mode) {
    case JIT_EA_DN:
    case JIT_EA_AN:
    case JIT_EA_AI:
    case JIT_EA_DI:
    case JIT_EA_AW:
    case JIT_EA_IMM:
        return 1;
    default:
        return 0;
    }
}

/*
 * Emit CMP with CCR update (no writeback)
 */
static void emit_cmp_with_ccr(jit_emit_context_t *ctx, uint8_t dst_reg, 
                               uint8_t src_reg, uint8_t size_enc)
{
    /* Perform subtraction for flags only - result discarded */
    /* Use temps to avoid modifying dst_reg */
    uint8_t temp = 17;
    jit_emit_mov_reg(ctx, temp, dst_reg);
    jit_emit_sub_reg(ctx, temp, temp, src_reg);
    
    /* N flag */
    jit_emit_lsr(ctx, AARCH64_R1, temp, 31);
    jit_emit_and_immed(ctx, AARCH64_R1, AARCH64_R1, 1);
    jit_emit_lsl(ctx, AARCH64_R1, AARCH64_R1, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R1, 24);
    
    /* Z flag */
    jit_emit_cmp(ctx, temp, 0);
    jit_emit_cset(ctx, AARCH64_R2, 1);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R2, 28);
    
    /* V flag */
    jit_emit_cset(ctx, AARCH64_R3, 10);
    jit_emit_lsl(ctx, AARCH64_R3, AARCH64_R3, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R3, 32);
    
    /* C flag: 68k C = borrow = NOT AArch64 C */
    jit_emit_cset(ctx, AARCH64_R4, 3);  /* CSET LO/CC */
    jit_emit_lsl(ctx, AARCH64_R4, AARCH64_R4, 8);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R4, 20);
    
    /* X flag: not affected by CMP */
}

/*
 * Translate CMP instruction
 */
int jit_translate_cmp(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint8_t size_enc;
    uint8_t ea_mode, ea_reg;
    uint8_t dst_dn;
    uint8_t dst_reg_aarch64;
    int is_immediate;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_cmp invalid context\n");
        return -1;
    }
    
    /* Analyze opcode */
    is_immediate = cmp_is_immediate(ctx->opcode);
    size_enc = cmp_get_size(ctx->opcode);
    dst_dn = cmp_get_dst_dn(ctx->opcode);
    ea_mode = cmp_get_ea_mode(ctx->opcode);
    ea_reg = cmp_get_ea_reg(ctx->opcode);
    
    /* Validate size */
    if (size_enc == 3) {
        LOG_ERROR("[CPU] m68xkcpu: CMP with illegal size at PC=0x%08X\n",
                  ctx->jit->current_pc);
        return -1;
    }
    
    /* Check if EA mode is supported */
    if (!cmp_ea_mode_supported(ea_mode)) {
        LOG_VERBOSE("[CPU] m68xkcpu: CMP with unsupported EA mode %u at PC=0x%08X\n",
                    ea_mode, ctx->jit->current_pc);
        return -1;
    }
    
    LOG_VERBOSE("[CPU] m68xkcpu: CMP EA=%u Reg=%u D%u (opcode=%04X)\n",
                ea_mode, ea_reg, dst_dn, ctx->opcode);
    
    /* Allocate code buffer */
    code_size = 256;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: CMP failed to allocate code buffer\n");
        return -1;
    }
    
    /* Initialize emitter */
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    /* Map 68k D register to AArch64 */
    dst_reg_aarch64 = AARCH64_R19 + dst_dn;
    
    /* Get extension word if needed */
    uint16_t ext_word = (ctx->ext_count > 0) ? ctx->ext_words[0] : 0;
    uint8_t temp_reg = 16;
    uint8_t addr_reg;
    
    if (ea_mode == JIT_EA_DN) {
        /* CMP Dn,Dn */
        uint8_t src_reg = AARCH64_R19 + ea_reg;
        emit_cmp_with_ccr(&emit_ctx, dst_reg_aarch64, src_reg, size_enc);
    } else if (ea_mode == JIT_EA_AN) {
        /* CMP An,Dn */
        uint8_t src_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) src_reg = AARCH64_R9;
        emit_cmp_with_ccr(&emit_ctx, dst_reg_aarch64, src_reg, size_enc);
    } else if (ea_mode == JIT_EA_AI) {
        /* CMP (An),Dn */
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        
        switch (size_enc) {
        case 0: jit_emit_ldr_b(&emit_ctx, temp_reg, addr_reg, 0); break;
        case 1: jit_emit_ldr_h(&emit_ctx, temp_reg, addr_reg, 0); break;
        case 2: jit_emit_ldr_w(&emit_ctx, temp_reg, addr_reg, 0); break;
        }
        
        emit_cmp_with_ccr(&emit_ctx, dst_reg_aarch64, temp_reg, size_enc);
    } else if (ea_mode == JIT_EA_DI) {
        /* CMP (d16,An),Dn */
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        
        jit_emit_mov_reg(&emit_ctx, 17, addr_reg);
        jit_emit_add_immed(&emit_ctx, 17, 17, (int16_t)ext_word);
        
        switch (size_enc) {
        case 0: jit_emit_ldr_b(&emit_ctx, temp_reg, 17, 0); break;
        case 1: jit_emit_ldr_h(&emit_ctx, temp_reg, 17, 0); break;
        case 2: jit_emit_ldr_w(&emit_ctx, temp_reg, 17, 0); break;
        }
        
        emit_cmp_with_ccr(&emit_ctx, dst_reg_aarch64, temp_reg, size_enc);
    } else if (ea_mode == JIT_EA_AW) {
        /* CMP (xxx).W,Dn */
        jit_emit_movz(&emit_ctx, 17, ext_word & 0xFFFF, 0);
        if (ext_word & 0x8000) {
            jit_emit_movk(&emit_ctx, 17, 0xFFFF, 1);
        }
        
        switch (size_enc) {
        case 0: jit_emit_ldr_b(&emit_ctx, temp_reg, 17, 0); break;
        case 1: jit_emit_ldr_h(&emit_ctx, temp_reg, 17, 0); break;
        case 2: jit_emit_ldr_w(&emit_ctx, temp_reg, 17, 0); break;
        }
        
        emit_cmp_with_ccr(&emit_ctx, dst_reg_aarch64, temp_reg, size_enc);
    } else if (ea_mode == JIT_EA_IMM) {
        /* CMP #<data>,Dn - immediate value in ext_word */
        /* For byte size, immediate is in low 8 bits */
        uint16_t imm_val = ext_word;
        if (size_enc == 0) {
            imm_val &= 0xFF;
        }
        
        jit_emit_movz(&emit_ctx, 17, imm_val, 0);
        if (imm_val > 0xFFFF || (size_enc == 2 && ext_word & 0x8000)) {
            jit_emit_movk(&emit_ctx, 17, (imm_val >> 16) & 0xFFFF, 1);
        }
        
        emit_cmp_with_ccr(&emit_ctx, dst_reg_aarch64, 17, size_enc);
    } else {
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    /* Advance PC */
    int pc_advance = 2;
    if (ea_mode == JIT_EA_DI || ea_mode == JIT_EA_AW || ea_mode == JIT_EA_IMM) {
        pc_advance = 4;
    }
    jit_emit_add_immed(&emit_ctx, AARCH64_R10, AARCH64_R10, pc_advance);
    
    /* Return cycles */
    int cycles = (ea_mode == JIT_EA_DN || ea_mode == JIT_EA_AN) ? 6 : 8;
    jit_emit_movz(&emit_ctx, AARCH64_R0, cycles, 0);
    
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: CMP code emission failed\n");
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    ctx->block->code_ptr = code_buffer;
    ctx->block->code_size = emit_ctx.offset;
    ctx->block->instruction_count = 1;
    ctx->block->cycle_count = cycles;
    ctx->block->flags = JIT_BLOCK_VALID;
    
    ctx->block->instructions[0].opcode = ctx->opcode;
    ctx->block->instructions[0].ext_count = ctx->ext_count;
    ctx->block->instructions[0].cycles = cycles;
    
    LOG_DEBUG("[CPU] m68xkcpu: CMP translated (code_size=%zu bytes)\n", emit_ctx.offset);
    
    return 0;
}
