# PPC Accelerator Bootstrap Contract (Stage 8)

This document freezes the current board-side bootstrap contract for the
compatibility-first (`1A`) path.

It defines what is stable now, what software may rely on, and what is still
explicitly out of scope.

## Scope

- Zorro-II board model identity and map
- Register semantics needed to start/reset PPC runtime
- Mailbox and IRQ ordering contract
- Shared-info discovery block at shared window base

## Stable Identity and Map

- Manufacturer: `0x07DB`
- Product: `0x0040`
- Aperture: `64 KiB` at AutoConfig-assigned Z2 base
- Layout:
  - `+0x0000-0x0FFF`: register window
  - `+0x1000-0x1FFF`: mailbox page
  - `+0x2000-0xFFFF`: shared window

## Register Contract (Big-Endian 32-bit)

- `MAGIC` (`+0x0000`) = `0x50504341` (`PPCA`)
- `ABI_VERSION` (`+0x0004`) = `1`
- `CONTROL` (`+0x0008`)
  - bit0 `START`
  - bit1 `RESET`
  - bit2 `IRQ_ENABLE`
- `STATUS` (`+0x000C`)
  - bit0 `RUNNING`
  - bit1 `FAULT`
- `DOORBELL` (`+0x0010`) write pulse marker
- `IRQ_STATUS` (`+0x0014`) pending bits
- `IRQ_ACK` (`+0x0018`) write-1-to-clear bits
- `MAILBOX_OFFSET`/`SIZE` (`+0x001C`/`+0x0020`) = `0x1000` / `0x1000`
- `SHARED_OFFSET`/`SIZE` (`+0x0024`/`+0x0028`) = `0x2000` / `0xE000`
- `PPC_RAM_BASE`/`SIZE` (`+0x002C`/`+0x0030`) = PPC runtime RAM mapping
  (default `0x08000000` / `0x08000000`)
- `BOOT_MAGIC` (`+0x0034`) = reset-trampoline descriptor magic
- `BOOT_ENTRY` (`+0x0038`) = reset-trampoline branch target
- `BOOT_STACK` (`+0x003C`) = reset-trampoline stack (`r1`)
- `BOOT_ARG0` (`+0x0040`) = reset-trampoline arg (`r3`)

## Shared-Info Block (`+0x2000`)

Read-only from Amiga side, written by emulator backend at init/bring-up.

- `signature` = `PPCA`
- `abi_version` = `1`
- `mailbox_offset`/`mailbox_size`
- `doorbell_register_offset`
- `feature_flags`
  - bit0 host service lane available
  - bit1 IRQ path available
  - bit2 PPC external interrupt path available
- `reserved0` = boot descriptor offset (`0x2040`)
- `reserved1` = boot descriptor size (`0x20`)

Boot descriptor (`+0x2040`, big-endian 32-bit fields):

- `magic` (`PPBT` by default)
- `entry` (PPC branch target used by reset trampoline)
- `stack` (loaded to `r1`)
- `arg0` (loaded to `r3`)
- `marker` (written by trampoline for trace visibility)

## Runtime Start/Reset Semantics

- `CONTROL.START=1`:
  - ensures PPC backend bootstrap is complete
  - starts PPC execution
  - sets `STATUS.RUNNING` on success
- `CONTROL.START=0`:
  - pauses PPC execution
  - clears `STATUS.RUNNING`
- `CONTROL.RESET=1`:
  - mailbox+shared window reset to contract defaults
  - PPC CPU reset to firmware entry
  - bit is self-clearing in stored control state

Current behavior is idempotent (repeated start/reset does not leak threads).

Reset-vector behavior:

- PPC reset lands at `0xFFF00100` (board reset window alias).
- Secondary firmware entry is a trampoline that validates boot descriptor magic,
  writes marker transitions, and branches to descriptor `entry`.
- Default descriptor points to primary mailbox firmware entry (`0x00000000`).

## Mailbox and IRQ Ordering Contract

Single in-flight command lane:

- in-flight iff `seq != ack_seq`
- submit only when `seq == ack_seq`

Ordering:

- requester writes cmd/args/results placeholders first, then writes `seq` last
- responder writes `result*`, then `status`, then memory barrier, then `ack_seq` last

`CMD_DONE` IRQ is derived from `ack_seq` transition reaching completed status.

## Implemented vs Not Implemented

Implemented now:

- board discovery and deterministic map
- bootrom-compatible AutoConfig flags with minimal DiagArea/name block at `base+0x4000`
- PPC runtime start/pause/reset control path
- dedicated PPC-visible RAM mapping (default `128 MiB` at `0x08000000`, tunable via env)
- mailbox command lane on real PPC execution path
- host service lane (`TIME32`, `MEM_CRC32`) and optional doorbell pulse
- `ppcshake` regression coverage (`--id`, `--irq`, round-trip path)

Not implemented yet:

- full BlizzardPPC/CyberStormPPC firmware/flash compatibility surface
- OS4 kernel/loader integration contract
- production interrupt delivery model beyond current polling + optional doorbell

## Regression Gate (Stage 8)

Required green checks before compatibility changes:

- `ppcshake --id`
- `ppcshake --irq`
- `ppcshake 10`
- no regressions in Stage 6C startup logs and mailbox completion ordering

Optional diagnostics:

- `PPC_ACCEL_AC_TRACE=1` for AutoConfig probe read/write traces
