# FC Enhancement Implementation Plan - Next Steps

## Current Status
- Code compiles and runs on Amiga without immediate bugs
- Many CPU portion changes have been merged
- Enhanced CPLD code with FC support is ready
- Decision made to use Amiga-enhanced CPLD with 7MHz clock (not Atari firmware with 8MHz)

## Next Steps for FC Implementation

### Step 1: Prepare Enhanced CPLD Code for Compilation
1. Ensure the `amiga_enhanced_fc_pistorm.v` file is properly formatted and complete
2. Create Quartus project files for the enhanced CPLD code
3. Verify all dependencies are met

### Step 2: Compile with Quartus on homer
1. Transfer the enhanced CPLD code to homer
2. Run compilation using Intel Quartus tools
3. Generate programming files (SVF format)

### Step 3: Flash to PiStorm Hardware
1. Flash the compiled enhanced CPLD code to the PiStorm hardware
2. Ensure proper backup of current working firmware before flashing

### Step 4: Post-Flash Validation
1. Test basic functionality after flashing
2. Verify FC lines are working properly
3. Check for any compatibility issues with Amiga hardware

## Implementation Details

### Enhanced CPLD Code
The enhanced CPLD code should include:

1. **FC Storage Register**:
   ```verilog
   reg [2:0] op_fc = 3'b111;  // Default to supervisor data access
   ```

2. **FC Extraction Logic**:
   ```verilog
   REG_ADDR_HI: begin
     op_rw <= PI_D[9];
     op_uds_n <= PI_D[8] ? a0 : 1'b0;
     op_lds_n <= PI_D[8] ? !a0 : 1'b0;
     op_fc <= PI_D[15:13];  // Extract FC from upper bits
   end
   ```

3. **Conditional FC Output**:
   ```verilog
   assign M68K_FC = M68K_BGACK_n ? op_fc : 3'bzzz;
   ```

4. **Initial Block Update**:
   ```verilog
   initial begin
     PI_TXN_IN_PROGRESS <= 1'b0;
     PI_IPL_ZERO <= 1'b0;
     PI_RESET <= 1'b0;
     M68K_FC <= 3'b000;  // Will be overridden by conditional assignment
     M68K_RW <= 1'b1;
     M68K_E <= 1'b0;
     M68K_VMA_n <= 1'b1;
     M68K_BG_n <= 1'b1;
   end
   ```

### Address Formation Update
The address high register formation should include FC bits:
- `(op_fc << 13) | (address >> 16)` instead of just `(address >> 16)`

## Safety Considerations
- All changes are on the dedicated feature branch
- Backup of current working firmware before flashing
- Testing in controlled environment
- Rollback plan if needed

## Expected Outcomes
- FC lines properly implemented in hardware
- Better 68000 bus cycle compatibility
- Improved system performance
- No regressions in existing functionality