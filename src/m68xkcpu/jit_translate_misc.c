/*
 * JIT LEA/CLR/TST/BitOps Translator
 * 
 * LEA <ea>,An      - Load Effective Address
 * CLR <ea>         - Clear (set to 0)
 * TST <ea>         - Test (set flags, discard value)
 * BTST <ea>,Dn     - Bit Test
 * BSET <ea>,Dn     - Bit Set
 * BCLR <ea>,Dn     - Bit Clear
 * BCHG <ea>,Dn     - Bit Change (toggle)
 * 
 * Reference: M68000PRM Sections 4.8-4.10, 4.14-4.17
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

/* Check if opcode is LEA */
static inline int misc_is_lea(uint16_t opcode)
{
    return (opcode & 0xF1C0) == 0x41C0;
}

/* Check if opcode is CLR */
static inline int misc_is_clr(uint16_t opcode)
{
    return (opcode & 0xFFC0) == 0x4200;
}

/* Check if opcode is TST */
static inline int misc_is_tst(uint16_t opcode)
{
    return (opcode & 0xFFC0) == 0x4A00;
}

/* Check if opcode is BTST/BSET/BCLR/BCHG */
static inline int misc_is_bitop(uint16_t opcode)
{
    uint16_t op = opcode & 0xF1C0;
    return (op == 0x0100 || op == 0x0140 || op == 0x0180 || op == 0x01C0);
}

/* Get bitop type: 0=BTST, 1=BCHG, 2=BCLR, 3=BSET */
static inline uint8_t bitop_get_type(uint16_t opcode)
{
    return (opcode >> 6) & 0x3;
}

/* Get EA mode from misc opcode */
static inline uint8_t misc_get_ea_mode(uint16_t opcode)
{
    return (opcode >> 3) & 0x7;
}

/* Get EA register from misc opcode */
static inline uint8_t misc_get_ea_reg(uint16_t opcode)
{
    return opcode & 0x7;
}

/* Get destination An register from LEA */
static inline uint8_t lea_get_an(uint16_t opcode)
{
    return (opcode >> 9) & 0x7;
}

/* Get size from CLR/TST */
static inline uint8_t misc_get_size(uint16_t opcode)
{
    return (opcode >> 6) & 0x3;
}

/* Check if EA mode is supported for these instructions */
static int misc_ea_mode_supported(uint8_t mode)
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
 * Emit LEA <ea>,An
 */
static int emit_lea(jit_emit_context_t *ctx, uint16_t opcode, uint16_t ext_word)
{
    uint8_t an = lea_get_an(opcode);
    uint8_t ea_mode = misc_get_ea_mode(opcode);
    uint8_t ea_reg = misc_get_ea_reg(opcode);
    uint8_t dst_reg = AARCH64_R27 + an;
    if (an == 7) dst_reg = AARCH64_R9;
    
    uint8_t addr_reg, temp_reg;
    
    switch (ea_mode) {
    case JIT_EA_DN:
        /* LEA Dn,An - just copy register */
        temp_reg = AARCH64_R19 + ea_reg;
        jit_emit_mov_reg(ctx, dst_reg, temp_reg);
        break;
        
    case JIT_EA_AN:
        /* LEA An,An - just copy register */
        temp_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) temp_reg = AARCH64_R9;
        jit_emit_mov_reg(ctx, dst_reg, temp_reg);
        break;
        
    case JIT_EA_AI:
        /* LEA (An),An - copy An */
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        jit_emit_mov_reg(ctx, dst_reg, addr_reg);
        break;
        
    case JIT_EA_DI:
        /* LEA (d16,An),An - An + sign_extend(d16) */
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        jit_emit_mov_reg(ctx, dst_reg, addr_reg);
        jit_emit_add_immed(ctx, dst_reg, dst_reg, (int16_t)ext_word);
        break;
        
    case JIT_EA_AW:
        /* LEA (xxx).W,An - sign_extend(ext_word) */
        jit_emit_movz(ctx, dst_reg, ext_word & 0xFFFF, 0);
        if (ext_word & 0x8000) {
            jit_emit_movk(ctx, dst_reg, 0xFFFF, 1);
        }
        break;
        
    default:
        return -1;
    }
    
    return 0;
}

/*
 * Emit CLR <ea>
 */
static int emit_clr(jit_emit_context_t *ctx, uint16_t opcode)
{
    uint8_t size = misc_get_size(opcode);
    uint8_t ea_mode = misc_get_ea_mode(opcode);
    uint8_t ea_reg = misc_get_ea_reg(opcode);
    uint8_t addr_reg, temp_reg;
    
    /* CLR always sets N=0, Z=1, V=0, C=0 */
    jit_emit_movz(ctx, AARCH64_R1, 0, 0);
    jit_emit_lsl(ctx, AARCH64_R1, AARCH64_R1, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R1, 24);  /* n_flag = 0 */
    jit_emit_movz(ctx, AARCH64_R2, 1, 0);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R2, 28);  /* not_z_flag = 1 */
    jit_emit_movz(ctx, AARCH64_R3, 0, 0);
    jit_emit_lsl(ctx, AARCH64_R3, AARCH64_R3, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R3, 32);  /* v_flag = 0 */
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R3, 20);  /* c_flag = 0 */
    
    switch (ea_mode) {
    case JIT_EA_DN:
        /* CLR Dn - set register to 0 */
        temp_reg = AARCH64_R19 + ea_reg;
        if (size == 0) {
            jit_emit_and_immed(ctx, temp_reg, temp_reg, 0xFFFFFF00);
        } else if (size == 1) {
            jit_emit_and_immed(ctx, temp_reg, temp_reg, 0xFFFF0000);
        } else {
            jit_emit_movz(ctx, temp_reg, 0, 0);
        }
        break;
        
    case JIT_EA_AN:
        /* CLR An - no effect (undocumented) */
        break;
        
    case JIT_EA_AI:
        /* CLR (An) - store 0 to memory */
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        temp_reg = AARCH64_R16;
        jit_emit_movz(ctx, temp_reg, 0, 0);
        if (size == 0) {
            jit_emit_str_b(ctx, temp_reg, addr_reg, 0);
        } else if (size == 1) {
            jit_emit_str_h(ctx, temp_reg, addr_reg, 0);
        } else {
            jit_emit_str_w(ctx, temp_reg, addr_reg, 0);
        }
        break;
        
    case JIT_EA_DI:
    case JIT_EA_AW:
        /* CLR (d16,An) or CLR (xxx).W - not implemented for now */
        return -1;
        
    default:
        return -1;
    }
    
    return 0;
}

/*
 * Emit TST <ea>
 */
static int emit_tst(jit_emit_context_t *ctx, uint16_t opcode, uint16_t ext_word)
{
    uint8_t size = misc_get_size(opcode);
    uint8_t ea_mode = misc_get_ea_mode(opcode);
    uint8_t ea_reg = misc_get_ea_reg(opcode);
    uint8_t addr_reg, temp_reg;
    
    temp_reg = AARCH64_R16;
    
    switch (ea_mode) {
    case JIT_EA_DN:
        /* TST Dn - load from Dn */
        addr_reg = AARCH64_R19 + ea_reg;
        jit_emit_mov_reg(ctx, temp_reg, addr_reg);
        break;
        
    case JIT_EA_AN:
        /* TST An - no effect (undocumented) */
        jit_emit_movz(ctx, temp_reg, 0, 0);
        break;
        
    case JIT_EA_AI:
        /* TST (An) - load from memory */
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        if (size == 0) {
            jit_emit_ldr_b(ctx, temp_reg, addr_reg, 0);
        } else if (size == 1) {
            jit_emit_ldr_h(ctx, temp_reg, addr_reg, 0);
        } else {
            jit_emit_ldr_w(ctx, temp_reg, addr_reg, 0);
        }
        break;
        
    case JIT_EA_DI:
        /* TST (d16,An) - load from memory */
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        jit_emit_mov_reg(ctx, AARCH64_R17, addr_reg);
        jit_emit_add_immed(ctx, AARCH64_R17, AARCH64_R17, (int16_t)ext_word);
        if (size == 0) {
            jit_emit_ldr_b(ctx, temp_reg, AARCH64_R17, 0);
        } else if (size == 1) {
            jit_emit_ldr_h(ctx, temp_reg, AARCH64_R17, 0);
        } else {
            jit_emit_ldr_w(ctx, temp_reg, AARCH64_R17, 0);
        }
        break;
        
    case JIT_EA_AW:
        /* TST (xxx).W - load from memory */
        jit_emit_movz(ctx, AARCH64_R17, ext_word & 0xFFFF, 0);
        if (ext_word & 0x8000) {
            jit_emit_movk(ctx, AARCH64_R17, 0xFFFF, 1);
        }
        if (size == 0) {
            jit_emit_ldr_b(ctx, temp_reg, AARCH64_R17, 0);
        } else if (size == 1) {
            jit_emit_ldr_h(ctx, temp_reg, AARCH64_R17, 0);
        } else {
            jit_emit_ldr_w(ctx, temp_reg, AARCH64_R17, 0);
        }
        break;
        
    default:
        return -1;
    }
    
    /* Update flags: N, Z based on value, V=0, C=0 */
    int shift = (size == 0) ? 7 : (size == 1) ? 15 : 31;
    jit_emit_lsr(ctx, AARCH64_R1, temp_reg, shift);
    jit_emit_and_immed(ctx, AARCH64_R1, AARCH64_R1, 1);
    jit_emit_lsl(ctx, AARCH64_R1, AARCH64_R1, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R1, 24);  /* n_flag */
    
    uint32_t mask = (size == 0) ? 0xFF : (size == 1) ? 0xFFFF : 0xFFFFFFFF;
    jit_emit_and_immed(ctx, AARCH64_R2, temp_reg, mask);
    jit_emit_cmp(ctx, AARCH64_R2, 0);
    jit_emit_cset(ctx, AARCH64_R3, 1);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R3, 28);  /* not_z_flag */
    
    jit_emit_movz(ctx, AARCH64_R4, 0, 0);
    jit_emit_lsl(ctx, AARCH64_R4, AARCH64_R4, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R4, 32);  /* v_flag = 0 */
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R4, 20);  /* c_flag = 0 */
    
    return 0;
}

/*
 * Emit BTST/BSET/BCLR/BCHG <ea>,Dn
 */
static int emit_bitop(jit_emit_context_t *ctx, uint16_t opcode, uint16_t ext_word)
{
    uint8_t bitop_type = bitop_get_type(opcode);  /* 0=BTST, 1=BCHG, 2=BCLR, 3=BSET */
    uint8_t ea_mode = misc_get_ea_mode(opcode);
    uint8_t ea_reg = misc_get_ea_reg(opcode);
    uint8_t bit_reg = AARCH64_R19 + ea_reg;  /* Dn contains bit number */
    uint8_t addr_reg, temp_reg, bit_mask_reg;
    
    temp_reg = AARCH64_R16;
    bit_mask_reg = AARCH64_R17;
    
    /* Calculate bit mask: 1 << (bit_number & 31) for long, & 7 for byte */
    jit_emit_mov_reg(ctx, bit_mask_reg, bit_reg);
    if (ea_mode != JIT_EA_DN) {
        /* Byte operation: bit_number & 7 */
        jit_emit_and_immed(ctx, bit_mask_reg, bit_mask_reg, 7);
    } else {
        /* Long operation: bit_number & 31 */
        jit_emit_and_immed(ctx, bit_mask_reg, bit_mask_reg, 31);
    }
    /* Create mask: 1 << bit_number */
    jit_emit_movz(ctx, AARCH64_R18, 1, 0);
    jit_emit_lsl(ctx, bit_mask_reg, AARCH64_R18, bit_mask_reg);
    
    switch (ea_mode) {
    case JIT_EA_DN:
        /* Bit operation on Dn */
        addr_reg = AARCH64_R19 + ea_reg;
        
        /* Load value */
        jit_emit_mov_reg(ctx, temp_reg, addr_reg);
        
        /* Test bit: Z = !(value & mask) */
        jit_emit_and_reg(ctx, AARCH64_R19, temp_reg, bit_mask_reg);
        jit_emit_cmp(ctx, AARCH64_R19, 0);
        jit_emit_cset(ctx, AARCH64_R1, 1);  /* Z = (result == 0) */
        jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R1, 28);  /* not_z_flag */
        
        /* Apply operation if not BTST */
        if (bitop_type == 1) {  /* BCHG - toggle */
            jit_emit_eor_reg(ctx, temp_reg, temp_reg, bit_mask_reg);
        } else if (bitop_type == 2) {  /* BCLR - clear */
            jit_emit_and_reg(ctx, AARCH64_R19, bit_mask_reg, bit_mask_reg);
            jit_emit_eor_reg(ctx, AARCH64_R19, AARCH64_R19, AARCH64_R19);  /* R19 = 0 */
            jit_emit_and_reg(ctx, AARCH64_R19, temp_reg, AARCH64_R19);  /* temp & ~mask */
            /* Actually: temp & ~mask */
            jit_emit_mov_reg(ctx, AARCH64_R19, bit_mask_reg);
            jit_emit_eor_reg(ctx, AARCH64_R19, AARCH64_R19, AARCH64_R19);  /* ~mask (wrong) */
            /* Correct: use MVN */
            jit_emit_movz(ctx, AARCH64_R19, 0xFFFF, 0);
            jit_emit_movk(ctx, AARCH64_R19, 0xFFFF, 1);
            jit_emit_eor_reg(ctx, AARCH64_R19, AARCH64_R19, bit_mask_reg);  /* ~mask */
            jit_emit_and_reg(ctx, temp_reg, temp_reg, AARCH64_R19);
        } else if (bitop_type == 3) {  /* BSET - set */
            jit_emit_orr_reg(ctx, temp_reg, temp_reg, bit_mask_reg);
        }
        
        /* Store result back to Dn */
        if (bitop_type != 0) {  /* Not BTST */
            jit_emit_mov_reg(ctx, addr_reg, temp_reg);
        }
        break;
        
    case JIT_EA_AI:
        /* Bit operation on (An) - byte operation */
        addr_reg = AARCH64_R27 + ea_reg;
        if (ea_reg == 7) addr_reg = AARCH64_R9;
        
        /* Load byte from memory */
        jit_emit_ldr_b(ctx, temp_reg, addr_reg, 0);
        
        /* Test bit */
        jit_emit_and_reg(ctx, AARCH64_R19, temp_reg, bit_mask_reg);
        jit_emit_cmp(ctx, AARCH64_R19, 0);
        jit_emit_cset(ctx, AARCH64_R1, 1);
        jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R1, 28);  /* not_z_flag */
        
        /* Apply operation if not BTST */
        if (bitop_type == 1) {  /* BCHG */
            jit_emit_eor_reg(ctx, temp_reg, temp_reg, bit_mask_reg);
            jit_emit_str_b(ctx, temp_reg, addr_reg, 0);
        } else if (bitop_type == 2) {  /* BCLR */
            jit_emit_movz(ctx, AARCH64_R19, 0xFFFF, 0);
            jit_emit_movk(ctx, AARCH64_R19, 0xFFFF, 1);
            jit_emit_eor_reg(ctx, AARCH64_R19, AARCH64_R19, bit_mask_reg);
            jit_emit_and_reg(ctx, temp_reg, temp_reg, AARCH64_R19);
            jit_emit_str_b(ctx, temp_reg, addr_reg, 0);
        } else if (bitop_type == 3) {  /* BSET */
            jit_emit_orr_reg(ctx, temp_reg, temp_reg, bit_mask_reg);
            jit_emit_str_b(ctx, temp_reg, addr_reg, 0);
        }
        break;
        
    default:
        /* Other EA modes not implemented yet */
        return -1;
    }
    
    /* V=0, C=0 for all bit operations */
    jit_emit_movz(ctx, AARCH64_R1, 0, 0);
    jit_emit_lsl(ctx, AARCH64_R1, AARCH64_R1, 7);
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R1, 32);  /* v_flag */
    jit_emit_str_offset(ctx, AARCH64_R0, AARCH64_R1, 20);  /* c_flag */
    
    return 0;
}

int jit_translate_misc(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint16_t opcode, ext_word;
    int result = -1;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_misc invalid context\n");
        return -1;
    }
    
    opcode = ctx->opcode;
    ext_word = (ctx->ext_count > 0) ? ctx->ext_words[0] : 0;
    
    code_size = 256;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: MISC failed to allocate code buffer\n");
        return -1;
    }
    
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    if (misc_is_lea(opcode)) {
        result = emit_lea(&emit_ctx, opcode, ext_word);
    } else if (misc_is_clr(opcode)) {
        result = emit_clr(&emit_ctx, opcode);
    } else if (misc_is_tst(opcode)) {
        result = emit_tst(&emit_ctx, opcode, ext_word);
    } else if (misc_is_bitop(opcode)) {
        result = emit_bitop(&emit_ctx, opcode, ext_word);
    }
    
    if (result < 0) {
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    /* PC advance */
    int pc_advance = 2;
    if (misc_is_lea(opcode) && (misc_get_ea_mode(opcode) == JIT_EA_DI || 
                                misc_get_ea_mode(opcode) == JIT_EA_AW)) {
        pc_advance = 4;
    }
    jit_emit_add_immed(&emit_ctx, AARCH64_R10, AARCH64_R10, pc_advance);
    
    /* Cycles (approximate) */
    int cycles = 4;
    if (misc_is_lea(opcode)) cycles = 4;
    else if (misc_is_clr(opcode)) cycles = 6;
    else if (misc_is_tst(opcode)) cycles = 4;
    else if (misc_is_bitop(opcode)) cycles = 8;
    
    jit_emit_movz(&emit_ctx, AARCH64_R0, cycles, 0);
    
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: MISC code emission failed\n");
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
    
    LOG_DEBUG("[CPU] m68xkcpu: MISC translated (code_size=%zu bytes)\n", emit_ctx.offset);
    
    return 0;
}
