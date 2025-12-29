# Legacy PiStorm GPIO backend (BCM `/dev/mem` MMIO)

This documents the **pre–Pi 5** GPIO path where the 40‑pin header GPIO was driven by the SoC’s **BCM GPIO** block and accessed via **`/dev/mem` + MMIO**.

Source of truth is `gpio/ps_protocol.h` and the `#else` (non‑`PISTORM_RP1`) parts of `gpio/ps_protocol.c`.

## 1) What the legacy backend does

- Maps a BCM peripheral window from `/dev/mem` and drives GPIO by writing/reading BCM GPIO registers:
  - `GPFSELn` (function select / direction)
  - `GPSET0` (set outputs high)
  - `GPCLR0` (set outputs low)
  - `GPLEV0` (sample input levels)
- Implements the PiStorm handshake by:
  - driving address/data onto GPIO lines,
  - pulsing `WR`/`RD`,
  - waiting for a CPLD “transaction in progress” line to deassert,
  - sampling data from the GPIO level register.

## 2) MMIO mapping and register usage (legacy)

- Peripheral base/constants live in `gpio/ps_protocol.h`:
  - `BCM2708_PERI_BASE` + `BCM2708_PERI_SIZE`
  - `GPIO_ADDR` (`0x200000`)
  - `GPCLK_ADDR` (`0x101000`)
- Mapping logic lives in `gpio/ps_protocol.c` in `setup_io()` (non‑RP1 path):
  - `gpio = map + GPIO_ADDR/4`
  - `gpclk = map + GPCLK_ADDR/4`
- BCM GPIO register indices used by the protocol:
  - `*(gpio + 0..2)` → `GPFSEL0..2`
  - `*(gpio + 7)` → `GPSET0`
  - `*(gpio + 10)` → `GPCLR0`
  - `*(gpio + 13)` → `GPLEV0`

## 3) Table A — GPIO assignment (legacy)

These are **BCM GPIO numbers** as used in the source (bit positions in `GPSET0/GPCLR0/GPLEV0`).

Header pin numbers are **not present in the code**, so they are intentionally omitted here.

| BCM GPIO | Symbol | Role | Direction (effective) | Active level | Where in code |
|---:|---|---|---|---|---|
| 0 | `PIN_TXN_IN_PROGRESS` | Handshake input: “transaction in progress” | Input | High = busy (wait loop spins while high) | `gpio/ps_protocol.h` + wait loops in `gpio/ps_protocol.c` |
| 1 | `PIN_IPL_ZERO` | Handshake input: IPL0 indicator | Input | High/low sampled (no inversion in code) | `gpio/ps_protocol.h`, `ps_get_ipl_zero()` |
| 2 | `PIN_A0` | Register select bit 0 | Output | High asserted during reg selection | `gpio/ps_protocol.h` macros write `(reg << PIN_A0)` |
| 3 | `PIN_A1` | Register select bit 1 | Output | High asserted during reg selection | `gpio/ps_protocol.h` |
| 4 | `PIN_CLK` | GPCLK0 output for PiStorm clock | ALT0 (GPCLK0) | Clock waveform | `setup_gpclk()` + `SET_GPIO_ALT(PIN_CLK,0)` |
| 5 | `PIN_RESET` | Defined but not actively driven by the legacy protocol | Input in GPFSEL tables | N/A | `gpio/ps_protocol.h` (not toggled in legacy code) |
| 6 | `PIN_RD` | Read strobe | Output | Pulse high then low | `GPIO_PIN_RD` / `ps_read_*()` |
| 7 | `PIN_WR` | Write strobe | Output | Pulse high then low | `GPIO_WRITEREG` / `ps_write_*()` |
| 8..23 | `PIN_D(0..15)` | 16‑bit data bus (also carries reg writes + address words) | Output during writes; input during reads | Data bits (no inversion in code) | `PIN_D(x)` macro; `GPFSEL_OUTPUT` vs `GPFSEL_INPUT` |

### Direction details from the actual GPFSEL tables

The legacy backend uses precomputed `GPFSEL*_INPUT` and `GPFSEL*_OUTPUT` constants (in `gpio/ps_protocol.h`) rather than per‑pin `INP_GPIO/OUT_GPIO`.

Decoded from those constants:

- **“INPUT mode” (`GPFSEL*_INPUT`)** configures:
  - outputs: GPIO2, GPIO3, GPIO6, GPIO7
  - alt0: GPIO4 (GPCLK0)
  - inputs: GPIO0,1,5,8..29
- **“OUTPUT mode” (`GPFSEL*_OUTPUT`)** configures:
  - outputs: GPIO2, GPIO3, GPIO6, GPIO7, GPIO8..GPIO23
  - alt0: GPIO4 (GPCLK0)
  - inputs: GPIO0,1,5,24..29

This matches the “control pins always outputs, data bus turns around for reads” design.

## 4) Table B — protocol sequencing (legacy)

This section describes what the **legacy** backend does (non‑RP1 path) in `gpio/ps_protocol.c` and helper macros in `gpio/ps_protocol.h`.

### `ps_write_16(address, data)`

1. Configure GPIO into “OUTPUT mode” (`GPFSEL_OUTPUT`).
2. Write `REG_DATA`:
   - Drive `(data << 8) | (REG_DATA << PIN_A0)` onto pins.
   - Pulse `PIN_WR` high then low.
   - Clear protocol outputs (`0xFFFFEC` mask via `GPCLR0`).
3. Write `REG_ADDR_LO`:
   - Drive `(address & 0xFFFF)` similarly.
   - Pulse `PIN_WR`; clear outputs.
4. Write `REG_ADDR_HI`:
   - Drive `(address >> 16)` with mode bits (`0x0000 | (address >> 16)`).
   - Pulse `PIN_WR`; clear outputs.
5. Switch to “INPUT mode” (`GPFSEL_INPUT`) for bus turn‑around.
6. Wait for handshake:
   - `while (GPLEV0 & (1 << PIN_TXN_IN_PROGRESS)) {}` (no timeout).

### `ps_read_16(address)`

1. Configure GPIO into “OUTPUT mode” (`GPFSEL_OUTPUT`).
2. Write `REG_ADDR_LO` (pulse `WR`; clear outputs).
3. Write `REG_ADDR_HI` with read opcode bits (`0x0200 | (address >> 16)`) (pulse `WR`; clear outputs).
4. Switch to “INPUT mode” (`GPFSEL_INPUT`) for bus turn‑around.
5. Select `REG_DATA` and assert `RD`:
   - Drive `(REG_DATA << PIN_A0)` (A0/A1 select).
   - Set `PIN_RD` high.
6. Wait for handshake:
   - `while (GPLEV0 & (1 << PIN_TXN_IN_PROGRESS)) {}` (no timeout).
7. Sample data:
   - `value = GPLEV0;`
   - return `(value >> 8) & 0xFFFF`
8. End transaction:
   - clear outputs with `GPCLR0` mask `0xFFFFEC`.

### Status + reset helpers

- `ps_write_status_reg(value)` writes `REG_STATUS` using the same “drive bus + pulse WR” pattern.
- `ps_read_status_reg()` reads `REG_STATUS` using the same “select reg + assert RD + wait + sample” pattern.
- `ps_reset_state_machine()`:
  - `ps_write_status_reg(STATUS_BIT_INIT); usleep(1500); ps_write_status_reg(0); usleep(100);`
- `ps_pulse_reset()`:
  - `ps_write_status_reg(0); usleep(100000); ps_write_status_reg(STATUS_BIT_RESET);`

## 5) Table C — timings (legacy)

The legacy backend has **no explicit timeout constants** for handshake waits; busy‑waits are unbounded.

| Timing / delay mechanism | Default value | Unit | Used by | What it does |
|---|---:|---|---|---|
| `usleep(10)` / `usleep(100)` | 10 / 100 | µs | `setup_gpclk()` | Spacing while reprogramming GPCLK0 registers |
| GPCLK busy‑bit polling loops | none | N/A | `setup_gpclk()` | Waits for clock generator state transitions (no timeout) |
| `asm("nop"); asm("nop");` | 2 nops | cycles | inline helpers in `gpio/ps_protocol.h` | Very short setup/hold delay between address programming and data sampling |
| `CHIP_FASTPATH` extra pulses | build‑time | N/A | `ps_write_status_reg()`, `ps_read_status_reg()` | Adds extra repeated `RD`/`WR` writes as a crude delay |
| `usleep(1500)` | 1500 | µs | `ps_reset_state_machine()` | Hold INIT long enough for CPLD state machine |
| `usleep(100)` | 100 | µs | `ps_reset_state_machine()` | Spacing after clearing INIT |
| `usleep(100000)` | 100000 | µs | `ps_pulse_reset()` | Reset pulse width |
| `while (GPLEV0 & (1<<PIN_TXN_IN_PROGRESS)) {}` | unbounded | N/A | most `ps_*` ops | Wait for CPLD handshake completion (can hang forever if stuck) |

## 6) Fast extraction commands (in-tree)

`ripgrep (rg)` is not always installed on Alpine; these `grep` equivalents work everywhere:

```sh
grep -RIn "/dev/mem\\|mmap(" gpio/ gpio/ps_protocol.*
grep -RIn "BCM2708_PERI_BASE\\|GPIO_ADDR\\|GPCLK_ADDR" gpio/ps_protocol.h
grep -RIn "GPFSEL\\|GPSET\\|GPCLR\\|GPLEV" gpio/ps_protocol.h gpio/ps_protocol.c
grep -RIn "PIN_TXN_IN_PROGRESS\\|PIN_IPL_ZERO\\|PIN_A0\\|PIN_A1\\|PIN_CLK\\|PIN_RD\\|PIN_WR\\|PIN_D" gpio/ps_protocol.h
grep -RInE "usleep\\(|asm\\(\"nop\"\\)|CHIP_FASTPATH" gpio/ps_protocol.h gpio/ps_protocol.c
```

