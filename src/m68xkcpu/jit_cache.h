/*
 * JIT Cache Header
 * 
 * Manages the code cache for compiled blocks.
 */

#ifndef JIT_CACHE_H
#define JIT_CACHE_H

#include "jit.h"
#include "jit_block.h"

/* Initialize the code cache */
int jit_cache_init(jit_context_t *jit);

/* Shutdown the code cache */
void jit_cache_shutdown(jit_context_t *jit);

/* Reset the cache (invalidate all blocks) */
void jit_cache_reset(jit_context_t *jit);

/* Allocate space in the cache */
void *jit_cache_alloc(jit_context_t *jit, size_t size);

/* Free space in the cache */
void jit_cache_free(jit_context_t *jit, void *ptr, size_t size);

/* Flush instruction cache for a region */
void jit_cache_flush(jit_context_t *jit, void *start, size_t size);

/* Lookup a block by PC */
jit_block_t *jit_cache_lookup(jit_context_t *jit, uint32_t pc);

/* Insert a block into the cache */
void jit_cache_insert(jit_context_t *jit, jit_block_t *block);

/* Remove a block from the cache */
void jit_cache_remove(jit_context_t *jit, jit_block_t *block);

/* Interpret-only block management */
int jit_cache_should_add_interpret_block(jit_context_t *jit);
void jit_cache_evict_oldest_interpret_block(jit_context_t *jit);

/* Get cache statistics */
size_t jit_cache_used(jit_context_t *jit);
size_t jit_cache_free_space(jit_context_t *jit);

#endif /* JIT_CACHE_H */
