// SPDX-License-Identifier: MIT

// selftest.c
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdalign.h>
#include <stddef.h>

#include "m68kcpu.h"

static void check_align(const char *name, uintptr_t p)
{
    if (p & 0xF) {
        fprintf(stderr,
            "[SELFTEST] FAIL: %s not 16-byte aligned (%p)\n",
            name, (void *)p);
        abort();
    }
}

static void check_align_n(const char *name, uintptr_t p, size_t align)
{
    // align must be power-of-two.
    if (align == 0 || (align & (align - 1)) != 0) {
        fprintf(stderr, "[SELFTEST] FAIL: invalid alignment for %s: %zu\n", name, align);
        abort();
    }

    if ((p & (align - 1)) != 0) {
        fprintf(stderr,
            "[SELFTEST] FAIL: %s not %zu-byte aligned (%p)\n",
            name, align, (void *)p);
        abort();
    }
}

static void check_offset_align(const char *name, size_t off, size_t align)
{
    if (align == 0 || (align & (align - 1)) != 0) {
        fprintf(stderr, "[SELFTEST] FAIL: invalid alignment for %s: %zu\n", name, align);
        abort();
    }

    if ((off & (align - 1)) != 0) {
        fprintf(stderr,
            "[SELFTEST] FAIL: %s offset not %zu-byte aligned (offsetof=%zu)\n",
            name, align, off);
        abort();
    }
}

void pistorm_selftest_alignment(void)
{
    m68ki_cpu_core stack_state = {0};
    m68ki_cpu_core *heap_state = malloc(sizeof(*heap_state));

    if (!heap_state) {
        fprintf(stderr, "[SELFTEST] malloc failed\n");
        abort();
    }

    // Compile-time / layout facts, verified at runtime too for clarity.
    check_align_n("_Alignof(m68ki_cpu_core)", (uintptr_t)_Alignof(m68ki_cpu_core), 1); // sanity only
    check_offset_align("offsetof(m68ki_cpu_core, fpr)", offsetof(m68ki_cpu_core, fpr), 16);

    // Prove the *object* alignment in memory (this is the important part for malloc/stack/global).
    check_align("global cpu", (uintptr_t)&m68ki_cpu);
    check_align("stack  cpu", (uintptr_t)&stack_state);
    check_align("heap   cpu", (uintptr_t)heap_state);

    // Prove the member alignment too (should follow from above + offsetof assertion).
    check_align("global fpr", (uintptr_t)&m68ki_cpu.fpr[0]);
    check_align("stack  fpr", (uintptr_t)&stack_state.fpr[0]);
    check_align("heap   fpr", (uintptr_t)&heap_state->fpr[0]);

    free(heap_state);

    fprintf(stderr,
        "[SELFTEST] alignment OK (cpu + fpr) | alignof(cpu)=%zu offsetof(fpr)=%zu sizeof(cpu)=%zu\n",
        (size_t)_Alignof(m68ki_cpu_core),
        (size_t)offsetof(m68ki_cpu_core, fpr),
        (size_t)sizeof(m68ki_cpu_core));
}
