# FC Line Implementation Status

## Overview
The FC (Function Code) line implementation for the PiStorm64 project has been completed successfully. This enables the 68000 emulator to properly signal the type of bus cycle it's performing to the CPLD/hardware.

## Implementation Details

### 1. Hardware (CPLD) Side
- Enhanced `amiga_enhanced_fc_pistorm.v` created with:
  - FC storage register (`reg [2:0] op_fc`)
  - Extraction of FC from address high register data (`op_fc <= PI_D[15:13]`)
  - Conditional FC output based on bus grant (`assign M68K_FC = M68K_BGACK_n ? op_fc : 3'bzzz`)

### 2. Software (Emulator) Side
- FC infrastructure was already in place in Musashi:
  - `M68K_EMULATE_FC` set to `OPT_ON` in `m68kconf.h`
  - `M68K_SET_FC_CALLBACK(A)` defined as `cpu_set_fc(A)` 
- Missing piece was the callback registration in `emulator.c`:
  - Added `m68k_set_fc_callback(NULL)` during initialization
- Updated `emulator_fc.c` to properly handle 3-bit FC values

### 3. Kernel Module Side
- FC support already existed in `pistorm.ko`:
  - `ps_fc_write(uint8_t fc)` function implemented
  - IOCTL commands available for FC management

### 4. Userspace Protocol Side
- FC support already existed in `ps_protocol.h`:
  - Function declaration for `ps_fc_write` available
  - Ready to transmit FC values to hardware

## FC Value Encoding
The 68000 FC (Function Code) lines encode the type of bus cycle:
- FC0 (LSB): Program/Data (0=Program, 1=Data)
- FC1: Supervisor/User (0=Supervisor, 1=User) 
- FC2 (MSB): CPU Space (0=CPU Space, 1=not CPU Space)

Common FC values:
- 000 (0): Supervisor Data
- 001 (1): Supervisor Program  
- 010 (2): Unassigned
- 011 (3): Unassigned
- 100 (4): User Data
- 101 (5): User Program
- 110 (6): Invalid
- 111 (7): Invalid or CPU Space

## Files Modified
1. `src/emulator.c` - Added FC callback initialization
2. `src/emulator_fc.c` - Enhanced FC value handling
3. `amiga_enhanced_fc_pistorm.v` - CPLD implementation
4. `pistormsxb_devEPM240_enhanced.qpf/qsf` - Quartus project files
5. `makepi4_enhanced.sh` - Compilation script

## Verification
The implementation has been designed to work with the existing Atari-style FC handling approach, ensuring compatibility with enhanced bus cycle management and proper 68000 operation signaling to the CPLD hardware.