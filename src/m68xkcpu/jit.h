/*
 * JIT Core Header
 * 
 * Main interface for the AArch64 JIT layer.
 */

#ifndef JIT_H
#define JIT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "generated/jit_68000_opinfo.h"

/* Forward declarations */
struct m68ki_cpu_core;

/* JIT cache configuration */
#define JIT_CACHE_SIZE          (16 * 1024 * 1024)  /* 16 MB default */
#define JIT_MAX_BLOCKS          65536
#define JIT_MAX_BLOCK_SIZE      4096                /* Max bytes per compiled block */
#define JIT_HASH_SIZE           4096                /* Hash table size for block lookup */
#define JIT_MAX_INTERPRET_BLOCKS  8192              /* Max interpret-only blocks to prevent unbounded growth */

/* JIT statistics */
typedef struct {
    uint32_t blocks_compiled;
    uint32_t blocks_executed;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t fast_step_count;
    uint32_t fallback_count;
    uint32_t cache_invalidations;
    uint32_t interpret_blocks_count;    /* Current count of interpret-only blocks */
    uint32_t interpret_blocks_evicted;  /* Count of evicted interpret-only blocks */
    size_t cache_bytes_used;
    size_t cache_bytes_free;
} jit_stats_t;

/* JIT block handle */
typedef struct jit_block jit_block_t;

/* JIT context */
typedef struct {
    /* Cache management */
    uint8_t *cache_base;          /* Base of code cache */
    uint8_t *cache_ptr;           /* Current allocation pointer */
    size_t cache_size;            /* Total cache size */
    
    /* Block lookup */
    jit_block_t *hash_table[JIT_HASH_SIZE];
    
    /* Current execution state */
    jit_block_t *current_block;
    uint32_t current_pc;
    
    /* Statistics */
    jit_stats_t stats;
    
    /* CPU state sync */
    struct m68ki_cpu_core *cpu;

    /* 68040 MMU tracking for cache safety */
    uint64_t mmu_state_sig;
    bool mmu_state_valid;
    
    /* Flags */
    bool initialized;
    bool enabled;
} jit_context_t;

/* Global JIT context */
extern jit_context_t g_jit;

/* Initialization */
int jit_init(struct m68ki_cpu_core *cpu, size_t cache_size);
void jit_shutdown(void);
void jit_reset(void);

/* Execution */
int jit_execute(uint32_t pc, int cycles);

/* Hard fail mode - abort on unsupported instructions */
int jit_no_fallback_enabled(void);
void jit_hard_fail(uint32_t pc, uint16_t opcode, const char *reason);

/* Cache management */
void jit_invalidate_range(uint32_t start_addr, uint32_t end_addr);
void jit_invalidate_all(void);

/* Statistics */
void jit_print_stats(void);

/* Block compilation */
jit_block_t *jit_compile_block(uint32_t pc);

#endif /* JIT_H */
