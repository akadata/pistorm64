// SPDX-License-Identifier: MIT
#ifndef ARMACCEL_ABI_TYPES_V1_H
#define ARMACCEL_ABI_TYPES_V1_H

#include <stdint.h>

/*
 * Core ABI numeric conventions (v1):
 * - coordinates: signed 32-bit pixel units
 * - sizes/strides/offsets: unsigned 32-bit units
 * - file/tick 64-bit values: split hi/lo u32, hi first
 * - core ABI uses integer arithmetic (no fixed-point requirement)
 */

#define ARMACCEL_COORD_UNIT_PIXELS 1u
#define ARMACCEL_SIZE_UNIT_PIXELS  1u
#define ARMACCEL_STRIDE_UNIT_BYTES 1u
#define ARMACCEL_TIME_UNIT_MS      1u
#define ARMACCEL_TIME_UNIT_TICKS   2u

#define ARMACCEL_COORD_FORMAT_S32  1u
#define ARMACCEL_SIZE_FORMAT_U32   1u

#define ARMACCEL_CORE_FIXEDPOINT_NONE 1u

/* 64-bit split/merge helpers for wire fields. */
#define ARMACCEL_U64_HI(v) ((uint32_t)((((uint64_t)(v)) >> 32u) & 0xFFFFFFFFu))
#define ARMACCEL_U64_LO(v) ((uint32_t)(((uint64_t)(v)) & 0xFFFFFFFFu))
#define ARMACCEL_U64_MAKE(hi, lo) ((((uint64_t)(hi)) << 32u) | ((uint64_t)(lo)))

struct armaccel_point_v1 {
  int32_t x;
  int32_t y;
};

struct armaccel_size_v1 {
  uint32_t w;
  uint32_t h;
};

struct armaccel_rect_v1 {
  int32_t x;
  int32_t y;
  uint32_t w;
  uint32_t h;
};

struct armaccel_u64_split_v1 {
  uint32_t hi;
  uint32_t lo;
};

#endif
