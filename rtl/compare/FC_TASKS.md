# FC Enhancement Task List for PiStorm64

## Project Branch
- **Current Branch**: `feature/read_write_mem_64_128_256_queue_mp_FC_ENHANCE`
- **Safety Note**: All changes will be made on this dedicated branch to avoid breaking main code

## Task List

### Phase 1: Preparation and Environment Setup

**Task 1.1: Environment Verification**
- [ ] Verify Intel Quartus Prime and ModelSim installation on homer
- [ ] Confirm `/opt/intelFPGA/20.1/modelsim_ase/bin/` contains required tools: `vlog`, `vcom`, `vsim`, etc.
- [ ] Test basic compilation with equivalent of Atari's makepi4.bat
- [ ] Verify we can compile the Atari RTL code on homer

**Task 1.2: Backup and Branch Verification**
- [ ] Verify current branch is `feature/read_write_mem_64_128_256_queue_mp_FC_ENHANCE`
- [ ] Create backup of current working state if needed
- [ ] Document current functionality to ensure no regressions

### Phase 2: Atari Code Analysis and Import

**Task 2.1: Analyze Atari Threading Approach**
- [ ] Note that Atari defines `cpu2()` and `cpu3()` functions but they are commented out in execution
- [ ] Understand that Atari uses a single CPU thread approach similar to our implementation
- [ ] Identify that Atari's main difference is in FC handling and caching, not threading model

**Task 2.2: Import Atari CPU Affinity Functions**
- [ ] Import the `cpu2()` and `cpu3()` functions from Atari's emulator.c:
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
- [ ] Add these functions to our `src/emulator.c`
- [ ] Add function declarations to `src/emulator.h`
- [ ] Determine if we should use these functions or stick with our current environment-based approach

### Phase 3: Enhanced CPLD/FPGA Code with FC Support

**Task 3.1: Create Enhanced Amiga CPLD with FC Support**
- [ ] Create `amiga_enhanced_fc_pistorm.v` based on our current implementation
- [ ] Add FC storage register: `reg [2:0] op_fc = 3'b111;`
- [ ] Modify address register handling to extract FC from data when REG_ADDR_HI is written:
  ```verilog
  REG_ADDR_HI: begin
    op_rw <= PI_D[9];
    op_uds_n <= PI_D[8] ? a0 : 1'b0;
    op_lds_n <= PI_D[8] ? !a0 : 1'b0;
    op_fc <= PI_D[15:13];  // Extract FC from upper bits
  end
  ```
- [ ] Change FC output to conditional assignment: `assign M68K_FC = M68K_BGACK_n ? op_fc : 3'bzzz;`
- [ ] Update initial block to initialize FC appropriately

**Task 3.2: RTL Compilation Setup**
- [ ] Adapt `pistorm-atari/rtl/makepi4.bat` for use on homer
- [ ] Create Quartus project files for enhanced CPLD code
- [ ] Test compilation with Intel Quartus tools
- [ ] Generate programming files for CPLD/FPGA

### Phase 4: Kernel Module Enhancement

**Task 4.1: IOCTL Interface Enhancement**
- [ ] Add new IOCTL commands for FC management:
  - `PISTORM_IOC_SET_FC _IOW(PISTORM_IOC_MAGIC, 0x15, uint8_t)`
  - `PISTORM_IOC_GET_FC _IOR(PISTORM_IOC_MAGIC, 0x16, uint8_t)`
- [ ] Add FC state variable to `pistorm_dev` structure

**Task 4.2: FC Register Write Functionality**
- [ ] Implement FC register write function in kernel module
- [ ] Update address operations to include FC bits in address high register
- [ ] Ensure FC is properly set before bus operations

**Task 4.3: Address Operation Enhancement**
- [ ] Modify address high register writes to include FC bits: `(fc << 13) | (address >> 16)`
- [ ] Add validation for FC values
- [ ] Update GPIO register access patterns to match Atari efficiency

### Phase 5: Userspace Protocol Enhancement

**Task 5.1: FC Tracking Implementation**
- [ ] Update `ps_protocol.h` to properly implement FC function
- [ ] Modify address formation to include FC bits
- [ ] Implement proper function code assignment based on operation type

**Task 5.2: Integration with Emulator FC Management**
- [ ] Verify integration with existing `emulator_fc.c` and `emulator_fc.h`
- [ ] Test FC mode management (OFF, STUB, CPLD)
- [ ] Ensure current FC tracking works properly

### Phase 6: Memory-Mapped Code Integration

**Task 6.1: Check Atari's memory_mapped.c**
- [ ] Compare Atari's memory_mapped.c with our current implementation
- [ ] Identify any FC-aware code in Atari's version
- [ ] Integrate any FC enhancements if not already present in our code

### Phase 7: Musashi and Emulator Integration

**Task 7.1: Musashi FC Handling**
- [ ] Compare Atari's `m68kcpu.c`, `m68k_in.c` with current src/musashi/
- [ ] Identify FC handling enhancements from Atari implementation
- [ ] Apply necessary changes to maintain compatibility

**Task 7.2: Emulator Integration**
- [ ] Update emulator to properly set FC values based on operation type
- [ ] Test with various operation types (supervisor/user, program/data)
- [ ] Verify bus cycle behavior with FC lines

### Phase 8: Testing and Validation

**Task 8.1: Unit Testing**
- [ ] Test FC line functionality with various operation types
- [ ] Verify proper bus cycle behavior
- [ ] Validate compatibility with existing code

**Task 8.2: Integration Testing**
- [ ] Test full system with enhanced FC support
- [ ] Verify no regressions in existing functionality
- [ ] Performance testing to ensure improvements

**Task 8.3: Regression Testing**
- [ ] Run existing test suite to ensure no functionality broken
- [ ] Verify all existing features still work correctly
- [ ] Document any performance improvements

### Phase 9: Documentation and Cleanup

**Task 9.1: Documentation Updates**
- [ ] Update README with new FC functionality
- [ ] Document new IOCTL commands and usage
- [ ] Update any relevant technical documentation

**Task 9.2: Code Cleanup**
- [ ] Remove any debug code or temporary implementations
- [ ] Ensure code follows project conventions
- [ ] Verify all changes are properly committed

## Prerequisites
- [ ] Access to homer with Intel Quartus Prime and ModelSim installed
- [ ] Valid license for Intel FPGA tools
- [ ] Current codebase on `feature/read_write_mem_64_128_256_queue_mp_FC_ENHANCE` branch
- [ ] Backup of current working state

## Success Criteria
- [ ] FC lines properly implemented in CPLD/FPGA code
- [ ] Kernel module supports FC management
- [ ] No regressions in existing functionality
- [ ] Performance improvements validated
- [ ] All tests passing
- [ ] Proper integration with emulator FC management

## Risk Mitigation
- All changes made on dedicated feature branch
- Regular backups and commits
- Thorough testing at each phase
- Rollback plan if needed