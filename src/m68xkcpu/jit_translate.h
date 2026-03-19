/*
 * JIT Translator Interface - Unified Model
 * 
 * Conventions:
 * - X19 holds pointer to m68ki_cpu state structure
 * - Translators append to emit context (no buffer allocation)
 * - Single block builder manages instruction_count and code_ptr
 */

#ifndef JIT_TRANSLATE_H
#define JIT_TRANSLATE_H

#include <stdint.h>
#include "jit_emit_aarch64.h"

/* Translator function type - appends to emit context */
typedef int (*jit_translator_fn)(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);

/* Translator declarations - all use unified signature */
int jit_translate_nop(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_moveq(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_move(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_add(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_addq(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_sub(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_subq(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_cmp(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_logic(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_branch(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_bsr(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_rts(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_jsr(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_jmp(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_movec(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_extb(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_lea(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);
int jit_translate_misc(jit_emit_context_t *ctx, uint16_t opcode, uint16_t *ext_words, int ext_count);

#endif /* JIT_TRANSLATE_H */
