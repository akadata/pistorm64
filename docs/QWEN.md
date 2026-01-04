# PiStorm canonical notes (for Qwen/Codex)

Use this as the ground truth when reasoning about the bus, clocks, and guards. Mirrors `docs/bus_interface_notes.md` and related tech notes.

## Bus architecture
- Data bus: raw 16-bit, always present on GPIO 8–23 ↔ D0–D15. Direction controlled via CPLD; never multiplexed to preserve DMA/UDS/LDS timing.
- Address bus: multiplexed over two register writes using GPIO2/3 as register select:
  - `REG_ADDR_LO` write latches A1–A16 via D0–D15.
  - `REG_ADDR_HI` write latches A17–A23 (+ flags) via D0–D15.
  - CPLD reconstructs full 24-bit address; avoids needing 23 GPIO lines.
- Control lines: PI_RD/PI_WR, TXN_IN_PROGRESS, AS/UDS/LDS, DTACK, FC[2:0], RESET/HALT, E/VMA, BR/BG/BGACK, IPL. CPLD arbitrates and ensures correct direction and timing.

## Clocking
- GPCLK0 on GPIO4 drives CPLD at high frequency: nominal 200 MHz; some bitstreams/parts can run at ~125 MHz or overclocked (>200 MHz) on Pi Zero2/3/4/400. Choose frequency to match the flashed bitstream.
- CPLD oversamples ~7.09 MHz Amiga bus, inserts wait-states with ~5 ns resolution.

## Platform scope
- Primary targets: Pi Zero 2 W / Pi3-class, Pi4/400. Pi5/RP1 is currently out-of-scope unless a dedicated `PLATFORM=PI5_RP1_64BIT` path is added.

## Constraints/projects
- Quartus 20.1 projects and SDC assume PI_CLK 5 ns (200 MHz) and M68K_CLK ~141 ns. Bitstreams: Amiga (Oct 2021, USERCODE 0x00185866) vs Atari (Dec 2023+, USERCODE 0x0017F4B8).

## Emulation/JIT notes (headlines)
- Keep chip/shared RAM uncached when DMA (blitter/Copper/Paula) is active; flush/disable WTC-like caches to avoid corruption.
- Optional RTG (raylib) can be disabled via `USE_RAYLIB=0` (Makefile).
- Use core pinning for emulator vs I/O threads to reduce jitter.
