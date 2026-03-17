/*
 * JIT MOVEQ Translator
 * 
 * Translates MOVEQ (Move Quick) instructions to AArch64.
 * 
 * MOVEQ Format:
 *   01110000 dddddddd
 *   Bits 11-9: Destination register (Dn)  
 *   Bits 7-0:  Immediate data (8-bit signed, sign-extended to 32-bit)
 * 
 * Operation:
 *   Dn <- sign_extend(byte_immediate)
 * 
 * CCR Effects:
 *   N: Set according to result (bit 31)
 *   Z: Set according to result (result == 0)
 *   V: Cleared
 *   C: Cleared  
 *   X: Not affected
 * 
 * Reference: M68000PRM Section 4.6.23 (MOVEQ)
 * 
 * Endianness Note:
 *   MOVEQ has no memory access - only registers and immediate values.
 *   CPU state structure is in LE format (matching AArch64 native).
 *   No BE/LE conversion needed for this instruction.
 *   For memory-accessing instructions (MOVE, etc.), use jit_mem_read/write
 *   which handle BE↔LE conversion via the backend.
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
 * MOVEQ opcode structure:
 * 
 * 15              8 7               0
 * +----------------+----------------+
 * | 0 1 1 1 0 0 0 0|  d 7 - d 0     |
 * +----------------+----------------+
 *                    |          |
 *                    |          +-- Immediate data (8-bit signed)
 *                    +------------- Destination register (bits 11-9 = bits 3-1 of immediate byte)
 * 
 * Actually, the format is:
 * Bits 11-9 encode Dn (destination data register)
 * Bits 7-0 are the immediate value
 */

/* Extract destination register from MOVEQ opcode (bits 11-9) */
static inline uint8_t moveq_get_dst_reg(uint16_t opcode)
{
    return (opcode >> 9) & 0x7;
}

/* Extract immediate value from MOVEQ opcode (bits 7-0), sign-extended to 32-bit */
static inline int32_t moveq_get_immediate(uint16_t opcode)
{
    int8_t imm8 = (int8_t)(opcode & 0xFF);
    return (int32_t)imm8;
}

/*
 * Translate MOVEQ instruction
 * 
 * Emits AArch64 code to:
 * 1. Load immediate into Dn register
 * 2. Update CCR flags (N, Z, V=0, C=0)
 * 3. Advance PC
 * 
 * @param ctx Translator context
 * @return 0 on success, -1 on failure
 */
int jit_translate_moveq(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint8_t dst_reg_68k;
    int32_t immediate;
    uint8_t dst_reg_aarch64;
    uint16_t negated;
    
    if (!ctx || !ctx->jit || !ctx->block) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_moveq invalid context\n");
        return -1;
    }
    
    /* Extract instruction information */
    dst_reg_68k = moveq_get_dst_reg(ctx->opcode);
    immediate = moveq_get_immediate(ctx->opcode);
    
    LOG_VERBOSE("[CPU] m68xkcpu: MOVEQ D%u, #%d (opcode=%04X)\n", 
                dst_reg_68k, immediate, ctx->opcode);
    
    /* Allocate code buffer for this instruction */
    code_size = 128; /* Should be plenty for MOVEQ */
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: MOVEQ failed to allocate code buffer\n");
        return -1;
    }
    
    /* Initialize emitter */
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    /* 
     * Register mapping (initial implementation):
     * D0-D7 -> R19-R26 (callee-saved)
     * CPU state pointer passed in R0
     */
    dst_reg_aarch64 = AARCH64_R19 + dst_reg_68k;
    
    /*
     * Emit AArch64 code for MOVEQ:
     */
    
    /* Step 1: Load immediate into destination register */
    if (immediate >= 0) {
        /* Positive: MOVZ */
        jit_emit_movz(&emit_ctx, dst_reg_aarch64, (uint16_t)immediate, 0);
    } else {
        /* Negative: MOVN then MOVK for sign extension */
        negated = (uint16_t)(~immediate & 0xFFFF);
        jit_emit_movn(&emit_ctx, dst_reg_aarch64, negated, 0);
        jit_emit_movk(&emit_ctx, dst_reg_aarch64, 0xFFFF, 1);
    }
    
    /* Step 2: Update CCR flags
     * N = result bit 31
     * Z = (result == 0)  
     * V = 0
     * C = 0
     * 
     * CPU state offsets (from m68kcpu.h):
     *   n_flag:     24 (0x18) - value 0x80 when set
     *   not_z_flag: 28 (0x1C) - value 1 when set (inverted logic)
     *   v_flag:     32 (0x20) - value 0x80 when set  
     *   c_flag:     20 (0x14) - value 0x100 when set
     */
    
    /* N flag: n_flag = (result >> 31) ? 0x80 : 0 */
    jit_emit_lsr(&emit_ctx, AARCH64_R1, dst_reg_aarch64, 31);
    jit_emit_lsl(&emit_ctx, AARCH64_R1, AARCH64_R1, 7);  /* R1 = 0 or 0x80 */
    jit_emit_str_offset(&emit_ctx, AARCH64_R0, AARCH64_R1, 24);  /* n_flag offset */
    
    /* Z flag: not_z_flag = (result != 0) ? 1 : 0 */
    jit_emit_cmp(&emit_ctx, dst_reg_aarch64, 0);
    jit_emit_cset(&emit_ctx, AARCH64_R2, 1);  /* CSET NE = set if != 0 */
    jit_emit_str_offset(&emit_ctx, AARCH64_R0, AARCH64_R2, 28);  /* not_z_flag offset */
    
    /* V = 0, C = 0 */
    jit_emit_movz(&emit_ctx, AARCH64_R3, 0, 0);
    jit_emit_str_offset(&emit_ctx, AARCH64_R0, AARCH64_R3, 32);  /* v_flag offset */
    jit_emit_str_offset(&emit_ctx, AARCH64_R0, AARCH64_R3, 20);  /* c_flag offset */
    
    /* Step 3: Advance PC by 2 (MOVEQ is single-word) */
    /* PC is in R10 per calling convention */
    jit_emit_add_immed(&emit_ctx, AARCH64_R10, AARCH64_R10, 2);
    
    /* Step 4: Return cycles used (MOVEQ = 4 cycles) */
    jit_emit_movz(&emit_ctx, AARCH64_R0, 4, 0);
    // RET removed - block epilogue handles return */
    
    /* Check for errors */
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: MOVEQ code emission failed\n");
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    /* Store compiled code in block */
    ctx->block->code_ptr = code_buffer;
    ctx->block->code_size = emit_ctx.offset;
    ctx->block->instruction_count = 1;
    ctx->block->cycle_count = 4;
    ctx->block->flags = JIT_BLOCK_VALID;
    
    /* Store instruction info for debugging */
    ctx->block->instructions[0].opcode = ctx->opcode;
    ctx->block->instructions[0].ext_count = 0;
    ctx->block->instructions[0].cycles = 4;
    
    LOG_DEBUG("[CPU] m68xkcpu: MOVEQ translated successfully (code_size=%zu bytes)\n", 
              emit_ctx.offset);
    
    return 0;
}
