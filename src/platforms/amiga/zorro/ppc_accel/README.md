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
- `PPC_ACCEL_DIAG_CONFIG=<u8>` overrides DiagArea `da_Config` at `base+0x4000`
  (default `0x00`; legacy probe test value: `0x90` for `DAC_WORDWIDE|DAC_CONFIGTIME`).
- `PPC_ACCEL_DIAG_DIAGPOINT=<u16>` overrides DiagArea `da_DiagPoint` (default `0x0000`).
- `PPC_ACCEL_DIAG_BOOTPOINT=<u16>` overrides DiagArea `da_BootPoint`
  (default `0x0000` = disabled; set `0x0020` to use built-in 68k start-stub).
- `PPC_ACCEL_DIAG_TRACE=1` logs DiagArea header/stub fetch reads.
- `PPC_ACCEL_DIAG_TRACE_LIMIT=<u32>` caps DiagArea trace lines (default `256`, `0` = unlimited).
- `PPC_ACCEL_PPC_RAM_BASE=<u32>` sets PPC-visible RAM base (default `0x08000000`).
- `PPC_ACCEL_PPC_RAM_MB=<u32>` sets PPC-visible RAM size in MiB (default `128`).
- `PPC_ACCEL_QEMU_LOG=1` forwards `qemu-uae.so` `uae_log()` output into PiStorm logs.
- `PPC_ACCEL_TRACE_IO=1` enables PPC I/O callback tracing (`uae_ppc_io_mem_*` path).
- `PPC_ACCEL_TRACE_IO_LIMIT=<u32>` caps trace lines (default `256`, `0` = unlimited).
- `PPC_ACCEL_MMIO_TRACE=1` logs Zorro MMIO reads/writes to the PPC board window.
- `PPC_ACCEL_MMIO_TRACE_LIMIT=<u32>` caps MMIO trace lines (default `512`, `0` = unlimited).
- `PPC_ACCEL_BOOT_MAGIC=<u32>` sets PPC reset-trampoline descriptor magic
  (default `0x50504254`, `PPBT`).
- `PPC_ACCEL_BOOT_ENTRY=<u32>` sets PPC reset-trampoline branch target
  (default `0x00000000`, mailbox firmware primary entry).
- `PPC_ACCEL_BOOT_STACK=<u32>` sets PPC reset-trampoline stack pointer
  (default top of mapped PPC RAM minus `0x1000`).
- `PPC_ACCEL_BOOT_ARG0=<u32>` sets PPC reset-trampoline `r3` argument
  (default mailbox base `0x00001000`).

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
- Card advertises `Z2_BOOTROM` and publishes a DiagArea at `base+0x4000`
  with a name string and minimal 68k stubs:
  bootpoint stub at `0x0020` and diagpoint-compatible stub at `0x0032`.
- The built-in bootpoint stub writes `CONTROL=1` to `board_base+0x0008`
  (using `ConfigDev->cd_BoardAddr`) and returns.
- The built-in diagpoint stub writes `CONTROL=1` using `A0=BoardBase` and returns `D0=1`.
- PPC reset now uses a secondary-entry trampoline (`0xFFF00100`) that reads
  a boot descriptor at shared `+0x2040` and branches to descriptor `entry`.
- Shared-info `reserved0`/`reserved1` publish boot-descriptor offset/size.
- Runtime logs `boot marker=...` when trampoline transitions state.
- For legacy probe debugging, set `PPC_ACCEL_AC_TRACE=1` before launching emulator to log
  PPC board AutoConfig reads/writes.
- Runtime assets must be available (`qemu-uae.so`, firmware/config paths) and
  `LD_LIBRARY_PATH` should not include Python 2.7 build paths at runtime.
