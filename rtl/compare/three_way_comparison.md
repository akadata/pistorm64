# Comparison of All Three PiStorm Firmware Versions

## Original (Niklas Ekström) Version
```verilog
module pistorm(
    input           PI_CLK,   // GPIO4
    input   [2:0]   PI_SA,    // GPIO[5,3,2]
    inout   [15:0]  PI_SD,    // GPIO[23..8]
    input           PI_SOE_n, // GPIO6
    input           PI_SWE_n, // GPIO7
    output reg      PI_AUX0,  // GPIO0
    output reg      PI_AUX1,  // GPIO1
    ...
);
```
- Uses PI_SA (address) and PI_SD (data) interface
- Uses PI_SOE_n (output enable) and PI_SWE_n (write enable)
- PI_AUX0 and PI_AUX1 for auxiliary signals
- Basic 8-state machine
- 7MHz clock system

## Amiga Version
```verilog
module pistorm(
    output reg      PI_TXN_IN_PROGRESS, // GPIO0
    output reg      PI_IPL_ZERO,        // GPIO1
    input   [1:0]   PI_A,       // GPIO[3..2]
    input           PI_CLK,     // GPIO4
    output reg      PI_RESET,   // GPIO5
    input           PI_RD,      // GPIO6
    input           PI_WR,      // GPIO7
    inout   [15:0]  PI_D,       // GPIO[23..8]
    ...
);
```
- Uses PI_A (address) and PI_D (data) interface
- PI_RD and PI_WR for read/write control
- PI_TXN_IN_PROGRESS and PI_IPL_ZERO for status
- Simplified bus arbitration
- 7MHz clock system with option for C1/C3

## Atari Version
```verilog
module pistorm(
    output          PI_TXN_IN_PROGRESS, // GPIO0
    output          PI_IPL_ZERO,        // GPIO1
    input   [1:0]   PI_A,      // GPIO[3..2]
    input           PI_CLK,    // GPIO4
    output          PI_RESET,  // GPIO5
    input           PI_RD,     // GPIO6
    input           PI_WR,     // GPIO7
    inout   [15:0]  PI_D,      // GPIO[23..8]
    ...
);
```
- Uses PI_A (address) and PI_D (data) interface (same as Amiga)
- No 'reg' keyword in output declarations
- Advanced bus arbitration with multiple delay registers
- 8MHz clock system (no C1/C3 option)
- Includes "long hold" feature
- Enhanced error handling

## Key Evolution Points

1. **Interface Evolution**:
   Original: PI_SA/PI_SD → Amiga/Atari: PI_A/PI_D
   
2. **Control Signals**:
   Original: PI_SOE_n/PI_SWE_n → Amiga/Atari: PI_RD/PI_WR
   
3. **Auxiliary Signals**:
   Original: PI_AUX0/PI_AUX1 → Amiga/Atari: PI_TXN_IN_PROGRESS/PI_IPL_ZERO
   
4. **Clock Systems**:
   Original: 7MHz only → Amiga: 7MHz with C1/C3 option → Atari: 8MHz only
   
5. **Complexity**:
   Original → Amiga: Simplified → Atari: Enhanced with advanced features