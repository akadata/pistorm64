
#pragma once
#define SUPPORT_THREADS

#include <limits.h>
#ifdef PATH_MAX
#define MAX_DPATH PATH_MAX
#else
#define MAX_DPATH 4096
#endif

// Essential JIT support
#if !defined (CPU_AMD64) && !defined (__x86_64__) && !defined (__MACH__)
#define JIT /* JIT compiler support */
#endif
#if defined(ARMV6T2) || defined(CPU_AARCH64)
#define USE_JIT_FPU
#endif
#define NATMEM_OFFSET regs.natmem_offset

// Essential CPU/FPU/PPC/MMU support
#define FPUEMU /* FPU emulation */
#define MMUEMU /* MMU emulation */
#define CPUEMU_0 /* generic 680x0 emulation */
#define CPUEMU_11 /* 68000/68010 prefetch emulation */
#define CPUEMU_13 /* 68000/68010 cycle-exact cpu&blitter */
#define CPUEMU_40 /* generic 680x0 with JIT direct memory access */
#define WITH_PPC /* PPC support */

#define UAE_RAND_MAX RAND_MAX

#include <stdint.h>

#if defined(__x86_64__) || defined(CPU_AARCH64) || defined(CPU_AMD64)
#define SIZEOF_VOID_P 8
#else
#define SIZEOF_VOID_P 4
#endif

typedef int32_t uae_atomic;

// Basic system capabilities
#define STDC_HEADERS 1
#define TIME_WITH_SYS_TIME 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SELECT 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_MEMCPY 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_SIGACTION 1
#define HAVE_ISNAN
#define HAVE_ISINF

// Basic type definitions
#define SIZEOF_CHAR 1
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#if defined(__x86_64__) || defined(CPU_AARCH64) || defined(CPU_AMD64)
#define SIZEOF_LONG 8
#else
#define SIZEOF_LONG 4
#endif
#define SIZEOF_LONG_LONG 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define SIZEOF___INT64 8

#define RETSIGTYPE void

// Basic types and macros
#ifndef __cdecl
#define __cdecl
#endif

#ifdef UAE4ALL_NO_USE_RESTRICT
#define _GCCRES_
#else
#define _GCCRES_ __restrict__
#endif

#define M68K_SPEED_7MHZ_CYCLES 0
#define M68K_SPEED_14MHZ_CYCLES 1024
#define M68K_SPEED_25MHZ_CYCLES 128

// TCHAR support for UAE
#ifndef UAE_TYPES_H
#define _T(x)               x
typedef char TCHAR;
#endif

// Basic boolean types
typedef unsigned char boolean;
#define FALSE 0U
#define TRUE 1U
