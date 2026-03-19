/*
 * JIT Translator Interface
 *
 * Single source of truth:
 * - block-based translation
 * - one translator context type (jit_translate_context_t)
 * - external AArch64 emitter implementation
 */

#ifndef JIT_TRANSLATE_H
#define JIT_TRANSLATE_H

#include <stdint.h>
#include "jit.h"
#include "jit_emit_aarch64.h"

typedef struct {
    jit_context_t *jit;
    jit_block_t *block;
    jit_emit_context_t *emit;
    uint16_t opcode;
    uint16_t *ext_words;
    int ext_count;
    uint16_t instruction_index;
} jit_translate_context_t;

typedef int (*jit_translator_fn)(jit_translate_context_t *ctx);

int jit_translate_nop(jit_translate_context_t *ctx);
int jit_translate_moveq(jit_translate_context_t *ctx);
int jit_translate_move(jit_translate_context_t *ctx);
int jit_translate_add(jit_translate_context_t *ctx);
int jit_translate_addq(jit_translate_context_t *ctx);
int jit_translate_sub(jit_translate_context_t *ctx);
int jit_translate_subq(jit_translate_context_t *ctx);
int jit_translate_cmp(jit_translate_context_t *ctx);
int jit_translate_logic(jit_translate_context_t *ctx);
int jit_translate_branch(jit_translate_context_t *ctx);
int jit_translate_bsr(jit_translate_context_t *ctx);
int jit_translate_rts(jit_translate_context_t *ctx);
int jit_translate_jsr(jit_translate_context_t *ctx);
int jit_translate_jmp(jit_translate_context_t *ctx);
int jit_translate_movec(jit_translate_context_t *ctx);
int jit_translate_extb(jit_translate_context_t *ctx);
int jit_translate_lea(jit_translate_context_t *ctx);
int jit_translate_misc(jit_translate_context_t *ctx);

#endif /* JIT_TRANSLATE_H */
