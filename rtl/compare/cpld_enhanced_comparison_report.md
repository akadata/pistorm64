# CPLD Enhanced Comparison Report: FC Enhancements

## Executive Summary

This report analyzes the Function Code (FC) enhancements required to bring Atari-level functionality to the Amiga PiStorm implementation. The FC lines are critical for proper 68000 bus operation, allowing the processor to indicate the type of bus cycle (supervisor/user, program/data, etc.).

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
- Update address high register writes to include FC bits
- Ensure FC is set before initiating bus cycles

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
2. Ensure FC is properly set before bus operations
3. Add validation for FC values

### 3.3 Step 3: Integration Testing
1. Test FC line functionality with various operation types
2. Verify proper bus cycle behavior
3. Validate compatibility with existing code

## 4. Enhanced Amiga CPLD Implementation

### 4.1 Proposed amiga_enhanced_fc_pistorm.v

```verilog
/*
 * Enhanced Amiga PiStorm CPLD with FC support
 * Incorporates best features from Atari implementation
 */

module pistorm(
    output reg      PI_TXN_IN_PROGRESS, // GPIO0
    output reg      PI_IPL_ZERO,        // GPIO1
    input   [1:0]   PI_A,       // GPIO[3..2]
    input           PI_CLK,     // GPIO4
    output reg      PI_RESET,   // GPIO5
    input           PI_RD,      // GPIO6
    input           PI_WR,      // GPIO7
    inout   [15:0]  PI_D,       // GPIO[23..8]

    output reg      LTCH_A_0,
    output reg      LTCH_A_8,
    output reg      LTCH_A_16,
    output reg      LTCH_A_24,
    output reg      LTCH_A_OE_n,
    output reg      LTCH_D_RD_U,
    output reg      LTCH_D_RD_L,
    output reg      LTCH_D_RD_OE_n,
    output reg      LTCH_D_WR_U,
    output reg      LTCH_D_WR_L,
    output reg      LTCH_D_WR_OE_n,

    input           M68K_CLK,
    output  [2:0]   M68K_FC,    // Enhanced: No longer reg, conditional assignment

    output reg      M68K_AS_n,
    output reg      M68K_UDS_n,
    output reg      M68K_LDS_n,
    output reg      M68K_RW,

    input           M68K_DTACK_n,
    input           M68K_BERR_n,

    input           M68K_VPA_n,
    output reg      M68K_E,
    output reg      M68K_VMA_n,

    input   [2:0]   M68K_IPL_n,

    inout           M68K_RESET_n,
    inout           M68K_HALT_n,

    input           M68K_BR_n,
    output          M68K_BG_n,  // Enhanced: No longer reg
    input           M68K_BGACK_n
);

  wire c200m = PI_CLK;
  reg [2:0] c7m_sync;
  //  wire c7m = M68K_CLK;
  wire c7m = c7m_sync[2];
  wire c1c3_clk = !(M68K_C1 ^ M68K_C3);  // Assuming these are defined elsewhere

  localparam REG_DATA = 2'd0;
  localparam REG_ADDR_LO = 2'd1;
  localparam REG_ADDR_HI = 2'd2;
  localparam REG_STATUS = 2'd3;

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

  reg [1:0] rd_sync;
  reg [1:0] wr_sync;

  always @(posedge c200m) begin
    rd_sync <= {rd_sync[0], PI_RD};
    wr_sync <= {wr_sync[0], PI_WR};
  end

  wire rd_rising = !rd_sync[1] && rd_sync[0];
  wire wr_rising = !wr_sync[1] && wr_sync[0];

  reg [15:0] data_out;
  assign PI_D = PI_A == REG_STATUS && PI_RD ? data_out : 16'bz;

  always @(posedge c200m) begin
    if (rd_rising && PI_A == REG_STATUS) begin
      data_out <= {ipl, 13'd0};
    end
  end

  reg [15:0] status;
  wire reset_out = !status[1];

  assign M68K_RESET_n = reset_out ? 1'b0 : 1'bz;
  assign M68K_HALT_n = reset_out ? 1'b0 : 1'bz;

  reg op_req = 1'b0;
  reg op_rw = 1'b1;
  reg op_uds_n = 1'b1;
  reg op_lds_n = 1'b1;

  // Enhanced: FC storage and handling
  reg [2:0] op_fc = 3'b111;  // Default to supervisor data access

  always @(*) begin
    LTCH_D_WR_U <= PI_A == REG_DATA && PI_WR;
    LTCH_D_WR_L <= PI_A == REG_DATA && PI_WR;

    LTCH_A_0 <= PI_A == REG_ADDR_LO && PI_WR;
    LTCH_A_8 <= PI_A == REG_ADDR_LO && PI_WR;

    LTCH_A_16 <= PI_A == REG_ADDR_HI && PI_WR;
    LTCH_A_24 <= PI_A == REG_ADDR_HI && PI_WR;

    LTCH_D_RD_OE_n <= !(PI_A == REG_DATA && PI_RD);
  end

  reg a0;

  always @(posedge c200m) begin
    c7m_sync <= {c7m_sync[1:0], (CLK_SEL?M68K_CLK:c1c3_clk)};
  end

  wire c7m_rising = !c7m_sync[2] && c7m_sync[1];
  wire c7m_falling = c7m_sync[2] && !c7m_sync[1];

  reg [2:0] ipl;
  reg [2:0] ipl_1;
  reg [2:0] ipl_2;

  always @(posedge c200m) begin
    if (c7m_falling) begin
      ipl_1 <= ~M68K_IPL_n;
      ipl_2 <= ipl_1;
    end

    if (ipl_2 == ipl_1)
      ipl <= ipl_2;

    PI_IPL_ZERO <= ipl == 3'd0;
  end

  always @(posedge c200m) begin
    PI_RESET <= reset_out ? 1'b1 : M68K_RESET_n;
  end

  reg [3:0] e_counter = 4'd0;

  always @(negedge c7m) begin
    if (e_counter == 4'd9)
      e_counter <= 4'd0;
    else
      e_counter <= e_counter + 4'd1;
  end

  always @(negedge c7m) begin
    if (e_counter == 4'd9)
      M68K_E <= 1'b0;
    else if (e_counter == 4'd5)
      M68K_E <= 1'b1;
  end

  reg [2:0] state = 3'd0;
  reg [2:0] PI_TXN_IN_PROGRESS_delay;

  always @(posedge c200m) begin

    if (wr_rising) begin
      case (PI_A)
        REG_ADDR_LO: begin
          a0 <= PI_D[0];
          PI_TXN_IN_PROGRESS <= 1'b1;
        end
        REG_ADDR_HI: begin
          op_req <= 1'b1;
          op_rw <= PI_D[9];
          op_uds_n <= PI_D[8] ? a0 : 1'b0;
          op_lds_n <= PI_D[8] ? !a0 : 1'b0;
          // Enhanced: Extract FC from upper bits of address high register
          op_fc <= PI_D[15:13];  // FC bits come from bits 15:13 of address high
        end
        REG_STATUS: begin
          status <= PI_D;
        end
      endcase
    end

    case (state)
      3'd0: begin // S0
        M68K_RW <= 1'b1; // S7 -> S0
//        if (c7m_falling) begin
//          if (op_req) begin
            state <= 2'd1;
//          end
//        end
      end

      3'd1: begin // S1
        if (op_req) begin
          if(c7m_rising) begin
            state <= 3'd2;
          end
        end
      end
      3'd2: begin // S2
        M68K_RW <= op_rw; // S1 -> S2
        LTCH_D_WR_OE_n <= op_rw;
        LTCH_A_OE_n <= 1'b0;
        M68K_AS_n <= 1'b0;
        M68K_UDS_n <= op_rw ? op_uds_n : 1'b1;
        M68K_LDS_n <= op_rw ? op_lds_n : 1'b1;
        if (c7m_falling) begin
          M68K_UDS_n <= op_uds_n;
          M68K_LDS_n <= op_lds_n;
          state <= 3'd3;
        end
      end

      3'd3: begin // S3
        op_req <= 1'b0;
        if(c7m_rising) begin
          if (!M68K_DTACK_n || (!M68K_VMA_n && e_counter == 4'd8)) begin
            state <= 3'd4;
            PI_TXN_IN_PROGRESS_delay[2:0] <= 3'b111;
          end
          else begin
            if (!M68K_VPA_n && e_counter == 4'd2) begin
              M68K_VMA_n <= 1'b0;
            end
          end
        end
      end
      3'd4: begin // S4
        PI_TXN_IN_PROGRESS_delay <= {PI_TXN_IN_PROGRESS_delay[1:0],1'b0};
        PI_TXN_IN_PROGRESS <= PI_TXN_IN_PROGRESS_delay[2];
        LTCH_D_RD_U <= 1'b1;
        LTCH_D_RD_L <= 1'b1;
        if (c7m_falling) begin
          state <= 3'd5;
          PI_TXN_IN_PROGRESS <= 1'b0;
        end
      end

      3'd5: begin // S5
        LTCH_D_RD_U <= 1'b0;
        LTCH_D_RD_L <= 1'b0;
        if (c7m_rising) begin
          state <= 3'd6;
        end
      end

      3'd6: begin // S6
        if (c7m_falling) begin
          M68K_VMA_n <= 1'b1;
          state <= 3'd7;
        end
      end

      3'd7: begin // S7
        LTCH_D_WR_OE_n <= 1'b1;
        LTCH_A_OE_n <= 1'b1;
        M68K_AS_n <= 1'b1;
        M68K_UDS_n <= 1'b1;
        M68K_LDS_n <= 1'b1;
//        if(c7m_rising) begin
//          M68K_RW <= 1'b1; // S7 -> S0
          state <= 3'd0;
//        end
      end
    endcase
  end

  // Enhanced: Conditional FC assignment based on bus grant
  assign M68K_FC = M68K_BGACK_n ? op_fc : 3'bzzz;
  assign M68K_BG_n = M68K_BGACK_n ? 1'b1 : 1'bz;  // Enhanced: Tri-state when not granted

endmodule
```

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

## 7. Recommendations

1. **Phase 1**: Enhance kernel module with FC support
2. **Phase 2**: Update CPLD code with FC handling
3. **Phase 3**: Integrate with emulator FC management
4. **Phase 4**: Test and validate functionality

This approach brings the best of Atari's FC implementation to the Amiga version while maintaining compatibility and improving overall system performance.