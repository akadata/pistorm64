# FC and IPL Implementation Analysis: Amiga vs Atari Pistorm

## Overview
This document analyzes the differences in FC (Function Code) and IPL (Interrupt Priority Level) handling between the Amiga and Atari Pistorm implementations, and determines what changes are needed to add these features to the current implementation.

## Current Amiga Implementation (amiga-pistorm.v)

### FC Handling
- **M68K_FC** is declared as an output: `output reg [2:0] M68K_FC`
- **Initialization**: `M68K_FC <= 3'd0;` in the initial block
- **No dynamic FC assignment**: The FC lines are not dynamically set based on operation type
- **Simple assignment**: FC is set to a constant value (0) and never changed during operation

### IPL Handling
- **M68K_IPL_n** is declared as input: `input [2:0] M68K_IPL_n`
- **Internal ipl register**: `reg [2:0] ipl;` with synchronization registers `ipl_1`, `ipl_2`
- **Synchronization**: Uses 3-stage synchronization to detect stable IPL values
- **Output**: `PI_IPL_ZERO` is set when `ipl == 3'd0`
- **Status reporting**: IPL value is sent to Pi via status register: `data_out <= {ipl, 13'd0};`

## Atari Implementation (pistorm-atari.v)

### FC Handling
- **M68K_FC** is declared as output: `output [2:0] M68K_FC` (note: no 'reg' keyword)
- **Internal FC register**: `reg [2:0] FC_INT;` for internal storage
- **Dynamic assignment**: FC is set based on received data: `op_fc <= PI_D[15:13];` when REG_ADDR_HI is written
- **Conditional output**: `assign M68K_FC = M68K_BGACK_n ? FC_INT : 3'bzzz;`
- **Usage in address formation**: FC is incorporated into address: `(fc << 13) | (address >> 16)`

### IPL Handling
- **M68K_IPL_n** is declared as input: `input [2:0] M68K_IPL_n`
- **Internal ipl register**: `reg [2:0] ipl;`
- **Different synchronization**: Updates on falling edge of c8m clock: `ipl <= ~M68K_IPL_n;`
- **Additional reset tracking**: Uses `reset_d` register to track reset state
- **Output**: `PI_IPL_ZERO <= ( ipl == 3'd0 && reset_d );`
- **Status reporting**: IPL value with additional reset info: `{ipl, 11'b0, !( M68K_RESET_n || reset_out ), 1'b0}`

## Userspace Protocol Implementation

### Atari ps_protocol.c
- **Global FC variable**: `uint8_t fc;` to track current function code
- **Address formation**: Uses FC in address high register: `( (fc << 13) | (address >> 16) )`
- **FC is sent with address**: Allows hardware to know the type of bus cycle being performed

### Current ps_protocol.h
- **FC function declaration**: `void ps_fc_write(uint8_t fc);` when PISTORM_KMOD is defined
- **Placeholder implementation**: Empty function when kernel module is not used

## Analysis: Can Amiga pistorm.v Use FC/IPL Without Changes?

### Answer: No, changes are required in both the CPLD/FPGA code and the userspace protocol

### Changes Required in FPGA/CPLD Code (amiga-pistorm.v):

1. **FC Storage**: Add internal register to store FC value:
   ```verilog
   reg [2:0] FC_INT;
   ```

2. **FC Assignment Logic**: Modify the address register handling to extract FC:
   ```verilog
   REG_ADDR_HI: begin
     op_rw <= PI_D[9];
     op_uds_n <= PI_D[8] ? a0 : 1'b0;
     op_lds_n <= PI_D[8] ? !a0 : 1'b0;
     FC_INT <= PI_D[15:13];  // Extract FC from upper bits
   end
   ```

3. **FC Output Assignment**: Change from direct register to conditional assignment:
   ```verilog
   assign M68K_FC = M68K_BGACK_n ? FC_INT : 3'bzzz;
   ```

4. **Update Initial Block**: Initialize FC_INT appropriately:
   ```verilog
   FC_INT <= 3'b111;  // Supervisor data access is typical default
   ```

### Changes Required in Userspace Protocol:

1. **Address Formation**: Modify address sending to include FC bits:
   - Current: Send `address >> 16` to REG_ADDR_HI
   - Required: Send `(fc << 13) | (address >> 16)` to REG_ADDR_HI

2. **FC Tracking**: Track FC value and send appropriate function codes:
   - Supervisor Program (001), Supervisor Data (000), User Program (101), User Data (100), etc.

### IPL Compatibility
- IPL handling is largely compatible between versions
- The Atari version has enhanced reset tracking, but basic IPL functionality would work

## Conclusion

The Amiga pistorm.v implementation **cannot** use FC lines without modifications. The changes required are:

1. **FPGA/CPLD Code Changes**: 
   - Add FC storage register
   - Modify address register handling to extract FC from data
   - Change FC output to conditional assignment based on bus grant

2. **Userspace Protocol Changes**:
   - Modify address formation to include FC bits
   - Implement FC tracking and appropriate function code assignment

The IPL handling is more compatible and would require minimal changes, mainly to take advantage of the Atari implementation's enhanced reset tracking features.

The implementation would need to be coordinated between the userspace protocol (which sends the FC in the address high register) and the FPGA/CPLD code (which extracts and outputs the FC lines).