# Evolution of PiStorm Firmware: From Original to Amiga and Atari Versions

## Overview
This document traces the evolution of the PiStorm FPGA firmware from the original implementation by Niklas Ekström to the Amiga and Atari variants.

## Original Version (Niklas Ekström)
The original version had the following characteristics:
- Used PI_SA (address) and PI_SD (data) signals for communication
- Had PI_SOE_n (output enable) and PI_SWE_n (write enable) signals
- Used PI_AUX0 and PI_AUX1 for auxiliary signals
- Implemented a basic 8-state machine for bus cycle control
- Used a 7MHz clock derived from M68K_CLK
- Had simpler bus arbitration without advanced features

## Evolution to Amiga Version

### Major Changes:
1. **Signal Interface Change**:
   - Changed from PI_SA/PI_SD interface to PI_A/PI_D interface
   - PI_AUX0 became PI_TXN_IN_PROGRESS
   - PI_AUX1 became PI_IPL_ZERO
   - PI_SOE_n became PI_RD
   - PI_SWE_n became PI_WR

2. **Simplified Bus Arbitration**:
   - Removed complex bus arbitration logic
   - Kept basic state machine but simplified some transitions

3. **Clock System**:
   - Maintained 7MHz clock system
   - Added option to use M68K_C1 and M68K_C3 for clock generation

4. **Register Interface**:
   - Changed to register-based parameter system (REG_DATA, REG_ADDR_LO, etc.)

## Evolution to Atari Version

### Major Changes from Original:
1. **Signal Interface**:
   - Similar to Amiga version with PI_A/PI_D interface
   - Same register-based parameter system

2. **Enhanced Bus Arbitration**:
   - Significantly more complex bus arbitration logic
   - Added multiple delay registers (BG_DELAY, BR_DELAY, AS_DELAY, BGK_DELAY)
   - Added sophisticated bus grant handling

3. **Clock System**:
   - Changed to 8MHz clock system using only M68K_CLK
   - Removed option for C1/C3 clock generation

4. **Advanced Features**:
   - Added "long hold" feature for 68000 compatibility
   - Enhanced error handling and reset mechanisms
   - More detailed state machine with defined constants

5. **Improved IPL Handling**:
   - More sophisticated interrupt priority level handling

## Key Innovations in Atari Version

### The "Long Hold" Feature
The most significant addition in the Atari version is the "long hold" feature, added in October 2023:
```
/* Oct 2023 - long hold as per Claude info for 68000 */
LTCH_D_WR_OE_n<= HI; // data-bus hi-z
LTCH_A_OE_n<= HI; // address-bus hi-z
```

This feature keeps the data and address buses in high-impedance state longer during certain phases of the bus cycle, which may be required for proper compatibility with Atari ST hardware or specific 68000 timing requirements.

### Enhanced Bus Arbitration
The Atari version includes much more sophisticated bus arbitration logic to handle complex scenarios with multiple bus masters, which is important for Atari ST systems that may have different bus behavior compared to Amiga systems.

## Summary of Evolution Path

Original (Niklas Ekström) → Amiga Version (Simplified)
                        → Atari Version (Enhanced with advanced features)

The Amiga version represents a simplification of the original concept, focusing on core functionality for Amiga systems.

The Atari version represents an enhancement with additional features and complexity to handle specific requirements of Atari ST systems, including the "long hold" feature and advanced bus arbitration.

Both variants evolved from the same original codebase but took different paths based on the specific requirements of their target platforms.