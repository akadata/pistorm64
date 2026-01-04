# PiStorm agent quick-start (Codex/Qwen)

Purpose: minimal, agent-friendly summary of bus/clock/guard rails. Treat as authoritative alongside `docs/bus_interface_notes.md` and `docs/EMP240.md`.

## Bus model
- Data bus: direct 16-bit on GPIO 8–23 ↔ D0–D15. Not multiplexed. CPLD controls direction; required for DMA correctness.
- Address bus: multiplexed via register selects on GPIO2/3:
  - `REG_ADDR_LO` (PI_WR): latch A1–A16 from D0–D15.
  - `REG_ADDR_HI` (PI_WR): latch A17–A23 (+ flags) from D0–D15.
  - CPLD reconstructs full address.
- Control/timing: TXN_IN_PROGRESS asserted on addr-low write; CPLD sequences AS/UDS/LDS/RW/FC and DTACK handshake. RESET/HALT driven from STATUS_BIT_INIT. E/VMA generated in CPLD. IPL sampled and exposed in STATUS.

## Clocking
- GPCLK0 on GPIO4 feeds CPLD. Nominal 200 MHz; some bitstreams/parts allow ~125 MHz or >200 MHz (Pi Zero2/3/4/400 overclock). Match bitstream expectations.
- Constraints assume PI_CLK 5 ns, M68K_CLK ~141 ns. Quartus 20.1 projects in `rtl/`.

## Platform scope
- Supported focus: Pi Zero 2 W/3 class and Pi4/400. Pi5/RP1 pending dedicated platform path.

## Build toggles
- `USE_RAYLIB=0` to drop RTG raylib backend.
- `BLITTER_ENABLED` (Atari fork) toggles faux blitter compile.
- CPU tuning via `PLATFORM=` (PI4/PI4_64BIT/PI3_BULLSEYE/ZEROW2_64), `OPT_LEVEL`, `USE_GOLD`.

## DMA/cache cautions
- Keep chip/shared RAM uncached when DMA (blitter/Copper/Paula) is active; flush/disable host-side caches touching shared RAM to avoid corruption.
- Ensure GPCLK is stable; do not rely on bcm2835 helpers for 200 MHz clock—use direct /dev/mem pokes.

## Firmware notes
- Amiga bitstream (Oct 2021) USERCODE 0x00185866; Atari bitstreams (Dec 2023+) USERCODE 0x0017F4B8 with hold/status fixes. Rebuild via Quartus from `pistorm.v` (Amiga) or `pistormSXB_devEPM240.v`/`EPM570` (Atari).
