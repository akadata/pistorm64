# Z2 PPC Accelerator (Stage 6A)

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
- Device name: `z2-ppc-accel`
- Device size: `64 KiB`

## Device memory map

- `0x0000-0x0FFF`: register window
- `0x1000-0x1FFF`: mailbox page
- `0x2000-0xFFFF`: shared RAM window

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

Commands implemented in Stage 6A:

- `1` `PING` -> `result0 = arg0 ^ 0xFFFFFFFF`
- `3` `HOST_TIME32` -> `result0 = monotonic_time_ns_low32`

Statuses:

- `0` `IDLE`
- `1` `BUSY`
- `2` `DONE`
- `3` `ERR`

## Notes

This Stage 6A implementation proves Amiga-side discovery and mailbox handshake.
It does not yet run the QEMU PPC core behind this board model.
