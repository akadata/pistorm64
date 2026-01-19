# Performance Benchmarks for PiStorm64

## Overview
This document captures the performance benchmarks achieved with the PiStorm64 emulator using various optimization levels.

## Latest Performance Results
Using `-Ofast` optimization level:

- **Dhrystone performance**: 63,861 Dhrystone MIPS
- **MIPS rating**: 66.66 MIPS
- **Floating-point performance**: 29.28 MFlops
- **Performance compared to A600**: 1.33x faster
- **Performance compared to A4000**: 3.50x faster
- **Overall speedup vs A600**: 120.72x

## Optimization Levels
The Makefile defaults to `-Ofast` optimization which provides aggressive optimizations including:
- Loop unrolling
- Function inlining
- Vectorization
- Floating-point optimizations that may violate IEEE compliance for performance

## Building with Performance Optimizations
The default build uses `-Ofast`:
```bash
make PLATFORM=PI4_64BIT
```

To override the optimization level:
```bash
make PLATFORM=PI4_64BIT OPT_LEVEL=-O3
```

## Platform-Specific Notes
- The PI4_64BIT_DEBUG platform uses `-O0` for debugging purposes
- All production builds should use the default `-Ofast` for optimal performance
- Performance may vary depending on the specific Raspberry Pi model and clock speeds

## Historical Context
These performance figures represent a significant achievement for the PiStorm64 project, demonstrating that modern ARM processors combined with optimized emulation can dramatically outperform classic Amiga hardware while maintaining compatibility.

## Contributing Factors to High Performance
1. Aggressive compiler optimizations (`-Ofast`)
2. Efficient memory mapping techniques
3. Optimized bus cycle handling
4. Direct memory access optimizations
5. Proper cache utilization