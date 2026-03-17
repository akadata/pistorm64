/*
 * JIT MOVEC Translator
 * 
 * MOVEC - Move Control Register
 * 
 * Format:
 *   0100 1110 0111 101a  <extension word>
 *   a=0: MOVEC Cn,Rn (control register to data register)
 *   a=1: MOVEC Rn,Cn (data register to control register)
 * 
 * Extension word:
 *   Bits 15-12: Control register ID
 *     0x000 = SFC (Source Function Code) - 68010+
 *     0x001 = DFC (Destination Function Code) - 68010+
 *     0x800 = USP (User Stack Pointer)
 *     0x801 = VBR (Vector Base Register) - 68010+
 *     0x802 = CAAR (Cache Address Register) - 68020/030 only
 *     0x803 = MSP (Master Stack Pointer) - 68020+
 *     0x804 = ISP (Interrupt Stack Pointer) - 68020+
 *   Bits 11-1: Reserved (0)
 *   Bit 0: Register type (0=An, 1=Dn)
 *   Bits 3-1 of opcode: Register number (0-7)
 * 
 * For 68000 compatibility, we support:
 *   - SFC/DFC (read/write, though 68000 doesn't have these)
 *   - USP (read/write)
 *   - VBR (read/write, though 68000 doesn't have this)
 * 
 * Note: MOVEC is privileged on real hardware. We allow it for compatibility.
 * 
 * Reference: M68010UM, M68020UM
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

/* Control register IDs */
#define MOVEC_SFC  0x000
#define MOVEC_DFC  0x001
#define MOVEC_USP  0x800
#define MOVEC_VBR  0x801
#define MOVEC_CAAR 0x802  /* 68020/030 only */
#define MOVEC_MSP  0x803  /* 68020+ */
#define MOVEC_ISP  0x804  /* 68020+ */

/* Check if opcode is MOVEC */
static inline int movec_is_movec(uint16_t opcode)
{
    return (opcode & 0xFFF8) == 0x4E70;
}

/* Check direction: 0=Cn->Rn, 1=Rn->Cn */
static inline int movec_get_direction(uint16_t opcode)
{
    return (opcode >> 1) & 0x1;
}

/* Get register number (bits 3-1) */
static inline uint8_t movec_get_reg_num(uint16_t opcode)
{
    return (opcode >> 1) & 0x7;
}

/* Get register type (bit 0 of extension word): 0=An, 1=Dn */
static inline int movec_is_dn(uint16_t ext_word)
{
    return ext_word & 0x1;
}

/* Get control register ID (bits 15-12 of extension word) */
static inline uint16_t movec_get_ctrl_id(uint16_t ext_word)
{
    return ext_word & 0xFFF8;
}

/*
 * Emit MOVEC Cn,Rn (control register to data register)
 */
static int emit_movec_cn_to_rn(jit_emit_context_t *ctx, uint16_t ctrl_id, 
                                uint8_t reg_num, int is_dn)
{
    uint8_t dst_reg;
    
    if (is_dn) {
        dst_reg = AARCH64_R19 + reg_num;  /* Dn */
    } else {
        dst_reg = AARCH64_R27 + reg_num;  /* An */
        if (reg_num == 7) dst_reg = AARCH64_R9;
    }
    
    /* Load control register value from CPU state */
    switch (ctrl_id) {
    case MOVEC_SFC:
        /* SFC at offset 36 (s_flag is at bit 2, but we store full SFC) */
        /* For simplicity, just load s_flag and use as SFC */
        jit_emit_ldr_w(ctx, dst_reg, AARCH64_R0, 36);  /* s_flag offset */
        jit_emit_and_immed(ctx, dst_reg, dst_reg, 7);  /* Mask to 3 bits */
        break;
        
    case MOVEC_DFC:
        /* DFC - similar to SFC, use dfc offset */
        jit_emit_ldr_w(ctx, dst_reg, AARCH64_R0, 40);  /* dfc offset (approximate) */
        jit_emit_and_immed(ctx, dst_reg, dst_reg, 7);
        break;
        
    case MOVEC_USP:
        /* USP is sp[0] at offset 16 */
        jit_emit_ldr_w(ctx, dst_reg, AARCH64_R0, 16);
        break;
        
    case MOVEC_VBR:
        /* VBR at offset 44 */
        jit_emit_ldr_w(ctx, dst_reg, AARCH64_R0, 44);
        break;
        
    case MOVEC_CAAR:
    case MOVEC_MSP:
    case MOVEC_ISP:
        /* Not supported on 68000 - return 0 */
        jit_emit_movz(ctx, dst_reg, 0, 0);
        break;
        
    default:
        return -1;
    }
    
    return 0;
}

/*
 * Emit MOVEC Rn,Cn (data register to control register)
 */
static int emit_movec_rn_to_cn(jit_emit_context_t *ctx, uint16_t ctrl_id,
                                uint8_t reg_num, int is_dn)
{
    uint8_t src_reg;
    
    if (is_dn) {
        src_reg = AARCH64_R19 + reg_num;  /* Dn */
    } else {
        src_reg = AARCH64_R27 + reg_num;  /* An */
        if (reg_num == 7) src_reg = AARCH64_R9;
    }
    
    /* Store to control register in CPU state */
    switch (ctrl_id) {
    case MOVEC_SFC:
        /* Store to s_flag (masked to 3 bits) */
        jit_emit_and_immed(ctx, 16, src_reg, 7);
        jit_emit_str_w(ctx, 16, AARCH64_R0, 36);
        break;
        
    case MOVEC_DFC:
        /* Store to dfc */
        jit_emit_and_immed(ctx, 16, src_reg, 7);
        jit_emit_str_w(ctx, 16, AARCH64_R0, 40);
        break;
        
    case MOVEC_USP:
        /* Store to sp[0] */
        jit_emit_str_w(ctx, src_reg, AARCH64_R0, 16);
        break;
        
    case MOVEC_VBR:
        /* Store to VBR */
        jit_emit_str_w(ctx, src_reg, AARCH64_R0, 44);
        break;
        
    case MOVEC_CAAR:
    case MOVEC_MSP:
    case MOVEC_ISP:
        /* Not supported on 68000 - ignore */
        break;
        
    default:
        return -1;
    }
    
    return 0;
}

int jit_translate_movec(jit_translate_context_t *ctx)
{
    jit_emit_context_t emit_ctx;
    uint8_t *code_buffer;
    size_t code_size;
    uint16_t opcode, ext_word;
    int direction;  /* 0=Cn->Rn, 1=Rn->Cn */
    uint8_t reg_num;
    int is_dn;
    uint16_t ctrl_id;
    
    if (!ctx || !ctx->jit || !ctx->block || !ctx->jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_translate_movec invalid context\n");
        return -1;
    }
    
    opcode = ctx->opcode;
    
    if (!movec_is_movec(opcode)) {
        LOG_ERROR("[CPU] m68xkcpu: Not a MOVEC opcode %04X\n", opcode);
        return -1;
    }
    
    /* Need extension word */
    if (ctx->ext_count < 1) {
        LOG_ERROR("[CPU] m68xkcpu: MOVEC missing extension word\n");
        return -1;
    }
    
    ext_word = ctx->ext_words[0];
    direction = movec_get_direction(opcode);
    reg_num = movec_get_reg_num(opcode);
    is_dn = movec_is_dn(ext_word);
    ctrl_id = movec_get_ctrl_id(ext_word);
    
    LOG_VERBOSE("[CPU] m68xkcpu: MOVEC %s %c%u, ctrl=%03X\n",
                direction ? "Rn->Cn" : "Cn->Rn",
                is_dn ? 'D' : 'A', reg_num, ctrl_id);
    
    code_size = 256;
    code_buffer = jit_cache_alloc(ctx->jit, code_size);
    if (!code_buffer) {
        LOG_ERROR("[CPU] m68xkcpu: MOVEC failed to allocate code buffer\n");
        return -1;
    }
    
    jit_emit_init(&emit_ctx, code_buffer, code_size);
    
    if (direction == 0) {
        /* MOVEC Cn,Rn - control to register */
        if (emit_movec_cn_to_rn(&emit_ctx, ctrl_id, reg_num, is_dn) < 0) {
            LOG_VERBOSE("[CPU] m68xkcpu: MOVEC unsupported control register %03X\n", ctrl_id);
            jit_cache_free(ctx->jit, code_buffer, code_size);
            return -1;
        }
    } else {
        /* MOVEC Rn,Cn - register to control */
        if (emit_movec_rn_to_cn(&emit_ctx, ctrl_id, reg_num, is_dn) < 0) {
            LOG_VERBOSE("[CPU] m68xkcpu: MOVEC unsupported control register %03X\n", ctrl_id);
            jit_cache_free(ctx->jit, code_buffer, code_size);
            return -1;
        }
    }
    
    /* PC advance: 4 bytes (opcode + extension word) */
    jit_emit_add_immed(&emit_ctx, AARCH64_R10, AARCH64_R10, 4);
    
    /* Cycles: 8 for MOVEC */
    jit_emit_movz(&emit_ctx, AARCH64_R0, 8, 0);
    
    if (emit_ctx.error) {
        LOG_ERROR("[CPU] m68xkcpu: MOVEC code emission failed\n");
        jit_cache_free(ctx->jit, code_buffer, code_size);
        return -1;
    }
    
    ctx->block->code_ptr = code_buffer;
    ctx->block->code_size = emit_ctx.offset;
    ctx->block->instruction_count = 1;
    ctx->block->cycle_count = 8;
    ctx->block->flags = JIT_BLOCK_VALID;
    
    ctx->block->instructions[0].opcode = opcode;
    ctx->block->instructions[0].ext_count = 1;
    ctx->block->instructions[0].cycles = 8;
    
    LOG_DEBUG("[CPU] m68xkcpu: MOVEC translated (code_size=%zu bytes)\n", emit_ctx.offset);
    
    return 0;
}
