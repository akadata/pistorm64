/*
 * JIT Core Implementation
 * 
 * Main execution engine for the AArch64 JIT layer.
 */

#include "jit.h"
#include "jit_block.h"
#include "jit_cache.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Global JIT context */
jit_context_t g_jit = {0};

/* External Musashi CPU reference */
extern struct m68ki_cpu_core *g_m68k_cpu;


/**
 * Initialize the JIT subsystem
 * 
 * @param cpu Pointer to the Musashi CPU state
 * @param cache_size Size of the code cache in bytes
 * @return 0 on success, -1 on failure
 */
int jit_init(struct m68ki_cpu_core *cpu, size_t cache_size)
{
    if (g_jit.initialized) {
        fprintf(stderr, "JIT: Already initialized\n");
        return -1;
    }

    memset(&g_jit, 0, sizeof(g_jit));
    
    g_jit.cpu = cpu;
    g_jit.cache_size = cache_size > 0 ? cache_size : JIT_CACHE_SIZE;
    
    /* Initialize cache */
    if (jit_cache_init(&g_jit) != 0) {
        fprintf(stderr, "JIT: Failed to initialize code cache\n");
        return -1;
    }
    
    /* Initialize hash table */
    memset(g_jit.hash_table, 0, sizeof(g_jit.hash_table));
    
    g_jit.enabled = true;
    g_jit.initialized = true;
    
    printf("JIT: Initialized with %zu KB cache\n", g_jit.cache_size / 1024);
    
    return 0;
}


/**
 * Shutdown the JIT subsystem
 */
void jit_shutdown(void)
{
    if (!g_jit.initialized) {
        return;
    }
    
    jit_cache_shutdown(&g_jit);
    
    memset(&g_jit, 0, sizeof(g_jit));
    
    printf("JIT: Shutdown complete\n");
}


/**
 * Reset JIT state (called on CPU reset)
 */
void jit_reset(void)
{
    if (!g_jit.initialized) {
        return;
    }
    
    /* Invalidate all compiled blocks on reset */
    jit_invalidate_all();
    
    printf("JIT: Reset complete\n");
}


/**
 * Execute code starting at the given PC
 * 
 * @param pc Program counter to start execution at
 * @param cycles Maximum cycles to execute
 * @return Number of cycles executed, or negative on error
 */
int jit_execute(uint32_t pc, int cycles)
{
    jit_block_t *block;
    int cycles_remaining = cycles;
    
    if (!g_jit.initialized || !g_jit.enabled) {
        return -1;
    }
    
    g_jit.current_pc = pc;
    
    /* Main execution loop */
    while (cycles_remaining > 0) {
        /* Look up compiled block */
        block = jit_cache_lookup(&g_jit, pc);
        
        if (block != NULL) {
            /* Block found - execute compiled code */
            g_jit.stats.cache_hits++;
            g_jit.stats.blocks_executed++;
            
            /* Execute the compiled block */
            int block_cycles = jit_block_execute(block, cycles_remaining);
            
            if (block_cycles < 0) {
                /* Block execution requested fallback */
                break;
            }
            
            cycles_remaining -= block_cycles;
            
            /* Update PC after block execution */
            pc = g_jit.current_pc;
            
            /* Check if block ended with a branch/exception */
            if (block->ends_block) {
                /* Continue to next block */
                continue;
            }
        } else {
            /* Block not found - compile or interpret */
            g_jit.stats.cache_misses++;
            
            /* Try to compile the block */
            block = jit_compile_block(pc);
            
            if (block != NULL) {
                /* Successfully compiled - execute it */
                g_jit.stats.blocks_compiled++;
                
                int block_cycles = jit_block_execute(block, cycles_remaining);
                
                if (block_cycles < 0) {
                    break;
                }
                
                cycles_remaining -= block_cycles;
                pc = g_jit.current_pc;
            } else {
                /* Compilation failed - fall back to interpreter */
                g_jit.stats.fallback_count++;
                
                /* Execute one instruction via Musashi */
                /* This will be implemented when we wire up the interpreter */
                break;
            }
        }
    }
    
    return cycles - cycles_remaining;
}


/**
 * Invalidate a range of addresses in the JIT cache
 * 
 * Called when memory is written to, to invalidate any compiled
 * blocks that overlap with the modified range.
 * 
 * @param start_addr Start address of modified range
 * @param end_addr End address of modified range
 */
void jit_invalidate_range(uint32_t start_addr, uint32_t end_addr)
{
    if (!g_jit.initialized) {
        return;
    }
    
    /* Walk through all blocks and invalidate those in range */
    for (int i = 0; i < JIT_HASH_SIZE; i++) {
        jit_block_t *block = g_jit.hash_table[i];
        jit_block_t *next;
        
        while (block != NULL) {
            next = block->hash_next;
            
            /* Check if block overlaps with invalidated range */
            if (block->start_pc >= start_addr && block->start_pc < end_addr) {
                jit_block_invalidate(&g_jit, block);
                g_jit.stats.cache_invalidations++;
            }
            
            block = next;
        }
    }
}


/**
 * Invalidate all compiled blocks
 */
void jit_invalidate_all(void)
{
    if (!g_jit.initialized) {
        return;
    }
    
    /* Clear hash table */
    for (int i = 0; i < JIT_HASH_SIZE; i++) {
        g_jit.hash_table[i] = NULL;
    }
    
    /* Reset cache pointer (all blocks invalid) */
    jit_cache_reset(&g_jit);
    
    g_jit.stats.cache_invalidations++;
}


/**
 * Print JIT statistics
 */
void jit_print_stats(void)
{
    if (!g_jit.initialized) {
        printf("JIT: Not initialized\n");
        return;
    }
    
    printf("\n=== JIT Statistics ===\n");
    printf("Blocks compiled:      %u\n", g_jit.stats.blocks_compiled);
    printf("Blocks executed:      %u\n", g_jit.stats.blocks_executed);
    printf("Cache hits:           %u\n", g_jit.stats.cache_hits);
    printf("Cache misses:         %u\n", g_jit.stats.cache_misses);
    printf("Fallback count:       %u\n", g_jit.stats.fallback_count);
    printf("Cache invalidations:  %u\n", g_jit.stats.cache_invalidations);
    printf("Cache bytes used:     %zu / %zu\n", 
           g_jit.stats.cache_bytes_used, g_jit.cache_size);
    printf("Cache bytes free:     %zu\n", g_jit.stats.cache_bytes_free);
    
    if (g_jit.stats.cache_hits + g_jit.stats.cache_misses > 0) {
        double hit_rate = (double)g_jit.stats.cache_hits / 
                         (g_jit.stats.cache_hits + g_jit.stats.cache_misses) * 100.0;
        printf("Cache hit rate:       %.2f%%\n", hit_rate);
    }
    
    printf("======================\n\n");
}


/**
 * Compile a basic block starting at the given PC
 * 
 * @param pc Program counter to start compilation at
 * @return Pointer to compiled block, or NULL on failure
 */
jit_block_t *jit_compile_block(uint32_t pc)
{
    jit_block_t *block;
    
    /* Allocate a new block structure */
    block = jit_block_alloc(&g_jit, pc);
    if (block == NULL) {
        return NULL;
    }
    
    /* Translate instructions until we hit a block boundary */
    if (jit_block_translate(&g_jit, block) != 0) {
        /* Translation failed - free the block */
        jit_block_free(&g_jit, block);
        return NULL;
    }
    
    /* Emit the compiled code */
    if (jit_block_emit(&g_jit, block) != 0) {
        /* Emission failed - free the block */
        jit_block_free(&g_jit, block);
        return NULL;
    }
    
    /* Insert block into cache */
    jit_cache_insert(&g_jit, block);
    
    return block;
}
