/*
 * JIT Block Implementation
 * 
 * Manages compiled basic blocks.
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
#include <string.h>
#include <stdlib.h>

/* Hash function for PC lookup */
static inline uint32_t jit_hash_pc(uint32_t pc)
{
    /* Simple hash - multiply by prime and fold */
    uint32_t hash = pc * 2654435761u;
    return (hash >> 16) & (JIT_HASH_SIZE - 1);
}

static inline int jit_family_codegen_supported(uint8_t family)
{
    /* Implemented: MOVEQ, MOVE, ADD, SUB, CMP, ADDQ, SUBQ, AND/OR/EOR, BRA/BCC/BSR/RTS/JSR/JMP, MOVEC, LEA, CLR, TST, bitops, NOP */
    return (family == JIT_FAMILY_NOP || 
            family == JIT_FAMILY_MOVEQ || 
            family == JIT_FAMILY_MOVE ||
            family == JIT_FAMILY_ADD ||
            family == JIT_FAMILY_ADDQ ||
            family == JIT_FAMILY_SUB ||
            family == JIT_FAMILY_SUBQ ||
            family == JIT_FAMILY_CMP ||
            family == JIT_FAMILY_CMPI ||
            family == JIT_FAMILY_CMPM ||
            family == JIT_FAMILY_AND ||
            family == JIT_FAMILY_OR ||
            family == JIT_FAMILY_EOR ||
            family == JIT_FAMILY_ANDI ||
            family == JIT_FAMILY_ORI ||
            family == JIT_FAMILY_EORI ||
            family == JIT_FAMILY_BRA ||
            family == JIT_FAMILY_BCC ||
            family == JIT_FAMILY_BSR ||
            family == JIT_FAMILY_RTS ||
            family == JIT_FAMILY_JSR ||
            family == JIT_FAMILY_JMP ||
            family == JIT_FAMILY_MOVEC ||
            family == JIT_FAMILY_LEA ||
            family == JIT_FAMILY_CLR ||
            family == JIT_FAMILY_TST ||
            family == JIT_FAMILY_BTST ||
            family == JIT_FAMILY_BSET ||
            family == JIT_FAMILY_BCLR ||
            family == JIT_FAMILY_BCHG);
}


/**
 * Allocate a new block structure
 * 
 * @param jit JIT context
 * @param pc Starting PC for the block
 * @return Pointer to allocated block, or NULL on failure
 */
jit_block_t *jit_block_alloc(jit_context_t *jit, uint32_t pc)
{
    jit_block_t *block;
    
    /* Allocate block structure */
    block = (jit_block_t *)calloc(1, sizeof(jit_block_t));
    if (block == NULL) {
        return NULL;
    }
    
    block->start_pc = pc;
    block->flags = JIT_BLOCK_VALID;
    
    return block;
}


/**
 * Free a block structure
 * 
 * @param jit JIT context
 * @param block Block to free
 */
void jit_block_free(jit_context_t *jit, jit_block_t *block)
{
    if (block == NULL) {
        return;
    }
    
    /* Return code cache space if allocated */
    if (block->code_ptr != NULL && block->code_size > 0) {
        jit_cache_free(jit, block->code_ptr, block->code_size);
    }
    
    free(block);
}


/**
 * Translate a basic block of 68k instructions
 * 
 * Walks through instructions starting at block->start_pc and
 * translates them to an intermediate representation until hitting
 * a block boundary (branch, trap, RTS, etc).
 * 
 * @param jit JIT context
 * @param block Block to translate
 * @return 0 on success, -1 on failure
 */
int jit_block_translate(jit_context_t *jit, jit_block_t *block)
{
    uint32_t pc = block->start_pc;
    int instr_count = 0;
    bool end_block = false;
    uint8_t fetch_fc;
    
    if (!jit || !block || !jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_block_translate invalid context (jit=%p cpu=%p block=%p)\n",
                  (void *)jit, (void *)(jit ? jit->cpu : NULL), (void *)block);
        return -1;
    }

    /* DEBUG: Instrument block at 0x00F80BD4 */
    bool debug_block = (block->start_pc == 0x00F80BD4);
    if (debug_block) {
        LOG_ERROR("[JIT-DEBUG] ===== BLOCK TRANSLATE START PC=0x%08X =====\n", block->start_pc);
        fflush(stderr);
    }

    fetch_fc = jit_get_fc(jit->cpu->s_flag ? 1 : 0, 1);
    
    while (!end_block && instr_count < JIT_MAX_BLOCK_INSTRUCTIONS) {
        const jit_opinfo_t *opinfo;
        uint16_t opcode;
        uint16_t ext_words[4] = {0};
        int ext_count = 0;
        
        /* Fetch opcode from memory */
        opcode = jit_fetch_word(pc, fetch_fc);
        
        if (debug_block) {
            LOG_ERROR("[JIT-DEBUG] Instr[%d] PC=0x%08X opcode=0x%04X\n", instr_count, pc, opcode);
        }
        
        /* Get opcode metadata */
        opinfo = jit_get_opinfo(opcode);
        
        if (debug_block) {
            LOG_ERROR("[JIT-DEBUG]   family=%u ext_words=%u\n", opinfo->family, opinfo->ext_words);
        }

        if (!jit_family_codegen_supported(opinfo->family)) {
            block->flags |= JIT_BLOCK_INTERPRET_ONLY;
            end_block = true;
        }
        
        /* Check for illegal instruction */
        if (opinfo->family == JIT_FAMILY_ILLEGAL ||
            opinfo->family == JIT_FAMILY_LINE_A ||
            opinfo->family == JIT_FAMILY_LINE_F) {
            /* Will trap - end block here */
            end_block = true;
            block->flags |= JIT_BLOCK_ENDS_TRAP;
        }
        
        /* Fetch extension words */
        ext_count = opinfo->ext_words;
        
        /* Special handling for JMP/JSR - EA mode determines ext word count */
        if (opinfo->family == JIT_FAMILY_JMP || opinfo->family == JIT_FAMILY_JSR) {
            uint8_t ea_mode = (opcode >> 3) & 0x7;
            uint8_t ea_reg = opcode & 0x7;
            /* EA mode 7, reg 0 = (xxx).W = 1 ext word */
            /* EA mode 7, reg 1 = (xxx).L = 2 ext words */
            if (ea_mode == 7 && ea_reg == 0) {
                ext_count = 1;
            } else if (ea_mode == 7 && ea_reg == 1) {
                ext_count = 2;
            } else if (ea_mode == 5) {  /* (d16,An) */
                ext_count = 1;
            }
        }
        
        if (debug_block && ext_count > 0) {
            LOG_ERROR("[JIT-DEBUG]   fetching %d ext words\n", ext_count);
        }
        
        for (int i = 0; i < ext_count && i < 4; i++) {
            ext_words[i] = jit_fetch_word(pc + 2 + (uint32_t)(i * 2), fetch_fc);
            if (debug_block) {
                LOG_ERROR("[JIT-DEBUG]     ext[%d]=0x%04X\n", i, ext_words[i]);
            }
        }
        
        /* Store instruction info */
        block->instructions[instr_count].opcode = opcode;
        block->instructions[instr_count].ext_count = ext_count;
        memcpy(block->instructions[instr_count].ext_words, ext_words, 
               sizeof(ext_words));
        
        /* Check for block-ending instructions */
        if (opinfo->flags & JIT_OPF_BLOCK_END) {
            end_block = true;
            
            if (opinfo->family == JIT_FAMILY_BRA ||
                opinfo->family == JIT_FAMILY_BCC ||
                opinfo->family == JIT_FAMILY_BSR) {
                block->flags |= JIT_BLOCK_ENDS_BRANCH;
            } else if (opinfo->family == JIT_FAMILY_RTS) {
                block->flags |= JIT_BLOCK_ENDS_RTS;
            } else if (opinfo->family == JIT_FAMILY_RTE) {
                block->flags |= JIT_BLOCK_ENDS_RTE;
            } else if (opinfo->family == JIT_FAMILY_JMP ||
                       opinfo->family == JIT_FAMILY_JSR) {
                block->flags |= JIT_BLOCK_ENDS_JMP;
            } else if (opinfo->flags & JIT_OPF_MAY_TRAP) {
                block->flags |= JIT_BLOCK_ENDS_TRAP;
            }
        }
        
        /* Check for privileged instruction */
        if (opinfo->flags & JIT_OPF_PRIVILEGED) {
            /* Will trap if not in supervisor mode - end block */
            end_block = true;
            block->flags |= JIT_BLOCK_ENDS_TRAP;
        }
        
        /* Advance PC */
        pc += 2 + (ext_count * 2);
        
        if (debug_block) {
            LOG_ERROR("[JIT-DEBUG]   PC advance to 0x%08X\n", pc);
        }
        
        instr_count++;
    }
    
    block->instruction_count = instr_count;
    block->end_pc = pc;
    block->ends_block = end_block ? 1 : 0;
    
    if (debug_block) {
        LOG_ERROR("[JIT-DEBUG] ===== BLOCK TRANSLATE COMPLETE: %d instructions, end_pc=0x%08X =====\n",
                  instr_count, pc);
        fflush(stderr);
    }
    
    return 0;
}


/**
 * Emit AArch64 code for a translated block
 * 
 * Takes the intermediate representation in the block and
 * generates AArch64 machine code in the code cache.
 * 
 * @param jit JIT context
 * @param block Block to emit
 * @return 0 on success, -1 on failure
 */
int jit_block_emit(jit_context_t *jit, jit_block_t *block)
{
    uint8_t tmp_code[JIT_MAX_BLOCK_SIZE];
    uint8_t *code_start;
    size_t final_size;
    
    /* DEBUG: Instrument block at 0x00F80BD4 */
    bool debug_block = (block->start_pc == 0x00F80BD4);
    if (debug_block) {
        LOG_ERROR("[JIT-DEBUG] ===== BLOCK EMIT ENTERED PC=0x%08X =====\n", block->start_pc);
        LOG_ERROR("[JIT-DEBUG] block=%p instruction_count=%u flags=0x%04X\n",
                  (void*)block, block->instruction_count, block->flags);
        fflush(stderr);
    }
    
    if (block->flags & JIT_BLOCK_INTERPRET_ONLY) {
        if (debug_block) {
            LOG_ERROR("[JIT-DEBUG] Block marked INTERPRET_ONLY, skipping emit\n");
        }
        return 0;
    }

    /* Initialize emitter context */
    jit_emit_context_t emit;
    jit_emit_init(&emit, tmp_code, JIT_MAX_BLOCK_SIZE);
    
    if (debug_block) {
        LOG_ERROR("[JIT-DEBUG] Emit prologue...\n");
    }
    
    /* Emit prologue - sync CPU state */
    jit_emit_prologue(&emit, block);
    
    /* Emit each instruction */
    if (debug_block) {
        LOG_ERROR("[JIT-DEBUG] EMIT LOOP START: instruction_count=%d\n", block->instruction_count);
        fflush(stderr);
    }
    for (int i = 0; i < block->instruction_count; i++) {
        uint16_t opcode = block->instructions[i].opcode;
        const jit_opinfo_t *opinfo = jit_get_opinfo(opcode);
        
        if (debug_block) {
            LOG_ERROR("[JIT-DEBUG] ===== EMIT LOOP i=%d/%d opcode=0x%04X family=%u =====\n",
                      i, block->instruction_count, opcode, opinfo->family);
            LOG_ERROR("[JIT-DEBUG] About to enter switch statement...\n");
            fflush(stderr);
        }
        
        /* Dispatch to appropriate translator based on family */
        switch (opinfo->family) {
            case JIT_FAMILY_NOP:
                /* Nothing to emit for NOP - it's a no-op */
                break;
                
            case JIT_FAMILY_MOVEQ:
                /* MOVEQ: Move Quick - 8-bit immediate to Dn */
                /* Call translator to emit code for this instruction */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = 0;
                    if (jit_translate_moveq(&tctx) < 0) {
                        LOG_ERROR("[CPU] m68xkcpu: MOVEQ translation failed at PC=0x%08X\n", block->start_pc);
                        emit.error = true;
                        return -1;
                    }
                    /* Code already emitted to block->code_ptr, copy to our buffer */
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_MOVE:
                /* MOVE: General move instruction */
                /* Call translator to emit code for this instruction */
                {
                    jit_translate_context_t tctx;
                    uint8_t *local_code_ptr;
                    size_t local_code_size;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (debug_block) {
                        LOG_ERROR("[JIT-DEBUG] Calling jit_translate_move...\n");
                        LOG_ERROR("[JIT-DEBUG] block->code_ptr before translate=%p\n", (void*)block->code_ptr);
                        fflush(stderr);
                    }
                    if (jit_translate_move(&tctx) < 0) {
                        /* Translation failed - likely unsupported EA mode */
                        if (debug_block) {
                            LOG_ERROR("[JIT-DEBUG] MOVE translation FAILED\n");
                            fflush(stderr);
                        }
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "MOVE translation failed (unsupported EA mode)");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (debug_block) {
                        LOG_ERROR("[JIT-DEBUG] MOVE translation RETURNED SUCCESS\n");
                        LOG_ERROR("[JIT-DEBUG] block->code_ptr after translate=%p block->code_size=%zu\n",
                                  (void*)block->code_ptr, block->code_size);
                        fflush(stderr);
                    }
                    /* IMMEDIATELY save code_ptr and code_size before they get overwritten! */
                    local_code_ptr = block->code_ptr;
                    local_code_size = block->code_size;
                    block->code_ptr = NULL;
                    block->code_size = 0;
                    if (debug_block) {
                        LOG_ERROR("[JIT-DEBUG] Saved: local_code_ptr=%p local_code_size=%zu\n",
                                  (void*)local_code_ptr, local_code_size);
                        fflush(stderr);
                    }
                    if (!local_code_ptr) {
                        if (debug_block) {
                            LOG_ERROR("[JIT-DEBUG] ERROR: local_code_ptr is NULL!\n");
                            fflush(stderr);
                        }
                        break;
                    }
                    if (debug_block) {
                        LOG_ERROR("[JIT-DEBUG] memcpy %zu bytes to offset %zu...\n",
                                  local_code_size, emit.offset);
                        fflush(stderr);
                    }
                    memcpy(tmp_code + emit.offset, local_code_ptr, local_code_size);
                    emit.offset += local_code_size;
                    if (debug_block) {
                        LOG_ERROR("[JIT-DEBUG] memcpy DONE, offset=%zu\n", emit.offset);
                        fflush(stderr);
                    }
                }
                if (debug_block) {
                    LOG_ERROR("[JIT-DEBUG] MOVE case COMPLETE, about to break...\n");
                    fflush(stderr);
                }
                break;
            case JIT_FAMILY_MOVEP:
            case JIT_FAMILY_MOVEM:
                /* TODO: Implement MOVE translators */
                if (jit_no_fallback_enabled()) {
                    jit_hard_fail(block->start_pc, opcode, "MOVEP/MOVEM not implemented");
                }
                jit_emit_unimplemented(&emit, opcode, "MOVE");
                break;
                
            case JIT_FAMILY_ADD:
                /* ADD: Addition instruction */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_add(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "ADD translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_ADDQ:
                /* ADDQ: Add quick immediate */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_addq_subq(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "ADDQ/SUBQ translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_ADDI:
            case JIT_FAMILY_ADDX:
                /* TODO: Implement ADDI/ADDX translators */
                if (jit_no_fallback_enabled()) {
                    jit_hard_fail(block->start_pc, opcode, "ADDI/ADDX not implemented");
                }
                jit_emit_unimplemented(&emit, opcode, "ADDI/ADDX");
                break;
                
            case JIT_FAMILY_SUB:
                /* SUB: Subtraction instruction */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_sub(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "SUB translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_SUBQ:
                /* SUBQ: Subtract quick immediate */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_addq_subq(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "ADDQ/SUBQ translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_SUBI:
            case JIT_FAMILY_SUBX:
                /* TODO: Implement SUBI/SUBX translators */
                if (jit_no_fallback_enabled()) {
                    jit_hard_fail(block->start_pc, opcode, "SUBI/SUBX not implemented");
                }
                jit_emit_unimplemented(&emit, opcode, "SUBI/SUBX");
                break;
                
            case JIT_FAMILY_CMP:
            case JIT_FAMILY_CMPI:
            case JIT_FAMILY_CMPM:
                /* CMP/CMPI/CMPM: Compare instruction */
                /* Note: CMPA (bit 11=1) is classified as FAMILY_CMP but handled by fallback */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_cmp(&tctx) < 0) {
                        /* CMPA or unsupported - fall back to interpreter */
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "CMP/CMPI/CMPM translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_AND:
            case JIT_FAMILY_ANDI:
                /* AND/ANDI: Logical AND */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_logic(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "Logic (AND/OR/EOR) translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_OR:
            case JIT_FAMILY_ORI:
                /* OR/ORI: Logical OR */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_logic(&tctx) < 0) {
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_EOR:
            case JIT_FAMILY_EORI:
                /* EOR/EORI: Logical EOR */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_logic(&tctx) < 0) {
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_BRA:
            case JIT_FAMILY_BCC:
                /* BRA/BCC: Branch instruction */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_branch(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "Branch (BRA/BCC) translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_BSR:
                /* BSR - Branch to Subroutine */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_bsr(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "BSR translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_RTS:
                /* RTS - Return from Subroutine */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_rts(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "RTS translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_JSR:
                /* JSR - Jump to Subroutine */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_jsr(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "JSR translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_JMP:
                /* JMP - Jump */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_jmp(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "JMP translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_MOVEC:
                /* MOVEC - Move Control Register (68010+) */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_movec(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "MOVEC translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            case JIT_FAMILY_LEA:
            case JIT_FAMILY_CLR:
            case JIT_FAMILY_TST:
            case JIT_FAMILY_BTST:
            case JIT_FAMILY_BSET:
            case JIT_FAMILY_BCLR:
            case JIT_FAMILY_BCHG:
                /* LEA, CLR, TST, bit operations */
                {
                    jit_translate_context_t tctx;
                    memset(&tctx, 0, sizeof(tctx));
                    tctx.jit = jit;
                    tctx.block = block;
                    tctx.opcode = opcode;
                    tctx.ext_count = block->instructions[i].ext_count;
                    memcpy(tctx.ext_words, block->instructions[i].ext_words, sizeof(tctx.ext_words));
                    if (jit_translate_misc(&tctx) < 0) {
                        if (jit_no_fallback_enabled()) {
                            jit_hard_fail(block->start_pc, opcode, "MISC (LEA/CLR/TST/bitops) translation failed");
                        }
                        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                        return 0;
                    }
                    if (block->code_ptr) {
                        memcpy(tmp_code + emit.offset, block->code_ptr, block->code_size);
                        emit.offset += block->code_size;
                    }
                }
                break;
                
            default:
                /* Unimplemented family - hard fail or fall back to interpreter */
                if (jit_no_fallback_enabled()) {
                    jit_hard_fail(block->start_pc, opcode, "Unknown/unimplemented instruction family");
                }
                jit_emit_unimplemented(&emit, opcode, "UNKNOWN");
                break;
        }
        
        if (debug_block) {
            LOG_ERROR("[JIT-DEBUG] Switch statement COMPLETE for i=%d\n", i);
            fflush(stderr);
        }
        
        /* Check for emission errors */
        if (emit.error) {
            if (debug_block) {
                LOG_ERROR("[JIT-DEBUG] EMIT ERROR at instruction %d!\n", i);
                fflush(stderr);
            }
            return -1;
        }
        
        if (debug_block) {
            LOG_ERROR("[JIT-DEBUG] ===== EMIT LOOP i=%d COMPLETE, emit.offset=%zu =====\n",
                      i, emit.offset);
            fflush(stderr);
        }
    }
    
    /* Emit epilogue - return to dispatcher */
    if (debug_block) {
        LOG_ERROR("[JIT-DEBUG] Emit epilogue, offset=%zu...\n", emit.offset);
    }
    jit_emit_epilogue(&emit, block);
    
    final_size = (emit.offset + 15u) & ~15u;
    
    if (debug_block) {
        LOG_ERROR("[JIT-DEBUG] Allocating cache: final_size=%zu...\n", final_size);
    }
    code_start = jit_cache_alloc(jit, final_size);
    if (code_start == NULL) {
        LOG_ERROR("[CPU] m68xkcpu: code cache exhausted while emitting block at 0x%08X (need=%zu)\n",
                  block->start_pc, final_size);
        if (debug_block) {
            LOG_ERROR("[JIT-DEBUG] CACHE ALLOCATION FAILED!\n");
        }
        return -1;
    }
    
    if (debug_block) {
        LOG_ERROR("[JIT-DEBUG] Cache allocated at %p, copying %zu bytes...\n", (void*)code_start, emit.offset);
    }
    memcpy(code_start, tmp_code, emit.offset);
    jit_cache_flush(jit, code_start, emit.offset);
    
    /* Update block with code location */
    block->code_ptr = code_start;
    block->code_size = final_size;
    
    if (debug_block) {
        LOG_ERROR("[JIT-DEBUG] ===== BLOCK EMIT COMPLETE: code_ptr=%p code_size=%zu =====\n",
                  (void*)code_start, final_size);
    }
    
    return 0;
}


/**
 * Execute a compiled block
 * 
 * @param block Block to execute
 * @param max_cycles Maximum cycles to execute
 * @return Number of cycles executed, or -1 if fallback needed
 */
int jit_block_execute(jit_block_t *block, int max_cycles)
{
    /* DEBUG: Instrument block at 0x00F80BD4 */
    bool debug_block = (block && block->start_pc == 0x00F80BD4);
    if (debug_block) {
        LOG_ERROR("[JIT-DEBUG] ===== BLOCK EXECUTE PC=0x%08X code_ptr=%p =====\n",
                  block->start_pc, (void*)block->code_ptr);
        fflush(stderr);
    }
    
    if (block == NULL || block->code_ptr == NULL) {
        if (debug_block) {
            LOG_ERROR("[JIT-DEBUG] EXECUTE ABORT: block=%p code_ptr=%p\n",
                      (void*)block, (void*)(block ? block->code_ptr : NULL));
            fflush(stderr);
        }
        return -1;
    }
    
    /* Cast to function pointer and execute */
    typedef int (*jit_func_t)(int cycles);
    jit_func_t func = (jit_func_t)block->code_ptr;
    
    if (debug_block) {
        LOG_ERROR("[JIT-DEBUG] Calling JIT function at %p...\n", (void*)func);
        fflush(stderr);
    }
    
    /* Execute the compiled code */
    int cycles = func(max_cycles);
    
    if (debug_block) {
        extern jit_context_t g_jit;
        LOG_ERROR("[JIT-DEBUG] JIT function returned cycles=%d\n", cycles);
        LOG_ERROR("[JIT-DEBUG] g_jit.current_pc after execution: 0x%08X\n", g_jit.current_pc);
        fflush(stderr);
    }
    
    /* Sync PC from CPU state after execution */
    extern struct m68ki_cpu_core m68ki_cpu;
    extern jit_context_t g_jit;
    g_jit.current_pc = (uint32_t)m68ki_cpu.pc;
    
    if (debug_block) {
        extern jit_context_t g_jit;
        LOG_ERROR("[JIT-DEBUG] Synced PC from m68ki_cpu.pc: 0x%08X\n", g_jit.current_pc);
        fflush(stderr);
    }
    
    return cycles;
}


/**
 * Invalidate a block
 * 
 * Marks the block as invalid and frees its code cache space.
 * 
 * @param jit JIT context
 * @param block Block to invalidate
 */
void jit_block_invalidate(jit_context_t *jit, jit_block_t *block)
{
    if (block == NULL) {
        return;
    }
    
    block->flags &= ~JIT_BLOCK_VALID;
    
    /* Free code cache */
    if (block->code_ptr != NULL) {
        jit_cache_free(jit, block->code_ptr, block->code_size);
        block->code_ptr = NULL;
        block->code_size = 0;
    }
}


/**
 * Dump block information for debugging
 * 
 * @param block Block to dump
 */
void jit_block_dump(jit_block_t *block)
{
    if (block == NULL) {
        printf("Block: NULL\n");
        return;
    }
    
    printf("Block @ 0x%08X:\n", block->start_pc);
    printf("  End PC:     0x%08X\n", block->end_pc);
    printf("  Code ptr:   %p\n", (void *)block->code_ptr);
    printf("  Code size:  %zu bytes\n", block->code_size);
    printf("  Instrs:     %u\n", block->instruction_count);
    printf("  Flags:      0x%04X\n", block->flags);
    printf("  Ends block: %s\n", block->ends_block ? "yes" : "no");
    
    printf("  Instructions:\n");
    for (int i = 0; i < block->instruction_count; i++) {
        printf("    [%02d] 0x%04X", i, block->instructions[i].opcode);
        if (block->instructions[i].ext_count > 0) {
            printf(" ext=%u", block->instructions[i].ext_count);
        }
        printf("\n");
    }
}
