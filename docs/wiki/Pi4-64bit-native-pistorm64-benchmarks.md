# PiStorm64 – Pi 4 64-bit Native Build Notes

## Overview

This document records a **known-good, high-performance configuration** for PiStorm64 on a Raspberry Pi 4 using:

* 64-bit userspace and kernel
* Musashi 68030 / 68040 core (non-JIT baseline)
* Kernel module backend (`/dev/pistorm`)
* RTG via raylib + DRM/KMS
* Real Zorro II/III bus timing emulation

The target Amiga in these tests is an A500 with PiStorm64 and 3.1 ROM.

---

## Host platform

Raspberry Pi 4B:

* CPU: Cortex-A72, 4 cores
* RAM: 8 GB
* OS: 64-bit Raspberry Pi OS with a custom kernel built `-march=native`
* PiStorm kernel module built against the same native kernel

Key runtime observations (example run under heavy load):

```text
top:
  ~200% CPU in emulator (cpu + ipl threads)
  load average ≈ 3.4
  MEM used ≈ 2.4 GiB of 7.5 GiB
  Swap: 0

vcgencmd measure_temp:
  temp ≈ 55–57°C under sustained load
```

---

## Firmware / overclock configuration

`/boot/firmware/config.txt` (Pi 4 section):

```ini
[all]
arm_64bit=1

[pi4]
kernel=kernel8-usb3-ssd.img
initramfs initramfs8-usb3-ssd.img followkernel
auto_initramfs=1
enable_uart=0

gpu_mem=256

# Turbo / OC
arm_boost=1
arm_freq=2150
force_turbo=1
over_volt=5
temp_limit=83

# KMS for RTG output
dtoverlay=vc4-kms-v3d,cma-512
#dtoverlay=vc4-fkms-v3d

# Framebuffer for PiGFX / RTG
hdmi_force_hotplug=1
hdmi_drive=2
framebuffer_width=1920
framebuffer_height=1080
framebuffer_depth=32
framebuffer_ignore_alpha=0

# Misc
initial_turbo=70
#dtparam=audio=off
```

Kernel command line (relevant bits):

```text
... cgroup_disable=memory nvme.max_host_mem_size_mb=0 \
video=HDMI-A-1:3840x2160M@30D,margin_left=48,margin_right=48,margin_top=48,margin_bottom=48 \
numa=fake=2 mitigations=off ...
```

Note: `numa=fake=2` can be removed from `/boot/firmware/cmdline.txt` for a simpler topology; the tests shown here were already good with fake NUMA present.

---

## Build configuration

All tests below use a **native 64-bit build** with LTO and the **default (BFD) linker** – Gold is disabled.

```sh
make clean

make PLATFORM=PI4_64BIT_NATIVE \
     USE_LTO=1 \
     USE_GOLD=0 \
     CC=gcc CXX=g++ \
     full -j4
```

Warnings are gated behind `VERBOSE=1` to keep normal builds quiet:

* Default: `WARNINGS = -w`
* Debug runs: `make VERBOSE=1` expands to `-Wall -Wextra -pedantic ...` etc.

Musashi is built with:

* `INLINE_INTO_M68KCPU_H=1`
* `PISTORM_ENABLE_BATCH=1`
* `PISTORM_IPL_RATELIMIT_US=100`
* `PISTORM_USE_DIRECT_OPS=0`
* `PISTORM_EXPERIMENT_PMMU` (040 MMU support compiled in, PFLUSHA currently logged as unhandled)

---

## Emulator launch configuration

Typical launch for benchmarking:

```sh
./emulator \
  --loopcycles 400 \
  --rtprio cpu=95,ipl=95,keyboard=50 \
  --affinity cpu=2,ipl=3,keyboard=1
```

These benchmarks use the non-JIT Musashi core. The UAE JIT backend is
available for bring-up (`make USE_UAE_JIT=1 uae-jit`, `./emulator --jit`) but is
not part of the validated benchmark path yet. See `UAE-JIT-Status.md` for the
current state.

The config file used in these tests sets:

```text
CPU type: 68040 (68030 also tested)
CPU loop cycles: 1–4096 depending on test
IPL NOP count: 8

Core affinity:
  cpu  -> core 2
  ipl  -> core 3
  kbd  -> core 1

Scheduling:
  cpu  -> SCHED_RR, priority 95
  ipl  -> SCHED_RR, priority 95
  kbd  -> SCHED_RR, priority 50
```

RT priorities require either `sudo` or appropriate `rtprio` limits in `/etc/security/limits.conf`:

```text
smalley   soft   rtprio   95
smalley   hard   rtprio   95
```

---

## Memory map (Musashi ranges)

On a typical run:

```text
ROM:        00F80000-00FFFFFF  (Kickstart 3.1, 512 KB)  [MAP 0]
CPU slot:   08000000-0FFFFFFF  (128 MB)                 [MAP 1]
Z3 fast:    10000000-1FFFFFFF  (256 MB)                 [MAP 2]
PiSCSI DMA: 60000000-60FFFFFF  (16 MB)                  [MAP 3]
Z3 fast:    D0000000-DFFFFFFF  (256 MB)                 [MAP 4]
Z2 fast:    00200000-009FFFFF  (8 MB)                   [MAP 5]
Autoconf:   00C00000-00CFFFFF, 00DC0000-00DCFFFF        [MAP 6,7]
Z3 fast:    50000000-5FFFFFFF  (256 MB)                 [MAP 4 W]
Z3 fast:    60000000-6FFFFFFF  (256 MB)                 [MAP 5 W]
```

Zorro device wiring (example log):

```text
Z2 serial echo     -> $00E00000
PiSCSI Z2          -> $00EA0000
A314               -> $00EB0000
PiStorm Z2 (host)  -> $00EC0000
Z2 RAM (8 MB)      -> $00200000-00A00000
Z3 autoconf device -> $40000000
Z3 RAM (256 MB)    -> $50000000 [W]
Z3 RAM (256 MB)    -> $60000000 [W]
```

---

## Performance results

### SysInfo (68040, loopcycles 400, native build, LTO, no Gold)

Representative numbers:

* Dhrystones: ~64.7k – 65.0k
* Relative to A600: ~121–123× A600
* MIPS: ~67–68 MIPS
* MFLOPS: ~28–30 MFLOPS

For 68030 core:

* Dhrystones: ~64.2k
* MIPS: ~68.1 MIPS
* MFLOPS: ~28.8 MFLOPS

These are achieved with full device stack active (PiSCSI, Zorro, RTG, A314, etc.), not a stripped-down synthetic build.

### Bustest – bus bandwidth

All tests `bustest chip fast rom` with:

* Buffer: 262,144 bytes
* Alignment: 32,768
* Address window: around `$0866_8000` (fast) and `$0003_8000` (chip)
* Clean native kernel + module build

Representative run (fast/chip/rom, size=256k):

```text
memtype  addr       op     cycle      bandwidth
------------------------------------------------------------
fast   $086668000  readw   28.0 ns   71.5 * 10^6  byte/s
fast   $086668000  readl   24.9 ns  160.7 * 10^6  byte/s
fast   $086668000  readm   15.3 ns  264.9 * 10^6  byte/s

fast   $086668000  writew  24.9 ns   81.5 * 10^6  byte/s
fast   $086668000  writel  23.5 ns  170.6 * 10^6  byte/s
fast   $086668000  writem  18.3 ns  218.0 * 10^6  byte/s

chip   $0003B8000  readw 2035.5 ns    1.0 * 10^6   byte/s
chip   $0003B8000  readl 3966.2 ns    1.0 * 10^6   byte/s
chip   $0003B8000  readm 4064.6 ns    1.0 * 10^6   byte/s
chip   $0003B8000  writew 1566.8 ns   1.3 * 10^6   byte/s
chip   $0003B8000  writel 3133.2 ns   1.3 * 10^6   byte/s
chip   $0003B8000  writem 3120.7 ns   1.3 * 10^6   byte/s

rom    $00F80000   readw   26.0 ns   76.8 * 10^6  byte/s
rom    $00F80000   readl   23.1 ns  173.4 * 10^6  byte/s
rom    $00F80000   readm   15.2 ns  262.3 * 10^6  byte/s
```

Key points:

* Fast RAM burst (readm/writem): ~265 MB/s read, ~218 MB/s write
* Chip RAM: ~1.0–1.3 MB/s, matching an A500-class chipset correctly throttled
* ROM: up to ~260 MB/s for `readm` when mapped to fast and cached

Compared against the classic accelerator list (Warp1260, TF1260, Blizzard 1260, etc.), this places PiStorm64’s fast RAM bandwidth in the same league as high-end 060 boards, while running on an A500 with a non-JIT interpreter.

### Large-buffer bustest (cache-stress)

Runs with `size=32768k` and `size=262144k` confirm that:

* Bandwidth remains very stable even when the host’s cache is heavily stressed.
* The Pi is sustaining heavy load: CPU threads near 200% combined, with `perf` showing:

```text
cpu thread:
  ~2.1 GHz
  ~1.9 instructions per cycle
  ~7.5e9 L1-dcache-loads / 5s
  ~0.2% L1-dcache-load-misses
```

The emulator is compute-bound, not I/O bound; storage shows minimal activity during these tests.

---

## Comparison vs published Emu68 numbers

From the public Emu68 A600 benchmarks (fannkuch, BinaryTree, SciMark), Emu68 typically claims:

* 3.8–5.0× 68060/50 on fannkuch/binary-tree
* ~10× 68060/50 on SciMark MFLOPS

In this PiStorm64 setup:

* Dhrystones and MIPS are in the same rough performance envelope as many 68060 accelerators, despite:

  * full device stack enabled
  * no JIT
  * conservative loopcycles to keep bus timing accurate

The key takeaway: **Musashi 64-bit on a tuned Pi 4 with proper threading, affinity, and RT priorities is nowhere near “too slow for 040+MMU”**. With careful host tuning it comfortably reaches and exceeds the level of several classic accelerators.

---

## Open questions / next steps

* Re-measure with:

  * `PLATFORM=PI4_64BIT_NATIVE` plus `-march=armv8.2-a+crypto` kernel/userspace where safe
  * different `loopcycles` values to explore the speed vs bus-accuracy tradeoff
* Document a standard “benchmark profile” config for reproducible comparisons:

  * exact Kickstart image
  * Workbench / SysInfo versions
  * PiSCSI layout and filesystem
* Optional: add a short script that:

  * launches emulator with a fixed config
  * runs a SysInfo Dhrystone pass and `bustest fast chip rom`
  * logs results to a CSV for tracking changes between commits
