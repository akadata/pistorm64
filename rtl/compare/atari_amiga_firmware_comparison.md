# Analysis of Amiga vs Atari PiStorm Firmware

## Overview
This document analyzes the differences between the Amiga and Atari versions of the PiStorm FPGA firmware. The Atari version appears to be an enhanced version of the Amiga implementation with additional features and refinements.

## Key Differences

### 1. Clock Generation
- **Amiga**: Uses a 7MHz clock (`c7m`) derived from either `M68K_CLK` or a combination of `M68K_C1` and `M68K_C3` depending on `CLK_SEL`
- **Atari**: Uses an 8MHz clock (`c8m`) derived directly from `M68K_CLK` only

### 2. Output Declaration Style
- **Amiga**: Uses `output reg` for output declarations
- **Atari**: Uses direct `output` declarations without the `reg` keyword

### 3. Bus Arbitration Complexity
- **Amiga**: Basic bus arbitration with simple state machine
- **Atari**: Advanced bus arbitration with:
  - Multiple delay registers (BG_DELAY, BR_DELAY, AS_DELAY, BGK_DELAY)
  - Sophisticated bus grant logic
  - Better handling of bus contention and timing

### 4. State Machine Enhancements
- **Amiga**: Basic 8-state machine with simple transitions
- **Atari**: Enhanced state machine with:
  - Defined constants for states and E-clock counter values
  - More detailed control over bus cycle phases
  - Additional error handling and acknowledgment logic

### 5. The "Long Hold" Feature
The most significant addition in the Atari version is the "long hold" feature, which is explicitly commented as being added in October 2023 "as per Claude info for 68000":

```
/* Oct 2023 - long hold as per Claude info for 68000 */
LTCH_D_WR_OE_n<= HI; // data-bus hi-z
LTCH_A_OE_n<= HI; // address-bus hi-z
```

This feature keeps the data and address buses in high-impedance state longer during certain phases of the bus cycle, which may be required for proper compatibility with Atari ST hardware or specific 68000 timing requirements.

### 6. IPL (Interrupt Priority Level) Handling
- **Amiga**: Uses a 3-stage synchronization (ipl_1, ipl_2) to detect stable IPL
- **Atari**: Uses a 2-bit shift register (reset_d) to track reset state alongside IPL detection

### 7. Reset and Error Handling
- **Amiga**: Basic reset and halt logic
- **Atari**: Enhanced reset logic with additional error handling and transaction reset mechanism

## Conclusion

The Atari version represents a more mature and robust implementation compared to the Amiga version. The key enhancements include:

1. The "long hold" feature for better 68000 compatibility
2. More sophisticated bus arbitration for improved stability
3. Enhanced error handling and reset mechanisms
4. Better timing control with the 8MHz clock system

These changes suggest that the Atari version was developed to address specific timing and compatibility requirements that differ from the Amiga implementation, possibly due to differences in how the Atari ST handles the 68000 bus cycles compared to Amiga systems.