# Review Findings

Date: 2026-03-16
Source: `/review` output

## Summary

The current patchset has two high-priority issues:

1. A regression in 68040 MOVEC legality handling that can break 68040/PMMU boot paths.
2. An unbounded interpret-only JIT cache growth path in safe-interpret mode.

## Findings

### [P1] Reinstate 68040 MOVEC register legality

- File: [src/musashi/m68kcpu.h](/home/smalley/pistorm64/src/musashi/m68kcpu.h)
- Location: lines 1191-1200
- Issue:
  - `m68ki_movec_reg_legal` now routes `TC/ITT0/ITT1/DTT0/DTT1/MMUSR/URP/SRP` to `default`.
  - These control registers are therefore treated as illegal for all CPU models.
- Impact:
  - On 68040 configurations (especially PMMU-enabled), ROM/OS `MOVEC` usage traps as illegal instruction.
  - Can fail early boot.

### [P2] Prevent unbounded growth of interpret-only block cache

- File: [src/m68xkcpu/jit.c](/home/smalley/pistorm64/src/m68xkcpu/jit.c)
- Location: lines 712-718
- Issue:
  - In safe-interpret mode, each cache miss allocates a `jit_block_t` and inserts it into hash table.
  - Entries are not bounded by code-cache size and there is no eviction path linked to insertion.
- Impact:
  - With `PISTORM_M68XK_EXEC=1` (default safe mode), long-running workloads that touch many PCs can keep accumulating entries.
  - Risks host memory exhaustion.

## Suggested Resolution Order

1. Fix P1 first (restores 68040 boot correctness).
2. Fix P2 next (stability/memory safety for long runs).
