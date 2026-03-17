/*
 * JIT BRA/BCC Translator
 * 
 * BRA/BCC: 0110cccc dddddddd (short) or with extension word (long)
 * cccc=0: BRA (always)
 * cccc=1-15: BCC (conditional)
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

static inline uint8_t branch_get_cond(uint16_t opcode)
{
    return (opcode >> 8) & 0x0F;
}

static inline int8_t branch_get_disp8(uint16_t opcode)
{
    return (int8_t)(opcode & 0xFF);
}

static inline int branch_is_long(uint16_t opcode)
{
    return (opcode & 0x00FF) == 0;
}

/*
 * Emit condition test, return offset of branch instruction for patching
 */
static int emit_bcc_test(jit_emit_context_t *ctx, uint8_t cond)
{
    size_t branch_pos;
    
    switch (cond) {
    case 0x4:  /* CC - carry clear */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 20);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_beq(ctx, 0);
        return (int)branch_pos;
    case 0x5:  /* CS - carry set */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 20);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_bne(ctx, 0);
        return (int)branch_pos;
    case 0x6:  /* NE */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 28);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_bne(ctx, 0);
        return (int)branch_pos;
    case 0x7:  /* EQ */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 28);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_beq(ctx, 0);
        return (int)branch_pos;
    case 0x8:  /* VC */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 32);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_beq(ctx, 0);
        return (int)branch_pos;
    case 0x9:  /* VS */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 32);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_bne(ctx, 0);
        return (int)branch_pos;
    case 0xA:  /* PL */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 24);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_beq(ctx, 0);
        return (int)branch_pos;
    case 0xB:  /* MI */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 24);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_bne(ctx, 0);
        return (int)branch_pos;
    case 0xC:  /* GE */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 24);
        jit_emit_ldr_w(ctx, AARCH64_R2, AARCH64_R0, 32);
        jit_emit_cmp(ctx, AARCH64_R1, AARCH64_R2);
        branch_pos = ctx->offset;
        jit_emit_beq(ctx, 0);
        return (int)branch_pos;
    case 0xD:  /* LT */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 24);
        jit_emit_ldr_w(ctx, AARCH64_R2, AARCH64_R0, 32);
        jit_emit_cmp(ctx, AARCH64_R1, AARCH64_R2);
        branch_pos = ctx->offset;
        jit_emit_bne(ctx, 0);
        return (int)branch_pos;
    case 0xE:  /* GT */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 28);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_beq(ctx, 0);
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 24);
        jit_emit_ldr_w(ctx, AARCH64_R2, AARCH64_R0, 32);
        jit_emit_cmp(ctx, AARCH64_R1, AARCH64_R2);
        branch_pos = ctx->offset;
        jit_emit_beq(ctx, 0);
        return (int)branch_pos;
    case 0xF:  /* LE */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 28);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_beq(ctx, 0);
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 24);
        jit_emit_ldr_w(ctx, AARCH64_R2, AARCH64_R0, 32);
        jit_emit_cmp(ctx, AARCH64_R1, AARCH64_R2);
        branch_pos = ctx->offset;
        jit_emit_bne(ctx, 0);
        return (int)branch_pos;
    case 0x2:  /* HI */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 20);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_bne(ctx, 0);
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 28);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_bne(ctx, 0);
        return (int)branch_pos;
    case 0x3:  /* LS */
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 20);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_bne(ctx, 0);
        jit_emit_ldr_w(ctx, AARCH64_R1, AARCH64_R0, 28);
        jit_emit_cmp(ctx, AARCH64_R1, 0);
        branch_pos = ctx->offset;
        jit_emit_beq(ctx, 0);
        return (int)branch_pos;
    default:
        return -1;
    }
}

int jit_translate_branch(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint16_t opcode;
    uint8_t cond;
    int32_t displacement;
    uint32_t target_pc;
    int is_long, pc_advance;
    size_t branch_offset, skip_offset;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_branch invalid context\n");
        return -1;
    }
    
    opcode = ctx->opcode;
    cond = branch_get_cond(opcode);
    is_long = branch_is_long(opcode);
    
    if (is_long) {
        if (ctx->ext_count < 1) {
            LOG_ERROR("[CPU] m68xkcpu: Long branch missing extension word\n");
            return -1;
        }
        displacement = (int16_t)ctx->ext_words[0];
        pc_advance = 4;
    } else {
        displacement = branch_get_disp8(opcode);
        pc_advance = 2;
    }
    
    target_pc = ctx->jit->current_pc + pc_advance + displacement;
    
    LOG_VERBOSE("[CPU] m68xkcpu: BRANCH cond=%u long=%d disp=%d target=%08X\n",
                cond, is_long, displacement, target_pc);
    
    code_size = 256;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: BRANCH failed to allocate code buffer\n");
        return -1;
    }
    
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    if (cond == 0x0) {
        /* BRA - always taken */
        jit_emit_movz(&emit_ctx, AARCH64_R10, target_pc & 0xFFFF, 0);
        if (target_pc & 0xFFFF0000) {
            jit_emit_movk(&emit_ctx, AARCH64_R10, (target_pc >> 16) & 0xFFFF, 1);
        }
    } else if (cond == 0x1) {
        /* BF - never taken */
        jit_emit_add_immed(&emit_ctx, AARCH64_R10, AARCH64_R10, pc_advance);
    } else {
        /* BCC - conditional: test, branch to taken, advance PC (not taken), branch to end, taken: set PC */
        
        /* Test and branch to taken */
        branch_offset = emit_bcc_test(&emit_ctx, cond);
        
        /* Not taken: advance PC */
        jit_emit_add_immed(&emit_ctx, AARCH64_R10, AARCH64_R10, pc_advance);
        
        /* Skip taken code */
        skip_offset = emit_ctx.offset;
        jit_emit_b(&emit_ctx, 0);
        size_t taken_offset = emit_ctx.offset;
        
        /* Taken: set PC to target */
        jit_emit_movz(&emit_ctx, AARCH64_R10, target_pc & 0xFFFF, 0);
        if (target_pc & 0xFFFF0000) {
            jit_emit_movk(&emit_ctx, AARCH64_R10, (target_pc >> 16) & 0xFFFF, 1);
        }
        size_t end_offset = emit_ctx.offset;
        
        /* Patch branches */
        if (branch_offset > 0) {
            jit_emit_patch_bcond(&emit_ctx, branch_offset, (int)(taken_offset - branch_offset));
        }
        jit_emit_patch_b(&emit_ctx, skip_offset, (int)(end_offset - skip_offset));
    }
    
    int cycles = is_long ? 12 : 10;
    jit_emit_movz(&emit_ctx, AARCH64_R0, cycles, 0);
    
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: BRANCH code emission failed\n");
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    ctx->block->code_ptr = code_buffer;
    ctx->block->code_size = emit_ctx.offset;
    ctx->block->instruction_count = 1;
    ctx->block->cycle_count = cycles;
    ctx->block->flags = JIT_BLOCK_VALID | JIT_BLOCK_ENDS_BRANCH;
    
    ctx->block->instructions[0].opcode = opcode;
    ctx->block->instructions[0].ext_count = ctx->ext_count;
    ctx->block->instructions[0].cycles = cycles;
    
    LOG_DEBUG("[CPU] m68xkcpu: BRANCH translated (code_size=%zu bytes)\n", emit_ctx.offset);
    
    return 0;
}
