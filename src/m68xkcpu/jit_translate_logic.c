/*
 * JIT Logic Operations Translator
 * 
 * AND, OR, EOR and immediate forms (ANDI, ORI, EORI)
 * 
 * Format:
 *   AND <ea>,Dn: 1100 00SS SSMM MRRR
 *   OR  <ea>,Dn: 1000 00SS SSMM MRRR
 *   EOR Dn,<ea>: 1011 00SS SSMM MRRR
 *   ANDI #<data>,<ea>: 0000 0010 00MMMRRR <data>
 *   ORI  #<data>,<ea>: 0000 0000 00MMMRRR <data>
 *   EORI #<data>,<ea>: 0000 0011 00MMMRRR <data>
 * 
 * CCR Effects:
 *   X: Not affected
 *   N: Set according to result
 *   Z: Set according to result
 *   V: Cleared
 *   C: Cleared
 * 
 * Reference: M68000PRM Sections 4.7 (AND), 4.11 (OR), 4.16 (EOR)
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

/* Check if opcode is AND (bits 15-12 = 1100 or 0000 0010) */
static inline int logic_is_and(uint16_t opcode)
{
    return ((opcode & 0xF100) == 0xC000) || ((opcode & 0xFF00) == 0x0200);
}

/* Check if opcode is OR (bits 15-12 = 1000 or 0000 0000) */
static inline int logic_is_or(uint16_t opcode)
{
    return ((opcode & 0xF100) == 0x8000) || ((opcode & 0xFF00) == 0x0000);
}

/* Check if opcode is EOR (bits 15-12 = 1011 or 0000 0011) */
static inline int logic_is_eor(uint16_t opcode)
{
    return ((opcode & 0xF100) == 0xB000) || ((opcode & 0xFF00) == 0x0300);
}

/* Check if opcode is immediate form */
static inline int logic_is_immediate(uint16_t opcode)
{
    return (opcode & 0xFF00) == 0x0000 || (opcode & 0xFF00) == 0x0200 || 
           (opcode & 0xFF00) == 0x0300;
}

/* Extract size (bits 11-9 for register form, bits 7-6 for immediate) */
static inline uint8_t logic_get_size(uint16_t opcode)
{
    if (logic_is_immediate(opcode)) {
        return (opcode >> 6) & 0x3;
    }
    return (opcode >> 9) & 0x3;
}

/* Extract EA mode */
static inline uint8_t logic_get_ea_mode(uint16_t opcode)
{
    return (opcode >> 3) & 0x7;
}

/* Extract EA register */
static inline uint8_t logic_get_ea_reg(uint16_t opcode)
{
    return opcode & 0x7;
}

/* Extract Dn register for AND/OR <ea>,Dn */
static inline uint8_t logic_get_dn_reg(uint16_t opcode)
{
    return (opcode >> 9) & 0x7;
}

/* Check if EA mode is supported */
static int logic_ea_mode_supported(uint8_t mode)
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
 * Emit logic operation with CCR update
 */
static void emit_logic_with_ccr(jit_emit_context_t *ctx, uint8_t dst_reg, 
                                 uint8_t src_reg, int op_type, uint8_t size_enc)
{
    /* op_type: 0=AND, 1=OR, 2=EOR */
    if (op_type == 0) {
        jit_emit_and_reg(ctx, dst_reg, dst_reg, src_reg);
    } else if (op_type == 1) {
        jit_emit_orr_reg(ctx, dst_reg, dst_reg, src_reg);
    } else {
        jit_emit_eor_reg(ctx, dst_reg, dst_reg, src_reg);
    }
    
    /* N flag */
    int shift = (size_enc == 0) ? 7 : (size_enc == 1) ? 15 : 31;
    jit_emit_lsr(ctx, AARCH64_R1, dst_reg, shift);
    jit_emit_and_immed(ctx, AARCH64_R1, AARCH64_R1, 1);
    jit_emit_lsl(ctx, AARCH64_R1, AARCH64_R1, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R1, 24);
    
    /* Z flag */
    uint32_t mask = (size_enc == 0) ? 0xFF : (size_enc == 1) ? 0xFFFF : 0xFFFFFFFF;
    jit_emit_and_immed(ctx, AARCH64_R2, dst_reg, mask);
    jit_emit_cmp(ctx, AARCH64_R2, 0);
    jit_emit_cset(ctx, AARCH64_R3, 1);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R3, 28);
    
    /* V = 0, C = 0 */
    jit_emit_movz(ctx, AARCH64_R4, 0, 0);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R4, 32);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R4, 20);
}

int jit_translate_logic(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint16_t opcode;
    uint8_t size_enc, ea_mode, ea_reg, dn_reg;
    int op_type;  /* 0=AND, 1=OR, 2=EOR */
    int is_immediate;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_logic invalid context\n");
        return -1;
    }
    
    opcode = ctx->opcode;
    is_immediate = logic_is_immediate(opcode);
    
    if (logic_is_and(opcode)) {
        op_type = 0;
    } else if (logic_is_or(opcode)) {
        op_type = 1;
    } else if (logic_is_eor(opcode)) {
        op_type = 2;
    } else {
        LOG_ERROR("[CPU] m68xkcpu: Unknown logic opcode %04X\n", opcode);
        return -1;
    }
    
    size_enc = logic_get_size(opcode);
    ea_mode = logic_get_ea_mode(opcode);
    ea_reg = logic_get_ea_reg(opcode);
    dn_reg = logic_get_dn_reg(opcode);
    
    /* Validate size */
    if (size_enc == 3) {
        LOG_ERROR("[CPU] m68xkcpu: Logic op with illegal size at PC=0x%08X\n",
                  ctx->jit->current_pc);
        return -1;
    }
    
    /* Check EA mode */
    if (!logic_ea_mode_supported(ea_mode)) {
        LOG_VERBOSE("[CPU] m68xkcpu: Logic op EA mode %u not supported\n", ea_mode);
        return -1;
    }
    
    LOG_VERBOSE("[CPU] m68xkcpu: %s EA=%u Reg=%u Dn=%u\n",
                op_type == 0 ? "AND" : (op_type == 1 ? "OR" : "EOR"),
                ea_mode, ea_reg, dn_reg);
    
    code_size = 256;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: Logic op failed to allocate code buffer\n");
        return -1;
    }
    
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    uint8_t src_reg, dst_reg, addr_reg;
    uint16_t ext_word = (ctx->ext_count > 0) ? ctx->ext_words[0] : 0;
    
    if (is_immediate) {
        /* Immediate form: ANDI/ORI/EORI #<data>,<ea> */
        /* Immediate data in extension word */
        src_reg = 17;
        jit_emit_movz(&emit_ctx, src_reg, ext_word & 0xFFFF, 0);
        if (size_enc == 2) {
            /* Long: need second extension word */
            if (ctx->ext_count > 1) {
                jit_emit_movk(&emit_ctx, src_reg, (ctx->ext_words[1] >> 16) & 0xFFFF, 1);
            }
        }
        
        if (ea_mode == JIT_EA_DN) {
            dst_reg = AARCH64_R19 + ea_reg;
            emit_logic_with_ccr(&emit_ctx, dst_reg, src_reg, op_type, size_enc);
        } else if (ea_mode == JIT_EA_AI) {
            addr_reg = AARCH64_R27 + ea_reg;
            if (ea_reg == 7) addr_reg = AARCH64_R9;
            
            dst_reg = 16;
            switch (size_enc) {
            case 0: jit_emit_ldr_b(&emit_ctx, dst_reg, addr_reg, 0); break;
            case 1: jit_emit_ldr_h(&emit_ctx, dst_reg, addr_reg, 0); break;
            case 2: jit_emit_ldr_w(&emit_ctx, dst_reg, addr_reg, 0); break;
            }
            
            emit_logic_with_ccr(&emit_ctx, dst_reg, src_reg, op_type, size_enc);
            
            switch (size_enc) {
            case 0: jit_emit_str_b(&emit_ctx, dst_reg, addr_reg, 0); break;
            case 1: jit_emit_str_h(&emit_ctx, dst_reg, addr_reg, 0); break;
            case 2: jit_emit_str_w(&emit_ctx, dst_reg, addr_reg, 0); break;
            }
        } else if (ea_mode == JIT_EA_DI || ea_mode == JIT_EA_AW) {
            if (ea_mode == JIT_EA_DI) {
                addr_reg = AARCH64_R27 + ea_reg;
                if (ea_reg == 7) addr_reg = AARCH64_R9;
                jit_emit_mov_reg(&emit_ctx, 17, addr_reg);
                jit_emit_add_immed(&emit_ctx, 17, 17, (int16_t)ext_word);
                addr_reg = 17;
            } else {
                jit_emit_movz(&emit_ctx, 17, ext_word & 0xFFFF, 0);
                if (ext_word & 0x8000) {
                    jit_emit_movk(&emit_ctx, 17, 0xFFFF, 1);
                }
                addr_reg = 17;
            }
            
            dst_reg = 16;
            switch (size_enc) {
            case 0: jit_emit_ldr_b(&emit_ctx, dst_reg, addr_reg, 0); break;
            case 1: jit_emit_ldr_h(&emit_ctx, dst_reg, addr_reg, 0); break;
            case 2: jit_emit_ldr_w(&emit_ctx, dst_reg, addr_reg, 0); break;
            }
            
            emit_logic_with_ccr(&emit_ctx, dst_reg, src_reg, op_type, size_enc);
            
            switch (size_enc) {
            case 0: jit_emit_str_b(&emit_ctx, dst_reg, addr_reg, 0); break;
            case 1: jit_emit_str_h(&emit_ctx, dst_reg, addr_reg, 0); break;
            case 2: jit_emit_str_w(&emit_ctx, dst_reg, addr_reg, 0); break;
            }
        }
    } else {
        /* Register form */
        if (op_type == 2 && ea_mode != JIT_EA_DN) {
            /* EOR is Dn,<ea> (reverse direction) */
            src_reg = AARCH64_R19 + dn_reg;
            
            if (ea_mode == JIT_EA_DN) {
                dst_reg = AARCH64_R19 + ea_reg;
                emit_logic_with_ccr(&emit_ctx, dst_reg, src_reg, op_type, size_enc);
            } else if (ea_mode == JIT_EA_AI) {
                addr_reg = AARCH64_R27 + ea_reg;
                if (ea_reg == 7) addr_reg = AARCH64_R9;
                
                dst_reg = 16;
                switch (size_enc) {
                case 0: jit_emit_ldr_b(&emit_ctx, dst_reg, addr_reg, 0); break;
                case 1: jit_emit_ldr_h(&emit_ctx, dst_reg, addr_reg, 0); break;
                case 2: jit_emit_ldr_w(&emit_ctx, dst_reg, addr_reg, 0); break;
                }
                
                emit_logic_with_ccr(&emit_ctx, dst_reg, src_reg, op_type, size_enc);
                
                switch (size_enc) {
                case 0: jit_emit_str_b(&emit_ctx, dst_reg, addr_reg, 0); break;
                case 1: jit_emit_str_h(&emit_ctx, dst_reg, addr_reg, 0); break;
                case 2: jit_emit_str_w(&emit_ctx, dst_reg, addr_reg, 0); break;
                }
            }
        } else {
            /* AND/OR <ea>,Dn */
            if (ea_mode == JIT_EA_DN) {
                src_reg = AARCH64_R19 + ea_reg;
                dst_reg = AARCH64_R19 + dn_reg;
                emit_logic_with_ccr(&emit_ctx, dst_reg, src_reg, op_type, size_enc);
            } else if (ea_mode == JIT_EA_AI) {
                addr_reg = AARCH64_R27 + ea_reg;
                if (ea_reg == 7) addr_reg = AARCH64_R9;
                
                src_reg = 16;
                switch (size_enc) {
                case 0: jit_emit_ldr_b(&emit_ctx, src_reg, addr_reg, 0); break;
                case 1: jit_emit_ldr_h(&emit_ctx, src_reg, addr_reg, 0); break;
                case 2: jit_emit_ldr_w(&emit_ctx, src_reg, addr_reg, 0); break;
                }
                
                dst_reg = AARCH64_R19 + dn_reg;
                emit_logic_with_ccr(&emit_ctx, dst_reg, src_reg, op_type, size_enc);
            }
        }
    }
    
    /* PC advance */
    int pc_advance = 2;
    if (is_immediate || ea_mode == JIT_EA_DI || ea_mode == JIT_EA_AW) pc_advance = 4;
    if (is_immediate && size_enc == 2) pc_advance = 6;  /* Long immediate */
    jit_emit_add_immed(&emit_ctx, AARCH64_R10, AARCH64_R10, pc_advance);
    
    /* Cycles */
    int cycles = 4;
    if (ea_mode != JIT_EA_DN && ea_mode != JIT_EA_AN) cycles = 8;
    if (is_immediate) cycles += 2;
    jit_emit_movz(&emit_ctx, AARCH64_R0, cycles, 0);
    
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: Logic op code emission failed\n");
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    ctx->block->code_ptr = code_buffer;
    ctx->block->code_size = emit_ctx.offset;
    ctx->block->instruction_count = 1;
    ctx->block->cycle_count = cycles;
    ctx->block->flags = JIT_BLOCK_VALID;
    
    ctx->block->instructions[0].opcode = opcode;
    ctx->block->instructions[0].ext_count = ctx->ext_count;
    ctx->block->instructions[0].cycles = cycles;
    
    LOG_DEBUG("[CPU] m68xkcpu: Logic op translated (code_size=%zu bytes)\n", emit_ctx.offset);
    
    return 0;
}
