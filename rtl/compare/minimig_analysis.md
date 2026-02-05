# Minimig Version Analysis: Comparison with Other PiStorm Variants

## Overview
This document analyzes the Minimig version of the PiStorm firmware and compares it with the other three variants: Original (Niklas Ekström), Amiga, and Atari.

## Minimig Version Characteristics

### Interface
- Uses the same PI_A/PI_D interface as Amiga and Atari versions
- Same register-based parameter system (REG_DATA, REG_ADDR_LO, etc.)
- PI_TXN_IN_PROGRESS and PI_IPL_ZERO for status signals

### Clock System
- Uses direct M68K_CLK as c7m (no clock derivation logic)
- No option for C1/C3 clock generation like in Amiga version
- Uses c7m clock directly instead of derived clock

### Unique Features
- Implements a unique state machine using S0-S7 wire definitions:
  ```verilog
  wire S0 = state == 2'd0 && c7m && !wait_req;
  wire Sr = state == 2'd0 && wait_req;
  wire S1 = state == 2'd1 && !c7m;
  // ... and so on
  ```
- Uses a 2-bit state system instead of 3-bit (compared to others)
- Has a wait_req mechanism for operation requests
- Implements a different approach to state transitions using clock edges

## Comparison with Original (Niklas Ekström) Version

### Similarities:
- Maintains the same basic module interface structure
- Keeps similar latch control signals (LTCH_A_*, LTCH_D_*)
- Preserves the 7MHz clock system concept

### Differences:
- Changes from PI_SA/PI_SD interface to PI_A/PI_D interface
- Removes PI_AUX0/PI_AUX1 signals
- Implements different state machine architecture
- Removes complex bus arbitration logic present in original

## Comparison with Amiga Version

### Similarities:
- Same PI_A/PI_D interface
- Same register parameter system
- Similar basic latch control signals
- Both use 2-bit state system

### Key Differences:
- **Clock handling**: Minimig uses direct M68K_CLK, Amiga has clock derivation options
- **State machine**: Minimig uses wire-based state definitions (S0-S7), Amiga uses traditional case statements
- **Operation flow**: Minimig has wait_req/wait_dtack mechanism, Amiga has simpler flow
- **Reset logic**: Different implementation approaches
- **Signal declarations**: Amiga uses `output reg` while Minimig also uses `output reg` consistently

## Comparison with Atari Version

### Major Differences:
- **Complexity**: Atari is significantly more complex with advanced bus arbitration
- **Clock system**: Atari uses 8MHz system, Minimig uses direct 7MHz from M68K_CLK
- **State machine**: Atari has enhanced 3-bit state machine with defined constants, Minimig has 2-bit wire-based states
- **Features**: Atari includes "long hold" feature, Minimig does not
- **Bus arbitration**: Atari has complex delay registers, Minimig has simpler approach
- **Output declarations**: Atari removes 'reg' keyword from outputs, Minimig keeps them

## Unique Aspects of Minimig Version

The Minimig version stands out with its unique approach to state machine implementation:

1. **Wire-based States**: Rather than traditional case statements, it defines states as wires based on conditions
2. **Wait Mechanisms**: Implements wait_req and wait_dtack for operation flow control
3. **Direct Clock Usage**: Uses M68K_CLK directly without derivation logic
4. **Simpler Architecture**: More streamlined than Atari but different approach than Amiga

## Conclusion

The Minimig version represents an intermediate approach between the original simplicity and the Atari complexity. It shares the modernized interface with Amiga and Atari versions but implements a unique state machine architecture that appears optimized for Minimig's specific requirements. The wire-based state definitions and wait mechanisms suggest it was designed for a specific timing or operational pattern required by the Minimig platform.

It appears to be a version that maintains compatibility with the newer interface standards (PI_A/PI_D) while implementing a different operational flow than either the Amiga or Atari versions.