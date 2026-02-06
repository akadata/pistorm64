# Configuration

The emulator uses a config file to define memory maps, ROMs, devices, and platform features.
See `src/config_file/readme.md` for full syntax details.

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

Common map options:
- `range=LOW-HIGH` as an alternative to `address` + `size`.
- `ovl=<addr>` for ROM overlay mirroring (e.g., Kickstart at `$00000000`).
- `autodump_mem` / `autodump_file` to dump ROM contents when the file is missing.

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

## CPU/runtime control

The config file supports core CPU tuning and runtime scheduling:

- `cpu <type>` (e.g., `cpu 68020`).
- `loopcycles <n>` to control how many 68k cycles run before service checks.
- `affinity <spec>` to set thread affinity (e.g., `cpu=3,ipl=2,keyboard=1,mouse=1`).
- `rtprio <spec>` to set SCHED_RR priorities (requires `CAP_SYS_NICE` or
  `RLIMIT_RTPRIO`).

You can also set these from the CLI:

```
./emulator --loopcycles 400 --affinity cpu=2,ipl=3 --rtprio cpu=80,ipl=70
```

## Input routing (Amiga)

These config commands set up keyboard/mouse forwarding:

- `keyboard {grab_key} {grab|nograb} {autoconnect|noautoconnect}`
- `kbfile <event_device>`
- `mouse <event_device> {grab_key} {grab|nograb} {autoconnect|noautoconnect}`

## JIT vs non-JIT

- **Non-JIT (default)**: Musashi core, launched with `./emulator`.
- **UAE JIT (experimental)**: build with `make USE_UAE_JIT=1 uae-jit` and run
  `./emulator --jit` (`--jit-fpu` for the FPU JIT). See `UAE-JIT-Status.md` for
  the current bring-up state.

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
  - GPCLK0 source and divider (default `gpclk_src=5`, `gpclk_div=6` → ~200 MHz
    on Pi4 with PLLC at 1.2 GHz).
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
