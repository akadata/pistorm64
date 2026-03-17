/*
 * JIT ADD Translator
 * 
 * Translates ADD instructions to AArch64.
 * 
 * ADD Formats:
 *   ADD <ea>,Dn    - Add memory/register to data register
 *   ADD Dn,<ea>    - Add data register to memory/register
 *   ADD #<data>,Dn - Add immediate to data register
 * 
 * Operation:
 *   <destination> <- <destination> + <source>
 * 
 * CCR Effects:
 *   X: Set according to carry from MSB
 *   N: Set according to result (bit 31/15/7)
 *   Z: Set according to result (==0)
 *   V: Set according to overflow
 *   C: Set according to carry
 * 
 * Reference: M68000PRM Section 4.4.1 (ADD)
 * 
 * Endianness Note:
 *   Memory accesses use jit_mem_read/write which handle BE↔LE conversion.
 *   Register operations are endian-neutral.
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
 * ADD opcode structure varies by form:
 * 
 * ADD <ea>,Dn (memory/register to Dn):
 *   1101 00SS SSMM MRRR
 *   Bits 11-9: Source EA mode
 *   Bits 8-6:  Source EA register
 *   Bits 3-0:  Destination Dn (bits 11-9 of opcode = 000 for this form)
 * 
 * ADD Dn,<ea> (Dn to memory/register):
 *   1101 01SS SSMM MRRR
 *   Bits 11-9: Dest EA mode
 *   Bits 8-6:  Dest EA register
 *   Bits 3-0:  Source Dn
 * 
 * ADD #<data>,Dn (immediate):
 *   0000 0110 SSSD DDDD
 *   Bits 7-6:  Size (00=byte, 01=word, 10=long)
 *   Bits 5-0:  Destination Dn
 */

/* Check if opcode is ADD immediate form */
static inline int add_is_immediate(uint16_t opcode)
{
    return (opcode & 0xFF00) == 0x0600;
}

/* Check if opcode is ADD <ea>,Dn form (bit 8 = 0) */
static inline int add_is_ea_to_dn(uint16_t opcode)
{
    return (opcode & 0x0100) == 0;
}

/* Extract size from ADD opcode */
static inline uint8_t add_get_size(uint16_t opcode)
{
    if (add_is_immediate(opcode)) {
        return (opcode >> 6) & 0x3;
    }
    return (opcode >> 8) & 0x3;
}

/* Extract destination Dn register */
static inline uint8_t add_get_dst_dn(uint16_t opcode)
{
    if (add_is_immediate(opcode)) {
        return opcode & 0x7;
    }
    if (add_is_ea_to_dn(opcode)) {
        return (opcode >> 9) & 0x7;
    }
    /* ADD Dn,<ea> - source is Dn */
    return (opcode >> 9) & 0x7;
}

/* Extract EA mode (for non-immediate forms) */
static inline uint8_t add_get_ea_mode(uint16_t opcode)
{
    if (add_is_immediate(opcode)) {
        return JIT_EA_IMM;
    }
    if (add_is_ea_to_dn(opcode)) {
        return (opcode >> 3) & 0x7;
    }
    return (opcode >> 3) & 0x7;
}

/* Extract EA register (for non-immediate forms) */
static inline uint8_t add_get_ea_reg(uint16_t opcode)
{
    if (add_is_immediate(opcode)) {
        return 0;
    }
    return opcode & 0x7;
}

/*
 * Check if EA mode is supported
 * Implemented: DN, AN, AI, DI, AW
 * Not yet: AL, PI, PD, IX, PCDI, PCIX, IMM
 */
static int add_ea_mode_supported(uint8_t mode)
{
    switch (mode) {
    case JIT_EA_DN:  /* Data register direct */
    case JIT_EA_AN:  /* Address register direct */
    case JIT_EA_AI:  /* (An) - Address register indirect */
    case JIT_EA_DI:  /* (d16,An) - Displacement */
    case JIT_EA_AW:  /* (xxx).W - Absolute word */
        return 1;
    default:
        return 0;
    }
}

/*
 * Get size in bytes from ADD size encoding
 */
static inline int add_size_bytes(uint8_t size_enc)
{
    switch (size_enc) {
    case 0: return 1;  /* Byte */
    case 1: return 2;  /* Word */
    case 2: return 4;  /* Long */
    default: return 0; /* Illegal */
    }
}

/*
 * Emit ADD with CCR update
 * 
 * AArch64 has native add with flag update, but we need to map
 * AArch64 flags to 68k CCR format.
 * 
 * AArch64 NZCV flags:
 *   N: Negative (bit 31 set)
 *   Z: Zero (result == 0)
 *   C: Carry (unsigned overflow)
 *   V: Overflow (signed overflow)
 * 
 * 68k CCR:
 *   X: Extended (same as C for ADD)
 *   N: Negative
 *   Z: Zero (inverted: 0=set, 1=clear)
 *   V: Overflow
 *   C: Carry
 */
static void emit_add_with_ccr(jit_emit_context_t *ctx, uint8_t dst_reg, 
                               uint8_t src_reg, uint8_t size_enc)
{
    int size_bits;
    
    /* Determine operation size */
    switch (size_enc) {
    case 0: size_bits = 8; break;
    case 1: size_bits = 16; break;
    case 2: size_bits = 32; break;
    default: return;
    }
    
    /* 
     * For initial implementation, use 32-bit ADD
     * TODO: Proper size handling with sign/zero extension
     */
    
    /* Perform addition - AArch64 updates NZCV flags */
    jit_emit_add_reg(ctx, dst_reg, dst_reg, src_reg);
    
    /*
     * Now extract AArch64 flags and store to 68k CCR
     * 
     * Use conditional select to extract each flag
     */
    
    /* N flag: n_flag = (result >> 31) ? 0x80 : 0 */
    jit_emit_lsr(ctx, AARCH64_R1, dst_reg, 31);
    jit_emit_and_immed(ctx, AARCH64_R1, AARCH64_R1, 1);
    jit_emit_lsl(ctx, AARCH64_R1, AARCH64_R1, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R1, 24);  /* n_flag */
    
    /* Z flag: not_z_flag = (result != 0) ? 1 : 0 */
    jit_emit_cmp(ctx, dst_reg, 0);
    jit_emit_cset(ctx, AARCH64_R2, 1);  /* CSET NE */
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R2, 28);  /* not_z_flag */
    
    /* V flag: v_flag = overflow ? 0x80 : 0 */
    /* Use CSVS (set if overflow set) */
    jit_emit_cset(ctx, AARCH64_R3, 10);  /* CSET VS (condition 10) */
    jit_emit_lsl(ctx, AARCH64_R3, AARCH64_R3, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R3, 32);  /* v_flag */
    
    /* C flag: c_flag = carry ? 0x100 : 0 */
    /* Use CSHS (set if carry set / higher or same) */
    jit_emit_cset(ctx, AARCH64_R4, 2);  /* CSET CS/HS (condition 2) */
    jit_emit_lsl(ctx, AARCH64_R4, AARCH64_R4, 8);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R4, 20);  /* c_flag */
    
    /* X flag: same as C for ADD */
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R4, 16);  /* x_flag */
}

/*
 * Translate ADD instruction
 * 
 * @param ctx Translator context
 * @return 0 on success, -1 on failure
 */
int jit_translate_add(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint8_t size_enc;
    uint8_t ea_mode, ea_reg;
    uint8_t dst_dn;
    uint8_t dst_reg_aarch64;
    uint8_t src_reg_aarch64;
    uint8_t fetch_fc;
    int is_immediate;
    int is_ea_to_dn;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_add invalid context\n");
        return -1;
    }
    
    /* Analyze opcode */
    is_immediate = add_is_immediate(ctx->opcode);
    is_ea_to_dn = add_is_ea_to_dn(ctx->opcode);
    size_enc = add_get_size(ctx->opcode);
    dst_dn = add_get_dst_dn(ctx->opcode);
    ea_mode = add_get_ea_mode(ctx->opcode);
    ea_reg = add_get_ea_reg(ctx->opcode);
    
    /* Validate size */
    if (size_enc == 3) {
        LOG_ERROR("[CPU] m68xkcpu: ADD with illegal size at PC=0x%08X\n",
                  ctx->jit->current_pc);
        return -1;
    }
    
    /* Check if EA mode is supported */
    if (!add_ea_mode_supported(ea_mode)) {
        LOG_VERBOSE("[CPU] m68xkcpu: ADD with unsupported EA mode %u at PC=0x%08X\n",
                    ea_mode, ctx->jit->current_pc);
        return -1;  /* Fall back to interpreter */
    }
    
    /* For initial implementation, handle Dn,Dn, (An), (d16,An), (xxx).W modes */
    if (!is_immediate && ea_mode != JIT_EA_DN && ea_mode != JIT_EA_AI &&
        ea_mode != JIT_EA_DI && ea_mode != JIT_EA_AW) {
        LOG_VERBOSE("[CPU] m68xkcpu: ADD EA mode %u not yet implemented\n", ea_mode);
        return -1;
    }
    
    LOG_VERBOSE("[CPU] m68xkcpu: ADD %s.%c %s%u, D%u (opcode=%04X)\n",
                is_immediate ? "#" : (is_ea_to_dn ? "D" : "D"),
                (size_enc == 0) ? 'b' : (size_enc == 1) ? 'w' : 'l',
                is_immediate ? "" : (ea_mode == JIT_EA_DN) ? "D" : "A",
                ea_reg, dst_dn, ctx->opcode);
    
    /* Allocate code buffer */
    code_size = 256;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: ADD failed to allocate code buffer\n");
        return -1;
    }
    
    /* Initialize emitter */
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    /* Get function code */
    fetch_fc = jit_get_fc(ctx->jit->cpu->s_flag ? 1 : 0, 1);
    
    /* Map 68k D register to AArch64 */
    dst_reg_aarch64 = AARCH64_R19 + dst_dn;  /* D0-D7 -> R19-R26 */
    
    /*
     * Emit AArch64 code for ADD:
     * 1. Get source value (from Dn or immediate)
     * 2. Perform addition
     * 3. Update CCR flags
     * 4. Advance PC
     * 5. Return cycles
     */
    
    if (is_immediate) {
        /* ADD #<data>,Dn - immediate value follows opcode */
        /* For now, mark as unimplemented - need to fetch extension word */
        LOG_VERBOSE("[CPU] m68xkcpu: ADD immediate not yet implemented\n");
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    /* Get extension word if needed */
    uint16_t ext_word = (ctx->ext_count > 0) ? ctx->ext_words[0] : 0;
    uint8_t temp_reg = 16;  /* IP0 for loaded values */
    uint8_t addr_reg;
    
    if (ea_mode == JIT_EA_DN) {
        /* ADD Dn,Dn */
        src_reg_aarch64 = AARCH64_R19 + ea_reg;
        emit_add_with_ccr(&emit_ctx, dst_reg_aarch64, src_reg_aarch64, size_enc);
    } else if (ea_mode == JIT_EA_AI) {
        /* ADD (An),Dn - load from memory then add */
        src_reg_aarch64 = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) src_reg_aarch64 = AARCH64_R9;
        
        /* Load from [An] into temp register */
        switch (size_enc) {
        case 0: jit_emit_ldr_b(&emit_ctx, temp_reg, src_reg_aarch64, 0); break;
        case 1: jit_emit_ldr_h(&emit_ctx, temp_reg, src_reg_aarch64, 0); break;
        case 2: jit_emit_ldr_w(&emit_ctx, temp_reg, src_reg_aarch64, 0); break;
        }
        
        /* Perform ADD with CCR update */
        emit_add_with_ccr(&emit_ctx, dst_reg_aarch64, temp_reg, size_enc);
    } else if (ea_mode == JIT_EA_DI) {
        /* ADD (d16,An),Dn - load from displacement then add */
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        
        /* Calculate EA = An + sign_extend(ext_word) */
        jit_emit_mov_reg(&emit_ctx, 17, addr_reg);  /* R17 = An */
        jit_emit_add_immed(&emit_ctx, 17, 17, (int16_t)ext_word);  /* R17 += d16 */
        
        /* Load from [R17] into temp register */
        switch (size_enc) {
        case 0: jit_emit_ldr_b(&emit_ctx, temp_reg, 17, 0); break;
        case 1: jit_emit_ldr_h(&emit_ctx, temp_reg, 17, 0); break;
        case 2: jit_emit_ldr_w(&emit_ctx, temp_reg, 17, 0); break;
        }
        
        /* Perform ADD with CCR update */
        emit_add_with_ccr(&emit_ctx, dst_reg_aarch64, temp_reg, size_enc);
    } else if (ea_mode == JIT_EA_AW) {
        /* ADD (xxx).W,Dn - load from absolute word address then add */
        /* Load absolute address from extension word */
        jit_emit_movz(&emit_ctx, 17, ext_word & 0xFFFF, 0);
        if (ext_word & 0x8000) {
            jit_emit_movk(&emit_ctx, 17, 0xFFFF, 1);  /* Sign extend */
        }
        
        /* Load from [R17] into temp register */
        switch (size_enc) {
        case 0: jit_emit_ldr_b(&emit_ctx, temp_reg, 17, 0); break;
        case 1: jit_emit_ldr_h(&emit_ctx, temp_reg, 17, 0); break;
        case 2: jit_emit_ldr_w(&emit_ctx, temp_reg, 17, 0); break;
        }
        
        /* Perform ADD with CCR update */
        emit_add_with_ccr(&emit_ctx, dst_reg_aarch64, temp_reg, size_enc);
    } else {
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    /* Advance PC by 2 or 4 depending on EA mode */
    int pc_advance = 2;
    if (ea_mode == JIT_EA_DI || ea_mode == JIT_EA_AW) pc_advance = 4;
    jit_emit_add_immed(&emit_ctx, AARCH64_R10, AARCH64_R10, pc_advance);
    
    /* Return cycles used
     * ADD Dn,Dn: 4 cycles
     */
    jit_emit_movz(&emit_ctx, AARCH64_R0, 4, 0);
    // RET removed - block epilogue handles return */
    
    /* Check for errors */
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: ADD code emission failed\n");
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    /* Store compiled code in block */
    ctx->block->code_ptr = code_buffer;
    ctx->block->code_size = emit_ctx.offset;
    ctx->block->instruction_count = 1;
    ctx->block->cycle_count = 4;
    ctx->block->flags = JIT_BLOCK_VALID;
    
    /* Store instruction info */
    ctx->block->instructions[0].opcode = ctx->opcode;
    ctx->block->instructions[0].ext_count = 0;
    ctx->block->instructions[0].cycles = 4;
    
    LOG_DEBUG("[CPU] m68xkcpu: ADD translated successfully (code_size=%zu bytes)\n",
              emit_ctx.offset);
    
    return 0;
}
