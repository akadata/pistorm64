# Z2 PPC Accelerator (Stage 6C)

This is a first board-model slice for a PiStorm PPC accelerator device exposed via
Zorro-II AutoConfig.

Canonical ABI constants are defined in `ppc_accel_regs.h` and shared with
Amiga-side tools to prevent register/mailbox offset drift.

Enable in config:

```ini
setvar zorro-ppc
```

Alternative alias:

```ini
setvar ppc-accel
```

## AutoConfig identity

- Manufacturer: `0x07DB` (PiStorm)
- Product: `0x0040` (`PISTORM_PROD_PPC_ACCEL_Z2`)
- AutoConfig serial default: `0x00420001` (non-zero, deterministic; tunable)
- Device name: `z2-ppc-accel`
- Device size: `64 KiB`

Optional tuning:

- `PPC_ACCEL_AC_SERIAL=<u32>` overrides the 32-bit AutoConfig serial field
  (`er_SerialNumber`) encoded into the board ROM nibbles.
- `PPC_ACCEL_AC_DIAG_VEC=<u16>` overrides `er_InitDiagVec` (default `0x4000`).
- `PPC_ACCEL_PPC_RAM_BASE=<u32>` sets PPC-visible RAM base (default `0x08000000`).
- `PPC_ACCEL_PPC_RAM_MB=<u32>` sets PPC-visible RAM size in MiB (default `128`).
- `PPC_ACCEL_QEMU_LOG=1` forwards `qemu-uae.so` `uae_log()` output into PiStorm logs.
- `PPC_ACCEL_TRACE_IO=1` enables PPC I/O callback tracing (`uae_ppc_io_mem_*` path).
- `PPC_ACCEL_TRACE_IO_LIMIT=<u32>` caps trace lines (default `256`, `0` = unlimited).

## Device memory map

- `0x0000-0x0FFF`: register window
- `0x1000-0x1FFF`: mailbox page
- `0x2000-0xFFFF`: shared RAM window

Shared window starts with a read-only shared-info block at `0x2000`:

- `+0x00` signature (`PPCA`)
- `+0x04` abi_version
- `+0x08` mailbox_offset
- `+0x0C` mailbox_size
- `+0x10` doorbell_register_offset
- `+0x14` feature_flags
- `+0x18` reserved0
- `+0x1C` reserved1

Register block (`32-bit big-endian`):

- `0x0000` `MAGIC` = `0x50504341` (`PPCA`)
- `0x0004` `ABI_VERSION` = `1`
- `0x0008` `CONTROL` (`START=bit0`, `RESET=bit1`, `IRQ_ENABLE=bit2`)
- `0x000C` `STATUS` (`RUNNING=bit0`, `FAULT=bit1`)
- `0x0010` `DOORBELL` (write-only pulse marker)
- `0x0014` `IRQ_STATUS`
- `0x0018` `IRQ_ACK` (write bits to clear)
- `0x001C` `MAILBOX_OFFSET` = `0x1000`
- `0x0020` `MAILBOX_SIZE` = `0x1000`
- `0x0024` `SHARED_OFFSET` = `0x2000`
- `0x0028` `SHARED_SIZE` = `0xE000`
- `0x002C` `PPC_RAM_BASE` (default `0x08000000`)
- `0x0030` `PPC_RAM_SIZE` (bytes, default `0x08000000` = `128 MiB`)

## Mailbox ABI v1 (inside device window at `base + 0x1000`)

All fields are `32-bit big-endian`.

- `+0x00` `magic` = `0x504D4241` (`PMBA`)
- `+0x04` `version` = `1`
- `+0x08` `seq`
- `+0x0C` `ack_seq`
- `+0x10` `cmd`
- `+0x14` `status`
- `+0x18` `arg0`
- `+0x1C` `arg1`
- `+0x20` `result0`
- `+0x24` `result1`

Single in-flight contract:

- command is in-flight while `seq != ack_seq`
- host must only submit when `seq == ack_seq`
- host publishes `seq` last
- responder publishes `status/result` first and `ack_seq` last

Commands implemented:

- `1` `PING` -> `result0 = arg0 ^ 0xFFFFFFFF`
- `3` `HOST_TIME32` -> `result0 = monotonic_time_ns_low32`

Statuses:

- `0` `IDLE`
- `1` `BUSY`
- `2` `DONE`
- `3` `ERR`

## Notes

- Stage 6C runs the mailbox handler on a real QEMU-UAE PPC core.
- The mailbox page is single-backed: Amiga Zorro access and PPC access touch the same memory.
- PPC runtime also maps a dedicated PPC RAM region (`128 MiB` at `0x08000000` by default)
  for compatibility experiments that expect CSPPC-style PPC RAM presence.
- Card now advertises `Z2_BOOTROM` and publishes a minimal DiagArea at `base+0x4000`
  with a name string for legacy board discovery tooling.
- For legacy probe debugging, set `PPC_ACCEL_AC_TRACE=1` before launching emulator to log
  PPC board AutoConfig reads/writes.
- Runtime assets must be available (`qemu-uae.so`, firmware/config paths) and
  `LD_LIBRARY_PATH` should not include Python 2.7 build paths at runtime.
