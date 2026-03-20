/*
 * JIT Block Implementation - Unified Model
 * 
 * Conventions:
 * - X19 holds pointer to m68ki_cpu state structure (callee-saved)
 * - Translators append to emit context (no buffer allocation)
 * - Single block builder manages instruction_count and code_ptr
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
#include <stddef.h>

/* Hash function for PC lookup */
static inline uint32_t jit_hash_pc(uint32_t pc)
{
    uint32_t hash = pc * 2654435761u;
    return (hash >> 16) & (JIT_HASH_SIZE - 1);
}

/* JIT Execution Logging */
static int g_jit_log_enabled = -1;
static uint32_t g_jit_log_pc_start = 0;
static uint32_t g_jit_log_pc_end = 0;

static void jit_log_init(void)
{
    if (g_jit_log_enabled < 0) {
        const char* e = getenv("PISTORM_M68XK_JIT_LOG");
        g_jit_log_enabled = (e && atoi(e) != 0) ? 1 : 0;
        if (g_jit_log_enabled) {
            LOG_INFO("[JIT] Logging enabled\n");
        }
    }
}

static int jit_log_should_log_pc(uint32_t pc)
{
    jit_log_init();
    if (!g_jit_log_enabled) return 0;
    
    /* Log specific PC ranges if set, otherwise log everything */
    if (g_jit_log_pc_start || g_jit_log_pc_end) {
        return (pc >= g_jit_log_pc_start && pc <= g_jit_log_pc_end);
    }
    return 1;
}

static void jit_log_block_compile(jit_block_t *block)
{
    if (!jit_log_should_log_pc(block->start_pc)) return;
    
    LOG_INFO("[JIT-COMPILE] Block PC=0x%08X end=0x%08X instrs=%u\n",
             block->start_pc, block->end_pc, block->instruction_count);
    
    for (int i = 0; i < block->instruction_count; i++) {
        uint16_t op = block->instructions[i].opcode;
        const jit_opinfo_t *opinfo = jit_get_opinfo(op);
        LOG_INFO("[JIT-COMPILE]   [%d] 0x%04X family=%u ext=%u flags=0x%02X\n",
                 i, op, opinfo->family, block->instructions[i].ext_count, opinfo->flags);
        
        /* Log family=0 (illegal) opcodes with more detail */
        if (opinfo->family == 0) {
            LOG_INFO("[JIT-ILLEGAL] Opcode 0x%04X at PC=0x%08X classified as ILLEGAL\n",
                     op, block->start_pc + (i * 2));
        }
    }
}

/* Dump generated AArch64 code for debugging */
static void jit_dump_aarch64_code(jit_block_t *block)
{
    if (block->code_ptr == NULL || block->code_size == 0) {
        return;
    }
    
    fprintf(stderr, "\n[AA64-DUMP] Block PC=0x%08X code=%p size=%zu\n", 
            block->start_pc, (void*)block->code_ptr, block->code_size);
    
    uint32_t *code = (uint32_t *)block->code_ptr;
    int words = block->code_size / 4;
    
    for (int i = 0; i < words && i < 64; i++) {
        fprintf(stderr, "  [%02d] 0x%08X\n", i, code[i]);
    }
    
    if (words > 64) {
        fprintf(stderr, "  ... (%d more words)\n", words - 64);
    }
}

static void jit_log_block_execute(jit_block_t *block, struct m68ki_cpu_core *cpu)
{
    if (!jit_log_should_log_pc(block->start_pc)) return;
    
    LOG_INFO("[JIT-EXEC] Block PC=0x%08X code=%p\n", block->start_pc, (void*)block->code_ptr);
    LOG_INFO("[JIT-EXEC]   SR=0x%04X A7=0x%08X PC=0x%08X\n",
             (cpu->s_flag << 13) | (cpu->m_flag << 11) | (cpu->int_mask << 8) |
             ((cpu->x_flag >> 4) << 4) | ((cpu->n_flag >> 4) << 3) |
             ((!cpu->not_z_flag) << 2) | ((cpu->v_flag >> 6) << 1) | (cpu->c_flag >> 8),
             cpu->dar[15], cpu->pc);
    
    /* Log instructions in block */
    for (int i = 0; i < block->instruction_count; i++) {
        uint16_t op = block->instructions[i].opcode;
        const jit_opinfo_t *opinfo = jit_get_opinfo(op);
        LOG_INFO("[JIT-EXEC]   [%d] 0x%04X family=%u ext=%u\n",
                 i, op, opinfo->family, block->instructions[i].ext_count);
    }
}

static void jit_log_block_done(jit_block_t *block, struct m68ki_cpu_core *cpu, int cycles)
{
    if (!jit_log_should_log_pc(block->start_pc)) return;
    
    LOG_INFO("[JIT-DONE] Block PC=0x%08X cycles=%d new_PC=0x%08X\n",
             block->start_pc, cycles, cpu->pc);
}

static inline int jit_family_codegen_supported(uint8_t family)
{
    /* Let translator dispatch decide support; only hard-trap families are denied here. */
    return (family != JIT_FAMILY_ILLEGAL &&
            family != JIT_FAMILY_LINE_A &&
            family != JIT_FAMILY_LINE_F);
}

jit_block_t *jit_block_alloc(jit_context_t *jit, uint32_t pc)
{
    jit_block_t *block = (jit_block_t *)calloc(1, sizeof(jit_block_t));
    if (block == NULL) {
        return NULL;
    }
    
    block->start_pc = pc;
    block->flags = JIT_BLOCK_VALID;
    
    return block;
}

void jit_block_free(jit_context_t *jit, jit_block_t *block)
{
    if (block == NULL) {
        return;
    }
    
    /* Decrement interpret block count if applicable */
    if (block->flags & JIT_BLOCK_INTERPRET_ONLY) {
        if (g_jit.stats.interpret_blocks_count > 0) {
            g_jit.stats.interpret_blocks_count--;
        }
    }
    
    if (block->code_ptr != NULL && block->code_size > 0) {
        jit_cache_free(jit, block->code_ptr, block->code_size);
    }
    
    free(block);
}

int jit_block_translate(jit_context_t *jit, jit_block_t *block)
{
    uint32_t pc = block->start_pc;
    int instr_count = 0;
    bool end_block = false;
    uint8_t fetch_fc;
    
    if (!jit || !block || !jit->cpu) {
        LOG_ERROR("[CPU] m68xkcpu: jit_block_translate invalid context\n");
        return -1;
    }

    fetch_fc = jit_get_fc(jit->cpu->s_flag ? 1 : 0, 1);
    
    while (!end_block && instr_count < JIT_MAX_BLOCK_INSTRUCTIONS) {
        const jit_opinfo_t *opinfo;
        uint16_t opcode;
        uint16_t ext_words[4] = {0};
        int ext_count = 0;
        
        opcode = jit_fetch_word(pc, fetch_fc);
        opinfo = jit_get_opinfo(opcode);

        if (!jit_family_codegen_supported(opinfo->family)) {
            block->flags |= JIT_BLOCK_INTERPRET_ONLY;
            end_block = true;
        }
        
        if (opinfo->family == JIT_FAMILY_ILLEGAL ||
            opinfo->family == JIT_FAMILY_LINE_A ||
            opinfo->family == JIT_FAMILY_LINE_F) {
            end_block = true;
            block->flags |= JIT_BLOCK_ENDS_TRAP;
        }
        
        ext_count = opinfo->ext_words;
        
        /* Mark BSR as interpret-only (not yet implemented) */
        /* BRA and BCC are implemented and will be compiled */
        if (opinfo->family == JIT_FAMILY_BSR) {
            block->flags |= JIT_BLOCK_INTERPRET_ONLY;
            end_block = true;
        }
        
        /* BRA/BCC end the block but will be compiled */
        if (opinfo->family == JIT_FAMILY_BRA ||
            opinfo->family == JIT_FAMILY_BCC) {
            end_block = true;
        }
        
        if (opinfo->family == JIT_FAMILY_JMP || opinfo->family == JIT_FAMILY_JSR) {
            uint8_t ea_mode = (opcode >> 3) & 0x7;
            if (ea_mode == 7 && (opcode & 0x7) == 0) {
                ext_count = 1;
            } else if (ea_mode == 7 && (opcode & 0x7) == 1) {
                ext_count = 2;
            } else if (ea_mode == 5) {
                ext_count = 1;
            }
        }
        
        for (int i = 0; i < ext_count && i < 4; i++) {
            ext_words[i] = jit_fetch_word(pc + 2 + (uint32_t)(i * 2), fetch_fc);
        }
        
        block->instructions[instr_count].opcode = opcode;
        block->instructions[instr_count].ext_count = ext_count;
        memcpy(block->instructions[instr_count].ext_words, ext_words, sizeof(ext_words));
        
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
        
        if (opinfo->flags & JIT_OPF_PRIVILEGED) {
            end_block = true;
            block->flags |= JIT_BLOCK_ENDS_TRAP;
        }
        
        pc += 2 + (ext_count * 2);
        instr_count++;
    }
    
    block->instruction_count = instr_count;
    block->end_pc = pc;
    block->ends_block = end_block ? 1 : 0;
    
    jit_log_block_compile(block);
    
    return 0;
}

int jit_block_emit(jit_context_t *jit, jit_block_t *block)
{
    uint8_t tmp_code[JIT_MAX_BLOCK_SIZE];
    uint8_t *code_start;
    size_t final_size;
    
    if (block->flags & JIT_BLOCK_INTERPRET_ONLY) {
        return 0;
    }

    /* Initialize emitter context - translators append to this */
    jit_emit_context_t emit;
    jit_emit_init(&emit, tmp_code, JIT_MAX_BLOCK_SIZE);
    
    /* Emit prologue - X19 should already contain &m68ki_cpu from caller */
    jit_emit_prologue(&emit);
    
    /* Emit each instruction by calling translator */
    for (int i = 0; i < block->instruction_count; i++) {
        uint16_t opcode = block->instructions[i].opcode;
        const jit_opinfo_t *opinfo = jit_get_opinfo(opcode);
        uint16_t *ext_words = block->instructions[i].ext_words;
        int ext_count = block->instructions[i].ext_count;
        
        jit_translator_fn translator = NULL;
        
        /* Select translator based on family */
        switch (opinfo->family) {
            case JIT_FAMILY_NOP:
                translator = jit_translate_nop;
                break;
            case JIT_FAMILY_MOVEQ:
                translator = jit_translate_moveq;
                break;
            case JIT_FAMILY_MOVE:
                translator = jit_translate_move;
                break;
            case JIT_FAMILY_ADD:
                translator = jit_translate_add;
                break;
            case JIT_FAMILY_ADDQ:
                translator = jit_translate_addq;
                break;
            case JIT_FAMILY_SUB:
                translator = jit_translate_sub;
                break;
            case JIT_FAMILY_SUBQ:
                translator = jit_translate_subq;
                break;
            case JIT_FAMILY_CMP:
            case JIT_FAMILY_CMPI:
            case JIT_FAMILY_CMPM:
                translator = jit_translate_cmp;
                break;
            case JIT_FAMILY_AND:
            case JIT_FAMILY_ANDI:
            case JIT_FAMILY_OR:
            case JIT_FAMILY_ORI:
            case JIT_FAMILY_EOR:
            case JIT_FAMILY_EORI:
                translator = jit_translate_logic;
                break;
            case JIT_FAMILY_BRA:
            case JIT_FAMILY_BCC:
                translator = jit_translate_branch;
                break;
            case JIT_FAMILY_BSR:
                translator = jit_translate_bsr;
                break;
            case JIT_FAMILY_RTS:
                translator = jit_translate_rts;
                break;
            case JIT_FAMILY_JSR:
                translator = jit_translate_jsr;
                break;
            case JIT_FAMILY_JMP:
                translator = jit_translate_jmp;
                break;
            case JIT_FAMILY_MOVEC:
                translator = jit_translate_movec;
                break;
            case JIT_FAMILY_EXTB:
            case JIT_FAMILY_EXT:
                translator = jit_translate_extb;
                break;
            case JIT_FAMILY_LEA:
                translator = jit_translate_lea;
                break;
            case JIT_FAMILY_CLR:
            case JIT_FAMILY_TST:
            case JIT_FAMILY_BTST:
            case JIT_FAMILY_BSET:
            case JIT_FAMILY_BCLR:
            case JIT_FAMILY_BCHG:
                translator = jit_translate_misc;
                break;
            default:
                /* Unimplemented family */
                LOG_INFO("[JIT-FALLBACK] Unsupported family=%u opcode=0x%04X PC=0x%08X\n",
                         opinfo->family, opcode, block->start_pc);
                block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                return 0;
        }
        
        if (translator) {
            jit_translate_context_t tctx = {
                .jit = jit,
                .block = block,
                .emit = &emit,
                .opcode = opcode,
                .ext_words = ext_words,
                .ext_count = ext_count,
                .instruction_index = (uint16_t)i,
            };
            if (translator(&tctx) < 0) {
                /* Translator failed - mark block for interpretation and return error
                 * so the block is NOT cached as executable code */
                block->flags |= JIT_BLOCK_INTERPRET_ONLY;
                return -1; /* Signal compilation failure - caller will use Musashi */
            }
        }
        
        if (emit.error) {
            LOG_ERROR("[JIT-EMIT] Buffer overflow at instruction %d PC=0x%08X\n", i, block->start_pc);
            return -1;
        }
    }
    
    /* Emit epilogue */
    jit_emit_epilogue(&emit);
    
    final_size = (emit.offset + 15u) & ~15u;
    
    code_start = jit_cache_alloc(jit, final_size);
    if (code_start == NULL) {
        LOG_ERROR("[CPU] m68xkcpu: code cache exhausted\n");
        return -1;
    }
    
    memcpy(code_start, tmp_code, emit.offset);
    jit_cache_flush(jit, code_start, emit.offset);
    
    block->code_ptr = code_start;
    block->code_size = final_size;
    
    /* DEBUG: Dump generated code */
    jit_dump_aarch64_code(block);
    
    return 0;
}

int jit_block_execute(jit_block_t *block, int max_cycles)
{
    if (block == NULL || block->code_ptr == NULL) {
        return -1;
    }
    
    extern struct m68ki_cpu_core m68ki_cpu;
    
    jit_log_block_execute(block, &m68ki_cpu);
    
    typedef int (*jit_func_t)(int cycles);
    jit_func_t func = (jit_func_t)block->code_ptr;

#if defined(__aarch64__)
    /* JIT ABI contract: compiled blocks expect CPU state pointer in X19. */
    asm volatile("mov x19, %0" : : "r"(&m68ki_cpu) : "x19");
#endif
    
    int cycles = func(max_cycles);
    
    /* Sync PC from CPU state after execution */
    extern jit_context_t g_jit;
    g_jit.current_pc = (uint32_t)m68ki_cpu.pc;
    
    jit_log_block_done(block, &m68ki_cpu, cycles);
    
    return cycles;
}

void jit_block_invalidate(jit_context_t *jit, jit_block_t *block)
{
    if (block == NULL) {
        return;
    }
    
    block->flags &= ~JIT_BLOCK_VALID;
    
    if (block->code_ptr != NULL) {
        jit_cache_free(jit, block->code_ptr, block->code_size);
        block->code_ptr = NULL;
        block->code_size = 0;
    }
}

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

/* ============================================================================
 * Translator implementations (single block-based path)
 * ============================================================================ */

int jit_translate_nop(jit_translate_context_t *tctx)
{
    /* NOP - no code to emit */
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    (void)ctx; (void)opcode; (void)ext_words; (void)ext_count;
    return 0;
}

int jit_translate_moveq(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* MOVEQ: 0111 dnnn dddddddd - move 8-bit immediate to Dn */
    int dn = (opcode >> 9) & 0x7;
    int8_t imm = (int8_t)(opcode & 0xFF);
    
    /* Load immediate into temp register */
    jit_emit_mov64(ctx, AARCH64_R0, (uint64_t)(int32_t)imm);
    
    /* Store to Dn */
    jit_emit_store_dn(ctx, AARCH64_R0, dn);
    
    /* Increment PC by 2 (MOVEQ has no extension words) */
    jit_emit_inc_pc(ctx, 2);
    
    (void)ext_words; (void)ext_count;
    return 0;
}

int jit_translate_move(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* MOVE: 0001 size(2) dst_mode(3) dst_reg(3) src_mode(3) src_reg(3)
     * Bits 15-12 = 0001, Bits 11-9 = size, Bits 8-6 = dst EA, Bits 5-3 = dst reg,
     * Bits 2-0 = src mode, but src mode is in bits 5-3 actually
     * Correct: bits 14-12 = size, 11-9 = dst mode, 8-6 = dst reg, 5-3 = src mode, 2-0 = src reg
     */
    uint8_t size = (opcode >> 12) & 0x3;  /* 0=byte, 1=word, 2=long */
    uint8_t dst_mode = (opcode >> 6) & 0x7;
    uint8_t dst_reg = (opcode >> 9) & 0x7;
    uint8_t src_mode = (opcode >> 3) & 0x7;
    uint8_t src_reg = opcode & 0x7;
    
    /* For now, implement common Dn/An modes */
    if (dst_mode == 0) {
        /* MOVE to Dn */
        if (src_mode == 0) {
            /* MOVE <Dn>, <Dn> */
            jit_emit_load_dn(ctx, AARCH64_R0, src_reg);
            if (size == 0) {
                /* Byte: zero-extend using LSL/ASR */
                jit_emit_dword(ctx, AARCH64_LSL(AARCH64_R0, AARCH64_R0, 24));
                jit_emit_dword(ctx, AARCH64_ASR(AARCH64_R0, AARCH64_R0, 24));
            } else if (size == 1) {
                /* Word: zero-extend using LSL/ASR */
                jit_emit_dword(ctx, AARCH64_LSL(AARCH64_R0, AARCH64_R0, 16));
                jit_emit_dword(ctx, AARCH64_ASR(AARCH64_R0, AARCH64_R0, 16));
            }
            /* Long: already 32-bit */
            jit_emit_store_dn(ctx, AARCH64_R0, dst_reg);
        } else if (src_mode == 2) {
            /* MOVE (An), Dn */
            jit_emit_load_an(ctx, AARCH64_R0, src_reg);
            if (size == 0) {
                jit_emit_dword(ctx, AARCH64_LDRB(AARCH64_R1, AARCH64_R0, 0));
                jit_emit_store_dn(ctx, AARCH64_R1, dst_reg);
            } else if (size == 1) {
                jit_emit_dword(ctx, AARCH64_LDRH(AARCH64_R1, AARCH64_R0, 0));
                jit_emit_store_dn(ctx, AARCH64_R1, dst_reg);
            } else {
                jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R1, AARCH64_R0, 0));
                jit_emit_store_dn(ctx, AARCH64_R1, dst_reg);
            }
        } else if (src_mode == 3) {
            /* MOVE (An)+, Dn */
            jit_emit_load_an(ctx, AARCH64_R0, src_reg);
            int incr = (size == 0) ? 1 : (size == 1) ? 2 : 4;
            if (size == 0) {
                jit_emit_dword(ctx, AARCH64_LDRB(AARCH64_R1, AARCH64_R0, 0));
                jit_emit_store_dn(ctx, AARCH64_R1, dst_reg);
            } else if (size == 1) {
                jit_emit_dword(ctx, AARCH64_LDRH(AARCH64_R1, AARCH64_R0, 0));
                jit_emit_store_dn(ctx, AARCH64_R1, dst_reg);
            } else {
                jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R1, AARCH64_R0, 0));
                jit_emit_store_dn(ctx, AARCH64_R1, dst_reg);
            }
            jit_emit_mov64(ctx, AARCH64_R1, incr);
            jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
            jit_emit_store_an(ctx, AARCH64_R0, src_reg);
        } else if (src_mode == 5) {
            /* MOVE d16(An), Dn */
            int16_t disp = (int16_t)ext_words[0];
            jit_emit_load_an(ctx, AARCH64_R0, src_reg);
            jit_emit_mov64(ctx, AARCH64_R1, (uint64_t)(int32_t)disp);
            jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
            if (size == 0) {
                jit_emit_dword(ctx, AARCH64_LDRB(AARCH64_R1, AARCH64_R0, 0));
                jit_emit_store_dn(ctx, AARCH64_R1, dst_reg);
            } else if (size == 1) {
                jit_emit_dword(ctx, AARCH64_LDRH(AARCH64_R1, AARCH64_R0, 0));
                jit_emit_store_dn(ctx, AARCH64_R1, dst_reg);
            } else {
                jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R1, AARCH64_R0, 0));
                jit_emit_store_dn(ctx, AARCH64_R1, dst_reg);
            }
        } else {
            return -1; /* Fallback for other modes */
        }
    } else if (dst_mode == 1) {
        /* MOVE to An - sign extend for word/byte */
        if (src_mode == 0) {
            /* MOVE Dn, An */
            jit_emit_load_dn(ctx, AARCH64_R0, src_reg);
            if (size == 0) {
                /* Byte: sign-extend using LSL/ASR */
                jit_emit_dword(ctx, AARCH64_LSL(AARCH64_R0, AARCH64_R0, 24));
                jit_emit_dword(ctx, AARCH64_ASR(AARCH64_R0, AARCH64_R0, 24));
            } else if (size == 1) {
                /* Word: sign-extend using LSL/ASR */
                jit_emit_dword(ctx, AARCH64_LSL(AARCH64_R0, AARCH64_R0, 16));
                jit_emit_dword(ctx, AARCH64_ASR(AARCH64_R0, AARCH64_R0, 16));
            }
            /* Long: already 32-bit, zero-extends to 64-bit in AArch64 */
            jit_emit_store_an(ctx, AARCH64_R0, dst_reg);
        } else {
            return -1; /* Fallback */
        }
    } else if (dst_mode == 2) {
        /* MOVE Dn, (An) */
        if (src_mode == 0) {
            /* MOVE Dn, (An) */
            jit_emit_load_dn(ctx, AARCH64_R0, src_reg);
            jit_emit_load_an(ctx, AARCH64_R1, dst_reg);
            if (size == 0) {
                jit_emit_dword(ctx, AARCH64_STRB(AARCH64_R0, AARCH64_R1, 0));
            } else if (size == 1) {
                jit_emit_dword(ctx, AARCH64_STRH(AARCH64_R0, AARCH64_R1, 0));
            } else {
                jit_emit_dword(ctx, AARCH64_STR(AARCH64_R0, AARCH64_R1, 0));
            }
        } else {
            return -1;
        }
    } else if (dst_mode == 3) {
        /* MOVE Dn, (An)+ */
        if (src_mode == 0) {
            int incr = (size == 0) ? 1 : (size == 1) ? 2 : 4;
            jit_emit_load_dn(ctx, AARCH64_R0, src_reg);
            jit_emit_load_an(ctx, AARCH64_R1, dst_reg);
            if (size == 0) {
                jit_emit_dword(ctx, AARCH64_STRB(AARCH64_R0, AARCH64_R1, 0));
            } else if (size == 1) {
                jit_emit_dword(ctx, AARCH64_STRH(AARCH64_R0, AARCH64_R1, 0));
            } else {
                jit_emit_dword(ctx, AARCH64_STR(AARCH64_R0, AARCH64_R1, 0));
            }
            jit_emit_mov64(ctx, AARCH64_R0, incr);
            jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R1, AARCH64_R1, AARCH64_R0));
            jit_emit_store_an(ctx, AARCH64_R1, dst_reg);
        } else {
            return -1;
        }
    } else if (dst_mode == 5) {
        /* MOVE Dn, d16(An) */
        if (src_mode == 0) {
            int16_t disp = (int16_t)ext_words[(src_mode == 5) ? 1 : 0];
            jit_emit_load_dn(ctx, AARCH64_R0, src_reg);
            jit_emit_load_an(ctx, AARCH64_R1, dst_reg);
            jit_emit_mov64(ctx, AARCH64_R2, (uint64_t)(int32_t)disp);
            jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R1, AARCH64_R1, AARCH64_R2));
            if (size == 0) {
                jit_emit_dword(ctx, AARCH64_STRB(AARCH64_R0, AARCH64_R1, 0));
            } else if (size == 1) {
                jit_emit_dword(ctx, AARCH64_STRH(AARCH64_R0, AARCH64_R1, 0));
            } else {
                jit_emit_dword(ctx, AARCH64_STR(AARCH64_R0, AARCH64_R1, 0));
            }
        } else {
            return -1;
        }
    } else {
        return -1; /* Fallback for other dst modes */
    }
    
    /* Increment PC */
    jit_emit_inc_pc(ctx, 2 + ext_count * 2);
    
    (void)ext_count;
    return 0;
}

int jit_translate_add(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* ADD: 1101 size(2) dst_mode(3) dst_reg(3) src_mode(3) src_reg(3) */
    uint8_t size = (opcode >> 12) & 0x3;
    uint8_t dst_mode = (opcode >> 6) & 0x7;
    uint8_t dst_reg = (opcode >> 9) & 0x7;
    uint8_t src_mode = (opcode >> 3) & 0x7;
    uint8_t src_reg = opcode & 0x7;
    
    /* ADD <ea>, Dn - most common form */
    if (dst_mode == 0) {
        /* ADD to Dn */
        jit_emit_load_dn(ctx, AARCH64_R0, dst_reg);
        
        if (src_mode == 0) {
            /* ADD Dn, Dn */
            jit_emit_load_dn(ctx, AARCH64_R1, src_reg);
        } else if (src_mode == 2) {
            /* ADD (An), Dn */
            jit_emit_load_an(ctx, AARCH64_R1, src_reg);
            if (size == 0) {
                jit_emit_dword(ctx, AARCH64_LDRB(AARCH64_R1, AARCH64_R1, 0));
            } else if (size == 1) {
                jit_emit_dword(ctx, AARCH64_LDRH(AARCH64_R1, AARCH64_R1, 0));
            } else {
                jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R1, AARCH64_R1, 0));
            }
        } else if (src_mode == 3) {
            /* ADD (An)+, Dn */
            jit_emit_load_an(ctx, AARCH64_R1, src_reg);
            int incr = (size == 0) ? 1 : (size == 1) ? 2 : 4;
            if (size == 0) {
                jit_emit_dword(ctx, AARCH64_LDRB(AARCH64_R2, AARCH64_R1, 0));
            } else if (size == 1) {
                jit_emit_dword(ctx, AARCH64_LDRH(AARCH64_R2, AARCH64_R1, 0));
            } else {
                jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R2, AARCH64_R1, 0));
            }
            jit_emit_mov64(ctx, AARCH64_R1, incr);
            jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R1, AARCH64_R1, AARCH64_R1));
            jit_emit_store_an(ctx, AARCH64_R1, src_reg);
            jit_emit_load_dn(ctx, AARCH64_R1, src_reg); /* Reload for add */
        } else if (src_mode == 5) {
            /* ADD d16(An), Dn */
            int16_t disp = (int16_t)ext_words[0];
            jit_emit_load_an(ctx, AARCH64_R1, src_reg);
            jit_emit_mov64(ctx, AARCH64_R2, (uint64_t)(int32_t)disp);
            jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R1, AARCH64_R1, AARCH64_R2));
            if (size == 0) {
                jit_emit_dword(ctx, AARCH64_LDRB(AARCH64_R1, AARCH64_R1, 0));
            } else if (size == 1) {
                jit_emit_dword(ctx, AARCH64_LDRH(AARCH64_R1, AARCH64_R1, 0));
            } else {
                jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R1, AARCH64_R1, 0));
            }
        } else {
            return -1;
        }
        
        /* Perform add with flags */
        if (size == 0) {
            jit_emit_dword(ctx, AARCH64_ADDS(AARCH64_R0, AARCH64_R0, AARCH64_R1));
            /* TODO: Extract N,Z,V,C flags properly for byte */
        } else if (size == 1) {
            jit_emit_dword(ctx, AARCH64_ADDS(AARCH64_R0, AARCH64_R0, AARCH64_R1));
            /* TODO: Extract flags for word */
        } else {
            jit_emit_dword(ctx, AARCH64_ADDS(AARCH64_R0, AARCH64_R0, AARCH64_R1));
        }
        jit_emit_store_dn(ctx, AARCH64_R0, dst_reg);
    } else {
        return -1; /* Fallback for other modes */
    }
    
    /* Increment PC */
    jit_emit_inc_pc(ctx, 2 + ext_count * 2);
    
    (void)ext_count;
    return 0;
}

int jit_translate_addq(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* ADDQ: #data, EA - data is 1-8 in bits 9-11 */
    int data = (opcode >> 9) & 0x7;
    if (data == 0) data = 8;
    
    int ea_mode = (opcode >> 3) & 0x7;
    int ea_reg = opcode & 0x7;
    
    if (ea_mode == 0) {
        /* ADDQ #data, Dn */
        jit_emit_load_dn(ctx, AARCH64_R0, ea_reg);
        jit_emit_mov64(ctx, AARCH64_R1, (uint64_t)data);
        jit_emit_dword(ctx, AARCH64_ADDS(AARCH64_R0, AARCH64_R0, AARCH64_R1));
        jit_emit_store_dn(ctx, AARCH64_R0, ea_reg);
    } else {
        return -1;
    }
    
    /* Increment PC */
    jit_emit_inc_pc(ctx, 2);
    
    (void)ext_words; (void)ext_count;
    return 0;
}

int jit_translate_sub(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* SUB: 1001 size(2) dst_mode(3) dst_reg(3) src_mode(3) src_reg(3) */
    uint8_t size = (opcode >> 12) & 0x3;
    uint8_t dst_mode = (opcode >> 6) & 0x7;
    uint8_t dst_reg = (opcode >> 9) & 0x7;
    uint8_t src_mode = (opcode >> 3) & 0x7;
    uint8_t src_reg = opcode & 0x7;
    
    /* SUB <ea>, Dn - most common form */
    if (dst_mode == 0) {
        /* SUB to Dn */
        jit_emit_load_dn(ctx, AARCH64_R0, dst_reg);
        
        if (src_mode == 0) {
            /* SUB Dn, Dn */
            jit_emit_load_dn(ctx, AARCH64_R1, src_reg);
        } else if (src_mode == 2) {
            /* SUB (An), Dn */
            jit_emit_load_an(ctx, AARCH64_R1, src_reg);
            if (size == 0) {
                jit_emit_dword(ctx, AARCH64_LDRB(AARCH64_R1, AARCH64_R1, 0));
            } else if (size == 1) {
                jit_emit_dword(ctx, AARCH64_LDRH(AARCH64_R1, AARCH64_R1, 0));
            } else {
                jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R1, AARCH64_R1, 0));
            }
        } else if (src_mode == 5) {
            /* SUB d16(An), Dn */
            int16_t disp = (int16_t)ext_words[0];
            jit_emit_load_an(ctx, AARCH64_R1, src_reg);
            jit_emit_mov64(ctx, AARCH64_R2, (uint64_t)(int32_t)disp);
            jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R1, AARCH64_R1, AARCH64_R2));
            if (size == 0) {
                jit_emit_dword(ctx, AARCH64_LDRB(AARCH64_R1, AARCH64_R1, 0));
            } else if (size == 1) {
                jit_emit_dword(ctx, AARCH64_LDRH(AARCH64_R1, AARCH64_R1, 0));
            } else {
                jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R1, AARCH64_R1, 0));
            }
        } else {
            return -1;
        }
        
        /* Perform sub with flags */
        jit_emit_dword(ctx, AARCH64_SUBS(AARCH64_R0, AARCH64_R0, AARCH64_R1));
        jit_emit_store_dn(ctx, AARCH64_R0, dst_reg);
    } else {
        return -1;
    }
    
    /* Increment PC */
    jit_emit_inc_pc(ctx, 2 + ext_count * 2);
    
    (void)ext_count;
    return 0;
}

int jit_translate_subq(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* SUBQ: #data, EA - similar to ADDQ */
    int data = (opcode >> 9) & 0x7;
    if (data == 0) data = 8;
    
    int ea_mode = (opcode >> 3) & 0x7;
    int ea_reg = opcode & 0x7;
    
    if (ea_mode == 0) {
        /* SUBQ #data, Dn */
        jit_emit_load_dn(ctx, AARCH64_R0, ea_reg);
        jit_emit_mov64(ctx, AARCH64_R1, (uint64_t)data);
        jit_emit_dword(ctx, AARCH64_SUBS(AARCH64_R0, AARCH64_R0, AARCH64_R1));
        jit_emit_store_dn(ctx, AARCH64_R0, ea_reg);
    } else {
        return -1;
    }
    
    /* Increment PC */
    jit_emit_inc_pc(ctx, 2);
    
    (void)ext_words; (void)ext_count;
    return 0;
}

int jit_translate_cmp(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* CMP: 1011 size(2) 00 dst_reg(3) src_mode(3) src_reg(3) */
    uint8_t size = (opcode >> 12) & 0x3;
    uint8_t dst_reg = (opcode >> 9) & 0x7;
    uint8_t src_mode = (opcode >> 3) & 0x7;
    uint8_t src_reg = opcode & 0x7;
    
    /* CMP <ea>, Dn */
    jit_emit_load_dn(ctx, AARCH64_R0, dst_reg);
    
    if (src_mode == 0) {
        /* CMP Dn, Dn */
        jit_emit_load_dn(ctx, AARCH64_R1, src_reg);
    } else if (src_mode == 2) {
        /* CMP (An), Dn */
        jit_emit_load_an(ctx, AARCH64_R1, src_reg);
        if (size == 0) {
            jit_emit_dword(ctx, AARCH64_LDRB(AARCH64_R1, AARCH64_R1, 0));
        } else if (size == 1) {
            jit_emit_dword(ctx, AARCH64_LDRH(AARCH64_R1, AARCH64_R1, 0));
        } else {
            jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R1, AARCH64_R1, 0));
        }
    } else if (src_mode == 3) {
        /* CMP (An)+, Dn */
        jit_emit_load_an(ctx, AARCH64_R1, src_reg);
        if (size == 0) {
            jit_emit_dword(ctx, AARCH64_LDRB(AARCH64_R1, AARCH64_R1, 0));
        } else if (size == 1) {
            jit_emit_dword(ctx, AARCH64_LDRH(AARCH64_R1, AARCH64_R1, 0));
        } else {
            jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R1, AARCH64_R1, 0));
        }
    } else if (src_mode == 5) {
        /* CMP d16(An), Dn */
        int16_t disp = (int16_t)ext_words[0];
        jit_emit_load_an(ctx, AARCH64_R1, src_reg);
        jit_emit_mov64(ctx, AARCH64_R2, (uint64_t)(int32_t)disp);
        jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R1, AARCH64_R1, AARCH64_R2));
        if (size == 0) {
            jit_emit_dword(ctx, AARCH64_LDRB(AARCH64_R1, AARCH64_R1, 0));
        } else if (size == 1) {
            jit_emit_dword(ctx, AARCH64_LDRH(AARCH64_R1, AARCH64_R1, 0));
        } else {
            jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R1, AARCH64_R1, 0));
        }
    } else {
        return -1;
    }
    
    /* CMP is Dn - <ea>, set flags but don't store result */
    jit_emit_dword(ctx, AARCH64_SUBS(AARCH64_R0, AARCH64_R0, AARCH64_R1));
    
    /* Increment PC */
    jit_emit_inc_pc(ctx, 2 + ext_count * 2);
    
    (void)ext_count;
    return 0;
}

int jit_translate_logic(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    int ext_count = tctx->ext_count;
    /* AND/OR/EOR: 1100/1000/1100 size(2) dst_mode(3) dst_reg(3) src_mode(3) src_reg(3) */
    uint8_t op = (opcode >> 13) & 0x7; /* 0=AND, 1=OR, 2=EOR */
    uint8_t dst_mode = (opcode >> 6) & 0x7;
    uint8_t dst_reg = (opcode >> 9) & 0x7;
    uint8_t src_mode = (opcode >> 3) & 0x7;
    uint8_t src_reg = opcode & 0x7;
    
    /* Only implement Dn, Dn for now */
    if (dst_mode != 0 || src_mode != 0) {
        return -1; /* Fallback for other modes */
    }
    
    /* Load source and dest */
    jit_emit_load_dn(ctx, AARCH64_R0, src_reg);
    jit_emit_load_dn(ctx, AARCH64_R1, dst_reg);
    
    /* Perform operation */
    if (op == 0) {
        /* AND */
        jit_emit_dword(ctx, AARCH64_AND(AARCH64_R0, AARCH64_R0, AARCH64_R1));
    } else if (op == 1) {
        /* OR */
        jit_emit_dword(ctx, AARCH64_ORR(AARCH64_R0, AARCH64_R0, AARCH64_R1));
    } else {
        /* EOR */
        jit_emit_dword(ctx, AARCH64_EOR(AARCH64_R0, AARCH64_R0, AARCH64_R1));
    }
    
    /* Store result */
    jit_emit_store_dn(ctx, AARCH64_R0, dst_reg);
    
    /* TODO: Update CCR flags (N, Z, V=0, C=0) */
    
    /* Increment PC */
    jit_emit_inc_pc(ctx, 2);
    
    (void)ext_count;
    return 0;
}

int jit_translate_branch(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* BCC/BRA: 0110 cond(4) displacement */
    uint8_t cond = (opcode >> 8) & 0x0F;
    int32_t disp;
    int instr_size;
    
    /* Get displacement - byte or word */
    if ((opcode & 0xFF) == 0) {
        /* Word displacement */
        disp = (int16_t)ext_words[0];
        instr_size = 4;
    } else {
        /* Byte displacement */
        disp = (int8_t)(opcode & 0xFF);
        instr_size = 2;
    }
    
    /* CCR flag offsets in m68ki_cpu */
    /* n_flag=348, not_z_flag=352, v_flag=356, c_flag=360 */
    
    if (cond == 0) {
        /* BRA - unconditional branch */
        /* Load current PC */
        jit_emit_load_pc(ctx, AARCH64_R0);
        /* Add instruction size */
        jit_emit_mov64(ctx, AARCH64_R1, instr_size);
        jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
        /* Add displacement */
        jit_emit_mov64(ctx, AARCH64_R1, disp);
        jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
        /* Store new PC */
        jit_emit_store_pc(ctx, AARCH64_R0);
    } else {
        /* Conditional branches - check CCR flags */
        /* Load flags */
        jit_emit_dword(ctx, AARCH64_LDR_W(AARCH64_R0, AARCH64_CPU_PTR, 348)); /* n_flag */
        jit_emit_dword(ctx, AARCH64_LDR_W(AARCH64_R1, AARCH64_CPU_PTR, 352)); /* not_z_flag */
        jit_emit_dword(ctx, AARCH64_LDR_W(AARCH64_R2, AARCH64_CPU_PTR, 356)); /* v_flag */
        jit_emit_dword(ctx, AARCH64_LDR_W(AARCH64_R3, AARCH64_CPU_PTR, 360)); /* c_flag */
        
        /* Convert to boolean (0 or 1) */
        jit_emit_dword(ctx, AARCH64_LSR(AARCH64_R0, AARCH64_R0, 7)); /* n = n_flag >> 7 */
        jit_emit_dword(ctx, AARCH64_AND_W(AARCH64_R1, AARCH64_R1, 1)); /* z = !not_z_flag & 1 */
        jit_emit_dword(ctx, AARCH64_LSR(AARCH64_R2, AARCH64_R2, 7)); /* v = v_flag >> 7 */
        jit_emit_dword(ctx, AARCH64_LSR(AARCH64_R3, AARCH64_R3, 8)); /* c = c_flag >> 8 */
        
        /* Evaluate condition */
        switch (cond) {
            case 0x1: /* BRA (already handled) */
                break;
            case 0x2: /* BHI: !C && !Z */
                jit_emit_dword(ctx, AARCH64_ORR_W(AARCH64_R4, AARCH64_R3, AARCH64_R1));
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R4, 8)); /* if (!C && !Z) branch */
                jit_emit_dword(ctx, AARCH64_B(4)); /* else skip */
                /* Branch taken: update PC */
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0x3: /* BLS: C || Z */
                jit_emit_dword(ctx, AARCH64_ORR_W(AARCH64_R4, AARCH64_R3, AARCH64_R1));
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R4, 8));
                jit_emit_dword(ctx, AARCH64_B(4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0x4: /* BCC/BHS: !C */
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R3, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0x5: /* BCS/BLO: C */
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R3, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0x6: /* BNE: !Z */
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R1, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0x7: /* BEQ: Z */
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R1, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0x8: /* BVC: !V */
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R2, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0x9: /* BVS: V */
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R2, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0xA: /* BPL: !N */
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R0, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0xB: /* BMI: N */
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R0, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0xC: /* BGE: N == V */
                jit_emit_dword(ctx, AARCH64_EOR_W(AARCH64_R4, AARCH64_R0, AARCH64_R2));
                jit_emit_dword(ctx, AARCH64_AND_W(AARCH64_R4, AARCH64_R4, 1));
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R4, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0xD: /* BLT: N != V */
                jit_emit_dword(ctx, AARCH64_EOR_W(AARCH64_R4, AARCH64_R0, AARCH64_R2));
                jit_emit_dword(ctx, AARCH64_AND_W(AARCH64_R4, AARCH64_R4, 1));
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R4, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0xE: /* BGT: !Z && (N == V) */
                jit_emit_dword(ctx, AARCH64_EOR_W(AARCH64_R4, AARCH64_R0, AARCH64_R2));
                jit_emit_dword(ctx, AARCH64_AND_W(AARCH64_R4, AARCH64_R4, 1));
                jit_emit_dword(ctx, AARCH64_ORR_W(AARCH64_R4, AARCH64_R4, AARCH64_R1));
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R4, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
            case 0xF: /* BLE: Z || (N != V) */
                jit_emit_dword(ctx, AARCH64_EOR_W(AARCH64_R4, AARCH64_R0, AARCH64_R2));
                jit_emit_dword(ctx, AARCH64_AND_W(AARCH64_R4, AARCH64_R4, 1));
                jit_emit_dword(ctx, AARCH64_ORR_W(AARCH64_R4, AARCH64_R4, AARCH64_R1));
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R4, 4));
                jit_emit_load_pc(ctx, AARCH64_R0);
                jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
                jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
                jit_emit_store_pc(ctx, AARCH64_R0);
                break;
        }
    }
    
    /* Increment PC for fall-through (branch not taken) */
    jit_emit_inc_pc(ctx, instr_size);
    
    (void)ext_count;
    return 0;
}

int jit_translate_bsr(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* BSR: Branch to subroutine - push return address, then branch */
    int32_t disp;
    int instr_size;
    
    /* Get displacement */
    if ((opcode & 0xFF) == 0) {
        disp = (int16_t)ext_words[0];
        instr_size = 4;
    } else {
        disp = (int8_t)(opcode & 0xFF);
        instr_size = 2;
    }
    
    /* Load A7 (SP) */
    jit_emit_load_an(ctx, AARCH64_R0, 7);
    
    /* Decrement SP by 4 */
    jit_emit_mov64(ctx, AARCH64_R1, 4);
    jit_emit_dword(ctx, AARCH64_SUB(AARCH64_R0, AARCH64_R0, AARCH64_R1));
    
    /* Load current PC + instr_size (return address) */
    jit_emit_load_pc(ctx, AARCH64_R1);
    jit_emit_mov64(ctx, AARCH64_R2, instr_size);
    jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R1, AARCH64_R1, AARCH64_R2));
    
    /* Push return address to stack */
    jit_emit_dword(ctx, AARCH64_STR(AARCH64_R1, AARCH64_R0, 0));
    
    /* Store updated SP */
    jit_emit_store_an(ctx, AARCH64_R0, 7);
    
    /* Calculate target PC = current PC + instr_size + disp */
    jit_emit_load_pc(ctx, AARCH64_R0);
    jit_emit_mov64(ctx, AARCH64_R1, instr_size + disp);
    jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
    jit_emit_store_pc(ctx, AARCH64_R0);
    
    (void)ext_count;
    return 0;
}

int jit_translate_rts(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* RTS: Return from subroutine - pop PC from stack */
    /* Load A7 (SP) */
    jit_emit_load_an(ctx, AARCH64_R0, 7);
    
    /* Load PC from stack */
    jit_emit_dword(ctx, AARCH64_LDR(AARCH64_R1, AARCH64_R0, 0));
    
    /* Store to PC */
    jit_emit_store_pc(ctx, AARCH64_R1);
    
    /* Increment SP by 4 */
    jit_emit_mov64(ctx, AARCH64_R2, 4);
    jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R2));
    jit_emit_store_an(ctx, AARCH64_R0, 7);
    
    /* Increment PC by 2 */
    jit_emit_inc_pc(ctx, 2);
    
    (void)opcode; (void)ext_words; (void)ext_count;
    return 0;
}

static void jit_movec_load_gpr(jit_emit_context_t *ctx, uint8_t reg_field, uint8_t dst)
{
    if (reg_field < 8) {
        jit_emit_load_dn(ctx, dst, reg_field);
    } else {
        jit_emit_load_an(ctx, dst, reg_field & 7);
    }
}

static void jit_movec_store_gpr(jit_emit_context_t *ctx, uint8_t reg_field, uint8_t src)
{
    if (reg_field < 8) {
        jit_emit_store_dn(ctx, src, reg_field);
    } else {
        jit_emit_store_an(ctx, src, reg_field & 7);
    }
}

static uint32_t jit_translate_current_pc(const jit_translate_context_t *tctx)
{
    uint32_t pc = tctx->block->start_pc;
    uint16_t i;
    for (i = 0; i < tctx->instruction_index; i++) {
        pc += 2 + (uint32_t)(tctx->block->instructions[i].ext_count * 2);
    }
    return pc;
}

int jit_translate_jsr(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    uint8_t ea_mode = (opcode >> 3) & 0x7;
    uint8_t ea_reg = opcode & 0x7;
    uint32_t return_pc = jit_translate_current_pc(tctx) + 2 + (uint32_t)(ext_count * 2);

    /* Compute effective address into R2. JSR/JMP target is EA itself, not [EA]. */
    if (ea_mode == 2) {
        jit_emit_load_an(ctx, AARCH64_R2, ea_reg); /* (An) */
    } else if (ea_mode == 5 && ext_count >= 1) {
        jit_emit_load_an(ctx, AARCH64_R2, ea_reg); /* (d16,An) */
        jit_emit_mov64(ctx, AARCH64_R3, (uint64_t)(int32_t)(int16_t)ext_words[0]);
        jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R2, AARCH64_R2, AARCH64_R3));
    } else if (ea_mode == 7 && ea_reg == 0 && ext_count >= 1) {
        jit_emit_mov64(ctx, AARCH64_R2, (uint64_t)(int32_t)(int16_t)ext_words[0]); /* (xxx).W */
    } else if (ea_mode == 7 && ea_reg == 1 && ext_count >= 2) {
        uint32_t abs_l = ((uint32_t)ext_words[0] << 16) | ext_words[1]; /* (xxx).L */
        jit_emit_mov64(ctx, AARCH64_R2, abs_l);
    } else {
        return -1;
    }

    /* Push return PC on supervisor stack (A7). */
    jit_emit_load_an(ctx, AARCH64_R0, 7);
    jit_emit_mov64(ctx, AARCH64_R1, 4);
    jit_emit_dword(ctx, AARCH64_SUB(AARCH64_R0, AARCH64_R0, AARCH64_R1));
    jit_emit_mov64(ctx, AARCH64_R1, return_pc);
    jit_emit_dword(ctx, AARCH64_STR_W(AARCH64_R1, AARCH64_R0, 0));
    jit_emit_store_an(ctx, AARCH64_R0, 7);

    /* PC = EA (no dereference). */
    jit_emit_store_pc(ctx, AARCH64_R2);
    return 0;
}

int jit_translate_jmp(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    uint8_t ea_mode = (opcode >> 3) & 0x7;
    uint8_t ea_reg = opcode & 0x7;

    /* Compute effective address into R2. JMP target is EA itself, not [EA]. */
    if (ea_mode == 2) {
        jit_emit_load_an(ctx, AARCH64_R2, ea_reg); /* (An) */
    } else if (ea_mode == 5 && ext_count >= 1) {
        jit_emit_load_an(ctx, AARCH64_R2, ea_reg); /* (d16,An) */
        jit_emit_mov64(ctx, AARCH64_R3, (uint64_t)(int32_t)(int16_t)ext_words[0]);
        jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R2, AARCH64_R2, AARCH64_R3));
    } else if (ea_mode == 7 && ea_reg == 0 && ext_count >= 1) {
        jit_emit_mov64(ctx, AARCH64_R2, (uint64_t)(int32_t)(int16_t)ext_words[0]); /* (xxx).W */
    } else if (ea_mode == 7 && ea_reg == 1 && ext_count >= 2) {
        uint32_t abs_l = ((uint32_t)ext_words[0] << 16) | ext_words[1]; /* (xxx).L */
        jit_emit_mov64(ctx, AARCH64_R2, abs_l);
    } else {
        return -1;
    }

    /* PC = EA (no dereference). */
    jit_emit_store_pc(ctx, AARCH64_R2);
    return 0;
}

int jit_translate_movec(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    m68ki_cpu_core *cpu = tctx->jit ? tctx->jit->cpu : NULL;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    uint16_t ext;
    uint8_t reg_field;
    uint16_t creg;
    int rn_to_cr;
    int handled = 1;
    const int off_sp = (int)offsetof(struct m68ki_cpu_core, sp);
    const int off_sp_elem = (int)sizeof(((struct m68ki_cpu_core *)0)->sp[0]);
    const int off_vbr = (int)offsetof(struct m68ki_cpu_core, vbr);
    const int off_sfc = (int)offsetof(struct m68ki_cpu_core, sfc);
    const int off_dfc = (int)offsetof(struct m68ki_cpu_core, dfc);
    const int off_cacr = (int)offsetof(struct m68ki_cpu_core, cacr);
    const int off_mmu_mmusr = (int)offsetof(struct m68ki_cpu_core, mmu_sr_040);
    const int off_mmu_urp = (int)offsetof(struct m68ki_cpu_core, mmu_urp_aptr);
    const int off_mmu_srp = (int)offsetof(struct m68ki_cpu_core, mmu_srp_aptr);

    if (!cpu || ext_count < 1) {
        return -1;
    }

    /* MOVEC is privileged; force fallback outside supervisor mode. */
    if (!cpu->s_flag) {
        return -1;
    }

    ext = ext_words[0];

    /* 68040 cache/MMU ops sharing MOVEC space (PFLUSH/CPUSH class). */
    if ((ext & 0xF000) == 0xF000) {
        jit_emit_inc_pc(ctx, 4);
        return 0;
    }

    /* MOVEC decode (matches Musashi): reg field in bits 15..12, control reg in bits 11..0. */
    reg_field = (uint8_t)((ext >> 12) & 0x0F); /* 0..7 Dn, 8..15 An */
    creg = ext & 0x0FFF;
    rn_to_cr = (opcode & 1) ? 1 : 0;          /* 4E7B: Rn->CR, 4E7A: CR->Rn */

    if (!rn_to_cr) {
        /* MOVEC CR,Rn */
        switch (creg) {
        case 0x000: /* SFC */
            jit_emit_load_cpu_reg(ctx, AARCH64_R0, off_sfc);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x001: /* DFC */
            jit_emit_load_cpu_reg(ctx, AARCH64_R0, off_dfc);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x800: /* USP */
            jit_emit_load_cpu_reg(ctx, AARCH64_R0, off_sp + (0 * off_sp_elem));
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x801: /* VBR */
            jit_emit_load_cpu_reg(ctx, AARCH64_R0, off_vbr);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x803: /* MSP */
            jit_emit_load_cpu_reg(ctx, AARCH64_R0, off_sp + (6 * off_sp_elem));
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x804: /* ISP */
            jit_emit_load_cpu_reg(ctx, AARCH64_R0, off_sp + (4 * off_sp_elem));
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x002: /* CACR */
            jit_emit_load_cpu_reg(ctx, AARCH64_R0, off_cacr);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x808: /* PCR */
            jit_emit_mov64(ctx, AARCH64_R0, 0x04310000u);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x003: /* TC */
            jit_emit_load_tc(ctx, AARCH64_R0);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x004: /* ITT0 */
            jit_emit_load_itt0(ctx, AARCH64_R0);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x005: /* ITT1 */
            jit_emit_load_itt1(ctx, AARCH64_R0);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x006: /* DTT0 */
            jit_emit_load_dtt0(ctx, AARCH64_R0);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x007: /* DTT1 */
            jit_emit_load_dtt1(ctx, AARCH64_R0);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x805: /* MMUSR */
            jit_emit_load_cpu_reg(ctx, AARCH64_R0, off_mmu_mmusr);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x806: /* URP */
            jit_emit_load_cpu_reg(ctx, AARCH64_R0, off_mmu_urp);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        case 0x807: /* SRP */
            jit_emit_load_cpu_reg(ctx, AARCH64_R0, off_mmu_srp);
            jit_movec_store_gpr(ctx, reg_field, AARCH64_R0);
            break;
        default:
            handled = 0;
            break;
        }
    } else {
        /* MOVEC Rn,CR */
        jit_movec_load_gpr(ctx, reg_field, AARCH64_R0);
        switch (creg) {
        case 0x000: /* SFC */
            jit_emit_store_cpu_reg(ctx, AARCH64_R0, off_sfc);
            break;
        case 0x001: /* DFC */
            jit_emit_store_cpu_reg(ctx, AARCH64_R0, off_dfc);
            break;
        case 0x800: /* USP */
            jit_emit_store_cpu_reg(ctx, AARCH64_R0, off_sp + (0 * off_sp_elem));
            break;
        case 0x801: /* VBR */
            jit_emit_store_cpu_reg(ctx, AARCH64_R0, off_vbr);
            break;
        case 0x803: /* MSP */
            jit_emit_store_cpu_reg(ctx, AARCH64_R0, off_sp + (6 * off_sp_elem));
            break;
        case 0x804: /* ISP */
            jit_emit_store_cpu_reg(ctx, AARCH64_R0, off_sp + (4 * off_sp_elem));
            break;
        case 0x002: /* CACR */
            jit_emit_store_cpu_reg(ctx, AARCH64_R0, off_cacr);
            break;
        case 0x003: /* TC */
            jit_emit_store_tc(ctx, AARCH64_R0);
            break;
        case 0x004: /* ITT0 */
            jit_emit_store_itt0(ctx, AARCH64_R0);
            break;
        case 0x005: /* ITT1 */
            jit_emit_store_itt1(ctx, AARCH64_R0);
            break;
        case 0x006: /* DTT0 */
            jit_emit_store_dtt0(ctx, AARCH64_R0);
            break;
        case 0x007: /* DTT1 */
            jit_emit_store_dtt1(ctx, AARCH64_R0);
            break;
        case 0x805: /* MMUSR */
            jit_emit_store_cpu_reg(ctx, AARCH64_R0, off_mmu_mmusr);
            break;
        case 0x806: /* URP */
            jit_emit_store_cpu_reg(ctx, AARCH64_R0, off_mmu_urp);
            break;
        case 0x807: /* SRP */
            jit_emit_store_cpu_reg(ctx, AARCH64_R0, off_mmu_srp);
            break;
        default:
            handled = 0;
            break;
        }
    }

    if (!handled) {
        return -1;
    }

    jit_emit_inc_pc(ctx, 4);
    return 0;
}

int jit_translate_extb(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* EXTB: Sign Extend Byte to Long (68020+)
     * Format: 0100100011000nnn (0x49C0-0x49C7)
     * Extends bit 7 of Dn to fill all 32 bits
     */
    (void)ext_words; (void)ext_count;
    
    uint8_t dn = opcode & 7;
    
    /* Load Dn, sign extend byte to 32 bits */
    jit_emit_load_dn(ctx, AARCH64_R0, dn);
    /* LSL #24, then ASR #24 to sign-extend byte */
    jit_emit_dword(ctx, AARCH64_LSL(AARCH64_R0, AARCH64_R0, 24));
    jit_emit_dword(ctx, AARCH64_ASR(AARCH64_R0, AARCH64_R0, 24));
    jit_emit_store_dn(ctx, AARCH64_R0, dn);
    
    /* Set N and Z flags, clear V and C */
    /* TODO: implement CCR update */
    
    /* Increment PC */
    jit_emit_inc_pc(ctx, 2);
    
    return 0;
}

int jit_translate_lea(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* LEA: Load Effective Address
     * Format: 0100 1111 MM MMM RRR (for xxx.L,An)
     *       or 0100 dd01 MM MMM RRR (for d16(An),An) etc.
     * Bits 11-6 = EA mode, Bits 5-3 = An (dest), Bits 2-0 = unused
     * 
     * EA modes supported:
     * - d16(An): 010 (ext_words[0] = displacement)
     * - d16(PC): 111 010 (ext_words[0] = displacement)
     * - xxx.W: 111 110 (ext_words[0] = absolute address)
     * - xxx.L: 111 111 (ext_words[0] = high word, ext_words[1] = low word)
     */
    (void)ext_count;
    
    uint8_t ea_mode = (opcode >> 3) & 0x7;
    uint8_t ea_reg = opcode & 0x7;
    uint8_t an_dest = (opcode >> 9) & 0x7;
    
    /* Calculate effective address based on EA mode */
    switch (ea_mode) {
        case 2: /* (An) - Address register indirect */
            /* LEA (An),An - just copy address register */
            jit_emit_load_an(ctx, AARCH64_R0, ea_reg);
            jit_emit_store_an(ctx, AARCH64_R0, an_dest);
            break;
            
        case 5: /* d16(An) - Address register indirect with displacement */
            /* LEA d16(An),An - An + sign-extended displacement */
            jit_emit_load_an(ctx, AARCH64_R0, ea_reg);
            jit_emit_mov64(ctx, AARCH64_R1, (uint64_t)(int32_t)(int16_t)ext_words[0]);
            jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
            jit_emit_store_an(ctx, AARCH64_R0, an_dest);
            break;
            
        case 6: /* d16(PC) - PC relative with displacement */
            /* LEA d16(PC),An - PC + sign-extended displacement */
            /* Note: PC here is the PC after the instruction */
            jit_emit_mov64(ctx, AARCH64_R0, (uint64_t)(int32_t)(int16_t)ext_words[0]);
            /* We need the actual PC value - this requires runtime PC */
            /* For now, emit a load from m68ki_cpu.pc + displacement */
            jit_emit_load_cpu_reg(ctx, AARCH64_R1, 128);  /* Load PC */
            jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
            jit_emit_store_an(ctx, AARCH64_R0, an_dest);
            break;
            
        case 7: /* Absolute - check ea_reg for .W vs .L */
            if (ea_reg == 0) {
                /* xxx.W - absolute word (sign extended) */
                jit_emit_mov64(ctx, AARCH64_R0, (uint64_t)(int32_t)(int16_t)ext_words[0]);
            } else if (ea_reg == 1) {
                /* xxx.L - absolute long */
                jit_emit_mov64(ctx, AARCH64_R0, ((uint64_t)ext_words[0] << 16) | ext_words[1]);
            }
            jit_emit_store_an(ctx, AARCH64_R0, an_dest);
            break;
            
        default:
            /* Unsupported EA mode - fall back */
            return -1;
    }
    
    /* Increment PC */
    jit_emit_inc_pc(ctx, 2 + ext_count * 2);
    
    return 0;
}

int jit_translate_misc(jit_translate_context_t *tctx)
{
    jit_emit_context_t *ctx = tctx->emit;
    uint16_t opcode = tctx->opcode;
    uint16_t *ext_words = tctx->ext_words;
    int ext_count = tctx->ext_count;
    /* DBcc: 0101 cond(4) 11001 Dn (4 bytes total) */
    if ((opcode & 0xF0F8) == 0x50C8) {
        uint8_t cond = (opcode >> 8) & 0x0F;
        uint8_t dn = opcode & 0x7;
        int16_t disp = (int16_t)ext_words[0];
        
        /* Load Dn counter */
        jit_emit_load_dn(ctx, AARCH64_R0, dn);
        
        /* Decrement counter */
        jit_emit_mov64(ctx, AARCH64_R1, 1);
        jit_emit_dword(ctx, AARCH64_SUB(AARCH64_R0, AARCH64_R0, AARCH64_R1));
        jit_emit_store_dn(ctx, AARCH64_R0, dn);
        
        /* Check if counter == -1 (0xFFFF) */
        jit_emit_dword(ctx, AARCH64_AND(AARCH64_R1, AARCH64_R0, 0xFFFF));
        
        /* Load CCR flags for condition check */
        jit_emit_dword(ctx, AARCH64_LDR_W(AARCH64_R2, AARCH64_CPU_PTR, 348)); /* n_flag */
        jit_emit_dword(ctx, AARCH64_LDR_W(AARCH64_R3, AARCH64_CPU_PTR, 352)); /* not_z_flag */
        jit_emit_dword(ctx, AARCH64_LDR_W(AARCH64_R4, AARCH64_CPU_PTR, 356)); /* v_flag */
        jit_emit_dword(ctx, AARCH64_LDR_W(AARCH64_R5, AARCH64_CPU_PTR, 360)); /* c_flag */
        
        /* Convert to boolean */
        jit_emit_dword(ctx, AARCH64_LSR(AARCH64_R2, AARCH64_R2, 7)); /* n */
        jit_emit_dword(ctx, AARCH64_AND(AARCH64_R3, AARCH64_R3, 1)); /* z */
        jit_emit_dword(ctx, AARCH64_LSR(AARCH64_R4, AARCH64_R4, 7)); /* v */
        jit_emit_dword(ctx, AARCH64_LSR(AARCH64_R5, AARCH64_R5, 8)); /* c */
        
        /* Evaluate condition - if TRUE, don't branch (counter not -1) */
        switch (cond) {
            case 0x0: /* DBRA/DBT: always branch if counter != -1 */
                break; /* Always true, just check counter */
            case 0x1: /* DBF/DBRA: always false, just check counter */
                break;
            case 0x2: /* DBHI: !C && !Z */
                jit_emit_dword(ctx, AARCH64_ORR_W(AARCH64_R6, AARCH64_R5, AARCH64_R3));
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R6, 8)); /* if C || Z, skip branch */
                break;
            case 0x3: /* DBLS: C || Z */
                jit_emit_dword(ctx, AARCH64_ORR_W(AARCH64_R6, AARCH64_R5, AARCH64_R3));
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R6, 8)); /* if !C && !Z, skip branch */
                break;
            case 0x4: /* DBCC: !C */
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R5, 8));
                break;
            case 0x5: /* DBCS: C */
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R5, 8));
                break;
            case 0x6: /* DBNE: !Z */
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R3, 8));
                break;
            case 0x7: /* DBEQ: Z */
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R3, 8));
                break;
            case 0x8: /* DBVC: !V */
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R4, 8));
                break;
            case 0x9: /* DBVS: V */
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R4, 8));
                break;
            case 0xA: /* DBPL: !N */
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R2, 8));
                break;
            case 0xB: /* DBMI: N */
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R2, 8));
                break;
            case 0xC: /* DBGE: N == V */
                jit_emit_dword(ctx, AARCH64_EOR_W(AARCH64_R6, AARCH64_R2, AARCH64_R4));
                jit_emit_dword(ctx, AARCH64_AND(AARCH64_R6, AARCH64_R6, 1));
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R6, 8));
                break;
            case 0xD: /* DBLT: N != V */
                jit_emit_dword(ctx, AARCH64_EOR_W(AARCH64_R6, AARCH64_R2, AARCH64_R4));
                jit_emit_dword(ctx, AARCH64_AND(AARCH64_R6, AARCH64_R6, 1));
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R6, 8));
                break;
            case 0xE: /* DBGT: !Z && (N == V) */
                jit_emit_dword(ctx, AARCH64_EOR_W(AARCH64_R6, AARCH64_R2, AARCH64_R4));
                jit_emit_dword(ctx, AARCH64_AND(AARCH64_R6, AARCH64_R6, 1));
                jit_emit_dword(ctx, AARCH64_ORR_W(AARCH64_R6, AARCH64_R6, AARCH64_R3));
                jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R6, 8));
                break;
            case 0xF: /* DBLE: Z || (N != V) */
                jit_emit_dword(ctx, AARCH64_EOR_W(AARCH64_R6, AARCH64_R2, AARCH64_R4));
                jit_emit_dword(ctx, AARCH64_AND(AARCH64_R6, AARCH64_R6, 1));
                jit_emit_dword(ctx, AARCH64_ORR_W(AARCH64_R6, AARCH64_R6, AARCH64_R3));
                jit_emit_dword(ctx, AARCH64_CBZ(AARCH64_R6, 8));
                break;
        }
        
        /* Branch if counter != -1 */
        jit_emit_dword(ctx, AARCH64_CBNZ(AARCH64_R1, 8)); /* if counter != -1, skip branch */
        
        /* Branch taken: update PC */
        jit_emit_load_pc(ctx, AARCH64_R0);
        jit_emit_mov64(ctx, AARCH64_R1, 4 + disp);
        jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
        jit_emit_store_pc(ctx, AARCH64_R0);
        
        /* Increment PC for fall-through */
        jit_emit_inc_pc(ctx, 4);
        
        return 0;
    }
    
    /* CLR, TST, bit ops - fallback to Musashi */
    (void)ctx; (void)opcode; (void)ext_words; (void)ext_count;
    return -1;
}
