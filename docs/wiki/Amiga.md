# Amiga Platform

PiStorm64 on Amiga focuses on the 68k bus engine, FC/BERR plumbing, and Z2/Z3
memory/RTG features. The emulator runs on the Pi and exposes devices to the
Amiga via the CPLD and Zorro bus.

## Kernel module (pistorm.ko)

Stable Pi4 setup (current default clocking, ~200 MHz GPCLK0 from PLLC):
```
sudo modprobe pistorm gpclk_src=5 gpclk_div=6 berr_reset_input=1 run_batch_enable=1
```

## Emulator launch

### Non-JIT (Musashi, default)

Enable the batching queue:
```
PISTORM_ENABLE_QUEUE=1 ./emulator
```

Optional batch hint (default 2048):
```
PISTORM_ENABLE_QUEUE=1 PISTORM_BATCH_BITS=2048 ./emulator
```
Batch hints are capped at **2560** for stability; higher values may regress
performance.

### UAE JIT (experimental)

Build and run the UAE JIT backend:
```
make USE_UAE_JIT=1 uae-jit
PISTORM_ENABLE_QUEUE=0 ./emulator --jit
```

Keep the initial JIT bring-up config minimal (ROM + chip RAM + platform) and
add devices incrementally. See `UAE-JIT-Status.md` for the current state and
known limitations.

## Notes

- FC is treated as a full 3‑bit value end‑to‑end (0–7).
- BERR is multiplexed onto GPIO5 when `berr_reset_input=1`.
- Pi4 stability is currently best at GPCLK div=6 (~200 MHz).
- Current testing coverage: Pi4 8 GB and Pi Zero 2 W.
- UAE JIT backend is experimental; see `UAE-JIT-Status.md` for current bring-up
  state and known limitations.

Only the Amiga Makes it Possible.
