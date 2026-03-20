#include "jit_emit_aarch64.h"
#include "musashi/m68kcpu.h"
#include <stddef.h>

#define JIT_CPU_OFF(member) ((int)offsetof(struct m68ki_cpu_core, member))
#define JIT_CPU_OFF_DAR(index) ((int)(offsetof(struct m68ki_cpu_core, dar) + ((index) * sizeof(((struct m68ki_cpu_core *)0)->dar[0]))))

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
    jit_emit_mov64(ctx, AARCH64_R0, 0);
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
    jit_emit_dword(ctx, AARCH64_NOP);
    (void)opcode;
}
