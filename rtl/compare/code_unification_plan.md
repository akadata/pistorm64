# Code Unification Task List: Amiga and Atari Implementations

## Overview
This document outlines the tasks required to unify the Amiga and Atari PiStorm implementations, focusing on FC (Function Code) enhancements and other shared functionality.

## Key Differences Identified

### 1. FC Implementation Differences

#### Atari Implementation
- Direct assignment in main loop: `fc = 6;` (during memory detection)
- Direct assignment during interrupt acknowledge: `fc = 0x7;` 
- Global `uint8_t fc` variable in ps_protocol.c
- FC bits encoded in address high register: `(fc << 13) | (address >> 16)`

#### Current Amiga Implementation
- Uses callback system with `cpu_set_fc()` function
- Wrapper function `fc_callback_wrapper()` to match expected signature
- FC mode management (OFF, STUB, CPLD)
- Shadow tracking for debugging purposes
- Proper integration with kernel module via `ps_fc_write()`

### 2. CPU Affinity and Threading Differences

#### Atari Implementation
- Uses `cpu2()` and `cpu3()` functions for thread affinity
- Direct `sched_setaffinity()` calls in these functions
- Separate IPL task on CPU 3

#### Current Amiga Implementation
- Uses `apply_affinity_from_env()` function for thread affinity
- More flexible configuration via environment variables
- Modern threading approach with pthread attributes

### 3. Cache Management Differences

#### Atari Implementation
- Uses `MYWTC` and `do_cache()` functions
- Custom caching mechanism for Atari memory

#### Current Amiga Implementation
- Uses `PISTORM_ENABLE_BATCH` for operation batching
- Different caching approach

## Unification Strategy

### Phase 1: FC System Unification

**Task 1.1: Update emulator.h**
- [ ] Add `cpu_set_fc()` declaration to src/emulator.h for consistency
- [ ] Ensure function signature matches between implementations

**Task 1.2: Enhance FC Management**
- [ ] Add the interrupt acknowledge FC setting from Atari: `cpu_set_fc(0x7)` during interrupt acknowledge
- [ ] Add memory detection FC setting: `cpu_set_fc(6)` during memory detection
- [ ] Ensure proper FC values are set for different operation types

**Task 1.3: Update Function Code Assignments**
- [ ] Implement proper FC assignment based on operation type:
  - Supervisor Program (001)
  - Supervisor Data (000) 
  - User Program (101)
  - User Data (100)
  - CPU Space (111)

### Phase 2: Threading and Affinity Unification

**Task 2.1: Threading Model Alignment**
- [ ] Evaluate if Atari's cpu2()/cpu3() approach offers benefits
- [ ] Determine if current affinity management is sufficient
- [ ] Consider adopting any performance improvements from Atari approach

### Phase 3: Memory Management Unification

**Task 3.1: Cache Management**
- [ ] Compare caching mechanisms between implementations
- [ ] Determine if Atari's approach offers performance benefits
- [ ] Consider hybrid approach if beneficial

### Phase 4: Common Infrastructure

**Task 4.1: Shared Components**
- [ ] Identify common functionality that can be shared
- [ ] Create common headers for shared definitions
- [ ] Abstract platform-specific differences

**Task 4.2: Conditional Compilation**
- [ ] Implement `#ifdef AMIGA` and `#ifdef ATARI` directives where needed
- [ ] Create unified build system that can target both platforms
- [ ] Maintain separate platform-specific customizations

## Implementation Plan

### Step 1: FC Enhancements (Priority: High)
The FC enhancements from Atari should be integrated into our current implementation since they represent proper 68000 bus cycle behavior:

```c
// During interrupt acknowledge (in cpu_irq_ack function)
cpu_set_fc(0x7); // CPU interrupt acknowledge

// During memory detection (at start of emulation)
cpu_set_fc(6);   // Set appropriate FC during memory detection
```

### Step 2: Platform Abstraction Layer
Create a common framework with platform-specific implementations:

```c
#ifdef PLATFORM_AMIGA
  // Amiga-specific implementations
#elif defined(PLATFORM_ATARI)
  // Atari-specific implementations
#else
  #error "Platform not defined"
#endif
```

### Step 3: Testing and Validation
- [ ] Test FC functionality with both Amiga and Atari systems
- [ ] Verify no performance regressions
- [ ] Ensure compatibility with existing functionality

## Benefits of Unification

1. **Better 68000 Bus Cycle Compliance**: Proper FC setting during interrupt acknowledge and memory operations
2. **Shared Improvements**: Both platforms benefit from enhancements
3. **Reduced Maintenance**: Common codebase reduces duplication
4. **Cross-Platform Learning**: Best practices from both implementations can be shared

## Risks and Mitigation

- **Risk**: Breaking existing functionality
  - **Mitigation**: All changes on feature branch with thorough testing
- **Risk**: Performance degradation
  - **Mitigation**: Benchmark before and after changes
- **Risk**: Platform-specific behavior lost
  - **Mitigation**: Careful abstraction that preserves platform-specific needs

## Conclusion

The Atari implementation contains valuable enhancements, particularly around proper FC line usage during interrupt acknowledge and memory operations. These improvements should be integrated into our current implementation while maintaining our more modern threading and kernel module integration approaches. The unification should focus on sharing the best aspects of both implementations while maintaining platform-specific optimizations.