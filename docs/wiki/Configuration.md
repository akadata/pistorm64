# Configuration

The emulator uses a config file to define memory maps, ROMs, devices, and platform features.
See `config_file/readme.md` for full syntax details.

## Map syntax

Example:
```
map type=rom address=0xF80000 size=0x80000 file=kick.rom ovl=0 id=kickstart autodump_mem
```

Key map types:
- `rom`: Read-only memory region (writes ignored or refused).
- `ram`: CPU-visible RAM (not DMA-capable for peripherals).
- `register`: I/O / register range.
- `ram_noalloc`: RAM backed by external buffer.
- `wtcram`: Write-through cache region (writes go to bus).

## Typical Amiga maps

- Kickstart ROM at `0x00F80000` (with `ovl=0` during overlay).
- Z2 fast at `0x00200000-0x009FFFFF` (8 MB).
- Z3 fast in high address space (e.g. `0x50000000+`).
- RTG framebuffer at `0x70010000+`.
- Optional PiSCSI DMA window at `0x60000000+`.

## Logging

Use the CLI to direct logs:
```
./emulator --log boot.log --debug-level debug
```

For heavy tracing only enable debug briefly, as it slows performance.

## Platform variables (setvar)

These are applied in the platform-specific config (e.g. `default.cfg` under the Amiga platform):

- `setvar enable_fc stub|cpld|off`
  - `stub`: enables FC shadow logging without driving hardware (default when set with no value).
  - `cpld`: enables FC mode intended for FC-capable CPLD bitstreams.
  - `off`: disables FC tracking.
- `setvar queue 0|1`
  - Controls the userspace batch queue (`PISTORM_ENABLE_QUEUE`).
- `setvar batch_bits <n>|true|false`
  - Sets `PISTORM_BATCH_BITS` (e.g., 2048). `true` defaults to 2048.

Common defaults (Amiga):
```
setvar queue true
setvar batch_bits true
```

## Kernel module parameters (pistorm.ko)

These are set when loading the kernel module (e.g. `modprobe pistorm <param>=<value>`):

- `gpclk_src` / `gpclk_div`
  - GPCLK0 source and divider (default `gpclk_src=5`, `gpclk_div=6` ~200 MHz on Pi3-class).
- `berr_reset_input=0|1`
  - `0`: GPIO5 is RESET output (legacy CPLD).
  - `1`: GPIO5 is treated as RESET/BERR input (FC/BERR CPLD).
- `run_batch_enable=0|1`
  - Enables the v2 batch ioctl (`PISTORM_IOC_RUN_BATCH`).

Example (stable Pi4 setup):
```
sudo modprobe pistorm gpclk_src=5 gpclk_div=6 berr_reset_input=1 run_batch_enable=1
```

You can verify module settings via:
```
modinfo pistorm
```

Typical `modinfo` fields to check:
```
parm: gpclk_src:GPCLK0 clock source (bcm2835 style: 5=PLLC, 6=PLLD, etc.) (uint)
parm: gpclk_div:GPCLK0 integer divider (default 6 ~200MHz on Pi3-class) (uint)
parm: berr_reset_input:Treat GPIO5 (RESET/BERR) as input to sample BERR when CPLD multiplexes it (bool)
parm: run_batch_enable:Enable PISTORM_IOC_RUN_BATCH v2 batch interface (bool)
```

## Userspace batch tuning (kmod backend)

Batching is compile-time gated by `PISTORM_ENABLE_BATCH`. When enabled, you can
cap the batch size at runtime using environment variables:

- `PISTORM_BATCH_BITS`
  - Batch size hint in bits. Values are clamped to a maximum of **2560**.
  - Examples: `64`, `128`, `256`, `512`, `1024`, `2048`, `2560`.
- `PISTORM_BATCH_OPS`
  - Directly sets the maximum number of ops per batch (overrides `PISTORM_BATCH_BITS`).

Example:
```
PISTORM_ENABLE_QUEUE=1 PISTORM_BATCH_BITS=2048 ./emulator
```
