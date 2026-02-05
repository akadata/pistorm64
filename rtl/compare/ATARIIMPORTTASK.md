# ATARI IMPORT TASK LIST - THREADING ENHANCEMENTS

## Overview
This document outlines the tasks required to import Atari's multi-core threading approach and caching system into our current implementation.

## Task 1: Multi-Core CPU Affinity Functions

### 1.1 Import Atari's CPU Affinity Functions
- [ ] Copy the `cpu2()` and `cpu3()` functions from Atari's emulator.c:
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

### 1.2 Update Threading Implementation
- [ ] Identify where our current emulator creates CPU threads
- [ ] Apply cpu2() or cpu3() to appropriate threads for better core isolation
- [ ] Consider using cpu2() for main CPU emulation thread
- [ ] Consider using cpu3() for IPL/interrupt handling thread

## Task 2: Caching System Integration

### 2.1 Analyze Atari's Caching Implementation
- [ ] Locate Atari's caching code in emulator.c (MYWTC related code)
- [ ] Understand the caching mechanism and how it differs from our batch system
- [ ] Identify the `do_cache()` function and related infrastructure
- [ ] Compare with our current `PISTORM_ENABLE_BATCH` system

### 2.2 Import Relevant Caching Code
- [ ] Import caching functions from Atari implementation if beneficial
- [ ] Determine if Atari's caching approach offers advantages over our batch system
- [ ] Consider hybrid approach combining both systems if beneficial

## Task 3: Memory-Mapped Code Integration

### 3.1 Analyze Atari's memory_mapped.c
- [ ] Check if Atari's memory_mapped.c contains FC-aware code
- [ ] Compare with our current memory_mapped.c implementation
- [ ] Identify any FC-related enhancements in Atari's version
- [ ] Look for differences in how memory operations handle function codes

### 3.2 Merge FC-Aware Memory Code
- [ ] Integrate any FC-aware memory handling from Atari if not already present
- [ ] Ensure compatibility with our current FC system
- [ ] Test memory operations with FC enhancements

## Task 4: Integration and Testing

### 4.1 Code Integration
- [ ] Add necessary includes for threading functions
- [ ] Update Makefile if needed for new functionality
- [ ] Ensure all function calls are properly linked

### 4.2 Testing
- [ ] Test multi-core threading functionality
- [ ] Verify no performance regressions
- [ ] Test with various Amiga configurations
- [ ] Validate FC functionality remains intact

## Task 5: Documentation and Cleanup

### 5.1 Update Documentation
- [ ] Document new threading approach
- [ ] Update any relevant technical documentation
- [ ] Add comments explaining multi-core usage

### 5.2 Code Cleanup
- [ ] Remove any debug code or temporary implementations
- [ ] Ensure code follows project conventions
- [ ] Verify all changes are properly committed

## Prerequisites
- [ ] Current codebase on feature branch
- [ ] Backup of working state
- [ ] Understanding of current threading model

## Success Criteria
- [ ] Multi-core CPU affinity functions successfully imported
- [ ] Threading model improved with proper core assignment
- [ ] Caching system evaluated and integrated if beneficial
- [ ] Memory-mapped code updated with any FC enhancements
- [ ] No regressions in existing functionality
- [ ] Performance improvements validated

## Risk Mitigation
- All changes made on dedicated feature branch
- Thorough testing at each step
- Rollback plan if needed
- Performance benchmarking before/after