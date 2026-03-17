/*
 * JIT SUB Translator
 * 
 * Translates SUB instructions to AArch64.
 * 
 * SUB Formats:
 *   SUB <ea>,Dn    - Subtract memory/register from data register
 *   SUB Dn,<ea>    - Subtract data register from memory/register
 *   SUB #<data>,Dn - Subtract immediate from data register
 * 
 * Operation:
 *   <destination> <- <destination> - <source>
 * 
 * CCR Effects:
 *   X: Set according to borrow
 *   N: Set according to result (bit 31/15/7)
 *   Z: Set according to result (==0)
 *   V: Set according to overflow
 *   C: Set according to borrow (NOT carry)
 * 
 * Reference: M68000PRM Section 4.5.2 (SUB)
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
 * SUB opcode structure:
 * 
 * SUB <ea>,Dn:
 *   1001 00SS SSMM MRRR
 *   Bits 11-9: Source EA mode
 *   Bits 8-6:  Source EA register
 *   Bits 3-0:  Destination Dn
 * 
 * SUB Dn,<ea>:
 *   1001 01SS SSMM MRRR
 *   Bits 11-9: Dest EA mode
 *   Bits 8-6:  Dest EA register
 *   Bits 3-0:  Source Dn
 */

/* Check if opcode is SUB <ea>,Dn form (bit 8 = 0) */
static inline int sub_is_ea_to_dn(uint16_t opcode)
{
    return (opcode & 0x0100) == 0;
}

/* Extract size from SUB opcode (bits 9-8) */
static inline uint8_t sub_get_size(uint16_t opcode)
{
    return (opcode >> 8) & 0x3;
}

/* Extract destination Dn register */
static inline uint8_t sub_get_dst_dn(uint16_t opcode)
{
    if (sub_is_ea_to_dn(opcode)) {
        return (opcode >> 9) & 0x7;
    }
    /* SUB Dn,<ea> - source is Dn */
    return (opcode >> 9) & 0x7;
}

/* Extract EA mode */
static inline uint8_t sub_get_ea_mode(uint16_t opcode)
{
    if (sub_is_ea_to_dn(opcode)) {
        return (opcode >> 3) & 0x7;
    }
    return (opcode >> 3) & 0x7;
}

/* Extract EA register */
static inline uint8_t sub_get_ea_reg(uint16_t opcode)
{
    return opcode & 0x7;
}

/*
 * Check if EA mode is supported
 * Implemented: DN, AN, AI, DI, AW
 */
static int sub_ea_mode_supported(uint8_t mode)
{
    switch (mode) {
    case JIT_EA_DN:
    case JIT_EA_AN:
    case JIT_EA_AI:
    case JIT_EA_DI:
    case JIT_EA_AW:
        return 1;
    default:
        return 0;
    }
}

/*
 * Get size in bytes
 */
static inline int sub_size_bytes(uint8_t size_enc)
{
    switch (size_enc) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 4;
    default: return 0;
    }
}

/*
 * Emit SUB with CCR update
 * 
 * AArch64 SUB updates NZCV flags same as ADD.
 * 68k C flag is borrow (inverse of AArch64 C).
 */
static void emit_sub_with_ccr(jit_emit_context_t *ctx, uint8_t dst_reg, 
                               uint8_t src_reg, uint8_t size_enc)
{
    /* Perform subtraction - AArch64 updates NZCV flags */
    jit_emit_sub_reg(ctx, dst_reg, dst_reg, src_reg);
    
    /* N flag: n_flag = (result >> 31) ? 0x80 : 0 */
    jit_emit_lsr(ctx, AARCH64_R1, dst_reg, 31);
    jit_emit_and_immed(ctx, AARCH64_R1, AARCH64_R1, 1);
    jit_emit_lsl(ctx, AARCH64_R1, AARCH64_R1, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R1, 24);
    
    /* Z flag: not_z_flag = (result != 0) ? 1 : 0 */
    jit_emit_cmp(ctx, dst_reg, 0);
    jit_emit_cset(ctx, AARCH64_R2, 1);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R2, 28);
    
    /* V flag: v_flag = overflow ? 0x80 : 0 */
    jit_emit_cset(ctx, AARCH64_R3, 10);  /* CSET VS */
    jit_emit_lsl(ctx, AARCH64_R3, AARCH64_R3, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R3, 32);
    
    /* C flag: 68k C = borrow = NOT AArch64 C */
    /* AArch64 C=1 means no borrow, C=0 means borrow */
    /* So 68k C = NOT AArch64 C */
    jit_emit_cset(ctx, AARCH64_R4, 3);  /* CSET LO/CC (condition 3 = !CS) */
    jit_emit_lsl(ctx, AARCH64_R4, AARCH64_R4, 8);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R4, 20);
    
    /* X flag: same as C for SUB */
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R4, 16);
}

/*
 * Translate SUB instruction
 */
int jit_translate_sub(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint8_t size_enc;
    uint8_t ea_mode, ea_reg;
    uint8_t dst_dn;
    uint8_t dst_reg_aarch64;
    uint8_t src_reg_aarch64;
    int is_ea_to_dn;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_sub invalid context\n");
        return -1;
    }
    
    /* Analyze opcode */
    is_ea_to_dn = sub_is_ea_to_dn(ctx->opcode);
    size_enc = sub_get_size(ctx->opcode);
    dst_dn = sub_get_dst_dn(ctx->opcode);
    ea_mode = sub_get_ea_mode(ctx->opcode);
    ea_reg = sub_get_ea_reg(ctx->opcode);
    
    /* Validate size */
    if (size_enc == 3) {
        LOG_ERROR("[CPU] m68xkcpu: SUB with illegal size at PC=0x%08X\n",
                  ctx->jit->current_pc);
        return -1;
    }
    
    /* Check if EA mode is supported */
    if (!sub_ea_mode_supported(ea_mode)) {
        LOG_VERBOSE("[CPU] m68xkcpu: SUB with unsupported EA mode %u at PC=0x%08X\n",
                    ea_mode, ctx->jit->current_pc);
        return -1;
    }
    
    /* For now, only handle <ea>,Dn form (memory to Dn) */
    if (!is_ea_to_dn) {
        LOG_VERBOSE("[CPU] m68xkcpu: SUB Dn,<ea> form not yet implemented\n");
        return -1;
    }
    
    LOG_VERBOSE("[CPU] m68xkcpu: SUB EA=%u Reg=%u D%u (opcode=%04X)\n",
                ea_mode, ea_reg, dst_dn, ctx->opcode);
    
    /* Allocate code buffer */
    code_size = 256;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: SUB failed to allocate code buffer\n");
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
        /* SUB Dn,Dn */
        src_reg_aarch64 = AARCH64_R19 + ea_reg;
        emit_sub_with_ccr(&emit_ctx, dst_reg_aarch64, src_reg_aarch64, size_enc);
    } else if (ea_mode == JIT_EA_AN) {
        /* SUB An,Dn - use address register value */
        src_reg_aarch64 = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) src_reg_aarch64 = AARCH64_R9;
        emit_sub_with_ccr(&emit_ctx, dst_reg_aarch64, src_reg_aarch64, size_enc);
    } else if (ea_mode == JIT_EA_AI) {
        /* SUB (An),Dn */
        src_reg_aarch64 = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) src_reg_aarch64 = AARCH64_R9;
        
        switch (size_enc) {
        case 0: jit_emit_ldr_b(&emit_ctx, temp_reg, src_reg_aarch64, 0); break;
        case 1: jit_emit_ldr_h(&emit_ctx, temp_reg, src_reg_aarch64, 0); break;
        case 2: jit_emit_ldr_w(&emit_ctx, temp_reg, src_reg_aarch64, 0); break;
        }
        
        emit_sub_with_ccr(&emit_ctx, dst_reg_aarch64, temp_reg, size_enc);
    } else if (ea_mode == JIT_EA_DI) {
        /* SUB (d16,An),Dn */
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        
        jit_emit_mov_reg(&emit_ctx, 17, addr_reg);
        jit_emit_add_immed(&emit_ctx, 17, 17, (int16_t)ext_word);
        
        switch (size_enc) {
        case 0: jit_emit_ldr_b(&emit_ctx, temp_reg, 17, 0); break;
        case 1: jit_emit_ldr_h(&emit_ctx, temp_reg, 17, 0); break;
        case 2: jit_emit_ldr_w(&emit_ctx, temp_reg, 17, 0); break;
        }
        
        emit_sub_with_ccr(&emit_ctx, dst_reg_aarch64, temp_reg, size_enc);
    } else if (ea_mode == JIT_EA_AW) {
        /* SUB (xxx).W,Dn */
        jit_emit_movz(&emit_ctx, 17, ext_word & 0xFFFF, 0);
        if (ext_word & 0x8000) {
            jit_emit_movk(&emit_ctx, 17, 0xFFFF, 1);
        }
        
        switch (size_enc) {
        case 0: jit_emit_ldr_b(&emit_ctx, temp_reg, 17, 0); break;
        case 1: jit_emit_ldr_h(&emit_ctx, temp_reg, 17, 0); break;
        case 2: jit_emit_ldr_w(&emit_ctx, temp_reg, 17, 0); break;
        }
        
        emit_sub_with_ccr(&emit_ctx, dst_reg_aarch64, temp_reg, size_enc);
    } else {
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    /* Advance PC */
    int pc_advance = 2;
    if (ea_mode == JIT_EA_DI || ea_mode == JIT_EA_AW) pc_advance = 4;
    jit_emit_add_immed(&emit_ctx, AARCH64_R10, AARCH64_R10, pc_advance);
    
    /* Return cycles (SUB <ea>,Dn: 8 cycles for memory modes) */
    int cycles = (ea_mode == JIT_EA_DN || ea_mode == JIT_EA_AN) ? 4 : 8;
    jit_emit_movz(&emit_ctx, AARCH64_R0, cycles, 0);
    
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: SUB code emission failed\n");
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
    
    LOG_DEBUG("[CPU] m68xkcpu: SUB translated (code_size=%zu bytes)\n", emit_ctx.offset);
    
    return 0;
}
