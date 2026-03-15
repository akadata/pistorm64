/*
 * JIT Translator Header
 * 
 * Interface for translating 68k instructions to intermediate representation.
 */

#ifndef JIT_TRANSLATE_H
#define JIT_TRANSLATE_H

#include <stdint.h>
#include "jit.h"
#include "jit_block.h"

/* Translator context */
typedef struct {
    jit_context_t *jit;
    jit_block_t *block;
    
    /* Current instruction state */
    uint16_t opcode;
    uint16_t ext_words[4];
    int ext_count;
    
    /* EA decoding */
    uint8_t src_ea_mode;
    uint8_t src_ea_reg;
    uint8_t dst_ea_mode;
    uint8_t dst_ea_reg;
    
    /* Size being operated on */
    uint8_t op_size;  /* 0=byte, 1=word, 2=long */
    
    /* Register allocations for this instruction */
    uint8_t src_reg;
    uint8_t dst_reg;
} jit_translate_context_t;

/* Initialize translator context */
void jit_translate_init(jit_translate_context_t *ctx, 
                        jit_context_t *jit, 
                        jit_block_t *block);

/* Decode effective address from opcode */
int jit_translate_decode_ea(jit_translate_context_t *ctx, 
                            uint16_t opcode, 
                            int is_src);

/* Read effective address value */
int jit_translate_read_ea(jit_translate_context_t *ctx, 
                          uint8_t ea_mode, 
                          uint8_t ea_reg,
                          uint8_t size);

/* Write effective address value */
int jit_translate_write_ea(jit_translate_context_t *ctx, 
                           uint8_t ea_mode, 
                           uint8_t ea_reg,
                           uint8_t size,
                           uint8_t value_reg);

/* Translate specific instruction families */
int jit_translate_move(jit_translate_context_t *ctx);
int jit_translate_moveq(jit_translate_context_t *ctx);
int jit_translate_add(jit_translate_context_t *ctx);
int jit_translate_sub(jit_translate_context_t *ctx);
int jit_translate_cmp(jit_translate_context_t *ctx);
int jit_translate_and(jit_translate_context_t *ctx);
int jit_translate_or(jit_translate_context_t *ctx);
int jit_translate_eor(jit_translate_context_t *ctx);
int jit_translate_branch(jit_translate_context_t *ctx);
int jit_translate_shift(jit_translate_context_t *ctx);

/* Helper: get size in bytes */
static inline int jit_size_bytes(uint8_t size)
{
    switch (size) {
        case 0: return 1;   /* Byte */
        case 1: return 2;   /* Word */
        case 2: return 4;   /* Long */
        default: return 0;
    }
}

/* Helper: get size mask */
static inline uint32_t jit_size_mask(uint8_t size)
{
    switch (size) {
        case 0: return 0xFF;
        case 1: return 0xFFFF;
        case 2: return 0xFFFFFFFF;
        default: return 0;
    }
}

#endif /* JIT_TRANSLATE_H */
