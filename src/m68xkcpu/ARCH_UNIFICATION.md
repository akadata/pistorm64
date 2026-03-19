# PiStorm AArch64 JIT: Architecture Unification

This documents the corrective unification applied to the JIT path.

## Single Architecture (Now)

- One translator interface: `jit_translate_context_t`
- One translator dispatch shape: `int (*jit_translator_fn)(jit_translate_context_t *ctx)`
- One emitter API surface in header declarations
- One emitter implementation in `jit_emit_aarch64.c`
- One active translator implementation path in `jit_block.c`

## Removed Conflicts

The unused standalone translator units using incompatible emitter contracts were removed:

- `jit_translate_add.c`
- `jit_translate_addq_subq.c`
- `jit_translate_branch.c`
- `jit_translate_cmp.c`
- `jit_translate_control.c`
- `jit_translate_logic.c`
- `jit_translate_misc.c`
- `jit_translate_move.c`
- `jit_translate_movec.c`
- `jit_translate_moveq.c`
- `jit_translate_sub.c`

## Correctness Fixes Included

- `JSR` and `JMP` now set `PC = EA` (no dereference of EA memory).
- `MOVEC` decode now uses:
  - register field: extension bits `[15:12]`
  - control-register id: extension bits `[11:0]`
  - direction: opcode bit 0 (`4E7A`: `CR->Rn`, `4E7B`: `Rn->CR`)
- MOVEC now advances PC by 4 bytes when handled.

## Scope

- This change is structural and semantic-corrective only.
- No optimization work was introduced.
