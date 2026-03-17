/*
 * JIT MOVE Translator
 * 
 * Translates MOVE instructions to AArch64.
 * 
 * MOVE Format:
 *   Bits 13-12: Size (00=byte, 01=word, 10=long)
 *   Bits 11-6:  Source EA mode/register
 *   Bits 5-0:   Destination EA mode/register
 * 
 * MOVE <ea_src>, <ea_dst>
 * 
 * Operation:
 *   <ea_dst> <- <ea_src>
 * 
 * CCR Effects:
 *   N: Set according to result
 *   Z: Set according to result
 *   V: Cleared
 *   C: Cleared
 *   X: Not affected
 * 
 * Reference: M68000PRM Section 4.6 (MOVE)
 * 
 * Endianness Note:
 *   Memory accesses use jit_mem_read/write which handle BE↔LE conversion.
 *   Register operations are endian-neutral.
 *   CPU state structure is in LE format (matching AArch64 native).
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
 * MOVE opcode structure:
 * 
 * 15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0
 *  0  0  D  D  S  S  S  M  M  M  D  D  M  M  M  R
 *     ^  ^  ^     ^        ^     ^
 *     |  |  |     |        |     +-- Dest EA reg (bits 2-0)
 *     |  |  |     |        +-------- Dest EA mode (bits 5-3)
 *     |  |  +-----+----------------- Source EA reg (bits 8-6)
 *     |  +-------------------------- Source EA mode (bits 11-9)
 *     +----------------------------- Size (bits 13-12)
 * 
 * Size encoding:
 *   00 = byte (.b)
 *   01 = word (.w)
 *   10 = long (.l)
 *   11 = illegal for MOVE
 */

/* Extract size from MOVE opcode (bits 13-12) */
static inline uint8_t move_get_size(uint16_t opcode)
{
    return (opcode >> 12) & 0x3;
}

/* Extract source EA mode from MOVE opcode (bits 11-9) */
static inline uint8_t move_get_src_mode(uint16_t opcode)
{
    return (opcode >> 9) & 0x7;
}

/* Extract source EA register from MOVE opcode (bits 8-6) */
static inline uint8_t move_get_src_reg(uint16_t opcode)
{
    return (opcode >> 6) & 0x7;
}

/* Extract destination EA mode from MOVE opcode (bits 5-3) */
static inline uint8_t move_get_dst_mode(uint16_t opcode)
{
    return (opcode >> 3) & 0x7;
}

/* Extract destination EA register from MOVE opcode (bits 2-0) */
static inline uint8_t move_get_dst_reg(uint16_t opcode)
{
    return opcode & 0x7;
}

/*
 * Check if EA mode is supported
 * 
 * Implemented: DN, AN, AI, DI, AW, AL, PI, PD
 * Not yet: IX, PCDI, PCIX, IMM (for source)
 */
static int ea_mode_supported(uint8_t mode)
{
    switch (mode) {
    case JIT_EA_DN:
    case JIT_EA_AN:
    case JIT_EA_AI:
    case JIT_EA_DI:
    case JIT_EA_AW:
    case JIT_EA_AL:
    case JIT_EA_PI:
    case JIT_EA_PD:
        return 1;
    case JIT_EA_IX:
    case JIT_EA_PCDI:
    case JIT_EA_PCIX:
    case JIT_EA_IMM:
        return 0;
    default:
        return 0;
    }
}

/*
 * Get size in bytes from MOVE size encoding
 */
static inline int move_size_bytes(uint8_t size_enc)
{
    switch (size_enc) {
    case 0: return 1;  /* Byte */
    case 1: return 2;  /* Word */
    case 2: return 4;  /* Long */
    default: return 0; /* Illegal */
    }
}

/*
 * Emit code to read from source EA
 * 
 * Returns AArch64 register containing the value (zero-extended for byte/word)
 * Returns 0 on failure
 */
static uint8_t emit_read_src_ea(jit_emit_context_t *ctx, uint8_t mode, uint8_t reg, 
                                 uint8_t size_enc, uint8_t fc, uint16_t ext_word)
{
    uint8_t value_reg;
    uint8_t addr_reg;
    int size_bytes;
    
    size_bytes = move_size_bytes(size_enc);
    if (size_bytes == 0) return 0;
    
    value_reg = 16;  /* Use IP0 as temp for loaded values */
    
    switch (mode) {
    case JIT_EA_DN:
        /* Source is data register Dn */
        return AARCH64_R19 + reg;  /* D0-D7 -> R19-R26 */
        
    case JIT_EA_AN:
        /* Source is address register An - treat as data for MOVE */
        addr_reg = AARCH64_R27 + reg;
        if (reg == 7) addr_reg = AARCH64_R9;
        return addr_reg;
        
    case JIT_EA_AI:
        /* (An) - Address register indirect */
        addr_reg = AARCH64_R27 + reg;
        if (reg == 7) addr_reg = AARCH64_R9;
        
        /* Load from memory [addr_reg] into value_reg */
        switch (size_enc) {
        case 0: jit_emit_ldr_b(ctx, value_reg, addr_reg, 0); break;
        case 1: jit_emit_ldr_h(ctx, value_reg, addr_reg, 0); break;
        case 2: jit_emit_ldr_w(ctx, value_reg, addr_reg, 0); break;
        }
        return value_reg;
        
    case JIT_EA_DI:
        /* (d16,An) - Displacement: EA = An + sign_extend(ext_word) */
        addr_reg = AARCH64_R27 + reg;
        if (reg == 7) addr_reg = AARCH64_R9;
        
        /* Load An into temp, add sign-extended displacement */
        jit_emit_mov_reg(ctx, 17, addr_reg);  /* R17 = An */
        jit_emit_add_immed(ctx, 17, 17, (int16_t)ext_word);  /* R17 += d16 */
        
        /* Load from [R17] into value_reg */
        switch (size_enc) {
        case 0: jit_emit_ldr_b(ctx, value_reg, 17, 0); break;
        case 1: jit_emit_ldr_h(ctx, value_reg, 17, 0); break;
        case 2: jit_emit_ldr_w(ctx, value_reg, 17, 0); break;
        }
        return value_reg;
        
    case JIT_EA_AW:
        /* (xxx).W - Absolute word: EA = sign_extend(ext_word) */
        /* Load absolute address from extension word */
        jit_emit_movz(ctx, 17, ext_word & 0xFFFF, 0);
        if (ext_word & 0x8000) {
            jit_emit_movk(ctx, 17, 0xFFFF, 1);  /* Sign extend */
        }
        
        /* Load from [R17] into value_reg */
        switch (size_enc) {
        case 0: jit_emit_ldr_b(ctx, value_reg, 17, 0); break;
        case 1: jit_emit_ldr_h(ctx, value_reg, 17, 0); break;
        case 2: jit_emit_ldr_w(ctx, value_reg, 17, 0); break;
        }
        return value_reg;
        
    case JIT_EA_AL:
        /* (xxx).L - Absolute long: ext_word is low 16 bits, need 2nd word for high */
        /* For now, unimplemented - needs 2 extension words */
        return 0;
        
    case JIT_EA_PI:
        /* (An)+ - Postincrement: load from [An], then An += size */
        addr_reg = AARCH64_R27 + reg;
        if (reg == 7) addr_reg = AARCH64_R9;
        
        /* Load from [An] */
        switch (size_enc) {
        case 0: jit_emit_ldr_b(ctx, value_reg, addr_reg, 0); break;
        case 1: jit_emit_ldr_h(ctx, value_reg, addr_reg, 0); break;
        case 2: jit_emit_ldr_w(ctx, value_reg, addr_reg, 0); break;
        }
        
        /* Post-increment An (A7 increments by 2 for byte access) */
        {
            int incr = size_bytes;
            if (reg == 7 && size_enc == 0) incr = 2;  /* A7 byte rule */
            jit_emit_add_immed(ctx, addr_reg, addr_reg, incr);
        }
        return value_reg;
        
    case JIT_EA_PD:
        /* -(An) - Predecrement: An -= size, then load from [An] */
        addr_reg = AARCH64_R27 + reg;
        if (reg == 7) addr_reg = AARCH64_R9;
        
        /* Pre-decrement An (A7 decrements by 2 for byte access) */
        {
            int decr = size_bytes;
            if (reg == 7 && size_enc == 0) decr = 2;  /* A7 byte rule */
            jit_emit_sub_immed(ctx, addr_reg, addr_reg, decr);
        }
        
        /* Load from [An] */
        switch (size_enc) {
        case 0: jit_emit_ldr_b(ctx, value_reg, addr_reg, 0); break;
        case 1: jit_emit_ldr_h(ctx, value_reg, addr_reg, 0); break;
        case 2: jit_emit_ldr_w(ctx, value_reg, addr_reg, 0); break;
        }
        return value_reg;
        
    default:
        return 0;
    }
}

/*
 * Emit code to write to destination EA
 * 
 * value_reg contains the value to store
 * Returns 0 on success, -1 on failure
 */
static int emit_write_dst_ea(jit_emit_context_t *ctx, uint8_t mode, uint8_t reg,
                              uint8_t size_enc, uint8_t value_reg, uint8_t fc, uint16_t ext_word)
{
    uint8_t addr_reg;
    int size_bytes;
    
    size_bytes = move_size_bytes(size_enc);
    if (size_bytes == 0) return -1;
    
    switch (mode) {
    case JIT_EA_DN:
        /* Destination is data register Dn */
        if (value_reg != (AARCH64_R19 + reg)) {
            jit_emit_mov_reg(ctx, AARCH64_R19 + reg, value_reg);
        }
        return 0;
        
    case JIT_EA_AN:
        /* Destination is address register An */
        addr_reg = AARCH64_R27 + reg;
        if (reg == 7) addr_reg = AARCH64_R9;
        
        if (value_reg != addr_reg) {
            jit_emit_mov_reg(ctx, addr_reg, value_reg);
        }
        return 0;
        
    case JIT_EA_AI:
        /* (An) - Address register indirect */
        addr_reg = AARCH64_R27 + reg;
        if (reg == 7) addr_reg = AARCH64_R9;
        
        /* Store value_reg to memory [addr_reg] */
        switch (size_enc) {
        case 0: jit_emit_str_b(ctx, value_reg, addr_reg, 0); break;
        case 1: jit_emit_str_h(ctx, value_reg, addr_reg, 0); break;
        case 2: jit_emit_str_w(ctx, value_reg, addr_reg, 0); break;
        }
        return 0;
        
    case JIT_EA_DI:
        /* (d16,An) - Displacement: EA = An + sign_extend(ext_word) */
        addr_reg = AARCH64_R27 + reg;
        if (reg == 7) addr_reg = AARCH64_R9;
        
        /* Calculate EA = An + d16 */
        jit_emit_mov_reg(ctx, 17, addr_reg);  /* R17 = An */
        jit_emit_add_immed(ctx, 17, 17, (int16_t)ext_word);  /* R17 += d16 */
        
        /* Store value_reg to [R17] */
        switch (size_enc) {
        case 0: jit_emit_str_b(ctx, value_reg, 17, 0); break;
        case 1: jit_emit_str_h(ctx, value_reg, 17, 0); break;
        case 2: jit_emit_str_w(ctx, value_reg, 17, 0); break;
        }
        return 0;
        
    case JIT_EA_AW:
        /* (xxx).W - Absolute word: EA = sign_extend(ext_word) */
        jit_emit_movz(ctx, 17, ext_word & 0xFFFF, 0);
        if (ext_word & 0x8000) {
            jit_emit_movk(ctx, 17, 0xFFFF, 1);  /* Sign extend */
        }
        
        /* Store value_reg to [R17] */
        switch (size_enc) {
        case 0: jit_emit_str_b(ctx, value_reg, 17, 0); break;
        case 1: jit_emit_str_h(ctx, value_reg, 17, 0); break;
        case 2: jit_emit_str_w(ctx, value_reg, 17, 0); break;
        }
        return 0;
        
    case JIT_EA_AL:
        /* (xxx).L - Absolute long: needs 2 extension words */
        return -1;
        
    case JIT_EA_PI:
        /* (An)+ - Postincrement: store to [An], then An += size */
        addr_reg = AARCH64_R27 + reg;
        if (reg == 7) addr_reg = AARCH64_R9;
        
        /* Store to [An] */
        switch (size_enc) {
        case 0: jit_emit_str_b(ctx, value_reg, addr_reg, 0); break;
        case 1: jit_emit_str_h(ctx, value_reg, addr_reg, 0); break;
        case 2: jit_emit_str_w(ctx, value_reg, addr_reg, 0); break;
        }
        
        /* Post-increment An (A7 increments by 2 for byte access) */
        {
            int incr = size_bytes;
            if (reg == 7 && size_enc == 0) incr = 2;  /* A7 byte rule */
            jit_emit_add_immed(ctx, addr_reg, addr_reg, incr);
        }
        return 0;
        
    case JIT_EA_PD:
        /* -(An) - Predecrement: An -= size, then store to [An] */
        addr_reg = AARCH64_R27 + reg;
        if (reg == 7) addr_reg = AARCH64_R9;
        
        /* Pre-decrement An (A7 decrements by 2 for byte access) */
        {
            int decr = size_bytes;
            if (reg == 7 && size_enc == 0) decr = 2;  /* A7 byte rule */
            jit_emit_sub_immed(ctx, addr_reg, addr_reg, decr);
        }
        
        /* Store to [An] */
        switch (size_enc) {
        case 0: jit_emit_str_b(ctx, value_reg, addr_reg, 0); break;
        case 1: jit_emit_str_h(ctx, value_reg, addr_reg, 0); break;
        case 2: jit_emit_str_w(ctx, value_reg, addr_reg, 0); break;
        }
        return 0;
        
    default:
        return -1;
    }
}

/*
 * Emit CCR update for MOVE
 * 
 * N = result bit (31 for long, 15 for word, 7 for byte)
 * Z = result == 0
 * V = 0
 * C = 0
 * 
 * Note: MOVE to An does NOT update CCR (handled by caller)
 */
static void emit_move_ccr_update(jit_emit_context_t *ctx, uint8_t result_reg, 
                                  uint8_t size_enc)
{
    int shift_amount;
    
    /* Determine shift for N flag */
    switch (size_enc) {
    case 0: shift_amount = 7; break;  /* Byte */
    case 1: shift_amount = 15; break; /* Word */
    case 2: shift_amount = 31; break; /* Long */
    default: return;
    }
    
    /* N flag: n_flag = (result >> shift) & 1 ? 0x80 : 0 */
    jit_emit_lsr(ctx, AARCH64_R1, result_reg, shift_amount);
    jit_emit_and_immed(ctx, AARCH64_R1, AARCH64_R1, 1);
    jit_emit_lsl(ctx, AARCH64_R1, AARCH64_R1, 7);  /* R1 = 0 or 0x80 */
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R1, 24);  /* n_flag offset */
    
    /* Z flag: not_z_flag = (result != 0) ? 1 : 0 */
    /* Mask to appropriate size first */
    switch (size_enc) {
    case 0:
        jit_emit_and_immed(ctx, AARCH64_R2, result_reg, 0xFF);
        break;
    case 1:
        jit_emit_and_immed(ctx, AARCH64_R2, result_reg, 0xFFFF);
        break;
    case 2:
    default:
        jit_emit_mov_reg(ctx, AARCH64_R2, result_reg);
        break;
    }
    
    jit_emit_cmp(ctx, AARCH64_R2, 0);
    jit_emit_cset(ctx, AARCH64_R3, 1);  /* CSET NE */
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R3, 28);  /* not_z_flag offset */
    
    /* V = 0, C = 0 */
    jit_emit_movz(ctx, AARCH64_R4, 0, 0);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R4, 32);  /* v_flag offset */
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R4, 20);  /* c_flag offset */
}

/*
 * Check if destination is An and size is valid (word/long only)
 * Returns: 0 = valid, 1 = invalid (byte to An), 2 = valid but no CCR (word/long to An)
 */
static int move_to_an_check(uint8_t dst_mode, uint8_t dst_reg, uint8_t size_enc)
{
    if (dst_mode != JIT_EA_AN) {
        return 0;  /* Not An, normal processing */
    }
    
    if (size_enc == 0) {
        return 1;  /* Byte to An - illegal, should fall back */
    }
    
    return 2;  /* Word/long to An - valid, no CCR update */
}

/*
 * Translate MOVE instruction
 * 
 * @param ctx Translator context
 * @return 0 on success, -1 on failure
 */
int jit_translate_move(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint8_t size_enc;
    uint8_t src_mode, src_reg;
    uint8_t dst_mode, dst_reg;
    uint8_t value_reg;
    int size_bytes;
    uint8_t fetch_fc;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_move invalid context\n");
        return -1;
    }
    
    /* Extract instruction fields */
    size_enc = move_get_size(ctx->opcode);
    src_mode = move_get_src_mode(ctx->opcode);
    src_reg = move_get_src_reg(ctx->opcode);
    dst_mode = move_get_dst_mode(ctx->opcode);
    dst_reg = move_get_dst_reg(ctx->opcode);
    
    /* Special handling for MOVEA (0x2000-0x2FFF and 0x3000-0x3FFF) */
    /* MOVEA format: 001S SSSS SMMM MRRR where bits 11-9 = dest An, 8-6 = src */
    if ((ctx->opcode & 0xF000) == 0x2000 || (ctx->opcode & 0xF000) == 0x3000) {
        /* This is MOVEA - destination An is in bits 11-9, source in bits 8-6 */
        dst_mode = JIT_EA_AN;
        dst_reg = (ctx->opcode >> 9) & 0x7;  /* Extract An from bits 11-9 */
        /* Source is in bits 8-6: bit 8 = mode high, bits 7-6 = mode low + reg high */
        /* For MOVEA, bits 8-6 encode: 000=Dn, 001=An, 010=(An), etc. */
        src_mode = (ctx->opcode >> 6) & 0x7;  /* Extract src mode from bits 8-6 */
        src_reg = 0;  /* Source reg is part of mode for most EA modes */
        if (src_mode == JIT_EA_DN || src_mode == JIT_EA_AN) {
            src_reg = (ctx->opcode >> 3) & 0x7;  /* For Dn/An, reg is in bits 5-3 */
        }

    }
    
    /* Validate size (11 = illegal for MOVE) */
    if (size_enc == 3) {
        LOG_ERROR("[CPU] m68xkcpu: MOVE with illegal size encoding at PC=0x%08X\n",
                  ctx->jit->current_pc);
        return -1;
    }
    
    size_bytes = move_size_bytes(size_enc);
    
    /* Check if EA modes are supported */
    if (!ea_mode_supported(src_mode) || !ea_mode_supported(dst_mode)) {
        /* Fall back to interpreter for unsupported EA modes */
        LOG_VERBOSE("[CPU] m68xkcpu: MOVE with unsupported EA mode (src=%u, dst=%u) at PC=0x%08X\n",
                    src_mode, dst_mode, ctx->jit->current_pc);
        return -1;
    }
    
    /* For initial implementation, handle register, AI, DI, AW modes */
    /* AL needs 2 extension words - not supported yet */
    if (src_mode == JIT_EA_AL || src_mode == JIT_EA_IMM) {
        LOG_VERBOSE("[CPU] m68xkcpu: MOVE src mode %u not supported - fallback\n", src_mode);
        return -1;
    }
    if (dst_mode == JIT_EA_AL) {
        LOG_VERBOSE("[CPU] m68xkcpu: MOVE dst mode %u not supported - fallback\n", dst_mode);
        return -1;
    }
    
    /* Check MOVE to An - byte is illegal, word/long skip CCR */
    int an_check = move_to_an_check(dst_mode, dst_reg, size_enc);
    if (an_check == 1) {
        LOG_VERBOSE("[CPU] m68xkcpu: MOVE byte to An is illegal - fallback\n");
        return -1;
    }
    LOG_VERBOSE("[CPU] m68xkcpu: MOVE %s.%c (mode %u->%u, opcode=%04X)\n",
                (size_enc == 0) ? "b" : (size_enc == 1) ? "w" : "l",
                src_mode, dst_mode, ctx->opcode);
    
    /* Allocate code buffer */
    code_size = 256;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: MOVE failed to allocate code buffer\n");
        return -1;
    }
    
    /* Initialize emitter */
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    /* Get function code for memory access (if needed) */
    fetch_fc = jit_get_fc(ctx->jit->cpu->s_flag ? 1 : 0, 1);
    
    /*
     * Emit AArch64 code for MOVE:
     * 1. Read from source EA
     * 2. Write to destination EA
     * 3. Update CCR flags
     * 4. Advance PC
     * 5. Return cycles
     */
    
    /* Step 1: Read from source */
    uint16_t src_ext = (ctx->ext_count > 0) ? ctx->ext_words[0] : 0;
    value_reg = emit_read_src_ea(&emit_ctx, src_mode, src_reg, size_enc, fetch_fc, src_ext);
    if (value_reg == 0) {
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    /* Step 2: Write to destination */
    uint16_t dst_ext = (ctx->ext_count > 0) ? ctx->ext_words[0] : 0;
    if (emit_write_dst_ea(&emit_ctx, dst_mode, dst_reg, size_enc, value_reg, fetch_fc, dst_ext) < 0) {
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    /* Step 3: Update CCR flags (skip for MOVE to An word/long) */
    if (an_check != 2) {
        emit_move_ccr_update(&emit_ctx, value_reg, size_enc);
    }
    
    /* Step 4: Advance PC by 2 or 4 depending on EA modes */
    int pc_advance = 2;
    if (src_mode == JIT_EA_DI || src_mode == JIT_EA_AW) pc_advance = 4;
    if (dst_mode == JIT_EA_DI || dst_mode == JIT_EA_AW) pc_advance = 4;
    jit_emit_add_immed(&emit_ctx, AARCH64_R10, AARCH64_R10, pc_advance);
    
    /* Step 5: Set cycles used */
    jit_emit_movz(&emit_ctx, AARCH64_R0, 4, 0);
    
    /* Check for errors */
    if (emit_ctx.error) {
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    /* Store compiled code in block */
    ctx->block->code_ptr = code_buffer;
    ctx->block->code_size = emit_ctx.offset;
    ctx->block->instruction_count = 1;
    ctx->block->cycle_count = 4;
    ctx->block->flags = JIT_BLOCK_VALID;
    LOG_ERROR("[JIT-MOVE] Translation COMPLETE\n");
    fflush(stderr);
    
    /* Store instruction info */
    ctx->block->instructions[0].opcode = ctx->opcode;
    ctx->block->instructions[0].ext_count = 0;
    ctx->block->instructions[0].cycles = 4;
    
    return 0;
}
