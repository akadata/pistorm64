# CPLD Enhanced Comparison Report: FC Enhancements

## Executive Summary

This report analyzes the Function Code (FC) enhancements required to bring Atari-level functionality to the Amiga PiStorm implementation. The FC lines are critical for proper 68000 bus operation, allowing the processor to indicate the type of bus cycle it's performing (supervisor/user, program/data, etc.).

## 1. High-Level Overview of FC Enhancements in CPLD

### 1.1 What is Involved in the CPLD

The Function Code (FC) lines consist of three signals (FC0, FC1, FC2) that the 68000 processor uses to indicate the type of bus cycle it's performing:

- **FC0**: Program/Data indicator (0=Program, 1=Data)
- **FC1**: Supervisor/User indicator (0=Supervisor, 1=User) 
- **FC2**: CPU/Not-CPU indicator (0=CPU Space, 1=Not CPU Space)

### 1.2 Current Amiga Implementation Limitations

The current Amiga implementation:
- Sets M68K_FC to a constant value (0) in initialization
- Does not dynamically update FC lines based on operation type
- Lacks proper FC handling in address register processing

### 1.3 Atari Implementation Enhancements

The Atari implementation adds:
- Dynamic FC assignment from address register data (bits 15:13)
- Conditional FC output based on bus grant status
- Proper FC integration in address formation
- Enhanced bus error handling with FC awareness

## 2. Kernel Module (pistorm.ko) Integration

### 2.1 Current State
The kernel module already has infrastructure for FC support:
- `ps_fc_write(uint8_t fc)` function declaration in ps_protocol.h
- FC mode management system (OFF, STUB, CPLD)
- Current FC tracking in emulator_fc.c

### 2.2 Required Kernel Module Changes

#### 2.2.1 IOCTL Interface Enhancement
Add new IOCTL commands for FC management:
```c
#define PISTORM_IOC_SET_FC      _IOW(PISTORM_IOC_MAGIC, 0x15, uint8_t)
#define PISTORM_IOC_GET_FC      _IOR(PISTORM_IOC_MAGIC, 0x16, uint8_t)
```

#### 2.2.2 FC State Management
Enhance the kernel module to track and set FC lines:
- Add FC state variable to pistorm_dev structure
- Implement FC register write functionality
- Integrate FC setting with address operations

#### 2.2.3 Address Operation Enhancement
Modify address operations to include FC bits:
- Update address high register writes to include FC bits: `(fc << 13) | (address >> 16)`
- Ensure FC is properly set before initiating bus cycles

### 2.3 Why Add FC Support to Kernel Module

1. **System Compatibility**: Proper FC lines are essential for accurate 68000 bus operation
2. **Memory Mapping**: Different FC values allow for proper memory mapping and protection
3. **Performance**: Enables more efficient bus cycle handling
4. **Future-Proofing**: Prepares for more advanced 68000 features and coprocessors

## 3. Implementation Plan for pistorm.ko

### 3.1 Step 1: Kernel Module Modifications
1. Add FC state tracking to pistorm_dev structure
2. Implement FC register write function
3. Add IOCTL commands for FC management
4. Update bus operation functions to include FC bits

### 3.2 Step 2: Address Formation Enhancement
1. Modify address high register writes to include FC bits
2. Ensure FC is set before bus operations
3. Add validation for FC values

### 3.3 Step 3: Integration Testing
1. Test FC line functionality with various operation types
2. Verify proper bus cycle behavior
3. Validate compatibility with existing code

## 4. Enhanced Amiga CPLD Implementation

### 4.1 Proposed amiga_enhanced_fc_pistorm.v

Based on the Atari implementation, the enhanced CPLD code should include:

```verilog
// Enhanced FC handling in address register processing
REG_ADDR_HI: begin
  op_rw <= PI_D[9];
  op_uds_n <= PI_D[8] ? a0 : 1'b0;
  op_lds_n <= PI_D[8] ? !a0 : 1'b0;
  op_fc <= PI_D[15:13];  // Extract FC from upper bits (15:13)
end

// Conditional FC output based on bus grant status
assign M68K_FC = M68K_BGACK_n ? op_fc : 3'bzzz;

// Initialize FC to supervisor data access (typical default)
initial begin
  // ... other initializations
  op_fc <= 3'b111;  // Supervisor data access
end
```

### 4.2 Address Formation with FC Bits
The address high register formation should include FC bits:
- `(op_fc << 13) | (address >> 16)` instead of just `(address >> 16)`

## 5. Atari vs Current Source Code Comparison

### 5.1 Musashi Changes
The Atari implementation likely includes:
- Enhanced FC handling in m68kcpu.c
- Updated function code tracking
- Better integration with bus cycle management

### 5.2 Emulator Integration
The Atari version has:
- More sophisticated FC management
- Better error handling
- Enhanced bus arbitration

### 5.3 Key Differences
1. **Address Formation**: Atari incorporates FC bits in address high register
2. **Bus Arbitration**: More sophisticated bus handling in Atari
3. **Error Handling**: Enhanced error detection in Atari
4. **Timing**: Different clock management approaches

## 6. Teaching pistorm.ko to Handle FC Lines

### 6.1 FC Line Management
The kernel module needs to:
1. Accept FC values from userspace
2. Encode FC bits into address operations
3. Manage FC state appropriately

### 6.2 Integration Points
1. **Bus Operations**: Modify address high register writes to include FC bits
2. **State Management**: Track current FC value
3. **Protocol**: Update GPIO protocol to handle FC bits

### 6.3 Implementation Strategy
1. Add FC parameter to bus operation structure
2. Modify address formation to include FC bits
3. Update ps_protocol functions to handle FC

## 7. Enhanced Amiga Implementation Based on Atari

### 7.1 Recommended Changes to amiga-pistorm.v

```verilog
// Add FC storage register
reg [2:0] op_fc = 3'b111;  // Default to supervisor data access

// Modify address register handling to extract FC
REG_ADDR_HI: begin
  op_rw <= PI_D[9];
  op_uds_n <= PI_D[8] ? a0 : 1'b0;
  op_lds_n <= PI_D[8] ? !a0 : 1'b0;
  op_fc <= PI_D[15:13];  // Extract FC from upper bits
end

// Change FC output to conditional assignment
assign M68K_FC = M68K_BGACK_n ? op_fc : 3'bzzz;
```

### 7.2 Userspace Protocol Updates
- Modify address formation to include FC bits: `(fc << 13) | (address >> 16)`
- Implement proper function code assignment based on operation type
- Update FC tracking and management

## 8. Atari Codebase Integration Points

### 8.1 Key Atari Files to Reference
- `pistorm-atari/gpio/ps_protocol.c` - Enhanced FC handling
- `pistorm-atari/m68k_in.c` - FC instruction implementations
- `pistorm-atari/m68kcpu.h` - FC-related macros and definitions
- `pistorm-atari/m68kmmu.h` - MMU and FC handling

### 8.2 FC Instruction Timing
According to the MC68040 manual referenced in the Atari codebase:
- PFLUSHA: 11 clocks total (1L + 10 clocks)
- Proper timing integration is essential for accurate emulation

## 9. Recommendations

1. **Phase 1**: Enhance kernel module with FC support
2. **Phase 2**: Update CPLD code with FC handling
3. **Phase 3**: Integrate with emulator FC management
4. **Phase 4**: Test and validate functionality

This approach brings the best of Atari's FC implementation to the Amiga version while maintaining compatibility and improving overall system performance.