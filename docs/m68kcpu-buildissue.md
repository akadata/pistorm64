# m68kcpu Build Issue Analysis

## Problem Description

The build is failing with the following error:
```
/usr/bin/ld: m68kcpu.o: in function `m68k_pulse_reset':
m68kcpu.c:(.text+0x2c48): undefined reference to `m68ki_ic_clear'
/usr/bin/ld: m68kops.o: in function `m68k_op_movec_32_rc':
m68kops.c:(.text+0x28ccc): undefined reference to `m68ki_ic_clear'
collect2: error: ld returned 1 exit status
```

## Root Cause Analysis

1. **Function Declaration vs Definition Issue**: The function `m68ki_ic_clear` is declared in `m68kcpu.h` as an inline function:
   ```c
   // clear the instruction cache
   inline void m68ki_ic_clear(m68ki_cpu_core *state)
   {
       int i;
       for (i=0; i< M68K_IC_SIZE; i++) {
           state->ic_address[i] = ~0;
       }
   }
   ```

2. **Linker Issue**: When the function is declared as `inline`, the compiler may not generate an actual function definition in the object file, causing the linker to fail when other object files (like `m68kcpu.o` and `m68kops.o`) reference it.

3. **Cross-File References**: The function is called from:
   - `m68kcpu.c` in the `m68k_pulse_reset` function (line 1179)
   - `m68k_in.c` in the `m68k_op_movec_32_rc` function (line 7043), which gets generated into `m68kops.c`

4. **Architecture/Compiler Issue**: On 64-bit ARM architecture (aarch64), the inline function handling might be different than expected, causing the function definition to not be available during linking.

## Technical Details

- The function `m68ki_ic_clear` is defined as an inline function in the header file `m68kcpu.h`
- This function is called from multiple source files after code generation
- The compiler should make the function available for linking, but it's not happening properly
- This is likely a combination of inline semantics and the build system not properly handling the function visibility

## Potential Solutions (without code changes)

1. **Modify Makefile to adjust compiler flags**: Add flags that ensure inline functions are properly handled during linking
2. **Compiler flags**: Use different optimization or inlining flags to ensure the function is available for linking
3. **Architecture-specific fixes**: Adjust build flags for aarch64 architecture

## Build Environment Issues

Additional build issues encountered:
- GCC flags `-mfloat-abi=hard` and `-mfpu=neon-fp-armv8` are not valid for aarch64 architecture
- The build system is using ARM-specific flags on a 64-bit ARM system (aarch64)

## Impact

This issue prevents the emulator from being built on 64-bit ARM systems, which includes the Raspberry Pi Zero W2 that is being used for this project.