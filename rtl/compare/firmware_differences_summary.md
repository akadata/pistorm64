# Firmware Differences Summary: Amiga vs Atari

## Key Logical Differences

### 1. Clock Handling
- **Amiga version**: Uses `c7m` clock derived from either `M68K_CLK` or a combination of `M68K_C1` and `M68K_C3`
- **Atari version**: Uses `c8m` clock derived only from `M68K_CLK`

### 2. Input/Output Declarations
- **Amiga version**: Uses `reg` keyword for output declarations (e.g., `output reg PI_RESET`)
- **Atari version**: Uses direct output declarations without `reg` keyword (e.g., `output PI_RESET`)

### 3. Bus Arbitration Logic
- **Amiga version**: Simpler bus arbitration with basic state machine
- **Atari version**: More complex bus arbitration with additional delays and controls:
  - BG_DELAY, BR_DELAY, AS_DELAY registers
  - BGK_DELAY for bus grant acknowledgment
  - BRstart register to track bus request timing
  - More sophisticated bus grant logic

### 4. State Machine Implementation
- **Amiga version**: Basic 8-state machine (S0-S7) with simpler transitions
- **Atari version**: Enhanced state machine with:
  - Defined constants for states (S0-S7) and E-clock counter values (E0-E10)
  - More detailed control over bus cycle phases
  - Additional logic for handling bus errors and acknowledgments

### 5. Address Latching
- **Amiga version**: Simple address latching in combinational block
- **Atari version**: Conditional address latching in write command block with explicit control

### 6. Data Direction Control
- **Amiga version**: Uses PI_RD signal to control PI_D assignment
- **Atari version**: Uses a CMDWR signal and more complex data output logic

### 7. Reset and Hold Logic
- **Amiga version**: Basic reset and halt logic
- **Atari version**: More complex reset logic with additional error handling

### 8. IPL (Interrupt Priority Level) Handling
- **Amiga version**: Uses a 3-stage synchronization (ipl_1, ipl_2) to detect stable IPL
- **Atari version**: Uses a 2-bit shift register (reset_d) to track reset state

### 9. "Long Hold" Feature (As mentioned in the code)
- **Amiga version**: Does not implement the long hold feature
- **Atari version**: Explicitly implements "long hold" as per Claude's info for 68000:
  ```
  /* Oct 2023 - long hold as per Claude info for 68000 */
  LTCH_D_WR_OE_n<= HI; // data-bus hi-z
  LTCH_A_OE_n<= HI; // address-bus hi-z
  ```

### 10. Bus Cycle Timing
- **Amiga version**: Uses 7MHz clock timing with basic E-clock counter
- **Atari version**: Uses 8MHz clock timing with more detailed E-clock management

## Summary
The Atari version appears to be a more refined and robust implementation compared to the Amiga version. It includes enhanced bus arbitration logic, better error handling, and the "long hold" feature that may be required for proper Atari ST compatibility. The additional complexity suggests that the Atari version addresses specific timing and bus control requirements that differ from the Amiga implementation.