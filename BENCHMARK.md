# Benchmark (PiStorm GPIO Bus)

This tool measures PiStorm bus throughput and per-transaction wait timing
against Amiga memory regions (chip/fast/z2/z3). It is intended to help
separate GPCLK setup issues from GPIO/handshake latency.

## Build

```
gcc -O0 -Wall -Wextra -I./ benchmark.c gpio/ps_protocol.c gpio/rpi_peri.c -o benchmark
```

## Basic Usage

Run against 1MB chip RAM (A500 default):

```
sudo ./benchmark --chip-kb 1024 --repeat 3
```

Add regions by base/size (size in KB):

```
sudo ./benchmark --chip-kb 1024 \
  --region fast:0x200000:8192 \
  --region z2:0x60000000:8192 \
  --region z3:0x40000000:65536 \
  --repeat 3
```

## Burst Mode (GPFSEL churn)

Burst mode keeps GPIO direction stable during a block of transactions.
This helps isolate per-transaction GPIO direction switching overhead.

By default, the tool tests burst sizes: `1`, `4`, `16`, `64`.
To test a single burst size:

```
sudo ./benchmark --chip-kb 1024 --repeat 3 --burst 16
```

## Pacing (loopcycles-like delay)

Add a fixed delay (microseconds) after each transaction to emulate
emulator pacing effects:

```
sudo ./benchmark --chip-kb 1024 --repeat 3 --pacing-us 5 --pacing-mode burst --pacing-kind spin
```

## Pacing Sweep

Sweep pacing values and report throughput with wait statistics:

```
sudo ./benchmark --chip-kb 1024 --repeat 3 --wait-sample 16 --pacing-sweep 0:50:2 --sweep-burst 16 --pacing-mode burst --pacing-kind spin
```

Output columns:

```
pacing_us w16 r16 w32 r32 r32_16eq wait_p95 wait_max txns16 txns32 exp_delay_us16 exp_delay_us32
```

Note: `--pacing-kind spin` avoids scheduler jitter but burns CPU.
Higher pacing values add a delay per transaction/burst, so sweeps can take
several minutes. Reduce `--chip-kb` or `--repeat` if needed.

## Wait Timing Sampling

Per-transaction wait timing uses `clock_gettime`, which adds overhead.
Use `--wait-sample N` to time only every Nth transaction.

```
sudo ./benchmark --chip-kb 1024 --repeat 3 --wait-sample 16
```

## Smoke Mode (crash localization)

Runs a small, fixed test intended to find where crashes occur:

- chip size: 64 KB
- burst: 16
- ops: `r16` then `w16`

```
sudo ./benchmark --smoke
```

If it crashes, the tool prints the last phase before exiting non-zero.

## Memory Test (chip/fast/z2/z3)

Runs a functional memory test over the selected region(s):

- address test
- walking 1s/0s
- fixed patterns: `0x0000`, `0xFFFF`, `0xAAAA`, `0x5555`, `0xA5A5`, `0x5A5A`
- random pattern (seed printed)

```
sudo ./benchmark --memtest --chip-kb 1024
```

Use `--region name:base:size_kb` to test other regions.

## Output Explained

Example:

```
[REG] chip     burst=16 base=0x000000 size=1024 KB | w8=1.10 r8=0.97 w16=2.23 r16=2.04 w32=2.23 r32=2.14 MB/s (sink=0x00000000)
       wait_us: w8[avg=3.10 p95=4 max=12] r8[avg=3.40 p95=5 max=18] w16[avg=2.20 p95=3 max=10] r16[avg=2.40 p95=3 max=11] w32[avg=2.30 p95=3 max=12] r32[avg=2.50 p95=4 max=14]
```

Fields:

- `w8/r8/w16/r16/w32/r32 MB/s`: throughput for 8/16/32-bit accesses.
- `wait_us`: time spent waiting for `TXN_IN_PROGRESS` to clear.
  - `avg`: average wait (microseconds)
  - `p95`: 95th percentile
  - `max`: worst case seen

## Notes

- `sudo` is required for `/dev/mem` access.
- The emulator must not be running during this benchmark.
- If you need to limit runtime, reduce `--chip-kb` or use fewer regions.
