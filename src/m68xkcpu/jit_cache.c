/*
 * JIT Cache Implementation
 * 
 * Manages the code cache for compiled blocks.
 */

#include "jit.h"
#include "jit_cache.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>

/* Hash function for PC lookup */
static inline uint32_t jit_cache_hash_pc(uint32_t pc)
{
    uint32_t hash = pc * 2654435761u;
    return (hash >> 16) & (JIT_HASH_SIZE - 1);
}


/**
 * Initialize the code cache
 * 
 * Allocates executable memory for compiled code.
 * 
 * @param jit JIT context
 * @return 0 on success, -1 on failure
 */
int jit_cache_init(jit_context_t *jit)
{
    void *cache;
    
    if (jit == NULL || jit->cache_size == 0) {
        return -1;
    }
    
    /* Allocate executable memory using mmap */
    cache = mmap(NULL, jit->cache_size,
                 PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS,
                 -1, 0);
    
    if (cache == MAP_FAILED) {
        LOG_ERROR("[CPU] m68xkcpu: mmap failed for code cache (%s)\n", strerror(errno));
        return -1;
    }
    
    jit->cache_base = (uint8_t *)cache;
    jit->cache_ptr = jit->cache_base;
    jit->stats.cache_bytes_used = 0;
    jit->stats.cache_bytes_free = jit->cache_size;
    
    return 0;
}


/**
 * Shutdown the code cache
 * 
 * Frees the executable memory.
 * 
 * @param jit JIT context
 */
void jit_cache_shutdown(jit_context_t *jit)
{
    if (jit == NULL || jit->cache_base == NULL) {
        return;
    }
    
    munmap(jit->cache_base, jit->cache_size);
    
    jit->cache_base = NULL;
    jit->cache_ptr = NULL;
    jit->stats.cache_bytes_used = 0;
    jit->stats.cache_bytes_free = 0;
}


/**
 * Reset the cache
 * 
 * Invalidates all blocks by resetting the allocation pointer.
 * Note: This doesn't actually free individual blocks - it just
 * marks all space as available again.
 * 
 * @param jit JIT context
 */
void jit_cache_reset(jit_context_t *jit)
{
    if (jit == NULL || jit->cache_base == NULL) {
        return;
    }
    
    jit->cache_ptr = jit->cache_base;
    jit->stats.cache_bytes_used = 0;
    jit->stats.cache_bytes_free = jit->cache_size;
}


/**
 * Allocate space in the cache
 * 
 * Uses a simple bump allocator - no free list management.
 * For a production JIT, you'd want a more sophisticated allocator.
 * 
 * @param jit JIT context
 * @param size Number of bytes to allocate
 * @return Pointer to allocated space, or NULL if insufficient space
 */
void *jit_cache_alloc(jit_context_t *jit, size_t size)
{
    uint8_t *ptr;
    
    if (jit == NULL || jit->cache_base == NULL) {
        return NULL;
    }
    
    /* Align to 16 bytes for AArch64 */
    size = (size + 15) & ~15;
    
    /* Check if we have enough space */
    if (jit->cache_ptr + size > jit->cache_base + jit->cache_size) {
        /* Cache full - could trigger GC in a more sophisticated implementation */
        return NULL;
    }
    
    ptr = jit->cache_ptr;
    jit->cache_ptr += size;
    jit->stats.cache_bytes_used += size;
    jit->stats.cache_bytes_free -= size;
    
    return ptr;
}


/**
 * Free space in the cache
 * 
 * In this simple implementation, we don't actually free space.
 * A production JIT would maintain a free list or use a region-based
 * allocator.
 * 
 * @param jit JIT context
 * @param ptr Pointer to free (ignored in simple implementation)
 * @param size Size of allocation (ignored in simple implementation)
 */
void jit_cache_free(jit_context_t *jit, void *ptr, size_t size)
{
    uint8_t *p;
    size_t aligned;
    if (jit == NULL || ptr == NULL || size == 0) {
        return;
    }

    /* Opportunistic LIFO reclaim for top-of-cache frees. */
    p = (uint8_t *)ptr;
    aligned = (size + 15u) & ~15u;
    if (p + aligned == jit->cache_ptr) {
        jit->cache_ptr = p;
        if (jit->stats.cache_bytes_used >= aligned) {
            jit->stats.cache_bytes_used -= aligned;
        } else {
            jit->stats.cache_bytes_used = 0;
        }
        jit->stats.cache_bytes_free = jit->cache_size - jit->stats.cache_bytes_used;
    }
}


/**
 * Flush instruction cache for a region
 * 
 * Ensures that newly generated code is visible to the instruction
 * fetch unit. On AArch64, this requires explicit cache maintenance.
 * 
 * @param jit JIT context
 * @param start Start of region to flush
 * @param size Size of region to flush
 */
void jit_cache_flush(jit_context_t *jit, void *start, size_t size)
{
    if (jit == NULL || start == NULL || size == 0) {
        return;
    }
    
    /* 
     * On AArch64, we need to:
     * 1. Clean data cache to point of unification
     * 2. Invalidate instruction cache to point of unification
     * 
     * For now, we use the __builtin___clear_cache which handles
     * this portably. In production code, you might want inline
     * assembly for better control.
     */
    __builtin___clear_cache((char *)start, (char *)start + size);
}


/**
 * Lookup a block by PC
 * 
 * @param jit JIT context
 * @param pc Program counter to look up
 * @return Pointer to block, or NULL if not found
 */
jit_block_t *jit_cache_lookup(jit_context_t *jit, uint32_t pc)
{
    uint32_t hash;
    jit_block_t *block;
    
    /* DEBUG: Instrument lookup for 0x00F80BD4 */
    bool debug_lookup = (pc == 0x00F80BD4);
    if (debug_lookup) {
        LOG_ERROR("[JIT-DEBUG] CACHE LOOKUP PC=0x%08X jit=%p\n", pc, (void*)jit);
    }
    
    if (jit == NULL) {
        if (debug_lookup) {
            LOG_ERROR("[JIT-DEBUG] CACHE LOOKUP FAILED: jit=NULL\n");
        }
        return NULL;
    }
    
    hash = jit_cache_hash_pc(pc);
    block = jit->hash_table[hash];
    
    if (debug_lookup) {
        LOG_ERROR("[JIT-DEBUG] hash=0x%08X table[hash]=%p\n", hash, (void*)block);
    }
    
    while (block != NULL) {
        if (debug_lookup) {
            LOG_ERROR("[JIT-DEBUG]   checking block: start_pc=0x%08X flags=0x%04X\n",
                      block->start_pc, block->flags);
        }
        if (block->start_pc == pc && (block->flags & JIT_BLOCK_VALID)) {
            if (debug_lookup) {
                LOG_ERROR("[JIT-DEBUG] CACHE HIT: block=%p code_ptr=%p\n",
                          (void*)block, (void*)block->code_ptr);
            }
            return block;
        }
        block = block->hash_next;
    }
    
    if (debug_lookup) {
        LOG_ERROR("[JIT-DEBUG] CACHE MISS: no block found\n");
    }
    return NULL;
}


/**
 * Insert a block into the cache
 * 
 * @param jit JIT context
 * @param block Block to insert
 */
void jit_cache_insert(jit_context_t *jit, jit_block_t *block)
{
    uint32_t hash;
    
    if (jit == NULL || block == NULL) {
        return;
    }
    
    hash = jit_cache_hash_pc(block->start_pc);
    
    /* Insert at head of hash chain */
    block->hash_next = jit->hash_table[hash];
    jit->hash_table[hash] = block;
}


/**
 * Remove a block from the cache
 * 
 * @param jit JIT context
 * @param block Block to remove
 */
void jit_cache_remove(jit_context_t *jit, jit_block_t *block)
{
    uint32_t hash;
    jit_block_t *prev, *curr;
    
    if (jit == NULL || block == NULL) {
        return;
    }
    
    hash = jit_cache_hash_pc(block->start_pc);
    prev = NULL;
    curr = jit->hash_table[hash];
    
    while (curr != NULL) {
        if (curr == block) {
            if (prev == NULL) {
                jit->hash_table[hash] = curr->hash_next;
            } else {
                prev->hash_next = curr->hash_next;
            }
            return;
        }
        prev = curr;
        curr = curr->hash_next;
    }
}


/**
 * Get cache bytes used
 * 
 * @param jit JIT context
 * @return Number of bytes used
 */
size_t jit_cache_used(jit_context_t *jit)
{
    if (jit == NULL) {
        return 0;
    }
    return jit->stats.cache_bytes_used;
}


/**
 * Get cache bytes free
 * 
 * @param jit JIT context
 * @return Number of bytes free
 */
size_t jit_cache_free_space(jit_context_t *jit)
{
    if (jit == NULL) {
        return 0;
    }
    return jit->stats.cache_bytes_free;
}
