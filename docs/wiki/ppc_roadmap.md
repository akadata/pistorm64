# PPC Roadmap

This page is the working status snapshot for PiStorm64 PPC accelerator bring-up.

## Purpose

- Record what is already implemented and validated.
- Record the current blockers with evidence.
- Keep next steps focused on the shortest path to normal OS-side PPC handoff.

## Snapshot (2026-03-07)

## Implemented and validated

- QEMU-UAE PPC runtime loads and starts from `z2-ppc-accel` via `CONTROL.START`.
- Dedicated PPC RAM mapping is active (default profile, typically `0x08000000` + `128 MiB`).
- Mailbox ABI path is active on real PPC execution (`PING`, `HOST_TIME32`, IRQ semantics).
- `ppcshake` regression tooling exists and works for board-level probes:
  - `--id`
  - `--irq`
  - `--boot`
  - `--reset`
  - `--shutdown`
  - `--elf <path>`
- DiagArea is visible and repeatedly parsed by Amiga-side code.
- Resident/Init Diag path now executes and logs stage transitions (`DST3`, `RST1`).
- BootArea mirror is present and logs valid `main` range for PPC RAM.
- Startup behavior is now gateable:
  - `PPC_ACCEL_BOOTSTRAP_AUTOSTART_DST3`
  - `PPC_ACCEL_BOOTSTRAP_AUTOSTART_RST1`
- AutoConfig identity and strings are now configurable for compatibility probing:
  - `PPC_ACCEL_AC_MANUFACTURER`
  - `PPC_ACCEL_AC_PRODUCT`
  - `PPC_ACCEL_DIAG_NAME`
  - `PPC_ACCEL_DIAG_ID`

## Current behavior

- On clean boot, board bootstrap reliably reaches:
  - `DST3`
  - `RST1`
- In current failing path, `start.x` does not perform additional PPC MMIO writes.
- Specifically, after `start.x`, no observed writes to:
  - `CONTROL` (`+0x0008`)
  - `BOOT_ENTRY` (`+0x0038`)
  - bootstrap stage/arg registers (`+0x0044`, `+0x0048`)
- Result: PPC firmware remains in entry-zero wait loop when started without a supplied entrypoint.

## Main blockers

- OS-side handoff is not being triggered by normal `start.x` path.
- `BOOT_ENTRY` remains `0x00000000` in this flow (no kernel entry handoff observed).
- Board is detectable/parsible, but still not accepted as a full boot-capable PPC target in the target OS path.
- Compatibility surface is still incomplete versus Blizzard/CyberStorm expectations:
  - exact AutoConfig identity combinations
  - full resident/bootnode/expansion contract timing
  - any additional board-specific side effects expected before handoff

## What this means

- PPC execution is not the current blocker.
- The remaining gap is Amiga-side board contract acceptance and handoff production (`BOOT_ENTRY` + start transition) during normal OS flow.

## Next steps (priority order)

- Continue compatibility A/B tests with real Phase5-style identity pairs and device strings.
- Keep `RST1` autostart disabled while testing, so `start.x` ownership of handoff can be observed clearly.
- Capture and compare logs around:
  - `BOOT_ENTRY` writes
  - `bootdesc.entry` transitions
  - first `CONTROL.START` write caused by normal OS boot path
- If still silent, implement the minimal additional expansion/resident behavior needed to force transition from "Diag parsed" to "board bootstrap executed by OS path".

## Related documents

- `docs/PPC_ACCELERATOR_PLAN.md`
- `docs/PPC_ACCEL_BOOTSTRAP_CONTRACT.md`
- `src/platforms/amiga/zorro/ppc_accel/README.md`

