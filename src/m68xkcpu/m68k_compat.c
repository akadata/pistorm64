// SPDX-License-Identifier: MIT
#include "m68k.h"
#include "m68kcpu.h"

#include <stdio.h>
#include <string.h>

#if !USE_MUSASHI

m68ki_cpu_core m68ki_cpu = {0};

static void m68k_compat_illegal_op(m68ki_cpu_core *state) {
    (void)state;
}

void (*m68ki_instruction_jump_table[0x10000])(m68ki_cpu_core *state);

static void m68k_compat_init_jump_table(void) {
    uint32_t i;
    for (i = 0; i < 0x10000u; ++i) {
        m68ki_instruction_jump_table[i] = m68k_compat_illegal_op;
    }
}

static void m68k_compat_apply_cpu_type(m68ki_cpu_core *state, unsigned int cpu_type) {
    if (state == NULL) {
        state = &m68ki_cpu;
    }

    state->cpu_type = cpu_type;
    switch (cpu_type) {
        case M68K_CPU_TYPE_68000:
        case M68K_CPU_TYPE_68010:
        case M68K_CPU_TYPE_68EC020:
            state->address_mask = 0x00FFFFFFu;
            state->sr_mask = 0xA71Fu;
            state->has_pmmu = 0;
            state->has_fpu = 0;
            break;
        case M68K_CPU_TYPE_68020:
            state->address_mask = 0xFFFFFFFFu;
            state->sr_mask = 0xF71Fu;
            state->has_pmmu = 0;
            state->has_fpu = 0;
            break;
        case M68K_CPU_TYPE_68EC030:
            state->address_mask = 0xFFFFFFFFu;
            state->sr_mask = 0xF71Fu;
            state->has_pmmu = 0;
            state->has_fpu = 1;
            break;
        case M68K_CPU_TYPE_68030:
            state->address_mask = 0xFFFFFFFFu;
            state->sr_mask = 0xF71Fu;
            state->has_pmmu = 1;
            state->has_fpu = 1;
            break;
        case M68K_CPU_TYPE_68EC040:
            state->address_mask = 0xFFFFFFFFu;
            state->sr_mask = 0xF71Fu;
            state->has_pmmu = 0;
            state->has_fpu = 0;
            break;
        case M68K_CPU_TYPE_68LC040:
            state->address_mask = 0xFFFFFFFFu;
            state->sr_mask = 0xF71Fu;
            state->has_pmmu = 1;
            state->has_fpu = 0;
            break;
        case M68K_CPU_TYPE_68040:
            state->address_mask = 0xFFFFFFFFu;
            state->sr_mask = 0xF71Fu;
            state->has_pmmu = 1;
            state->has_fpu = 1;
            break;
        default:
            state->address_mask = 0xFFFFFFFFu;
            state->sr_mask = 0xF71Fu;
            state->has_pmmu = 0;
            state->has_fpu = 0;
            break;
    }
}

void m68k_set_cpu_type(struct m68ki_cpu_core *state, unsigned int cpu_type) {
    m68k_compat_apply_cpu_type(state, cpu_type);
}

void m68k_init(void) {
    memset(&m68ki_cpu, 0, sizeof(m68ki_cpu));
    m68k_compat_init_jump_table();
    m68k_compat_apply_cpu_type(&m68ki_cpu, M68K_CPU_TYPE_68000);
    m68ki_cpu.not_z_flag = 1;
}

void m68k_set_illg_instr_callback(m68ki_cpu_core *state, int (*callback)(int)) {
    if (state == NULL) {
        state = &m68ki_cpu;
    }
    state->illg_instr_callback = callback;
}

void m68k_set_fc_callback(m68ki_cpu_core *state, void (*callback)(unsigned int new_fc)) {
    if (state == NULL) {
        state = &m68ki_cpu;
    }
    state->set_fc_callback = callback;
}

void m68k_set_instr_hook_callback(m68ki_cpu_core *state, void (*callback)(unsigned int pc)) {
    if (state == NULL) {
        state = &m68ki_cpu;
    }
    state->instr_hook_callback = callback;
}

void m68k_pulse_reset(m68ki_cpu_core *state) {
    uint32_t initial_sp;
    uint32_t initial_pc;

    if (state == NULL) {
        state = &m68ki_cpu;
    }

    state->pmmu_enabled = 0;
    state->mmu_tc = 0;
    state->mmu_tt0 = 0;
    state->mmu_tt1 = 0;
    state->stopped = 0;
    state->remaining_cycles = 0;
    state->initial_cycles = 0;
    state->t1_flag = 0;
    state->t0_flag = 0;
    state->int_mask = 0x0700;
    state->int_level = 0;
    state->virq_state = 0;
    state->nmi_pending = 0;
    state->vbr = 0;
    state->s_flag = SFLAG_SET;
    state->m_flag = MFLAG_CLEAR;
    state->x_flag = 0;
    state->n_flag = 0;
    state->not_z_flag = 1;
    state->v_flag = 0;
    state->c_flag = 0;
    state->ppc = 0;

    initial_sp = m68k_read_memory_32(0);
    initial_pc = m68k_read_memory_32(4);
    state->dar[15] = initial_sp;
    state->sp[0] = initial_sp;
    state->sp[4] = initial_sp;
    state->sp[6] = initial_sp;
    state->pc = initial_pc;
    state->ppc = initial_pc;
}

int m68k_execute(struct m68ki_cpu_core *state, int num_cycles) {
    (void)state;
    (void)num_cycles;
    return 0;
}

int m68k_cycles_run(void) {
    return m68ki_cpu.initial_cycles - m68ki_cpu.remaining_cycles;
}

int m68k_cycles_remaining(void) {
    return m68ki_cpu.remaining_cycles;
}

void m68k_modify_timeslice(int cycles) {
    m68ki_cpu.remaining_cycles += cycles;
}

void m68k_end_timeslice(void) {
    m68ki_cpu.remaining_cycles = 0;
}

int m68k_cycles_run_state(struct m68ki_cpu_core *state) {
    if (state == NULL) {
        state = &m68ki_cpu;
    }
    return state->initial_cycles - state->remaining_cycles;
}

int m68k_cycles_remaining_state(struct m68ki_cpu_core *state) {
    if (state == NULL) {
        state = &m68ki_cpu;
    }
    return state->remaining_cycles;
}

void m68k_modify_timeslice_state(struct m68ki_cpu_core *state, int cycles) {
    if (state == NULL) {
        state = &m68ki_cpu;
    }
    state->remaining_cycles += cycles;
}

void m68k_end_timeslice_state(struct m68ki_cpu_core *state) {
    if (state == NULL) {
        state = &m68ki_cpu;
    }
    state->remaining_cycles = 0;
}

void m68k_set_irq_state(m68ki_cpu_core *state, unsigned int int_level) {
    uint old_level;
    if (state == NULL) {
        state = &m68ki_cpu;
    }
    old_level = state->int_level;
    state->int_level = (int_level << 8);
    if (old_level != 0x0700u && state->int_level == 0x0700u) {
        state->nmi_pending = 1;
    }
}

void m68k_set_irq(unsigned int int_level) {
    m68k_set_irq_state(&m68ki_cpu, int_level);
}

void m68k_set_virq_state(m68ki_cpu_core *state, unsigned int level, unsigned int active) {
    uint virq_state;
    uint blevel;

    if (state == NULL) {
        state = &m68ki_cpu;
    }
    virq_state = state->virq_state;
    if (active) {
        virq_state |= (1u << level);
    } else {
        virq_state &= ~(1u << level);
    }
    state->virq_state = virq_state;

    for (blevel = 7u; blevel > 0u; --blevel) {
        if (virq_state & (1u << blevel)) {
            break;
        }
    }
    m68k_set_irq_state(state, blevel);
}

unsigned int m68k_get_virq_state(m68ki_cpu_core *state, unsigned int level) {
    if (state == NULL) {
        state = &m68ki_cpu;
    }
    return (state->virq_state & (1u << level)) ? 1u : 0u;
}

void m68k_set_virq(unsigned int level, unsigned int active) {
    m68k_set_virq_state(&m68ki_cpu, level, active);
}

unsigned int m68k_get_virq(unsigned int level) {
    return m68k_get_virq_state(&m68ki_cpu, level);
}

unsigned int m68k_get_reg(void *context, m68k_register_t regnum) {
    m68ki_cpu_core *cpu = (context != NULL) ? (m68ki_cpu_core *)context : &m68ki_cpu;

    switch (regnum) {
        case M68K_REG_D0: return cpu->dar[0];
        case M68K_REG_D1: return cpu->dar[1];
        case M68K_REG_D2: return cpu->dar[2];
        case M68K_REG_D3: return cpu->dar[3];
        case M68K_REG_D4: return cpu->dar[4];
        case M68K_REG_D5: return cpu->dar[5];
        case M68K_REG_D6: return cpu->dar[6];
        case M68K_REG_D7: return cpu->dar[7];
        case M68K_REG_A0: return cpu->dar[8];
        case M68K_REG_A1: return cpu->dar[9];
        case M68K_REG_A2: return cpu->dar[10];
        case M68K_REG_A3: return cpu->dar[11];
        case M68K_REG_A4: return cpu->dar[12];
        case M68K_REG_A5: return cpu->dar[13];
        case M68K_REG_A6: return cpu->dar[14];
        case M68K_REG_A7: return cpu->dar[15];
        case M68K_REG_PC: return cpu->pc;
        case M68K_REG_SR:
            return cpu->t1_flag | cpu->t0_flag | (cpu->s_flag << 11) |
                   (cpu->m_flag << 11) | cpu->int_mask |
                   ((cpu->x_flag & XFLAG_SET) >> 4) |
                   ((cpu->n_flag & NFLAG_SET) >> 4) |
                   ((!cpu->not_z_flag) << 2) |
                   ((cpu->v_flag & VFLAG_SET) >> 6) |
                   ((cpu->c_flag & CFLAG_SET) >> 8);
        case M68K_REG_SP: return cpu->dar[15];
        case M68K_REG_USP: return cpu->s_flag ? cpu->sp[0] : cpu->dar[15];
        case M68K_REG_ISP: return (cpu->s_flag && !cpu->m_flag) ? cpu->dar[15] : cpu->sp[4];
        case M68K_REG_MSP: return (cpu->s_flag && cpu->m_flag) ? cpu->dar[15] : cpu->sp[6];
        case M68K_REG_SFC: return cpu->sfc;
        case M68K_REG_DFC: return cpu->dfc;
        case M68K_REG_VBR: return cpu->vbr;
        case M68K_REG_CACR: return cpu->cacr;
        case M68K_REG_CAAR: return cpu->caar;
        case M68K_REG_PREF_ADDR: return cpu->pref_addr;
        case M68K_REG_PREF_DATA: return cpu->pref_data;
        case M68K_REG_PPC: return cpu->ppc;
        case M68K_REG_IR: return cpu->ir;
        case M68K_REG_CPU_TYPE: return cpu->cpu_type;
        default: return 0;
    }
}

unsigned int m68k_disassemble(char *str_buff, unsigned int pc, unsigned int cpu_type) {
    uint16_t opcode;
    (void)cpu_type;
    opcode = (uint16_t)m68k_read_memory_16(pc);
    if (str_buff != NULL) {
        snprintf(str_buff, 64, "dc.w $%04X", opcode);
    }
    return 2;
}

#endif
