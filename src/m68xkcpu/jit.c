/*
 * JIT Core Implementation
 * 
 * Main execution engine for the AArch64 JIT layer.
 */

#include "jit.h"
#include "jit_block.h"
#include "jit_cache.h"
#include "jit_arch.h"
#include "musashi/m68k.h"
#include "musashi/m68kcpu.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <ucontext.h>

/* Global JIT context */
jit_context_t g_jit = {0};
static const char *g_jit_last_compile_fail_reason = "unknown";
static int g_jit_faststep_enabled = -1;
static int g_jit_safe_interp_enabled = -1;
static int g_jit_no_fallback_enabled = -1;
static int g_jit_trace_blocks_enabled = -1;

/* Signal handler for illegal instruction (SIGILL) */
static void jit_sigill_handler(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = (ucontext_t *)context;
    uint64_t fault_pc = uc->uc_mcontext.pc;
    
    fprintf(stderr, "\n\n!!! SIGILL - Illegal Instruction !!!\n");
    fprintf(stderr, "Fault PC: 0x%016lX (AArch64)\n", fault_pc);
    fprintf(stderr, "JIT enabled: %d\n", g_jit.enabled);
    fprintf(stderr, "Safe interp: %d\n", g_jit_safe_interp_enabled);
    fprintf(stderr, "Current 68k PC: 0x%08X\n", g_jit.current_pc);
    
    if (g_jit.cpu) {
        fprintf(stderr, "\n68k CPU State:\n");
        fprintf(stderr, "D0-D7: %08X %08X %08X %08X %08X %08X %08X %08X\n",
                g_jit.cpu->dar[0], g_jit.cpu->dar[1], g_jit.cpu->dar[2], g_jit.cpu->dar[3],
                g_jit.cpu->dar[4], g_jit.cpu->dar[5], g_jit.cpu->dar[6], g_jit.cpu->dar[7]);
        fprintf(stderr, "A0-A7: %08X %08X %08X %08X %08X %08X %08X %08X\n",
                g_jit.cpu->dar[8], g_jit.cpu->dar[9], g_jit.cpu->dar[10], g_jit.cpu->dar[11],
                g_jit.cpu->dar[12], g_jit.cpu->dar[13], g_jit.cpu->dar[14], g_jit.cpu->dar[15]);
        fprintf(stderr, "PC: %08X\n", g_jit.cpu->pc);
    }
    
    fprintf(stderr, "\nThis is likely a JIT code generation bug.\n");
    fprintf(stderr, "Use PISTORM_M68XK_SAFE_INTERP=1 for stable (slow) operation.\n");
    
    _exit(1);
}

static void jit_install_signal_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = jit_sigill_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGILL, &sa, NULL);
}

static int jit_faststep_enabled(void)
{
    if (g_jit_faststep_enabled < 0) {
        const char *e = getenv("PISTORM_M68XK_FASTSTEP");
        /* Safety-first default: disabled unless explicitly enabled. */
        g_jit_faststep_enabled = (e && atoi(e) != 0) ? 1 : 0;
        if (g_jit_faststep_enabled) {
            LOG_INFO("[CPU] m68xkcpu fast-step enabled via PISTORM_M68XK_FASTSTEP=1\n");
        } else if (e) {
            LOG_WARN("[CPU] m68xkcpu fast-step disabled via PISTORM_M68XK_FASTSTEP=0\n");
        } else {
            LOG_INFO("[CPU] m68xkcpu fast-step disabled by default (set PISTORM_M68XK_FASTSTEP=1 to enable)\n");
        }
    }
    return g_jit_faststep_enabled;
}

static int jit_safe_interp_enabled(void)
{
    if (g_jit_safe_interp_enabled < 0) {
        const char *e = getenv("PISTORM_M68XK_SAFE_INTERP");
        /* Safety-first default: keep m68xkcpu in interpret-only mode unless explicitly disabled. */
        g_jit_safe_interp_enabled = (e && atoi(e) == 0) ? 0 : 1;
        if (g_jit_safe_interp_enabled) {
            if (e) {
                LOG_INFO("[CPU] m68xkcpu safe interpret-only mode enabled via PISTORM_M68XK_SAFE_INTERP=1\n");
            } else {
                LOG_INFO("[CPU] m68xkcpu safe interpret-only mode enabled by default (set PISTORM_M68XK_SAFE_INTERP=0 to allow decode/emit path)\n");
            }
        } else {
            LOG_WARN("[CPU] m68xkcpu safe interpret-only mode disabled via PISTORM_M68XK_SAFE_INTERP=0\n");
        }
    }
    return g_jit_safe_interp_enabled;
}

static int jit_trace_blocks_enabled(void)
{
    if (g_jit_trace_blocks_enabled < 0) {
        const char *e = getenv("PISTORM_M68XK_TRACE_BLOCKS");
        g_jit_trace_blocks_enabled = (e && atoi(e) != 0) ? 1 : 0;
    }
    return g_jit_trace_blocks_enabled;
}

static void jit_trace_block_exec(const char *phase, uint32_t pc, uint32_t new_pc, int cycles, uint8_t ends_block)
{
    if (!jit_trace_blocks_enabled()) {
        return;
    }
    if (phase && strcmp(phase, "enter") == 0) {
        LOG_INFO("[CPU][m68xkcpu][BLOCK] enter pc=0x%08X cycles=%d ends_block=%u\n",
                 pc, cycles, (unsigned)ends_block);
    } else {
        LOG_INFO("[CPU][m68xkcpu][BLOCK] exit  pc=0x%08X new_pc=0x%08X cycles=%d ends_block=%u\n",
                 pc, new_pc, cycles, (unsigned)ends_block);
    }
}

int jit_no_fallback_enabled(void)
{
    if (g_jit_no_fallback_enabled < 0) {
        const char *e = getenv("PISTORM_M68XK_NO_FALLBACK");
        g_jit_no_fallback_enabled = (e && atoi(e) != 0) ? 1 : 0;
        if (g_jit_no_fallback_enabled) {
            LOG_WARN("[CPU] m68xkcpu NO_FALLBACK mode enabled - unsupported instructions will abort()\n");
        }
    }
    return g_jit_no_fallback_enabled;
}

void jit_hard_fail(uint32_t pc, uint16_t opcode, const char *reason)
{
    LOG_ERROR("[CPU] m68xkcpu HARD FAIL at PC=0x%08X: opcode=0x%04X reason=%s\n", pc, opcode, reason);
    LOG_ERROR("[CPU] m68xkcpu: Aborting due to unsupported instruction with NO_FALLBACK=1\n");
    
    /* Dump CPU state for debugging */
    extern struct m68ki_cpu_core m68ki_cpu;
    LOG_ERROR("[CPU] D0-D7: %08X %08X %08X %08X %08X %08X %08X %08X\n",
              m68ki_cpu.dar[0], m68ki_cpu.dar[1], m68ki_cpu.dar[2], m68ki_cpu.dar[3],
              m68ki_cpu.dar[4], m68ki_cpu.dar[5], m68ki_cpu.dar[6], m68ki_cpu.dar[7]);
    LOG_ERROR("[CPU] A0-A7: %08X %08X %08X %08X %08X %08X %08X %08X\n",
              m68ki_cpu.dar[8], m68ki_cpu.dar[9], m68ki_cpu.dar[10], m68ki_cpu.dar[11],
              m68ki_cpu.dar[12], m68ki_cpu.dar[13], m68ki_cpu.dar[14], m68ki_cpu.dar[15]);
    LOG_ERROR("[CPU] PC=%08X SR=%04X\n", m68ki_cpu.pc, 
              (m68ki_cpu.s_flag << 13) | (m68ki_cpu.m_flag << 11) | (m68ki_cpu.int_mask << 8) |
              ((m68ki_cpu.x_flag >> 4) << 4) | ((m68ki_cpu.n_flag >> 4) << 3) |
              ((!m68ki_cpu.not_z_flag) << 2) | ((m68ki_cpu.v_flag >> 6) << 1) | (m68ki_cpu.c_flag >> 8));
    
    abort();
}

static int jit_fallback_interpreter_step(jit_context_t *jit, int cycles_budget)
{
    int ran;
    int slice;
    if (!jit || !jit->cpu) {
        return -1;
    }

    if (cycles_budget <= 0) {
        return 0;
    }

    /* Keep fallback semantics aligned with the emulator's normal Musashi path. */
    extern void musashi_backend_execute(m68ki_cpu_core *state, int cycles);
    slice = (cycles_budget > 256) ? 256 : cycles_budget;
    musashi_backend_execute(jit->cpu, slice);
    ran = slice;
    jit->current_pc = (uint32_t)m68k_get_reg(jit->cpu, M68K_REG_PC);
    return ran;
}

static inline int jit_flag_c(const m68ki_cpu_core *cpu) { return (cpu->c_flag & 0x100u) != 0; }
static inline int jit_flag_v(const m68ki_cpu_core *cpu) { return (cpu->v_flag & 0x80u) != 0; }
static inline int jit_flag_n(const m68ki_cpu_core *cpu) { return (cpu->n_flag & 0x80u) != 0; }
static inline int jit_flag_z(const m68ki_cpu_core *cpu) { return cpu->not_z_flag == 0; }

static int jit_eval_cond(const m68ki_cpu_core *cpu, uint8_t cond)
{
    int c = jit_flag_c(cpu);
    int v = jit_flag_v(cpu);
    int n = jit_flag_n(cpu);
    int z = jit_flag_z(cpu);
    switch (cond & 0x0F) {
    case 0x0: return 1;                 /* T  */
    case 0x1: return 0;                 /* F  */
    case 0x2: return (!c) && (!z);      /* HI */
    case 0x3: return c || z;            /* LS */
    case 0x4: return !c;                /* CC */
    case 0x5: return c;                 /* CS */
    case 0x6: return !z;                /* NE */
    case 0x7: return z;                 /* EQ */
    case 0x8: return !v;                /* VC */
    case 0x9: return v;                 /* VS */
    case 0xA: return !n;                /* PL */
    case 0xB: return n;                 /* MI */
    case 0xC: return n == v;            /* GE */
    case 0xD: return n != v;            /* LT */
    case 0xE: return (!z) && (n == v);  /* GT */
    case 0xF: return z || (n != v);     /* LE */
    default: return 0;
    }
}

static int jit_cycles_base(const m68ki_cpu_core *cpu, uint16_t opcode)
{
    int c = (int)cpu->cyc_instruction[opcode];
    return (c > 0) ? c : 1;
}

static void jit_flags_add_32(m68ki_cpu_core *cpu, uint32_t src, uint32_t dst, uint32_t res)
{
    uint64_t wide = (uint64_t)src + (uint64_t)dst;
    cpu->n_flag = (res >> 24) & 0x80;
    cpu->v_flag = (((src ^ res) & (dst ^ res)) >> 24) & 0x80;
    cpu->c_flag = cpu->x_flag = (wide >> 32) ? 0x100 : 0;
    cpu->not_z_flag = res;
}

static void jit_flags_add_16(m68ki_cpu_core *cpu, uint16_t src, uint16_t dst, uint16_t res)
{
    uint32_t wide = (uint32_t)src + (uint32_t)dst;
    cpu->n_flag = (res >> 8) & 0x80;
    cpu->v_flag = (((src ^ res) & (dst ^ res)) >> 8) & 0x80;
    cpu->c_flag = cpu->x_flag = (wide & 0x10000u) ? 0x100 : 0;
    cpu->not_z_flag = res;
}

static void jit_flags_add_8(m68ki_cpu_core *cpu, uint8_t src, uint8_t dst, uint8_t res, uint16_t wide)
{
    cpu->n_flag = res & 0x80;
    cpu->v_flag = ((src ^ res) & (dst ^ res)) & 0x80;
    cpu->c_flag = cpu->x_flag = (wide & 0x100u) ? 0x100 : 0;
    cpu->not_z_flag = res;
}

static int jit_try_fast_step(jit_context_t *jit, uint32_t pc, int cycles_budget)
{
    m68ki_cpu_core *cpu;
    uint16_t opcode;
    uint8_t fc_prog;
    uint8_t fc_data;

    if (!jit || !jit->cpu || cycles_budget <= 0) {
        return 0;
    }

    cpu = jit->cpu;
    fc_prog = jit_get_fc(cpu->s_flag ? 1 : 0, 1);
    fc_data = jit_get_fc(cpu->s_flag ? 1 : 0, 0);
    opcode = jit_fetch_word(pc, fc_prog);

    /* Bcc/BRA (8-bit and 16-bit displacement), excluding BSR. */
    if ((opcode & 0xF000) == 0x6000) {
        uint8_t cond = (uint8_t)((opcode >> 8) & 0x0F);
        int taken;
        int cycles = jit_cycles_base(cpu, opcode);
        if (cond == 0x1) {
            return 0; /* BSR not handled here yet. */
        }
        taken = jit_eval_cond(cpu, cond);
        if ((opcode & 0x00FF) == 0x00) {
            int16_t disp = (int16_t)jit_fetch_word(pc + 2, fc_prog);
            cpu->pc = taken ? (uint32_t)(pc + 2 + disp) : (pc + 4);
            if (!taken) cycles += cpu->cyc_bcc_notake_w;
        } else {
            int8_t disp = (int8_t)(opcode & 0x00FF);
            cpu->pc = taken ? (uint32_t)(pc + 2 + disp) : (pc + 2);
            if (!taken) cycles += cpu->cyc_bcc_notake_b;
        }
        jit->current_pc = cpu->pc;
        return (cycles > 0) ? cycles : 1;
    }

    /* DBcc */
    if ((opcode & 0xF0F8) == 0x50C8) {
        uint8_t cond = (uint8_t)((opcode >> 8) & 0x0F);
        uint8_t dn = opcode & 0x07;
        int16_t disp = (int16_t)jit_fetch_word(pc + 2, fc_prog);
        int cycles = jit_cycles_base(cpu, opcode);
        if (!jit_eval_cond(cpu, cond)) {
            uint16_t ctr = (uint16_t)((cpu->dar[dn] - 1) & 0xFFFFu);
            cpu->dar[dn] = (cpu->dar[dn] & 0xFFFF0000u) | ctr;
            if (ctr != 0xFFFFu) {
                cpu->pc = (uint32_t)(pc + 2 + disp);
                cycles += cpu->cyc_dbcc_f_noexp;
            } else {
                cpu->pc = pc + 4;
                cycles += cpu->cyc_dbcc_f_exp;
            }
        } else {
            cpu->pc = pc + 4;
        }
        jit->current_pc = cpu->pc;
        return (cycles > 0) ? cycles : 1;
    }

    /* ADDQ */
    if ((opcode & 0xF100) == 0x5000) {
        uint8_t imm = (uint8_t)((opcode >> 9) & 7);
        uint8_t sz = (uint8_t)((opcode >> 6) & 3);
        uint8_t mode = (uint8_t)((opcode >> 3) & 7);
        uint8_t reg = opcode & 7;
        int cycles = jit_cycles_base(cpu, opcode);
        if (imm == 0) imm = 8;

        if (mode == 0) { /* Dn */
            uint32_t d = cpu->dar[reg];
            if (sz == 0) {
                uint8_t dst = (uint8_t)d;
                uint16_t wide = (uint16_t)dst + imm;
                uint8_t res = (uint8_t)wide;
                cpu->dar[reg] = (d & 0xFFFFFF00u) | res;
                jit_flags_add_8(cpu, imm, dst, res, wide);
            } else if (sz == 1) {
                uint16_t dst = (uint16_t)d;
                uint16_t res = (uint16_t)(dst + imm);
                cpu->dar[reg] = (d & 0xFFFF0000u) | res;
                jit_flags_add_16(cpu, imm, dst, res);
            } else if (sz == 2) {
                uint32_t dst = d;
                uint32_t res = dst + imm;
                cpu->dar[reg] = res;
                jit_flags_add_32(cpu, imm, dst, res);
            } else {
                return 0;
            }
        } else if (mode == 1) { /* An */
            if (sz == 1 || sz == 2) {
                cpu->dar[8 + reg] = cpu->dar[8 + reg] + imm;
            } else {
                return 0;
            }
        } else {
            return 0;
        }

        cpu->pc = pc + 2;
        jit->current_pc = cpu->pc;
        return (cycles > 0) ? cycles : 1;
    }

    /* LEA */
    if ((opcode & 0xF1C0) == 0x41C0) {
        uint8_t an = (uint8_t)((opcode >> 9) & 7);
        uint8_t mode = (uint8_t)((opcode >> 3) & 7);
        uint8_t reg = opcode & 7;
        uint32_t ea;
        int cycles = jit_cycles_base(cpu, opcode);

        if (mode == 7 && reg == 0) { /* abs.w */
            ea = (uint32_t)(int32_t)(int16_t)jit_fetch_word(pc + 2, fc_prog);
            cpu->pc = pc + 4;
        } else if (mode == 7 && reg == 1) { /* abs.l */
            ea = jit_fetch_long(pc + 2, fc_prog);
            cpu->pc = pc + 6;
        } else if (mode == 2) { /* (An) */
            ea = cpu->dar[8 + reg];
            cpu->pc = pc + 2;
        } else if (mode == 5) { /* (d16,An) */
            int16_t disp = (int16_t)jit_fetch_word(pc + 2, fc_prog);
            ea = cpu->dar[8 + reg] + (int32_t)disp;
            cpu->pc = pc + 4;
        } else if (mode == 7 && reg == 2) { /* (d16,PC) */
            int16_t disp = (int16_t)jit_fetch_word(pc + 2, fc_prog);
            ea = (uint32_t)(pc + 2 + (int32_t)disp);
            cpu->pc = pc + 4;
        } else {
            return 0;
        }

        cpu->dar[8 + an] = ea;
        jit->current_pc = cpu->pc;
        return (cycles > 0) ? cycles : 1;
    }

    /* ADD.L <ea>,Dn (limited: Dn/(An)/(An)+ source forms) */
    if ((opcode & 0xF1C0) == 0xD080) {
        uint8_t dn = (uint8_t)((opcode >> 9) & 7);
        uint8_t opmode = (uint8_t)((opcode >> 6) & 7);
        uint8_t mode = (uint8_t)((opcode >> 3) & 7);
        uint8_t reg = opcode & 7;
        uint32_t src, dst, res;
        int cycles = jit_cycles_base(cpu, opcode);

        if (opmode != 2) {
            return 0;
        }

        if (mode == 0) { /* Dn */
            src = cpu->dar[reg];
        } else if (mode == 2) { /* (An) */
            if (jit_mem_read32(cpu->dar[8 + reg], fc_data, &src) != 0) return 0;
        } else if (mode == 3) { /* (An)+ */
            if (jit_mem_read32(cpu->dar[8 + reg], fc_data, &src) != 0) return 0;
            cpu->dar[8 + reg] += 4;
        } else {
            return 0;
        }

        dst = cpu->dar[dn];
        res = dst + src;
        cpu->dar[dn] = res;
        jit_flags_add_32(cpu, src, dst, res);
        cpu->pc = pc + 2;
        jit->current_pc = cpu->pc;
        return (cycles > 0) ? cycles : 1;
    }

    return 0;
}

static void jit_trace_fallback(jit_context_t *jit, uint32_t pc_before, const char *reason)
{
    static int trace_enabled = -1;
    static int trace_cached = -1;
    static unsigned int trace_count = 0;
    const unsigned int trace_limit = 64;
    uint8_t fc;
    uint16_t opcode;
    uint32_t sr;
    const jit_opinfo_t *opinfo;

    if (!jit || !jit->cpu) {
        return;
    }

    if (trace_enabled < 0) {
        const char *e = getenv("PISTORM_M68XK_TRACE_FALLBACK");
        trace_enabled = (e && atoi(e) != 0) ? 1 : 0;
    }
    if (trace_cached < 0) {
        const char *e = getenv("PISTORM_M68XK_TRACE_FALLBACK_CACHED");
        trace_cached = (e && atoi(e) != 0) ? 1 : 0;
    }
    if (!trace_enabled || trace_count >= trace_limit) {
        return;
    }
    if (!trace_cached && reason && strstr(reason, "cached") != NULL) {
        return;
    }

    sr = (uint32_t)m68k_get_reg(jit->cpu, M68K_REG_SR);
    fc = jit_get_fc((sr & 0x2000u) ? 1 : 0, 1);
    opcode = jit_fetch_word(pc_before, fc);
    opinfo = jit_get_opinfo(opcode);

    LOG_INFO("[CPU][m68xkcpu] fallback[%u] reason=%s pc=0x%08X fc=%u opcode=0x%04X family=%u ext=%u flags=0x%02X\n",
             trace_count + 1, reason ? reason : "unknown", pc_before, (unsigned)fc,
             (unsigned)opcode, (unsigned)opinfo->family, (unsigned)opinfo->ext_words,
             (unsigned)opinfo->flags);
    trace_count++;
}

/**
 * Initialize the JIT subsystem
 * 
 * @param cpu Pointer to the Musashi CPU state
 * @param cache_size Size of the code cache in bytes
 * @return 0 on success, -1 on failure
 */
int jit_init(struct m68ki_cpu_core *cpu, size_t cache_size)
{
#if !defined(__aarch64__)
    (void)cpu;
    (void)cache_size;
    fprintf(stderr, "JIT: m68xkcpu backend currently requires an AArch64 host\n");
    return -1;
#else
    if (g_jit.initialized) {
        fprintf(stderr, "JIT: Already initialized\n");
        return -1;
    }

    /* Install SIGILL handler for debugging */
    jit_install_signal_handler();
    
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
#endif
}


/**
 * Shutdown the JIT subsystem
 */
void jit_shutdown(void)
{
    if (!g_jit.initialized) {
        return;
    }

    {
        const char *e = getenv("PISTORM_M68XK_STATS");
        if (e && atoi(e) != 0) {
            jit_print_stats();
        }
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
    int tight_window_hits = 0;
    
    if (!g_jit.initialized || !g_jit.enabled) {
        return -1;
    }
    
    /* DEBUG: Log entry */
    LOG_INFO("[JIT-EXEC-ENTRY] pc=0x%08X cycles=%d\n", pc, cycles);
    
    /* Sync PC to CPU struct */
    g_jit.cpu->pc = pc;
    g_jit.current_pc = pc;
    
    /* Main execution loop */
    while (cycles_remaining > 0) {
        /* Look up compiled block - use PC from CPU struct for consistency */
        pc = g_jit.cpu->pc;
        block = jit_cache_lookup(&g_jit, pc);
        
        if (block != NULL) {
            if (block->flags & JIT_BLOCK_INTERPRET_ONLY) {
                int ran;
                ran = jit_faststep_enabled() ? jit_try_fast_step(&g_jit, pc, cycles_remaining) : 0;
                if (ran > 0) {
                    g_jit.stats.fast_step_count++;
                    cycles_remaining -= ran;
                    pc = g_jit.current_pc;
                    continue;
                }
                g_jit.stats.fallback_count++;
                jit_trace_fallback(&g_jit, pc, "cached interpret-only -> musashi");
                ran = jit_fallback_interpreter_step(&g_jit, cycles_remaining);
                if (ran <= 0) {
                    break;
                }
                cycles_remaining -= ran;
                pc = g_jit.current_pc;
                continue;
            }

            /* Block found - execute compiled code */
            g_jit.stats.cache_hits++;
            g_jit.stats.blocks_executed++;
            
            /* Execute the compiled block */
            uint32_t pc_before = g_jit.cpu->pc;
            jit_trace_block_exec("enter", pc_before, 0, cycles_remaining, block->ends_block);
            int block_cycles = jit_block_execute(block, cycles_remaining);
            
            /* Read updated PC from CPU struct after execution */
            uint32_t pc_after = g_jit.cpu->pc;
            uint32_t pc_delta = (pc_after >= pc_before) ? (pc_after - pc_before) : (pc_before - pc_after);
            g_jit.current_pc = pc_after;
            jit_trace_block_exec("exit", pc_before, pc_after, block_cycles, block->ends_block);

            if (pc_delta <= 8u) {
                tight_window_hits++;
            } else {
                tight_window_hits = 0;
            }
            
            if (block_cycles <= 0 || pc_after == pc_before || tight_window_hits >= 64) {
                /* Discard non-progress block and execute via interpreter fallback. */
                g_jit.stats.fallback_count++;
                jit_trace_fallback(&g_jit, pc_before,
                                   (tight_window_hits >= 64) ? "tight-loop cached block" : "non-progress cached block");
                jit_cache_remove(&g_jit, block);
                jit_block_free(&g_jit, block);
                block_cycles = jit_fallback_interpreter_step(&g_jit, cycles_remaining);
                if (block_cycles <= 0) {
                    break;
                }
                cycles_remaining -= block_cycles;
                pc = g_jit.cpu->pc;
                tight_window_hits = 0;
                continue;
            }
            
            cycles_remaining -= block_cycles;
            
            /* Update PC after block execution */
            pc = pc_after;
            
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
                if (block->flags & JIT_BLOCK_INTERPRET_ONLY) {
                    int ran;
                    ran = jit_faststep_enabled() ? jit_try_fast_step(&g_jit, pc, cycles_remaining) : 0;
                    if (ran > 0) {
                        g_jit.stats.fast_step_count++;
                        cycles_remaining -= ran;
                        pc = g_jit.current_pc;
                        continue;
                    }
                    g_jit.stats.fallback_count++;
                    jit_trace_fallback(&g_jit, pc, "new interpret-only -> musashi");
                    ran = jit_fallback_interpreter_step(&g_jit, cycles_remaining);
                    if (ran <= 0) {
                        break;
                    }
                    cycles_remaining -= ran;
                    pc = g_jit.current_pc;
                    continue;
                }

                /* Successfully compiled - execute it */
                g_jit.stats.blocks_compiled++;
                
                uint32_t pc_before = g_jit.cpu->pc;
                jit_trace_block_exec("enter", pc_before, 0, cycles_remaining, block->ends_block);
                int block_cycles = jit_block_execute(block, cycles_remaining);
                
                /* Read updated PC from CPU struct after execution */
                uint32_t pc_after = g_jit.cpu->pc;
                uint32_t pc_delta = (pc_after >= pc_before) ? (pc_after - pc_before) : (pc_before - pc_after);
                g_jit.current_pc = pc_after;
                jit_trace_block_exec("exit", pc_before, pc_after, block_cycles, block->ends_block);

                if (pc_delta <= 8u) {
                    tight_window_hits++;
                } else {
                    tight_window_hits = 0;
                }
                
                if (block_cycles <= 0 || pc_after == pc_before || tight_window_hits >= 64) {
                    /* Discard non-progress block and execute via interpreter fallback. */
                    g_jit.stats.fallback_count++;
                    jit_trace_fallback(&g_jit, pc_before,
                                       (tight_window_hits >= 64) ? "tight-loop compiled block" : "non-progress compiled block");
                    jit_cache_remove(&g_jit, block);
                    jit_block_free(&g_jit, block);
                    block_cycles = jit_fallback_interpreter_step(&g_jit, cycles_remaining);
                    if (block_cycles <= 0) {
                        break;
                    }
                    cycles_remaining -= block_cycles;
                    pc = g_jit.cpu->pc;
                    tight_window_hits = 0;
                    continue;
                }
                
                cycles_remaining -= block_cycles;
                pc = pc_after;
            } else {
                /* Compilation failed - try fast-step first, then interpreter fallback. */
                int ran = jit_faststep_enabled() ? jit_try_fast_step(&g_jit, pc, cycles_remaining) : 0;
                if (ran > 0) {
                    g_jit.stats.fast_step_count++;
                    cycles_remaining -= ran;
                    pc = g_jit.current_pc;
                    continue;
                }
                g_jit.stats.fallback_count++;
                {
                    char reason[96];
                    snprintf(reason, sizeof(reason), "compile failed(%s) -> musashi",
                             g_jit_last_compile_fail_reason ? g_jit_last_compile_fail_reason : "unknown");
                    jit_trace_fallback(&g_jit, pc, reason);
                }
                ran = jit_fallback_interpreter_step(&g_jit, cycles_remaining);
                if (ran <= 0) {
                    break;
                }
                cycles_remaining -= ran;
                pc = g_jit.current_pc;
                continue;
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
    printf("Fast-step count:      %u\n", g_jit.stats.fast_step_count);
    printf("Fallback count:       %u\n", g_jit.stats.fallback_count);
    printf("Cache invalidations:  %u\n", g_jit.stats.cache_invalidations);
    printf("Interpret blocks:     %u (max %d)\n", 
           g_jit.stats.interpret_blocks_count, JIT_MAX_INTERPRET_BLOCKS);
    printf("Interpret evictions:  %u\n", g_jit.stats.interpret_blocks_evicted);
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
    g_jit_last_compile_fail_reason = "unknown";
    
    /* Allocate a new block structure */
    block = jit_block_alloc(&g_jit, pc);
    if (block == NULL) {
        g_jit_last_compile_fail_reason = "alloc";
        return NULL;
    }

    if (jit_safe_interp_enabled()) {
        /* Check interpret block limit to prevent unbounded growth */
        if (!jit_cache_should_add_interpret_block(&g_jit)) {
            /* Limit reached - evict an old interpret block */
            jit_cache_evict_oldest_interpret_block(&g_jit);
        }
        
        block->flags |= JIT_BLOCK_INTERPRET_ONLY;
        block->instruction_count = 0;
        block->end_pc = pc;
        block->ends_block = 1;
        jit_cache_insert(&g_jit, block);
        g_jit.stats.interpret_blocks_count++;
        g_jit_last_compile_fail_reason = NULL;
        return block;
    }
    
    /* Translate instructions until we hit a block boundary */
    if (jit_block_translate(&g_jit, block) != 0) {
        /* Translation failed - free the block */
        jit_block_free(&g_jit, block);
        g_jit_last_compile_fail_reason = "translate";
        return NULL;
    }
    
    /* Emit the compiled code */
    if (jit_block_emit(&g_jit, block) != 0) {
        /* Emission failed - free the block */
        jit_block_free(&g_jit, block);
        g_jit_last_compile_fail_reason = "emit";
        return NULL;
    }
    
    /* Insert block into cache */
    jit_cache_insert(&g_jit, block);
    g_jit_last_compile_fail_reason = NULL;
    
    return block;
}
