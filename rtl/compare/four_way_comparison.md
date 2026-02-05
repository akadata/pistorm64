# Four-Way Comparison: PiStorm Firmware Evolution

## Overview
This document provides a comprehensive comparison of all four PiStorm firmware variants:
1. Original (Niklas Ekström)
2. Amiga
3. Atari  
4. Minimig

## Interface Evolution

### Original (Niklas Ekström)
```
- PI_CLK, PI_SA[2:0], PI_SD[15:0], PI_SOE_n, PI_SWE_n
- PI_AUX0, PI_AUX1 for auxiliary signals
```

### Amiga & Atari & Minimig
```
- PI_CLK, PI_A[1:0], PI_D[15:0], PI_RD, PI_WR
- PI_TXN_IN_PROGRESS, PI_IPL_ZERO for status
```

## Clock Systems

### Original & Amiga
- 7MHz clock system
- Amiga adds option for C1/C3 clock derivation

### Atari
- 8MHz clock system
- Direct M68K_CLK only

### Minimig
- Direct M68K_CLK usage
- No clock derivation logic

## State Machine Architectures

### Original
- Traditional 8-state machine (3-bit)
- Sequential state transitions in case statement

### Amiga
- Traditional 8-state machine (3-bit)
- Simplified from original

### Atari
- Enhanced 8-state machine (3-bit)
- Defined constants for states (S0-S7)
- Advanced bus arbitration logic

### Minimig
- 4-state machine (2-bit)
- Wire-based state definitions (S0-S7 as wires)
- wait_req/wait_dtack flow control mechanism

## Bus Arbitration Complexity

### Original
- Moderate complexity
- Basic bus arbitration

### Amiga
- Simplified from original
- Minimal bus arbitration

### Atari
- Maximum complexity
- Multiple delay registers (BG_DELAY, BR_DELAY, etc.)
- "Long hold" feature for 68000 compatibility

### Minimig
- Moderate complexity
- Different approach than original
- Flow control mechanisms

## Key Distinguishing Features

### Original
- First implementation
- PI_SA/PI_SD interface
- Basic functionality

### Amiga
- Simplified version
- Compatible with Amiga systems
- Maintains core functionality

### Atari
- Most advanced
- "Long hold" feature
- Complex bus arbitration
- Optimized for Atari ST systems

### Minimig
- Unique state machine design
- Wire-based state definitions
- Direct clock usage
- Optimized for Minimig platform

## Evolution Summary

```
Original (Niklas Ekström)
├── Amiga (Simplification)
├── Minimig (Alternative approach)
└── Atari (Enhancement with advanced features)
```

Each branch evolved to meet the specific requirements of its target platform while maintaining the core PiStorm concept of interfacing between the Raspberry Pi and the 68000 processor.