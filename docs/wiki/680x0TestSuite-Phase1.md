# 680x0 Test Suite Phase 1

Phase 1 establishes a repeatable baseline for 68k core regression checks before deeper PMMU work.

## Targets

- `make processortests-quick`
  - Validates ProcessorTests dataset structure/opcode map on a quick subset.
- `make musashi-ref-tests-quick`
  - Builds a standalone Musashi runner (`build/musashi_ref_test_driver`) and runs a small 68000/68040 smoke set.
- `make musashi-ref-tests-68040`
  - Runs the full reference 68040 binary set from `reference/Musashi/test/mc68040`.
- `make musashi-ref-tests-68040-ci`
  - Runs the same 68040 set with expected-failure baseline gating.
  - Fails on new regressions and also on unexpected passes (to force baseline updates).
- `make musashi-ref-tests-68040-pmmu`
  - Same as above, forcing `USE_PMMU=1` in the build invocation.
- `make stage1-680x0`
  - Combined Phase 1 baseline (`processortests-quick` + `musashi-ref-tests-quick`).
- `make stage1-680x0-ci`
  - CI-oriented baseline (`musashi-ref-tests-quick` + `musashi-ref-tests-68040-ci`).

## Reference paths

- ProcessorTests root:
  - default prefers `$(CURDIR)/third_party/ProcessorTests`
  - falls back to `/home/smalley/reference/ProcessorTests` if vendored data is absent
- Musashi test root: `MUSASHI_REF_TEST_ROOT` (default `/home/smalley/reference/Musashi/test`)
- 68040 expected-fail baseline: `tools/baselines/musashi_ref_68040.xfail`

Override at invocation time as needed, for example:

```bash
make MUSASHI_REF_TEST_ROOT=/some/other/path musashi-ref-tests-68040
```

## Notes

- The standalone runner is intentionally isolated from platform devices; it tests core CPU behavior.
- If `musashi-ref-tests-68040` fails, treat that as a correctness signal, not a build issue.
- The CI target uses xfail gating so known issues do not hide new regressions.

## Later Integration Plan (Not Implemented Yet)

Keep CPU conformance and full Amiga integration as separate layers:

1. CPU-core truth (headless, no Amiga boot)
- Goal: deterministic instruction-level pass/fail with minimal noise.
- Candidate CLI shape:
  - `./emulator --cpu-tests quick`
  - `./emulator --cpu-tests full`
  - `./emulator --cpu-tests movec`
  - `./emulator --cpu-tests exceptions`
  - `./emulator --cpu-tests alignment`

2. Amiga-map truth (CPU tests under Amiga memory rules)
- Goal: verify behavior with VBR/exception vectors, FC-sensitive accesses, supervisor transitions, and map-specific behavior.
- Candidate CLI shape:
  - `./emulator --cpu-tests amiga`

3. Full platform truth (chipset/device integrated)
- Goal: validate CPU correctness with autoconfig, RTG, storage/network services, and boot utilities.
- Candidate CLI shape:
  - `./emulator --cpu-tests chipset`

Rationale:
- Stage 1 failures must remain instruction-focused and debuggable without RTG/PiSCSI/A314 noise.
- PMMU work should be added after Stage 1 reporting is stable and reproducible.
