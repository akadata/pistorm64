# PiStorm Amiga Memory Paths (Pi-side vs Amiga-side)

This document explains which address ranges are served locally on the Pi
(no GPIO/CPLD bus) versus which are forwarded to the real Amiga bus
via `ps_read_*`/`ps_write_*`.

## How the decision is made

1) `platform_read_check` / `platform_write_check` in `emulator.c`:
   - Handles platform/custom devices (PiSCSI, Pi-Net, RTG, Pi-AHI, PiStorm-dev).
   - These are Pi-side (no GPIO).

2) `handle_mapped_read/write` in `memory_mapped.c`:
   - Handles `map` entries from the active config file.
   - These are Pi-side (RAM/ROM/REGISTER) and do **not** touch GPIO.

3) Everything else:
   - Falls through to `ps_read_*` / `ps_write_*` (GPIO/CPLD/Amiga bus).

## Fixed Pi-side ranges (custom devices)

These are always serviced locally when enabled:

| Range | Path | Notes |
| --- | --- | --- |
| `0x70000000..0x72810000` | Pi-side | RTG window (`PIGFX_RTG_BASE`..`PIGFX_UPPER`) |
| `0x80000000..0x80010000` | Pi-side | PiSCSI regs (`PISCSI_OFFSET`..`PISCSI_UPPER`) |
| `0x80010000..0x80020000` | Pi-side | Pi-Net regs (`PINET_OFFSET`..`PINET_UPPER`) |
| `0x88000000..0x8A000000` | Pi-side | Pi-AHI (`PI_AHI_OFFSET`..`PI_AHI_UPPER`) |
| `pistorm_dev_base..+0x10000` | Pi-side | PiStorm-dev Z2 device (base assigned during Autoconf) |
| `0xE80000..0xE80000+AC_SIZE` | Pi-side | Z2 Autoconf space (`AC_Z2_BASE`) |
| `0xFF000000..0xFF000000+AC_SIZE` | Pi-side | Z3 Autoconf space (`AC_Z3_BASE`) |

Notes:
- Autoconf assigns `pistorm_dev_base`, Z2 Fast, and Z3 Fast at runtime.
- Assigned bases are printed at startup (see `LOG_INFO` in `platforms/amiga/amiga-platform.c`
  and `platforms/amiga/amiga-autoconf.c`).

## Config-driven Pi-side ranges (mapped RAM/ROM/register)

Any `map` entry in the active config file becomes Pi-side:

```
map type=ram address=0x08000000 size=256M
map type=rom address=0xF80000 size=0x80000 file=kick512.rom ovl=0
map type=register address=0xD80000 size=0x70000
```

These are handled by `handle_mapped_read/write` in `memory_mapped.c` and
do not touch GPIO/CPLD.

Active config file:
- Passed via `--config-file`, or
- `default.cfg` if present, otherwise an empty config.

### Current config: `myamiga3.cfg`

These entries are Pi-side in your setup:

```
map type=rom address=0xF80000 size=0x80000 file=Kickstart-v3.1-r40.068.rom id=kickstart
map type=ram address=0x10000000 size=32M  id=z3_autoconf_fast
map type=ram address=0x08000000 size=128M id=cpu_slot_ram
map type=ram address=0x200000 size=8M id=z2_autoconf_fast
```

## Amiga-side (GPIO/CPLD) ranges

Everything **not** matched by the above is Amiga-side and goes through:

```
ps_read_8 / ps_read_16 / ps_read_32
ps_write_8 / ps_write_16 / ps_write_32
```

On an A500:
- Chip RAM and custom chip accesses are 16-bit bus cycles.
- 32-bit accesses are two 16-bit cycles.

## Where to look in code

- `emulator.c`: `platform_read_check`, `platform_write_check`
- `memory_mapped.c`: `handle_mapped_read`, `handle_mapped_write`
- `platforms/amiga/amiga-platform.c`: custom range setup and Autoconf
