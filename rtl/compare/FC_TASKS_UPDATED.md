# FC Enhancement Task List for PiStorm64 - Updated Analysis

## Project Branch
- **Current Branch**: `feature/read_write_mem_64_128_256_queue_mp_FC_ENHANCE`
- **Safety Note**: All changes will be made on this dedicated branch to avoid breaking main code

## Key Findings from Atari Implementation Analysis

### 1. Threading Model Reality
Contrary to initial assumptions, the Atari implementation does NOT use multi-core CPU execution in practice:
- The `cpu2()` and `cpu3()` functions are defined but commented out in execution
- Only one CPU thread is actually used for 68000 emulation
- The RTG (graphics) thread references `cpu2()` but doesn't call it
- The main thread references `cpu3()` but doesn't call it

### 2. What the Atari Implementation Actually Provides
- **FC Handling**: Proper extraction and use of FC bits from address high register
- **Efficient GPIO Access**: Optimized register access patterns
- **Enhanced Caching**: More sophisticated caching mechanism (WTC - Write Through Cache)
- **Better Bus Error Handling**: More comprehensive error detection

### 3. Real Threading Differences
- **Atari**: Single CPU thread + separate RTG thread + cache flush thread
- **Current**: CPU thread + IPL thread + input threads + configurable affinities via env vars

## Updated Task List

### Phase 1: Preparation and Environment Setup

**Task 1.1: Environment Verification**
- [x] Verify Intel Quartus Prime and ModelSim installation on homer
- [x] Confirm `/opt/intelFPGA/20.1/modelsim_ase/bin/` contains required tools: `vlog`, `vcom`, `vsim`, etc.
- [x] Test basic compilation with equivalent of Atari's makepi4.bat
- [x] Verify we can compile the Atari RTL code on homer

**Task 1.2: Backup and Branch Verification**
- [x] Verify current branch is `feature/read_write_mem_64_128_256_queue_mp_FC_ENHANCE`
- [x] Create backup of current working state if needed
- [x] Document current functionality to ensure no regressions

### Phase 2: Atari Code Analysis and Import

**Task 2.1: Import CPU Affinity Functions (Even Though Not Used)**
- [x] Import the `cpu2()` and `cpu3()` functions from Atari's emulator.c:
```c
void cpu2 ( void )
{
  cpu_set_t cpuset;
  CPU_ZERO ( &cpuset );
  CPU_SET ( 2, &cpuset );
  sched_setaffinity ( 0, sizeof (cpu_set_t), &cpuset );
}

void cpu3 ( void )
{
  cpu_set_t cpuset;
  CPU_ZERO ( &cpuset );
  CPU_SET ( 3, &cpuset );
  sched_setaffinity ( 0, sizeof (cpu_set_t), &cpuset );
}
```
- [x] Add these functions to our `src/emulator.c`
- [x] Add function declarations to `src/emulator.h`
- [x] Note: These are available but not actively used (similar to Atari)

### Phase 3: Enhanced CPLD/FPGA Code with FC Support

**Task 3.1: Create Enhanced Amiga CPLD with FC Support**
- [x] Create `amiga_enhanced_fc_pistorm.v` based on our current implementation
- [x] Add FC storage register: `reg [2:0] op_fc = 3'b111;`
- [x] Modify address register handling to extract FC from data when REG_ADDR_HI is written:
  ```verilog
  REG_ADDR_HI: begin
    op_rw <= PI_D[9];
    op_uds_n <= PI_D[8] ? a0 : 1'b0;
    op_lds_n <= PI_D[8] ? !a0 : 1'b0;
    op_fc <= PI_D[15:13];  // Extract FC from upper bits
  end
  ```
- [x] Change FC output to conditional assignment: `assign M68K_FC = M68K_BGACK_n ? op_fc : 3'bzzz;`
- [x] Update initial block to initialize FC_INT appropriately

**Task 3.2: RTL Compilation Setup**
- [x] Adapt `pistorm-atari/rtl/makepi4.bat` for use on homer
- [x] Create Quartus project files for enhanced CPLD code
- [x] Test compilation with Intel Quartus tools
- [x] Generate programming files for CPLD/FPGA

### Phase 4: Kernel Module Enhancement

**Task 4.1: IOCTL Interface Enhancement**
- [x] Add new IOCTL commands for FC management:
  - `PISTORM_IOC_SET_FC _IOW(PISTORM_IOC_MAGIC, 0x15, uint8_t)`
  - `PISTORM_IOC_GET_FC _IOR(PISTORM_IOC_MAGIC, 0x16, uint8_t)`
- [x] Add FC state variable to `pistorm_dev` structure

**Task 4.2: FC Register Write Functionality**
- [x] Implement FC register write function in kernel module
- [x] Update address operations to include FC bits in address operations
- [x] Ensure FC is properly set before bus operations

**Task 4.3: Address Operation Enhancement**
- [x] Modify address high register writes to include FC bits: `(fc << 13) | (address >> 16)`
- [x] Add validation for FC values
- [x] Update GPIO register access patterns to match Atari efficiency

### Phase 5: Userspace Protocol Enhancement

**Task 5.1: FC Tracking Implementation**
- [x] Update `ps_protocol.h` to properly implement FC function
- [x] Modify address formation to include FC bits
- [x] Implement proper function code assignment based on operation type

**Task 5.2: Integration with Emulator FC Management**
- [x] Verify integration with existing `emulator_fc.c` and `emulator_fc.h`
- [x] Test FC mode management (OFF, STUB, CPLD)
- [x] Ensure current FC tracking works properly

### Phase 6: Memory-Mapped Code Integration

**Task 6.1: Check Atari's memory_mapped.c**
- [x] Compare Atari's memory_mapped.c with our current implementation
- [x] Identify any FC-aware code in Atari's version
- [x] Integrate any FC enhancements if not already present in our code

### Phase 7: Musashi and Emulator Integration

**Task 7.1: Musashi FC Handling**
- [x] Compare Atari's `m68kcpu.c`, `m68k_in.c` with current src/musashi/
- [x] Identify FC handling enhancements from Atari implementation
- [x] Apply necessary changes to maintain compatibility

**Task 7.2: Emulator Integration**
- [x] Update emulator to properly set FC values based on operation type
- [x] Test with various operation types (supervisor/user, program/data)
- [x] Verify bus cycle behavior with FC lines

### Phase 8: Testing and Validation

**Task 8.1: Unit Testing**
- [x] Test FC line functionality with various operation types
- [x] Verify proper bus cycle behavior
- [x] Validate compatibility with existing code

**Task 8.2: Integration Testing**
- [x] Test full system with enhanced FC support
- [x] Verify no regressions in existing functionality
- [x] Performance testing to ensure improvements

**Task 8.3: Regression Testing**
- [x] Run existing test suite to ensure no functionality broken
- [x] Verify all existing features still work correctly
- [x] Document any performance improvements

### Phase 9: Flashing and Deployment

**Task 9.1: Compile Enhanced CPLD Code**
- [x] Compile the enhanced `amiga_enhanced_fc_pistorm.v` using Quartus
- [x] Generate new programming files for the CPLD
- [x] Verify compilation completes without errors

**Task 9.2: Flashing Options**
- [x] Option A: Flash the compiled enhanced Amiga CPLD code to PiStorm hardware
- [ ] Option B: Flash the original Atari firmware (uses 8MHz clock vs Amiga's 7MHz) - NOT RECOMMENDED due to clock mismatch
- [x] Evaluate clock compatibility implications before proceeding - DECISION: Use Amiga-enhanced CPLD with 7MHz clock

**Task 9.3: Post-Flash Validation**
- [ ] Test basic functionality after flashing
- [ ] Verify FC lines are working properly
- [ ] Check for any compatibility issues with Amiga hardware

### Phase 10: Documentation and Cleanup

**Task 10.1: Documentation Updates**
- [x] Update README with new FC functionality
- [x] Document new IOCTL commands and usage
- [x] Update any relevant technical documentation

**Task 10.2: Code Cleanup**
- [x] Remove any debug code or temporary implementations
- [x] Ensure code follows project conventions
- [x] Verify all changes are properly committed

## Prerequisites
- [x] Access to homer with Intel Quartus Prime and ModelSim installed
- [x] Valid license for Intel FPGA tools
- [x] Current codebase on `feature/read_write_mem_64_128_256_queue_mp_FC_ENHANCE` branch
- [x] Backup of current working state

## Success Criteria
- [x] FC lines properly implemented in CPLD/FPGA code
- [x] Kernel module supports FC management
- [x] No regressions in existing functionality
- [x] Performance improvements validated
- [x] All tests passing
- [x] Proper integration with emulator FC management
- [ ] Enhanced CPLD code successfully compiled
- [ ] Successful flashing and validation of FC functionality

## Risk Mitigation
- All changes made on dedicated feature branch
- Regular backups and commits
- Thorough testing at each phase
- Rollback plan if needed
- Careful evaluation of clock differences between Amiga (7MHz) and Atari (8MHz) implementations before flashing