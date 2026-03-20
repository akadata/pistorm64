#include "jit_emit_aarch64.h"
#include "musashi/m68kcpu.h"
#include <stddef.h>

#define JIT_CPU_OFF(member) ((int)offsetof(struct m68ki_cpu_core, member))
#define JIT_CPU_OFF_DAR(index) ((int)(offsetof(struct m68ki_cpu_core, dar) + ((index) * sizeof(((struct m68ki_cpu_core *)0)->dar[0]))))

extern unsigned int m68k_read_memory_8(unsigned int address);
extern unsigned int m68k_read_memory_16(unsigned int address);
extern unsigned int m68k_read_memory_32(unsigned int address);
extern void m68k_write_memory_8(unsigned int address, unsigned int value);
extern void m68k_write_memory_16(unsigned int address, unsigned int value);
extern void m68k_write_memory_32(unsigned int address, unsigned int value);
extern struct m68ki_cpu_core m68ki_cpu;

void jit_emit_init(jit_emit_context_t *ctx, uint8_t *buffer, size_t size)
{
    ctx->buffer = buffer;
    ctx->size = size;
    ctx->offset = 0;
    ctx->error = false;
}

void jit_emit_byte(jit_emit_context_t *ctx, uint8_t byte)
{
    if (ctx->offset >= ctx->size) {
        ctx->error = true;
        return;
    }
    ctx->buffer[ctx->offset++] = byte;
}

void jit_emit_word(jit_emit_context_t *ctx, uint16_t word)
{
    jit_emit_byte(ctx, word & 0xFF);
    jit_emit_byte(ctx, (word >> 8) & 0xFF);
}

void jit_emit_dword(jit_emit_context_t *ctx, uint32_t dword)
{
    jit_emit_word(ctx, dword & 0xFFFF);
    jit_emit_word(ctx, (dword >> 16) & 0xFFFF);
}

void jit_emit_mov64(jit_emit_context_t *ctx, uint8_t rd, uint64_t imm)
{
    if (imm == 0) {
        jit_emit_dword(ctx, AARCH64_EOR(rd, rd, rd));
        return;
    }

    jit_emit_dword(ctx, AARCH64_MOVZ(rd, imm & 0xFFFF, 0));

    for (int shift = 16; shift < 64; shift += 16) {
        uint16_t chunk = (imm >> shift) & 0xFFFF;
        if (chunk != 0) {
            jit_emit_dword(ctx, AARCH64_MOVK(rd, chunk, shift / 16));
        }
    }
}

void jit_emit_load_cpu_reg(jit_emit_context_t *ctx, uint8_t rt, int offset)
{
    jit_emit_dword(ctx, AARCH64_LDR_W(rt, AARCH64_CPU_PTR, offset));
}

void jit_emit_store_cpu_reg(jit_emit_context_t *ctx, uint8_t rt, int offset)
{
    jit_emit_dword(ctx, AARCH64_STR_W(rt, AARCH64_CPU_PTR, offset));
}

void jit_emit_load_dn(jit_emit_context_t *ctx, uint8_t rt, int dn)
{
    jit_emit_load_cpu_reg(ctx, rt, JIT_CPU_OFF_DAR(dn & 0xF));
}

void jit_emit_store_dn(jit_emit_context_t *ctx, uint8_t rt, int dn)
{
    jit_emit_store_cpu_reg(ctx, rt, JIT_CPU_OFF_DAR(dn & 0xF));
}

void jit_emit_load_an(jit_emit_context_t *ctx, uint8_t rt, int an)
{
    jit_emit_load_cpu_reg(ctx, rt, JIT_CPU_OFF_DAR(8 + (an & 0x7)));
}

void jit_emit_store_an(jit_emit_context_t *ctx, uint8_t rt, int an)
{
    jit_emit_store_cpu_reg(ctx, rt, JIT_CPU_OFF_DAR(8 + (an & 0x7)));
}

void jit_emit_load_pc(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_dword(ctx, AARCH64_LDR_W(rt, AARCH64_CPU_PTR, JIT_CPU_OFF(pc)));
}

void jit_emit_store_pc(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_dword(ctx, AARCH64_STR_W(rt, AARCH64_CPU_PTR, JIT_CPU_OFF(pc)));
}

void jit_emit_store_itt0(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_store_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_itt0));
}

void jit_emit_load_itt0(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_load_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_itt0));
}

void jit_emit_store_itt1(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_store_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_itt1));
}

void jit_emit_load_itt1(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_load_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_itt1));
}

void jit_emit_store_dtt0(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_store_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_dtt0));
}

void jit_emit_load_dtt0(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_load_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_dtt0));
}

void jit_emit_store_dtt1(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_store_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_dtt1));
}

void jit_emit_load_dtt1(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_load_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_dtt1));
}

void jit_emit_store_tc(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_store_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_tc));
}

void jit_emit_load_tc(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_load_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_tc));
}

void jit_emit_store_acr(jit_emit_context_t *ctx, uint8_t rt, int acr_num)
{
    jit_emit_store_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_acr0) + (acr_num * (int)sizeof(((struct m68ki_cpu_core *)0)->mmu_acr0)));
}

void jit_emit_load_acr(jit_emit_context_t *ctx, uint8_t rt, int acr_num)
{
    jit_emit_load_cpu_reg(ctx, rt, JIT_CPU_OFF(mmu_acr0) + (acr_num * (int)sizeof(((struct m68ki_cpu_core *)0)->mmu_acr0)));
}

void jit_emit_load_sr(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_dword(ctx, AARCH64_LDRH(rt, AARCH64_CPU_PTR, 192));
}

void jit_emit_store_sr(jit_emit_context_t *ctx, uint8_t rt)
{
    jit_emit_dword(ctx, AARCH64_STRH(rt, AARCH64_CPU_PTR, 192));
}

void jit_emit_prologue(jit_emit_context_t *ctx)
{
    (void)ctx;
}

void jit_emit_epilogue(jit_emit_context_t *ctx)
{
    /* Return a positive cycle count so jit_execute() keeps block execution on JIT path. */
    jit_emit_mov64(ctx, AARCH64_R0, 1);
    jit_emit_dword(ctx, AARCH64_RET);
}

void jit_emit_inc_pc(jit_emit_context_t *ctx, int instr_size)
{
    jit_emit_dword(ctx, AARCH64_LDR_W(AARCH64_R0, AARCH64_CPU_PTR, JIT_CPU_OFF(pc)));
    jit_emit_mov64(ctx, AARCH64_R1, instr_size);
    jit_emit_dword(ctx, AARCH64_ADD(AARCH64_R0, AARCH64_R0, AARCH64_R1));
    jit_emit_dword(ctx, AARCH64_STR_W(AARCH64_R0, AARCH64_CPU_PTR, JIT_CPU_OFF(pc)));
}

void jit_emit_unimplemented(jit_emit_context_t *ctx, uint16_t opcode)
{
    ctx->error = true;
    (void)opcode;
}

static void jit_emit_call_read_common(jit_emit_context_t *ctx,
                                      uint8_t dst_reg,
                                      uint8_t addr_reg,
                                      uintptr_t fn_addr)
{
    /* Place address argument in X0/W0. */
    if (addr_reg != AARCH64_R0) {
        jit_emit_dword(ctx, AARCH64_ORR(AARCH64_R0, addr_reg, addr_reg));
    }

    /* Indirect call through a scratch register. */
    jit_emit_mov64(ctx, AARCH64_R6, (uint64_t)fn_addr);
    jit_emit_dword(ctx, AARCH64_BLR(AARCH64_R6));

    /* Re-anchor CPU pointer register after C helper calls. */
    jit_emit_mov64(ctx, AARCH64_CPU_PTR, (uint64_t)(uintptr_t)&m68ki_cpu);

    /* Move return value from X0/W0 if needed. */
    if (dst_reg != AARCH64_R0) {
        jit_emit_dword(ctx, AARCH64_ORR(dst_reg, AARCH64_R0, AARCH64_R0));
    }
}

void jit_emit_call_read8(jit_emit_context_t *ctx, uint8_t dst_reg, uint8_t addr_reg)
{
    jit_emit_call_read_common(ctx, dst_reg, addr_reg, (uintptr_t)&m68k_read_memory_8);
}

void jit_emit_call_read16(jit_emit_context_t *ctx, uint8_t dst_reg, uint8_t addr_reg)
{
    jit_emit_call_read_common(ctx, dst_reg, addr_reg, (uintptr_t)&m68k_read_memory_16);
}

void jit_emit_call_read32(jit_emit_context_t *ctx, uint8_t dst_reg, uint8_t addr_reg)
{
    jit_emit_call_read_common(ctx, dst_reg, addr_reg, (uintptr_t)&m68k_read_memory_32);
}

static void jit_emit_call_write_common(jit_emit_context_t *ctx,
                                       uint8_t value_reg,
                                       uint8_t addr_reg,
                                       uintptr_t fn_addr)
{
    /* x0/w0 = address, x1/w1 = value */
    if (addr_reg != AARCH64_R0) {
        jit_emit_dword(ctx, AARCH64_ORR(AARCH64_R0, addr_reg, addr_reg));
    }
    if (value_reg != AARCH64_R1) {
        jit_emit_dword(ctx, AARCH64_ORR(AARCH64_R1, value_reg, value_reg));
    }

    jit_emit_mov64(ctx, AARCH64_R6, (uint64_t)fn_addr);
    jit_emit_dword(ctx, AARCH64_BLR(AARCH64_R6));

    /* Re-anchor CPU pointer register after C helper calls. */
    jit_emit_mov64(ctx, AARCH64_CPU_PTR, (uint64_t)(uintptr_t)&m68ki_cpu);
}

void jit_emit_call_write8(jit_emit_context_t *ctx, uint8_t value_reg, uint8_t addr_reg)
{
    jit_emit_call_write_common(ctx, value_reg, addr_reg, (uintptr_t)&m68k_write_memory_8);
}

void jit_emit_call_write16(jit_emit_context_t *ctx, uint8_t value_reg, uint8_t addr_reg)
{
    jit_emit_call_write_common(ctx, value_reg, addr_reg, (uintptr_t)&m68k_write_memory_16);
}

void jit_emit_call_write32(jit_emit_context_t *ctx, uint8_t value_reg, uint8_t addr_reg)
{
    jit_emit_call_write_common(ctx, value_reg, addr_reg, (uintptr_t)&m68k_write_memory_32);
}

void jit_emit_store_nzcv_flags(jit_emit_context_t *ctx, int invert_carry)
{
    const int off_n = JIT_CPU_OFF(n_flag);
    const int off_notz = JIT_CPU_OFF(not_z_flag);
    const int off_v = JIT_CPU_OFF(v_flag);
    const int off_c = JIT_CPU_OFF(c_flag);
    const int off_x = JIT_CPU_OFF(x_flag);

    /* R3 <= NZCV (bits 31..28 are N,Z,C,V). */
    jit_emit_dword(ctx, AARCH64_MRS_NZCV(AARCH64_R3));
    jit_emit_mov64(ctx, AARCH64_R7, 1);

    /* n_flag = (N << 7) */
    jit_emit_dword(ctx, AARCH64_LSR(AARCH64_R4, AARCH64_R3, 31));
    jit_emit_dword(ctx, AARCH64_AND_W(AARCH64_R4, AARCH64_R4, AARCH64_R7));
    jit_emit_dword(ctx, AARCH64_LSL(AARCH64_R4, AARCH64_R4, 7));
    jit_emit_store_cpu_reg(ctx, AARCH64_R4, off_n);

    /* not_z_flag = !Z (0 when equal, 1 when not equal). */
    jit_emit_dword(ctx, AARCH64_LSR(AARCH64_R4, AARCH64_R3, 30));
    jit_emit_dword(ctx, AARCH64_AND_W(AARCH64_R4, AARCH64_R4, AARCH64_R7));
    jit_emit_dword(ctx, AARCH64_EOR_W(AARCH64_R4, AARCH64_R4, AARCH64_R7));
    jit_emit_store_cpu_reg(ctx, AARCH64_R4, off_notz);

    /* v_flag = (V << 7) */
    jit_emit_dword(ctx, AARCH64_LSR(AARCH64_R4, AARCH64_R3, 28));
    jit_emit_dword(ctx, AARCH64_AND_W(AARCH64_R4, AARCH64_R4, AARCH64_R7));
    jit_emit_dword(ctx, AARCH64_LSL(AARCH64_R4, AARCH64_R4, 7));
    jit_emit_store_cpu_reg(ctx, AARCH64_R4, off_v);

    /* c_flag/x_flag = (C << 8), with optional inversion for SUB/CMP semantics. */
    jit_emit_dword(ctx, AARCH64_LSR(AARCH64_R4, AARCH64_R3, 29));
    jit_emit_dword(ctx, AARCH64_AND_W(AARCH64_R4, AARCH64_R4, AARCH64_R7));
    if (invert_carry) {
        jit_emit_dword(ctx, AARCH64_EOR_W(AARCH64_R4, AARCH64_R4, AARCH64_R7));
    }
    jit_emit_dword(ctx, AARCH64_LSL(AARCH64_R4, AARCH64_R4, 8));
    jit_emit_store_cpu_reg(ctx, AARCH64_R4, off_c);
    jit_emit_store_cpu_reg(ctx, AARCH64_R4, off_x);
}
