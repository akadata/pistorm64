/*
 * JIT Block Header
 * 
 * Manages compiled basic blocks.
 */

#ifndef JIT_BLOCK_H
#define JIT_BLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "jit.h"

/* Maximum instructions per block */
#define JIT_MAX_BLOCK_INSTRUCTIONS  64

/* Block flags */
#define JIT_BLOCK_VALID         0x01    /* Block is valid and executable */
#define JIT_BLOCK_ENDS_BRANCH   0x02    /* Block ends with a branch */
#define JIT_BLOCK_ENDS_TRAP     0x04    /* Block ends with a trap/exception */
#define JIT_BLOCK_ENDS_RTS      0x08    /* Block ends with RTS */
#define JIT_BLOCK_ENDS_RTE      0x10    /* Block ends with RTE */
#define JIT_BLOCK_ENDS_JMP      0x20    /* Block ends with JMP/JSR */

/* Compiled block structure */
struct jit_block {
    /* Lookup key */
    uint32_t start_pc;              /* Starting PC of this block */
    uint32_t end_pc;                /* Ending PC (after last instruction) */
    
    /* Code cache location */
    uint8_t *code_ptr;              /* Pointer to compiled code in cache */
    size_t code_size;               /* Size of compiled code in bytes */
    
    /* Block metadata */
    uint16_t instruction_count;     /* Number of instructions in block */
    uint16_t flags;                 /* Block flags */
    uint8_t ends_block;             /* Block ends with control flow change */
    uint8_t cycle_count;            /* Base cycle count for the block */
    
    /* Hash table linkage */
    jit_block_t *hash_next;         /* Next block in hash chain */
    
    /* Instruction info for debugging */
    struct {
        uint16_t opcode;
        uint16_t ext_words[4];
        uint8_t ext_count;
        uint8_t cycles;
    } instructions[JIT_MAX_BLOCK_INSTRUCTIONS];
};

/* Block allocation */
jit_block_t *jit_block_alloc(jit_context_t *jit, uint32_t pc);
void jit_block_free(jit_context_t *jit, jit_block_t *block);

/* Block translation */
int jit_block_translate(jit_context_t *jit, jit_block_t *block);

/* Block emission */
int jit_block_emit(jit_context_t *jit, jit_block_t *block);

/* Block execution */
int jit_block_execute(jit_block_t *block, int max_cycles);

/* Block invalidation */
void jit_block_invalidate(jit_context_t *jit, jit_block_t *block);

/* Block debugging */
void jit_block_dump(jit_block_t *block);

#endif /* JIT_BLOCK_H */
