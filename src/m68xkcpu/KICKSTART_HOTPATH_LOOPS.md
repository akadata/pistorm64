# Kickstart Hot-Path Loop Notes (68040 JIT)

## Purpose

Capture the first reproducible loop regions seen after enabling active `jit m68xkcpu` execution without startup gating, and document the control-flow patterns that still need semantic stabilization.

This file is intentionally focused on the known hot PCs, not broad feature work.

## Source Evidence

- Early run logs after emitter repair and active JIT execution.
- Fallback traces around `0x00F800E2/0x00F800E4/0x00F800E6/0x00F800E8`.
- Later repeated-entry windows around `0x00F80F2E/0x00F80F30` and `0x00F81AEC`.
- MMU state-change invalidation observed around `0x00F80D16`.

## Range A: `0x00F800D2` .. `0x00F800F0`

Observed recurring opcodes near the loop core:

- `0x00F800E2`: `0xDA98`
- `0x00F800E4`: `0x6402` (short conditional branch form)
- `0x00F800E6`: `0x5285`
- `0x00F800E8`: `0x51C9` (DBcc-class loop form)

Inferred behavior from repeated entry traces:

1. Entry lands in the `E2/E4/E6/E8` window.
2. `0x6402` drives a short branch edge over a tiny address delta.
3. `0x51C9` re-enters the same local window repeatedly.
4. When JIT retirement semantics are wrong, this produces tiny-window oscillation even without host SIGILL.

## Range B: `0x00F80F20` .. `0x00F80F40`

Observed hot re-entry points:

- `0x00F80F2E`
- `0x00F80F30`

Pattern:

- Repeated return to a 2-address pair indicates branch/flag/retirement semantics are still unstable in this window.
- This is no longer an opcode-emission legality issue; it is semantic non-progress in block progression.

## Range C: `0x00F81AE0` .. `0x00F81AF4`

Observed hot re-entry point:

- `0x00F81AEC`

Pattern:

- Persistent revisit of the same narrow range after fallback implies churn between compiled execution and fallback boundary.
- This range should be treated as a hot-loop candidate until branch/CCR behavior is stabilized.

## Practical Trace Command

Use this when collecting refreshed loop windows:

```bash
sudo env PISTORM_M68XK_TRACE_BLOCKS=1 PISTORM_M68XK_TRACE_FALLBACK=1 PISTORM_ILLG_TRACE_CTX=1 ./emulator --log-level info --log jit68xk_hotloops.log
```

## Current Interpretation

- Host instruction legality has improved (no immediate SIGILL in the first 68040 run path).
- Remaining blocker is semantic:
  - block retirement and next-PC progression;
  - tiny-window control-flow oscillation;
  - fallback re-entry churn at hot PCs.

This is the expected next debug target for 68040 stabilization.
