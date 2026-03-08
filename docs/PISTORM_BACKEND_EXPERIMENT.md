# PiStorm Optional Userspace Backend Experiment

## Goal

Determine whether removing the remaining kernel/userspace bus boundary can improve real Amiga chip-memory timing.

Primary timing target:

- Approach ECS ideal fastest CPU-visible chip cycle around **560 ns**.

## Current Conclusion

This branch proved userspace-MMIO is viable on Pi 4, but not compelling as a primary path.

- `pistorm_truth_test` confirms real hardware transactions in userspace mode.
- SSSpeed chip metrics improved only slightly in tested runs.
- Real workloads did not materially improve (example observed: Real3D candle scene ~47s on both backends).
- Some comparisons are currently contaminated by the known Z2 memory bug.

Decision from current evidence:

- Keep `pistorm.ko` as the default/primary backend.
- Keep userspace-MMIO as an experimental backend only.
- Prioritize fixing the Z2 memory bug before further backend conclusions.
- Prefer `pistorm_truth_test` for backend validation; do not spend more time chasing tiny MMIO tuning gains without a new hypothesis.

## Build

```bash
make
```

Default backend remains kernel (`pistorm.ko` + `/dev/pistorm`).

## Backend Selection

Backend selection is explicit in config files via one directive:

```text
pistorm kernel
```

or

```text
pistorm userspace
```

Public names are intentionally kept simple: `kernel` and `userspace`.

Userspace GPCLK can be configured in config files (no env var required):

```text
pistorm-gpclk-src 5
pistorm-gpclk-div 6
```

Userspace MMIO strobe stretch can also be tuned:

```text
pistorm-mmio-wr-stretch 2
pistorm-mmio-rd-stretch 2
```

## Userspace Backend Safety (First Pass)

Userspace mode assumes **exclusive ownership** of the bus-facing MMIO path.

Before using `pistorm userspace`, unload the kernel module:

```bash
sudo rmmod pistorm
```

No shared ownership is attempted in this first pass.

## MMIO Mapping Scope

Userspace backend mapping policy:

- Prefer `/dev/gpiomem` for GPIO register access.
- Use `/dev/mem` only when required for additional blocks (CPRMAN/GPCLK setup).

Why `/dev/mem` may still be required:

- GPCLK control registers are in CPRMAN and are not exposed via `/dev/gpiomem`.

## Hardware Semantics Source of Truth

For FC and bus semantics, **`rtl.fc/pistorm_fc.v` is the hardware truth source**.
The userspace backend transaction sequence follows that Verilog behavior.

## Current Limitations

- Userspace backend is experimental and focused on comparability, not feature expansion.
- If userspace MMIO init fails, no implicit runtime backend switching is performed.

## Comparison Procedure

Run the same emulator binary and same config/workload, changing only:

- `pistorm kernel`
- `pistorm userspace`

Collect and compare at minimum:

- `SSSpeed056`
- chip RAM execution timing
- chip RAM copy timing
- I/O-sensitive latency/timing tests

Do not claim gains without measured results from both backends.

## Quick Hardware Sanity Loop

Use the lightweight truth test for fast bus-health checks before full benchmarks:

```bash
./pistorm_truth_test -c default.cfg 1000
```

## Sweep Harness

Use the harness to generate per-case configs, run optional prechecks, and launch emulator
for each case in sequence:

```bash
./tools/pistorm-backend-harness.sh --userspace-divs 6,8,10 --userspace-wr 2,4,8 --userspace-rd 2,4,8
```

During each emulator run, execute your Amiga-side benchmark, then move to next case by
exiting emulator or running:

```bash
sudo killall -9 emulator
```

Note: this harness is operational, but if your environment has unstable `sudo`/process
ownership behavior, prefer manual case stepping with `pistorm_truth_test` + direct emulator runs.
