# PPC Accelerator Plan (Stage 7+)

## Goal

Make the PiStorm PPC board model visible and usable as a real accelerator path:
`Amiga OS side -> board registers/shared window/IRQ -> PPC runtime -> host services`.

## Current Baseline (Stage 6C)

- PPC runtime is real: `CONTROL.START` boots QEMU-UAE PPC and runs mailbox firmware.
- Mailbox transport is real: command lane + host service lane are operational.
- Amiga-side regression tool exists: `ppcshake`, `ppcshake --irq`, `ppcshake --id`.
- Not done yet: OS4 kernel/platform integration (board compatibility expectations, bootstraps, drivers).

## Assumptions

- Zorro memory mapping remains the board surface exposed to Amiga software.
- Runtime assets are externally provided (`qemu-uae.so`, firmware, config).
- Stage 6 ABI/register map remains stable unless explicitly version-bumped.

## Compatibility Target (Chosen Path: 1A)

Active direction is compatibility-first so OS-facing software can use the board
without inventing a new stack first.

- Primary target: BlizzardPPC-style expectations (identity/boot/control/IRQ behavior).
- Secondary fallback: CyberStormPPC-style expectations if primary assumptions fail.

This still does not mean full board emulation. The immediate objective is to
implement the minimum behavior contract needed for OS-facing detection and work
scheduling.

## Milestones

### Stage 7: Board Identity + Stable Registers

- Add deterministic board identity dump path (`ppcshake --id`).
- Keep register block semantics fixed and documented.
- Publish read-only shared info struct in shared window (`0x2000`).
- Freeze this as the compatibility baseline and avoid ABI drift.

### Stage 8: Bootstrapping Contract

- Define and freeze accelerator bootstrap contract used by OS-side code:
  - shared memory layout
  - mailbox/IRQ usage
  - CPU start/reset sequence
- Add compatibility notes for BlizzardPPC-style startup expectations and
  explicitly document what is implemented vs. not implemented.
- Add regression checks for contract invariants.
- Canonical reference: `docs/PPC_ACCEL_BOOTSTRAP_CONTRACT.md`.

### Stage 9: OS Integration Attempt

- Attempt OS-facing integration against selected compatibility target.
- Measure what is missing (identity, bootstrap, interrupts, loader expectations).
- Iterate with compatibility-focused deltas, not mailbox feature creep.

## Stage 7A Shared Info Struct

Location: shared window base (`0x2000` absolute board offset), 32-byte block, big-endian fields.

- signature (`PPCA`)
- abi_version
- mailbox_offset
- mailbox_size
- doorbell_register_offset
- feature_flags
- reserved[2]

Use:

- read-only for Amiga-side clients
- written by emulator/backend at init/runtime bring-up
- versioned so future extensions remain forward-compatible
