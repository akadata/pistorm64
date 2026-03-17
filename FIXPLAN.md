# Fix Plan

Date: 2026-03-16  
Based on: [REVIEW.md](/home/smalley/pistorm64/REVIEW.md)

## Scope

This plan fixes two review findings only:

1. P1: 68040 MOVEC legality regression in Musashi control-register handling.
2. P2: Unbounded growth of interpret-only JIT block cache in safe-interpret mode.

No unrelated feature work should be included while executing this plan.

## Execution Order

Fix in this order:

1. P1 first (functional correctness / boot blocker).
2. P2 second (runtime stability / memory growth).

## P1 Plan: Reinstate 68040 MOVEC Register Legality

Target file:

- [src/musashi/m68kcpu.h](/home/smalley/pistorm64/src/musashi/m68kcpu.h)

Implementation steps:

1. Restore correct legality gates in `m68ki_movec_reg_legal` for 68040 PMMU control registers:
   - `TC`, `ITT0`, `ITT1`, `DTT0`, `DTT1`, `MMUSR`, `URP`, `SRP`.
2. Ensure logic matches CPU model capabilities and does not grant these registers to unsupported models.
3. Keep existing behavior for non-040 models unchanged.

Validation:

1. Build emulator (`make -j4 emulator`).
2. Run 68040 boot path that previously faulted on MOVEC.
3. Confirm no new illegal traps from those MOVEC register accesses.
4. Re-run existing 68040 reference tests (`make musashi-ref-tests-68040` and PMMU variant if used locally).

Exit criteria:

- 68040 boot no longer fails due to MOVEC legality regression.
- No regression in existing 68040 test baseline.

## P2 Plan: Bound Interpret-Only JIT Cache Growth

Target file:

- [src/m68xkcpu/jit.c](/home/smalley/pistorm64/src/m68xkcpu/jit.c)

Implementation options (pick one, keep minimal):

1. Do not insert interpret-only blocks into global cache in safe mode.
2. Or keep insertion but add strict bound/eviction policy tied to code-cache or explicit block-count cap.

Preferred approach:

- Option 1 (no insertion in safe-interpret mode), because it is simplest, lowest-risk, and directly removes leak path.

Implementation steps:

1. In safe-interpret path (`jit_compile_block`), return an ephemeral interpret-only block or bypass block allocation/insertion entirely.
2. Ensure no stale references remain in hash table for this path.
3. If ephemeral block is used, free it deterministically after fallback execution.

Validation:

1. Build emulator (`make -j4 emulator`).
2. Run long safe-interpret session:
   - `PISTORM_M68XK_SAFE_INTERP=1`
   - `PISTORM_M68XK_EXEC=1`
3. Monitor memory usage over time (RSS) and confirm it stabilizes instead of growing unbounded.
4. Confirm fallback execution still works and does not regress startup behavior.

Exit criteria:

- No unbounded memory growth from interpret-only cache path in long runs.
- No crash/regression from fallback execution path.

## Test Matrix (Post-Fix)

Run the following minimum matrix:

1. 68040 + PMMU expected workload (boot path that previously failed).
2. 68000 + JIT safe interpret mode (`SAFE_INTERP=1`, `FASTSTEP=0`).
3. Musashi-only control run for same 68000 config.
4. Existing reference test suites currently used in branch.

## Risks

1. P1 risk:
   - Over-permissive legality gate could incorrectly enable MOVEC registers on lower CPU models.
2. P2 risk:
   - Changing safe-mode block handling could reduce performance in safe mode (acceptable for correctness/stability).
   - Incorrect lifetime management for ephemeral blocks could cause use-after-free if not isolated.

## Rollback Strategy

If a fix regresses boot/tests:

1. Revert the specific commit for that finding only.
2. Keep the other finding fix if it remains green.
3. Re-run minimal matrix to confirm restored baseline.

## Deliverables

1. One commit for P1.
2. One commit for P2.
3. Updated `REVIEW.md` status note (resolved/open) after validation.
