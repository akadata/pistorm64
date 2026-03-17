/*
 * JIT Control Flow Translator
 * 
 * BSR - Branch to Subroutine
 * RTS - Return from Subroutine
 * JSR - Jump to Subroutine
 * JMP - Jump
 * 
 * Reference: M68000PRM Sections 4.2.2 (BSR), 4.13 (RTS), 4.8 (JSR), 4.9 (JMP)
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

/* Check if opcode is BSR */
static inline int ctrl_is_bsr(uint16_t opcode)
{
    return (opcode & 0xFF00) == 0x6100;
}

/* Check if opcode is RTS */
static inline int ctrl_is_rts(uint16_t opcode)
{
    return opcode == 0x4E75;
}

/* Check if opcode is JSR */
static inline int ctrl_is_jsr(uint16_t opcode)
{
    return (opcode & 0xFFC0) == 0x4E80;
}

/* Check if opcode is JMP */
static inline int ctrl_is_jmp(uint16_t opcode)
{
    return (opcode & 0xFFC0) == 0x4EC0;
}

/* Get displacement for BSR */
static inline int16_t bsr_get_disp(uint16_t opcode)
{
    return (int8_t)(opcode & 0xFF);
}

/* Check if BSR is long format */
static inline int bsr_is_long(uint16_t opcode)
{
    return (opcode & 0x00FF) == 0;
}

/* Get EA mode for JSR/JMP */
static inline uint8_t ctrl_get_ea_mode(uint16_t opcode)
{
    return (opcode >> 3) & 0x7;
}

static inline uint8_t ctrl_get_ea_reg(uint16_t opcode)
{
    return opcode & 0x7;
}

/*
 * BSR - Branch to Subroutine
 * Pushes return address (PC+disp) onto stack, then branches
 */
int jit_translate_bsr(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint16_t opcode;
    int32_t displacement;
    uint32_t target_pc, return_pc;
    int is_long, pc_advance;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_bsr invalid context\n");
        return -1;
    }
    
    opcode = ctx->opcode;
    is_long = bsr_is_long(opcode);
    
    if (is_long) {
        if (ctx->ext_count < 1) {
            LOG_ERROR("[CPU] m68xkcpu: BSR.L missing extension word\n");
            return -1;
        }
        displacement = (int16_t)ctx->ext_words[0];
        pc_advance = 4;
    } else {
        displacement = bsr_get_disp(opcode);
        pc_advance = 2;
    }
    
    return_pc = ctx->jit->current_pc + pc_advance;
    target_pc = return_pc + displacement;
    
    LOG_VERBOSE("[CPU] m68xkcpu: BSR disp=%d target=%08X return=%08X\n",
                displacement, target_pc, return_pc);
    
    code_size = 256;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: BSR failed to allocate code buffer\n");
        return -1;
    }
    
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    /* Push return PC onto stack (A7) */
    /* A7 is R9 */
    uint8_t sp_reg = AARCH64_R9;
    
    /* Decrement SP by 4 */
    jit_emit_sub_immed(&emit_ctx, sp_reg, sp_reg, 4);
    
    /* Store return PC at [SP] */
    jit_emit_movz(&emit_ctx, 16, return_pc & 0xFFFF, 0);
    if (return_pc & 0xFFFF0000) {
        jit_emit_movk(&emit_ctx, 16, (return_pc >> 16) & 0xFFFF, 1);
    }
    jit_emit_str_w(&emit_ctx, 16, sp_reg, 0);
    
    /* Set PC to target */
    jit_emit_movz(&emit_ctx, AARCH64_R10, target_pc & 0xFFFF, 0);
    if (target_pc & 0xFFFF0000) {
        jit_emit_movk(&emit_ctx, AARCH64_R10, (target_pc >> 16) & 0xFFFF, 1);
    }
    
    /* Return cycles: 18 for short, 22 for long */
    int cycles = is_long ? 22 : 18;
    jit_emit_movz(&emit_ctx, AARCH64_R0, cycles, 0);
    
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: BSR code emission failed\n");
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
    
    LOG_DEBUG("[CPU] m68xkcpu: BSR translated (code_size=%zu bytes)\n", emit_ctx.offset);
    
    return 0;
}

/*
 * RTS - Return from Subroutine
 * Pops return address from stack into PC
 */
int jit_translate_rts(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_rts invalid context\n");
        return -1;
    }
    
    LOG_VERBOSE("[CPU] m68xkcpu: RTS\n");
    
    code_size = 128;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: RTS failed to allocate code buffer\n");
        return -1;
    }
    
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    /* Pop return PC from stack (A7 = R9) */
    uint8_t sp_reg = AARCH64_R9;
    
    /* Load PC from [SP] */
    jit_emit_ldr_w(&emit_ctx, 16, sp_reg, 0);
    
    /* Increment SP by 4 */
    jit_emit_add_immed(&emit_ctx, sp_reg, sp_reg, 4);
    
    /* Set PC */
    jit_emit_mov_reg(&emit_ctx, AARCH64_R10, 16);
    
    /* Return cycles: 16 */
    jit_emit_movz(&emit_ctx, AARCH64_R0, 16, 0);
    
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: RTS code emission failed\n");
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    ctx->block->code_ptr = code_buffer;
    ctx->block->code_size = emit_ctx.offset;
    ctx->block->instruction_count = 1;
    ctx->block->cycle_count = 16;
    ctx->block->flags = JIT_BLOCK_VALID | JIT_BLOCK_ENDS_RTS;
    
    ctx->block->instructions[0].opcode = ctx->opcode;
    ctx->block->instructions[0].ext_count = 0;
    ctx->block->instructions[0].cycles = 16;
    
    LOG_DEBUG("[CPU] m68xkcpu: RTS translated (code_size=%zu bytes)\n", emit_ctx.offset);
    
    return 0;
}

/*
 * JSR - Jump to Subroutine
 * Pushes return address onto stack, then jumps to EA
 */
int jit_translate_jsr(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint16_t opcode;
    uint8_t ea_mode, ea_reg;
    uint32_t return_pc;
    uint16_t ext_word;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_jsr invalid context\n");
        return -1;
    }
    
    opcode = ctx->opcode;
    ea_mode = ctrl_get_ea_mode(opcode);
    ea_reg = ctrl_get_ea_reg(opcode);
    ext_word = (ctx->ext_count > 0) ? ctx->ext_words[0] : 0;
    return_pc = ctx->jit->current_pc + 2 + (ctx->ext_count * 2);
    
    LOG_VERBOSE("[CPU] m68xkcpu: JSR EA=%u Reg=%u ext_count=%d\n", ea_mode, ea_reg, ctx->ext_count);
    
    /* Support (An), (d16,An), (xxx).W, (xxx).L */
    if (ea_mode != JIT_EA_AI && ea_mode != JIT_EA_DI && ea_mode != JIT_EA_AW &&
        !(ea_mode == 7 && (ea_reg == 0 || ea_reg == 1))) {
        LOG_VERBOSE("[CPU] m68xkcpu: JSR EA mode %u reg %u not supported\n", ea_mode, ea_reg);
        return -1;
    }
    
    code_size = 256;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: JSR failed to allocate code buffer\n");
        return -1;
    }
    
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    uint8_t sp_reg = AARCH64_R9;
    uint8_t addr_reg;
    
    /* Calculate target address */
    if (ea_mode == JIT_EA_AI) {
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        jit_emit_mov_reg(&emit_ctx, 17, addr_reg);
    } else if (ea_mode == JIT_EA_DI) {
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        jit_emit_mov_reg(&emit_ctx, 17, addr_reg);
        jit_emit_add_immed(&emit_ctx, 17, 17, (int16_t)ext_word);
    } else if (ea_mode == JIT_EA_AW) {
        jit_emit_movz(&emit_ctx, 17, ext_word & 0xFFFF, 0);
        if (ext_word & 0x8000) {
            jit_emit_movk(&emit_ctx, 17, 0xFFFF, 1);
        }
    } else if (ea_mode == 7 && ea_reg == 0) {  /* (xxx).W */
        jit_emit_movz(&emit_ctx, 17, ext_word & 0xFFFF, 0);
        if (ext_word & 0x8000) {
            jit_emit_movk(&emit_ctx, 17, 0xFFFF, 1);
        }
    } else if (ea_mode == 7 && ea_reg == 1) {  /* (xxx).L - absolute long */
        if (ctx->ext_count < 2) {
            LOG_ERROR("[CPU] m68xkcpu: JMP.L needs 2 ext words, have %d\n", ctx->ext_count);
            jit_cache_free(ctx->jit, code_buffer, code_size);
            return -1;
        }
        uint32_t target_addr = ((uint32_t)ctx->ext_words[0] << 16) | ctx->ext_words[1];
        jit_emit_movz(&emit_ctx, 17, target_addr & 0xFFFF, 0);
        jit_emit_movk(&emit_ctx, 17, (target_addr >> 16) & 0xFFFF, 1);
    }
    
    /* Load target PC from memory */
    jit_emit_ldr_w(&emit_ctx, AARCH64_R10, 17, 0);
    
    /* Push return PC onto stack */
    jit_emit_sub_immed(&emit_ctx, sp_reg, sp_reg, 4);
    jit_emit_movz(&emit_ctx, 16, return_pc & 0xFFFF, 0);
    if (return_pc & 0xFFFF0000) {
        jit_emit_movk(&emit_ctx, 16, (return_pc >> 16) & 0xFFFF, 1);
    }
    jit_emit_str_w(&emit_ctx, 16, sp_reg, 0);
    
    /* Return cycles: 18 + memory access */
    jit_emit_movz(&emit_ctx, AARCH64_R0, 22, 0);
    
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: JSR code emission failed\n");
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    ctx->block->code_ptr = code_buffer;
    ctx->block->code_size = emit_ctx.offset;
    ctx->block->instruction_count = 1;
    ctx->block->cycle_count = 22;
    ctx->block->flags = JIT_BLOCK_VALID | JIT_BLOCK_ENDS_BRANCH;
    
    ctx->block->instructions[0].opcode = opcode;
    ctx->block->instructions[0].ext_count = ctx->ext_count;
    ctx->block->instructions[0].cycles = 22;
    
    LOG_DEBUG("[CPU] m68xkcpu: JSR translated (code_size=%zu bytes)\n", emit_ctx.offset);
    
    return 0;
}

/*
 * JMP - Jump
 * Jumps to EA (no stack operation)
 */
int jit_translate_jmp(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint16_t opcode;
    uint8_t ea_mode, ea_reg;
    uint16_t ext_word;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_jmp invalid context\n");
        return -1;
    }
    
    opcode = ctx->opcode;
    ea_mode = ctrl_get_ea_mode(opcode);
    ea_reg = ctrl_get_ea_reg(opcode);
    ext_word = (ctx->ext_count > 0) ? ctx->ext_words[0] : 0;
    
    LOG_VERBOSE("[CPU] m68xkcpu: JMP EA=%u Reg=%u ext_count=%d\n", ea_mode, ea_reg, ctx->ext_count);
    
    /* Support (An), (d16,An), (xxx).W, (xxx).L */
    /* EA mode 7: 0=(xxx).W, 1=(xxx).L, 2=(d16,PC), 3=(d8,PC,Xn) */
    if (ea_mode != JIT_EA_AI && ea_mode != JIT_EA_DI && ea_mode != JIT_EA_AW &&
        !(ea_mode == 7 && (ea_reg == 0 || ea_reg == 1))) {
        LOG_VERBOSE("[CPU] m68xkcpu: JMP EA mode %u reg %u not supported\n", ea_mode, ea_reg);
        return -1;
    }
    
    /* Special handling for JMP.L - need 2 extension words */
    if (ea_mode == 7 && ea_reg == 1) {
        if (ctx->ext_count < 2) {
            LOG_ERROR("[CPU] m68xkcpu: JMP.L needs 2 ext words, have %d\n", ctx->ext_count);
            return -1;
        }
        uint32_t addr = ((uint32_t)ctx->ext_words[0] << 16) | ctx->ext_words[1];
        LOG_VERBOSE("[CPU] m68xkcpu: JMP.L target=0x%08X\n", addr);
    }
    
    code_size = 256;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: JMP failed to allocate code buffer\n");
        return -1;
    }
    
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    uint8_t addr_reg;
    
    /* Calculate target address */
    if (ea_mode == JIT_EA_AI) {
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        jit_emit_mov_reg(&emit_ctx, 17, addr_reg);
    } else if (ea_mode == JIT_EA_DI) {
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        jit_emit_mov_reg(&emit_ctx, 17, addr_reg);
        jit_emit_add_immed(&emit_ctx, 17, 17, (int16_t)ext_word);
    } else if (ea_mode == JIT_EA_AW) {
        jit_emit_movz(&emit_ctx, 17, ext_word & 0xFFFF, 0);
        if (ext_word & 0x8000) {
            jit_emit_movk(&emit_ctx, 17, 0xFFFF, 1);
        }
    } else if (ea_mode == 7 && ea_reg == 0) {  /* (xxx).W */
        jit_emit_movz(&emit_ctx, 17, ext_word & 0xFFFF, 0);
        if (ext_word & 0x8000) {
            jit_emit_movk(&emit_ctx, 17, 0xFFFF, 1);
        }
    } else if (ea_mode == 7 && ea_reg == 1) {  /* (xxx).L - absolute long */
        if (ctx->ext_count < 2) {
            LOG_ERROR("[CPU] m68xkcpu: JSR.L needs 2 ext words, have %d\n", ctx->ext_count);
            jit_cache_free(ctx->jit, code_buffer, code_size);
            return -1;
        }
        uint32_t target_addr = ((uint32_t)ctx->ext_words[0] << 16) | ctx->ext_words[1];
        jit_emit_movz(&emit_ctx, 17, target_addr & 0xFFFF, 0);
        jit_emit_movk(&emit_ctx, 17, (target_addr >> 16) & 0xFFFF, 1);
    }
    
    /* Load target PC from memory */
    jit_emit_ldr_w(&emit_ctx, AARCH64_R10, 17, 0);
    
    /* PC advance: 2 for opcode, +2 for .W, +4 for .L */
    int pc_advance = 2;
    if (ea_mode == JIT_EA_AW || (ea_mode == 7 && ea_reg == 0)) {
        pc_advance = 4;
    } else if (ea_mode == 7 && ea_reg == 1) {
        pc_advance = 6;
    }
    jit_emit_add_immed(&emit_ctx, AARCH64_R10, AARCH64_R10, pc_advance);
    
    /* Return cycles: 8 + memory access */
    int cycles = 12;
    if (ea_mode == 7 && ea_reg == 1) cycles = 18;
    jit_emit_movz(&emit_ctx, AARCH64_R0, cycles, 0);
    
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: JMP code emission failed\n");
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    ctx->block->code_ptr = code_buffer;
    ctx->block->code_size = emit_ctx.offset;
    ctx->block->instruction_count = 1;
    ctx->block->cycle_count = 12;
    ctx->block->flags = JIT_BLOCK_VALID | JIT_BLOCK_ENDS_JMP;
    
    ctx->block->instructions[0].opcode = opcode;
    ctx->block->instructions[0].ext_count = ctx->ext_count;
    ctx->block->instructions[0].cycles = cycles;
    
    LOG_DEBUG("[CPU] m68xkcpu: JMP translated (code_size=%zu bytes)\n", emit_ctx.offset);
    
    return 0;
}
