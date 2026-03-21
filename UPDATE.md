# 68040 AArch64 JIT correction plan for Codex

## Goal

Bring the PiStorm64 `m68xkcpu` JIT onto a single, disciplined path:

* JIT target is **68040 only** for now.
* **68030 and lower remain on Musashi**.
* 68040 integer core, control flow, MOVEC, internal **FPU**, and internal **MMU** must be made correct before chasing speed.
* The implementation target is a **correct 68040 AArch64 JIT**, not a mixed interpreter/JIT experiment with drifting semantics.

This task is a **path correction**. Remove drift. Restore one coherent design. Keep scope narrow. Do not broaden to 68060 now.

## Scope rules

Codex should work to these rules:

1. Keep **Musashi** as the execution path for 68000, 68010, 68020, and 68030.
2. Treat **68040 as the only JIT CPU target**.
3. Do not introduce new CPU-family ambitions during this pass.
4. Do not add 68060 JIT work in this task.
5. Preserve and use the existing `--jitfpu` option, while making its behavior match the selected CPU model.
6. For 68040, reflect that **FPU and MMU are internal**, not external 68881/68882 devices.
7. For lower CPUs where external FPU is relevant, keep that under Musashi for now unless code already needs a minimal compatibility bridge.

## Important local references

Codex should actively use local reference material already present in the repository.

### CPU manuals

Located under:

`Hardware/mc68xxx/`

Relevant files include:

* `MC68040UM.txt`
* `MC68020UMAD.txt`
* `MC68030UM-P1.txt`
* `MC68881_MC68882_Floating-Point_Coprocessor_Users_Manual_1ed_1987_hocr_searchtext.txt`
* `MC68851.txt`
* `MC68060UM.txt`
* `MC68060DE.txt`

Primary authority for this task:

* **MC68040UM.txt** for CPU, MMU, MOVEC, cache/MMU control, exceptions, FPU integration.
* **MC68881/68882 manual** for external FPU behavior comparison and instruction semantics where helpful, while remembering 68040 has internal FPU behavior constraints.

### Processor tests

Located under:

* `third_party/ProcessorTests/`
* `third_party/ProcessorTests/680x0/`

These are useful for instruction semantics and regression planning. They are not a substitute for the 68040 manual, especially for full MMU/FPU integration behavior.

### Local reference code outside the repo root

There is also a reference directory available:

* `../reference`

Codex should inspect this carefully for:

* prior JIT implementations
* 68040-related code
* 68060 simulator code
* emitter patterns
* decode/dispatch ideas
* MMU/FPU handling ideas

Use it as reference material, not as something to copy blindly.

## Current strategic decision

The current strategy is:

* **Do not JIT 68000–68030 right now**.
* Use Musashi there because it is already fast and stable enough.
* Build a **single correct 68040 JIT path**.
* Once 68040 integer + control + MOVEC + MMU/FPU are correct, later decide whether lower-family JIT is worth doing.

## Current known symptoms and failure pattern

From observed runs, the current JIT is not yet executing a stable 68040 path.

### What is working

These are encouraging signs and should be preserved:

* JIT backend selection and startup wiring are in place.
* Blocks are being compiled and entered.
* AArch64 code is being emitted.
* The bad immediate-shift encoding issue appears to have been identified and at least partly corrected.
* The host-side SIGILL caused by invalid AArch64 shift macro encodings was likely a real root issue in one phase.
* There is already evidence of progress beyond the first host SIGILL once corrected encodings were emitted.

### What is still wrong

These are the important current problems:

1. The execution path around early Kickstart boot blocks is still unstable.
2. There is repeated execution of the same early boot PCs, especially around:

   * `0x00F800E2`
   * `0x00F800E4`
   * `0x00F800E6`
   * `0x00F800E8`
3. This strongly suggests a **PC progression / block exit / condition code / branch translation** problem.
4. Earlier there was a host **SIGILL** caused by invalid AArch64 instruction emission.
5. There were earlier illegal-instruction symptoms around 68k-side execution near MOVEC handling and boot control flow.
6. The codebase drifted into a mixed model where translator interfaces and emitter semantics were not consistently aligned.
7. There is a high risk that some instructions are still being emitted with semantics that do not match Musashi or 68040 documentation.

## Most likely technical fault zones

Codex should review these first and treat them as top-risk areas.

### 1. AArch64 emitter correctness

The emitted host code must be verified instruction by instruction for all helper macros used by active translators.

High-risk classes:

* shifts and bitfield aliases
* arithmetic vs logical sign-extension helpers
* PC load/store helpers
* SR/CCR access helpers
* branch helpers
* block epilogue and return path
* memory/register width handling

This needs a pass over:

* `src/m68xkcpu/jit_emit_aarch64.h`
* `src/m68xkcpu/jit_emit_aarch64.c`

### 2. Block execution and PC progression

The current loop pattern suggests the JIT may be:

* not advancing PC correctly
* writing back stale PC
* ending blocks incorrectly
* translating condition/branch exits incorrectly
* returning from a block without consistent CPU state

This needs a close pass over:

* `src/m68xkcpu/jit.c`
* `src/m68xkcpu/jit_block.c`
* `src/m68xkcpu/jit_cache.c`

### 3. Translator semantic drift

The project has already gone through a unification attempt. Codex must verify whether the resulting unified path is actually semantically sound.

High-risk translator areas:

* `MOVE`
* `ADD` / `SUB`
* `CMP`
* branch family
* `BSR`
* `JSR`
* `JMP`
* `RTS`
* `MOVEC`
* miscellaneous control / exception-sensitive ops

### 4. 68040-specific control register and cache/MMU semantics

This is central to the final target.

Must be correct for:

* `VBR`
* `SFC`
* `DFC`
* `CACR`
* `TC`
* `ITT0/ITT1`
* `DTT0/DTT1`
* `URP/SRP` if applicable in the chosen model
* 68040-valid MOVEC register set
* cache/MMU control instruction behavior such as PFLUSH/CPUSH class where implemented

### 5. Exception model integration

A JIT that mutates CPU state must still respect exception entry and exit rules.

Must check:

* illegal instruction path
* privilege violations
* trap/exception dispatch handoff
* PC and SR writeback before exception entry
* interaction with Musashi fallback when a JIT block cannot safely complete

### 6. FPU integration for 68040

This should not be treated as generic 68882-on-anything.

68040 has an internal FPU model with different expectations than an external coprocessor path.

The final architecture should:

* expose correct 68040 FPU availability when CPU is 68040
* gate `--jitfpu` behavior correctly
* not pretend a 68040 is using an external 68882 model unless explicitly emulating that at a system level, which should not be done in the JIT core itself

### 7. MMU integration for 68040

The 68040 MMU is not optional for “feature complete 68040” work.

Even if full MMU acceleration is staged, the architecture must be designed so that:

* JIT knows when MMU-sensitive behavior invalidates assumptions
* translation caches can be invalidated appropriately
* MMU-control instructions are not mishandled as no-ops unless the behavior is explicitly and correctly modeled

## Staged correction plan

Codex should work in stages and keep each stage small, reviewable, and reversible.

### Stage 0 — Establish the architecture boundary

Objective:

Freeze scope and cleanly separate CPU-family responsibilities.

Tasks:

* Make CPU selection logic explicit:

  * 68000/68010/68020/68030 -> Musashi execution path
  * 68040 -> eligible for JIT path
* Audit config handling to ensure the selected CPU actually controls JIT eligibility.
* Ensure `--jitfpu` is parsed and retained, however only acts where appropriate.
* Make sure logs clearly say whether execution is:

  * Musashi only
  * 68040 JIT integer only
  * 68040 JIT with JIT FPU enabled

Deliverable:

A clean architectural gate that stops mixed-family confusion.

### Stage 1 — Verify emitter correctness

Objective:

Prove the AArch64 emitter is mechanically correct before blaming translator logic.

Tasks:

* Audit all active emitter macros and helpers.
* Cross-check every emitted AArch64 opcode form used by active translators.
* Verify width correctness for W/X register forms.
* Verify shift/bitfield aliases.
* Verify sign extension and zero extension helpers.
* Verify block prologue and epilogue.
* Verify block return convention and register preservation.
* Confirm the intended dedicated CPU-state register assignment is consistent everywhere.
* Remove dead or duplicate emitter logic.

Deliverable:

A small emitter validation note in-tree and no remaining known invalid encodings.

### Stage 2 — Fix block execution semantics

Objective:

Make block execution advance correctly and stop looping on early boot PCs.

Tasks:

* Audit `jit_execute()` and `jit_block_execute()` interaction.
* Confirm the source of truth for PC.
* Confirm when `g_jit.cpu->pc` and `g_jit.current_pc` are updated.
* Verify block end conditions.
* Verify `ends_block` behavior.
* Ensure non-progress detection is correct and not masking a translator bug.
* Add precise instrumentation for:

  * block entry PC
  * block exit PC
  * cycles consumed
  * branch-taken decisions
  * fallbacks and reasons

Deliverable:

Early Kickstart boot blocks progress monotonically instead of cycling around `0x00F800E2`–`0x00F800E8`.

### Stage 3 — Control flow and status correctness

Objective:

Make the core control-flow instructions correct before expanding instruction coverage.

Tasks:

Audit and correct these translators first:

* `MOVE`
* `CMP`
* `ADD`
* `SUB`
* conditional branch family
* `BSR`
* `JSR`
* `JMP`
* `RTS`

Checks required:

* effective address semantics
* PC-relative semantics
* return address calculation
* status register / condition code effects
* operand width handling
* sign extension rules

Deliverable:

A reliable integer/control-flow core capable of boot progress without interpreter rescue on ordinary early 68040 boot code.

### Stage 4 — 68040 MOVEC and privileged control path

Objective:

Make MOVEC and privileged 68040 control behavior match the manual and system expectations.

Tasks:

* Build an explicit 68040 MOVEC register map from `MC68040UM.txt`.
* Reject invalid control register numbers exactly as appropriate for 68040.
* Handle direction bits correctly.
* Handle data register vs address register source/target correctly where legal.
* Model cache/MMU-class instructions correctly for the intended emulation level.
* Ensure exceptions are raised when needed.

Focus especially on:

* `VBR`
* `SFC`
* `DFC`
* `CACR`
* `TC`
* `ITT0`
* `ITT1`
* `DTT0`
* `DTT1`

Deliverable:

Correct early-boot control-register traffic on 68040 path.

### Stage 5 — Exception and fallback discipline

Objective:

Make fallback a correctness mechanism, not a hidden second execution engine.

Tasks:

* Define exactly when fallback to Musashi is legal.
* Ensure no partial state corruption before fallback.
* Ensure illegal instruction and privilege exceptions happen with correct state.
* Audit exception entry interactions with JIT-written CPU state.
* Remove any silent “emit NOP and continue” behavior for instructions that must trap or fall back.

Deliverable:

A predictable JIT/interpreter boundary with clean state handoff.

### Stage 6 — 68040 internal FPU design

Objective:

Prepare and implement the correct FPU model for a 68040 target.

Tasks:

* Review 68040 internal FPU behavior from `MC68040UM.txt`.
* Review external 68881/68882 instruction/reference behavior from the coprocessor manual only as supporting reference.
* Make CPU/FPU selection logic truthful:

  * lower CPUs may use Musashi external-FPU modeling as currently applicable
  * 68040 should present internal FPU semantics
* Define how `--jitfpu` behaves on 68040.
* Decide staged implementation order for FPU opclasses.
* Prefer correctness and fallback over half-implemented arithmetic.

Suggested order:

1. FPU presence / configuration truthfulness
2. FPU state structure and save/restore correctness
3. FMOVE / simple register transfer path
4. basic arithmetic classes
5. compare / test / status integration
6. exception and rounding behavior

Deliverable:

A staged 68040 internal FPU plan wired to the JIT architecture, not an ad hoc 68882 bolt-on.

### Stage 7 — 68040 MMU design and integration

Objective:

Create a real plan for 68040 MMU correctness, even where execution still falls back.

Tasks:

* Review 68040 MMU registers and translation model from the manual.
* Define what is needed for correctness in this emulator.
* Define where JIT assumptions become invalid under MMU changes.
* Add invalidation hooks for MMU-affecting register writes and flush operations.
* Ensure MMU-sensitive instructions do not silently behave as harmless NOPs unless that is intentionally documented and safe.

Deliverable:

A correct MMU-aware JIT design, even if some MMU-heavy flows still temporarily fall back to Musashi.

### Stage 8 — Testing and regression harness

Objective:

Move from “boots sometimes” to measured correctness.

Tasks:

* Use processor tests where directly applicable.
* Add focused regression cases for:

  * early Kickstart boot PCs observed in logs
  * MOVEC sequences
  * JSR/JMP/RTS
  * branch progression
  * SR/CCR-sensitive compare/branch combinations
* Add small targeted JIT-vs-Musashi comparison tests for the translated subset.
* Record exactly which instruction families are JIT-trusted for 68040.

Deliverable:

A staged validation matrix for the 68040 JIT subset.

## Current priorities for Codex

Codex should not try to solve everything at once. The immediate priority order should be:

1. Restore a coherent 68040-only JIT boundary.
2. Audit and fix emitter correctness.
3. Fix block progress / repeated early-PC looping.
4. Fix control-flow and status semantics.
5. Fix 68040 MOVEC and privileged register semantics.
6. Then design FPU/MMU integration cleanly.

## What to preserve

Codex should preserve these good decisions where present:

* keeping Musashi available as the reference engine
* using JIT fallback for unsupported cases
* using local CPU manuals and test sets
* building on the unified translator/emitter concept, provided it is corrected rather than expanded carelessly

## What to remove or avoid

Codex should be ruthless about removing the following kinds of drift:

* duplicate translator paths
* dead emitter interfaces
* mixed execution conventions
* silent NOP substitution for unsupported privileged instructions
* CPU-family ambiguity in feature exposure
* half-correct generic FPU handling presented as 68040-complete
* speculative 68060 work

## Recommendation on Codex mode

Use **shell Codex** first for this work, not web Codex as the primary path.

Reason:

* the task is rooted in local source, local manuals, local tests, and local logs
* it needs careful, staged code correction
* it benefits from tight iteration on exact files
* shell Codex is less likely to drift into broad speculative rewrites

Use web Codex only for narrowly scoped parallel subtasks, such as:

* summarising a manual section
* extracting 68040 MOVEC register tables
* comparing 68040 FPU vs 68882 behavior notes

Do not split core JIT correction into many simultaneous Codex tasks. This path is too stateful and too easy to fragment.

## Final instruction to Codex

Treat this as a disciplined repair of the 68040 JIT architecture.

The desired end state is:

* Musashi for 68030 and below
* one coherent 68040 AArch64 JIT path
* correct control flow and PC progression
* correct MOVEC and privileged control semantics
* correct 68040 internal FPU/MMU integration plan and staged implementation
* no false claims of completeness before validation proves it
* if required start by checking out the code in src/musashi from main branch and reverting if the current branch is found to be poluted, if this is done. do so then remove this line to prevent future reverts
