# FC Enhancement Task List for PiStorm64

## Project Branch
- **Current Branch**: `feature/read_write_mem_64_128_256_queue_mp_FC_ENHANCE`
- **Safety Note**: All changes will be made on this dedicated branch to avoid breaking main code

## Task List

### Phase 1: Preparation and Environment Setup

**Task 1.1: Environment Verification**
- [x] Verify Intel Quartus Prime and ModelSim installation on homer (SSH connection confirmed)
- [x] Confirm `/opt/intelFPGA/20.1/modelsim_ase/bin/` contains required tools (vlog, vcom, vsim, etc. verified)
- [x] Examine `pistorm-atari/rtl/makepi4.bat` for reference (available and reviewed)
- [x] Create Linux equivalent `makepi4_enhanced.sh` for FC-enhanced CPLD compilation (created: makepi4_enhanced.sh)

**Task 1.2: Backup and Branch Verification**
- [x] Verify current branch is `feature/read_write_mem_64_128_256_queue_mp_FC_ENHANCE` (verified: feature/read_write_mem_64_128_256_queue_mp_FC_ENHANCE)
- [x] Create backup of current working state if needed (completed: git status clean)
- [x] Document current functionality to ensure no regressions (baseline established)

### Phase 2: CPLD/FPGA Code Enhancement

**Task 2.1: Enhanced Amiga CPLD Implementation**
- [x] Create `amiga_enhanced_fc_pistorm.v` based on analysis (completed: amiga_enhanced_fc_pistorm.v)
- [x] Implement FC storage register (`reg [2:0] op_fc`)
- [x] Modify address register handling to extract FC from data: `op_fc <= PI_D[15:13];`
- [x] Change FC output to conditional assignment: `assign M68K_FC = M68K_BGACK_n ? op_fc : 3'bzzz;`
- [x] Update initial block to initialize FC appropriately

**Task 2.2: RTL Compilation Setup**
- [x] Adapt `pistorm-atari/rtl/makepi4.bat` for use on homer (completed: makepi4_enhanced.sh)
- [x] Create Quartus project files for enhanced CPLD code (completed: pistormsxb_devEPM240_enhanced.qpf/qsf)
- [x] Test compilation with Intel Quartus tools (completed: script corrected and tested)
- [x] Generate programming files for CPLD/FPGA (completed: via Quartus compilation)

### Phase 3: Kernel Module Enhancement

**Task 3.1: IOCTL Interface Enhancement**
- [x] Add new IOCTL commands for FC management:
  - `PISTORM_IOC_SET_FC _IOW(PISTORM_IOC_MAGIC, 0x15, uint8_t)`
  - `PISTORM_IOC_GET_FC _IOR(PISTORM_IOC_MAGIC, 0x16, uint8_t)`
- [x] Add FC state variable to `pistorm_dev` structure

**Task 3.2: FC Register Write Functionality**
- [x] Implement FC register write function in kernel module
- [x] Update address operations to include FC bits in address high register
- [x] Ensure FC is properly set before bus operations

**Task 3.3: Address Operation Enhancement**
- [x] Modify address high register writes to include FC bits: `(fc << 13) | (address >> 16)`
- [x] Add validation for FC values
- [x] Update GPIO register access patterns to match Atari efficiency

### Phase 4: Userspace Protocol Enhancement

**Task 4.1: FC Tracking Implementation**
- [x] Update `ps_protocol.h` to properly implement FC function
- [x] Modify address formation to include FC bits
- [x] Implement proper function code assignment based on operation type

**Task 4.2: Integration with Emulator FC Management**
- [x] Verify integration with existing `emulator_fc.c` and `emulator_fc.h`
- [x] Test FC mode management (OFF, STUB, CPLD)
- [x] Ensure current FC tracking works properly

### Phase 5: Musashi and Emulator Integration

**Task 5.1: Musashi FC Handling**
- [x] Compare Atari's `m68kcpu.c`, `m68k_in.c` with current src/musashi/
- [x] Identify FC handling enhancements from Atari implementation
- [x] Found FC infrastructure already in place (M68K_EMULATE_FC=OPT_ON, M68K_SET_FC_CALLBACK defined)
- [x] Applied activation by adding m68k_set_fc_callback() call in emulator initialization
- [x] Updated M68K_EMULATE_FC to OPT_SPECIFY_HANDLER to match Atari approach
- [x] Enhanced emulator_fc.c with SFC/DFC register handling functions

**Task 5.2: Emulator Integration**
- [x] Update emulator to properly set FC values based on operation type (added m68k_set_fc_callback to emulator.c)
- [x] Test with various operation types (supervisor/user, program/data)
- [x] Verify bus cycle behavior with FC lines
- [x] Updated emulator_fc.c to properly handle 3-bit FC values

### Phase 6: Testing and Validation

**Task 6.1: Unit Testing**
- [x] Test FC line functionality with various operation types
- [x] Verify proper bus cycle behavior
- [x] Validate compatibility with existing code

**Task 6.2: Integration Testing**
- [x] Test full system with enhanced FC support
- [x] Verify no regressions in existing functionality
- [x] Performance testing to ensure improvements

**Task 6.3: Regression Testing**
- [x] Run existing test suite to ensure no functionality broken
- [x] Verify all existing features still work correctly
- [x] Document any performance improvements

### Phase 7: Documentation and Cleanup

**Task 7.1: Documentation Updates**
- [x] Update README with new FC functionality
- [x] Document new IOCTL commands and usage
- [x] Update any relevant technical documentation

**Task 7.2: Code Cleanup**
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

## Risk Mitigation
- All changes made on dedicated feature branch
- Regular backups and commits
- Thorough testing at each phase
- Rollback plan if needed

## Completed Deliverables
- [x] Enhanced CPLD code: `amiga_enhanced_fc_pistorm.v`
- [x] Quartus project files: `pistormsxb_devEPM240_enhanced.qpf/qsf`
- [x] Compilation script: `makepi4_enhanced.sh`
- [x] Files transferred to homer for compilation
- [x] Updated kernel module with FC support
- [x] Updated userspace protocol with FC handling
- [x] Updated emulator integration with FC management
- [x] Enhanced Musashi FC configuration (OPT_ON with callback wrapper)
- [x] Enhanced emulator_fc.c/h with SFC/DFC register handling
- [x] Resolved compilation issues with FC callback wrapper function
- [x] Complete testing and validation
- [x] Documentation and cleanup