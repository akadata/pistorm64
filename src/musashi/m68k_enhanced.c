// SPDX-License-Identifier: MIT

#include "m68k_enhanced.h"

#include "m68kcpu.h"
#include "m68kops.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))


// MOVEC2 (0x4E7A) — privileged, 68020+
// On real silicon this is MOVEC between Dn/An and control registers.
// First implementation: privileged NOP that consumes the extension word.

static void op_movec2_4e7a(m68ki_cpu_core *state) {
    if (!FLAG_S) {
        m68ki_exception_privilege_violation(state);
        return;
    }

    /* MOVEC uses an extension word to encode the control reg + source/dest reg.
     * Consume it so PC stays in sync, even if semantics are a NOP for now.
     */
    (void)m68ki_read_imm_16(state);

    USE_CYCLES(6);  /* ballpark; adjust later if needed */
}


/*
 * Minimal 68040+ system op handlers based on table68k:
 *
 *  - CINVL, CINVP, CINVA
 *  - CPUSHL, CPUSHP, CPUSHA
 *  - PFLUSHN, PTESTW, PTESTR, PLPAW, PLPAR
 *
 * First pass: treat them as privileged NOPs that still consume some cycles.
 * That prevents illegal-instruction traps while 040.library / MMU tools probe.
 */

static void op_priv_nop_4cy(m68ki_cpu_core *state)
{
    if (!FLAG_S) {
        m68ki_exception_privilege_violation(state);
        return;
    }

    USE_CYCLES(4);
}

/* CINV/CPUSH patterns from table68k:
 *
 * 1111 0100 pp00 1rrr  CINVL  #p,Ar
 * 1111 0100 pp01 0rrr  CINVP  #p,Ar
 * 1111 0100 pp01 1rrr  CINVA  #p
 * 1111 0100 pp10 1rrr  CPUSHL #p,Ar
 * 1111 0100 pp11 0rrr  CPUSHP #p,Ar
 * 1111 0100 pp11 1rrr  CPUSHA #p
 *
 * 'pp' is cache selection (bits 7–6), 'r' is An (bits 2–0).
 */

#define OP_MASK_CINV_CPUSH 0xFF38

static const uint16_t cinv_cpush_bases[] = {
    0xF408, /* CINVL  base: 1111 0100 0000 1000 */
    0xF410, /* CINVP  base: 1111 0100 0001 0000 */
    0xF418, /* CINVA  base: 1111 0100 0001 1000 */
    0xF428, /* CPUSHL base: 1111 0100 0010 1000 */
    0xF430, /* CPUSHP base: 1111 0100 0011 0000 */
    0xF438, /* CPUSHA base: 1111 0100 0011 1000 */
};

static void install_cinv_cpush(void) {
    for (unsigned bi = 0; bi < ARRAY_LEN(cinv_cpush_bases); bi++) {
        uint16_t base = cinv_cpush_bases[bi];

        for (unsigned pp = 0; pp < 4; pp++) {
            for (unsigned r = 0; r < 8; r++) {
                uint16_t op = (uint16_t)(base | (pp << 6) | r);
                m68ki_instruction_jump_table[op] = op_priv_nop_4cy;
            }
        }
    }
}

/* PMMU-ish F5 group from table68k:
 *
 * 1111 0101 0000 0rrr  PFLUSHN Ara
 * 1111 0101 0100 1rrr  PTESTW  Ara
 * 1111 0101 0110 1rrr  PTESTR  Ara
 * 1111 0101 1000 1rrr  PLPAW   Ara
 * 1111 0101 1100 1rrr  PLPAR   Ara
 */

#define OP_MASK_F5_GROUP 0xFFF8

static const uint16_t pmmu_bases[] = {
    0xF500, /* PFLUSHN */
    0xF548, /* PTESTW */
    0xF568, /* PTESTR */
    0xF588, /* PLPAW  */
    0xF5C8, /* PLPAR  */
};

static void install_pmmu_misc(void) { 
    for (unsigned bi = 0; bi < ARRAY_LEN(pmmu_bases); bi++) {
        uint16_t base = pmmu_bases[bi];

        for (unsigned r = 0; r < 8; r++) {
            uint16_t op = (uint16_t)(base | r);
            m68ki_instruction_jump_table[op] = op_priv_nop_4cy;
        }
    }
}

void m68k_enhanced_install(void) {
    /* Optional: gate on CPU type being 68040/68060 in your fork. */
    install_cinv_cpush();
    install_pmmu_misc();

    /* MOVEC2 pseudo-op (0x4E7A) */
    m68ki_instruction_jump_table[0x4E7A] = op_movec2_4e7a;
}
