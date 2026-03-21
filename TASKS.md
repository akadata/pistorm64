# Codex Split Task Files for 68040 JIT Recovery

Use these as separate task documents for Shell Codex. Run them one at a time. Do not merge stages. Do not skip checkpoints.

---

# TASK-01-cpu-boundary.md

## Objective

Lock execution ownership so only 68040 enters the AArch64 JIT path. All 68030 and below must remain on Musashi only.

## Scope

* `src/emulator.c`
* `src/config_file/*`
* `src/m68xkcpu/jit.c`
* CPU/backend selection code

## Rules

* No feature work in this task
* No emitter work in this task
* No translator work in this task
* No MMU/FPU work in this task

## Required outcome

* 68000, 68010, 68020, 68030: Musashi only
* 68040: eligible for JIT
* Logs must clearly state which core is active
* No accidental mixed execution path

## Steps

1. Audit CPU selection and backend activation path.
2. Identify every place where `m68xkcpu` may be selected.
3. Force a single rule: JIT is permitted only for 68040.
4. Ensure lower CPU models never enter JIT even when JIT flags are set.
5. Ensure fallback logging clearly states when Musashi is selected because CPU < 68040.

## Checkpoints

* Search for all JIT enable decisions.
* Search for all CPU model checks.
* Confirm no hidden fast path bypass exists.

## Commit message

`jit: restrict AArch64 JIT execution to 68040 only`

---

# TASK-02-emitter-validation.md

## Objective

Repair and validate the AArch64 emitter so emitted host instructions are always legal and correctly encoded.

## Scope

* `src/m68xkcpu/jit_emit_aarch64.h`
* `src/m68xkcpu/jit_emit_aarch64.c`

## Rules

* Do not change translator structure unless required by emitter correctness.
* Do not touch Musashi.
* Do not add new instruction families yet.

## Required outcome

* No SIGILL from emitted host code
* Shift/bitfield/macros use valid encodings
* W/X register width usage is correct
* Immediate encodings are correct

## Steps

1. Audit all emitter macros and helpers.
2. Validate:

   * `LSL`
   * `LSR`
   * `ASR`
   * move immediate helpers
   * load/store helpers
   * arithmetic helpers
   * return/branch helpers
3. Confirm every helper matches actual AArch64 encoding rules.
4. Remove or rewrite invalid macro forms.
5. Keep emitter as single source of truth.

## Checkpoints

* Inspect host dump for each changed helper.
* Confirm no invalid opcodes remain in AA64 dump.
* Confirm 32-bit operations use W forms where intended.

## Commit message

`jit: fix and validate AArch64 emitter encodings`

---

# TASK-03-block-execution.md

## Objective

Fix non-progress execution and repeated looping at early boot PCs.

## Scope

* `src/m68xkcpu/jit.c`
* `src/m68xkcpu/jit_block.c`
* cache lookup/execute loop

## Current observed failure

Repeated cycling around:

* `0x00F800E2`
* `0x00F800E4`
* `0x00F800E6`
* `0x00F800E8`

## Rules

* Do not broaden scope into MMU/FPU.
* Do not add new instruction support just to mask loop bugs.

## Required outcome

* Block execution makes forward progress
* PC writeback is correct
* Non-progress is detected and handled correctly
* No fake execution loops

## Steps

1. Audit `jit_execute()` loop.
2. Audit `jit_block_execute()` entry/exit behavior.
3. Confirm source of truth for PC before and after block execution.
4. Confirm block exit semantics are consistent.
5. Confirm cycle accounting does not cause re-entry bugs.
6. Add temporary logging for:

   * block enter
   * block exit
   * pc_before
   * pc_after
   * block flags
   * ends_block
7. Fix root cause, not symptoms.

## Checkpoints

* One block should not re-enter without correct PC reason.
* Cached block execution should not keep returning same PC unless instruction semantics demand it.
* Non-progress fallback path should be rare and explicit.

## Commit message

`jit: fix block execution progress and PC writeback`

---

# TASK-04-control-flow.md

## Objective

Make control flow instructions semantically correct before expanding instruction coverage.

## Scope

* translators in `src/m68xkcpu/jit_block.c`
* any associated emitter use

## Priority instructions

* `Bcc`
* `BSR`
* `JSR`
* `JMP`
* `RTS`

## Rules

* No guessing
* PC-relative handling must match 68k semantics
* Return address handling must be exact

## Required outcome

* Effective addresses are correct
* PC-relative branch offsets are correct
* return stack behavior is correct
* no dereference where EA itself is target

## Steps

1. Validate each control-flow translator against manuals and real 68k semantics.
2. Confirm `JSR/JMP` target is EA, not `[EA]`, unless instruction semantics require memory fetch.
3. Confirm `BSR` return address is exact.
4. Confirm `RTS` pop and PC writeback are exact.
5. Confirm conditional branch logic matches CCR state.

## Checkpoints

* Kickstart branch flow reaches new regions cleanly.
* No loop caused by bad return PC.
* No branch target truncation or width mismatch.

## Commit message

`jit: correct control-flow semantics for 68040 execution`

---

# TASK-05-movec-68040.md

## Objective

Implement correct 68040 MOVEC and control-register behavior needed for boot and MMU-aware execution.

## Scope

* `src/m68xkcpu/jit_block.c`
* supporting helpers

## Reference

* `Hardware/mc68xxx/MC68040UM.txt`

## Registers to handle correctly

* `VBR`
* `SFC`
* `DFC`
* `CACR`
* `TC`
* `ITT0`
* `ITT1`
* `DTT0`
* `DTT1`

## Rules

* 68040 semantics only
* privilege checks matter
* direction decode must be exact

## Required outcome

* Kickstart MOVEC paths run correctly
* no illegal instruction due to bad decode
* register IDs match 68040 manual

## Steps

1. Audit MOVEC decode bitfields.
2. Confirm control register number mapping.
3. Confirm `CR,Rn` vs `Rn,CR` direction logic.
4. Confirm privilege enforcement.
5. Confirm writeback targets are correct widths.

## Checkpoints

* boot reaches and survives known MOVEC sites
* no bogus illegal traps on valid MOVEC paths

## Commit message

`jit: fix 68040 MOVEC decode and control register handling`

---

# TASK-06-fallback-exceptions.md

## Objective

Make fallback and exception boundaries safe, explicit, and state-correct.

## Scope

* `src/m68xkcpu/jit.c`
* exception/fallback helpers

## Rules

* No silent NOP substitution
* No partial state corruption
* No implicit success after failed translation

## Required outcome

* JIT either executes correctly or falls back cleanly
* exception entry uses correct CPU state
* fallback reason is logged

## Steps

1. Audit all fallback reasons.
2. Ensure unsupported instructions return controlled fallback.
3. Ensure exception state is synchronized before fallback.
4. Remove any old “emit NOP and continue” behavior.

## Checkpoints

* every fallback is explainable
* no hidden state drift after fallback

## Commit message

`jit: harden fallback and exception state boundaries`

---

# TASK-07-fpu-68040.md

## Objective

Stage correct 68040 internal FPU support and selection behavior.

## Scope

* JIT FPU selection logic
* future FPU translator/emitter support

## Reference

* `Hardware/mc68xxx/MC68040UM.txt`
* `Hardware/mc68xxx/MC68881_MC68882_Floating-Point_Coprocessor_Users_Manual_1ed_1987_hocr_searchtext.txt`

## Rules

* Do not model 68040 as generic 68882
* `--jitfpu` must respect CPU type
* internal 68040 FPU differs from external coprocessor model

## Required outcome

* clear architecture for 68040 internal FPU
* proper option mapping
* groundwork for later instruction support

## Stages

1. Presence and selection model
2. FP state structure
3. basic move ops
4. arithmetic
5. compare / condition handling
6. exceptions

## Checkpoints

* option handling reflects correct CPU/FPU pairing
* no fake 68882 identity on 68040 path

## Commit message

`jit: stage 68040 internal FPU model and selection logic`

---

# TASK-08-mmu-68040.md

## Objective

Design and begin correct MMU-aware JIT behavior for 68040.

## Scope

* MMU control state
* JIT invalidation points
* translation assumptions

## Reference

* `Hardware/mc68xxx/MC68040UM.txt`
* `Hardware/mc68xxx/MC68851.txt` for comparison only, not model substitution

## Rules

* 68040 MMU only
* do not copy 68851 behavior directly
* no unsafe cache persistence after MMU control changes

## Required outcome

* JIT recognizes MMU state changes
* JIT invalidates blocks when needed
* architecture ready for address-translation-aware execution

## Stages

1. Identify MMU-visible control state
2. Track MMU-affecting writes
3. Add block invalidation on relevant state changes
4. define later translation-aware phase

## Checkpoints

* MMU control changes cannot leave stale translated blocks alive

## Commit message

`jit: stage 68040 MMU state tracking and block invalidation`

---

# TASK-09-validation.md

## Objective

Prove correctness through targeted regression and boot validation.

## Scope

* ProcessorTests integration where relevant
* targeted boot probes
* regression notes

## References

* `third_party/ProcessorTests/`
* `Hardware/mc68xxx/*`

## Rules

* do not claim correctness without evidence
* focus on relevant 68040 paths first

## Required outcome

* repeatable validation path
* tracked regressions
* known-good checkpoints after each stage

## Steps

1. Identify minimal test subsets for fixed instruction families.
2. Add focused validation runs for:

   * control flow
   * MOVEC
   * exception/fallback behavior
3. Capture known boot PCs before and after fixes.
4. Maintain a short status note after each stage.

## Checkpoints

* no SIGILL from host JIT code
* no early boot infinite loop
* known MOVEC sites survive

## Commit message

`jit: add staged validation checkpoints for 68040 JIT`

---

# Recommended execution order

1. `TASK-01-cpu-boundary.md`
2. `TASK-02-emitter-validation.md`
3. `TASK-03-block-execution.md`
4. `TASK-04-control-flow.md`
5. `TASK-05-movec-68040.md`
6. `TASK-06-fallback-exceptions.md`
7. `TASK-07-fpu-68040.md`
8. `TASK-08-mmu-68040.md`
9. `TASK-09-validation.md`

---

# Instruction to give Shell Codex before starting

Use exactly one task file at a time. Make minimal edits within task scope. Stop at checkpoint completion. Report changed files, reasoning, and exact commit message. Do not broaden scope. Do not rewrite Musashi. Do not add 68000–68030 JIT. Do not start 68060. Keep 68040 JIT correctness first.

